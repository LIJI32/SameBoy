#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "gb.h"

typedef uint8_t read_function_t(GB_gameboy_t *gb, uint16_t addr);
typedef void write_function_t(GB_gameboy_t *gb, uint16_t addr, uint8_t value);

typedef enum {
    GB_BUS_MAIN, /* In DMG: Cart and RAM. In CGB: Cart only */
    GB_BUS_RAM, /* In CGB only. */
    GB_BUS_VRAM,
} bus_t;

static bus_t bus_for_addr(GB_gameboy_t *gb, uint16_t addr)
{
    if (addr < 0x8000) {
        return GB_BUS_MAIN;
    }
    if (addr < 0xA000) {
        return GB_BUS_VRAM;
    }
    if (addr < 0xC000) {
        return GB_BUS_MAIN;
    }
    return GB_is_cgb(gb)? GB_BUS_RAM : GB_BUS_MAIN;
}

static uint16_t bitwise_glitch(uint16_t a, uint16_t b, uint16_t c)
{
    return ((a ^ c) & (b ^ c)) ^ c;
}

static uint16_t bitwise_glitch_read(uint16_t a, uint16_t b, uint16_t c)
{
    return b | (a & c);
}

static uint16_t bitwise_glitch_read_secondary(uint16_t a, uint16_t b, uint16_t c, uint16_t d)
{
    return (b & (a | c | d)) | (a & c & d);
}

/*
 Used on the MGB in some scenarios
static uint16_t bitwise_glitch_mgb(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e, bool variant)
{
    return (c & (e | d | b | (variant? 0 : a))) | (b & d & (a | e));
}
*/

static uint16_t bitwise_glitch_tertiary_read_1(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e)
{
    return c | (a & b & d & e);
}

static uint16_t bitwise_glitch_tertiary_read_2(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e)
{
    return (c & (a | b | d | e)) | (a & b & d & e);
}

static uint16_t bitwise_glitch_tertiary_read_3(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e)
{
    return (c & (a | b | d | e)) | (b & d & e);
}

static uint16_t bitwise_glitch_quaternary_read_dmg(uint16_t a, uint16_t b, uint16_t c, uint16_t d,
                                                   uint16_t e, uint16_t f, uint16_t g, uint16_t h)
{
    /* On my DMG, some cases are non-deterministic, while on some other DMGs they yield constant zeros.
       The non deterministic cases are affected by (on the row 40 case) 34, 36, 3e and 28, and potentially
       others. For my own sanity I'm going to emulate the DMGs that output zeros. */
    (void)a;
    return (e & (h | g | (~d & f) | c | b)) | (c & g & h);
}

/*
 
// Oh my.
static uint16_t bitwise_glitch_quaternary_read_mgb(uint16_t a, uint16_t b, uint16_t c, uint16_t d,
                                                   uint16_t e, uint16_t f, uint16_t g, uint16_t h)
{
    return (e & (h | g | c | (a & b))) | ((c & h) & (g & (~f | b | a | ~d) | (a & b & f)));
}
*/

static uint16_t bitwise_glitch_quaternary_read_sgb2(uint16_t a, uint16_t b, uint16_t c, uint16_t d,
                                                    uint16_t e, uint16_t f, uint16_t g, uint16_t h)
{
    return (e & (h | g | c | (a & b))) | ((c & g & h) & (b | a | ~f));
}

void GB_trigger_oam_bug(GB_gameboy_t *gb, uint16_t address)
{
    if (GB_is_cgb(gb)) return;
    
    if (address >= 0xFE00 && address < 0xFF00) {
        GB_display_sync(gb);
        if (gb->accessed_oam_row != 0xFF && gb->accessed_oam_row >= 8) {
            uint16_t *base = (uint16_t *)(gb->oam + gb->accessed_oam_row);
            base[0] = bitwise_glitch(base[0],
                                     base[-4],
                                     base[-2]);
            for (unsigned i = 2; i < 8; i++) {
                gb->oam[gb->accessed_oam_row + i] = gb->oam[gb->accessed_oam_row - 8 + i];
            }
        }
    }
}

static void oam_bug_secondary_read_corruption(GB_gameboy_t *gb)
{
    if (gb->accessed_oam_row < 0x98) {
        uint16_t *base = (uint16_t *)(gb->oam + gb->accessed_oam_row);
        base[-4] = bitwise_glitch_read_secondary(base[-8],
                                                 base[-4],
                                                 base[0],
                                                 base[-2]);
        for (unsigned i = 0; i < 8; i++) {
            gb->oam[gb->accessed_oam_row - 0x10 + i] = gb->oam[gb->accessed_oam_row - 0x08 + i];
        }
    }
}

/*
static void oam_bug_tertiary_read_corruption_mgb(GB_gameboy_t *gb)
{
    if (gb->accessed_oam_row < 0x98) {
        uint16_t *base = (uint16_t *)(gb->oam + gb->accessed_oam_row);
        uint16_t temp = bitwise_glitch_mgb(
                                           base[0],
                                           base[-2],
                                           base[-4],
                                           base[-8],
                                           base[-16],
                                           true);
        
        base[-4] = bitwise_glitch_mgb(
                                      base[0],
                                      base[-2],
                                      base[-4],
                                      base[-8],
                                      base[-16],
                                      false);
        for (unsigned i = 0; i < 8; i++) {
            gb->oam[gb->accessed_oam_row - 0x10 + i] = gb->oam[gb->accessed_oam_row - 0x20 + i] = gb->oam[gb->accessed_oam_row - 0x08 + i];
        }
        
        base[-8] = temp;
        base[-16] = temp;
    }
}
*/

static void oam_bug_quaternary_read_corruption(GB_gameboy_t *gb, typeof(bitwise_glitch_quaternary_read_dmg) *bitwise_op)
{
    if (gb->accessed_oam_row < 0x98) {
        uint16_t *base = (uint16_t *)(gb->oam + gb->accessed_oam_row);

        base[-4] = bitwise_op(*(uint16_t *)gb->oam,
                              base[0],
                              base[-2],
                              base[-3],
                              base[-4],
                              base[-7],
                              base[-8],
                              base[-16]);
        for (unsigned i = 0; i < 8; i++) {
            gb->oam[gb->accessed_oam_row - 0x10 + i] = gb->oam[gb->accessed_oam_row - 0x20 + i] = gb->oam[gb->accessed_oam_row - 0x08 + i];
        }
    }
}

static void oam_bug_tertiary_read_corruption(GB_gameboy_t *gb, typeof(bitwise_glitch_tertiary_read_1) *bitwise_op)
{
    if (gb->accessed_oam_row < 0x98) {
        uint16_t *base = (uint16_t *)(gb->oam + gb->accessed_oam_row);
        
        /* On some instances, the corruption row is copied to the first row for some accessed row. On my DMG it happens
         for row 80, and on my MGB it happens on row 60. Some instances only copy odd or even bytes. Additionally,
         for some instances on accessed rows that do not affect the first row, the last two bytes of the preceeding
         row are also corrupted in a non-deterministic probability. */
        
        base[-4] = bitwise_op(
                              base[0],
                              base[-2],
                              base[-4],
                              base[-8],
                              base[-16]);
        for (unsigned i = 0; i < 8; i++) {
            gb->oam[gb->accessed_oam_row - 0x10 + i] = gb->oam[gb->accessed_oam_row - 0x20 + i] = gb->oam[gb->accessed_oam_row - 0x08 + i];
        }
    }
}

void GB_trigger_oam_bug_read(GB_gameboy_t *gb, uint16_t address)
{
    if (GB_is_cgb(gb)) return;
    
    if (address >= 0xFE00 && address < 0xFF00) {
        if (gb->accessed_oam_row != 0xFF && gb->accessed_oam_row >= 8) {
            if ((gb->accessed_oam_row & 0x18) == 0x10) {
                oam_bug_secondary_read_corruption(gb);
            }
            else if ((gb->accessed_oam_row & 0x18) == 0x00) {
                /* Everything in this specific case is *extremely* revision and instance specific. */
                if (gb->model == GB_MODEL_MGB) {
                    /* TODO: This is rather simplified, research further */
                    oam_bug_tertiary_read_corruption(gb, bitwise_glitch_tertiary_read_3);
                }
                else if (gb->accessed_oam_row == 0x40) {
                     oam_bug_quaternary_read_corruption(gb,
                                                        ((gb->model & ~GB_MODEL_NO_SFC_BIT) == GB_MODEL_SGB2)?
                                                        bitwise_glitch_quaternary_read_sgb2:
                                                        bitwise_glitch_quaternary_read_dmg);
                }
                else if ((gb->model & ~GB_MODEL_NO_SFC_BIT) != GB_MODEL_SGB2) {
                    if (gb->accessed_oam_row == 0x20) {
                        oam_bug_tertiary_read_corruption(gb, bitwise_glitch_tertiary_read_2);
                    }
                    else if (gb->accessed_oam_row == 0x60) {
                        oam_bug_tertiary_read_corruption(gb, bitwise_glitch_tertiary_read_3);
                    }
                    else {
                        oam_bug_tertiary_read_corruption(gb, bitwise_glitch_tertiary_read_1);
                    }
                }
                else {
                    oam_bug_tertiary_read_corruption(gb, bitwise_glitch_tertiary_read_2);
                }
            }
            else {
                uint16_t *base = (uint16_t *)(gb->oam + gb->accessed_oam_row);
                base[-4] =
                base[0]     = bitwise_glitch_read(base[0],
                                                  base[-4],
                                                  base[-2]);
            }
            for (unsigned i = 0; i < 8; i++) {
                gb->oam[gb->accessed_oam_row + i] = gb->oam[gb->accessed_oam_row - 8 + i];
            }
            if (gb->accessed_oam_row == 0x80) {
                memcpy(gb->oam, gb->oam + gb->accessed_oam_row, 8);
            }
            else if (gb->model == GB_MODEL_MGB && gb->accessed_oam_row == 0x40) {
                memcpy(gb->oam, gb->oam + gb->accessed_oam_row, 8);
            }
        }
    }
}


