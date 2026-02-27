#include <png.h>
#include <SDL.h>
#include <stdbool.h>
#include "save_png.h"

bool save_png(const char *filename, uint32_t width, uint32_t height, const void *pixels, SDL_PixelFormat *pixel_format)
{
    FILE *f = fopen(filename, "wb");
    if (!f) return false;
    
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png);
    uint8_t *row = malloc(width * 3);
    
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(f);
        free(row);
        return false;
    }
    png_init_io(png, f);
    
    png_set_IHDR(png,
                 info,
                 width,
                 height,
                 8,
                 PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    
    png_write_info(png, info);
    
    for (uint32_t y = 0; y < height; y++) {
        const uint32_t *src = (uint32_t *)pixels + y * width;
        uint8_t *dest = row;
                
        for (uint32_t x = 0; x < width; x++) {
            uint8_t dummy;
            SDL_GetRGBA(*(src++), pixel_format, &dest[0], &dest[1], &dest[2], &dummy);
            dest += 3;
        }
        
        png_write_row(png, row);
    }
    
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    fclose(f);
    
    return true;

}
