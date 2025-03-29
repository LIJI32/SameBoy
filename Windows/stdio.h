#pragma once

#ifdef noinline
#undef noinline
#include_next <stdio.h>
#define noinline __attribute__((noinline))
#else
#include_next <stdio.h>
#endif

#include <stdlib.h>
#include <stdarg.h>

#if _WIN64
#define fseek(...) _fseeki64(__VA_ARGS__)
#endif

int access(const char *filename, int mode);
#define R_OK 4
#define W_OK 2

#ifndef __MINGW32__
#ifndef __LIBRETRO__
int vasprintf(char **str, const char *fmt, va_list args);
int asprintf(char **strp, const char *fmt, ...);
#endif
#endif

intptr_t getline(char **lineptr, size_t *n, FILE *stream);

#define snprintf _snprintf
#define printf(...) fprintf(stdout, __VA_ARGS__)