static bool is_addr_in_dma_use(GB_gameboy_t *gb, uint16_t addr)
{
    if (!GB_is_dma_active(gb) || addr >= 0xFE00 || gb->hdma_in_progress) return false;
    if (gb->dma_current_dest == 0xFF || gb->dma_current_dest == 0x0) return false; // Warm up
    if (addr >= 0xFE00) return false;
    if (gb->dma_current_src == addr) return false; // Shortcut for DMA access flow
    if (gb->dma_current_src >= 0xE000 && (gb->dma_current_src & ~0x2000) == addr) return false;
    if (GB_is_cgb(gb)) {
        if (addr >= 0xC000) {
            return bus_for_addr(gb, gb->dma_current_src) != GB_BUS_VRAM;
        }
        if (gb->dma_current_src >= 0xE000) {
            return bus_for_addr(gb, addr) != GB_BUS_VRAM;
        }
    }
    return bus_for_addr(gb, addr) == bus_for_addr(gb, gb->dma_current_src);
}

static uint8_t read_rom(GB_gameboy_t *gb, uint16_t addr)
{
    if (addr < 0x100 && !gb->boot_rom_finished) {
        return gb->boot_rom[addr];
    }

    if (addr >= 0x200 && addr < 0x900 && GB_is_cgb(gb) && !gb->boot_rom_finished) {
        return gb->boot_rom[addr];
    }

    if (!gb->rom_size) {
        return 0xFF;
    }
    unsigned effective_address = (addr & 0x3FFF) + gb->mbc_rom0_bank * 0x4000;
    return gb->rom[effective_address & (gb->rom_size - 1)];
}

static uint8_t read_mbc_rom(GB_gameboy_t *gb, uint16_t addr)
{
    unsigned effective_address = (addr & 0x3FFF) + gb->mbc_rom_bank * 0x4000;
    return gb->rom[effective_address & (gb->rom_size - 1)];
}

static uint8_t read_vram(GB_gameboy_t *gb, uint16_t addr)
{
    if (likely(!GB_is_dma_active(gb))) {
        /* Prevent syncing from a DMA read. Batching doesn't happen during DMA anyway. */
        GB_display_sync(gb);
    }
    else {
        if (unlikely((gb->dma_current_dest & 0xE000) == 0x8000)) {
            // TODO: verify conflict behavior
            return gb->cpu_vram_bus = gb->vram[(addr & 0x1FFF) + (gb->cgb_vram_bank? 0x2000 : 0)];
        }
    }
    
    if (unlikely(gb->vram_read_blocked && !gb->in_dma_read)) {
        return 0xFF;
    }
    if (unlikely(gb->display_state == 22)) {
        if (!GB_is_cgb(gb)) {
            if (addr & 0x1000 && !(gb->last_tile_data_address & 0x1000)) {
                addr &= ~0x1000; // TODO: verify
            }
        }
        else if (!gb->cgb_double_speed) {
            if (addr & 0x1000) {
                if (gb->model <= GB_MODEL_CGB_C && !(gb->last_tile_data_address & 0x1000)) {
                    return 0;
                }
                addr = gb->last_tile_index_address;
            }
            else if (gb->last_tile_data_address & 0x1000) {
                if (gb->model >= GB_MODEL_CGB_E) {
                    uint8_t ret = gb->cpu_vram_bus;
                    gb->cpu_vram_bus = gb->vram[(addr & 0x1FFF) + (gb->cgb_vram_bank? 0x2000 : 0)];
                    return ret;
                }
                return gb->cpu_vram_bus;
            }
            else {
                addr = gb->last_tile_data_address;
            }
        }
    }
    return gb->cpu_vram_bus = gb->vram[(addr & 0x1FFF) + (gb->cgb_vram_bank? 0x2000 : 0)];
}

static uint8_t read_mbc7_ram(GB_gameboy_t *gb, uint16_t addr)
{
    if (!gb->mbc_ram_enable || !gb->mbc7.secondary_ram_enable) return 0xFF;
    if (addr >= 0xB000) return 0xFF;
    switch ((addr >> 4) & 0xF) {
        case 2: return gb->mbc7.x_latch;
        case 3: return gb->mbc7.x_latch >> 8;
        case 4: return gb->mbc7.y_latch;
        case 5: return gb->mbc7.y_latch >> 8;
        case 6: return 0;
        case 8: return  gb->mbc7.eeprom_do | (gb->mbc7.eeprom_di << 1) |
                       (gb->mbc7.eeprom_clk << 6) | (gb->mbc7.eeprom_cs << 7);
    }
    return 0xFF;
}

static uint8_t read_mbc_ram(GB_gameboy_t *gb, uint16_t addr)
{
    if (gb->cartridge_type->mbc_type == GB_MBC7) {
        return read_mbc7_ram(gb, addr);
    }

    if (gb->cartridge_type->mbc_type == GB_HUC3) {
        switch (gb->huc3.mode) {
            case 0xC: // RTC read
                if (gb->huc3.access_flags == 0x2) {
                    return 1;
                }
                return gb->huc3.read;
            case 0xD: // RTC status
                return 1;
            case 0xE: // IR mode
                return gb->effective_ir_input; // TODO: What are the other bits?
            default:
                GB_log(gb, "Unsupported HuC-3 mode %x read: %04x\n", gb->huc3.mode, addr);
                return 1; // TODO: What happens in this case?
            case 0: // TODO: R/O RAM? (or is it disabled?)
            case 0xA: // RAM
                break;
        }
    }
    
    if (gb->cartridge_type->mbc_type == GB_TPP1) {
        switch (gb->tpp1.mode) {
            case 0:
                switch (addr & 3) {
                    case 0: return gb->tpp1.rom_bank;
                    case 1: return gb->tpp1.rom_bank >> 8;
                    case 2: return gb->tpp1.ram_bank;
                    case 3: return gb->rumble_strength | gb->tpp1_mr4;
                    nodefault;
                }
            case 2:
            case 3:
                break; // Read RAM
            case 5:
                return gb->rtc_latched.data[(addr & 3) ^ 3];
            default:
                gb->returned_open_bus = true;
                return gb->data_bus;
        }
    }
    else if ((!gb->mbc_ram_enable) &&
        gb->cartridge_type->mbc_type != GB_CAMERA &&
        gb->cartridge_type->mbc_type != GB_HUC1 &&
        gb->cartridge_type->mbc_type != GB_HUC3) {
        gb->returned_open_bus = true;
        return gb->data_bus;
    }
    
    if (gb->cartridge_type->mbc_type == GB_HUC1 && gb->huc1.ir_mode) {
        return 0xC0 | gb->effective_ir_input;
    }
    
    if (gb->cartridge_type->has_rtc && gb->cartridge_type->mbc_type != GB_HUC3 &&
        gb->mbc3.rtc_mapped) {
        /* RTC read */
        if (gb->mbc_ram_bank <= 4) {
            gb->rtc_latched.seconds &= 0x3F;
            gb->rtc_latched.minutes &= 0x3F;
            gb->rtc_latched.hours &= 0x1F;
            gb->rtc_latched.high &= 0xC1;
            return gb->rtc_latched.data[gb->mbc_ram_bank];
        }
        gb->returned_open_bus = true;
        return gb->data_bus;
    }

    if (gb->camera_registers_mapped) {
        return GB_camera_read_register(gb, addr);
    }

    if (!gb->mbc_ram || !gb->mbc_ram_size) {
        gb->returned_open_bus = true;
        return gb->data_bus;
    }

    if (gb->cartridge_type->mbc_type == GB_CAMERA) {
        /* Forbid reading RAM while the camera is busy. */
        if (gb->camera_registers[GB_CAMERA_SHOOT_AND_1D_FLAGS] & 1) {
            return 0;
        }

        if (gb->mbc_ram_bank == 0 && addr >= 0xA100 && addr < 0xAF00) {
            return GB_camera_read_image(gb, addr - 0xA100);
        }
    }

    uint8_t effective_bank = gb->mbc_ram_bank;
    if (gb->cartridge_type->mbc_type == GB_MBC3 && !gb->is_mbc30) {
        if (gb->cartridge_type->has_rtc) {
            if (effective_bank > 3) return 0xFF;
        }
        effective_bank &= 0x3;
    }
    uint8_t ret = gb->mbc_ram[((addr & 0x1FFF) + effective_bank * 0x2000) & (gb->mbc_ram_size - 1)];
    if (gb->cartridge_type->mbc_type == GB_MBC2) {
        ret |= 0xF0;
    }
    return ret;
}

static uint8_t read_ram(GB_gameboy_t *gb, uint16_t addr)
{
    return gb->ram[addr & 0x0FFF];
}

static uint8_t read_banked_ram(GB_gameboy_t *gb, uint16_t addr)
{
    return gb->ram[(addr & 0x0FFF) + gb->cgb_ram_bank * 0x1000];
}

static inline void sync_ppu_if_needed(GB_gameboy_t *gb, uint8_t register_accessed)
{
    switch (register_accessed) {
        case GB_IO_IF:
        case GB_IO_LCDC:
        case GB_IO_STAT:
        case GB_IO_SCY:
        case GB_IO_SCX:
        case GB_IO_LY:
        case GB_IO_LYC:
        case GB_IO_DMA:
        case GB_IO_BGP:
        case GB_IO_OBP0:
        case GB_IO_OBP1:
        case GB_IO_WY:
        case GB_IO_WX:
        case GB_IO_HDMA1:
        case GB_IO_HDMA2:
        case GB_IO_HDMA3:
        case GB_IO_HDMA4:
        case GB_IO_HDMA5:
        case GB_IO_BGPI:
        case GB_IO_BGPD:
        case GB_IO_OBPI:
        case GB_IO_OBPD:
        case GB_IO_OPRI:
            GB_display_sync(gb);
            break;
    }
}

