#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include "gb.h"

const GB_palette_t GB_PALETTE_GREY = {{{0x00, 0x00, 0x00}, {0x55, 0x55, 0x55}, {0xAA, 0xAA, 0xAA}, {0xFF, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF}}};
const GB_palette_t GB_PALETTE_DMG  = {{{0x08, 0x18, 0x10}, {0x39, 0x61, 0x39}, {0x84, 0xA5, 0x63}, {0xC6, 0xDE, 0x8C}, {0xD2, 0xE6, 0xA6}}};
const GB_palette_t GB_PALETTE_MGB  = {{{0x07, 0x10, 0x0E}, {0x3A, 0x4C, 0x3A}, {0x81, 0x8D, 0x66}, {0xC2, 0xCE, 0x93}, {0xCF, 0xDA, 0xAC}}};
const GB_palette_t GB_PALETTE_GBL  = {{{0x0A, 0x1C, 0x15}, {0x35, 0x78, 0x62}, {0x56, 0xB4, 0x95}, {0x7F, 0xE2, 0xC3}, {0x91, 0xEA, 0xD0}}};

void GB_update_dmg_palette(GB_gameboy_t *gb)
{
    const GB_palette_t *palette = gb->dmg_palette ?: &GB_PALETTE_GREY;
    if (gb->rgb_encode_callback && !GB_is_cgb(gb)) {
        gb->object_palettes_rgb[4] = gb->object_palettes_rgb[0] = gb->background_palettes_rgb[0] =
        gb->rgb_encode_callback(gb, palette->colors[3].r, palette->colors[3].g, palette->colors[3].b);
        gb->object_palettes_rgb[5] = gb->object_palettes_rgb[1] = gb->background_palettes_rgb[1] =
        gb->rgb_encode_callback(gb, palette->colors[2].r, palette->colors[2].g, palette->colors[2].b);
        gb->object_palettes_rgb[6] = gb->object_palettes_rgb[2] = gb->background_palettes_rgb[2] =
        gb->rgb_encode_callback(gb, palette->colors[1].r, palette->colors[1].g, palette->colors[1].b);
        gb->object_palettes_rgb[7] = gb->object_palettes_rgb[3] = gb->background_palettes_rgb[3] =
        gb->rgb_encode_callback(gb, palette->colors[0].r, palette->colors[0].g, palette->colors[0].b);
        
        // LCD off color
        gb->background_palettes_rgb[4] =
        gb->rgb_encode_callback(gb, palette->colors[4].r, palette->colors[4].g, palette->colors[4].b);
    }
}

void GB_set_palette(GB_gameboy_t *gb, const GB_palette_t *palette)
{
    gb->dmg_palette = palette;
    GB_update_dmg_palette(gb);
}

const GB_palette_t *GB_get_palette(GB_gameboy_t *gb)
{
    return gb->dmg_palette;
}

void GB_set_vblank_callback(GB_gameboy_t *gb, GB_vblank_callback_t callback)
{
    if (!callback) {
        GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    }
    gb->vblank_callback = callback;
}

void GB_set_rgb_encode_callback(GB_gameboy_t *gb, GB_rgb_encode_callback_t callback)
{
    if (!callback) {
        GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    }
    
    gb->rgb_encode_callback = callback;
    GB_update_dmg_palette(gb);
    
    for (unsigned i = 0; i < 32; i++) {
        GB_palette_changed(gb, true, i * 2);
        GB_palette_changed(gb, false, i * 2);
    }
}

void GB_set_pixels_output(GB_gameboy_t *gb, uint32_t *output)
{
    GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    gb->screen = output;
}

/* FIFO functions */

static inline unsigned fifo_size(GB_fifo_t *fifo)
{
    return fifo->size;
}

static void fifo_clear(GB_fifo_t *fifo)
{
    fifo->read_end = fifo->size = 0;
}

static const GB_fifo_item_t *fifo_pop(GB_fifo_t *fifo)
{
    assert(fifo->size);
    assert(fifo->size <= 8);
    GB_fifo_item_t *ret = &fifo->fifo[fifo->read_end];
    fifo->read_end++;
    fifo->read_end &= (GB_FIFO_LENGTH - 1);
    fifo->size--;
    return ret;
}

static void fifo_push_bg_row(GB_fifo_t *fifo, uint8_t lower, uint8_t upper, uint8_t palette, bool bg_priority, bool flip_x)
{
    assert(fifo->size == 0);
    fifo->size = 8;
    if (!flip_x) {
        unrolled for (unsigned i = 0; i < 8; i++) {
            fifo->fifo[i] = (GB_fifo_item_t) {
                (lower >> 7) | ((upper >> 7) << 1),
                palette,
                0,
                bg_priority,
            };
            lower <<= 1;
            upper <<= 1;
        }
    }
    else {
        unrolled for (unsigned i = 0; i < 8; i++) {
            fifo->fifo[i] = (GB_fifo_item_t) {
                (lower & 1) | ((upper & 1) << 1),
                palette,
                0,
                bg_priority,
            };
            lower >>= 1;
            upper >>= 1;
        }
    }
}

static void fifo_overlay_object_row(GB_fifo_t *fifo, uint8_t lower, uint8_t upper, uint8_t palette, bool bg_priority, uint8_t priority, bool flip_x)
{
    while (fifo->size < GB_FIFO_LENGTH) {
        fifo->fifo[(fifo->read_end + fifo->size) & (GB_FIFO_LENGTH - 1)] = (GB_fifo_item_t) {0,};
        fifo->size++;
    }
    
    uint8_t flip_xor = flip_x? 0: 0x7;
    
    unrolled for (unsigned i = 8; i--;) {
        uint8_t pixel = (lower >> 7) | ((upper >> 7) << 1);
        GB_fifo_item_t *target = &fifo->fifo[(fifo->read_end + (i ^ flip_xor)) & (GB_FIFO_LENGTH - 1)];
        if (pixel != 0 && (target->pixel == 0 || target->priority > priority)) {
            target->pixel = pixel;
            target->palette = palette;
            target->bg_priority = bg_priority;
            target->priority = priority;
        }
        lower <<= 1;
        upper <<= 1;
    }
}


/*
 Each line is 456 cycles. Without scrolling, objects or a window:
 Mode 2 - 80  cycles / OAM Transfer
 Mode 3 - 172 cycles / Rendering
 Mode 0 - 204 cycles / HBlank
 
 Mode 1 is VBlank
 */

#define MODE2_LENGTH (80)
#define LINE_LENGTH (456)
#define LINES (144)
#define WIDTH (160)
#define BORDERED_WIDTH 256
#define BORDERED_HEIGHT 224
#define VIRTUAL_LINES (LCDC_PERIOD / LINE_LENGTH) // = 154

typedef struct __attribute__((packed)) {
    uint8_t y;
    uint8_t x;
    uint8_t tile;
    uint8_t flags;
} object_t;

void GB_display_vblank(GB_gameboy_t *gb, GB_vblank_type_t type)
{
    gb->vblank_just_occured = true;
    gb->cycles_since_vblank_callback = 0;
    gb->lcd_disabled_outside_of_vblank = false;
    
    /* TODO: Slow in turbo mode! */
    if (GB_is_hle_sgb(gb)) {
        GB_sgb_render(gb);
    }
    
    if (gb->turbo) {
        if (GB_timing_sync_turbo(gb)) {
            return;
        }
    }
    
    if (GB_is_cgb(gb) && type == GB_VBLANK_TYPE_NORMAL_FRAME && gb->frame_repeat_countdown > 0 && gb->frame_skip_state == GB_FRAMESKIP_LCD_TURNED_ON) {
        GB_handle_rumble(gb);
        
        if (gb->vblank_callback) {
            gb->vblank_callback(gb, GB_VBLANK_TYPE_REPEAT);
        }
        GB_timing_sync(gb);
        return;
    }
    
    bool is_ppu_stopped = !GB_is_cgb(gb) && gb->stopped && gb->io_registers[GB_IO_LCDC] & GB_LCDC_ENABLE;
    
    if (!gb->disable_rendering && ((!(gb->io_registers[GB_IO_LCDC] & GB_LCDC_ENABLE) || is_ppu_stopped) || gb->frame_skip_state == GB_FRAMESKIP_LCD_TURNED_ON)) {
        /* LCD is off, set screen to white or black (if LCD is on in stop mode) */
        if (!GB_is_sgb(gb)) {
            uint32_t color = 0;
            if (GB_is_cgb(gb)) {
                color = GB_convert_rgb15(gb, 0x7FFF, false);
            }
            else {
                color = is_ppu_stopped ?
                            gb->background_palettes_rgb[0] :
                            gb->background_palettes_rgb[4];
            }
            if (gb->border_mode == GB_BORDER_ALWAYS) {
                for (unsigned y = 0; y < LINES; y++) {
                    for (unsigned x = 0; x < WIDTH; x++) {
                        gb ->screen[x + y * BORDERED_WIDTH + (BORDERED_WIDTH - WIDTH) / 2 + (BORDERED_HEIGHT - LINES) / 2 * BORDERED_WIDTH] = color;
                    }
                }
            }
            else {
                for (unsigned i = 0; i < WIDTH * LINES; i++) {
                    gb ->screen[i] = color;
                }
            }
        }
    }
    
    if (!gb->disable_rendering && gb->border_mode == GB_BORDER_ALWAYS && !GB_is_sgb(gb)) {
        GB_borrow_sgb_border(gb);
        uint32_t border_colors[16 * 4];
        
        if (!gb->has_sgb_border && GB_is_cgb(gb) && gb->model <= GB_MODEL_CGB_E) {
            uint16_t colors[] = {
                0x2095, 0x5129, 0x1EAF, 0x1EBA, 0x4648,
                0x30DA, 0x69AD, 0x2B57, 0x2B5D, 0x632C,
                0x1050, 0x3C84, 0x0E07, 0x0E18, 0x2964,
            };
            unsigned index = gb->rom? gb->rom[0x14E] % 5 : 0;
            if (gb->model == GB_MODEL_CGB_0) {
                index = 1; // CGB 0 was only available in indigo!
            }
            else if (gb->model == GB_MODEL_CGB_A) {
                index = 0; // CGB A was only available in red!
            }
            gb->borrowed_border.palette[0] = LE16(colors[index]);
            gb->borrowed_border.palette[10] = LE16(colors[5 + index]);
            gb->borrowed_border.palette[14] = LE16(colors[10 + index]);

        }
        
        for (unsigned i = 0; i < 16 * 4; i++) {
            border_colors[i] = GB_convert_rgb15(gb, LE16(gb->borrowed_border.palette[i]), true);
        }
        
        for (unsigned tile_y = 0; tile_y < 28; tile_y++) {
            for (unsigned tile_x = 0; tile_x < 32; tile_x++) {
                if (tile_x >= 6 && tile_x < 26 && tile_y >= 5 && tile_y < 23) {
                    continue;
                }
                uint16_t tile = LE16(gb->borrowed_border.map[tile_x + tile_y * 32]);
                uint8_t flip_x = (tile & 0x4000)? 0:7;
                uint8_t flip_y = (tile & 0x8000)? 7:0;
                uint8_t palette = (tile >> 10) & 3;
                for (unsigned y = 0; y < 8; y++) {
                    unsigned base = (tile & 0xFF) * 32 + (y ^ flip_y) * 2;
                    for (unsigned x = 0; x < 8; x++) {
                        uint8_t bit = 1 << (x ^ flip_x);
                        uint8_t color = ((gb->borrowed_border.tiles[base] & bit)      ? 1 : 0) |
                                        ((gb->borrowed_border.tiles[base + 1] & bit)  ? 2 : 0) |
                                        ((gb->borrowed_border.tiles[base + 16] & bit) ? 4 : 0) |
                                        ((gb->borrowed_border.tiles[base + 17] & bit) ? 8 : 0);
                        uint32_t *output = gb->screen + tile_x * 8 + x + (tile_y * 8 + y) * 256;
                        if (color == 0) {
                            *output = border_colors[0];
                        }
                        else {
                            *output = border_colors[color + palette * 16];
                        }
                    }
                }
            }
        }
    }
    GB_handle_rumble(gb);

    if (gb->vblank_callback) {
        gb->vblank_callback(gb, type);
    }
    GB_timing_sync(gb);
}

static inline void temperature_tint(double temperature, double *r, double *g, double *b)
{
    if (temperature >= 0) {
        *r = 1;
        *g = pow(1 - temperature, 0.375);
        if (temperature >= 0.75) {
            *b = 0;
        }
        else {
            *b = sqrt(0.75 - temperature) / sqrt(0.75);
        }
    }
    else {
        *b = 1;
        double squared = pow(temperature, 2);
        *g = 0.125 * squared + 0.3 * temperature + 1.0;
        *r = 0.21875 * squared + 0.5 * temperature + 1.0;
    }
}

