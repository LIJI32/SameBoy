#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "gb.h"

/* FIFO functions */

static inline unsigned fifo_size(GB_fifo_t *fifo)
{
    return (fifo->write_end - fifo->read_end) & (GB_FIFO_LENGTH - 1);
}

static void fifo_clear(GB_fifo_t *fifo)
{
    fifo->read_end = fifo->write_end = 0;
}

static GB_fifo_item_t *fifo_pop(GB_fifo_t *fifo)
{
    GB_fifo_item_t *ret = &fifo->fifo[fifo->read_end];
    fifo->read_end++;
    fifo->read_end &= (GB_FIFO_LENGTH - 1);
    return ret;
}

static void fifo_push_bg_row(GB_fifo_t *fifo, uint8_t lower, uint8_t upper, uint8_t palette, bool bg_priority, bool flip_x)
{
    if (!flip_x) {
        for (unsigned i = 8; i--;) {
            fifo->fifo[fifo->write_end] = (GB_fifo_item_t) {
                (lower >> 7) | ((upper >> 7) << 1),
                palette,
                0,
                bg_priority,
            };
            lower <<= 1;
            upper <<= 1;
            
            fifo->write_end++;
            fifo->write_end &= (GB_FIFO_LENGTH - 1);
        }
    }
    else {
        for (unsigned i = 8; i--;) {
            fifo->fifo[fifo->write_end] = (GB_fifo_item_t) {
                (lower & 1) | ((upper & 1) << 1),
                palette,
                0,
                bg_priority,
            };
            lower >>= 1;
            upper >>= 1;
            
            fifo->write_end++;
            fifo->write_end &= (GB_FIFO_LENGTH - 1);
        }
    }
}