internal uint8_t GB_read_oam(GB_gameboy_t *gb, uint8_t addr)
{
    if (addr < 0xA0) {
        return gb->oam[addr];
    }
    
    switch (gb->model) {
        case GB_MODEL_CGB_E:
        case GB_MODEL_AGB_A:
        case GB_MODEL_GBP_A:
            return (addr & 0xF0) | (addr >> 4);
            
        case GB_MODEL_CGB_D:
            if (addr >= 0xC0) {
                addr |= 0xF0;
            }
            return gb->extra_oam[addr - 0xA0];
            
        case GB_MODEL_CGB_C:
        case GB_MODEL_CGB_B:
        case GB_MODEL_CGB_A:
        case GB_MODEL_CGB_0:
            addr &= ~0x18;
            return gb->extra_oam[addr - 0xA0];
            
        case GB_MODEL_DMG_B:
        case GB_MODEL_MGB:
        case GB_MODEL_SGB_NTSC:
        case GB_MODEL_SGB_PAL:
        case GB_MODEL_SGB_NTSC_NO_SFC:
        case GB_MODEL_SGB_PAL_NO_SFC:
        case GB_MODEL_SGB2:
        case GB_MODEL_SGB2_NO_SFC:
            return 0;
    }
    unreachable();
}

static uint8_t read_high_memory(GB_gameboy_t *gb, uint16_t addr)
{
    if (addr < 0xFE00) {
        return read_banked_ram(gb, addr);
    }

    if (addr < 0xFF00) {
        GB_display_sync(gb);
        if (gb->oam_write_blocked && !GB_is_cgb(gb)) {
            if (!gb->disable_oam_corruption) {
                GB_trigger_oam_bug_read(gb, addr);
            }
            return 0xFF;
        }
        
        if (GB_is_dma_active(gb) && (gb->dma_current_dest != 0 || gb->dma_restarting)) {
            /* Todo: Does reading from OAM during DMA causes the OAM bug? */
            return 0xFF;
        }
        
        if (gb->oam_read_blocked) {
            if (!GB_is_cgb(gb) && !gb->disable_oam_corruption) {
                if (addr < 0xFEA0) {
                    uint16_t *oam = (uint16_t *)gb->oam;
                    if (gb->accessed_oam_row == 0) {
                        oam[(addr & 0xF8) >> 1] =
                        oam[0] = bitwise_glitch_read(oam[0],
                                                     oam[(addr & 0xF8) >> 1],
                                                     oam[(addr & 0xFF) >> 1]);

                        for (unsigned i = 2; i < 8; i++) {
                            gb->oam[i] = gb->oam[(addr & 0xF8) + i];
                        }
                    }
                    else if (gb->accessed_oam_row == 0xA0) {
                        uint8_t target = (addr & 7) | 0x98;
                        uint16_t a = oam[0x9C >> 1],
                                 b = oam[target >> 1],
                                 c = oam[(addr & 0xF8) >> 1];
                        switch (addr & 7) {
                            case 0:
                            case 1:
                                /* Probably instance specific */
                                if ((gb->model & GB_MODEL_FAMILY_MASK) == GB_MODEL_DMG_FAMILY) {
                                    oam[target >> 1] = (a & b) | (a & c) | (b & c);
                                }
                                else {
                                    oam[target >> 1] = bitwise_glitch_read(a, b, c);
                                }
                                break;
                            case 2:
                            case 3: {
                                /* Probably instance specific */
                                c = oam[(addr & 0xFE) >> 1];
                                
                                // MGB only: oam[target >> 1] = bitwise_glitch_read(a, b, c);
                                oam[target >> 1] = (a & b) | (a & c) | (b & c);
                                break;
                            }
                            case 4:
                            case 5:
                                break; // No additional corruption
                            case 6:
                            case 7:
                                oam[target >> 1] = bitwise_glitch_read(a, b, c);
                                break;
                                
                            nodefault;
                        }
                        
                        for (unsigned i = 0; i < 8; i++) {
                            gb->oam[(addr & 0xF8) + i] = gb->oam[0x98 + i];
                        }
                    }
                }
            }
            return 0xFF;
        }
        
        return GB_read_oam(gb, addr);
    }

    if (addr < 0xFF80) {
        sync_ppu_if_needed(gb, addr);
        switch (addr & 0xFF) {
            case GB_IO_IF:
                return gb->io_registers[GB_IO_IF] | 0xE0;
            case GB_IO_TAC:
                return gb->io_registers[GB_IO_TAC] | 0xF8;
            case GB_IO_STAT:
                return gb->io_registers[GB_IO_STAT] | 0x80;
            case GB_IO_OPRI:
                if (!GB_is_cgb(gb)) {
                    return 0xFF;
                }
                return gb->io_registers[GB_IO_OPRI] | 0xFE;

            case GB_IO_PCM12:
                if (!GB_is_cgb(gb)) return 0xFF;
                GB_apu_run(gb, true);
                return ((gb->apu.is_active[GB_SQUARE_2] ? (gb->apu.samples[GB_SQUARE_2] << 4) : 0) |
                        (gb->apu.is_active[GB_SQUARE_1] ? (gb->apu.samples[GB_SQUARE_1]) : 0)) & (gb->model <= GB_MODEL_CGB_C? gb->apu.pcm_mask[0] : 0xFF);
            case GB_IO_PCM34:
                if (!GB_is_cgb(gb)) return 0xFF;
                GB_apu_run(gb, true);
                return ((gb->apu.is_active[GB_NOISE] ? (gb->apu.samples[GB_NOISE] << 4) : 0) |
                        (gb->apu.is_active[GB_WAVE] ? (gb->apu.samples[GB_WAVE]) : 0))  & (gb->model <= GB_MODEL_CGB_C? gb->apu.pcm_mask[1] : 0xFF);
            case GB_IO_JOYP:
                gb->joyp_accessed = true;
                GB_timing_sync(gb);
                if (unlikely(gb->joyp_switching_delay)) {
                    return (gb->io_registers[addr & 0xFF] & ~0x30) | (gb->joyp_switch_value & 0x30);
                }
                return gb->io_registers[addr & 0xFF];
            case GB_IO_TMA:
            case GB_IO_LCDC:
            case GB_IO_SCY:
            case GB_IO_SCX:
            case GB_IO_LY:
            case GB_IO_LYC:
            case GB_IO_BGP:
            case GB_IO_OBP0:
            case GB_IO_OBP1:
            case GB_IO_WY:
            case GB_IO_WX:
            case GB_IO_SC:
            case GB_IO_SB:
            case GB_IO_DMA:
                return gb->io_registers[addr & 0xFF];
            case GB_IO_TIMA:
                if (gb->tima_reload_state == GB_TIMA_RELOADING) {
                    return 0;
                }
                return gb->io_registers[GB_IO_TIMA];
            case GB_IO_DIV:
                return gb->div_counter >> 8;
            case GB_IO_HDMA5:
                if (!gb->cgb_mode) return 0xFF;
                return ((gb->hdma_on || gb->hdma_on_hblank)? 0 : 0x80) | ((gb->hdma_steps_left - 1) & 0x7F);
            case GB_IO_SVBK:
                if (!gb->cgb_mode) {
                    return 0xFF;
                }

                return gb->io_registers[GB_IO_SVBK];
            case GB_IO_VBK:
                if (!GB_is_cgb(gb)) {
                    return 0xFF;
                }
                return gb->cgb_vram_bank | ~0x1;

            /* Todo: It seems that a CGB in DMG mode can access BGPI and OBPI, but not BGPD and OBPD? */
            case GB_IO_BGPI:
            case GB_IO_OBPI:
                if (!GB_is_cgb(gb)) {
                    return 0xFF;
                }
                return gb->io_registers[addr & 0xFF] | 0x40;

            case GB_IO_BGPD:
            case GB_IO_OBPD:
            {
                if (!gb->cgb_mode && gb->boot_rom_finished) {
                    return 0xFF;
                }
                if (gb->cgb_palettes_blocked) {
                    return 0xFF;
                }
                uint8_t index_reg = (addr & 0xFF) - 1;
                return ((addr & 0xFF) == GB_IO_BGPD?
                       gb->background_palettes_data :
                       gb->object_palettes_data)[gb->io_registers[index_reg] & 0x3F];
            }

            case GB_IO_KEY1:
                if (!gb->cgb_mode) {
                    return 0xFF;
                }
                return (gb->io_registers[GB_IO_KEY1] & 0x7F) | (gb->cgb_double_speed? 0xFE : 0x7E);
            case GB_IO_BANK:
                return 0xFE | gb->boot_rom_finished;
            case GB_IO_RP: {
                if (!gb->cgb_mode) return 0xFF;
                /* You will read your own IR LED if it's on. */
                uint8_t ret = (gb->io_registers[GB_IO_RP] & 0xC1) | 0x2E;
                if (gb->model != GB_MODEL_CGB_E) {
                    ret |= 0x10;
                }
                if (((gb->io_registers[GB_IO_RP] & 0xC0) == 0xC0 && gb->effective_ir_input) && gb->model <= GB_MODEL_CGB_E) {
                    ret &= ~2;
                }
                return ret;
            }
            case GB_IO_PSWX:
            case GB_IO_PSWY:
                return GB_is_cgb(gb)? gb->io_registers[addr & 0xFF] : 0xFF;
            case GB_IO_PSW:
                return gb->cgb_mode? gb->io_registers[addr & 0xFF] : 0xFF;
            case GB_IO_PGB:
                return GB_is_cgb(gb)? gb->io_registers[addr & 0xFF] | 0x8F : 0xFF;
            default:
                if ((addr & 0xFF) >= GB_IO_NR10 && (addr & 0xFF) <= GB_IO_WAV_END) {
                    return GB_apu_read(gb, addr & 0xFF);
                }
                return 0xFF;
        }
        unreachable();
    }

    if (addr == 0xFFFF) {
        /* Interrupt Mask */
        return gb->interrupt_enable;
    }

    /* HRAM */
    return gb->hram[addr - 0xFF80];
}