static inline uint8_t scale_channel(uint8_t x)
{
    return (x << 3) | (x >> 2);
}

static inline uint8_t scale_channel_with_curve(uint8_t x)
{
    return inline_const(uint8_t[], {0,6,12,20,28,36,45,56,66,76,88,100,113,125,137,149,161,172,182,192,202,210,218,225,232,238,243,247,250,252,254,255})[x];
}

static inline uint8_t scale_channel_with_curve_agb(uint8_t x)
{
    return inline_const(uint8_t[], {0,3,8,14,20,26,33,40,47,54,62,70,78,86,94,103,112,120,129,138,147,157,166,176,185,195,205,215,225,235,245,255})[x];
}

static inline uint8_t scale_channel_with_curve_sgb(uint8_t x)
{
    return inline_const(uint8_t[], {0,2,5,9,15,20,27,34,42,50,58,67,76,85,94,104,114,123,133,143,153,163,173,182,192,202,211,220,229,238,247,255})[x];
}


uint32_t GB_convert_rgb15(GB_gameboy_t *gb, uint16_t color, bool for_border)
{
    uint8_t r = (color) & 0x1F;
    uint8_t g = (color >> 5) & 0x1F;
    uint8_t b = (color >> 10) & 0x1F;
    
    if (gb->color_correction_mode == GB_COLOR_CORRECTION_DISABLED || (for_border && !gb->has_sgb_border)) {
        r = scale_channel(r);
        g = scale_channel(g);
        b = scale_channel(b);
    }
    else if (GB_is_sgb(gb) || for_border) {
        r = scale_channel_with_curve_sgb(r);
        g = scale_channel_with_curve_sgb(g);
        b = scale_channel_with_curve_sgb(b);
    }
    else {
        bool agb = gb->model > GB_MODEL_CGB_E;
        r = agb? scale_channel_with_curve_agb(r) : scale_channel_with_curve(r);
        g = agb? scale_channel_with_curve_agb(g) : scale_channel_with_curve(g);
        b = agb? scale_channel_with_curve_agb(b) : scale_channel_with_curve(b);
        
        if (gb->color_correction_mode != GB_COLOR_CORRECTION_CORRECT_CURVES) {
            uint8_t new_r, new_g, new_b;
            if (g != b) { // Minor optimization
                double gamma = 2.2;
                if (gb->color_correction_mode < GB_COLOR_CORRECTION_REDUCE_CONTRAST) {
                    /* Don't use absolutely gamma-correct mixing for the high-contrast
                       modes, to prevent the blue hues from being too washed out */
                    gamma = 1.6;
                }
                
                // TODO: Optimze pow out using a LUT
                if (agb) {
                    new_g = round(pow((pow(g / 255.0, gamma) * 5 + pow(b / 255.0, gamma)) / 6, 1 / gamma) * 255);
                }
                else {
                    new_g = round(pow((pow(g / 255.0, gamma) * 3 + pow(b / 255.0, gamma)) / 4, 1 / gamma) * 255);
                }
            }
            else {
                new_g = g;
            }
   
            new_r = r;
            new_b = b;
            if (gb->color_correction_mode == GB_COLOR_CORRECTION_REDUCE_CONTRAST) {
                r = new_r;
                g = new_g;
                b = new_b;
                
                new_r = new_r * 15 / 16 + (    g + b) / 32;
                new_g = new_g * 15 / 16 + (r   +   b) / 32;
                new_b = new_b * 15 / 16 + (r + g    ) / 32;
                
                if (agb) {
                    new_r = new_r * (224 - 20) / 255 + 20;
                    new_g = new_g * (220 - 18) / 255 + 18;
                    new_b = new_b * (216 - 16) / 255 + 16;
                }
                else {
                    new_r = new_r * (220 - 40) / 255 + 40;
                    new_g = new_g * (224 - 36) / 255 + 36;
                    new_b = new_b * (216 - 32) / 255 + 32;
                }
            }
            else if (gb->color_correction_mode == GB_COLOR_CORRECTION_LOW_CONTRAST) {
                r = new_r;
                g = new_g;
                b = new_b;

                new_r = new_r * 15 / 16 + (    g + b) / 32;
                new_g = new_g * 15 / 16 + (r   +   b) / 32;
                new_b = new_b * 15 / 16 + (r + g    ) / 32;
                
                if (agb) {
                    new_r = new_r * (167 - 27) / 255 + 27;
                    new_g = new_g * (165 - 24) / 255 + 24;
                    new_b = new_b * (157 - 22) / 255 + 22;
                }
                else {
                    new_r = new_r * (162 - 45) / 255 + 45;
                    new_g = new_g * (167 - 41) / 255 + 41;
                    new_b = new_b * (157 - 38) / 255 + 38;
                }
            }
            else if (gb->color_correction_mode == GB_COLOR_CORRECTION_MODERN_BOOST_CONTRAST) {
                uint8_t old_max = MAX(r, MAX(g, b));
                uint8_t new_max = MAX(new_r, MAX(new_g, new_b));
                
                if (new_max != 0) {
                    new_r = new_r * old_max / new_max;
                    new_g = new_g * old_max / new_max;
                    new_b = new_b * old_max / new_max;
                }
                
                uint8_t old_min = MIN(r, MIN(g, b));
                uint8_t new_min = MIN(new_r, MIN(new_g, new_b));
                
                if (new_min != 0xFF) {
                    new_r = 0xFF - (0xFF - new_r) * (0xFF - old_min) / (0xFF - new_min);
                    new_g = 0xFF - (0xFF - new_g) * (0xFF - old_min) / (0xFF - new_min);
                    new_b = 0xFF - (0xFF - new_b) * (0xFF - old_min) / (0xFF - new_min);
                }
            }
            r = new_r;
            g = new_g;
            b = new_b;
        }
    }
    
    if (gb->light_temperature) {
        double light_r, light_g, light_b;
        temperature_tint(gb->light_temperature, &light_r, &light_g, &light_b);
        r = round(light_r * r);
        g = round(light_g * g);
        b = round(light_b * b);
    }
    
    return gb->rgb_encode_callback(gb, r, g, b);
}

void GB_palette_changed(GB_gameboy_t *gb, bool background_palette, uint8_t index)
{
    if (!gb->rgb_encode_callback || !GB_is_cgb(gb)) return;
    uint8_t *palette_data = background_palette? gb->background_palettes_data : gb->object_palettes_data;
    uint16_t color = palette_data[index & ~1] | (palette_data[index | 1] << 8);

    (background_palette? gb->background_palettes_rgb : gb->object_palettes_rgb)[index / 2] = GB_convert_rgb15(gb, color, false);
}

void GB_set_color_correction_mode(GB_gameboy_t *gb, GB_color_correction_mode_t mode)
{
    gb->color_correction_mode = mode;
    if (GB_is_cgb(gb)) {
        nounroll for (unsigned i = 0; i < 32; i++) {
            GB_palette_changed(gb, false, i * 2);
            GB_palette_changed(gb, true, i * 2);
        }
    }
}

void GB_set_light_temperature(GB_gameboy_t *gb, double temperature)
{
    gb->light_temperature = temperature;
    if (GB_is_cgb(gb)) {
        nounroll for (unsigned i = 0; i < 32; i++) {
            GB_palette_changed(gb, false, i * 2);
            GB_palette_changed(gb, true, i * 2);
        }
    }
}

static void wy_check(GB_gameboy_t *gb)
{
    if (!(gb->io_registers[GB_IO_LCDC] & GB_LCDC_ENABLE)) return;
    
    uint8_t comparison = gb->current_line;
    if ((!GB_is_cgb(gb) || gb->cgb_double_speed) && gb->ly_for_comparison != (uint8_t)-1) {
        comparison = gb->ly_for_comparison;
    }
    
    if ((gb->io_registers[GB_IO_LCDC] & GB_LCDC_WIN_ENABLE) &&
        gb->io_registers[GB_IO_WY] == comparison) {
        gb->wy_triggered = true;
    }
}

void GB_STAT_update(GB_gameboy_t *gb)
{
    if (!(gb->io_registers[GB_IO_LCDC] & GB_LCDC_ENABLE)) return;
    if (GB_is_dma_active(gb) && (gb->io_registers[GB_IO_STAT] & 3) == 2) {
        gb->io_registers[GB_IO_STAT] &= ~3;
    }
    
    bool previous_interrupt_line = gb->stat_interrupt_line;
    /* Set LY=LYC bit */
    if (gb->ly_for_comparison != (uint16_t)-1 || (gb->model <= GB_MODEL_CGB_C && !gb->cgb_double_speed)) {
        if (gb->ly_for_comparison == gb->io_registers[GB_IO_LYC]) {
            gb->lyc_interrupt_line = true;
            gb->io_registers[GB_IO_STAT] |= 4;
        }
        else {
            if (gb->ly_for_comparison != (uint16_t)-1) {
                gb->lyc_interrupt_line = false;
            }
            gb->io_registers[GB_IO_STAT] &= ~4;
        }
    }
    
    switch (gb->mode_for_interrupt) {
        case 0: gb->stat_interrupt_line = gb->io_registers[GB_IO_STAT] & 8; break;
        case 1: gb->stat_interrupt_line = gb->io_registers[GB_IO_STAT] & 0x10; break;
        case 2: gb->stat_interrupt_line = gb->io_registers[GB_IO_STAT] & 0x20; break;
        default: gb->stat_interrupt_line = false;
    }
    
    /* User requested a LY=LYC interrupt and the LY=LYC bit is on */
    if ((gb->io_registers[GB_IO_STAT] & 0x40) && gb->lyc_interrupt_line) {
        gb->stat_interrupt_line = true;
    }
    
    if (gb->stat_interrupt_line && !previous_interrupt_line) {
        gb->io_registers[GB_IO_IF] |= 2;
    }
}

void GB_lcd_off(GB_gameboy_t *gb)
{
    gb->cycles_for_line = 0;
    gb->display_state = 0;
    gb->display_cycles = 0;
    /* When the LCD is disabled, state is constant */
    
    if (gb->hdma_on_hblank && (gb->io_registers[GB_IO_STAT] & 3)) {
        gb->hdma_on = true;
    }
    
    /* When the LCD is off, LY is 0 and STAT mode is 0.  */
    gb->io_registers[GB_IO_LY] = 0;
    gb->io_registers[GB_IO_STAT] &= ~3;
    
    
    gb->oam_read_blocked = false;
    gb->vram_read_blocked = false;
    gb->oam_write_blocked = false;
    gb->vram_write_blocked = false;
    gb->cgb_palettes_blocked = false;
    
    gb->current_line = 0;
    gb->ly_for_comparison = 0;
    
    gb->accessed_oam_row = -1;
    gb->wy_triggered = false;
    
    if (unlikely(gb->lcd_line_callback)) {
        gb->lcd_line_callback(gb, 0);
    }
}

static inline uint8_t oam_read(GB_gameboy_t *gb, uint8_t addr)
{
    if (unlikely(gb->oam_ppu_blocked)) {
        return 0xFF;
    }
    if (unlikely(gb->dma_current_dest <= 0xA0 && gb->dma_current_dest > 0)) { // TODO: what happens in the last and first M cycles?
        if (gb->hdma_in_progress) {
            return GB_read_oam(gb, (gb->hdma_current_src & ~1) | (addr & 1));
        }
        if (gb->dma_current_dest != 0xA0) {
            return gb->oam[(gb->dma_current_dest & ~1) | (addr & 1)];
        }
    }
    return gb->oam[addr];
}

static void add_object_from_index(GB_gameboy_t *gb, unsigned index)
{
    if (likely(!GB_is_dma_active(gb) || gb->halted || gb->stopped)) {
        gb->mode2_y_bus = oam_read(gb, index * 4);
        gb->mode2_x_bus = oam_read(gb, index * 4 + 1);
    }

    if (unlikely(gb->n_visible_objs == 10)) return;
    
    /* TODO: It appears that DMA blocks PPU access to OAM, but it needs verification. */
    if (unlikely(GB_is_dma_active(gb) && (gb->halted || gb->stopped))) {
        if (gb->model < GB_MODEL_CGB_E) {
            return;
        }
        /* CGB-0 to CGB-D: Halted DMA blocks Mode 2;
           Pre-CGB: Unit specific behavior, some units read FFs, some units read using
                    several different corruption pattterns. For simplicity, we emulate
                    FFs. */
    }
    
    if (unlikely(gb->oam_ppu_blocked)) {
        return;
    }
    
    bool height_16 = (gb->io_registers[GB_IO_LCDC] & GB_LCDC_OBJ_SIZE) != 0;
    signed y = gb->mode2_y_bus - 16;
    /* This reverse sorts the visible objects by location and priority */
    if (y <= gb->current_line && y + (height_16? 16 : 8) > gb->current_line) {
        unsigned j = 0;
        for (; j < gb->n_visible_objs; j++) {
            if (gb->objects_x[j] <= gb->mode2_x_bus) break;
        }
        memmove(gb->visible_objs + j + 1, gb->visible_objs + j, gb->n_visible_objs - j);
        memmove(gb->objects_x + j + 1, gb->objects_x + j, gb->n_visible_objs - j);
        memmove(gb->objects_y + j + 1, gb->objects_y + j, gb->n_visible_objs - j);
        gb->visible_objs[j] = index;
        gb->objects_x[j] = gb->mode2_x_bus;
        gb->objects_y[j] = gb->mode2_y_bus;
        gb->n_visible_objs++;
    }
}

