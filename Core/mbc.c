#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "gb.h"

const GB_cartridge_t GB_cart_defs[256] = {
    // From http://gbdev.gg8.se/wiki/articles/The_Cartridge_Header#0147_-_Cartridge_Type
    /* MBC        RAM    BAT.   RTC    RUMB.   */
    {  GB_NO_MBC, false, false, false, false}, // 00h  ROM ONLY
    {  GB_MBC1  , false, false, false, false}, // 01h  MBC1
    {  GB_MBC1  , true , false, false, false}, // 02h  MBC1+RAM
    {  GB_MBC1  , true , true , false, false}, // 03h  MBC1+RAM+BATTERY
    [5] =
    {  GB_MBC2  , true , false, false, false}, // 05h  MBC2
    {  GB_MBC2  , true , true , false, false}, // 06h  MBC2+BATTERY
    [8] =
    {  GB_NO_MBC, true , false, false, false}, // 08h  ROM+RAM
    {  GB_NO_MBC, true , true , false, false}, // 09h  ROM+RAM+BATTERY
    [0xB] =
    {  GB_MMM01 , false, false, false, false}, // 0Bh  MMM01
    {  GB_MMM01 , true , false, false, false}, // 0Ch  MMM01+RAM
    {  GB_MMM01 , true , true , false, false}, // 0Dh  MMM01+RAM+BATTERY
    [0xF] =
    {  GB_MBC3  , false, true,  true , false}, // 0Fh  MBC3+TIMER+BATTERY
    {  GB_MBC3  , true , true,  true , false}, // 10h  MBC3+TIMER+RAM+BATTERY
    {  GB_MBC3  , false, false, false, false}, // 11h  MBC3
    {  GB_MBC3  , true , false, false, false}, // 12h  MBC3+RAM
    {  GB_MBC3  , true , true , false, false}, // 13h  MBC3+RAM+BATTERY
    [0x19] =
    {  GB_MBC5  , false, false, false, false}, // 19h  MBC5
    {  GB_MBC5  , true , false, false, false}, // 1Ah  MBC5+RAM
    {  GB_MBC5  , true , true , false, false}, // 1Bh  MBC5+RAM+BATTERY
    {  GB_MBC5  , false, false, false, true }, // 1Ch  MBC5+RUMBLE
    {  GB_MBC5  , true , false, false, true }, // 1Dh  MBC5+RUMBLE+RAM
    {  GB_MBC5  , true , true , false, true }, // 1Eh  MBC5+RUMBLE+RAM+BATTERY
    [0x22] =
    {  GB_MBC7  , true,  true,  false, false}, // 22h  MBC7+ACCEL+EEPROM
    [0xFC] =
    {  GB_CAMERA, true , true , false, false}, // FCh  POCKET CAMERA
    {  GB_NO_MBC, false, false, false, false}, // FDh  BANDAI TAMA5 (Todo: Not supported)
    {  GB_HUC3  , true , true , true,  false}, // FEh  HuC3
    {  GB_HUC1  , true , true , false, false}, // FFh  HuC1+RAM+BATTERY
};