static read_function_t *const read_map[] =
{
    read_rom,         read_rom,         read_rom, read_rom,         /* 0XXX, 1XXX, 2XXX, 3XXX */
    read_mbc_rom,     read_mbc_rom,     read_mbc_rom, read_mbc_rom, /* 4XXX, 5XXX, 6XXX, 7XXX */
    read_vram,        read_vram,                                    /* 8XXX, 9XXX */
    read_mbc_ram,     read_mbc_ram,                                 /* AXXX, BXXX */
    read_ram,         read_banked_ram,                              /* CXXX, DXXX */
    read_ram,         read_high_memory,                             /* EXXX FXXX */
};

void GB_set_read_memory_callback(GB_gameboy_t *gb, GB_read_memory_callback_t callback)
{
    if (!callback) {
        GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    }
    gb->read_memory_callback = callback;
}

uint8_t GB_read_memory(GB_gameboy_t *gb, uint16_t addr)
{
    GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    
#ifndef GB_DISABLE_DEBUGGER
    if (unlikely(gb->n_watchpoints)) {
        GB_debugger_test_read_watchpoint(gb, addr);
    }
#endif
    if (unlikely(is_addr_in_dma_use(gb, addr))) {
        if (GB_is_cgb(gb) && bus_for_addr(gb, addr) == GB_BUS_MAIN && gb->dma_current_src >= 0xE000) {
            /* This is cart specific! Everdrive 7X on a CGB-A or 0 behaves differently. */
            return 0xFF;
        }

        if (GB_is_cgb(gb) && bus_for_addr(gb, gb->dma_current_src) != GB_BUS_RAM && addr >= 0xC000) {
            // TODO: this should probably affect the DMA dest as well
            addr = ((gb->dma_current_src - 1) & 0x1000) | (addr & 0xFFF) | 0xC000;
        }
        else if (GB_is_cgb(gb) && gb->dma_current_src >= 0xE000 && addr >= 0xC000) {
            // TODO: this should probably affect the DMA dest as well
            addr = ((gb->dma_current_src - 1) & 0x1000) | (addr & 0xFFF) | 0xC000;
        }
        else {
            addr = (gb->dma_current_src - 1);
        }
    }
    uint8_t data = read_map[addr >> 12](gb, addr);
    GB_apply_cheat(gb, addr, &data);
    if (unlikely(gb->read_memory_callback)) {
        data = gb->read_memory_callback(gb, addr, data);
    }
    
    /* TODO: this is very naïve due to my lack of a cart that properly handles open-bus scnenarios,
             but should be good enough */
    if (bus_for_addr(gb, addr) == GB_BUS_MAIN && addr < 0xFF00) {
        if (unlikely(gb->returned_open_bus)) {
            gb->returned_open_bus = false;
        }
        else {
            gb->data_bus = data;
            gb->data_bus_decay_countdown = gb->data_bus_decay;
        }
    }
    return data;
}

uint8_t GB_safe_read_memory(GB_gameboy_t *gb, uint16_t addr)
{
    GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)

    if (unlikely(addr == 0xFF00 + GB_IO_JOYP)) {
        return gb->io_registers[GB_IO_JOYP];
    }
    gb->disable_oam_corruption = true;
    uint8_t data = read_map[addr >> 12](gb, addr);
    gb->disable_oam_corruption = false;
    GB_apply_cheat(gb, addr, &data);
    if (unlikely(gb->read_memory_callback)) {
        data = gb->read_memory_callback(gb, addr, data);
    }
    return data;
}

static void write_mbc(GB_gameboy_t *gb, uint16_t addr, uint8_t value)
{
    switch (gb->cartridge_type->mbc_type) {
        case GB_NO_MBC: return;
        case GB_MBC1:
            switch (addr & 0xF000) {
                case 0x0000: case 0x1000: gb->mbc_ram_enable = (value & 0xF) == 0xA; break;
                case 0x2000: case 0x3000: gb->mbc1.bank_low  = value; break;
                case 0x4000: case 0x5000: gb->mbc1.bank_high = value; break;
                case 0x6000: case 0x7000: gb->mbc1.mode      = value; break;
            }
            break;
        case GB_MBC2:
            switch (addr & 0x4100) {
                case 0x0000: gb->mbc_ram_enable = (value & 0xF) == 0xA; break;
                case 0x0100: gb->mbc2.rom_bank  = value; break;
            }
            break;
        case GB_MBC3:
            switch (addr & 0xF000) {
                case 0x0000: case 0x1000: gb->mbc_ram_enable = (value & 0xF) == 0xA; break;
                case 0x2000: case 0x3000: gb->mbc3.rom_bank  = value; break;
                case 0x4000: case 0x5000:
                    gb->mbc3.ram_bank  = value;
                    gb->mbc3.rtc_mapped = value & 8;
                    break;
                case 0x6000: case 0x7000:
                    memcpy(&gb->rtc_latched, &gb->rtc_real, sizeof(gb->rtc_real));
                    break;
            }
            break;
        case GB_MBC5:
            switch (addr & 0xF000) {
                case 0x0000: case 0x1000: gb->mbc_ram_enable      = value == 0x0A; break;
                case 0x2000:              gb->mbc5.rom_bank_low   = value; break;
                case 0x3000:              gb->mbc5.rom_bank_high  = value; break;
                case 0x4000: case 0x5000:
                    if (gb->cartridge_type->has_rumble) {
                        if (!!(value & 8) != !!gb->rumble_strength) {
                            gb->rumble_strength = gb->rumble_strength? 0 : 3;
                        }
                        value &= 7;
                    }
                    gb->mbc5.ram_bank = value;
                    break;
            }
            break;
        case GB_CAMERA:
            switch (addr & 0xF000) {
                case 0x0000: case 0x1000: gb->mbc_ram_enable = (value & 0xF) == 0xA; break;
                case 0x2000: case 0x3000: gb->mbc5.rom_bank_low   = value; break;
                case 0x4000: case 0x5000:
                    gb->mbc5.ram_bank = value;
                    gb->camera_registers_mapped = (value & 0x10);
                    break;
            }
            break;
        case GB_MBC7:
            switch (addr & 0xF000) {
                case 0x0000: case 0x1000: gb->mbc_ram_enable             = value == 0x0A; break;
                case 0x2000: case 0x3000: gb->mbc7.rom_bank              = value; break;
                case 0x4000: case 0x5000: gb->mbc7.secondary_ram_enable  = value == 0x40; break;
            }
            break;
        case GB_MMM01:
            switch (addr & 0xF000) {
                case 0x0000: case 0x1000:
                    gb->mbc_ram_enable = (value & 0xF) == 0xA;
                    if (!gb->mmm01.locked) {
                        gb->mmm01.ram_bank_mask = value >> 4;
                        gb->mmm01.locked = value & 0x40;
                    }
                    break;
                case 0x2000: case 0x3000:
                    if (!gb->mmm01.locked) {
                        gb->mmm01.rom_bank_mid = value >> 5;
                    }
                    gb->mmm01.rom_bank_low &= (gb->mmm01.rom_bank_mask << 1);
                    gb->mmm01.rom_bank_low |= ~(gb->mmm01.rom_bank_mask << 1) & value;
                    break;
                case 0x4000: case 0x5000:
                    gb->mmm01.ram_bank_low = value | ~gb->mmm01.ram_bank_mask;
                    if (!gb->mmm01.locked) {
                        gb->mmm01.ram_bank_high = value >> 2;
                        gb->mmm01.rom_bank_high = value >> 4;
                        gb->mmm01.mbc1_mode_disable = value & 0x40;
                    }
                    break;
                case 0x6000: case 0x7000:
                    if (!gb->mmm01.mbc1_mode_disable) {
                        gb->mmm01.mbc1_mode = (value & 1);
                    }
                    if (!gb->mmm01.locked) {
                        gb->mmm01.rom_bank_mask = value >> 2;
                        gb->mmm01.multiplex_mode = value & 0x40;
                    }
                    break;
            }
            break;
        case GB_HUC1:
            switch (addr & 0xF000) {
                case 0x0000: case 0x1000: gb->huc1.ir_mode = (value & 0xF) == 0xE; break;
                case 0x2000: case 0x3000: gb->huc1.bank_low  = value; break;
                case 0x4000: case 0x5000: gb->huc1.bank_high = value; break;
            }
            break;
        case GB_HUC3:
            switch (addr & 0xF000) {
                case 0x0000: case 0x1000:
                    gb->huc3.mode = value;
                    gb->mbc_ram_enable = gb->huc3.mode == 0xA;
                    break;
                case 0x2000: case 0x3000: gb->huc3.rom_bank  = value; break;
                case 0x4000: case 0x5000: gb->huc3.ram_bank  = value; break;
            }
            break;
        case GB_TPP1:
            switch (addr & 3) {
                case 0:
                    gb->tpp1.rom_bank &= 0xFF00;
                    gb->tpp1.rom_bank |= value;
                    break;
                case 1:
                    gb->tpp1.rom_bank &= 0xFF;
                    gb->tpp1.rom_bank |= value << 8;
                    break;
                case 2:
                    gb->tpp1.ram_bank = value;
                    break;
                case 3:
                    switch (value) {
                        case 0:
                        case 2:
                        case 3:
                        case 5:
                            gb->tpp1.mode = value;
                            break;
                        case 0x10:
                            memcpy(&gb->rtc_latched, &gb->rtc_real, sizeof(gb->rtc_real));
                            break;
                        case 0x11: {
                            memcpy(&gb->rtc_real, &gb->rtc_latched, sizeof(gb->rtc_real));
                            break;
                        }
                        case 0x14:
                            gb->tpp1_mr4 &= ~0x8;
                            break;
                        case 0x18:
                            gb->tpp1_mr4 &= ~0x4;
                            break;
                        case 0x19:
                            gb->tpp1_mr4 |= 0x4;
                            break;
                            
                        case 0x20:
                        case 0x21:
                        case 0x22:
                        case 0x23:
                            gb->rumble_strength = value & 3;
                            break;
                    }
            }
            break;
            nodefault;
    }
    GB_update_mbc_mappings(gb);
}