static void render_pixel_if_possible(GB_gameboy_t *gb)
{
    const GB_fifo_item_t *fifo_item = NULL;
    const GB_fifo_item_t *oam_fifo_item = NULL;
    bool draw_oam = false;
    bool bg_enabled = true, bg_priority = false;
 
    // Rendering (including scrolling adjustment) does not occur as long as an object at x=0 is pending
    if (gb->n_visible_objs != 0 &&
        (gb->io_registers[GB_IO_LCDC] & GB_LCDC_OBJ_EN || GB_is_cgb(gb)) &&
        gb->objects_x[gb->n_visible_objs - 1] == 0) {
        return;
    }
    
    if (unlikely(!fifo_size(&gb->bg_fifo))) return;
    
    if (unlikely(gb->insert_bg_pixel)) {
        gb->insert_bg_pixel = false;
        fifo_item = ({static const GB_fifo_item_t empty_item = {0,}; &empty_item;});
    }
    else {
        fifo_item = fifo_pop(&gb->bg_fifo);
    }
    bg_priority = fifo_item->bg_priority;
    
    if (fifo_size(&gb->oam_fifo)) {
        oam_fifo_item = fifo_pop(&gb->oam_fifo);
        if (oam_fifo_item->pixel && (gb->io_registers[GB_IO_LCDC] & GB_LCDC_OBJ_EN) && unlikely(!gb->objects_disabled)) {
            draw_oam = true;
            bg_priority |= oam_fifo_item->bg_priority;
        }
    }
    
    // (gb->position_in_line + 16 < 8) is (gb->position_in_line < -8) in unsigned logic
    if (((uint8_t)(gb->position_in_line + 16) < 8)) {
        if (gb->position_in_line == (uint8_t)-17) {
            gb->position_in_line = -16;
        }
        else if ((gb->position_in_line & 7) == (gb->io_registers[GB_IO_SCX] & 7)) {
            gb->position_in_line = -8;
        }
        else if (gb->window_is_being_fetched && (gb->position_in_line & 7) == 6 && (gb->io_registers[GB_IO_SCX] & 7) == 7) { // TODO: Why does this happen?
            gb->position_in_line = -8;
        }
        else if (gb->position_in_line == (uint8_t) -9) {
            gb->position_in_line = -16;
            return;
        }
        else {
            gb->line_has_fractional_scrolling = true;
        }
    }

    gb->window_is_being_fetched = false;
    
    /* Drop pixels for scrollings */
    if (gb->position_in_line >= 160 || (gb->disable_rendering && !gb->sgb)) {
        gb->position_in_line++;
        return;
    }
    
    /* Mixing */
    
    if ((gb->io_registers[GB_IO_LCDC] & GB_LCDC_BG_EN) == 0) {
        if (gb->cgb_mode) {
            bg_priority = false;
        }
        else {
            bg_enabled = false;
        }
    }
    
    if (unlikely(gb->background_disabled)) {
        bg_enabled = false;
        static const GB_fifo_item_t empty_item = {0,};
        fifo_item = &empty_item;
    }

    uint8_t icd_pixel = 0;
    uint32_t *dest = NULL;
    if (!gb->sgb) {
        if (gb->border_mode != GB_BORDER_ALWAYS) {
            dest = gb->screen + gb->lcd_x + gb->current_line * WIDTH;
        }
        else {
            dest = gb->screen + gb->lcd_x + gb->current_line * BORDERED_WIDTH + (BORDERED_WIDTH - WIDTH) / 2 + (BORDERED_HEIGHT - LINES) / 2 * BORDERED_WIDTH;
        }
    }
    
    {
        uint8_t pixel = bg_enabled? fifo_item->pixel : 0;
        if (pixel && bg_priority) {
            draw_oam = false;
        }
        if (!gb->cgb_mode) {
            pixel = ((gb->io_registers[GB_IO_BGP] >> (pixel << 1)) & 3);
        }
        if (gb->sgb) {
            if (gb->current_lcd_line < LINES) {
                gb->sgb->screen_buffer[gb->lcd_x + gb->current_lcd_line * WIDTH] = gb->stopped? 0 : pixel;
            }
        }
        else if (gb->model & GB_MODEL_NO_SFC_BIT) {
            if (gb->icd_pixel_callback) {
                icd_pixel = pixel;
            }
        }
        else if (gb->cgb_palettes_ppu_blocked) {
            *dest = gb->rgb_encode_callback(gb, 0, 0, 0);
        }
        else {
            *dest = gb->background_palettes_rgb[fifo_item->palette * 4 + pixel];
        }
    }
    
    if (draw_oam) {
        uint8_t pixel = oam_fifo_item->pixel;
        if (!gb->cgb_mode) {
            /* Todo: Verify access timings */
            pixel = ((gb->io_registers[oam_fifo_item->palette? GB_IO_OBP1 : GB_IO_OBP0] >> (pixel << 1)) & 3);
        }
        if (gb->sgb) {
            if (gb->current_lcd_line < LINES) {
                gb->sgb->screen_buffer[gb->lcd_x + gb->current_lcd_line * WIDTH] = gb->stopped? 0 : pixel;
            }
        }
        else if (gb->model & GB_MODEL_NO_SFC_BIT) {
            if (gb->icd_pixel_callback) {
                icd_pixel = pixel;
            }
        }
        else if (gb->cgb_palettes_ppu_blocked) {
            *dest = gb->rgb_encode_callback(gb, 0, 0, 0);
        }
        else {
            *dest = gb->object_palettes_rgb[oam_fifo_item->palette * 4 + pixel];
        }
    }
    
    if (gb->model & GB_MODEL_NO_SFC_BIT) {
        if (gb->icd_pixel_callback) {
            gb->icd_pixel_callback(gb, icd_pixel);
        }
    }
    
    gb->position_in_line++;
    gb->lcd_x++;
}

static inline void dma_sync(GB_gameboy_t *gb, unsigned *cycles)
{
    if (unlikely(GB_is_dma_active(gb))) {
        unsigned offset = *cycles - gb->display_cycles; // Time passed in 8MHz ticks
        if (offset) {
            *cycles = gb->display_cycles;
            if (!gb->cgb_double_speed) {
                offset >>= 1; // Convert to T-cycles
            }
            unsigned old = gb->dma_cycles;
            gb->dma_cycles = offset;
            GB_dma_run(gb);
            gb->dma_cycles = old - offset;
        }
    }
}

static inline uint8_t fetcher_y(GB_gameboy_t *gb)
{
    return gb->wx_triggered? gb->window_y : gb->current_line + gb->io_registers[GB_IO_SCY];
}

static inline uint8_t vram_read(GB_gameboy_t *gb, uint16_t addr)
{
    if (unlikely(gb->vram_ppu_blocked)) {
        return 0xFF;
    }
    if (unlikely(gb->hdma_in_progress)) {
        gb->addr_for_hdma_conflict = addr;
        return 0;
    }
    // TODO: what if both?
    else if (unlikely(gb->dma_current_dest <= 0xA0 && gb->dma_current_dest > 0 && (gb->dma_current_src & 0xE000) == 0x8000)) { // TODO: what happens in the last and first M cycles?
        // DMAing from VRAM!
        /* TODO: AGS has its own, very different pattern, but AGS is not currently a supported model */
        /* TODO: Research this when researching odd modes */
        /* TODO: probably not 100% on the first few reads during halt/stop modes*/
        unsigned offset = 1 - (gb->halted || gb->stopped);
        if (GB_is_cgb(gb)) {
            if (gb->dma_ppu_vram_conflict) {
                addr = (gb->dma_ppu_vram_conflict_addr & 0x1FFF) | (addr & 0x2000);
            }
            else if (gb->dma_cycles_modulo && !gb->halted && !gb->stopped) {
                addr &= 0x2000;
                addr |= ((gb->dma_current_src - offset) & 0x1FFF);
            }
            else {
                addr &= 0x2000 | ((gb->dma_current_src - offset) & 0x1FFF);
                gb->dma_ppu_vram_conflict_addr = addr;
                gb->dma_ppu_vram_conflict = !gb->halted && !gb->stopped;
            }
        }
        else {
            addr |= ((gb->dma_current_src - offset) & 0x1FFF);
        }
        gb->oam[gb->dma_current_dest - offset] = gb->vram[(addr & 0x1FFF) | (gb->cgb_vram_bank? 0x2000 : 0)];
    }
    return gb->vram[addr];
}

typedef enum {
    /* VRAM reads take 2 T-cycles. In read address is determined in the first
     cycle, and the read actually completes in the second cycle.*/
    GB_FETCHER_GET_TILE_T1,
    GB_FETCHER_GET_TILE_T2,
    GB_FETCHER_GET_TILE_DATA_LOWER_T1,
    GB_FETCHER_GET_TILE_DATA_LOWER_T2,
    GB_FETCHER_GET_TILE_DATA_HIGH_T1,
    GB_FETCHER_GET_TILE_DATA_HIGH_T2,
    GB_FETCHER_PUSH,
} fetcher_step_t;

static uint8_t data_for_tile_sel_glitch(GB_gameboy_t *gb, bool *should_use, bool *cgb_d_glitch)
{
    /*
     Based on Matt Currie's research here:
     https://github.com/mattcurrie/mealybug-tearoom-tests/blob/master/the-comprehensive-game-boy-ppu-documentation.md#tile_sel-bit-4
     */
    *should_use = true;
    *cgb_d_glitch = false;
    
    if (gb->last_tileset) {
        if (gb->model != GB_MODEL_CGB_D) {
            *should_use = !(gb->current_tile & 0x80);
            return gb->current_tile;
        }
        *cgb_d_glitch = true;
        *should_use = false;
        gb->last_tile_data_address &= ~0x1000;
        if (gb->fetcher_state == GB_FETCHER_GET_TILE_DATA_LOWER_T2) {
            *cgb_d_glitch = true;
            return 0;
        }
        return 0;
    }
    return gb->data_for_sel_glitch;
}

internal void GB_update_wx_glitch(GB_gameboy_t *gb)
{
    if (!GB_is_cgb(gb)) return;
    if (!(gb->io_registers[GB_IO_LCDC] & GB_LCDC_WIN_ENABLE) || !gb->wy_triggered) {
        gb->cgb_wx_glitch = false;
        return;
    }
    if (unlikely(gb->io_registers[GB_IO_WX] == 0)) {
        // (gb->position_in_line + 16 <= 8) is (gb->position_in_line <= -8) in unsigned
        gb->cgb_wx_glitch = ((uint8_t)(gb->position_in_line + 16) <= 8 ||
                             (gb->position_in_line == (uint8_t)-7 && gb->line_has_fractional_scrolling));
        return;
    }
    gb->cgb_wx_glitch = (uint8_t)(gb->position_in_line + 7 + gb->window_is_being_fetched) == gb->io_registers[GB_IO_WX];
}