void GB_update_mbc_mappings(GB_gameboy_t *gb)
{
    switch (gb->cartridge_type->mbc_type) {
        case GB_NO_MBC: return;
        case GB_MBC1:
            switch (gb->mbc1_wiring) {
                case GB_STANDARD_MBC1_WIRING:
                    gb->mbc_rom_bank = gb->mbc1.bank_low | (gb->mbc1.bank_high << 5);
                    if (gb->mbc1.mode == 0) {
                        gb->mbc_ram_bank = 0;
                        gb->mbc_rom0_bank = 0;
                    }
                    else {
                        gb->mbc_ram_bank = gb->mbc1.bank_high;
                        gb->mbc_rom0_bank = gb->mbc1.bank_high << 5;
                    }
                    if ((gb->mbc_rom_bank & 0x1F) == 0) {
                        gb->mbc_rom_bank++;
                    }
                    break;
                case GB_MBC1M_WIRING:
                    gb->mbc_rom_bank = (gb->mbc1.bank_low & 0xF) | (gb->mbc1.bank_high << 4);
                    if (gb->mbc1.mode == 0) {
                        gb->mbc_ram_bank = 0;
                        gb->mbc_rom0_bank = 0;
                    }
                    else {
                        gb->mbc_rom0_bank = gb->mbc1.bank_high << 4;
                        gb->mbc_ram_bank = 0;
                    }
                    if ((gb->mbc1.bank_low & 0x1F) == 0) {
                        gb->mbc_rom_bank++;
                    }
                    break;
                nodefault;
            }
            break;
        case GB_MBC2:
            gb->mbc_rom_bank = gb->mbc2.rom_bank;
            if ((gb->mbc_rom_bank & 0xF) == 0) {
                gb->mbc_rom_bank = 1;
            }
            break;
        case GB_MBC3:
            gb->mbc_rom_bank = gb->mbc3.rom_bank;
            gb->mbc_ram_bank = gb->mbc3.ram_bank;
            if (!gb->is_mbc30) {
                gb->mbc_rom_bank &= 0x7F;
            }
            if (gb->mbc_rom_bank == 0) {
                gb->mbc_rom_bank = 1;
            }
            break;
        case GB_MBC5:
        case GB_CAMERA:
            gb->mbc_rom_bank = gb->mbc5.rom_bank_low | (gb->mbc5.rom_bank_high << 8);
            gb->mbc_ram_bank = gb->mbc5.ram_bank;
            break;
        case GB_MBC7:
            gb->mbc_rom_bank = gb->mbc7.rom_bank;
            break;
        case GB_MMM01:
            if (gb->mmm01.locked) {
                if (gb->mmm01.multiplex_mode) {
                    gb->mbc_rom0_bank = (gb->mmm01.rom_bank_low & (gb->mmm01.rom_bank_mask << 1)) |
                    ((gb->mmm01.mbc1_mode? 0 : gb->mmm01.ram_bank_low) << 5) |
                        (gb->mmm01.rom_bank_high << 7);
                    gb->mbc_rom_bank = gb->mmm01.rom_bank_low |
                        (gb->mmm01.ram_bank_low << 5) |
                        (gb->mmm01.rom_bank_high << 7);
                    gb->mbc_ram_bank = gb->mmm01.rom_bank_mid | (gb->mmm01.ram_bank_high << 2);
                }
                else {
                    gb->mbc_rom0_bank = (gb->mmm01.rom_bank_low & (gb->mmm01.rom_bank_mask << 1)) |
                        (gb->mmm01.rom_bank_mid << 5) |
                        (gb->mmm01.rom_bank_high << 7);
                    gb->mbc_rom_bank = gb->mmm01.rom_bank_low |
                        (gb->mmm01.rom_bank_mid << 5) |
                        (gb->mmm01.rom_bank_high << 7);
                    if (gb->mmm01.mbc1_mode) {
                        gb->mbc_ram_bank = gb->mmm01.ram_bank_low | (gb->mmm01.ram_bank_high << 2);
                    }
                    else {
                        gb->mbc_ram_bank = gb->mmm01.ram_bank_low | (gb->mmm01.ram_bank_high << 2);
                    }
                }
                if (gb->mbc_rom_bank == gb->mbc_rom0_bank) {
                    gb->mbc_rom_bank++;
                }
            }
            else {
                gb->mbc_rom_bank = -1;
                gb->mbc_rom0_bank = -2;
            }
            break;
        case GB_HUC1:
            gb->mbc_rom_bank = gb->huc1.bank_low;
            gb->mbc_ram_bank = gb->huc1.bank_high;
            break;
        case GB_HUC3:
            gb->mbc_rom_bank = gb->huc3.rom_bank;
            gb->mbc_ram_bank = gb->huc3.ram_bank;
            break;
        case GB_TPP1:
            gb->mbc_rom_bank = gb->tpp1.rom_bank;
            gb->mbc_ram_bank = gb->tpp1.ram_bank;
            gb->mbc_ram_enable = (gb->tpp1.mode == 2) || (gb->tpp1.mode == 3);
            break;
        nodefault;
    }
}