static void write_vram(GB_gameboy_t *gb, uint16_t addr, uint8_t value)
{
    GB_display_sync(gb);
    if (unlikely(gb->vram_write_blocked)) {
        //GB_log(gb, "Wrote %02x to %04x (VRAM) during mode 3\n", value, addr);
        return;
    }
    gb->vram[(addr & 0x1FFF) + (gb->cgb_vram_bank? 0x2000 : 0)] = value;
}

static bool huc3_write(GB_gameboy_t *gb, uint8_t value)
{
    switch (gb->huc3.mode) {
        case 0xB: // RTC Write
            switch (value >> 4) {
                case 1:
                    if (gb->huc3.access_index < 3) {
                        gb->huc3.read = (gb->huc3.minutes >> (gb->huc3.access_index  * 4)) & 0xF;
                    }
                    else if (gb->huc3.access_index < 7) {
                        gb->huc3.read = (gb->huc3.days >> ((gb->huc3.access_index - 3) * 4)) & 0xF;
                    }
                    else {
                        // GB_log(gb, "Attempting to read from unsupported HuC-3 register: %03x\n", gb->huc3.access_index);
                    }
                    gb->huc3.access_index++;
                    break;
                case 2:
                case 3:
                    if (gb->huc3.access_index < 3) {
                        gb->huc3.minutes &= ~(0xF << (gb->huc3.access_index * 4));
                        gb->huc3.minutes |= ((value & 0xF) << (gb->huc3.access_index * 4));
                    }
                    else if (gb->huc3.access_index < 7)  {
                        gb->huc3.days &= ~(0xF << ((gb->huc3.access_index - 3) * 4));
                        gb->huc3.days |= ((value & 0xF) << ((gb->huc3.access_index - 3) * 4));
                    }
                    else if (gb->huc3.access_index >= 0x58 && gb->huc3.access_index <= 0x5A) {
                        gb->huc3.alarm_minutes &= ~(0xF << ((gb->huc3.access_index - 0x58) * 4));
                        gb->huc3.alarm_minutes |= ((value & 0xF) << ((gb->huc3.access_index - 0x58) * 4));
                    }
                    else if (gb->huc3.access_index >= 0x5B && gb->huc3.access_index <= 0x5E) {
                        gb->huc3.alarm_days &= ~(0xF << ((gb->huc3.access_index - 0x5B) * 4));
                        gb->huc3.alarm_days |= ((value & 0xF) << ((gb->huc3.access_index - 0x5B) * 4));
                    }
                    else if (gb->huc3.access_index == 0x5F) {
                        gb->huc3.alarm_enabled = value & 1;
                    }
                    else {
                        // GB_log(gb, "Attempting to write %x to unsupported HuC-3 register: %03x\n", value & 0xF, gb->huc3.access_index);
                    }
                    if ((value >> 4) == 3) {
                        gb->huc3.access_index++;
                    }
                    break;
                case 4:
                    gb->huc3.access_index &= 0xF0;
                    gb->huc3.access_index |= value & 0xF;
                    break;
                case 5:
                    gb->huc3.access_index &= 0x0F;
                    gb->huc3.access_index |= (value & 0xF) << 4;
                    break;
                case 6:
                    gb->huc3.access_flags = (value & 0xF);
                    break;
                    
                default:
                    break;
            }
            
            return true;
        case 0xD: // RTC status
            // Not sure what writes here mean, they're always 0xFE
            return true;
        case 0xE: { // IR mode
            if (gb->cart_ir != (value & 1)) {
                gb->cart_ir = value & 1;
                if (gb->infrared_callback) {
                    gb->infrared_callback(gb, value & 1);
                }
            }
            return true;
        }
        case 0xC:
            return true;
        default:
            return false;
        case 0: // Disabled
        case 0xA: // RAM
            return false;
    }
}

static void write_mbc7_ram(GB_gameboy_t *gb, uint16_t addr, uint8_t value)
{
    if (!gb->mbc_ram_enable || !gb->mbc7.secondary_ram_enable) return;
    if (addr >= 0xB000) return;
    switch ((addr >> 4) & 0xF) {
        case 0: {
            if (value == 0x55) {
                gb->mbc7.latch_ready = true;
                gb->mbc7.x_latch = gb->mbc7.y_latch = 0x8000;
            }
        }
        case 1: {
            if (value == 0xAA) {
                gb->mbc7.latch_ready = false;
                gb->mbc7.x_latch = 0x81D0 + 0x70 * gb->accelerometer_x;
                gb->mbc7.y_latch = 0x81D0 + 0x70 * gb->accelerometer_y;
            }
        }
        case 8: {
            gb->mbc7.eeprom_cs = value & 0x80;
            gb->mbc7.eeprom_di = value & 2;
            if (gb->mbc7.eeprom_cs) {
                if (!gb->mbc7.eeprom_clk && (value & 0x40)) { // Clocked
                    gb->mbc7.eeprom_do = gb->mbc7.read_bits >> 15;
                    gb->mbc7.read_bits <<= 1;
                    gb->mbc7.read_bits |= 1;
                    if (gb->mbc7.argument_bits_left == 0) {
                        /* Not transferring extra bits for a command*/
                        gb->mbc7.eeprom_command <<= 1;
                        gb->mbc7.eeprom_command |= gb->mbc7.eeprom_di;
                        if (gb->mbc7.eeprom_command & 0x400) {
                            // Got full command
                            switch ((gb->mbc7.eeprom_command >> 6) & 0xF) {
                                case 0x8:
                                case 0x9:
                                case 0xA:
                                case 0xB:
                                    // READ
                                    gb->mbc7.read_bits = LE16(((uint16_t *)gb->mbc_ram)[gb->mbc7.eeprom_command & 0x7F]);
                                    gb->mbc7.eeprom_command = 0;
                                    break;
                                case 0x3: // EWEN (Eeprom Write ENable)
                                    gb->mbc7.eeprom_write_enabled = true;
                                    gb->mbc7.eeprom_command = 0;
                                    break;
                                case 0x0: // EWDS (Eeprom Write DiSable)
                                    gb->mbc7.eeprom_write_enabled = false;
                                    gb->mbc7.eeprom_command = 0;
                                    break;
                                case 0x4:
                                case 0x5:
                                case 0x6:
                                case 0x7:
                                    // WRITE
                                    if (gb->mbc7.eeprom_write_enabled) {
                                        ((uint16_t *)gb->mbc_ram)[gb->mbc7.eeprom_command & 0x7F] = 0;
                                    }
                                    gb->mbc7.argument_bits_left = 16;
                                    // We still need to process this command, don't erase eeprom_command
                                    break;
                                case 0xC:
                                case 0xD:
                                case 0xE:
                                case 0xF:
                                    // ERASE
                                    if (gb->mbc7.eeprom_write_enabled) {
                                        ((uint16_t *)gb->mbc_ram)[gb->mbc7.eeprom_command & 0x7F] = 0xFFFF;
                                        gb->mbc7.read_bits = 0x3FFF; // Emulate some time to settle
                                    }
                                    gb->mbc7.eeprom_command = 0;
                                    break;
                                case 0x2:
                                    // ERAL (ERase ALl)
                                    if (gb->mbc7.eeprom_write_enabled) {
                                        memset(gb->mbc_ram, 0xFF, gb->mbc_ram_size);
                                        ((uint16_t *)gb->mbc_ram)[gb->mbc7.eeprom_command & 0x7F] = 0xFFFF;
                                        gb->mbc7.read_bits = 0xFF; // Emulate some time to settle
                                    }
                                    gb->mbc7.eeprom_command = 0;
                                    break;
                                case 0x1:
                                    // WRAL (WRite ALl)
                                    if (gb->mbc7.eeprom_write_enabled) {
                                        memset(gb->mbc_ram, 0, gb->mbc_ram_size);
                                    }
                                    gb->mbc7.argument_bits_left = 16;
                                    // We still need to process this command, don't erase eeprom_command
                                    break;
                            }
                        }
                    }
                    else {
                        // We're shifting in extra bits for a WRITE/WRAL command
                        gb->mbc7.argument_bits_left--;
                        gb->mbc7.eeprom_do = true;
                        if (gb->mbc7.eeprom_di) {
                            uint16_t bit = LE16(1 << gb->mbc7.argument_bits_left);
                            if (gb->mbc7.eeprom_command & 0x100) {
                                // WRITE
                                ((uint16_t *)gb->mbc_ram)[gb->mbc7.eeprom_command & 0x7F] |= bit;
                            }
                            else {
                                // WRAL
                                for (unsigned i = 0; i < 0x7F; i++) {
                                    ((uint16_t *)gb->mbc_ram)[i] |= bit;
                                }
                            }
                        }
                        if (gb->mbc7.argument_bits_left == 0) { // We're done
                            gb->mbc7.eeprom_command = 0;
                            gb->mbc7.read_bits = (gb->mbc7.eeprom_command & 0x100)? 0xFF : 0x3FFF; // Emulate some time to settle
                        }
                    }
                }
            }
            gb->mbc7.eeprom_clk = value & 0x40;
        }
    }
}

