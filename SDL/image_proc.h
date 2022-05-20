#ifndef IMAGE_H_INCLUDED
#define IMAGE_H_INCLUDED
#include <SDL.h>
#include <SDL_endian.h>
#include "../Core/gb.h"
#include <stdlib.h>



#ifdef __linux__
    #include <dirent.h>
    #include <sys/stat.h>
#endif

#ifdef __APPLE__

#endif

#ifdef __TARGET_MAC_OS__

#endif

#ifdef _WIN32


#endif




extern SDL_Surface *surface;
extern char save_bmp_filename[];
extern char *rom_path;
extern char final_image_filename[];
extern char rom_title[];


void generate_BMPimage(GB_gameboy_t *gb, void *current_pixel_buffer, void *secondary_pixel_buffer);

int create_path_to_screenshot_directory(GB_gameboy_t *gb);

void generate_image_filename(GB_gameboy_t *gb);




#endif // IMAGE_H_INCLUDED
