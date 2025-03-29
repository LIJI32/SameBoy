#include <stdio.h>
#include <unistd.h>

#ifdef USE_MSVCRT_DLL

FILE *__acrt_iob_func(unsigned index)
{
    static FILE *files[3];
    if (index > sizeof(files) / sizeof(files[0])) return NULL;
    if (files[index]) return files[index];
    return (files[index] = fdopen(index, index == STDIN_FILENO? "r" : "w"));
}

#endif

#ifndef __MINGW32__
#ifndef __LIBRETRO__
int vasprintf(char **str, const char *fmt, va_list args)
{
    size_t size = _vscprintf(fmt, args) + 1;
    *str = (char*)malloc(size);
    int ret = vsprintf(*str, fmt, args);
    if (ret != size - 1) {
        free(*str);
        *str = NULL;
        return -1;
    }
    return ret;
}

int asprintf(char **strp, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int r = vasprintf(strp, fmt, args);
    va_end(args);
    return r;
}

#endif
#endif

/* This code is public domain -- Will Hartung 4/9/09 */
intptr_t getline(char **lineptr, size_t *n, FILE *stream)
{
    char *bufptr = NULL;
    char *p = bufptr;
    size_t size;
    int c;
    
    if (lineptr == NULL) {
        return -1;
    }
    if (stream == NULL) {
        return -1;
    }
    if (n == NULL) {
        return -1;
    }
    bufptr = *lineptr;
    size = *n;
    
    errno = 0;
    c = fgetc(stream);
    if (c == EOF) {
        return -1;
    }
    if (bufptr == NULL) {
        bufptr = (char*)malloc(128);
        if (bufptr == NULL) {
            return -1;
        }
        size = 128;
    }
    p = bufptr;
    while (c != EOF) {
        if ((p - bufptr) > (size - 1)) {
            size = size + 128;
            bufptr = (char*)realloc(bufptr, size);
            if (bufptr == NULL) {
                return -1;
            }
        }
        *p++ = c;
        if (c == '\n') {
            break;
        }
        c = fgetc(stream);
    }
    
    *p++ = '\0';
    *lineptr = bufptr;
    *n = size;
    
    return p - bufptr - 1;
}
