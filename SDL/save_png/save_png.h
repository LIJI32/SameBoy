#include <stdint.h>
#include <stdbool.h>
#include <SDL.h>

bool save_png(const char *filename, uint32_t width, uint32_t height, const void *pixels, SDL_PixelFormat *pixel_format);
