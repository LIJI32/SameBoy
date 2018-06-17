#ifndef utils_h
#define utils_h
#include <stddef.h>

const char *executable_folder(void);
char *executable_relative_path(const char *filename);
void replace_extension(const char *src, size_t length, char *dest, const char *ext);

#endif /* utils_h */