static void fifo_overlay_object_row(GB_fifo_t *fifo, uint8_t lower, uint8_t upper, uint8_t palette, bool bg_priority, uint8_t priority, bool flip_x)
{
    while (fifo_size(fifo) < 8) {
        fifo->fifo[fifo->write_end] = (GB_fifo_item_t) {0,};
        fifo->write_end++;
        fifo->write_end &= (GB_FIFO_LENGTH - 1);
    }
    
    uint8_t flip_xor = flip_x? 0: 0x7;
    
    for (unsigned i = 8; i--;) {
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
 Each line is 456 cycles. Without scrolling, sprites or a window::
 Mode 2 - 80  cycles / OAM Transfer
 Mode 3 - 172 cycles / Rendering
 Mode 0 - 204 cycles / HBlank
 
 Mode 1 is VBlank
 */

/* Todo: Clean up the glitched line 0 and get rid of these defines */
#define MODE2_LENGTH (80)
#define MODE3_LENGTH (172)
#define MODE0_LENGTH (204)

#define LINE_LENGTH (MODE2_LENGTH + MODE3_LENGTH + MODE0_LENGTH) // = 456
#define LINES (144)
#define WIDTH (160)
#define FRAME_LENGTH (LCDC_PERIOD)
#define VIRTUAL_LINES (FRAME_LENGTH / LINE_LENGTH) // = 154

typedef struct __attribute__((packed)) {
    uint8_t y;
    uint8_t x;
    uint8_t tile;
    uint8_t flags;
} GB_object_t;

static bool window_enabled(GB_gameboy_t *gb)
{
    if ((gb->io_registers[GB_IO_LCDC] & 0x1) == 0) {
        if (!gb->cgb_mode && gb->is_cgb) {
            return false;
        }
    }
    return (gb->io_registers[GB_IO_LCDC] & 0x20) && gb->io_registers[GB_IO_WX] < 167;
}

static void display_vblank(GB_gameboy_t *gb)
{  
    gb->vblank_just_occured = true;

    if (gb->turbo) {
        if (GB_timing_sync_turbo(gb)) {
            return;
        }
    }
    
    if (!gb->disable_rendering && ((!(gb->io_registers[GB_IO_LCDC] & 0x80) || gb->stopped) || gb->frame_skip_state == GB_FRAMESKIP_LCD_TURNED_ON)) {
        /* LCD is off, set screen to white or black (if LCD is on in stop mode) */
        uint32_t color = (gb->io_registers[GB_IO_LCDC] & 0x80)  && gb->stopped ?
                            gb->rgb_encode_callback(gb, 0, 0, 0) :
                            gb->rgb_encode_callback(gb, 0xFF, 0xFF, 0xFF);
        for (unsigned i = 0; i < WIDTH * LINES; i++) {
            gb ->screen[i] = color;
        }
    }

    gb->vblank_callback(gb);
    GB_timing_sync(gb);
}

static inline uint8_t scale_channel(uint8_t x)
{
    return (x << 3) | (x >> 2);
}

static inline uint8_t scale_channel_with_curve(uint8_t x)
{
    return (uint8_t[]){0,2,4,7,12,18,25,34,42,52,62,73,85,97,109,121,134,146,158,170,182,193,203,213,221,230,237,243,248,251,253,255,}[x];
}

uint32_t GB_convert_rgb15(GB_gameboy_t *gb, uint16_t color)
{
    uint8_t r = (color) & 0x1F;
    uint8_t g = (color >> 5) & 0x1F;
    uint8_t b = (color >> 10) & 0x1F;
    
    if (gb->color_correction_mode == GB_COLOR_CORRECTION_DISABLED) {
        r = scale_channel(r);
        g = scale_channel(g);
        b = scale_channel(b);
    }
    else {
        r = scale_channel_with_curve(r);
        g = scale_channel_with_curve(g);
        b = scale_channel_with_curve(b);
        
        if (gb->color_correction_mode != GB_COLOR_CORRECTION_CORRECT_CURVES) {
            uint8_t new_g = (g * 3 + b) / 4;
            uint8_t new_r = r, new_b = b;
            if (gb->color_correction_mode == GB_COLOR_CORRECTION_PRESERVE_BRIGHTNESS) {
                uint8_t old_max = MAX(r, MAX(g, b));
                uint8_t new_max = MAX(new_r, MAX(new_g, new_b));
                
                if (new_max != 0) {
                    new_r = new_r * old_max / new_max;
                    new_g = new_g * old_max / new_max;
                    new_b = new_b * old_max / new_max;
                }
                
                uint8_t old_min = MIN(r, MIN(g, b));
                uint8_t new_min = MIN(new_r, MIN(new_g, new_b));
                
                if (new_min != 0xff) {
                    new_r = 0xff - (0xff - new_r) * (0xff - old_min) / (0xff - new_min);
                    new_g = 0xff - (0xff - new_g) * (0xff - old_min) / (0xff - new_min);
                    new_b = 0xff - (0xff - new_b) * (0xff - old_min) / (0xff - new_min);;
                }
            }
            r = new_r;
            g = new_g;
            b = new_b;
        }
    }
    
    return gb->rgb_encode_callback(gb, r, g, b);
}

void GB_palette_changed(GB_gameboy_t *gb, bool background_palette, uint8_t index)
{
    if (!gb->rgb_encode_callback || !gb->is_cgb) return;
    uint8_t *palette_data = background_palette? gb->background_palettes_data : gb->sprite_palettes_data;
    uint16_t color = palette_data[index & ~1] | (palette_data[index | 1] << 8);

    (background_palette? gb->background_palettes_rgb : gb->sprite_palettes_rgb)[index / 2] = GB_convert_rgb15(gb, color);
}

void GB_set_color_correction_mode(GB_gameboy_t *gb, GB_color_correction_mode_t mode)
{
    gb->color_correction_mode = mode;
    if (gb->is_cgb) {
        for (unsigned i = 0; i < 32; i++) {
            GB_palette_changed(gb, false, i * 2);
            GB_palette_changed(gb, true, i * 2);
        }
    }
}

/*
 STAT interrupt is implemented based on this finding:
 http://board.byuu.org/phpbb3/viewtopic.php?p=25527#p25531
 
 General timing is based on GiiBiiAdvance's documents:
 https://github.com/AntonioND/giibiiadvance
 
 */

static void trigger_oam_interrupt(GB_gameboy_t *gb)
{
    if (!gb->stat_interrupt_line && gb->oam_interrupt_line) {
        gb->io_registers[GB_IO_IF] |= 2;
        gb->stat_interrupt_line = true;
    }
}

/* Todo: When the CPU and PPU write to IF at the same T-cycle, the PPU write is ignored. */
void GB_STAT_update(GB_gameboy_t *gb)
{
    if (!(gb->io_registers[GB_IO_LCDC] & 0x80)) return;
    
    bool previous_interrupt_line = gb->stat_interrupt_line | gb->oam_interrupt_line;
    gb->stat_interrupt_line = gb->oam_interrupt_line;
    /* Set LY=LYC bit */
    if (gb->ly_for_comparison != (uint16_t)-1 || !gb->is_cgb) {
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
    
    switch (gb->io_registers[GB_IO_STAT] & 3) {
        case 0: gb->stat_interrupt_line = (gb->io_registers[GB_IO_STAT] & 8) && !gb->is_first_line_mode2; break;
        case 1: gb->stat_interrupt_line = gb->io_registers[GB_IO_STAT] & 0x10; break;
        /* The OAM interrupt is handled differently, it reads the writable flags from STAT less frequenctly,
            and is not based on the mode bits of STAT. */
        // case 2: gb->stat_interrupt_line = gb->io_registers[GB_IO_STAT] & 0x20; break;
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
    gb->display_state = 0;
    gb->display_cycles = 0;
    /* When the LCD is disabled, state is constant */
    
    /* When the LCD is off, LY is 0 and STAT mode is 0.  */
    gb->io_registers[GB_IO_LY] = 0;
    gb->io_registers[GB_IO_STAT] &= ~3;
    if (gb->hdma_on_hblank) {
        gb->hdma_on_hblank = false;
        gb->hdma_on = false;
        
        /* Todo: is this correct? */
        gb->hdma_steps_left = 0xff;
    }
    
    gb->oam_read_blocked = false;
    gb->vram_read_blocked = false;
    gb->oam_write_blocked = false;
    gb->vram_write_blocked = false;
    
    /* Reset window rendering state */
    gb->wy_diff = 0;
    gb->window_disabled_while_active = false;
    gb->current_line = 0;
    gb->ly_for_comparison = 0;
    
    gb->accessed_oam_row = -1;
}

static void add_object_from_index(GB_gameboy_t *gb, unsigned index)
{
    if (gb->n_visible_objs == 10) return;
    
    /* TODO: It appears that DMA blocks PPU access to OAM, but it needs verification. */
    if (gb->dma_steps_left && (gb->dma_cycles >= 0 || gb->is_dma_restarting)) {
        return;
    }

    /* This reverse sorts the visible objects by location and priority */
    GB_object_t *objects = (GB_object_t *) &gb->oam;
    bool height_16 = (gb->io_registers[GB_IO_LCDC] & 4) != 0;
    signed y = objects[index].y - 16;
    if (y <= gb->current_line && y + (height_16? 16 : 8) > gb->current_line) {
        unsigned j = 0;
        for (; j < gb->n_visible_objs; j++) {
            if (gb->obj_comperators[j] <= objects[index].x) break;
        }
        memmove(gb->visible_objs + j + 1, gb->visible_objs + j, gb->n_visible_objs - j);
        memmove(gb->obj_comperators + j + 1, gb->obj_comperators + j, gb->n_visible_objs - j);
        gb->visible_objs[j] = index;
        gb->obj_comperators[j] = objects[index].x;
        gb->n_visible_objs++;
    }
}

static void render_pixel_if_possible(GB_gameboy_t *gb)
{
    GB_fifo_item_t *fifo_item = NULL;
    GB_fifo_item_t *oam_fifo_item = NULL;
    bool draw_oam = false;
    bool bg_enabled = true, bg_priority = false;
    
    if (!gb->bg_fifo_paused) {
        fifo_item = fifo_pop(&gb->bg_fifo);
        bg_priority = fifo_item->bg_priority;
    }
    
    if (!gb->oam_fifo_paused && fifo_size(&gb->oam_fifo)) {
        oam_fifo_item = fifo_pop(&gb->oam_fifo);
        /* Todo: Verify access timings */
        if (oam_fifo_item->pixel && (gb->io_registers[GB_IO_LCDC] & 2)) {
            draw_oam = true;
            bg_priority |= oam_fifo_item->bg_priority;
        }
    }
    
    /* Drop pixels for scrollings */
    if (gb->position_in_line >= 160 || gb->disable_rendering) {
        gb->position_in_line++;
        return;
    }
    if (gb->bg_fifo_paused) return;
    
    /* Mixing */
    
    /* Todo: Verify access timings */
    if ((gb->io_registers[GB_IO_LCDC] & 0x1) == 0) {
        if (gb->cgb_mode) {
            bg_priority = false;
        }
        else {
            bg_enabled = false;
        }
    }
    if (!gb->is_cgb && gb->in_window) {
        bg_enabled = true;
    }
    
    {
        uint8_t pixel = bg_enabled? fifo_item->pixel : 0;
        if (pixel && bg_priority) {
            draw_oam = false;
        }
        if (!gb->cgb_mode) {
            pixel = ((gb->io_registers[GB_IO_BGP] >> (pixel << 1)) & 3);
        }
        gb->screen[gb->position_in_line + gb->current_line * WIDTH] = gb->background_palettes_rgb[fifo_item->palette * 4 + pixel];
    }
    
    if (draw_oam) {
        uint8_t pixel = oam_fifo_item->pixel;
        if (!gb->cgb_mode) {
            /* Todo: Verify access timings */
            pixel = ((gb->io_registers[oam_fifo_item->palette? GB_IO_OBP1 : GB_IO_OBP0] >> (pixel << 1)) & 3);
        }
        gb->screen[gb->position_in_line + gb->current_line * WIDTH] = gb->sprite_palettes_rgb[oam_fifo_item->palette * 4 + pixel];
    }
    
    gb->position_in_line++;
}

/* All verified CGB timings are based on CGB CPU E. CGB CPUs >= D are known to have
   slightly different timings than CPUs <= C.
 
   Todo: Add support to CPU C and older */

static inline uint8_t fetcher_y(GB_gameboy_t *gb)
{
    return gb->current_line + (gb->in_window? - gb->io_registers[GB_IO_WY] - gb->wy_diff : gb->io_registers[GB_IO_SCY]);
}

static void advance_fetcher_state_machine(GB_gameboy_t *gb)
{
    typedef enum {
        GB_FETCHER_GET_TILE,
        GB_FETCHER_GET_TILE_DATA_LOWER,
        GB_FETCHER_GET_TILE_DATA_HIGH,
        GB_FETCHER_PUSH,
        GB_FETCHER_SLEEP,
    } fetcher_step_t;
    
    fetcher_step_t fetcher_state_machine [8] = {
        GB_FETCHER_SLEEP,
        GB_FETCHER_GET_TILE,
        GB_FETCHER_SLEEP,
        GB_FETCHER_GET_TILE_DATA_LOWER,
        GB_FETCHER_SLEEP,
        GB_FETCHER_GET_TILE_DATA_HIGH,
        GB_FETCHER_SLEEP,
        GB_FETCHER_PUSH,
    };
    
    switch (fetcher_state_machine[gb->fetcher_state]) {
        case GB_FETCHER_GET_TILE: {
            uint16_t map = 0x1800;
            
            /* Todo: Verified for DMG (Tested: SGB2), CGB timing is wrong. */
            if (gb->io_registers[GB_IO_LCDC] & 0x08 && !gb->in_window) {
                map = 0x1C00;
            }
            else if (gb->io_registers[GB_IO_LCDC] & 0x40 && gb->in_window) {
                map = 0x1C00;
            }
            
            /* Todo: Verified for DMG (Tested: SGB2), CGB timing is wrong. */
            uint8_t y = fetcher_y(gb);
            if (gb->is_cgb) {
                /* This value is cached on the CGB, so it cannot be used to mix tiles together */
                /* Todo: This is NOT true on CGB-B! This is likely the case for all CGBs prior to D.
                 Currently, SameBoy is emulating CGB-E, but if other revisions are added in the future
                 this should be taken care of */
                gb->fetcher_y = y;
            }
            gb->current_tile = gb->vram[map + gb->fetcher_x + y / 8 * 32];
            if (gb->is_cgb) {
                /* The CGB actually accesses both the tile index AND the attributes in the same T-cycle.
                 This probably means the CGB has a 16-bit data bus for the VRAM. */
                gb->current_tile_attributes = gb->vram[map + gb->fetcher_x + y / 8 * 32 + 0x2000];
            }
            gb->fetcher_x++;
            gb->fetcher_x &= 0x1f;
        }
        gb->fetcher_state++;
        break;
            
        case GB_FETCHER_GET_TILE_DATA_LOWER: {
            uint8_t y_flip = 0;
            uint16_t tile_address = 0;
            uint8_t y = gb->is_cgb? gb->fetcher_y : fetcher_y(gb);
            
            /* Todo: Verified for DMG (Tested: SGB2), CGB timing is wrong. */
            if (gb->io_registers[GB_IO_LCDC] & 0x10) {
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
            gb->current_tile_data[0] =
            gb->vram[tile_address + ((y & 7) ^ y_flip) * 2];
        }
        gb->fetcher_state++;
        break;
            
        case GB_FETCHER_GET_TILE_DATA_HIGH: {
            /* Todo: Verified for DMG (Tested: SGB2), CGB timing is wrong.
             Additionally, on CGB-D and newer mixing two tiles by changing the tileset
             bit mid-fetching causes a glitched mixing of the two, in comparison to the
             more logical DMG version. */
            uint16_t tile_address = 0;
            uint8_t y = gb->is_cgb? gb->fetcher_y : fetcher_y(gb);
            
            if (gb->io_registers[GB_IO_LCDC] & 0x10) {
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
            gb->current_tile_data[1] =
            gb->vram[tile_address +  ((y & 7) ^ y_flip) * 2 + 1];
        }
        gb->fetcher_state++;
        break;
            
        case GB_FETCHER_PUSH: {
            if (fifo_size(&gb->bg_fifo) > 0) break;
            fifo_push_bg_row(&gb->bg_fifo, gb->current_tile_data[0], gb->current_tile_data[1],
                             gb->current_tile_attributes & 7, gb->current_tile_attributes & 0x80, gb->current_tile_attributes & 0x20);
            gb->bg_fifo_paused = false;
            gb->oam_fifo_paused = false;
            gb->fetcher_state++;
        }
        break;
            
        case GB_FETCHER_SLEEP:
        {
            gb->fetcher_state++;
        }
        break;
    }
    
    gb->fetcher_state &= 7;
}

void GB_display_run(GB_gameboy_t *gb, uint8_t cycles)
{
    GB_object_t *objects = (GB_object_t *) &gb->oam;
    
    GB_STATE_MACHINE(gb, display, cycles, 2) {
        GB_STATE(gb, display, 1);
        GB_STATE(gb, display, 2);
        GB_STATE(gb, display, 3);
        GB_STATE(gb, display, 4);
        GB_STATE(gb, display, 5);
        GB_STATE(gb, display, 6);
        GB_STATE(gb, display, 7);
        GB_STATE(gb, display, 8);
        // GB_STATE(gb, display, 9);
        GB_STATE(gb, display, 10);
        GB_STATE(gb, display, 11);
        GB_STATE(gb, display, 12);
        GB_STATE(gb, display, 13);
        GB_STATE(gb, display, 14);
        GB_STATE(gb, display, 15);
        GB_STATE(gb, display, 16);
        GB_STATE(gb, display, 17);
        // GB_STATE(gb, display, 19);
        GB_STATE(gb, display, 20);
        GB_STATE(gb, display, 21);
        GB_STATE(gb, display, 22);
        GB_STATE(gb, display, 23);
        GB_STATE(gb, display, 24);
        GB_STATE(gb, display, 25);
        GB_STATE(gb, display, 26);
        GB_STATE(gb, display, 27);
        GB_STATE(gb, display, 28);
        GB_STATE(gb, display, 29);
    }
    
    if (!(gb->io_registers[GB_IO_LCDC] & 0x80)) {
        while (true) {
            GB_SLEEP(gb, display, 1, LCDC_PERIOD);
            display_vblank(gb);
        }
        return;
    }
    
    if (!gb->is_cgb) {
        GB_SLEEP(gb, display, 23, 1);
    }

    /* Handle the very first line 0 */
    gb->is_first_line_mode2 = true;
    gb->current_line = 0;
    gb->ly_for_comparison = 0;
    gb->io_registers[GB_IO_STAT] &= ~3;
    gb->oam_read_blocked = false;
    gb->vram_read_blocked = false;
    gb->oam_write_blocked = false;
    gb->vram_write_blocked = false;
    gb->cycles_for_line = MODE2_LENGTH - 2;
    GB_STAT_update(gb);
    GB_SLEEP(gb, display, 2, MODE2_LENGTH - 2);
    
    gb->io_registers[GB_IO_STAT] &= ~3;
    gb->io_registers[GB_IO_STAT] |= 3;
    gb->oam_read_blocked = true;
    gb->vram_read_blocked = !gb->is_cgb;
    gb->oam_write_blocked = true;
    gb->vram_write_blocked = !gb->is_cgb;
    gb->is_first_line_mode2 = false;
    GB_STAT_update(gb);
    
    gb->cycles_for_line += 2;
    GB_SLEEP(gb, display, 24, 2);
    gb->vram_read_blocked = true;
    gb->vram_write_blocked = true;
    
    gb->cycles_for_line += MODE3_LENGTH + (gb->io_registers[GB_IO_SCX] & 7) - 2;
    GB_SLEEP(gb, display, 3, MODE3_LENGTH + (gb->io_registers[GB_IO_SCX] & 7) - 2);
    
    if (!gb->cgb_double_speed) {
        gb->io_registers[GB_IO_STAT] &= ~3;
        gb->oam_read_blocked = false;
        gb->vram_read_blocked = false;
        gb->oam_write_blocked = false;
        gb->vram_write_blocked = false;
    }
    gb->cycles_for_line += 1;
    GB_SLEEP(gb, display, 4, 1);
    
    gb->io_registers[GB_IO_STAT] &= ~3;
    gb->oam_read_blocked = false;
    gb->vram_read_blocked = false;
    gb->oam_write_blocked = false;
    gb->vram_write_blocked = false;
    
    GB_STAT_update(gb);
    /* Mode 0 is shorter in the very first line */
    GB_SLEEP(gb, display, 5, LINE_LENGTH - gb->cycles_for_line - 8);
    
    gb->current_line = 1;
    while (true) {
        /* Lines 0 - 143 */
        for (; gb->current_line < LINES; gb->current_line++) {
            gb->oam_write_blocked = gb->is_cgb;
            gb->accessed_oam_row = 0;
            GB_SLEEP(gb, display, 6, 3);
            gb->io_registers[GB_IO_LY] = gb->current_line;
            gb->oam_read_blocked = true;
            gb->ly_for_comparison = gb->current_line? -1 : 0;
            
            /* The OAM STAT interrupt occurs 1 T-cycle before STAT actually changes, except on line 0.
             PPU glitch? (Todo: and in double speed mode?) */
            if (gb->current_line != 0 && !gb->cgb_double_speed) {
                gb->oam_interrupt_line = gb->io_registers[GB_IO_STAT] & 0x20;
            }
            trigger_oam_interrupt(gb);
            GB_STAT_update(gb);
            if (gb->current_line != 0 || !gb->is_cgb) {
                gb->io_registers[GB_IO_STAT] &= ~3;
            }

            GB_SLEEP(gb, display, 7, 1);
            
            gb->io_registers[GB_IO_STAT] &= ~3;
            gb->io_registers[GB_IO_STAT] |= 2;
            gb->oam_write_blocked = true;
            gb->ly_for_comparison = gb->current_line;
            gb->oam_interrupt_line = gb->io_registers[GB_IO_STAT] & 0x20;
            trigger_oam_interrupt(gb);
            GB_STAT_update(gb);
            gb->n_visible_objs = 0;
            
            for (gb->oam_search_index = 0; gb->oam_search_index < 40; gb->oam_search_index++) {
                if (gb->is_cgb) {
                    add_object_from_index(gb, gb->oam_search_index);
                    /* The CGB does not care about the accessed OAM row as there's no OAM bug*/
                }
                GB_SLEEP(gb, display, 8, 2);
                if (!gb->is_cgb) {
                    add_object_from_index(gb, gb->oam_search_index);
                    gb->accessed_oam_row = (gb->oam_search_index & ~1) * 4 + 8;
                }
                if (gb->oam_search_index == 37) {
                    gb->vram_read_blocked = !gb->is_cgb;
                    gb->vram_write_blocked = false;
                    gb->oam_write_blocked = gb->is_cgb;
                    GB_STAT_update(gb);
                }
            }
            gb->accessed_oam_row = -1;
            gb->io_registers[GB_IO_STAT] &= ~3;
            gb->io_registers[GB_IO_STAT] |= 3;
            gb->vram_read_blocked = true;
            gb->vram_write_blocked = true;
            gb->oam_write_blocked = true;
            gb->oam_interrupt_line = false;
            trigger_oam_interrupt(gb);
            GB_STAT_update(gb);

            gb->cycles_for_line = MODE2_LENGTH + 4;
            fifo_clear(&gb->bg_fifo);
            fifo_clear(&gb->oam_fifo);
            /* Fill the FIFO with 8 pixels of "junk", it's going to be dropped anyway. */
            fifo_push_bg_row(&gb->bg_fifo, 0, 0, 0, false, false);
            /* Todo: find out actual access time of SCX */
            gb->position_in_line = - (gb->io_registers[GB_IO_SCX] & 7) - 8;
            gb->fetcher_x = ((gb->io_registers[GB_IO_SCX]) / 8) & 0x1f;
            gb->extra_penalty_for_sprite_at_0 = (gb->io_registers[GB_IO_SCX] & 7);
            
            gb->cycles_for_line += 5;
            GB_SLEEP(gb, display, 10, 5);

            /* The actual rendering cycle */
            gb->fetcher_state = 0;
            gb->bg_fifo_paused = false;
            gb->oam_fifo_paused = false;
            gb->in_window = false;
            while (true) {
                /* Handle objects */
                /* When the sprite enabled bit is off, this proccess is skipped entirely on the DMG, but not on the CGB.
                   On the CGB, this bit is checked only when the pixel is actually popped from the FIFO. */
                
                while (gb->n_visible_objs != 0 &&
                       (gb->position_in_line < 160 || gb->position_in_line >= (uint8_t)(-8)) &&
                       gb->obj_comperators[gb->n_visible_objs - 1] < (uint8_t)(gb->position_in_line + 8)) {
                    gb->n_visible_objs--;
                }
                while (gb->n_visible_objs != 0 &&
                       (gb->io_registers[GB_IO_LCDC] & 2 || gb->is_cgb) &&
                       gb->obj_comperators[gb->n_visible_objs - 1] == (uint8_t)(gb->position_in_line + 8)) {
                    
                    while (gb->fetcher_state < 5) {
                        advance_fetcher_state_machine(gb);
                        gb->cycles_for_line++;
                        GB_SLEEP(gb, display, 27, 1);
                    }
                    
                    /* Todo: Measure if penalty occurs before or after waiting for the fetcher. */
                    if (gb->extra_penalty_for_sprite_at_0 != 0) {
                        if (gb->obj_comperators[gb->n_visible_objs - 1] == 0) {
                            gb->cycles_for_line += gb->extra_penalty_for_sprite_at_0;
                            GB_SLEEP(gb, display, 28, gb->extra_penalty_for_sprite_at_0);
                            gb->extra_penalty_for_sprite_at_0 = 0;
                        }
                    }
                    
                    gb->cycles_for_line += 6;
                    GB_SLEEP(gb, display, 20, 6);
                    /* TODO: what does the PPU read if DMA is active? */
                    GB_object_t *object = &objects[gb->visible_objs[gb->n_visible_objs - 1]];
                    
                    bool height_16 = (gb->io_registers[GB_IO_LCDC] & 4) != 0; /* Todo: Which T-cycle actually reads this? */
                    uint8_t tile_y = (gb->current_line - object->y) & (height_16? 0xF : 7);
                    
                    if (object->flags & 0x40) { /* Flip Y */
                        tile_y ^= height_16? 0xF : 7;
                    }
                    /* Todo: I'm not 100% sure an access to OAM can't trigger the OAM bug while we're accessing this */
                    uint16_t line_address = (height_16? object->tile & 0xFE : object->tile) * 0x10 + tile_y * 2;
                    
                    if (gb->cgb_mode && (object->flags & 0x8)) { /* Use VRAM bank 2 */
                        line_address += 0x2000;
                    }
                    
                    uint8_t palette = (object->flags & 0x10) ? 1 : 0;
                    if (gb->cgb_mode) {
                        palette = object->flags & 0x7;
                    }
                    
                    fifo_overlay_object_row(&gb->oam_fifo,
                                            gb->vram[line_address],
                                            gb->vram[line_address + 1],
                                            palette,
                                            object->flags & 0x80,
                                            gb->cgb_mode? gb->visible_objs[gb->n_visible_objs - 1] : 0,
                                            object->flags & 0x20);
                    
                    gb->n_visible_objs--;
                }
                
                /* Handle window */
                /* Todo: Timing (Including penalty and access timings) not verified by test ROM */
                if (!gb->in_window && window_enabled(gb) &&
                    gb->current_line >= gb->io_registers[GB_IO_WY] + gb->wy_diff &&
                    gb->position_in_line + 7 == gb->io_registers[GB_IO_WX]) {
                    gb->in_window = true;
                    fifo_clear(&gb->bg_fifo);
                    gb->bg_fifo_paused = true;
                    gb->oam_fifo_paused = true;
                    gb->fetcher_x = 0;
                    gb->fetcher_state = 0;
                }
                
                render_pixel_if_possible(gb);
                advance_fetcher_state_machine(gb);

                if (gb->position_in_line == 160) break;
                gb->cycles_for_line++;
                GB_SLEEP(gb, display, 21, 1);
            }
            if (!gb->cgb_double_speed) {
                gb->io_registers[GB_IO_STAT] &= ~3;
                gb->oam_read_blocked = false;
                gb->vram_read_blocked = false;
                gb->oam_write_blocked = false;
                gb->vram_write_blocked = false;
            }
            
            gb->cycles_for_line++;
            GB_SLEEP(gb, display, 22, 1);
            
            gb->io_registers[GB_IO_STAT] &= ~3;
            gb->oam_read_blocked = false;
            gb->vram_read_blocked = false;
            gb->oam_write_blocked = false;
            gb->vram_write_blocked = false;
            GB_STAT_update(gb);

            /* Todo: Measure this value */
            
            gb->cycles_for_line += 16;
            GB_SLEEP(gb, display, 25, 16);
            
            if (gb->hdma_on_hblank) {
                gb->hdma_starting = true;
            }
            GB_SLEEP(gb, display, 11, LINE_LENGTH - gb->cycles_for_line);
        }
        
        /* Lines 144 - 152 */
        for (; gb->current_line < VIRTUAL_LINES - 1; gb->current_line++) {
            gb->io_registers[GB_IO_LY] = gb->current_line;
            gb->ly_for_comparison = -1;
            GB_SLEEP(gb, display, 26, 2);
            if (gb->current_line == LINES) {
                gb->oam_interrupt_line = gb->io_registers[GB_IO_STAT] & 0x20;
                trigger_oam_interrupt(gb);
            }
            GB_STAT_update(gb);
            gb->oam_interrupt_line = false;
            GB_SLEEP(gb, display, 12, 2);
            gb->ly_for_comparison = gb->current_line;
            
            if (gb->current_line == LINES) {
                /* Entering VBlank state triggers the OAM interrupt */
                gb->oam_interrupt_line = gb->io_registers[GB_IO_STAT] & 0x20;
                gb->io_registers[GB_IO_STAT] &= ~3;
                gb->io_registers[GB_IO_STAT] |= 1;
                gb->io_registers[GB_IO_IF] |= 1;
                trigger_oam_interrupt(gb);
                GB_STAT_update(gb);
                gb->oam_interrupt_line = false;
                
                if (gb->io_registers[GB_IO_STAT] & 0x20) {
                    gb->stat_interrupt_line = true;
                }
                
                if (gb->frame_skip_state == GB_FRAMESKIP_LCD_TURNED_ON) {
                    display_vblank(gb);
                    gb->frame_skip_state = GB_FRAMESKIP_SECOND_FRAME_RENDERED;
                }
                else {
                    gb->frame_skip_state = GB_FRAMESKIP_SECOND_FRAME_RENDERED;
                    display_vblank(gb);
                }
            }
            
            GB_STAT_update(gb);
            GB_SLEEP(gb, display, 13, LINE_LENGTH - 4);
        }
        
        /* TODO: Verified on SGB2 and CGB-E. Actual interrupt timings not tested. */
        /* Lines 153 */
        gb->io_registers[GB_IO_LY] = 153;
        gb->ly_for_comparison = -1;
        GB_STAT_update(gb);
        GB_SLEEP(gb, display, 14, gb->is_cgb? 4: 6);
        
        if (!gb->is_cgb) {
            gb->io_registers[GB_IO_LY] = 0;
        }
        gb->ly_for_comparison = 153;
        GB_STAT_update(gb);
        GB_SLEEP(gb, display, 15, gb->is_cgb? 4: 2);
        
        gb->io_registers[GB_IO_LY] = 0;
        gb->ly_for_comparison = gb->is_cgb? 153 : -1;
        GB_STAT_update(gb);
        GB_SLEEP(gb, display, 16, 4);
        
        gb->ly_for_comparison = 0;
        GB_STAT_update(gb);
        GB_SLEEP(gb, display, 29, 12); /* Writing to LYC during this period on a CGB has side effects */
        GB_SLEEP(gb, display, 17, LINE_LENGTH - 24);
        
        
        /* Reset window rendering state */
        gb->wy_diff = 0;
        gb->window_disabled_while_active = false;
        gb->current_line = 0;
    }
}

void GB_draw_tileset(GB_gameboy_t *gb, uint32_t *dest, GB_palette_type_t palette_type, uint8_t palette_index)
{
    uint32_t none_palette[4];
    uint32_t *palette = NULL;
    
    switch (gb->is_cgb? palette_type : GB_PALETTE_NONE) {
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
            palette = gb->sprite_palettes_rgb + (4 * (palette_index & 7));
            break;
    }
    
    for (unsigned y = 0; y < 192; y++) {
        for (unsigned x = 0; x < 256; x++) {
            if (x >= 128 && !gb->is_cgb) {
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
    
    switch (gb->is_cgb? palette_type : GB_PALETTE_NONE) {
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
            palette = gb->sprite_palettes_rgb + (4 * (palette_index & 7));
            break;
        case GB_PALETTE_AUTO:
            break;
    }
    
    if (map_type == GB_MAP_9C00 || (map_type == GB_MAP_AUTO && gb->io_registers[GB_IO_LCDC] & 0x08)) {
        map = 0x1c00;
    }
    
    if (tileset_type == GB_TILESET_AUTO) {
        tileset_type = (gb->io_registers[GB_IO_LCDC] & 0x10)? GB_TILESET_8800 : GB_TILESET_8000;
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

uint8_t GB_get_oam_info(GB_gameboy_t *gb, GB_oam_info_t *dest, uint8_t *sprite_height)
{
    uint8_t count = 0;
    *sprite_height = (gb->io_registers[GB_IO_LCDC] & 4) ? 16:8;
    uint8_t oam_to_dest_index[40] = {0,};
    for (unsigned y = 0; y < LINES; y++) {
        GB_object_t *sprite = (GB_object_t *) &gb->oam;
        uint8_t sprites_in_line = 0;
        for (uint8_t i = 0; i < 40; i++, sprite++) {
            int sprite_y = sprite->y - 16;
            bool obscured = false;
            // Is sprite not in this line?
            if (sprite_y > y || sprite_y + *sprite_height <= y) continue;
            if (++sprites_in_line == 11) obscured = true;
            
            GB_oam_info_t *info = NULL;
            if (!oam_to_dest_index[i]) {
                info = dest + count;
                oam_to_dest_index[i] = ++count;
                info->x = sprite->x;
                info->y = sprite->y;
                info->tile = *sprite_height == 16? sprite->tile & 0xFE : sprite->tile;
                info->flags = sprite->flags;
                info->obscured_by_line_limit = false;
                info->oam_addr = 0xFE00 + i * sizeof(*sprite);
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
        if (gb->is_cgb && (flags & 0x8)) {
            vram_address += 0x2000;
        }

        for (unsigned y = 0; y < *sprite_height; y++) {
            for (unsigned x = 0; x < 8; x++) {
                uint8_t color = (((gb->vram[vram_address    ] >> ((~x)&7)) & 1 ) |
                                 ((gb->vram[vram_address + 1] >> ((~x)&7)) & 1) << 1 );
                
                if (!gb->cgb_mode) {
                    color = (gb->io_registers[palette? GB_IO_OBP1:GB_IO_OBP0] >> (color << 1)) & 3;
                }
                dest[i].image[((flags & 0x20)?7-x:x) + ((flags & 0x40)?*sprite_height - 1 -y:y) * 8] = gb->sprite_palettes_rgb[palette * 4 + color];
            }
            vram_address += 2;
        }
    }
    return count;
}

/* Called when a write might enable or disable the window */
void GB_window_related_write(GB_gameboy_t *gb, uint8_t addr, uint8_t value)
{
    bool before = window_enabled(gb);
    gb->io_registers[addr] = value;
    bool after = window_enabled(gb);
    
    if (before != after && gb->current_line >= LINES) {
        /* Window was disabled or enabled outside of vblank */
        if (gb->current_line >= gb->io_registers[GB_IO_WY]) {
            if (after) {
                if (!gb->window_disabled_while_active) {
                    /* Window was turned on for the first time this frame while LY > WY,
                       should start window in the next line */
                    gb->wy_diff = gb->current_line + 1 - gb->io_registers[GB_IO_WY];
                }
                else {
                    gb->wy_diff += gb->current_line;
                }
            }
            else {
                gb->wy_diff -= gb->current_line;
                gb->window_disabled_while_active = true;
            }
        }
    }
}