void GB_configure_cart(GB_gameboy_t *gb)
{
    memset(GB_GET_SECTION(gb, mbc), 0, GB_SECTION_SIZE(mbc));
    gb->cartridge_type = &GB_cart_defs[gb->rom[0x147]];
    if (gb->cartridge_type->mbc_type == GB_MMM01) {
        uint8_t *temp = malloc(0x8000);
        memcpy(temp, gb->rom, 0x8000);
        memmove(gb->rom, gb->rom + 0x8000, gb->rom_size - 0x8000);
        memcpy(gb->rom + gb->rom_size - 0x8000, temp, 0x8000);
        free(temp);
    }
    else {
        const GB_cartridge_t *maybe_mmm01_type = &GB_cart_defs[gb->rom[gb->rom_size - 0x8000 + 0x147]];
        if (memcmp(gb->rom + 0x104, gb->rom + gb->rom_size - 0x8000 + 0x104, 0x30) == 0) {
            if (maybe_mmm01_type->mbc_type == GB_MMM01) {
                gb->cartridge_type = maybe_mmm01_type;
            }
            else if(gb->rom[gb->rom_size - 0x8000 + 0x147] == 0x11) {
                GB_log(gb, "ROM header reports MBC3, but it appears to be an MMM01 ROM. Assuming cartridge uses MMM01.");
                gb->cartridge_type = &GB_cart_defs[0xB];
            }
        }
    }

    if (gb->rom[0x147] == 0xBC &&
        gb->rom[0x149] == 0xC1 &&
        gb->rom[0x14A] == 0x65) {
        static const GB_cartridge_t tpp1 = {GB_TPP1, true, true, true, true};
        gb->cartridge_type = &tpp1;
        gb->tpp1.rom_bank = 1;
    }
    
    if (gb->cartridge_type->mbc_type != GB_MMM01) {
        if (gb->rom[0x147] == 0 && gb->rom_size > 0x8000) {
            GB_log(gb, "ROM header reports no MBC, but file size is over 32Kb. Assuming cartridge uses MBC3.\n");
            gb->cartridge_type = &GB_cart_defs[0x11];
        }
        else if (gb->rom[0x147] != 0 && memcmp(gb->cartridge_type, &GB_cart_defs[0], sizeof(GB_cart_defs[0])) == 0) {
            GB_log(gb, "Cartridge type %02x is not yet supported.\n", gb->rom[0x147]);
        }
    }
    
    if (!gb->cartridge_type->has_ram &&
        gb->cartridge_type->mbc_type != GB_NO_MBC &&
        gb->cartridge_type->mbc_type != GB_TPP1 &&
        gb->rom[0x149]) {
        GB_log(gb, "ROM header reports no RAM, but also reports a non-zero RAM size. Assuming cartridge has RAM.\n");
        gb->cartridge_type++;
    }
        
    size_t old_mbc_ram_size = gb->mbc_ram_size;
    gb->mbc_ram_size = 0;
    
    if (gb->cartridge_type->has_ram) {
        if (gb->cartridge_type->mbc_type == GB_MBC2) {
            gb->mbc_ram_size = 0x200;
        }
        else if (gb->cartridge_type->mbc_type == GB_MBC7) {
            gb->mbc_ram_size = 0x100;
        }
        else if (gb->cartridge_type->mbc_type == GB_TPP1) {
            if (gb->rom[0x152] >= 1 && gb->rom[0x152] <= 9) {
                gb->mbc_ram_size = 0x2000 << (gb->rom[0x152] - 1);
            }
        }
        else {
            static const unsigned ram_sizes[256] = {0, 0x800, 0x2000, 0x8000, 0x20000, 0x10000};
            if (gb->cartridge_type->mbc_type == GB_MMM01) {
                gb->mbc_ram_size = ram_sizes[gb->rom[gb->rom_size - 0x8000 + 0x149]];
            }
            else {
                gb->mbc_ram_size = ram_sizes[gb->rom[0x149]];
            }
        }
        
        if (gb->mbc_ram_size && gb->mbc_ram_size < 0x2000) {
            GB_log(gb, "This ROM requests a RAM size smaller than a bank, it may misbehave if this was not done intentionally.\n");
        }
        
        if (gb->mbc_ram && old_mbc_ram_size != gb->mbc_ram_size) {
            free(gb->mbc_ram);
            gb->mbc_ram = NULL;
        }
        
        if (gb->mbc_ram_size && !gb->mbc_ram) {
            gb->mbc_ram = malloc(gb->mbc_ram_size);
            /* Todo: Some games assume unintialized MBC RAM is 0xFF. It this true for all cartridge types? */
            memset(gb->mbc_ram, 0xFF, gb->mbc_ram_size);
        }
    }

    /* MBC1 has at least 3 types of wiring (We currently support two (Standard and 4bit-MBC1M) of these).
       See http://forums.nesdev.com/viewtopic.php?f=20&t=14099 */

    /* Attempt to "guess" wiring */
    if (gb->cartridge_type->mbc_type == GB_MBC1) {
        if (gb->rom_size >= 0x44000 && memcmp(gb->rom + 0x104, gb->rom + 0x40104, 0x30) == 0) {
            gb->mbc1_wiring = GB_MBC1M_WIRING;
        }
    }
    
    /* Detect MBC30 */
    if (gb->cartridge_type->mbc_type == GB_MBC3) {
        if (gb->rom_size > 0x200000 || gb->mbc_ram_size > 0x8000) {
            gb->is_mbc30 = true;
        }
    }
    
    GB_reset_mbc(gb);
}

void GB_reset_mbc(GB_gameboy_t *gb)
{
    gb->mbc_rom0_bank = 0;
    if (gb->cartridge_type->mbc_type == GB_MMM01) {
        gb->mbc_rom_bank = -1;
        gb->mbc_rom0_bank = -2;
        gb->mmm01.ram_bank_mask = -1;
    }
    else if (gb->cartridge_type->mbc_type == GB_MBC5 ||
             gb->cartridge_type->mbc_type == GB_CAMERA) {
        gb->mbc5.rom_bank_low = 1;
        gb->mbc_rom_bank = 1;
    }
    else if (gb->cartridge_type->mbc_type == GB_MBC7) {
        gb->mbc7.x_latch = gb->mbc7.y_latch = 0x8000;
        gb->mbc7.latch_ready = true;
        gb->mbc7.read_bits = -1;
        gb->mbc7.eeprom_do = true;
    }
    else {
        gb->mbc_rom_bank = 1;
    }
}