static void advance_fetcher_state_machine(GB_gameboy_t *gb, unsigned *cycles)
{
    switch ((fetcher_step_t)gb->fetcher_state) {
        case GB_FETCHER_GET_TILE_T1: {
            GB_update_wx_glitch(gb);
            uint16_t map = 0x1800;
            
            if (!(gb->io_registers[GB_IO_LCDC] & GB_LCDC_WIN_ENABLE)) {
                gb->wx_triggered = false;
            }
            
            if (gb->io_registers[GB_IO_LCDC] & GB_LCDC_BG_MAP && !gb->wx_triggered) {
                map = 0x1C00;
            }
            else if (gb->io_registers[GB_IO_LCDC] & GB_LCDC_WIN_MAP && gb->wx_triggered) {
                map = 0x1C00;
            }
            
            uint8_t y = fetcher_y(gb);
            uint8_t x = 0;
            if (gb->wx_triggered) {
                x = gb->window_tile_x;
            }
            else if ((uint8_t)(gb->position_in_line + 16) < 8) {
                x = gb->io_registers[GB_IO_SCX] >> 3;
            }
            else {
                x = ((gb->io_registers[GB_IO_SCX] + gb->position_in_line + 8 - (GB_is_cgb(gb) && !gb->during_object_fetch)) / 8) & 0x1F;
            }
            if (gb->model > GB_MODEL_CGB_C) {
                /* This value is cached on the CGB-D and newer, so it cannot be used to mix tiles together */
                gb->fetcher_y = y;
            }
            gb->last_tile_index_address = map + x + y / 8 * 32;
        }
        gb->fetcher_state++;
        break;
        case GB_FETCHER_GET_TILE_T2: {
            if (gb->cgb_wx_glitch) {
                gb->fetcher_state++;
                break;
            }
            dma_sync(gb, cycles);
            gb->current_tile = vram_read(gb, gb->last_tile_index_address);
            if (GB_is_cgb(gb)) {
                /* The CGB actually accesses both the tile index AND the attributes in the same T-cycle.
                   This probably means the CGB has a 16-bit data bus for the VRAM. */
                gb->current_tile_attributes = vram_read(gb, gb->last_tile_index_address + 0x2000);
            }
        }
        gb->fetcher_state++;
        break;
            
        case GB_FETCHER_GET_TILE_DATA_LOWER_T1: {
            GB_update_wx_glitch(gb);

            uint8_t y_flip = 0;
            uint16_t tile_address = 0;
            uint8_t y = gb->model > GB_MODEL_CGB_C ? gb->fetcher_y : fetcher_y(gb);
            
            gb->last_tileset = gb->io_registers[GB_IO_LCDC] & GB_LCDC_TILE_SEL;
            if (gb->last_tileset) {
                tile_address = gb->current_tile * 0x10;
            }
            else {
                tile_address =  (int8_t)gb->current_tile * 0x10 + 0x1000;
            }
            if (gb->current_tile_attributes & 8) {
                tile_address += 0x2000;
            }
            if (gb->current_tile_attributes & 0x40) {
                y_flip = 0x7;
            }
            gb->last_tile_data_address = tile_address + ((y & 7) ^ y_flip) * 2;
        }
        gb->fetcher_state++;
        break;
            
        case GB_FETCHER_GET_TILE_DATA_LOWER_T2: {
            if (gb->cgb_wx_glitch) {
                gb->current_tile_data[0] = gb->current_tile_data[1];
                gb->fetcher_state++;
                break;
            }
            dma_sync(gb, cycles);
            bool use_glitched = false;
            bool cgb_d_glitch = false;
            if (gb->tile_sel_glitch) {
                gb->current_tile_data[0] = data_for_tile_sel_glitch(gb, &use_glitched, &cgb_d_glitch);
            }
            if (!use_glitched) {
                gb->current_tile_data[0] = vram_read(gb, gb->last_tile_data_address);
            }
            if (gb->last_tileset && gb->tile_sel_glitch) {
                gb->data_for_sel_glitch = vram_read(gb, gb->last_tile_data_address);
            }
            else if (cgb_d_glitch) {
                gb->data_for_sel_glitch = vram_read(gb, gb->last_tile_data_address & ~0x1000);
            }
        }
        gb->fetcher_state++;
        break;
            
        case GB_FETCHER_GET_TILE_DATA_HIGH_T1: {
            GB_update_wx_glitch(gb);

            uint16_t tile_address = 0;
            uint8_t y = gb->model > GB_MODEL_CGB_C ? gb->fetcher_y : fetcher_y(gb);
            
            gb->last_tileset = gb->io_registers[GB_IO_LCDC] & GB_LCDC_TILE_SEL;
            if (gb->last_tileset) {
                tile_address = gb->current_tile * 0x10;
            }
            else {
                tile_address =  (int8_t)gb->current_tile * 0x10 + 0x1000;
            }
            if (gb->current_tile_attributes & 8) {
                tile_address += 0x2000;
            }
            uint8_t y_flip = 0;
            if (gb->current_tile_attributes & 0x40) {
                y_flip = 0x7;
            }
            gb->last_tile_data_address = tile_address +  ((y & 7) ^ y_flip) * 2 + 1;
        }
        gb->fetcher_state++;
        break;
            
        case GB_FETCHER_GET_TILE_DATA_HIGH_T2: {
            if (gb->cgb_wx_glitch) {
                gb->current_tile_data[1] = gb->current_tile_data[0];
                gb->fetcher_state++;
                if (gb->wx_triggered) {
                    gb->window_tile_x++;
                    gb->window_tile_x &= 0x1F;
                }
                break;
            }
            dma_sync(gb, cycles);
            bool use_glitched = false;
            bool cgb_d_glitch = false;
            if (gb->tile_sel_glitch) {
                gb->current_tile_data[1] = data_for_tile_sel_glitch(gb, &use_glitched, &cgb_d_glitch);
                if (cgb_d_glitch) {
                    gb->last_tile_data_address--;
                }
            }
            if (!use_glitched) {
                gb->data_for_sel_glitch = gb->current_tile_data[1] =
                    vram_read(gb, gb->last_tile_data_address);
            }
            if (gb->last_tileset && gb->tile_sel_glitch) {
                gb->data_for_sel_glitch = vram_read(gb, gb->last_tile_data_address);

            }
            else if (cgb_d_glitch) {
                gb->data_for_sel_glitch = vram_read(gb, (gb->tile_sel_glitch & ~0x1000) + 1);
            }
        }
        if (gb->wx_triggered) {
            gb->window_tile_x++;
            gb->window_tile_x &= 0x1F;
        }
            
        // fallthrough
        default:
        case GB_FETCHER_PUSH: {
            gb->fetcher_state = GB_FETCHER_PUSH;
            if (fifo_size(&gb->bg_fifo) > 0) break;
            
            if (unlikely(gb->wy_triggered && !(gb->io_registers[GB_IO_LCDC] & GB_LCDC_WIN_ENABLE) && !GB_is_cgb(gb) && !gb->disable_window_pixel_insertion_glitch)) {
                /* See https://github.com/LIJI32/SameBoy/issues/278 for documentation */
                uint8_t logical_position = gb->position_in_line + 7;
                if (logical_position > 167) {
                    logical_position = 0;
                }
                if (gb->io_registers[GB_IO_WX] == logical_position) {
                    gb->bg_fifo.read_end--;
                    gb->bg_fifo.read_end &= GB_FIFO_LENGTH - 1;
                    gb->bg_fifo.fifo[gb->bg_fifo.read_end] = (GB_fifo_item_t){0,};
                    gb->bg_fifo.size = 1;
                    break;
                }
            }
            
            fifo_push_bg_row(&gb->bg_fifo, gb->current_tile_data[0], gb->current_tile_data[1],
                             gb->current_tile_attributes & 7, gb->current_tile_attributes & 0x80, gb->current_tile_attributes & 0x20);
            gb->fetcher_state = GB_FETCHER_GET_TILE_T1;
        }
        break;
    }
}

static uint16_t get_object_line_address(GB_gameboy_t *gb, uint8_t y, uint8_t tile, uint8_t flags)
{
    bool height_16 = (gb->io_registers[GB_IO_LCDC] & GB_LCDC_OBJ_SIZE) != 0; /* Todo: Which T-cycle actually reads this? */
    uint8_t tile_y = (gb->current_line - y) & (height_16? 0xF : 7);
    
    if (flags & 0x40) { /* Flip Y */
        tile_y ^= height_16? 0xF : 7;
    }
    
    /* Todo: I'm not 100% sure an access to OAM can't trigger the OAM bug while we're accessing this */
    uint16_t line_address = (height_16? tile & 0xFE : tile) * 0x10 + tile_y * 2;
    
    if (gb->cgb_mode && (flags & 0x8)) { /* Use VRAM bank 2 */
        line_address += 0x2000;
    }
    return line_address;
}

static inline uint8_t flip(uint8_t x)
{
    x = (x & 0xF0) >> 4 | (x & 0x0F) << 4;
    x = (x & 0xCC) >> 2 | (x & 0x33) << 2;
    x = (x & 0xAA) >> 1 | (x & 0x55) << 1;
    return x;
}

static inline void get_tile_data(const GB_gameboy_t *gb, uint8_t tile_x, uint8_t y, uint16_t map, uint8_t *attributes, uint8_t *data0, uint8_t *data1)
{
    uint8_t current_tile = gb->vram[map + (tile_x & 0x1F) + y / 8 * 32];
    *attributes = GB_is_cgb(gb)? gb->vram[0x2000 + map + (tile_x & 0x1F) + y / 8 * 32] : 0;
    
    uint16_t tile_address = 0;
    
    if (gb->io_registers[GB_IO_LCDC] & GB_LCDC_TILE_SEL) {
        tile_address = current_tile * 0x10;
    }
    else {
        tile_address =  (int8_t)current_tile * 0x10 + 0x1000;
    }
    if (*attributes & 8) {
        tile_address += 0x2000;
    }
    uint8_t y_flip = 0;
    if (*attributes & 0x40) {
        y_flip = 0x7;
    }
    
    *data0 = gb->vram[tile_address +  ((y & 7) ^ y_flip) * 2];
    *data1 = gb->vram[tile_address +  ((y & 7) ^ y_flip) * 2 + 1];
    
    if (*attributes & 0x20) {
        *data0 = flip(*data0);
        *data1 = flip(*data1);
    }

}

static void render_line(GB_gameboy_t *gb)
{
    if (gb->disable_rendering) return;
    if (!gb->screen) return;
    if (gb->current_line > 144) return; // Corrupt save state
    
    struct {
        unsigned pixel:2; // Color, 0-3
        unsigned priority:6; // Object priority – 0 in DMG, OAM index in CGB
        unsigned palette:3; // Palette, 0 - 7 (CGB); 0-1 in DMG (or just 0 for BG)
        bool bg_priority:1; // BG priority bit
    } _object_buffer[160 + 16]; // allocate extra to avoid per pixel checks
    static const uint8_t empty_object_buffer[sizeof(_object_buffer)];
    const typeof(_object_buffer[0]) *object_buffer;
    
    if (gb->n_visible_objs && !gb->objects_disabled && (gb->io_registers[GB_IO_LCDC] & GB_LCDC_OBJ_EN)) {
        object_buffer = &_object_buffer[0];
        object_t *objects = (object_t *) &gb->oam;
        memset(_object_buffer, 0, sizeof(_object_buffer));

        while (gb->n_visible_objs) {
            unsigned object_index = gb->visible_objs[gb->n_visible_objs - 1];
            unsigned priority = (gb->object_priority == GB_OBJECT_PRIORITY_X)? 0 : object_index;
            const object_t *object = &objects[object_index];
            gb->n_visible_objs--;
            
            uint16_t line_address = get_object_line_address(gb, object->y, object->tile, object->flags);
            uint8_t data0 = gb->vram[line_address];
            uint8_t data1 = gb->vram[line_address + 1];
            if (gb->n_visible_objs == 0) {
                gb->data_for_sel_glitch = data1;
            }
            if (object->flags & 0x20) {
                data0 = flip(data0);
                data1 = flip(data1);
            }

            typeof(_object_buffer[0]) *p = _object_buffer + object->x;
            if (object->x >= 168) {
                continue;
            }
            unrolled for (unsigned x = 0; x < 8; x++) {
                unsigned pixel = (data0 >> 7) | ((data1 >> 7) << 1);
                data0 <<= 1;
                data1 <<= 1;
                if (pixel && (!p->pixel || priority < p->priority)) {
                    p->pixel = pixel;
                    p->priority = priority;
                    
                    if (gb->cgb_mode) {
                        p->palette = object->flags & 0x7;
                    }
                    else {
                        p->palette = (object->flags & 0x10) >> 4;
                    }
                    p->bg_priority = object->flags & 0x80;
                }
                p++;
            }
        }
    }
    else {
        object_buffer = (const void *)empty_object_buffer;
    }
    
    
    uint32_t *restrict p = gb->screen;
    typeof(object_buffer[0]) *object_buffer_pointer = object_buffer + 8;
    if (gb->border_mode == GB_BORDER_ALWAYS) {
        p += (BORDERED_WIDTH - (WIDTH)) / 2 + BORDERED_WIDTH * (BORDERED_HEIGHT - LINES) / 2;
        p += BORDERED_WIDTH * gb->current_line;
    }
    else {
        p += WIDTH * gb->current_line;
    }
    
    if (unlikely(gb->background_disabled) || (!gb->cgb_mode && !(gb->io_registers[GB_IO_LCDC] & GB_LCDC_BG_EN))) {
        uint32_t bg = gb->background_palettes_rgb[gb->cgb_mode? 0 : (gb->io_registers[GB_IO_BGP] & 3)];
        for (unsigned i = 160; i--;) {
            if (unlikely(object_buffer_pointer->pixel)) {
                uint8_t pixel = object_buffer_pointer->pixel;
                if (!gb->cgb_mode) {
                    pixel = ((gb->io_registers[GB_IO_OBP0 + object_buffer_pointer->palette] >> (pixel << 1)) & 3);
                }
                *(p++) = gb->object_palettes_rgb[pixel + (object_buffer_pointer->palette & 7) * 4];
            }
            else {
                *(p++) = bg;
            }
            object_buffer_pointer++;
        }
        return;
    }
    
    unsigned pixels = 0;
    uint8_t tile_x = gb->io_registers[GB_IO_SCX] / 8;
    unsigned fractional_scroll = gb->io_registers[GB_IO_SCX] & 7;
    uint16_t map = 0x1800;
    if (gb->io_registers[GB_IO_LCDC] & GB_LCDC_BG_MAP) {
        map = 0x1C00;
    }
    uint8_t y = gb->current_line + gb->io_registers[GB_IO_SCY];
    uint8_t attributes;
    uint8_t data0, data1;
    get_tile_data(gb, tile_x, y, map, &attributes, &data0, &data1);
    
#define DO_PIXEL() \
uint8_t pixel = (data0 >> 7) | ((data1 >> 7) << 1);\
data0 <<= 1;\
data1 <<= 1;\
\
if (unlikely(object_buffer_pointer->pixel) && (pixel == 0 || !(object_buffer_pointer->bg_priority || (attributes & 0x80)) || !(gb->io_registers[GB_IO_LCDC] & GB_LCDC_BG_EN))) {\
    pixel = object_buffer_pointer->pixel;\
    if (!gb->cgb_mode) {\
        pixel = ((gb->io_registers[GB_IO_OBP0 + object_buffer_pointer->palette] >> (pixel << 1)) & 3);\
    }\
    *(p++) = gb->object_palettes_rgb[pixel + (object_buffer_pointer->palette & 7) * 4];\
}\
else {\
    if (!gb->cgb_mode) {\
        pixel = ((gb->io_registers[GB_IO_BGP] >> (pixel << 1)) & 3);\
    }\
    *(p++) = gb->background_palettes_rgb[pixel + (attributes & 7) * 4];\
}\
pixels++;\
object_buffer_pointer++\
    
    // First 1-8 pixels
    data0 <<= fractional_scroll;
    data1 <<= fractional_scroll;
    bool check_window = gb->wy_triggered && (gb->io_registers[GB_IO_LCDC] & GB_LCDC_WIN_ENABLE);
    nounroll for (unsigned i = fractional_scroll; i < 8; i++) {
        if (check_window && gb->io_registers[GB_IO_WX] == pixels + 7) {
activate_window:
            check_window = false;
            map = gb->io_registers[GB_IO_LCDC] & GB_LCDC_WIN_MAP? 0x1C00 : 0x1800;
            tile_x = -1;
            y = ++gb->window_y;
            break;
        }
        DO_PIXEL();
    }
    tile_x++;
    
    while (pixels < 160 - 8) {
        get_tile_data(gb, tile_x, y, map, &attributes, &data0, &data1);
        nounroll for (unsigned i = 0; i < 8; i++) {
            if (check_window && gb->io_registers[GB_IO_WX] == pixels + 7) {
                goto activate_window;
            }
            DO_PIXEL();
        }
        tile_x++;
    }
    
    gb->fetcher_state = (160 - pixels) & 7;
    get_tile_data(gb, tile_x, y, map, &attributes, &data0, &data1);
    while (pixels < 160) {
        if (check_window && gb->io_registers[GB_IO_WX] == pixels + 7) {
            goto activate_window;
        }
        DO_PIXEL();
    }
    tile_x++;

    get_tile_data(gb, tile_x, y, map, &attributes, gb->current_tile_data, gb->current_tile_data + 1);
#undef DO_PIXEL
}

