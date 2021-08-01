#ifndef get_image_for_rom_h
#define get_image_for_rom_h
#include <stdint.h>
#include <stdbool.h>

typedef bool (*cancel_callback_t)(void*);

int get_image_for_rom_alt(const char *filename, const unsigned char *buffer, size_t size, uint32_t *output, uint8_t *cgb_flag);
int get_image_for_rom(const char *filename, const char *boot_path, uint32_t *output, uint8_t *cgb_flag);


#endif