static void write_mbc_ram(GB_gameboy_t *gb, uint16_t addr, uint8_t value)
{
    if (gb->cartridge_type->mbc_type == GB_MBC7) {
        write_mbc7_ram(gb, addr, value);
        return;
    }
    
    if (gb->cartridge_type->mbc_type == GB_HUC3) {
        if (huc3_write(gb, value)) return;
    }
    
    if (gb->camera_registers_mapped) {
        GB_camera_write_register(gb, addr, value);
        return;
    }
    
    if (gb->cartridge_type->mbc_type == GB_TPP1) {
        switch (gb->tpp1.mode) {
            case 3:
                break;
            case 5:
                gb->rtc_latched.data[(addr & 3) ^ 3] = value;
                return;
            default:
                return;
        }
    }
    
    if ((!gb->mbc_ram_enable)
       && gb->cartridge_type->mbc_type != GB_HUC1) return;
    
    if (gb->cartridge_type->mbc_type == GB_HUC1 && gb->huc1.ir_mode) {
        if (gb->cart_ir != (value & 1)) {
            gb->cart_ir = value & 1;
            if (gb->infrared_callback) {
                gb->infrared_callback(gb, value & 1);
            }
        }
        return;
    }

    if (gb->cartridge_type->has_rtc && gb->mbc3.rtc_mapped) {
        if (gb->mbc_ram_bank <= 4) {
            if (gb->mbc_ram_bank == 0) {
                gb->rtc_cycles = 0;
            }
            gb->rtc_real.data[gb->mbc_ram_bank] = value;
        }
        return;
    }

    if (!gb->mbc_ram || !gb->mbc_ram_size) {
        return;
    }

    if (gb->cartridge_type->mbc_type == GB_CAMERA && (gb->camera_registers[GB_CAMERA_SHOOT_AND_1D_FLAGS] & 1)) {
        /* Forbid writing to RAM while the camera is busy. */
        return;
    }

    uint8_t effective_bank = gb->mbc_ram_bank;
    if (gb->cartridge_type->mbc_type == GB_MBC3 && !gb->is_mbc30) {
        if (gb->cartridge_type->has_rtc) {
            if (effective_bank > 3) return;
        }
        effective_bank &= 0x3;
    }

    gb->mbc_ram[((addr & 0x1FFF) + effective_bank * 0x2000) & (gb->mbc_ram_size - 1)] = value;
}

static void write_ram(GB_gameboy_t *gb, uint16_t addr, uint8_t value)
{
    gb->ram[addr & 0x0FFF] = value;
}

static void write_banked_ram(GB_gameboy_t *gb, uint16_t addr, uint8_t value)
{
    gb->ram[(addr & 0x0FFF) + gb->cgb_ram_bank * 0x1000] = value;
}

static void write_oam(GB_gameboy_t *gb, uint8_t addr, uint8_t value)
{
    if (addr < 0xA0) {
        gb->oam[addr] = value;
        return;
    }
    switch (gb->model) {
        case GB_MODEL_CGB_D:
            if (addr >= 0xC0) {
                addr |= 0xF0;
            }
            gb->extra_oam[addr - 0xA0] = value;
            break;
        case GB_MODEL_CGB_C:
        case GB_MODEL_CGB_B:
        case GB_MODEL_CGB_A:
        case GB_MODEL_CGB_0:
            addr &= ~0x18;
            gb->extra_oam[addr - 0xA0] = value;
            break;
        case GB_MODEL_CGB_E:
        case GB_MODEL_AGB_A:
        case GB_MODEL_GBP_A:
        case GB_MODEL_DMG_B:
        case GB_MODEL_MGB:
        case GB_MODEL_SGB_NTSC:
        case GB_MODEL_SGB_PAL:
        case GB_MODEL_SGB_NTSC_NO_SFC:
        case GB_MODEL_SGB_PAL_NO_SFC:
        case GB_MODEL_SGB2:
        case GB_MODEL_SGB2_NO_SFC:
            break;
    }
}