static void render_line_sgb(GB_gameboy_t *gb)
{
    if (gb->current_line > 144) return; // Corrupt save state
    
    struct {
        unsigned pixel:2; // Color, 0-3
        unsigned palette:1; // Palette, 0 - 7 (CGB); 0-1 in DMG (or just 0 for BG)
        bool bg_priority:1; // BG priority bit
    } _object_buffer[160 + 16]; // allocate extra to avoid per pixel checks
    static const uint8_t empty_object_buffer[sizeof(_object_buffer)];
    const typeof(_object_buffer[0]) *object_buffer;
    
    if (gb->n_visible_objs && !gb->objects_disabled && (gb->io_registers[GB_IO_LCDC] & GB_LCDC_OBJ_EN)) {
        object_buffer = &_object_buffer[0];
        object_t *objects = (object_t *) &gb->oam;
        memset(_object_buffer, 0, sizeof(_object_buffer));
        
        while (gb->n_visible_objs) {
            const object_t *object = &objects[gb->visible_objs[gb->n_visible_objs - 1]];
            gb->n_visible_objs--;
            
            uint16_t line_address = get_object_line_address(gb, object->y, object->tile, object->flags);
            uint8_t data0 = gb->vram[line_address];
            uint8_t data1 = gb->vram[line_address + 1];
            if (object->flags & 0x20) {
                data0 = flip(data0);
                data1 = flip(data1);
            }
            
            typeof(_object_buffer[0]) *p = _object_buffer + object->x;
            if (object->x >= 168) {
                continue;
            }
            unrolled for (unsigned x = 0; x < 8; x++) {
                unsigned pixel = (data0 >> 7) | ((data1 >> 7) << 1);
                data0 <<= 1;
                data1 <<= 1;
                if (!p->pixel) {
                    p->pixel = pixel;
                    p->palette = (object->flags & 0x10) >> 4;
                    p->bg_priority = object->flags & 0x80;
                }
                p++;
            }
        }
    }
    else {
        object_buffer = (const void *)empty_object_buffer;
    }
    
    
    uint8_t *restrict p = gb->sgb->screen_buffer;
    typeof(object_buffer[0]) *object_buffer_pointer = object_buffer + 8;
    p += WIDTH * gb->current_line;
    
    if (unlikely(gb->background_disabled) || (!gb->cgb_mode && !(gb->io_registers[GB_IO_LCDC] & GB_LCDC_BG_EN))) {
        for (unsigned i = 160; i--;) {
            if (unlikely(object_buffer_pointer->pixel)) {
                uint8_t pixel = object_buffer_pointer->pixel;
                pixel = ((gb->io_registers[GB_IO_OBP0 + object_buffer_pointer->palette] >> (pixel << 1)) & 3);
                *(p++) = pixel;
            }
            else {
                *(p++) = gb->io_registers[GB_IO_BGP] & 3;
            }
            object_buffer_pointer++;
        }
        return;
    }
    
    unsigned pixels = 0;
    uint8_t tile_x = gb->io_registers[GB_IO_SCX] / 8;
    unsigned fractional_scroll = gb->io_registers[GB_IO_SCX] & 7;
    uint16_t map = 0x1800;
    if (gb->io_registers[GB_IO_LCDC] & GB_LCDC_BG_MAP) {
        map = 0x1C00;
    }
    uint8_t y = gb->current_line + gb->io_registers[GB_IO_SCY];
    uint8_t attributes;
    uint8_t data0, data1;
    get_tile_data(gb, tile_x, y, map, &attributes, &data0, &data1);
    
#define DO_PIXEL() \
uint8_t pixel = (data0 >> 7) | ((data1 >> 7) << 1);\
data0 <<= 1;\
data1 <<= 1;\
\
if (unlikely(object_buffer_pointer->pixel) && (pixel == 0 || !object_buffer_pointer->bg_priority || !(gb->io_registers[GB_IO_LCDC] & GB_LCDC_BG_EN))) {\
    pixel = object_buffer_pointer->pixel;\
    pixel = ((gb->io_registers[GB_IO_OBP0 + object_buffer_pointer->palette] >> (pixel << 1)) & 3);\
    *(p++) = pixel;\
}\
else {\
    pixel = ((gb->io_registers[GB_IO_BGP] >> (pixel << 1)) & 3);\
    *(p++) = pixel;\
}\
pixels++;\
object_buffer_pointer++\

    // First 1-8 pixels
    data0 <<= fractional_scroll;
    data1 <<= fractional_scroll;
    bool check_window = gb->wy_triggered && (gb->io_registers[GB_IO_LCDC] & GB_LCDC_WIN_ENABLE);
    nounroll for (unsigned i = fractional_scroll; i < 8; i++) {
        if (check_window && gb->io_registers[GB_IO_WX] == pixels + 7) {
        activate_window:
            check_window = false;
            map = gb->io_registers[GB_IO_LCDC] & GB_LCDC_WIN_MAP? 0x1C00 : 0x1800;
            tile_x = -1;
            y = ++gb->window_y;
            break;
        }
        DO_PIXEL();
    }
    tile_x++;
    
    while (pixels < 160 - 8) {
        get_tile_data(gb, tile_x, y, map, &attributes, &data0, &data1);
        nounroll for (unsigned i = 0; i < 8; i++) {
            if (check_window && gb->io_registers[GB_IO_WX] == pixels + 7) {
                goto activate_window;
            }
            DO_PIXEL();
        }
        tile_x++;
    }
    
    get_tile_data(gb, tile_x, y, map, &attributes, &data0, &data1);
    while (pixels < 160) {
        if (check_window && gb->io_registers[GB_IO_WX] == pixels + 7) {
            goto activate_window;
        }
        DO_PIXEL();
    }
}

static inline uint16_t mode3_batching_length(GB_gameboy_t *gb)
{
    if (gb->position_in_line != (uint8_t)-16) return 0;
    if (gb->model & GB_MODEL_NO_SFC_BIT) return 0;
    if (gb->hdma_on) return 0;
    if (gb->stopped) return 0;
    if (GB_is_dma_active(gb)) return 0;
    if (gb->wx_triggered) return 0;
    if (gb->wy_triggered) {
        if (gb->io_registers[GB_IO_LCDC] & GB_LCDC_WIN_ENABLE) {
            if ((gb->io_registers[GB_IO_WX] < 7 || gb->io_registers[GB_IO_WX] == 166 || gb->io_registers[GB_IO_WX] == 167)) {
                return 0;
            }
        }
        else {
            if (gb->io_registers[GB_IO_WX] < 167 && !GB_is_cgb(gb)) {
                return 0;
            }
        }
    }

    // No objects or window, timing is trivial
    if (gb->n_visible_objs == 0 && !(gb->wy_triggered && (gb->io_registers[GB_IO_LCDC] & GB_LCDC_WIN_ENABLE))) return 167 + (gb->io_registers[GB_IO_SCX] & 7);

    if (gb->hdma_on_hblank) return 0;
    
    // 300 is a bit more than the maximum Mode 3 length
    
    // No HBlank interrupt
    if (!(gb->io_registers[GB_IO_STAT] & 0x8)) return 300;
    // No STAT interrupt requested
    if (!(gb->interrupt_enable & 2)) return 300;
    
    
    return 0;
}

static inline uint8_t x_for_object_match(GB_gameboy_t *gb)
{
    uint8_t ret = gb->position_in_line + 8;
    if (ret > (uint8_t)-16) return 0;
    return ret;
}

static void update_frame_parity(GB_gameboy_t *gb)
{
    if (gb->model <= GB_MODEL_CGB_E) {
        gb->is_odd_frame ^= true;
    }
    else {
        // Faster than division, it's normally only once
        while (gb->frame_parity_ticks > LCDC_PERIOD * 2) {
            gb->frame_parity_ticks -= LCDC_PERIOD * 2;
            gb->is_odd_frame ^= true;
        }
    }
}

/*
 TODO: It seems that the STAT register's mode bits are always "late" by 4 T-cycles.
       The PPU logic can be greatly simplified if that delay is simply emulated.
 */
