#pragma once

#include "gb.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    GB_VBLANK_TYPE_NORMAL_FRAME, // An actual Vblank-triggered frame
    GB_VBLANK_TYPE_LCD_OFF, // An artificial frame pushed while the LCD was off
    GB_VBLANK_TYPE_ARTIFICIAL, // An artificial frame pushed for some other reason
    GB_VBLANK_TYPE_REPEAT, // A frame that would not render on actual hardware, but the screen should retain the previous frame
} GB_vblank_type_t;

#ifdef GB_INTERNAL
internal void GB_display_run(GB_gameboy_t *gb, unsigned cycles, bool force);
internal void GB_palette_changed(GB_gameboy_t *gb, bool background_palette, uint8_t index);
internal void GB_STAT_update(GB_gameboy_t *gb);
internal void GB_lcd_off(GB_gameboy_t *gb);
internal void GB_display_vblank(GB_gameboy_t *gb, GB_vblank_type_t type);
internal void GB_update_wx_glitch(GB_gameboy_t *gb);
#define GB_display_sync(gb) GB_display_run(gb, 0, true)

enum {
  GB_OBJECT_PRIORITY_X,
  GB_OBJECT_PRIORITY_INDEX,
};

#endif

typedef enum {
    GB_PALETTE_NONE,
    GB_PALETTE_BACKGROUND,
    GB_PALETTE_OAM,
    GB_PALETTE_AUTO,
} GB_palette_type_t;

typedef enum {
    GB_MAP_AUTO,
    GB_MAP_9800,
    GB_MAP_9C00,
} GB_map_type_t;

typedef enum {
    GB_TILESET_AUTO,
    GB_TILESET_8800,
    GB_TILESET_8000,
} GB_tileset_type_t;

typedef struct {
    uint32_t image[128];
    uint8_t x, y, tile, flags;
    uint16_t oam_addr;
    bool obscured_by_line_limit;
} GB_oam_info_t;

typedef enum {
    GB_COLOR_CORRECTION_DISABLED,
    GB_COLOR_CORRECTION_CORRECT_CURVES,
    GB_COLOR_CORRECTION_MODERN_BALANCED,
    GB_COLOR_CORRECTION_MODERN_BOOST_CONTRAST,
    GB_COLOR_CORRECTION_REDUCE_CONTRAST,
    GB_COLOR_CORRECTION_LOW_CONTRAST,
    GB_COLOR_CORRECTION_MODERN_ACCURATE,
} GB_color_correction_mode_t;

static const GB_color_correction_mode_t __attribute__((deprecated("Use GB_COLOR_CORRECTION_MODERN_BALANCED instead"))) GB_COLOR_CORRECTION_EMULATE_HARDWARE = GB_COLOR_CORRECTION_MODERN_BALANCED;
static const GB_color_correction_mode_t __attribute__((deprecated("Use GB_COLOR_CORRECTION_MODERN_BOOST_CONTRAST instead"))) GB_COLOR_CORRECTION_PRESERVE_BRIGHTNESS = GB_COLOR_CORRECTION_MODERN_BOOST_CONTRAST;

void GB_draw_tileset(GB_gameboy_t *gb, uint32_t *dest, GB_palette_type_t palette_type, uint8_t palette_index);
void GB_draw_tilemap(GB_gameboy_t *gb, uint32_t *dest, GB_palette_type_t palette_type, uint8_t palette_index, GB_map_type_t map_type, GB_tileset_type_t tileset_type);
uint8_t GB_get_oam_info(GB_gameboy_t *gb, GB_oam_info_t *dest, uint8_t *object_height);
uint32_t GB_convert_rgb15(GB_gameboy_t *gb, uint16_t color, bool for_border);
void GB_set_color_correction_mode(GB_gameboy_t *gb, GB_color_correction_mode_t mode);
void GB_set_light_temperature(GB_gameboy_t *gb, double temperature);
bool GB_is_odd_frame(GB_gameboy_t *gb);

void GB_set_object_rendering_disabled(GB_gameboy_t *gb, bool disabled);
void GB_set_background_rendering_disabled(GB_gameboy_t *gb, bool disabled);
bool GB_is_object_rendering_disabled(GB_gameboy_t *gb);
bool GB_is_background_rendering_disabled(GB_gameboy_t *gb);
