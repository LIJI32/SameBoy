#pragma once

#include "defs.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct GB_sgb_s GB_sgb_t;
typedef struct {
    uint8_t tiles[0x100 * 8 * 4];
#ifdef GB_INTERNAL
    union {
        struct {
            uint16_t map[32 * 32];
            uint16_t palette[16 * 4];
        };
        uint16_t raw_data[0x440];
    };
#else
    uint16_t raw_data[0x440];
#endif
} GB_sgb_border_t;

#ifdef GB_INTERNAL
#define GB_SGB_INTRO_ANIMATION_LENGTH 200

struct GB_sgb_s {
    uint8_t command[16 * 7];
    uint16_t command_write_index;
    bool ready_for_pulse;
    bool ready_for_write;
    bool ready_for_stop;
    bool disable_commands;
    
    /* Screen buffer */
    uint8_t screen_buffer[160 * 144]; // Live image from the Game Boy
    uint8_t effective_screen_buffer[160 * 144]; // Image actually rendered to the screen
    
    /* Multiplayer Input */
    uint8_t player_count, current_player;
    
    /* Mask */
    uint8_t mask_mode;
    
    /* Data Transfer */
    uint8_t vram_transfer_countdown, transfer_dest;
    
    /* Border */
    GB_sgb_border_t border, pending_border;
    uint8_t border_animation;
    
    /* Colorization */
    uint16_t effective_palettes[4 * 4];
    uint16_t ram_palettes[4 * 512];
    uint8_t attribute_map[20 * 18];
    uint8_t attribute_files[0xFD2];
    uint8_t attribute_files_padding[0xFE0 - 0xFD2];
    
    /* Intro */
    int16_t intro_animation;
    
    /* GB Header */
    uint8_t received_header[0x54];
};

internal void GB_sgb_write(GB_gameboy_t *gb, uint8_t value);
internal void GB_sgb_render(GB_gameboy_t *gb);
internal void GB_sgb_load_default_data(GB_gameboy_t *gb);

#endif
unsigned GB_get_player_count(GB_gameboy_t *gb);
