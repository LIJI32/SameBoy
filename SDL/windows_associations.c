#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <stdbool.h>
#include "windows_associations.h"

static bool set_registry_string(HKEY hive, const char *folder, const char *name, const char *value)
{
    HKEY hkey;
    LONG status = RegCreateKeyExA(hive, folder, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hkey, NULL);
    if (status != ERROR_SUCCESS || hkey == NULL) {
        return false;
    }
    status = RegSetValueExA(hkey, name, 0, REG_SZ, (void *)value, strlen(value) + 1);
    RegCloseKey(hkey);
    return status == ERROR_SUCCESS;
}

static bool delete_registry_key(HKEY hive, const char *folder, const char *name)
{
    HKEY hkey;
    LONG status = RegCreateKeyExA(hive, folder, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hkey, NULL);
    if (status != ERROR_SUCCESS || hkey == NULL) {
        return false;
    }
    status = RegDeleteTreeA(hkey, name);
    RegCloseKey(hkey);
    return status == ERROR_SUCCESS;
}

static bool set_registry_string_unicode(HKEY hive, const char *folder, const char *name, const wchar_t *value)
{
    HKEY hkey;
    LONG status = RegCreateKeyExA(hive, folder, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hkey, NULL);
    if (status != ERROR_SUCCESS || hkey == NULL) {
        return false;
    }
    
    wchar_t wide_name[strlen(name) + 1];
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wide_name, sizeof(wide_name) / sizeof(wide_name[0]));
    status = RegSetValueExW(hkey, wide_name, 0, REG_SZ, (void *)value, (wcslen(value) + 1) * 2);
    
    RegCloseKey(hkey);
    return status == ERROR_SUCCESS;
}


static bool associate(const char *extension, const char *class, const char *description, signed icon)
{
    char path[128] = "Software\\Classes\\";
    strcat(path, extension);
    if (!set_registry_string(HKEY_CURRENT_USER, path, "", class)) return false;
    
    strcpy(path, "Software\\Classes\\");
    strcat(path, class);
    if (!set_registry_string(HKEY_CURRENT_USER, path, "", description)) return false;
    
    strcat(path, "\\shell\\open\\command");
    
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    
    wchar_t temp[sizeof(exe) + 32];
    wsprintfW(temp, L"\"\%s\" \"%%1\"", exe);
    if (!set_registry_string_unicode(HKEY_CURRENT_USER, path, "", temp)) return false;
    
    strcpy(path, "Software\\Classes\\");
    strcat(path, class);
    strcat(path, "\\DefaultIcon");
    
    wsprintfW(temp, L"\%s,%d", exe, icon);
    if (!set_registry_string_unicode(HKEY_CURRENT_USER, path, "", temp)) return false;
    
    strcpy(path, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\");
    strcat(path, extension);
    delete_registry_key(HKEY_CURRENT_USER, path, "UserChoice"); // Might not exist, do not check return value
    
    return true;
}

bool GB_do_windows_association(void)
{
    bool ret = true;
    ret &= associate(".gb",  "SameBoy.gb",  "Game Boy Game", 1);
    ret &= associate(".gbc", "SameBoy.gbc", "Game Boy Color Game", 2);
    ret &= associate(".isx", "SameBoy.isx", "Game Boy ISX File", 2);
    
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    
    return ret;
}
#endif
