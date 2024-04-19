#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include "utils.h"

static const char *resource_folder(void)
{
    static const char *ret = NULL;
    if (!ret) {
        ret = SDL_GetBasePath();
        if (!ret) {
            ret = "./";
        }
    }
    return ret;
}

char *resource_path(const char *filename)
{
    static char path[PATH_MAX + 1];
   
    snprintf(path, sizeof(path), "%s%s", resource_folder(), filename);
#ifdef DATA_DIR
    if (access(path, F_OK) == 0) {
        return path;
    }
    snprintf(path, sizeof(path), "%s%s", DATA_DIR, filename);
#endif
    return path;
}


void replace_extension(const char *src, size_t length, char *dest, const char *ext)
{
    memcpy(dest, src, length);
    dest[length] = 0;

    /* Remove extension */
    for (size_t i = length; i--;) {
        if (dest[i] == '/') break;
        if (dest[i] == '.') {
            dest[i] = 0;
            break;
        }
    }

    /* Add new extension */
    strcat(dest, ext);
}