static void write_high_memory(GB_gameboy_t *gb, uint16_t addr, uint8_t value)
{
    if (addr < 0xFE00) {
        GB_log(gb, "Wrote %02x to %04x (RAM Mirror)\n", value, addr);
        write_banked_ram(gb, addr, value);
        return;
    }

    if (addr < 0xFF00) {
        GB_display_sync(gb);
        if (gb->oam_write_blocked) {
            GB_trigger_oam_bug(gb, addr);
            return;
        }
        
        if (GB_is_dma_active(gb)) {
            /* Todo: Does writing to OAM during DMA causes the OAM bug? */
            return;
        }
        
        if (GB_is_cgb(gb)) {
            write_oam(gb, addr, value);
            return;
        }
        
        if (addr < 0xFEA0) {
            if (gb->accessed_oam_row == 0xA0) {
                for (unsigned i = 0; i < 8; i++) {
                    if ((i & 6)  != (addr & 6)) {
                        gb->oam[(addr & 0xF8) + i] = gb->oam[0x98 + i];
                    }
                    else {
                        gb->oam[(addr & 0xF8) + i] = bitwise_glitch(gb->oam[(addr & 0xF8) + i], gb->oam[0x9C], gb->oam[0x98 + i]);
                    }
                }
            }
            
            gb->oam[addr & 0xFF] = value;
            
            if (gb->accessed_oam_row == 0) {
                gb->oam[0] = bitwise_glitch(gb->oam[0],
                                            gb->oam[(addr & 0xF8)],
                                            gb->oam[(addr & 0xFE)]);
                gb->oam[1] = bitwise_glitch(gb->oam[1],
                                            gb->oam[(addr & 0xF8) + 1],
                                            gb->oam[(addr & 0xFE) | 1]);
                for (unsigned i = 2; i < 8; i++) {
                    gb->oam[i] = gb->oam[(addr & 0xF8) + i];
                }
            }
        }
        else if (gb->accessed_oam_row == 0) {
            gb->oam[addr & 0x7] = value;
        }
        return;
    }

    /* Todo: Clean this code up: use a function table and move relevant code to display.c and timing.c
       (APU read and writes are already at apu.c) */
    if (addr < 0xFF80) {
        sync_ppu_if_needed(gb, addr);
        
        /* Hardware registers */
        switch (addr & 0xFF) {
                
            case GB_IO_WX:
                gb->io_registers[addr & 0xFF] = value;
                GB_update_wx_glitch(gb);
                break;
                
            case GB_IO_IF:
            case GB_IO_SCX:
            case GB_IO_SCY:
            case GB_IO_BGP:
            case GB_IO_OBP0:
            case GB_IO_OBP1:
            case GB_IO_SB:
            case GB_IO_PSWX:
            case GB_IO_PSWY:
            case GB_IO_PSW:
            case GB_IO_PGB:
                gb->io_registers[addr & 0xFF] = value;
                return;
            case GB_IO_OPRI:
                if ((!gb->boot_rom_finished || (gb->io_registers[GB_IO_KEY0] & 8)) && GB_is_cgb(gb)) {
                    gb->io_registers[addr & 0xFF] = value;
                    gb->object_priority = (value & 1) ? GB_OBJECT_PRIORITY_X : GB_OBJECT_PRIORITY_INDEX;
                }
                else if (gb->cgb_mode) {
                    gb->io_registers[addr & 0xFF] = value;

                }
                return;
            case GB_IO_WY:
                gb->io_registers[addr & 0xFF] = value;
                gb->wy_check_scheduled = true;
                return;
                
            case GB_IO_LYC:
                /* TODO: Probably completely wrong in double speed mode */
                
                /* TODO: This hack is disgusting */
                if (gb->display_state == 29 && GB_is_cgb(gb)) {
                    gb->ly_for_comparison = 153;
                    GB_STAT_update(gb);
                    gb->ly_for_comparison = 0;
                }
                
                gb->io_registers[addr & 0xFF] = value;
                
                /* These are the states when LY changes, let the display routine call GB_STAT_update for use
                   so it correctly handles T-cycle accurate LYC writes */
                if (!GB_is_cgb(gb) || (
                    gb->display_state != 35 &&
                    gb->display_state != 26 &&
                    gb->display_state != 15 &&
                    gb->display_state != 16)) {
                    
                    /* More hacks to make LYC write conflicts work */
                    if (gb->display_state == 14 && GB_is_cgb(gb)) {
                        gb->ly_for_comparison = 153;
                        GB_STAT_update(gb);
                        gb->ly_for_comparison = -1;
                    }
                    else {
                        GB_STAT_update(gb);
                    }
                }
                return;
                
            case GB_IO_TIMA:
                if (gb->tima_reload_state != GB_TIMA_RELOADED) {
                    gb->io_registers[GB_IO_TIMA] = value;
                }
                return;

            case GB_IO_TMA:
                gb->io_registers[GB_IO_TMA] = value;
                if (gb->tima_reload_state != GB_TIMA_RUNNING) {
                    gb->io_registers[GB_IO_TIMA] = value;
                }
                return;

            case GB_IO_TAC:
                GB_emulate_timer_glitch(gb, gb->io_registers[GB_IO_TAC], value);
                gb->io_registers[GB_IO_TAC] = value;
                return;


            case GB_IO_LCDC:
                if ((value & GB_LCDC_ENABLE) && !(gb->io_registers[GB_IO_LCDC] & GB_LCDC_ENABLE)) {
                    // LCD turned on
                    if (gb->lcd_status_callback) {
                        gb->lcd_status_callback(gb, true);
                    }
                    if (!gb->lcd_disabled_outside_of_vblank &&
                        (gb->cycles_since_vblank_callback > 10 * 456 || GB_is_sgb(gb))) {
                        // Trigger a vblank here so we don't exceed LCDC_PERIOD
                        GB_display_vblank(gb, GB_VBLANK_TYPE_ARTIFICIAL);
                    }

                    gb->display_cycles = 0;
                    gb->display_state = 0;
                    gb->double_speed_alignment = 0;
                    gb->cycles_for_line = 0;
                    if (GB_is_sgb(gb)) {
                        gb->frame_skip_state = GB_FRAMESKIP_FIRST_FRAME_RENDERED;
                    }
                    else {
                        gb->frame_skip_state = GB_FRAMESKIP_LCD_TURNED_ON;
                    }
                    GB_timing_sync(gb);
                }
                else if (!(value & GB_LCDC_ENABLE) && (gb->io_registers[GB_IO_LCDC] & GB_LCDC_ENABLE)) {
                    /* Sync after turning off LCD */
                    if (gb->lcd_status_callback) {
                        gb->lcd_status_callback(gb, false);
                    }
                    gb->double_speed_alignment = 0;
                    if (gb->model <= GB_MODEL_CGB_E) {
                        /* TODO: Verify this, it's a bit... odd */
                        gb->is_odd_frame ^= true;
                    }
                    GB_timing_sync(gb);
                    GB_lcd_off(gb);
                }
                /* Handle disabling objects while already fetching an object */
                if (!GB_is_cgb(gb) && (gb->io_registers[GB_IO_LCDC] & GB_LCDC_OBJ_EN) && !(value & GB_LCDC_OBJ_EN)) {
                    if (gb->during_object_fetch) {
                        gb->cycles_for_line += gb->display_cycles / 2;
                        gb->display_cycles = 0;
                        gb->object_fetch_aborted = true;
                    }
                }
                gb->io_registers[GB_IO_LCDC] = value;
                gb->wy_check_scheduled = true;
                return;

            case GB_IO_STAT:
                gb->io_registers[GB_IO_STAT] &= 7;
                gb->io_registers[GB_IO_STAT] |= value & ~7;
                gb->io_registers[GB_IO_STAT] |= 0x80;
                
                /* Annoying edge timing case */
                if (gb->cgb_double_speed &&
                    gb->display_state == 8 &&
                    gb->oam_search_index == 0 &&
                    gb->display_cycles == 0 &&
                    (value & 0x20)) {
                    gb->mode_for_interrupt = 2;
                    GB_STAT_update(gb);
                    gb->mode_for_interrupt = -1;
                }
                else {
                    GB_STAT_update(gb);
                }
                return;

            case GB_IO_DIV:
                gb->during_div_write = true;
                GB_set_internal_div_counter(gb, 0);
                gb->during_div_write = false;
                /* Reset the div state machine */
                gb->div_state = 0;
                gb->div_cycles = 0;
                return;

            case GB_IO_JOYP:
                if (gb->joyp_write_callback) {
                    gb->joyp_write_callback(gb, value);
                    GB_update_joyp(gb);
                }
                else if ((gb->io_registers[GB_IO_JOYP] & 0x30) != (value & 0x30)) {
                    if (!GB_is_cgb(gb) && !GB_is_sgb(gb)) {
                        if (gb->joyp_switching_delay) {
                            gb->io_registers[GB_IO_JOYP] = (gb->joyp_switch_value & 0xF0) | (gb->io_registers[GB_IO_JOYP] & 0x0F);
                        }
                        gb->joyp_switch_value = value;
                        uint8_t delay = 0;
                        switch (((gb->io_registers[GB_IO_JOYP] & 0x30) >> 4) |
                                ((value & 0x30) >> 2)) {
                            case 0x4: delay = 48; break;
                            case 0x6: delay = gb->model == GB_MODEL_MGB? 56 : 48; break;
                            case 0x8: delay = 24; break;
                            case 0x9: delay = 24; break;
                            case 0xC: delay = 48; break;
                            case 0xD: delay = 24; break;
                            case 0xE: delay = 48; break;
                        }
                        if (delay && gb->model == GB_MODEL_MGB) {
                            delay -= 16;
                        }
                        gb->joyp_switching_delay = MAX(gb->joyp_switching_delay, delay);
                        if (gb->joyp_switching_delay) {
                            value &= gb->io_registers[GB_IO_JOYP];
                            gb->joypad_is_stable = false;
                        }
                    }
                    GB_sgb_write(gb, value);
                    gb->io_registers[GB_IO_JOYP] = (value & 0xF0) | (gb->io_registers[GB_IO_JOYP] & 0x0F);
                    GB_update_joyp(gb);
                }
                return;

            case GB_IO_BANK:
                gb->boot_rom_finished |= value & 1;
                return;

            case GB_IO_KEY0:
                if (GB_is_cgb(gb) && !gb->boot_rom_finished) {
                    gb->cgb_mode = !(value & 0xC); /* The real "contents" of this register aren't quite known yet. */
                    gb->io_registers[GB_IO_KEY0] = value;
                }
                return;

            case GB_IO_DMA:
                gb->dma_restarting = (gb->dma_current_dest != 0xA1 && gb->dma_current_dest != 0xA0);
                gb->dma_cycles = 0;
                gb->dma_cycles_modulo = 2;
                gb->dma_current_dest = 0xFF;
                gb->dma_current_src = value << 8;
                gb->io_registers[GB_IO_DMA] = value;
                GB_STAT_update(gb);
                return;
            case GB_IO_SVBK:
                if (gb->cgb_mode || (GB_is_cgb(gb) && !gb->boot_rom_finished)) {
                    gb->cgb_ram_bank = value & 0x7;
                    if (!gb->cgb_ram_bank) {
                        gb->cgb_ram_bank++;
                    }
                    gb->io_registers[GB_IO_SVBK] = value | ~0x7;
                }
                return;
            case GB_IO_VBK:
                if (!gb->cgb_mode) {
                    return;
                }
                gb->cgb_vram_bank = value & 0x1;
                return;

            case GB_IO_BGPI:
            case GB_IO_OBPI:
                if (!GB_is_cgb(gb)) {
                    return;
                }
                gb->io_registers[addr & 0xFF] = value;
                return;
            case GB_IO_BGPD:
            case GB_IO_OBPD:
                if (!gb->cgb_mode && gb->boot_rom_finished) {
                    /* Todo: Due to the behavior of a broken Game & Watch Gallery 2 ROM on a real CGB. A proper test ROM
                       is required. */
                    return;
                }
                
                uint8_t index_reg = (addr & 0xFF) - 1;
                if (gb->cgb_palettes_blocked) {
                    if (gb->io_registers[index_reg] & 0x80) {
                        gb->io_registers[index_reg]++;
                        gb->io_registers[index_reg] |= 0x80;
                    }
                    return;
                }
                ((addr & 0xFF) == GB_IO_BGPD?
                 gb->background_palettes_data :
                 gb->object_palettes_data)[gb->io_registers[index_reg] & 0x3F] = value;
                GB_palette_changed(gb, (addr & 0xFF) == GB_IO_BGPD, gb->io_registers[index_reg] & 0x3F);
                if (gb->io_registers[index_reg] & 0x80) {
                    gb->io_registers[index_reg]++;
                    gb->io_registers[index_reg] |= 0x80;
                }
                return;
            case GB_IO_KEY1:
                if (!gb->cgb_mode) {
                    return;
                }
                gb->io_registers[GB_IO_KEY1] = value;
                return;
            case GB_IO_HDMA1:
                if (gb->cgb_mode) {
                    gb->hdma_current_src &= 0xF0;
                    gb->hdma_current_src |= value << 8;
                }
                /* Range 0xE*** like 0xF***  and can't overflow (with 0x800 bytes) to anything meaningful */
                if (gb->hdma_current_src >= 0xE000) {
                    gb->hdma_current_src |= 0xF000;
                }
                return;
            case GB_IO_HDMA2:
                if (gb->cgb_mode) {
                    gb->hdma_current_src &= 0xFF00;
                    gb->hdma_current_src |= value & 0xF0;
                }
                return;
            case GB_IO_HDMA3:
                if (gb->cgb_mode) {
                    gb->hdma_current_dest &= 0xF0;
                    gb->hdma_current_dest |= value << 8;
                }
                return;
            case GB_IO_HDMA4:
                if (gb->cgb_mode) {
                    gb->hdma_current_dest &= 0xFF00;
                    gb->hdma_current_dest |= value & 0xF0;
                }
                return;
            case GB_IO_HDMA5:
                if (!gb->cgb_mode) return;
                gb->hdma_steps_left = (value & 0x7F) + 1;
                if ((value & 0x80) == 0 && gb->hdma_on_hblank) {
                    gb->hdma_on_hblank = false;
                    return;
                }
                gb->hdma_on = (value & 0x80) == 0;
                gb->hdma_on_hblank = (value & 0x80) != 0;
                if (gb->hdma_on_hblank && (gb->io_registers[GB_IO_STAT] & 3) == 0 && gb->display_state != 7) {
                    gb->hdma_on = true;
                }
                return;

            /*  TODO: What happens when starting a transfer during external clock?
                TODO: When a cable is connected, the clock of the other side affects "zombie" serial clocking */
            case GB_IO_SC:
                gb->serial_count = 0;
                if (!gb->cgb_mode) {
                    value |= 2;
                }
                if (gb->serial_master_clock) {
                    GB_serial_master_edge(gb);
                }
                gb->io_registers[GB_IO_SC] = value | (~0x83);
                gb->serial_mask = gb->cgb_mode && (value & 2)? 4 : 0x80;
                if ((value & 0x80) && (value & 0x1) ) {
                    if (gb->serial_transfer_bit_start_callback) {
                        gb->serial_transfer_bit_start_callback(gb, gb->io_registers[GB_IO_SB] & 0x80);
                    }
                }

                return;

            case GB_IO_RP: {
                if (!GB_is_cgb(gb)) {
                    return;
                }
                if ((gb->io_registers[GB_IO_RP] ^ value) & 1) {
                    if (gb->infrared_callback) {
                        gb->infrared_callback(gb, value & 1);
                    }
                }
                gb->io_registers[GB_IO_RP] = value;

                return;
            }

            default:
                if ((addr & 0xFF) >= GB_IO_NR10 && (addr & 0xFF) <= GB_IO_WAV_END) {
                    GB_apu_write(gb, addr & 0xFF, value);
                    return;
                }
                GB_log(gb, "Wrote %02x to %04x (HW Register)\n", value, addr);
                return;
        }
    }

    if (addr == 0xFFFF) {
        GB_display_sync(gb);
        /* Interrupt mask */
        gb->interrupt_enable = value;
        return;
    }
    
    /* HRAM */
    gb->hram[addr - 0xFF80] = value;
}