void GB_display_run(GB_gameboy_t *gb, unsigned cycles, bool force)
{
    if (gb->wy_triggered) {
        gb->wy_check_scheduled = false;
    }
    else if (gb->wy_check_scheduled) {
        force = true;
        unsigned cycles_to_check;
        // TODO: When speed-switching while the LCD is on, the modulo might be affected. Odd-modes are going to be fun.
        if (gb->cgb_double_speed) {
            cycles_to_check = (8 - ((gb->wy_check_modulo + 6) & 7));
        }
        else if (GB_is_cgb(gb)) {
            cycles_to_check = (8 - ((gb->wy_check_modulo + 0) & 7));
        }
        else {
            cycles_to_check = (8 - ((gb->wy_check_modulo + 2) & 7));
        }
        
        if (cycles >= cycles_to_check) {
            gb->wy_check_scheduled = false;
            GB_display_run(gb, cycles_to_check, true);
            wy_check(gb);
            if (gb->display_state == 21 && GB_is_cgb(gb) && !gb->cgb_double_speed) {
                gb->wy_just_checked = true;
            }
            cycles -= cycles_to_check;
        }
    }

    if (unlikely((gb->io_registers[GB_IO_LCDC] & GB_LCDC_ENABLE) && (signed)(gb->cycles_for_line * 2 + cycles + gb->display_cycles) > LINE_LENGTH * 2)) {
        unsigned first_batch = (LINE_LENGTH * 2 - gb->cycles_for_line * 2 + gb->display_cycles);
        GB_display_run(gb, first_batch, force);
        cycles -= first_batch;
        if (gb->display_state == 22) {
            gb->io_registers[GB_IO_STAT] &= ~3;
            gb->mode_for_interrupt = 0;
            GB_STAT_update(gb);
        }
        gb->display_state = 9;
        gb->display_cycles = 0;
    }
    if (unlikely(gb->delayed_glitch_hblank_interrupt && cycles && gb->current_line < LINES)) {
        gb->delayed_glitch_hblank_interrupt = false;
        gb->mode_for_interrupt = 0;
        GB_STAT_update(gb);
        gb->mode_for_interrupt = 3;
    }
    gb->cycles_since_vblank_callback += cycles / 2;
    
    gb->frame_parity_ticks += cycles;
    gb->wy_check_modulo += cycles;
    
    if (cycles < gb->frame_repeat_countdown) {
        gb->frame_repeat_countdown -= cycles;
    }
    else {
        gb->frame_repeat_countdown = 0;
    }

    /* The PPU does not advance while in STOP mode on the DMG */
    if (gb->stopped && !GB_is_cgb(gb)) {
        if (gb->cycles_since_vblank_callback >= LCDC_PERIOD) {
            GB_display_vblank(gb, GB_VBLANK_TYPE_ARTIFICIAL);
        }
        return;
    }
        
    GB_BATCHABLE_STATE_MACHINE(gb, display, cycles, 2, !force) {
        GB_STATE(gb, display, 1);
        GB_STATE(gb, display, 2);
        GB_STATE(gb, display, 3);
        GB_STATE(gb, display, 4);
        GB_STATE(gb, display, 5);
        GB_STATE(gb, display, 6);
        GB_STATE(gb, display, 7);
        GB_STATE(gb, display, 8);
        GB_STATE(gb, display, 9);
        GB_STATE(gb, display, 10);
        GB_STATE(gb, display, 11);
        GB_STATE(gb, display, 12);
        GB_STATE(gb, display, 13);
        GB_STATE(gb, display, 14);
        GB_STATE(gb, display, 15);
        GB_STATE(gb, display, 16);
        GB_STATE(gb, display, 17);
        
        GB_STATE(gb, display, 19);
        GB_STATE(gb, display, 20);
        GB_STATE(gb, display, 21);
        GB_STATE(gb, display, 22);
        GB_STATE(gb, display, 23);
        GB_STATE(gb, display, 24);
        GB_STATE(gb, display, 26);
        GB_STATE(gb, display, 27);
        GB_STATE(gb, display, 28);
        GB_STATE(gb, display, 29);
        
        GB_STATE(gb, display, 31);
        GB_STATE(gb, display, 32);
        GB_STATE(gb, display, 33);
        GB_STATE(gb, display, 34);
        GB_STATE(gb, display, 35);
        GB_STATE(gb, display, 36);
        GB_STATE(gb, display, 37);
        GB_STATE(gb, display, 38);
        GB_STATE(gb, display, 39);
        GB_STATE(gb, display, 40);
        GB_STATE(gb, display, 41);
        GB_STATE(gb, display, 42);
        GB_STATE(gb, display, 43);
    }
    
    gb->wy_check_modulo = cycles;
    gb->wy_just_checked = false;
    
    if (!(gb->io_registers[GB_IO_LCDC] & GB_LCDC_ENABLE)) {
        while (true) {
            if (gb->cycles_since_vblank_callback < LCDC_PERIOD) {
                GB_SLEEP(gb, display, 1, LCDC_PERIOD - gb->cycles_since_vblank_callback);
            }
            update_frame_parity(gb); // TODO: test actual timing
            GB_display_vblank(gb, GB_VBLANK_TYPE_LCD_OFF);
        }
        return;
    }
        
    if (!GB_is_cgb(gb)) {
        GB_SLEEP(gb, display, 23, 1);
    }

    /* Handle mode 2 on the very first line 0 */
    gb->current_line = 0;
    gb->window_y = -1;
    gb->wy_triggered = false;
    gb->position_in_line = -16;
    gb->line_has_fractional_scrolling = false;
    
    gb->ly_for_comparison = 0;
    gb->io_registers[GB_IO_STAT] &= ~3;
    gb->mode_for_interrupt = -1;
    gb->oam_read_blocked = false;
    gb->vram_read_blocked = false;
    gb->oam_write_blocked = false;
    gb->vram_write_blocked = false;
    gb->cgb_palettes_blocked = false;
    gb->cycles_for_line = MODE2_LENGTH - 4;
    GB_STAT_update(gb);
    GB_SLEEP(gb, display, 2, MODE2_LENGTH - 4);
    
    gb->oam_write_blocked = true;
    gb->cycles_for_line += 2;
    GB_STAT_update(gb);
    GB_SLEEP(gb, display, 34, 2);
    
    gb->n_visible_objs = 0;
    gb->orig_n_visible_objs = 0;
    gb->cycles_for_line += 8; // Mode 0 is shorter on the first line 0, so we augment cycles_for_line by 8 extra cycles.

    gb->io_registers[GB_IO_STAT] &= ~3;
    gb->io_registers[GB_IO_STAT] |= 3;
    gb->mode_for_interrupt = 3;

    gb->oam_write_blocked = true;
    gb->oam_read_blocked = true;
    gb->vram_read_blocked = gb->cgb_double_speed;
    gb->vram_write_blocked = gb->cgb_double_speed;
    if (!GB_is_cgb(gb)) {
        gb->vram_read_blocked = true;
        gb->vram_write_blocked = true;
    }
    gb->cycles_for_line += 2;
    GB_SLEEP(gb, display, 37, 2);
    
    gb->cgb_palettes_blocked = true;
    gb->cycles_for_line += 3;
    GB_SLEEP(gb, display, 38, 3);
    
    gb->vram_read_blocked = true;
    gb->vram_write_blocked = true;
    gb->wx_triggered = false;
    goto mode_3_start;

    // Mode 3 abort, state 9
    display9: {
        // TODO: Timing of things in this scenario is almost completely untested
        if (gb->current_line < LINES && !GB_is_sgb(gb) && !gb->disable_rendering) {
            GB_log(gb, "The ROM is preventing line %d from fully rendering, this could damage a real device's LCD display.\n", gb->current_line);
            uint32_t *dest = NULL;
            if (gb->border_mode != GB_BORDER_ALWAYS) {
                dest = gb->screen + gb->lcd_x + gb->current_line * WIDTH;
            }
            else {
                dest = gb->screen + gb->lcd_x + gb->current_line * BORDERED_WIDTH + (BORDERED_WIDTH - WIDTH) / 2 + (BORDERED_HEIGHT - LINES) / 2 * BORDERED_WIDTH;
            }
            uint32_t color = GB_is_cgb(gb)? GB_convert_rgb15(gb, 0x7FFF, false) : gb->background_palettes_rgb[4];
            while (gb->lcd_x < 160) {
                *(dest++) = color;
                gb->lcd_x++;
            }
        }
        gb->n_visible_objs = gb->orig_n_visible_objs;
        gb->current_line++;
        wy_check(gb);
        gb->cycles_for_line = 0;
        if (gb->current_line != LINES) {
            gb->cycles_for_line = 2;
            GB_SLEEP(gb, display, 28, 2);
            gb->io_registers[GB_IO_LY] = gb->current_line;
            if (gb->position_in_line >= 156 && gb->position_in_line < (uint8_t)-16) {
                gb->delayed_glitch_hblank_interrupt = true;
            }
            GB_STAT_update(gb);
            gb->position_in_line = -15;
            goto mode_3_start;
        }
        else {
            if (gb->position_in_line >= 156 && gb->position_in_line < (uint8_t)-16) {
                gb->delayed_glitch_hblank_interrupt = true;
            }
            gb->position_in_line = -16;
            gb->line_has_fractional_scrolling = false;
        }
    }
        
    while (true) {
        /* Lines 0 - 143 */
        for (; gb->current_line < LINES; gb->current_line++) {
            wy_check(gb);
            
            if (unlikely(gb->lcd_line_callback)) {
                gb->lcd_line_callback(gb, gb->current_line);
            }
            
            gb->oam_write_blocked = GB_is_cgb(gb) && !gb->cgb_double_speed;
            gb->accessed_oam_row = 0;
            
            GB_SLEEP(gb, display, 35, 2);
            gb->oam_write_blocked = GB_is_cgb(gb);
            
            GB_SLEEP(gb, display, 6, 1);
            gb->io_registers[GB_IO_LY] = gb->current_line;
            gb->oam_read_blocked = !gb->cgb_double_speed || gb->model >= GB_MODEL_CGB_D;
            gb->ly_for_comparison = gb->current_line? -1 : 0;
            
            /* The OAM STAT interrupt occurs 1 T-cycle before STAT actually changes, except on line 0.
             PPU glitch? */
            if (gb->current_line != 0) {
                gb->mode_for_interrupt = 2;
                gb->io_registers[GB_IO_STAT] &= ~3;
            }
            else if (!GB_is_cgb(gb)) {
                gb->io_registers[GB_IO_STAT] &= ~3;
            }
            GB_STAT_update(gb);

            GB_SLEEP(gb, display, 7, 1);
            gb->oam_read_blocked = true;
            gb->io_registers[GB_IO_STAT] &= ~3;
            gb->io_registers[GB_IO_STAT] |= 2;
            gb->mode_for_interrupt = 2;
            gb->oam_write_blocked = true;
            gb->ly_for_comparison = gb->current_line;
            wy_check(gb);
            
            GB_STAT_update(gb);
            gb->mode_for_interrupt = -1;
            GB_STAT_update(gb);
            gb->n_visible_objs = 0;
            gb->orig_n_visible_objs = 0;
            
            if (!GB_is_dma_active(gb) && !gb->oam_ppu_blocked) {
                GB_BATCHPOINT(gb, display, 5, 80);
            }
            for (gb->oam_search_index = 0; gb->oam_search_index < 40; gb->oam_search_index++) {
                if (GB_is_cgb(gb)) {
                    add_object_from_index(gb, gb->oam_search_index);
                    /* The CGB does not care about the accessed OAM row as there's no OAM bug */
                }
                GB_SLEEP(gb, display, 8, 2);
                if (!GB_is_cgb(gb)) {
                    add_object_from_index(gb, gb->oam_search_index);
                    gb->accessed_oam_row = (gb->oam_search_index & ~1) * 4 + 8;
                }
                if (gb->oam_search_index == 37) {
                    gb->vram_read_blocked = !GB_is_cgb(gb);
                    gb->vram_write_blocked = false;
                    gb->cgb_palettes_blocked = false;
                    gb->oam_write_blocked = GB_is_cgb(gb);
                }
            }
            gb->cycles_for_line = MODE2_LENGTH + 4;
            gb->orig_n_visible_objs = gb->n_visible_objs;
            gb->accessed_oam_row = -1;
            gb->io_registers[GB_IO_STAT] &= ~3;
            gb->io_registers[GB_IO_STAT] |= 3;
            gb->mode_for_interrupt = 3;
            gb->vram_read_blocked = true;
            gb->vram_write_blocked = true;
            gb->cgb_palettes_blocked = false;
            gb->oam_write_blocked = true;
            gb->oam_read_blocked = true;

            GB_STAT_update(gb);

            
            gb->cycles_for_line += 3;
            GB_SLEEP(gb, display, 10, 3);
            
            gb->cgb_palettes_blocked = true;
            gb->cycles_for_line += 2;
            GB_SLEEP(gb, display, 32, 2);
        mode_3_start:
            gb->disable_window_pixel_insertion_glitch = false;

            fifo_clear(&gb->bg_fifo);
            fifo_clear(&gb->oam_fifo);
            /* Fill the FIFO with 8 pixels of "junk", it's going to be dropped anyway. */
            fifo_push_bg_row(&gb->bg_fifo, 0, 0, 0, false, false);
            gb->lcd_x = 0;
            
            /* The actual rendering cycle */
            gb->fetcher_state = GB_FETCHER_GET_TILE_T1;
            if ((gb->mode3_batching_length = mode3_batching_length(gb))) {
                GB_BATCHPOINT(gb, display, 3, gb->mode3_batching_length);
                if (GB_BATCHED_CYCLES(gb, display) >= gb->mode3_batching_length) {
                    // Successfully batched!
                    gb->lcd_x = gb->position_in_line = 160;
                    gb->cycles_for_line += gb->mode3_batching_length;
                    if (gb->sgb) {
                        render_line_sgb(gb);
                    }
                    else {
                        render_line(gb);
                    }
                    GB_SLEEP(gb, display, 4, gb->mode3_batching_length);
                    goto skip_slow_mode_3;
                }
            }
            while (true) {
                /* Handle window */
                /* TODO: It appears that WX checks if the window begins *next* pixel, not *this* pixel. For this reason,
                   WX=167 has no effect at all (It checks if the PPU X position is 161, which never happens) and WX=166
                   has weird artifacts (It appears to activate the window during HBlank, as PPU X is temporarily 160 at
                   that point. The code should be updated to represent this, and this will fix the time travel hack in
                   WX's access conflict code. */
                gb->wx_166_interrupt_glitch = false;
                if (unlikely(gb->wy_just_checked)) {
                    gb->wy_just_checked = false;
                }
                else if (!gb->wx_triggered && gb->wy_triggered && (gb->io_registers[GB_IO_LCDC] & GB_LCDC_WIN_ENABLE)) {
                    bool should_activate_window = false;
                    if (unlikely(gb->io_registers[GB_IO_WX] == 0)) {
                        if (gb->position_in_line == (uint8_t)-7) {
                            should_activate_window = true;
                        }
                        else if (gb->position_in_line == (uint8_t)-16 && (gb->io_registers[GB_IO_SCX] & 7)) {
                            should_activate_window = true;
                        }
                        else if (gb->position_in_line >= (uint8_t)-15 && gb->position_in_line <= (uint8_t)-8) {
                            should_activate_window = true;
                        }
                    }
                    else if (gb->io_registers[GB_IO_WX] < 166 + GB_is_cgb(gb)) { // TODO: 166 on the CGB behaves a bit weird
                        if (gb->io_registers[GB_IO_WX] == (uint8_t) (gb->position_in_line + 7)) {
                            should_activate_window = true;
                        }
                        else if (!GB_is_cgb(gb) && gb->io_registers[GB_IO_WX] == (uint8_t) (gb->position_in_line + 6) && !gb->wx_just_changed) {
                            should_activate_window = true;
                            /* LCD-PPU horizontal desync! It only appears to happen on DMGs, but not all of them.
                               This doesn't seem to be CPU revision dependent, but most revisions */
                            if ((gb->model & GB_MODEL_FAMILY_MASK) == GB_MODEL_DMG_FAMILY && !GB_is_sgb(gb)) {
                                if (gb->lcd_x > 0) {
                                    gb->lcd_x--;
                                }
                            }
                        }
                    }
                    
                    if (should_activate_window) {
                        gb->window_y++;
                        gb->window_tile_x = 0;
                        fifo_clear(&gb->bg_fifo);
                        /* TODO: Verify fetcher access timings in this case */
                        if (gb->io_registers[GB_IO_WX] == 0 && (gb->io_registers[GB_IO_SCX] & 7) && !GB_is_cgb(gb)) {
                            gb->cycles_for_line += 1;
                            GB_SLEEP(gb, display, 42, 1);
                        }
                        else if (gb->io_registers[GB_IO_WX] == 166) {
                            gb->wx_166_interrupt_glitch = true;
                        }
                        gb->wx_triggered = true;
                        gb->fetcher_state = GB_FETCHER_GET_TILE_T1;
                        gb->window_is_being_fetched = true;
                    }
                    else if (!GB_is_cgb(gb) && gb->io_registers[GB_IO_WX] == 166 && gb->io_registers[GB_IO_WX] == (uint8_t) (gb->position_in_line + 7)) {
                        gb->window_y++;
                    }
                }

                if (unlikely(gb->io_registers[GB_IO_WX] == (uint8_t) (gb->position_in_line + 7) &&
                             (!GB_is_cgb(gb) || gb->io_registers[GB_IO_WX] == 0) &&
                             gb->wx_triggered && !gb->window_is_being_fetched &&
                             gb->fetcher_state == GB_FETCHER_GET_TILE_T1 &&
                             gb->bg_fifo.size == 8)) {
                    // Insert a pixel right at the FIFO's end
                    gb->insert_bg_pixel = true;
                }
                
                /* Handle objects */
                /* When the object enabled bit is off, this proccess is skipped entirely on the DMG, but not on the CGB.
                   On the CGB, this bit is checked only when the pixel is actually popped from the FIFO. */
                                
                while (gb->n_visible_objs != 0 &&
                       gb->objects_x[gb->n_visible_objs - 1] < x_for_object_match(gb)) {
                    gb->n_visible_objs--;
                }
                
                gb->during_object_fetch = true;
                while (gb->n_visible_objs != 0 &&
                       (gb->io_registers[GB_IO_LCDC] & GB_LCDC_OBJ_EN || GB_is_cgb(gb)) &&
                       gb->objects_x[gb->n_visible_objs - 1] == x_for_object_match(gb)) {
                    
                    while (gb->fetcher_state < GB_FETCHER_GET_TILE_DATA_HIGH_T2 || fifo_size(&gb->bg_fifo) == 0) {
                        advance_fetcher_state_machine(gb, &cycles);
                        gb->cycles_for_line++;
                        GB_SLEEP(gb, display, 27, 1);
                        if (gb->object_fetch_aborted) {
                            goto abort_fetching_object;
                        }
                    }
                    
                    /* TODO: Can this be deleted?  { */
                    advance_fetcher_state_machine(gb, &cycles);
                    gb->cycles_for_line++;
                    GB_SLEEP(gb, display, 41, 1);
                    if (gb->object_fetch_aborted) {
                        goto abort_fetching_object;
                    }
                    /* } */
                    
                    advance_fetcher_state_machine(gb, &cycles);
                    dma_sync(gb, &cycles);
                    gb->mode2_y_bus = oam_read(gb, gb->visible_objs[gb->n_visible_objs - 1] * 4 + 2);
                    gb->object_flags = oam_read(gb, gb->visible_objs[gb->n_visible_objs - 1] * 4 + 3);
                                        
                    gb->cycles_for_line += 2;
                    GB_SLEEP(gb, display, 20, 2);
                    if (gb->object_fetch_aborted) {
                        goto abort_fetching_object;
                    }
                    
                    /* TODO: timing not verified */
                    dma_sync(gb, &cycles);
                    gb->object_low_line_address = get_object_line_address(gb,
                                                                          gb->objects_y[gb->n_visible_objs - 1],
                                                                          gb->mode2_y_bus,
                                                                          gb->object_flags);
                    gb->object_tile_data[0] = vram_read(gb, gb->object_low_line_address);

                    
                    gb->cycles_for_line += 2;
                    GB_SLEEP(gb, display, 39, 2);
                    if (gb->object_fetch_aborted) {
                        goto abort_fetching_object;
                    }
                    
                    gb->during_object_fetch = false;
                    gb->cycles_for_line++;
                    gb->object_low_line_address = get_object_line_address(gb,
                                                                          gb->objects_y[gb->n_visible_objs - 1],
                                                                          gb->mode2_y_bus,
                                                                          gb->object_flags);

                    dma_sync(gb, &cycles);
                    gb->object_tile_data[1] = vram_read(gb, gb->object_low_line_address + 1);
                    GB_SLEEP(gb, display, 40, 1);

                    
                    uint8_t palette = (gb->object_flags & 0x10) ? 1 : 0;
                    if (gb->cgb_mode) {
                        palette = gb->object_flags & 0x7;
                    }
                    fifo_overlay_object_row(&gb->oam_fifo,
                                            gb->object_tile_data[0],
                                            gb->object_tile_data[1],
                                            palette,
                                            gb->object_flags & 0x80,
                                            gb->object_priority == GB_OBJECT_PRIORITY_INDEX? gb->visible_objs[gb->n_visible_objs - 1] : 0,
                                            gb->object_flags & 0x20);

                    gb->data_for_sel_glitch = gb->vram_ppu_blocked? 0xFF : gb->vram[gb->object_low_line_address + 1];
                    gb->n_visible_objs--;
                }
                
abort_fetching_object:
                gb->object_fetch_aborted = false;
                gb->during_object_fetch = false;
                
                render_pixel_if_possible(gb);
                advance_fetcher_state_machine(gb, &cycles);
                if (gb->position_in_line == 160) break;

                gb->cycles_for_line++;
                GB_SLEEP(gb, display, 21, 1);
                if (unlikely(gb->wx_166_interrupt_glitch)) {
                    gb->mode_for_interrupt = 0;
                    GB_STAT_update(gb);
                }
            }
skip_slow_mode_3:
            gb->position_in_line = -16;
            gb->line_has_fractional_scrolling = false;
            

            /* TODO: Commented code seems incorrect (glitches Tesserae), verify further */

            if (gb->fetcher_state == GB_FETCHER_GET_TILE_DATA_HIGH_T1 ||
                gb->fetcher_state == GB_FETCHER_GET_TILE_DATA_HIGH_T2) {
                // Make sure current_tile_data[1] holds the last tile data byte read
                gb->current_tile_data[1] = gb->current_tile_data[0];
                
                //gb->data_for_sel_glitch = gb->current_tile_data[0];
            }
            /*
            else {
                gb->data_for_sel_glitch = gb->current_tile_data[1];
            }
            */
            while (gb->lcd_x != 160 && !gb->disable_rendering && gb->screen && !gb->sgb) {
                /* Oh no! The PPU and LCD desynced! Fill the rest of the line with the last color. */
                uint32_t *dest = NULL;
                if (gb->border_mode != GB_BORDER_ALWAYS) {
                    dest = gb->screen + gb->lcd_x + gb->current_line * WIDTH;
                }
                else {
                    dest = gb->screen + gb->lcd_x + gb->current_line * BORDERED_WIDTH + (BORDERED_WIDTH - WIDTH) / 2 + (BORDERED_HEIGHT - LINES) / 2 * BORDERED_WIDTH;
                }
                *dest = (gb->lcd_x == 0)? gb->background_palettes_rgb[0] : dest[-1];
                gb->lcd_x++;

            }
            
            /* TODO: Verify timing { */
            if (gb->current_line == 143) {
                gb->window_y = -1;
            }
            if (!GB_is_cgb(gb) && gb->wy_triggered && (gb->io_registers[GB_IO_LCDC] & GB_LCDC_WIN_ENABLE) && gb->io_registers[GB_IO_WX] == 166) {
                gb->wx_triggered = true;
                gb->window_tile_x = 1;
                gb->window_y++;
            }
            else {
                gb->wx_triggered = false;
            }
            /* } */
            
            if (!gb->cgb_double_speed) {
                gb->io_registers[GB_IO_STAT] &= ~3;
                gb->mode_for_interrupt = 0;
                gb->oam_read_blocked = gb->model >= GB_MODEL_CGB_D;
                gb->vram_read_blocked = false;
                gb->oam_write_blocked = false;
                gb->vram_write_blocked = false;
            }
            
            gb->cycles_for_line++;
            GB_SLEEP(gb, display, 22, 1);
            
            gb->io_registers[GB_IO_STAT] &= ~3;
            gb->mode_for_interrupt = 0;
            gb->oam_read_blocked = false;
            gb->vram_read_blocked = false;
            gb->oam_write_blocked = false;
            gb->vram_write_blocked = false;
            GB_STAT_update(gb);

            /* Todo: Measure this value */
            gb->cycles_for_line += 2;
            GB_SLEEP(gb, display, 33, 2);
            gb->cgb_palettes_blocked = !gb->cgb_double_speed;
            
            if (gb->hdma_on_hblank && !gb->halted && !gb->stopped) {
                gb->hdma_on = true;
            }
            
            gb->cycles_for_line += 2;
            GB_SLEEP(gb, display, 36, 2);
            gb->cgb_palettes_blocked = false;
            
            if (gb->cycles_for_line > LINE_LENGTH - 2) {
                gb->cycles_for_line = 0;
                GB_SLEEP(gb, display, 43, LINE_LENGTH - gb->cycles_for_line);
                goto display9;
            }
            
            {
                uint16_t cycles_for_line =  gb->cycles_for_line;
                gb->cycles_for_line = 0;
                GB_SLEEP(gb, display, 11, LINE_LENGTH - cycles_for_line - 2);
            }
            
            gb->cycles_for_line = 0;
            GB_SLEEP(gb, display, 31, 2);
            if (gb->current_line != LINES - 1) {
                gb->mode_for_interrupt = 2;
            }
          
            // Todo: unverified timing
            gb->current_lcd_line++;
            if (gb->current_lcd_line == LINES && GB_is_sgb(gb)) {
                GB_display_vblank(gb, GB_VBLANK_TYPE_NORMAL_FRAME);
            }
            
            if (gb->icd_hreset_callback) {
                gb->icd_hreset_callback(gb);
            }
        }
        /* Lines 144 - 152 */
        for (; gb->current_line < VIRTUAL_LINES - 1; gb->current_line++) {
            gb->ly_for_comparison = -1;
            if (unlikely(gb->lcd_line_callback)) {
                gb->lcd_line_callback(gb, gb->current_line);
            }
            GB_STAT_update(gb);
            GB_SLEEP(gb, display, 26, 2);
            gb->io_registers[GB_IO_LY] = gb->current_line;
            if (gb->current_line == LINES && !gb->stat_interrupt_line && (gb->io_registers[GB_IO_STAT] & 0x20)) {
                gb->io_registers[GB_IO_IF] |= 2;
            }
            GB_SLEEP(gb, display, 12, 2);
            if (gb->delayed_glitch_hblank_interrupt) {
                gb->delayed_glitch_hblank_interrupt = false;
                gb->mode_for_interrupt = 0;
            }
            gb->ly_for_comparison = gb->current_line;
            GB_STAT_update(gb);
            GB_SLEEP(gb, display, 24, 1);

            if (gb->current_line == LINES) {
                /* Entering VBlank state triggers the OAM interrupt */
                gb->io_registers[GB_IO_STAT] &= ~3;
                gb->io_registers[GB_IO_STAT] |= 1;
                gb->io_registers[GB_IO_IF] |= 1;
                if (!gb->stat_interrupt_line && (gb->io_registers[GB_IO_STAT] & 0x20)) {
                    gb->io_registers[GB_IO_IF] |= 2;
                }
                gb->mode_for_interrupt = 1;
                GB_STAT_update(gb);
                
                if (gb->frame_skip_state == GB_FRAMESKIP_LCD_TURNED_ON) {
                    if (GB_is_cgb(gb)) {
                        GB_display_vblank(gb, GB_VBLANK_TYPE_NORMAL_FRAME);
                        gb->frame_skip_state = GB_FRAMESKIP_FIRST_FRAME_RENDERED;
                    }
                    else {
                        if (!GB_is_sgb(gb) || gb->current_lcd_line < LINES) {
                            update_frame_parity(gb); // TODO: test actual timing
                            GB_display_vblank(gb, GB_VBLANK_TYPE_NORMAL_FRAME);
                        }
                        gb->frame_skip_state = GB_FRAMESKIP_FIRST_FRAME_RENDERED;
                    }
                }
                else {
                    if (!GB_is_sgb(gb) || gb->current_lcd_line < LINES) {
                        update_frame_parity(gb); // TODO: test actual timing
                        GB_display_vblank(gb, GB_VBLANK_TYPE_NORMAL_FRAME);
                    }
                }
            }
            
            /* 3640 is just a few cycles less than 4 lines, no clue where the
               AGB constant comes from (These are measured and confirmed)  */
            gb->frame_repeat_countdown = LINES * LINE_LENGTH * 2 + (gb->model > GB_MODEL_CGB_E? 5982 : 3640); // 8MHz units
            if (gb->display_cycles < gb->frame_repeat_countdown) {
                gb->frame_repeat_countdown -= gb->display_cycles;
            }
            else {
                gb->frame_repeat_countdown = 0;
            }
            
            GB_SLEEP(gb, display, 13, LINE_LENGTH - 5);
        }
        
        /* Lines 153 */
        gb->ly_for_comparison = -1;
        GB_STAT_update(gb);
        GB_SLEEP(gb, display, 19, 2);
        gb->io_registers[GB_IO_LY] = 153;
        GB_SLEEP(gb, display, 14, (gb->model > GB_MODEL_CGB_C)? 2: 4);
        
        if (gb->model <= GB_MODEL_CGB_C && !gb->cgb_double_speed) {
            gb->io_registers[GB_IO_LY] = 0;
        }
        gb->ly_for_comparison = 153;
        GB_STAT_update(gb);
        GB_SLEEP(gb, display, 15, (gb->model > GB_MODEL_CGB_C)? 4: 2);
        
        gb->io_registers[GB_IO_LY] = 0;
        gb->ly_for_comparison = (gb->model > GB_MODEL_CGB_C || gb->cgb_double_speed)? 153 : -1;
        GB_STAT_update(gb);
        GB_SLEEP(gb, display, 16, 4);
        
        gb->ly_for_comparison = 0;
        GB_STAT_update(gb);
        GB_SLEEP(gb, display, 29, 12); /* Writing to LYC during this period on a CGB has side effects */
        GB_SLEEP(gb, display, 17, LINE_LENGTH - 24);
        
        
        gb->current_line = 0;
        gb->wy_triggered = false;
        
        // TODO: not the correct timing
        gb->current_lcd_line = 0;
        if (gb->icd_vreset_callback) {
            gb->icd_vreset_callback(gb);
        }
    }
}

