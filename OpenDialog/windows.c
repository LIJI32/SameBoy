#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include "open_dialog.h"

static char *wc_to_utf8_alloc(const wchar_t *wide)
{
    unsigned int cb = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (cb) {
        char *buffer = (char*) malloc(cb);
        if (buffer) {
            WideCharToMultiByte(CP_UTF8, 0, wide, -1, buffer, cb, NULL, NULL);
            return buffer;
        }
    }
    return NULL;
}

char *do_open_rom_dialog(void)
{
    OPENFILENAMEW dialog;
    wchar_t filename[MAX_PATH];

    filename[0] = '\0';
    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = filename;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrFilter = L"Game Boy ROMs\0*.gb;*.gbc;*.sgb;*.isx;*.zip\0All files\0*.*\0\0";
    dialog.nFilterIndex = 1;
    dialog.lpstrFileTitle = NULL;
    dialog.nMaxFileTitle = 0;
    dialog.lpstrInitialDir = NULL;
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileNameW(&dialog)) {
        return wc_to_utf8_alloc(filename);
    }

    return NULL;
}

char *do_open_folder_dialog(void)
{
    char *ret = NULL;
    BROWSEINFOW dialog;

    memset(&dialog, 0, sizeof(dialog));
    dialog.ulFlags = BIF_USENEWUI | BIF_RETURNONLYFSDIRS;
    dialog.lpszTitle = L"Select Boot ROMs Folder";

    HRESULT hrOleInit = OleInitialize(NULL);
    LPITEMIDLIST list = SHBrowseForFolderW(&dialog);
    if (list) {
        wchar_t filename[MAX_PATH];
        if (SHGetPathFromIDListW(list, filename)) {
            ret = wc_to_utf8_alloc(filename);
        }
        CoTaskMemFree(list);
    }

    if (SUCCEEDED(hrOleInit)) OleUninitialize();
    return ret;
}

char *do_save_recording_dialog(unsigned frequency)
{
    OPENFILENAMEW dialog;
    wchar_t filename[MAX_PATH + 5] = L"recording.wav";
    static wchar_t filter[] = L"RIFF WAVE\0*.wav\0Apple AIFF\0*.aiff;*.aif;*.aifc\0Raw PCM (Stereo _______Hz, 16-bit LE)\0*.raw;*.pcm;\0All files\0*.*\0\0";

    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = filename;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrFilter = filter;
    swprintf(filter + sizeof("RIFF WAVE\0*.wav\0Apple AIFF\0*.aiff;*.aif;*.aifc\0Raw PCM (Stereo ") - 1,
             sizeof("_______Hz, 16-bit LE)"),
             L"%dHz, 16-bit LE)       ",
             frequency);

    dialog.nFilterIndex = 1;
    dialog.lpstrInitialDir = NULL;
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
    
    if (GetSaveFileNameW(&dialog)) {
        if (dialog.nFileExtension == 0) {
            switch (dialog.nFilterIndex) {
                case 1:
                    wcscat(filename, L".wav");
                    break;
                case 2:
                    wcscat(filename, L".aiff");
                    break;
                case 3:
                    wcscat(filename, L".raw");
                    break;
            }
        }
        return wc_to_utf8_alloc(filename);
    }
    
    return NULL;
}
