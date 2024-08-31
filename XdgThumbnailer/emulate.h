#pragma once

#include <stdint.h>

#define GB_SCREEN_WIDTH 160
#define GB_SCREEN_HEIGHT 144

uint8_t emulate(const char *path, uint32_t screen[static GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT]);