void GB_draw_tileset(GB_gameboy_t *gb, uint32_t *dest, GB_palette_type_t palette_type, uint8_t palette_index)
{
    uint32_t none_palette[4];
    uint32_t *palette = NULL;
    
    switch (GB_is_cgb(gb)? palette_type : GB_PALETTE_NONE) {
        default:
        case GB_PALETTE_NONE:
            none_palette[0] = gb->rgb_encode_callback(gb, 0xFF, 0xFF, 0xFF);
            none_palette[1] = gb->rgb_encode_callback(gb, 0xAA, 0xAA, 0xAA);
            none_palette[2] = gb->rgb_encode_callback(gb, 0x55, 0x55, 0x55);
            none_palette[3] = gb->rgb_encode_callback(gb, 0,    0,    0   );
            palette = none_palette;
            break;
        case GB_PALETTE_BACKGROUND:
            palette = gb->background_palettes_rgb + (4 * (palette_index & 7));
            break;
        case GB_PALETTE_OAM:
            palette = gb->object_palettes_rgb + (4 * (palette_index & 7));
            break;
    }
    
    for (unsigned y = 0; y < 192; y++) {
        for (unsigned x = 0; x < 256; x++) {
            if (x >= 128 && !GB_is_cgb(gb)) {
                *(dest++) = gb->background_palettes_rgb[0];
                continue;
            }
            uint16_t tile = (x % 128) / 8 + y / 8 * 16;
            uint16_t tile_address = tile * 0x10 + (x >= 128? 0x2000 : 0);
            uint8_t pixel = (((gb->vram[tile_address + (y & 7) * 2    ] >> ((~x)&7)) & 1 ) |
                             ((gb->vram[tile_address + (y & 7) * 2 + 1] >> ((~x)&7)) & 1) << 1);
            
            if (!gb->cgb_mode) {
                if (palette_type == GB_PALETTE_BACKGROUND) {
                    pixel = ((gb->io_registers[GB_IO_BGP] >> (pixel << 1)) & 3);
                }
                else if (!gb->cgb_mode) {
                    if (palette_type == GB_PALETTE_OAM) {
                        pixel = ((gb->io_registers[palette_index == 0? GB_IO_OBP0 : GB_IO_OBP1] >> (pixel << 1)) & 3);
                    }
                }
            }
            
            
            *(dest++) = palette[pixel];
        }
    }
}