static write_function_t *const write_map[] =
{
    write_mbc,         write_mbc,        write_mbc, write_mbc, /* 0XXX, 1XXX, 2XXX, 3XXX */
    write_mbc,         write_mbc,        write_mbc, write_mbc, /* 4XXX, 5XXX, 6XXX, 7XXX */
    write_vram,        write_vram,                             /* 8XXX, 9XXX */
    write_mbc_ram,     write_mbc_ram,                          /* AXXX, BXXX */
    write_ram,         write_banked_ram,                       /* CXXX, DXXX */
    write_ram,         write_high_memory,                      /* EXXX FXXX */
};

void GB_set_write_memory_callback(GB_gameboy_t *gb, GB_write_memory_callback_t callback)
{
    if (!callback) {
        GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    }
    gb->write_memory_callback = callback;
}

void GB_write_memory(GB_gameboy_t *gb, uint16_t addr, uint8_t value)
{
    GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
#ifndef GB_DISABLE_DEBUGGER
    if (unlikely(gb->n_watchpoints)) {
        GB_debugger_test_write_watchpoint(gb, addr, value);
    }
#endif
    if (bus_for_addr(gb, addr) == GB_BUS_MAIN && addr < 0xFF00) {
        gb->data_bus = value;
        gb->data_bus_decay_countdown = gb->data_bus_decay;
    }
    
    if (unlikely(gb->write_memory_callback)) {
        if (!gb->write_memory_callback(gb, addr, value)) return;
    }
    
    if (unlikely(is_addr_in_dma_use(gb, addr))) {
        bool oam_write = addr >= 0xFE00;
        if (GB_is_cgb(gb) && bus_for_addr(gb, addr) == GB_BUS_MAIN && gb->dma_current_src >= 0xE000) {
            /* This is cart specific! Everdrive 7X on a CGB-A or 0 behaves differently. */
            return;
        }
        
        if (GB_is_cgb(gb) && (gb->dma_current_src < 0xC000 || gb->dma_current_src >= 0xE000) && addr >= 0xC000) {
            // TODO: this should probably affect the DMA dest as well
            addr = ((gb->dma_current_src - 1) & 0x1000) | (addr & 0xFFF) | 0xC000;
            goto write;
        }
        else if (GB_is_cgb(gb) && gb->dma_current_src >= 0xE000 && addr >= 0xC000) {
            // TODO: this should probably affect the DMA dest as well
            addr = ((gb->dma_current_src - 1) & 0x1000) | (addr & 0xFFF) | 0xC000;
        }
        else {
            addr = (gb->dma_current_src - 1);
        }
        if (GB_is_cgb(gb) || addr >= 0xA000) {
            if (addr < 0xA000) {
                gb->oam[gb->dma_current_dest - 1] = 0;
            }
            else if ((gb->model < GB_MODEL_CGB_0 || gb->model == GB_MODEL_CGB_B)) {
                gb->oam[gb->dma_current_dest - 1] &= value;
            }
            else if ((gb->model < GB_MODEL_CGB_C || gb->model > GB_MODEL_CGB_E) && !oam_write) {
                gb->oam[gb->dma_current_dest - 1] = value;
            }
            if (gb->model < GB_MODEL_CGB_E || addr >= 0xA000) return;
        }
    }
write:
    write_map[addr >> 12](gb, addr, value);
}

bool GB_is_dma_active(GB_gameboy_t *gb)
{
    return gb->dma_current_dest != 0xA1;
}

void GB_dma_run(GB_gameboy_t *gb)
{
    if (gb->dma_current_dest == 0xA1) return;
    if (unlikely(gb->halted || gb->stopped)) return;
    signed cycles = gb->dma_cycles + gb->dma_cycles_modulo;
    gb->in_dma_read = true;
    while (unlikely(cycles >= 4)) {
        cycles -= 4;
        if (gb->dma_current_dest >= 0xA0) {
            gb->dma_current_dest++;
            if (gb->display_state == 8) {
                gb->io_registers[GB_IO_STAT] |= 2;
                GB_STAT_update(gb);
            }
            break;
        }
        if (unlikely(gb->hdma_in_progress && (gb->hdma_steps_left > 1 || (gb->hdma_current_dest & 0xF) != 0xF))) {
            gb->dma_current_dest++;
        }
        else if (gb->dma_current_src < 0xE000) {
            gb->oam[gb->dma_current_dest++] = GB_read_memory(gb, gb->dma_current_src);
        }
        else {
            if (GB_is_cgb(gb)) {
                gb->oam[gb->dma_current_dest++] = 0xFF;
            }
            else {
                gb->oam[gb->dma_current_dest++] = GB_read_memory(gb, gb->dma_current_src & ~0x2000);
            }
        }
        
        /* dma_current_src must be the correct value during GB_read_memory */
        gb->dma_current_src++;
        gb->dma_ppu_vram_conflict = false;
    }
    gb->in_dma_read = false;
    gb->dma_cycles_modulo = cycles;
    gb->dma_cycles = 0;
}

void GB_hdma_run(GB_gameboy_t *gb)
{
    unsigned cycles = gb->cgb_double_speed? 4 : 2;
    /* TODO: This portion of code is probably inaccurate because it probably depends on my specific GB-Live32 */
    #if 0
    /* This is a bit cart, revision and unit specific. TODO: what if PC is in cart RAM? */
    if (gb->model < GB_MODEL_CGB_D || gb->pc > 0x8000) {
        gb->data_bus = 0xFF;
    }
    #endif
    gb->addr_for_hdma_conflict = 0xFFFF;
    uint16_t vram_base = gb->cgb_vram_bank? 0x2000 : 0;
    gb->hdma_in_progress = true;
    GB_advance_cycles(gb, cycles);
    while (gb->hdma_on) {
        uint8_t byte = gb->data_bus;
        gb->addr_for_hdma_conflict = 0xFFFF;
        
        if (gb->hdma_current_src < 0x8000 ||
            (gb->hdma_current_src & 0xE000) == 0xC000 ||
            (gb->hdma_current_src & 0xE000) == 0xA000) {
            byte = GB_read_memory(gb, gb->hdma_current_src);
        }
        if (unlikely(GB_is_dma_active(gb)) && (gb->dma_cycles_modulo == 2 || gb->cgb_double_speed)) {
            write_oam(gb, gb->hdma_current_src, byte);
        }
        gb->hdma_current_src++;
        GB_advance_cycles(gb, cycles);
        if (gb->addr_for_hdma_conflict == 0xFFFF /* || ((gb->model & ~GB_MODEL_GBP_BIT) >= GB_MODEL_AGB_B && gb->cgb_double_speed) */) {
            uint16_t addr = (gb->hdma_current_dest++ & 0x1FFF);
            gb->vram[vram_base + addr] = byte;
            // TODO: vram_write_blocked might not be the correct timing
            if (gb->vram_write_blocked /* && (gb->model & ~GB_MODEL_GBP_BIT) < GB_MODEL_AGB_B */) {
                gb->vram[(vram_base ^ 0x2000) + addr] = byte;
            }
        }
        else {
            if (gb->model == GB_MODEL_CGB_E || gb->cgb_double_speed) {
                /*
                    These corruptions revision (unit?) specific in single speed. They happen only on my CGB-E.
                */
                gb->addr_for_hdma_conflict &= 0x1FFF;
                // TODO: there are *some* scenarions in single speed mode where this write doesn't happen. What's the logic?
                uint16_t addr = (gb->hdma_current_dest & gb->addr_for_hdma_conflict & 0x1FFF);
                gb->vram[vram_base + addr] = byte;
                // TODO: vram_write_blocked might not be the correct timing
                if (gb->vram_write_blocked /* && (gb->model & ~GB_MODEL_GBP_BIT) < GB_MODEL_AGB_B */) {
                    gb->vram[(vram_base ^ 0x2000) + addr] = byte;
                }
            }
            gb->hdma_current_dest++;
        }
        
        if ((gb->hdma_current_dest & 0xF) == 0) {
            if (--gb->hdma_steps_left == 0 || gb->hdma_current_dest == 0) {
                gb->hdma_on = false;
                gb->hdma_on_hblank = false;
            }
            else if (gb->hdma_on_hblank) {
                gb->hdma_on = false;
            }
        }
    }
    gb->hdma_in_progress = false; // TODO: timing? (affects VRAM reads)
    if (!gb->cgb_double_speed) {
        GB_advance_cycles(gb, 2);
    }
}
