#ifndef get_image_for_rom_h
#define get_image_for_rom_h
#include <stdint.h>

typedef bool (*cancel_callback_t)(void*);

int get_image_for_rom(const char *filename, uint32_t *output);

#endif