void GB_draw_tilemap(GB_gameboy_t *gb, uint32_t *dest, GB_palette_type_t palette_type, uint8_t palette_index, GB_map_type_t map_type, GB_tileset_type_t tileset_type)
{
    uint32_t none_palette[4];
    uint32_t *palette = NULL;
    uint16_t map = 0x1800;
    
    switch (GB_is_cgb(gb)? palette_type : GB_PALETTE_NONE) {
        case GB_PALETTE_NONE:
            none_palette[0] = gb->rgb_encode_callback(gb, 0xFF, 0xFF, 0xFF);
            none_palette[1] = gb->rgb_encode_callback(gb, 0xAA, 0xAA, 0xAA);
            none_palette[2] = gb->rgb_encode_callback(gb, 0x55, 0x55, 0x55);
            none_palette[3] = gb->rgb_encode_callback(gb, 0,    0,    0   );
            palette = none_palette;
            break;
        case GB_PALETTE_BACKGROUND:
            palette = gb->background_palettes_rgb + (4 * (palette_index & 7));
            break;
        case GB_PALETTE_OAM:
            palette = gb->object_palettes_rgb + (4 * (palette_index & 7));
            break;
        case GB_PALETTE_AUTO:
            break;
    }
    
    if (map_type == GB_MAP_9C00 || (map_type == GB_MAP_AUTO && gb->io_registers[GB_IO_LCDC] & GB_LCDC_BG_MAP)) {
        map = 0x1C00;
    }
    
    if (tileset_type == GB_TILESET_AUTO) {
        tileset_type = (gb->io_registers[GB_IO_LCDC] & GB_LCDC_TILE_SEL)? GB_TILESET_8800 : GB_TILESET_8000;
    }
    
    for (unsigned y = 0; y < 256; y++) {
        for (unsigned x = 0; x < 256; x++) {
            uint8_t tile = gb->vram[map + x/8 + y/8 * 32];
            uint16_t tile_address;
            uint8_t attributes = 0;
            
            if (tileset_type == GB_TILESET_8800) {
                tile_address = tile * 0x10;
            }
            else {
                tile_address = (int8_t) tile * 0x10 + 0x1000;
            }
            
            if (gb->cgb_mode) {
                attributes = gb->vram[map + x/8 + y/8 * 32 + 0x2000];
            }
            
            if (attributes & 0x8) {
                tile_address += 0x2000;
            }
            
            uint8_t pixel = (((gb->vram[tile_address + (((attributes & 0x40)? ~y : y) & 7) * 2    ] >> (((attributes & 0x20)? x : ~x)&7)) & 1 ) |
                             ((gb->vram[tile_address + (((attributes & 0x40)? ~y : y) & 7) * 2 + 1] >> (((attributes & 0x20)? x : ~x)&7)) & 1) << 1);
            
            if (!gb->cgb_mode && (palette_type == GB_PALETTE_BACKGROUND || palette_type == GB_PALETTE_AUTO)) {
                pixel = ((gb->io_registers[GB_IO_BGP] >> (pixel << 1)) & 3);
            }
            
            if (palette) {
                *(dest++) = palette[pixel];
            }
            else {
                *(dest++) = gb->background_palettes_rgb[(attributes & 7) * 4 + pixel];
            }
        }
    }
}

uint8_t GB_get_oam_info(GB_gameboy_t *gb, GB_oam_info_t *dest, uint8_t *object_height)
{
    uint8_t count = 0;
    *object_height = (gb->io_registers[GB_IO_LCDC] & GB_LCDC_OBJ_SIZE) ? 16:8;
    uint8_t oam_to_dest_index[40] = {0,};
    for (signed y = 0; y < LINES; y++) {
        object_t *object = (object_t *) &gb->oam;
        uint8_t objects_in_line = 0;
        bool obscured = false;
        for (uint8_t i = 0; i < 40; i++, object++) {
            signed object_y = object->y - 16;
            // Is object not in this line?
            if (object_y > y || object_y + *object_height <= y) continue;
            if (++objects_in_line == 11) obscured = true;
            
            GB_oam_info_t *info = NULL;
            if (!oam_to_dest_index[i]) {
                info = dest + count;
                oam_to_dest_index[i] = ++count;
                info->x = object->x;
                info->y = object->y;
                info->tile = *object_height == 16? object->tile & 0xFE : object->tile;
                info->flags = object->flags;
                info->obscured_by_line_limit = false;
                info->oam_addr = 0xFE00 + i * sizeof(*object);
            }
            else {
                info = dest + oam_to_dest_index[i] - 1;
            }
            info->obscured_by_line_limit |= obscured;
        }
    }
    
    for (unsigned i = 0; i < count; i++) {
        uint16_t vram_address = dest[i].tile * 0x10;
        uint8_t flags = dest[i].flags;
        uint8_t palette = gb->cgb_mode? (flags & 7) : ((flags & 0x10)? 1 : 0);
        if (GB_is_cgb(gb) && (flags & 0x8)) {
            vram_address += 0x2000;
        }

        for (unsigned y = 0; y < *object_height; y++) {
            unrolled for (unsigned x = 0; x < 8; x++) {
                uint8_t color = (((gb->vram[vram_address    ] >> ((~x)&7)) & 1 ) |
                                 ((gb->vram[vram_address + 1] >> ((~x)&7)) & 1) << 1 );
                
                if (!gb->cgb_mode) {
                    color = (gb->io_registers[palette? GB_IO_OBP1:GB_IO_OBP0] >> (color << 1)) & 3;
                }
                dest[i].image[((flags & 0x20)?7-x:x) + ((flags & 0x40)?*object_height - 1 -y:y) * 8] = gb->object_palettes_rgb[palette * 4 + color];
            }
            vram_address += 2;
        }
    }
    return count;
}


bool GB_is_odd_frame(GB_gameboy_t *gb)
{
    return gb->is_odd_frame;
}

void GB_set_object_rendering_disabled(GB_gameboy_t *gb, bool disabled)
{
    gb->objects_disabled = disabled;
}

void GB_set_background_rendering_disabled(GB_gameboy_t *gb, bool disabled)
{
    gb->background_disabled = disabled;
}

bool GB_is_object_rendering_disabled(GB_gameboy_t *gb)
{
    return gb->objects_disabled;
}

bool GB_is_background_rendering_disabled(GB_gameboy_t *gb)
{
    return gb->background_disabled;
}

