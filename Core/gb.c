#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#ifndef _WIN32
#include <sys/select.h>
#include <unistd.h>
#endif
#include "random.h"
#include "gb.h"


void GB_attributed_logv(GB_gameboy_t *gb, GB_log_attributes_t attributes, const char *fmt, va_list args)
{
    int errno_backup = errno;
    char *string = NULL;
    vasprintf(&string, fmt, args);
    if (string) {
        if (gb->log_callback) {
            gb->log_callback(gb, string, attributes);
        }
        else {
            /* Todo: Add ANSI escape sequences for attributed text */
            printf("%s", string);
        }
    }
    free(string);
    errno = errno_backup;
}

void GB_attributed_log(GB_gameboy_t *gb, GB_log_attributes_t attributes, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    GB_attributed_logv(gb, attributes, fmt, args);
    va_end(args);
}

void GB_log(GB_gameboy_t *gb, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    GB_attributed_logv(gb, 0, fmt, args);
    va_end(args);
}

#ifndef GB_DISABLE_DEBUGGER
static char *default_input_callback(GB_gameboy_t *gb)
{
    char *expression = NULL;
    size_t size = 0;
    if (gb->debug_stopped) {
        printf(">");
    }

    if (getline(&expression, &size, stdin) == -1) {
        /* The user doesn't have STDIN or used ^D. We make sure the program keeps running. */
        GB_set_async_input_callback(gb, NULL); /* Disable async input */
        return strdup("c");
    }

    if (!expression) {
        return strdup("");
    }

    size_t length = strlen(expression);
    if (expression[length - 1] == '\n') {
        expression[length - 1] = 0;
    }
    
    if (expression[0] == '\x03') {
        GB_debugger_break(gb);
        free(expression);
        return strdup("");
    }
    return expression;
}

static char *default_async_input_callback(GB_gameboy_t *gb)
{
#ifndef _WIN32
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    struct timeval time = {0,};
    if (select(1, &set, NULL, NULL, &time) == 1) {
        if (feof(stdin)) {
            GB_set_async_input_callback(gb, NULL); /* Disable async input */
            return NULL;
        }
        return default_input_callback(gb);
    }
#endif
    return NULL;
}
#endif

static void load_default_border(GB_gameboy_t *gb)
{
    if (gb->has_sgb_border) return;
    
    #define LOAD_BORDER() do { \
        memcpy(gb->borrowed_border.map, tilemap, sizeof(tilemap));\
        memcpy(gb->borrowed_border.palette, palette, sizeof(palette));\
        memcpy(gb->borrowed_border.tiles, tiles, sizeof(tiles));\
    } while (false);
    
#ifdef GB_BIG_ENDIAN
    for (unsigned i = 0; i < sizeof(gb->borrowed_border.map) / 2; i++) {
        gb->borrowed_border.map[i] = LE16(gb->borrowed_border.map[i]);
    }
    for (unsigned i = 0; i < sizeof(gb->borrowed_border.palette) / 2; i++) {
        gb->borrowed_border.palette[i] = LE16(gb->borrowed_border.palette[i]);
    }
#endif
    
    if (gb->model > GB_MODEL_CGB_E) {
        #include "graphics/agb_border.inc"
        LOAD_BORDER();
    }
    else if (gb->model == GB_MODEL_MGB) {
        #include "graphics/mgb_border.inc"
        LOAD_BORDER();
        if (gb->dmg_palette &&
            gb->dmg_palette->colors[4].b > gb->dmg_palette->colors[4].r) {
            for (unsigned i = 0; i < 7; i++) {
                gb->borrowed_border.map[13 + 24 * 32 + i] = i + 1;
                gb->borrowed_border.map[13 + 25 * 32 + i] = i + 8;
            }
        }
    }
    else if (GB_is_cgb(gb)) {
        #include "graphics/cgb_border.inc"
        LOAD_BORDER();
    }
    else {
        #include "graphics/dmg_border.inc"
        LOAD_BORDER();
    }
}

size_t GB_allocation_size(void)
{
    return sizeof(GB_gameboy_t);
}

GB_gameboy_t *GB_alloc(void)
{
    GB_gameboy_t *ret = malloc(sizeof(*ret));
    ret->magic = 0;
    return ret;
}

GB_gameboy_t *GB_init(GB_gameboy_t *gb, GB_model_t model)
{
    memset(gb, 0, sizeof(*gb));
    gb->model = model;
    if (GB_is_cgb(gb)) {
        gb->ram = malloc(gb->ram_size = 0x1000 * 8);
        gb->vram = malloc(gb->vram_size = 0x2000 * 2);
    }
    else {
        gb->ram = malloc(gb->ram_size = 0x2000);
        gb->vram = malloc(gb->vram_size = 0x2000);
    }

#ifndef GB_DISABLE_DEBUGGER
    gb->input_callback = default_input_callback;
    gb->async_input_callback = default_async_input_callback;
#endif
    gb->cartridge_type = &GB_cart_defs[0]; // Default cartridge type
    gb->clock_multiplier = 1.0;
    gb->apu_output.max_cycles_per_sample = 0x400;
    
    if (model & GB_MODEL_NO_SFC_BIT) {
        /* Disable time syncing. Timing should be done by the SFC emulator. */
        gb->turbo = true;
    }
    
    gb->data_bus_decay = 12;
    
    GB_reset(gb);
    load_default_border(gb);
    return gb;
}

GB_model_t GB_get_model(GB_gameboy_t *gb)
{
    return gb->model;
}

void GB_free(GB_gameboy_t *gb)
{
    GB_ASSERT_NOT_RUNNING(gb)
    gb->magic = 0;
    if (gb->ram) {
        free(gb->ram);
    }
    if (gb->vram) {
        free(gb->vram);
    }
    if (gb->mbc_ram) {
        free(gb->mbc_ram);
    }
    if (gb->rom) {
        free(gb->rom);
    }
    if (gb->sgb) {
        free(gb->sgb);
    }
#ifndef GB_DISABLE_DEBUGGER
    GB_debugger_clear_symbols(gb);
    if (gb->breakpoints) {
        free(gb->breakpoints);
    }
    if (gb->watchpoints) {
        free(gb->watchpoints);
    }
    if (gb->nontrivial_jump_state) {
        free(gb->nontrivial_jump_state);
    }
    if (gb->undo_state) {
        free(gb->undo_state);
    }
#endif
    GB_rewind_reset(gb);
#ifndef GB_DISABLE_CHEATS
    GB_remove_all_cheats(gb);
#endif
#ifndef GB_DISABLE_CHEAT_SEARCH
    GB_cheat_search_reset(gb);
#endif
    GB_stop_audio_recording(gb);
        memset(gb, 0, sizeof(*gb));
}

void GB_dealloc(GB_gameboy_t *gb)
{
    if (GB_is_inited(gb)) {
        GB_free(gb);
    }
    free(gb);
}

int GB_load_boot_rom(GB_gameboy_t *gb, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        GB_log(gb, "Could not open boot ROM: %s.\n", strerror(errno));
        return errno;
    }
    fread(gb->boot_rom, sizeof(gb->boot_rom), 1, f);
    fclose(f);
    return 0;
}

void GB_load_boot_rom_from_buffer(GB_gameboy_t *gb, const unsigned char *buffer, size_t size)
{
    if (size > sizeof(gb->boot_rom)) {
        size = sizeof(gb->boot_rom);
    }
    memset(gb->boot_rom, 0xFF, sizeof(gb->boot_rom));
    memcpy(gb->boot_rom, buffer, size);
}

void GB_borrow_sgb_border(GB_gameboy_t *gb)
{
    if (GB_is_sgb(gb)) return;
    if (gb->border_mode != GB_BORDER_ALWAYS) return;
    if (gb->tried_loading_sgb_border) return;
    gb->tried_loading_sgb_border = true;
    if (gb->rom && gb->rom[0x146] != 3) return; // Not an SGB game, nothing to borrow
    if (!gb->boot_rom_load_callback) return; // Can't borrow a border without this callback
    GB_gameboy_t sgb;
    GB_init(&sgb, GB_MODEL_SGB);
    sgb.cartridge_type = gb->cartridge_type;
    sgb.rom = gb->rom;
    sgb.rom_size = gb->rom_size;
    sgb.turbo = true;
    sgb.turbo_dont_skip = true;
    // sgb.disable_rendering = true;
    
    /* Load the boot ROM using the existing gb object */
    typeof(gb->boot_rom) boot_rom_backup;
    memcpy(boot_rom_backup, gb->boot_rom, sizeof(gb->boot_rom));
    gb->boot_rom_load_callback(gb, GB_BOOT_ROM_SGB);
    memcpy(sgb.boot_rom, gb->boot_rom, sizeof(gb->boot_rom));
    memcpy(gb->boot_rom, boot_rom_backup, sizeof(gb->boot_rom));
    sgb.sgb->intro_animation = -1;
    
    for (unsigned i = 600; i--;) {
        GB_run_frame(&sgb);
        if (sgb.sgb->border_animation) {
            gb->has_sgb_border = true;
            memcpy(&gb->borrowed_border, &sgb.sgb->pending_border, sizeof(gb->borrowed_border));
            gb->borrowed_border.palette[0] = sgb.sgb->effective_palettes[0];
            break;
        }
    }
    
    sgb.rom = NULL;
    sgb.rom_size = 0;
    GB_free(&sgb);
}

static size_t rounded_rom_size(size_t size)
{
    size = (size + 0x3FFF) & ~0x3FFF; /* Round to bank */
    /* And then round to a power of two */
    while (size & (size - 1)) {
        /* I promise this works. */
        size |= size >> 1;
        size++;
    }
    if (size < 0x8000) {
        size = 0x8000;
    }
    return size;
}

int GB_load_rom(GB_gameboy_t *gb, const char *path)
{
    GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    
    FILE *f = fopen(path, "rb");
    if (!f) {
        GB_log(gb, "Could not open ROM: %s.\n", strerror(errno));
        return errno;
    }
    fseek(f, 0, SEEK_END);
    gb->rom_size = rounded_rom_size(ftell(f));
    fseek(f, 0, SEEK_SET);
    if (gb->rom) {
        free(gb->rom);
    }
    gb->rom = malloc(gb->rom_size);
    memset(gb->rom, 0xFF, gb->rom_size); /* Pad with 0xFFs */
    fread(gb->rom, 1, gb->rom_size, f);
    fclose(f);
    GB_configure_cart(gb);
    gb->tried_loading_sgb_border = false;
    gb->has_sgb_border = false;
    load_default_border(gb);
    return 0;
}

#define GBS_ENTRY 0x61
#define GBS_ENTRY_SIZE 13

static void generate_gbs_entry(GB_gameboy_t *gb, uint8_t *data)
{
    memcpy(data, (uint8_t[]) {
        0xCD, // Call $XXXX
        LE16(gb->gbs_header.init_address),
        LE16(gb->gbs_header.init_address) >> 8,
        0x76, // HALT
        0x00, // NOP
        0xAF, // XOR a
        0xE0, // LDH [$FFXX], a
        GB_IO_IF,
        0xCD, // Call $XXXX
        LE16(gb->gbs_header.play_address),
        LE16(gb->gbs_header.play_address) >> 8,
        0x18, // JR pc ± $XX
        -10   // To HALT
    }, GBS_ENTRY_SIZE);
}

void GB_gbs_switch_track(GB_gameboy_t *gb, uint8_t track)
{
    GB_reset(gb);
    GB_write_memory(gb, 0xFF00 + GB_IO_LCDC, GB_LCDC_ENABLE);
    GB_write_memory(gb, 0xFF00 + GB_IO_TAC, gb->gbs_header.TAC);
    GB_write_memory(gb, 0xFF00 + GB_IO_TMA, gb->gbs_header.TMA);
    GB_write_memory(gb, 0xFF00 + GB_IO_NR52, 0x80);
    GB_write_memory(gb, 0xFF00 + GB_IO_NR51, 0xFF);
    GB_write_memory(gb, 0xFF00 + GB_IO_NR50, 0x77);
    memset(gb->ram, 0, gb->ram_size);
    memset(gb->hram, 0, sizeof(gb->hram));
    memset(gb->oam, 0, sizeof(gb->oam));
    if (gb->gbs_header.TAC || gb->gbs_header.TMA) {
        GB_write_memory(gb, 0xFFFF, 0x04);
    }
    else {
        GB_write_memory(gb, 0xFFFF, 0x01);
    }
    if (gb->gbs_header.TAC & 0x80) {
        gb->cgb_double_speed = true; // Might mean double speed mode on a DMG
    }
    if (gb->gbs_header.load_address) {
        gb->sp = LE16(gb->gbs_header.sp);
        gb->pc = GBS_ENTRY;
    }
    else {
        gb->pc = gb->sp = LE16(gb->gbs_header.sp - GBS_ENTRY_SIZE);
        uint8_t entry[GBS_ENTRY_SIZE];
        generate_gbs_entry(gb, entry);
        for (unsigned i = 0; i < sizeof(entry); i++) {
            GB_write_memory(gb, gb->pc + i, entry[i]);
        }
    }
    
    gb->boot_rom_finished = true;
    gb->a = track;
    if (gb->sgb) {
        gb->sgb->intro_animation = GB_SGB_INTRO_ANIMATION_LENGTH;
        gb->sgb->disable_commands = true;
    }
    if (gb->gbs_header.TAC & 0x4) {
        gb->interrupt_enable = 4;
    }
    else {
        gb->interrupt_enable = 1;
    }
}

int GB_load_gbs_from_buffer(GB_gameboy_t *gb, const uint8_t *buffer, size_t size, GB_gbs_info_t *info)
{
    GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    
    if (size < sizeof(gb->gbs_header)) {
        GB_log(gb, "Not a valid GBS file.\n");
        return -1;
    }

    memcpy(&gb->gbs_header, buffer, sizeof(gb->gbs_header));

    if (gb->gbs_header.magic != BE32('GBS\x01') ||
        ((LE16(gb->gbs_header.load_address) < GBS_ENTRY + GBS_ENTRY_SIZE ||
        LE16(gb->gbs_header.load_address) >= 0x8000) &&
        LE16(gb->gbs_header.load_address) != 0)) {
        GB_log(gb, "Not a valid GBS file.\n");
        return -1;
    }

    size_t data_size = size - sizeof(gb->gbs_header);

    gb->rom_size = rounded_rom_size(data_size + LE16(gb->gbs_header.load_address));
    
    if (gb->rom) {
        free(gb->rom);
    }

    gb->rom = malloc(gb->rom_size);
    memset(gb->rom, 0xFF, gb->rom_size); /* Pad with 0xFFs */
    memcpy(gb->rom + LE16(gb->gbs_header.load_address), buffer + sizeof(gb->gbs_header), data_size);
    
    gb->cartridge_type = &GB_cart_defs[0x11];
    if (gb->mbc_ram) {
        free(gb->mbc_ram);
        gb->mbc_ram = NULL;
        gb->mbc_ram_size = 0;
    }
    
    if (gb->cartridge_type->has_ram) {
        gb->mbc_ram_size = 0x2000;
        gb->mbc_ram = malloc(gb->mbc_ram_size);
        memset(gb->mbc_ram, 0xFF, gb->mbc_ram_size);
    }
    
    bool has_interrupts = gb->gbs_header.TAC & 0x40;
    
    if (gb->gbs_header.load_address) {
        // Generate interrupt handlers
        for (unsigned i = 0; i <= (has_interrupts? 0x50 : 0x38); i += 8) {
            gb->rom[i] = 0xC3; // jp $XXXX
            gb->rom[i + 1] = (LE16(gb->gbs_header.load_address) + i);
            gb->rom[i + 2] = (LE16(gb->gbs_header.load_address) + i) >> 8;
        }
        for (unsigned i = has_interrupts? 0x58 : 0x40; i <= 0x60; i += 8) {
            gb->rom[i] = 0xC9; // ret
        }
        
        // Generate entry
        generate_gbs_entry(gb, gb->rom + GBS_ENTRY);
    }

    
    GB_gbs_switch_track(gb, gb->gbs_header.first_track - 1);
    if (info) {
        memset(info, 0, sizeof(*info));
        info->first_track = gb->gbs_header.first_track - 1;
        info->track_count = gb->gbs_header.track_count;
        memcpy(info->title, gb->gbs_header.title, sizeof(gb->gbs_header.title));
        memcpy(info->author, gb->gbs_header.author, sizeof(gb->gbs_header.author));
        memcpy(info->copyright, gb->gbs_header.copyright, sizeof(gb->gbs_header.copyright));
    }
    
    gb->tried_loading_sgb_border = true; // Don't even attempt on GBS files
    gb->has_sgb_border = false;
    load_default_border(gb);
    return 0;
}

int GB_load_gbs(GB_gameboy_t *gb, const char *path, GB_gbs_info_t *info)
{
    GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    
    FILE *f = fopen(path, "rb");
    if (!f) {
        GB_log(gb, "Could not open GBS: %s.\n", strerror(errno));
        return errno;
    }
    fseek(f, 0, SEEK_END);
    size_t file_size = MIN(ftell(f), sizeof(GB_gbs_header_t) + 0x4000 * 0x100); // Cap with the maximum MBC3 ROM size + GBS header
    fseek(f, 0, SEEK_SET);
    uint8_t *file_data = malloc(file_size);
    fread(file_data, 1, file_size, f);
    fclose(f);

    int r = GB_load_gbs_from_buffer(gb, file_data, file_size, info);
    free(file_data);
    return r;
}

int GB_load_isx(GB_gameboy_t *gb, const char *path)
{
    GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    
    FILE *f = fopen(path, "rb");
    if (!f) {
        GB_log(gb, "Could not open ISX file: %s.\n", strerror(errno));
        return errno;
    }
    char magic[4];
#define READ(x) if (fread(&x, sizeof(x), 1, f) != 1) goto error
    fread(magic, 1, sizeof(magic), f);
    

    bool extended = *(uint32_t *)&magic == BE32('ISX ');
    
    fseek(f, extended? 0x20 : 0, SEEK_SET);
    
    
    uint8_t *old_rom = gb->rom;
    uint32_t old_size = gb->rom_size;
    gb->rom = NULL;
    gb->rom_size = 0;
    
    while (true) {
        uint8_t record_type = 0;
        if (fread(&record_type, sizeof(record_type), 1, f) != 1) break;
        switch (record_type) {
            case 0x01: { // Binary
                uint16_t bank;
                uint16_t address;
                uint16_t length;
                uint8_t byte;
                READ(byte);
                bank = byte;
                if (byte >= 0x80) {
                    READ(byte);
                    /* TODO: This is just a guess, the docs don't elaborate on how banks > 0xFF are saved,
                       other than the fact that banks >= 80 requires two bytes to store them, and I haven't
                       encountered an ISX file for a ROM larger than 4MBs yet. */
                    bank += byte << 7;
                }
                
                READ(address);
                address = LE16(address);
                if (bank) {
                    address &= 0x3FFF;
                }
                else {
                    address &= 0x7FFF;
                }

                READ(length);
                length = LE16(length);

                size_t needed_size = bank * 0x4000 + address + length;
                if (needed_size > 1024 * 1024 * 32) goto error;
                
                if (gb->rom_size < needed_size) {
                    gb->rom = realloc(gb->rom, needed_size);
                    memset(gb->rom + gb->rom_size, 0, needed_size - gb->rom_size);
                    gb->rom_size = needed_size;
                }
                
                if (fread(gb->rom + (bank * 0x4000 + address), length, 1, f) != 1) goto error;
                
                break;
            }
                
            case 0x11: { // Extended Binary
                uint32_t address;
                uint32_t length;
                
                READ(address);
                address = LE32(address);
                
                READ(length);
                length = LE32(length);
                size_t needed_size = address + length;
                if (needed_size > 1024 * 1024 * 32) goto error;

                if (gb->rom_size < needed_size) {
                    gb->rom = realloc(gb->rom, needed_size);
                    memset(gb->rom + gb->rom_size, 0, needed_size - gb->rom_size);
                    gb->rom_size = needed_size;
                }
                
                if (fread(gb->rom + address, length, 1, f) != 1) goto error;
                
                break;
            }
                
            case 0x04: { // Symbol
                uint16_t count;
                uint8_t length;
                char name[257];
                uint8_t flag;
                uint8_t bank;
                uint16_t address;
                READ(count);
                count = LE16(count);
                while (count--) {
                    READ(length);
                    if (fread(name, length, 1, f) != 1) goto error;
                    name[length] = 0;
                    READ(flag); // unused
                    
                    READ(bank);
                    
                    READ(address);
                    address = LE16(address);
                    GB_debugger_add_symbol(gb, bank, address, name);
                }
                break;
            }
                
            case 0x14: { // Extended Binary
                uint16_t count;
                uint8_t length;
                char name[257];
                uint8_t flag;
                uint32_t address;
                READ(count);
                count = LE16(count);
                while (count--) {
                    READ(length);
                    if (fread(name, length + 1, 1, f) != 1) goto error;
                    name[length] = 0;
                    READ(flag); // unused
                    
                    READ(address);
                    address = LE32(address);
                    // TODO: How to convert 32-bit addresses to Bank:Address? Needs to tell RAM and ROM apart
                }
                break;
            }
                
            default:
                goto done;
        }
    }
done:;
#undef READ
    if (gb->rom_size == 0) goto error;
    
    size_t needed_size = (gb->rom_size + 0x3FFF) & ~0x3FFF; /* Round to bank */
    
    /* And then round to a power of two */
    while (needed_size & (needed_size - 1)) {
        /* I promise this works. */
        needed_size |= needed_size >> 1;
        needed_size++;
    }
    
    if (needed_size < 0x8000) {
        needed_size = 0x8000;
    }
    
    if (gb->rom_size < needed_size) {
        gb->rom = realloc(gb->rom, needed_size);
        memset(gb->rom + gb->rom_size, 0, needed_size - gb->rom_size);
        gb->rom_size = needed_size;
    }
    
    GB_configure_cart(gb);
    
    // Fix a common wrong MBC error
    if (gb->rom[0x147] == 3) { // MBC1 + RAM + Battery
        bool needs_fix = false;
        if (gb->rom_size >= 0x21 * 0x4000) {
            for (unsigned i = 0x20 * 0x4000; i < 0x21 * 0x4000; i++) {
                if (gb->rom[i]) {
                    needs_fix = true;
                    break;
                }
            }
        }
        if (!needs_fix && gb->rom_size >= 0x41 * 0x4000) {
            for (unsigned i = 0x40 * 0x4000; i < 0x41 * 0x4000; i++) {
                if (gb->rom[i]) {
                    needs_fix = true;
                    break;
                }
            }
        }
        if (!needs_fix && gb->rom_size >= 0x61 * 0x4000) {
            for (unsigned i = 0x60 * 0x4000; i < 0x61 * 0x4000; i++) {
                if (gb->rom[i]) {
                    needs_fix = true;
                    break;
                }
            }
        }
        if (needs_fix) {
            gb->rom[0x147] = 0x10; // MBC3 + RTC + RAM + Battery
            GB_configure_cart(gb);
            gb->rom[0x147] = 0x3;
            GB_log(gb, "ROM claims to use MBC1 but appears to require MBC3 or 5, assuming MBC3.\n");
        }
    }
    
    // Inject a correct checksum, the official linker doesn't always fix it, which breaks the official boot ROMs
    uint8_t original_checksum = gb->rom[0x14d];
    gb->rom[0x14d] = 0;
    for (unsigned addr = 0x0134; addr <= 0x014C; addr++) {
        gb->rom[0x14d] -= gb->rom[addr] + 1;
    }
    
    if (original_checksum != gb->rom[0x14d]) {
        GB_log(gb, "This ROM's header checksum has been automatically corrected\n");
    }
    
    if (old_rom) {
        free(old_rom);
    }
    
    return 0;
error:
    GB_log(gb, "Invalid or unsupported ISX file.\n");
    if (gb->rom) {
        free(gb->rom);
        gb->rom = old_rom;
        gb->rom_size = old_size;
    }
    fclose(f);
    gb->tried_loading_sgb_border = false;
    gb->has_sgb_border = false;
    load_default_border(gb);
    return -1;
}

void GB_load_rom_from_buffer(GB_gameboy_t *gb, const uint8_t *buffer, size_t size)
{
    GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    
    gb->rom_size = rounded_rom_size(size);
    if (gb->rom) {
        free(gb->rom);
    }
    gb->rom = malloc(gb->rom_size);
    memset(gb->rom, 0xFF, gb->rom_size);
    memcpy(gb->rom, buffer, size);
    GB_configure_cart(gb);
    gb->tried_loading_sgb_border = false;
    gb->has_sgb_border = false;
    load_default_border(gb);
}

typedef struct {
    uint8_t seconds;
    uint8_t padding1[3];
    uint8_t minutes;
    uint8_t padding2[3];
    uint8_t hours;
    uint8_t padding3[3];
    uint8_t days;
    uint8_t padding4[3];
    uint8_t high;
    uint8_t padding5[3];
} vba_rtc_time_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint8_t mr4;
    uint8_t reserved;
    uint64_t last_rtc_second;
    uint8_t rtc_data[4];
} tpp1_rtc_save_t;

typedef union {
    struct __attribute__((packed)) {
        GB_rtc_time_t rtc_real;
        time_t last_rtc_second; /* Platform specific endianess and size */
    } sameboy_legacy;
    struct {
        /* Used by VBA versions with 32-bit timestamp*/
        vba_rtc_time_t rtc_real, rtc_latched;
        uint32_t last_rtc_second; /* Always little endian */
    } vba32;
    struct {
        /* Used by BGB and VBA versions with 64-bit timestamp*/
        vba_rtc_time_t rtc_real, rtc_latched;
        uint64_t last_rtc_second; /* Always little endian */
    } vba64;
} rtc_save_t;

static void fill_tpp1_save_data(GB_gameboy_t *gb, tpp1_rtc_save_t *data)
{
    data->magic = BE32('TPP1');
    data->version = BE16(0x100);
    data->mr4 = gb->tpp1_mr4;
    data->reserved = 0;
    data->last_rtc_second = LE64(time(NULL));
    unrolled for (unsigned i = 4; i--;) {
        data->rtc_data[i] = gb->rtc_real.data[i ^ 3];
    }
}

int GB_save_battery_size(GB_gameboy_t *gb)
{
    if (!gb->cartridge_type->has_battery) return 0; // Nothing to save.
    if (gb->cartridge_type->mbc_type == GB_TPP1 && !(gb->rom[0x153] & 8)) return 0; // Nothing to save.

    if (gb->mbc_ram_size == 0 && !gb->cartridge_type->has_rtc) return 0; /* Claims to have battery, but has no RAM or RTC */

    if (gb->cartridge_type->mbc_type == GB_HUC3) {
        return gb->mbc_ram_size + sizeof(GB_huc3_rtc_time_t);
    }
    
    if (gb->cartridge_type->mbc_type == GB_TPP1) {
        return gb->mbc_ram_size + sizeof(tpp1_rtc_save_t);
    }
    
    rtc_save_t rtc_save_size;
    return gb->mbc_ram_size + (gb->cartridge_type->has_rtc ? sizeof(rtc_save_size.vba64) : 0);
}

int GB_save_battery_to_buffer(GB_gameboy_t *gb, uint8_t *buffer, size_t size)
{
    GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)

    if (!gb->cartridge_type->has_battery) return 0; // Nothing to save.
    if (gb->cartridge_type->mbc_type == GB_TPP1 && !(gb->rom[0x153] & 8)) return 0; // Nothing to save.
    if (gb->mbc_ram_size == 0 && !gb->cartridge_type->has_rtc) return 0; /* Claims to have battery, but has no RAM or RTC */

    if (size < GB_save_battery_size(gb)) return EIO;

    memcpy(buffer, gb->mbc_ram, gb->mbc_ram_size);

    if (gb->cartridge_type->mbc_type == GB_TPP1) {
        buffer += gb->mbc_ram_size;
        tpp1_rtc_save_t rtc_save;
        fill_tpp1_save_data(gb, &rtc_save);
        memcpy(buffer, &rtc_save, sizeof(rtc_save));
    }
    else if (gb->cartridge_type->mbc_type == GB_HUC3) {
        buffer += gb->mbc_ram_size;

        GB_huc3_rtc_time_t rtc_save = {
            LE64(gb->last_rtc_second),
            LE16(gb->huc3.minutes),
            LE16(gb->huc3.days),
            LE16(gb->huc3.alarm_minutes),
            LE16(gb->huc3.alarm_days),
            gb->huc3.alarm_enabled,
        };
        memcpy(buffer, &rtc_save, sizeof(rtc_save));
    }
    else if (gb->cartridge_type->has_rtc) {
        rtc_save_t rtc_save = {{{{0,}},},};
        rtc_save.vba64.rtc_real.seconds = gb->rtc_real.seconds;
        rtc_save.vba64.rtc_real.minutes = gb->rtc_real.minutes;
        rtc_save.vba64.rtc_real.hours = gb->rtc_real.hours;
        rtc_save.vba64.rtc_real.days = gb->rtc_real.days;
        rtc_save.vba64.rtc_real.high = gb->rtc_real.high;
        rtc_save.vba64.rtc_latched.seconds = gb->rtc_latched.seconds;
        rtc_save.vba64.rtc_latched.minutes = gb->rtc_latched.minutes;
        rtc_save.vba64.rtc_latched.hours = gb->rtc_latched.hours;
        rtc_save.vba64.rtc_latched.days = gb->rtc_latched.days;
        rtc_save.vba64.rtc_latched.high = gb->rtc_latched.high;
        rtc_save.vba64.last_rtc_second = LE64(time(NULL));
        memcpy(buffer + gb->mbc_ram_size, &rtc_save.vba64, sizeof(rtc_save.vba64));
    }

    return 0;
}

int GB_save_battery(GB_gameboy_t *gb, const char *path)
{
    GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)

    if (!gb->cartridge_type->has_battery) return 0; // Nothing to save.
    if (gb->cartridge_type->mbc_type == GB_TPP1 && !(gb->rom[0x153] & 8)) return 0; // Nothing to save.
    if (gb->mbc_ram_size == 0 && !gb->cartridge_type->has_rtc) return 0; /* Claims to have battery, but has no RAM or RTC */
    FILE *f = fopen(path, "wb");
    if (!f) {
        GB_log(gb, "Could not open battery save: %s.\n", strerror(errno));
        return errno;
    }

    if (fwrite(gb->mbc_ram, 1, gb->mbc_ram_size, f) != gb->mbc_ram_size) {
        fclose(f);
        return EIO;
    }
    if (gb->cartridge_type->mbc_type == GB_TPP1) {
        tpp1_rtc_save_t rtc_save;
        fill_tpp1_save_data(gb, &rtc_save);
        
        if (fwrite(&rtc_save, sizeof(rtc_save), 1, f) != 1) {
            fclose(f);
            return EIO;
        }
    }
    else if (gb->cartridge_type->mbc_type == GB_HUC3) {
        GB_huc3_rtc_time_t rtc_save = {
            LE64(gb->last_rtc_second),
            LE16(gb->huc3.minutes),
            LE16(gb->huc3.days),
            LE16(gb->huc3.alarm_minutes),
            LE16(gb->huc3.alarm_days),
            gb->huc3.alarm_enabled,
        };

        if (fwrite(&rtc_save, sizeof(rtc_save), 1, f) != 1) {
            fclose(f);
            return EIO;
        }
    }
    else if (gb->cartridge_type->has_rtc) {
        rtc_save_t rtc_save = {{{{0,}},},};
        rtc_save.vba64.rtc_real.seconds = gb->rtc_real.seconds;
        rtc_save.vba64.rtc_real.minutes = gb->rtc_real.minutes;
        rtc_save.vba64.rtc_real.hours = gb->rtc_real.hours;
        rtc_save.vba64.rtc_real.days = gb->rtc_real.days;
        rtc_save.vba64.rtc_real.high = gb->rtc_real.high;
        rtc_save.vba64.rtc_latched.seconds = gb->rtc_latched.seconds;
        rtc_save.vba64.rtc_latched.minutes = gb->rtc_latched.minutes;
        rtc_save.vba64.rtc_latched.hours = gb->rtc_latched.hours;
        rtc_save.vba64.rtc_latched.days = gb->rtc_latched.days;
        rtc_save.vba64.rtc_latched.high = gb->rtc_latched.high;
        rtc_save.vba64.last_rtc_second = LE64(time(NULL));
        if (fwrite(&rtc_save.vba64, 1, sizeof(rtc_save.vba64), f) != sizeof(rtc_save.vba64)) {
            fclose(f);
            return EIO;
        }

    }

    fclose(f);
    return 0;
}

static void load_tpp1_save_data(GB_gameboy_t *gb, const tpp1_rtc_save_t *data)
{
    gb->last_rtc_second = LE64(data->last_rtc_second);
    unrolled for (unsigned i = 4; i--;) {
        gb->rtc_real.data[i ^ 3] = data->rtc_data[i];
    }
}

void GB_load_battery_from_buffer(GB_gameboy_t *gb, const uint8_t *buffer, size_t size)
{
    GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    
    memcpy(gb->mbc_ram, buffer, MIN(gb->mbc_ram_size, size));
    if (size <= gb->mbc_ram_size) {
        goto reset_rtc;
    }
    
    if (gb->cartridge_type->mbc_type == GB_TPP1) {
        tpp1_rtc_save_t rtc_save;
        if (size - gb->mbc_ram_size < sizeof(rtc_save)) {
            goto reset_rtc;
        }
        memcpy(&rtc_save, buffer + gb->mbc_ram_size, sizeof(rtc_save));
        
        load_tpp1_save_data(gb, &rtc_save);
        
        if (gb->last_rtc_second > time(NULL)) {
            /* We must reset RTC here, or it will not advance. */
            goto reset_rtc;
        }
        return;
    }
    
    if (gb->cartridge_type->mbc_type == GB_HUC3) {
        GB_huc3_rtc_time_t rtc_save;
        if (size - gb->mbc_ram_size < sizeof(rtc_save)) {
            goto reset_rtc;
        }
        memcpy(&rtc_save, buffer + gb->mbc_ram_size, sizeof(rtc_save));
        gb->last_rtc_second = LE64(rtc_save.last_rtc_second);
        gb->huc3.minutes = LE16(rtc_save.minutes);
        gb->huc3.days = LE16(rtc_save.days);
        gb->huc3.alarm_minutes = LE16(rtc_save.alarm_minutes);
        gb->huc3.alarm_days = LE16(rtc_save.alarm_days);
        gb->huc3.alarm_enabled = rtc_save.alarm_enabled;
        if (gb->last_rtc_second > time(NULL)) {
            /* We must reset RTC here, or it will not advance. */
            goto reset_rtc;
        }
        return;
    }

    rtc_save_t rtc_save;
    memcpy(&rtc_save, buffer + gb->mbc_ram_size, MIN(sizeof(rtc_save), size));
    switch (size - gb->mbc_ram_size) {
        case sizeof(rtc_save.sameboy_legacy):
            memcpy(&gb->rtc_real, &rtc_save.sameboy_legacy.rtc_real, sizeof(gb->rtc_real));
            memcpy(&gb->rtc_latched, &rtc_save.sameboy_legacy.rtc_real, sizeof(gb->rtc_real));
            gb->last_rtc_second = rtc_save.sameboy_legacy.last_rtc_second;
            break;
            
        case sizeof(rtc_save.vba32):
            gb->rtc_real.seconds = rtc_save.vba32.rtc_real.seconds;
            gb->rtc_real.minutes = rtc_save.vba32.rtc_real.minutes;
            gb->rtc_real.hours = rtc_save.vba32.rtc_real.hours;
            gb->rtc_real.days = rtc_save.vba32.rtc_real.days;
            gb->rtc_real.high = rtc_save.vba32.rtc_real.high;
            gb->rtc_latched.seconds = rtc_save.vba32.rtc_latched.seconds;
            gb->rtc_latched.minutes = rtc_save.vba32.rtc_latched.minutes;
            gb->rtc_latched.hours = rtc_save.vba32.rtc_latched.hours;
            gb->rtc_latched.days = rtc_save.vba32.rtc_latched.days;
            gb->rtc_latched.high = rtc_save.vba32.rtc_latched.high;
            gb->last_rtc_second = LE32(rtc_save.vba32.last_rtc_second);
            break;
            
        case sizeof(rtc_save.vba64):
            gb->rtc_real.seconds = rtc_save.vba64.rtc_real.seconds;
            gb->rtc_real.minutes = rtc_save.vba64.rtc_real.minutes;
            gb->rtc_real.hours = rtc_save.vba64.rtc_real.hours;
            gb->rtc_real.days = rtc_save.vba64.rtc_real.days;
            gb->rtc_real.high = rtc_save.vba64.rtc_real.high;
            gb->rtc_latched.seconds = rtc_save.vba64.rtc_latched.seconds;
            gb->rtc_latched.minutes = rtc_save.vba64.rtc_latched.minutes;
            gb->rtc_latched.hours = rtc_save.vba64.rtc_latched.hours;
            gb->rtc_latched.days = rtc_save.vba64.rtc_latched.days;
            gb->rtc_latched.high = rtc_save.vba64.rtc_latched.high;
            gb->last_rtc_second = LE64(rtc_save.vba64.last_rtc_second);
            break;
            
        default:
            goto reset_rtc;
    }
    if (gb->last_rtc_second > time(NULL)) {
        /* We must reset RTC here, or it will not advance. */
        goto reset_rtc;
    }

    if (gb->last_rtc_second < 852076800) { /* 1/1/97. There weren't any RTC games that time,
                                            so if the value we read is lower it means it wasn't
                                            really RTC data. */
        goto reset_rtc;
    }
    GB_rtc_set_time(gb, time(NULL));
    goto exit;
    
reset_rtc:
    gb->last_rtc_second = time(NULL);
    gb->rtc_real.high |= 0x80; /* This gives the game a hint that the clock should be reset. */
    if (gb->cartridge_type->mbc_type == GB_HUC3) {
        gb->huc3.days = 0xFFFF;
        gb->huc3.minutes = 0xFFF;
        gb->huc3.alarm_enabled = false;
    }
exit:
    return;
}

/* Loading will silently stop if the format is incomplete */
int GB_load_battery(GB_gameboy_t *gb, const char *path)
{
    GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    
    FILE *f = fopen(path, "rb");
    if (!f) {
        return errno;
    }

    if (fread(gb->mbc_ram, 1, gb->mbc_ram_size, f) != gb->mbc_ram_size) {
        goto reset_rtc;
    }
    
    if (gb->cartridge_type->mbc_type == GB_TPP1) {
        tpp1_rtc_save_t rtc_save;
        if (fread(&rtc_save, sizeof(rtc_save), 1, f) != 1) {
            goto reset_rtc;
        }
        
        load_tpp1_save_data(gb, &rtc_save);
        
        if (gb->last_rtc_second > time(NULL)) {
            /* We must reset RTC here, or it will not advance. */
            goto reset_rtc;
        }
        return 0;
    }
    
    if (gb->cartridge_type->mbc_type == GB_HUC3) {
        GB_huc3_rtc_time_t rtc_save;
        if (fread(&rtc_save, sizeof(rtc_save), 1, f) != 1) {
            goto reset_rtc;
        }
        gb->last_rtc_second = LE64(rtc_save.last_rtc_second);
        gb->huc3.minutes = LE16(rtc_save.minutes);
        gb->huc3.days = LE16(rtc_save.days);
        gb->huc3.alarm_minutes = LE16(rtc_save.alarm_minutes);
        gb->huc3.alarm_days = LE16(rtc_save.alarm_days);
        gb->huc3.alarm_enabled = rtc_save.alarm_enabled;

        if (gb->last_rtc_second > time(NULL)) {
            /* We must reset RTC here, or it will not advance. */
            goto reset_rtc;
        }
        return 0;
    }

    rtc_save_t rtc_save;
    switch (fread(&rtc_save, 1, sizeof(rtc_save), f)) {
        case sizeof(rtc_save.sameboy_legacy):
            memcpy(&gb->rtc_real, &rtc_save.sameboy_legacy.rtc_real, sizeof(gb->rtc_real));
            memcpy(&gb->rtc_latched, &rtc_save.sameboy_legacy.rtc_real, sizeof(gb->rtc_real));
            gb->last_rtc_second = rtc_save.sameboy_legacy.last_rtc_second;
            break;
            
        case sizeof(rtc_save.vba32):
            gb->rtc_real.seconds = rtc_save.vba32.rtc_real.seconds;
            gb->rtc_real.minutes = rtc_save.vba32.rtc_real.minutes;
            gb->rtc_real.hours = rtc_save.vba32.rtc_real.hours;
            gb->rtc_real.days = rtc_save.vba32.rtc_real.days;
            gb->rtc_real.high = rtc_save.vba32.rtc_real.high;
            gb->rtc_latched.seconds = rtc_save.vba32.rtc_latched.seconds;
            gb->rtc_latched.minutes = rtc_save.vba32.rtc_latched.minutes;
            gb->rtc_latched.hours = rtc_save.vba32.rtc_latched.hours;
            gb->rtc_latched.days = rtc_save.vba32.rtc_latched.days;
            gb->rtc_latched.high = rtc_save.vba32.rtc_latched.high;
            gb->last_rtc_second = LE32(rtc_save.vba32.last_rtc_second);
            break;
            
        case sizeof(rtc_save.vba64):
            gb->rtc_real.seconds = rtc_save.vba64.rtc_real.seconds;
            gb->rtc_real.minutes = rtc_save.vba64.rtc_real.minutes;
            gb->rtc_real.hours = rtc_save.vba64.rtc_real.hours;
            gb->rtc_real.days = rtc_save.vba64.rtc_real.days;
            gb->rtc_real.high = rtc_save.vba64.rtc_real.high;
            gb->rtc_latched.seconds = rtc_save.vba64.rtc_latched.seconds;
            gb->rtc_latched.minutes = rtc_save.vba64.rtc_latched.minutes;
            gb->rtc_latched.hours = rtc_save.vba64.rtc_latched.hours;
            gb->rtc_latched.days = rtc_save.vba64.rtc_latched.days;
            gb->rtc_latched.high = rtc_save.vba64.rtc_latched.high;
            gb->last_rtc_second = LE64(rtc_save.vba64.last_rtc_second);
            break;
            
        default:
            goto reset_rtc;
    }
    if (gb->last_rtc_second > time(NULL)) {
        /* We must reset RTC here, or it will not advance. */
        goto reset_rtc;
    }

    if (gb->last_rtc_second < 852076800) { /* 1/1/97. There weren't any RTC games that time,
                                            so if the value we read is lower it means it wasn't
                                            really RTC data. */
        goto reset_rtc;
    }
    GB_rtc_set_time(gb, time(NULL));
    goto exit;
    
reset_rtc:
    gb->last_rtc_second = time(NULL);
    gb->rtc_real.high |= 0x80; /* This gives the game a hint that the clock should be reset. */
    if (gb->cartridge_type->mbc_type == GB_HUC3) {
        gb->huc3.days = 0xFFFF;
        gb->huc3.minutes = 0xFFF;
        gb->huc3.alarm_enabled = false;
    }
exit:
    fclose(f);
    return 0;
}

unsigned GB_run(GB_gameboy_t *gb)
{
    GB_ASSERT_NOT_RUNNING(gb)
    gb->vblank_just_occured = false;

    if (unlikely(gb->sgb && gb->sgb->intro_animation < 96)) {
        /* On the SGB, the GB is halted after finishing the boot ROM.
           Then, after the boot animation is almost done, it's reset.
           Since the SGB HLE does not perform any header validity checks,
           we just halt the CPU (with hacky code) until the correct time.
           This ensures the Nintendo logo doesn't flash on screen, and
           the game does "run in background" while the animation is playing. */
        
        GB_set_running_thread(gb);
        GB_display_run(gb, 228, true);
        GB_clear_running_thread(gb);
        gb->cycles_since_last_sync += 228;
        return 228;
    }
    
    GB_debugger_run(gb);
    gb->cycles_since_run = 0;
    GB_set_running_thread(gb);
    GB_cpu_run(gb);
    GB_clear_running_thread(gb);
    if (unlikely(gb->vblank_just_occured)) {
        GB_update_faux_analog(gb);
        GB_debugger_handle_async_commands(gb);
        GB_set_running_thread(gb);
        GB_rewind_push(gb);
        GB_clear_running_thread(gb);
    }
    if (!(gb->io_registers[GB_IO_IF] & 0x10) && (gb->io_registers[GB_IO_JOYP] & 0x30) != 0x30) {
        gb->joyp_accessed = true;
    }
    return gb->cycles_since_run;
}

uint64_t GB_run_frame(GB_gameboy_t *gb)
{
    /* Configure turbo temporarily, the user wants to handle FPS capping manually. */
    bool old_turbo = gb->turbo;
    bool old_dont_skip = gb->turbo_dont_skip;
    double old_turbo_cap = gb->turbo_cap_multiplier;
    gb->turbo = true;
    gb->turbo_dont_skip = true;
    gb->turbo_cap_multiplier = 0;
    
    gb->cycles_since_last_sync = 0;
    while (true) {
        GB_run(gb);
        if (gb->vblank_just_occured) {
            break;
        }
    }
    gb->turbo = old_turbo;
    gb->turbo_dont_skip = old_dont_skip;
    gb->turbo_cap_multiplier = old_turbo_cap;
    return gb->cycles_since_last_sync * 1000000000LL / 2 / GB_get_clock_rate(gb); /* / 2 because we use 8MHz units */
}

uint32_t *GB_get_pixels_output(GB_gameboy_t *gb)
{
    return gb->screen;
}

void GB_set_log_callback(GB_gameboy_t *gb, GB_log_callback_t callback)
{
    if (!callback) {
        GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    }
    gb->log_callback = callback;
}

void GB_set_input_callback(GB_gameboy_t *gb, GB_input_callback_t callback)
{
#ifndef GB_DISABLE_DEBUGGER
    if (!callback) {
        GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    }
    if (gb->input_callback == default_input_callback) {
        gb->async_input_callback = NULL;
    }
    gb->input_callback = callback;
#endif
}

void GB_set_async_input_callback(GB_gameboy_t *gb, GB_input_callback_t callback)
{
#ifndef GB_DISABLE_DEBUGGER
    if (!callback) {
        GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    }
    gb->async_input_callback = callback;
#endif
}

void GB_set_execution_callback(GB_gameboy_t *gb, GB_execution_callback_t callback)
{
    if (!callback) {
        GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    }
    gb->execution_callback = callback;
}

void GB_set_lcd_line_callback(GB_gameboy_t *gb, GB_lcd_line_callback_t callback)
{
    if (!callback) {
        GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    }
    gb->lcd_line_callback = callback;
}

void GB_set_lcd_status_callback(GB_gameboy_t *gb, GB_lcd_status_callback_t callback)
{
    if (!callback) {
        GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    }
    gb->lcd_status_callback = callback;
}

void GB_set_infrared_callback(GB_gameboy_t *gb, GB_infrared_callback_t callback)
{
    if (!callback) {
        GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    }
    gb->infrared_callback = callback;
}

void GB_set_infrared_input(GB_gameboy_t *gb, bool state)
{
    gb->infrared_input = state;
}

void GB_set_rumble_callback(GB_gameboy_t *gb, GB_rumble_callback_t callback)
{
    if (!callback) {
        GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    }
    gb->rumble_callback = callback;
}

void GB_set_serial_transfer_bit_start_callback(GB_gameboy_t *gb, GB_serial_transfer_bit_start_callback_t callback)
{
    if (!callback) {
        GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    }
    gb->serial_transfer_bit_start_callback = callback;
}

void GB_set_serial_transfer_bit_end_callback(GB_gameboy_t *gb, GB_serial_transfer_bit_end_callback_t callback)
{
    if (!callback) {
        GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    }
    gb->serial_transfer_bit_end_callback = callback;
}

bool GB_serial_get_data_bit(GB_gameboy_t *gb)
{
    if (!(gb->io_registers[GB_IO_SC] & 0x80)) {
        /* Disabled serial returns 0 bits */
        return false;
    }
    
    if (gb->io_registers[GB_IO_SC] & 1) {
        /* Internal Clock */
        GB_log(gb, "Serial read request while using internal clock. \n");
        return true;
    }
    return gb->io_registers[GB_IO_SB] & 0x80;
}

void GB_serial_set_data_bit(GB_gameboy_t *gb, bool data)
{
    if (!(gb->io_registers[GB_IO_SC] & 0x80)) {
        /* Serial disabled */
        return;
    }

    if (gb->io_registers[GB_IO_SC] & 1) {
        /* Internal Clock */
        GB_log(gb, "Serial write request while using internal clock. \n");
        return;
    }
    
    gb->io_registers[GB_IO_SB] <<= 1;
    gb->io_registers[GB_IO_SB] |= data;
    gb->serial_count++;
    if (gb->serial_count == 8) {
        gb->io_registers[GB_IO_SC] &= ~0x80;
        gb->io_registers[GB_IO_IF] |= 8;
        gb->serial_count = 0;
    }
}

void GB_disconnect_serial(GB_gameboy_t *gb)
{
    GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    gb->serial_transfer_bit_start_callback = NULL;
    gb->serial_transfer_bit_end_callback = NULL;
    
    /* Reset any internally-emulated device. */
    memset(GB_GET_SECTION(gb, accessory), 0, GB_SECTION_SIZE(accessory));
}

GB_accessory_t GB_get_built_in_accessory(GB_gameboy_t *gb)
{
    return gb->accessory;
}

bool GB_is_inited(GB_gameboy_t *gb)
{
    return gb->magic == GB_state_magic();
}

bool GB_is_cgb(const GB_gameboy_t *gb)
{
    return gb->model >= GB_MODEL_CGB_0;
}

bool GB_is_cgb_in_cgb_mode(GB_gameboy_t *gb)
{
    return gb->cgb_mode;
}

bool GB_is_sgb(GB_gameboy_t *gb)
{
    return (gb->model & ~GB_MODEL_PAL_BIT & ~GB_MODEL_NO_SFC_BIT) == GB_MODEL_SGB || (gb->model & ~GB_MODEL_NO_SFC_BIT) == GB_MODEL_SGB2;
}

bool GB_is_hle_sgb(GB_gameboy_t *gb)
{
    return (gb->model & ~GB_MODEL_PAL_BIT) == GB_MODEL_SGB || gb->model == GB_MODEL_SGB2;
}

void GB_set_turbo_mode(GB_gameboy_t *gb, bool on, bool no_frame_skip)
{
    gb->turbo = on;
    gb->turbo_dont_skip = no_frame_skip;
}

void GB_set_turbo_cap(GB_gameboy_t *gb, double multiplier)
{
    gb->turbo_cap_multiplier = multiplier;
}

void GB_set_rendering_disabled(GB_gameboy_t *gb, bool disabled)
{
    gb->disable_rendering = disabled;
}

void *GB_get_user_data(GB_gameboy_t *gb)
{
    return gb->user_data;
}

void GB_set_user_data(GB_gameboy_t *gb, void *data)
{
    gb->user_data = data;
}

static void reset_ram(GB_gameboy_t *gb)
{
    switch (gb->model) {
        case GB_MODEL_MGB:
        case GB_MODEL_CGB_E:
        case GB_MODEL_AGB_A: /* Unverified */
        case GB_MODEL_GBP_A:
            for (unsigned i = 0; i < gb->ram_size; i++) {
                gb->ram[i] = GB_random();
            }
            break;
            
        case GB_MODEL_DMG_B:
        case GB_MODEL_SGB_NTSC: /* Unverified*/
        case GB_MODEL_SGB_PAL: /* Unverified */
        case GB_MODEL_SGB_NTSC_NO_SFC: /* Unverified */
        case GB_MODEL_SGB_PAL_NO_SFC: /* Unverified */
            for (unsigned i = 0; i < gb->ram_size; i++) {
                gb->ram[i] = GB_random();
                if (i & 0x100) {
                    gb->ram[i] &= GB_random();
                }
                else {
                    gb->ram[i] |= GB_random();
                }
            }
            break;
            
        case GB_MODEL_SGB2:
        case GB_MODEL_SGB2_NO_SFC:
            for (unsigned i = 0; i < gb->ram_size; i++) {
                gb->ram[i] = 0x55;
                gb->ram[i] ^= GB_random() & GB_random() & GB_random();
            }
            break;

        case GB_MODEL_CGB_0:
        case GB_MODEL_CGB_A:
        case GB_MODEL_CGB_B:
        case GB_MODEL_CGB_C:
            for (unsigned i = 0; i < gb->ram_size; i++) {
                if ((i & 0x808) == 0x800 || (i & 0x808) == 0x008) {
                    gb->ram[i] = 0;
                }
                else {
                    gb->ram[i] = GB_random() | GB_random() | GB_random() | GB_random() | GB_random();
                }
            }
            break;
        case GB_MODEL_CGB_D:
             for (unsigned i = 0; i < gb->ram_size; i++) {
                gb->ram[i] = GB_random();
                if (i & 0x800) {
                    gb->ram[i] &= GB_random();
                }
                else {
                    gb->ram[i] |= GB_random();
                }
            }
            break;
    }
    
    /* HRAM */
    switch (gb->model) {
        case GB_MODEL_CGB_0:
        case GB_MODEL_CGB_A:
        case GB_MODEL_CGB_B:
        case GB_MODEL_CGB_C:
        case GB_MODEL_CGB_D:
        case GB_MODEL_CGB_E:
        case GB_MODEL_AGB_A:
        case GB_MODEL_GBP_A:
            nounroll for (unsigned i = 0; i < sizeof(gb->hram); i++) {
                gb->hram[i] = GB_random();
            }
            break;
            
        case GB_MODEL_DMG_B:
        case GB_MODEL_MGB:
        case GB_MODEL_SGB_NTSC: /* Unverified*/
        case GB_MODEL_SGB_PAL: /* Unverified */
        case GB_MODEL_SGB_NTSC_NO_SFC: /* Unverified */
        case GB_MODEL_SGB_PAL_NO_SFC: /* Unverified */
        case GB_MODEL_SGB2:
        case GB_MODEL_SGB2_NO_SFC:
            nounroll for (unsigned i = 0; i < sizeof(gb->hram); i++) {
                if (i & 1) {
                    gb->hram[i] = GB_random() | GB_random() | GB_random();
                }
                else {
                    gb->hram[i] = GB_random() & GB_random() & GB_random();
                }
            }
            break;
    }
    
    /* OAM */
    switch (gb->model) {
        case GB_MODEL_CGB_0:
        case GB_MODEL_CGB_A:
        case GB_MODEL_CGB_B:
        case GB_MODEL_CGB_C:
        case GB_MODEL_CGB_D: 
        case GB_MODEL_CGB_E:
        case GB_MODEL_AGB_A:
        case GB_MODEL_GBP_A:
            /* Zero'd out by boot ROM anyway */
            break;
            
        case GB_MODEL_DMG_B:
        case GB_MODEL_MGB:
        case GB_MODEL_SGB_NTSC: /* Unverified */
        case GB_MODEL_SGB_PAL: /* Unverified */
        case GB_MODEL_SGB_NTSC_NO_SFC: /* Unverified */
        case GB_MODEL_SGB_PAL_NO_SFC: /* Unverified */
        case GB_MODEL_SGB2:
        case GB_MODEL_SGB2_NO_SFC:
            for (unsigned i = 0; i < sizeof(gb->oam); i++) {
                if (i & 2) {
                    gb->oam[i] = GB_random() & GB_random() & GB_random();
                }
                else {
                    gb->oam[i] = GB_random() | GB_random() | GB_random();
                }
            }
            break;
    }
    
    /* Wave RAM */
    switch (gb->model) {
        case GB_MODEL_CGB_0:
        case GB_MODEL_CGB_A:
        case GB_MODEL_CGB_B:
        case GB_MODEL_CGB_C:
        case GB_MODEL_CGB_D:
        case GB_MODEL_CGB_E:
        case GB_MODEL_AGB_A:
        case GB_MODEL_GBP_A:
            /* Initialized by CGB-A and newer, 0s in CGB-0 */
            break;
        case GB_MODEL_MGB: {
            nounroll for (unsigned i = 0; i < GB_IO_WAV_END - GB_IO_WAV_START; i++) {
                if (i & 1) {
                    gb->io_registers[GB_IO_WAV_START + i] = GB_random() & GB_random();
                }
                else {
                    gb->io_registers[GB_IO_WAV_START + i] = GB_random() | GB_random();
                }
            }
            break;
        }
        case GB_MODEL_DMG_B:
        case GB_MODEL_SGB_NTSC: /* Unverified*/
        case GB_MODEL_SGB_PAL: /* Unverified */
        case GB_MODEL_SGB_NTSC_NO_SFC: /* Unverified */
        case GB_MODEL_SGB_PAL_NO_SFC: /* Unverified */
        case GB_MODEL_SGB2:
        case GB_MODEL_SGB2_NO_SFC: {
            nounroll for (unsigned i = 0; i < GB_IO_WAV_END - GB_IO_WAV_START; i++) {
                if (i & 1) {
                    gb->io_registers[GB_IO_WAV_START + i] = GB_random() & GB_random() & GB_random();
                }
                else {
                    gb->io_registers[GB_IO_WAV_START + i] = GB_random() | GB_random() | GB_random();
                }
            }
            break;
        }
    }
    
    for (unsigned i = 0; i < sizeof(gb->extra_oam); i++) {
        gb->extra_oam[i] = GB_random();
    }
    
    if (GB_is_cgb(gb)) {
        for (unsigned i = 0; i < 64; i++) {
            gb->background_palettes_data[i] = GB_random(); /* Doesn't really matter as the boot ROM overrides it anyway*/
            gb->object_palettes_data[i] = GB_random();
        }
        for (unsigned i = 0; i < 32; i++) {
            GB_palette_changed(gb, true, i * 2);
            GB_palette_changed(gb, false, i * 2);
        }
    }
    
    if (!gb->cartridge_type->has_battery) {
        memset(gb->mbc_ram, 0xFF, gb->mbc_ram_size);
    }
}

static void request_boot_rom(GB_gameboy_t *gb)
{
    if (gb->boot_rom_load_callback) {
        GB_boot_rom_t type = 0;
        switch (gb->model) {
            case GB_MODEL_DMG_B:
                type = GB_BOOT_ROM_DMG;
                break;
            case GB_MODEL_MGB:
                type = GB_BOOT_ROM_MGB;
                break;
            case GB_MODEL_SGB_NTSC:
            case GB_MODEL_SGB_PAL:
            case GB_MODEL_SGB_NTSC_NO_SFC:
            case GB_MODEL_SGB_PAL_NO_SFC:
                type = GB_BOOT_ROM_SGB;
                break;
            case GB_MODEL_SGB2:
            case GB_MODEL_SGB2_NO_SFC:
                type = GB_BOOT_ROM_SGB2;
                break;
            case GB_MODEL_CGB_0:
                type = GB_BOOT_ROM_CGB_0;
                break;
            case GB_MODEL_CGB_A:
            case GB_MODEL_CGB_B:
            case GB_MODEL_CGB_C:
            case GB_MODEL_CGB_D:
                type = GB_BOOT_ROM_CGB;
                break;
            case GB_MODEL_CGB_E:
                type = GB_BOOT_ROM_CGB_E;
                break;
            case GB_MODEL_AGB_A:
            case GB_MODEL_GBP_A:
                type = GB_BOOT_ROM_AGB;
                break;
        }
        gb->boot_rom_load_callback(gb, type);
    }
}

static void GB_reset_internal(GB_gameboy_t *gb, bool quick)
{
    struct {
        uint8_t hram[sizeof(gb->hram)];
        uint8_t background_palettes_data[sizeof(gb->background_palettes_data)];
        uint8_t object_palettes_data[sizeof(gb->object_palettes_data)];
        uint8_t oam[sizeof(gb->oam)];
        uint8_t extra_oam[sizeof(gb->extra_oam)];
        uint8_t dma, obp0, obp1;
    } *preserved_state = NULL;
        
    if (quick) {
        preserved_state = alloca(sizeof(*preserved_state));
        memcpy(preserved_state->hram, gb->hram, sizeof(gb->hram));
        memcpy(preserved_state->background_palettes_data, gb->background_palettes_data, sizeof(gb->background_palettes_data));
        memcpy(preserved_state->object_palettes_data, gb->object_palettes_data, sizeof(gb->object_palettes_data));
        memcpy(preserved_state->oam, gb->oam, sizeof(gb->oam));
        memcpy(preserved_state->extra_oam, gb->extra_oam, sizeof(gb->extra_oam));
        preserved_state->dma = gb->io_registers[GB_IO_DMA];
        preserved_state->obp0 = gb->io_registers[GB_IO_OBP0];
        preserved_state->obp1 = gb->io_registers[GB_IO_OBP1];
    }
    
    uint32_t mbc_ram_size = gb->mbc_ram_size;
    GB_model_t model = gb->model;
    GB_update_clock_rate(gb);
    uint8_t rtc_section[GB_SECTION_SIZE(rtc)];
    memcpy(rtc_section, GB_GET_SECTION(gb, rtc), sizeof(rtc_section));
    memset(gb, 0, GB_SECTION_OFFSET(unsaved));
    memcpy(GB_GET_SECTION(gb, rtc), rtc_section, sizeof(rtc_section));
    gb->model = model;
    gb->version = STRUCT_VERSION;
    
    GB_reset_mbc(gb);
    
    gb->last_rtc_second = time(NULL);
    gb->cgb_ram_bank = 1;
    gb->io_registers[GB_IO_JOYP] = 0xCF;
    gb->mbc_ram_size = mbc_ram_size;
    if (GB_is_cgb(gb)) {
        gb->ram_size = 0x1000 * 8;
        gb->vram_size = 0x2000 * 2;
        memset(gb->vram, 0, gb->vram_size);
        gb->cgb_mode = true;
        gb->object_priority = GB_OBJECT_PRIORITY_INDEX;
    }
    else {
        gb->ram_size = 0x2000;
        gb->vram_size = 0x2000;
        memset(gb->vram, 0, gb->vram_size);
        gb->object_priority = GB_OBJECT_PRIORITY_X;        
        GB_update_dmg_palette(gb);
    }
    
    gb->serial_mask = 0x80;
    gb->io_registers[GB_IO_SC] = 0x7E;
    gb->accessed_oam_row = -1;
    gb->dma_current_dest = 0xA1;

    if (GB_is_hle_sgb(gb)) {
        if (!gb->sgb) {
            gb->sgb = malloc(sizeof(*gb->sgb));
        }
        memset(gb->sgb, 0, sizeof(*gb->sgb));
        memset(gb->sgb_intro_jingle_phases, 0, sizeof(gb->sgb_intro_jingle_phases));
        gb->sgb_intro_sweep_phase = 0;
        gb->sgb_intro_sweep_previous_sample = 0;
        gb->sgb->intro_animation = -10;
        
        gb->sgb->player_count = 1;
        GB_sgb_load_default_data(gb);

    }
    else {
        if (gb->sgb) {
            free(gb->sgb);
            gb->sgb = NULL;
        }
    }
    
    GB_set_internal_div_counter(gb, 8);
    /* TODO: AGS-101 is inverted in comparison to AGS-001 and AGB */
    gb->is_odd_frame = gb->model > GB_MODEL_CGB_E;

#ifndef GB_DISABLE_DEBUGGER
    if (gb->nontrivial_jump_state) {
        free(gb->nontrivial_jump_state);
        gb->nontrivial_jump_state = NULL;
    }
#endif
    
    if (!quick) {
        reset_ram(gb);
        /* These are not deterministic, but 00 (CGB) and FF (DMG) are the most common initial values by far.
         The retain their previous values on quick resets */
        gb->io_registers[GB_IO_DMA] = gb->io_registers[GB_IO_OBP0] = gb->io_registers[GB_IO_OBP1] = GB_is_cgb(gb)? 0x00 : 0xFF;
    }
    else {
        memcpy(gb->hram, preserved_state->hram, sizeof(gb->hram));
        memcpy(gb->background_palettes_data, preserved_state->background_palettes_data, sizeof(gb->background_palettes_data));
        memcpy(gb->object_palettes_data, preserved_state->object_palettes_data, sizeof(gb->object_palettes_data));
        memcpy(gb->oam, preserved_state->oam, sizeof(gb->oam));
        memcpy(gb->extra_oam, preserved_state->extra_oam, sizeof(gb->extra_oam));
        gb->io_registers[GB_IO_DMA] = preserved_state->dma;
        gb->io_registers[GB_IO_OBP0] = preserved_state->obp0;
        gb->io_registers[GB_IO_OBP1] = preserved_state->obp1;
    }
    gb->apu.apu_cycles_in_2mhz = true;
    
    gb->magic = GB_state_magic();
    request_boot_rom(gb);
    GB_rewind_push(gb);
}

void GB_reset(GB_gameboy_t *gb)
{
    GB_ASSERT_NOT_RUNNING(gb)
    GB_reset_internal(gb, false);
}

void GB_quick_reset(GB_gameboy_t *gb)
{
    GB_ASSERT_NOT_RUNNING(gb)
    GB_reset_internal(gb, true);
}

void GB_switch_model_and_reset(GB_gameboy_t *gb, GB_model_t model)
{
    GB_ASSERT_NOT_RUNNING(gb)
    
#ifndef GB_DISABLE_CHEAT_SEARCH
    GB_cheat_search_reset(gb);
#endif
    
    gb->model = model;
    if (GB_is_cgb(gb)) {
        gb->ram = realloc(gb->ram, gb->ram_size = 0x1000 * 8);
        gb->vram = realloc(gb->vram, gb->vram_size = 0x2000 * 2);
    }
    else {
        gb->ram = realloc(gb->ram, gb->ram_size = 0x2000);
        gb->vram = realloc(gb->vram, gb->vram_size = 0x2000);
    }
#ifndef GB_DISABLE_DEBUGGER
    if (gb->undo_state) {
        free(gb->undo_state);
        gb->undo_state = NULL;
    }
#endif
    GB_rewind_reset(gb);
    GB_reset(gb);
    load_default_border(gb);
}

void *GB_get_direct_access(GB_gameboy_t *gb, GB_direct_access_t access, size_t *size, uint16_t *bank)
{
    /* Set size and bank to dummy pointers if not set */
    size_t dummy_size;
    uint16_t dummy_bank;
    if (!size) {
        size = &dummy_size;
    }
    
    if (!bank) {
        bank = &dummy_bank;
    }
    
    
    switch (access) {
        case GB_DIRECT_ACCESS_ROM:
            *size = gb->rom_size;
            *bank = gb->mbc_rom_bank & (gb->rom_size / 0x4000 - 1);
            return gb->rom;
        case GB_DIRECT_ACCESS_ROM0:
            *size = gb->rom_size;
            *bank = gb->mbc_rom0_bank & (gb->rom_size / 0x4000 - 1);
            return gb->rom;
        case GB_DIRECT_ACCESS_RAM:
            *size = gb->ram_size;
            *bank = gb->cgb_ram_bank;
            return gb->ram;
        case GB_DIRECT_ACCESS_CART_RAM:
            *size = gb->mbc_ram_size;
            *bank = gb->mbc_ram_bank & (gb->mbc_ram_size / 0x2000 - 1);
            return gb->mbc_ram;
        case GB_DIRECT_ACCESS_VRAM:
            *size = gb->vram_size;
            *bank = gb->cgb_vram_bank;
            return gb->vram;
        case GB_DIRECT_ACCESS_HRAM:
            *size = sizeof(gb->hram);
            *bank = 0;
            return &gb->hram;
        case GB_DIRECT_ACCESS_IO:
            *size = sizeof(gb->io_registers);
            *bank = 0;
            return &gb->io_registers;
        case GB_DIRECT_ACCESS_BOOTROM:
            *size = GB_is_cgb(gb)? sizeof(gb->boot_rom) : 0x100;
            *bank = 0;
            return &gb->boot_rom;
        case GB_DIRECT_ACCESS_OAM:
            *size = sizeof(gb->oam);
            *bank = 0;
            return &gb->oam;
        case GB_DIRECT_ACCESS_BGP:
            *size = sizeof(gb->background_palettes_data);
            *bank = 0;
            return &gb->background_palettes_data;
        case GB_DIRECT_ACCESS_OBP:
            *size = sizeof(gb->object_palettes_data);
            *bank = 0;
            return &gb->object_palettes_data;
        case GB_DIRECT_ACCESS_IE:
            *size = sizeof(gb->interrupt_enable);
            *bank = 0;
            return &gb->interrupt_enable;
        default:
            *size = 0;
            *bank = 0;
            return NULL;
    }
}

GB_registers_t *GB_get_registers(GB_gameboy_t *gb)
{
    return (GB_registers_t *)&gb->registers;
}

void GB_set_clock_multiplier(GB_gameboy_t *gb, double multiplier)
{
    if (multiplier != gb->clock_multiplier) {
        gb->clock_multiplier = multiplier;
        GB_update_clock_rate(gb);
    }
}

uint32_t GB_get_clock_rate(GB_gameboy_t *gb)
{
    return gb->clock_rate;
}

uint32_t GB_get_unmultiplied_clock_rate(GB_gameboy_t *gb)
{
    return gb->unmultiplied_clock_rate;
}

void GB_update_clock_rate(GB_gameboy_t *gb)
{
    if (gb->model & GB_MODEL_PAL_BIT) {
        gb->unmultiplied_clock_rate = SGB_PAL_FREQUENCY;
    }
    else if ((gb->model & ~GB_MODEL_NO_SFC_BIT) == GB_MODEL_SGB) {
        gb->unmultiplied_clock_rate = SGB_NTSC_FREQUENCY;
    }
    else {
        gb->unmultiplied_clock_rate = CPU_FREQUENCY;
    }
    
    gb->clock_rate = gb->unmultiplied_clock_rate * gb->clock_multiplier;
    GB_set_sample_rate(gb, gb->apu_output.sample_rate);
}

void GB_set_border_mode(GB_gameboy_t *gb, GB_border_mode_t border_mode)
{
    if (gb->border_mode > GB_BORDER_ALWAYS) return;
    gb->border_mode = border_mode;
}

void GB_set_joyp_write_callback(GB_gameboy_t *gb, GB_joyp_write_callback_t callback)
{
    if (!callback) {
        GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    }
    gb->joyp_write_callback = callback;
}

void GB_set_icd_pixel_callback(GB_gameboy_t *gb, GB_icd_pixel_callback_t callback)
{
    if (!callback) {
        GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    }
    gb->icd_pixel_callback = callback;
}

void GB_set_icd_hreset_callback(GB_gameboy_t *gb, GB_icd_hreset_callback_t callback)
{
    if (!callback) {
        GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    }
    gb->icd_hreset_callback = callback;
}


void GB_set_icd_vreset_callback(GB_gameboy_t *gb, GB_icd_vreset_callback_t callback)
{
    if (!callback) {
        GB_ASSERT_NOT_RUNNING_OTHER_THREAD(gb)
    }
    gb->icd_vreset_callback = callback;
}

void GB_set_boot_rom_load_callback(GB_gameboy_t *gb, GB_boot_rom_load_callback_t callback)
{
    gb->boot_rom_load_callback = callback;
    request_boot_rom(gb);
}

unsigned GB_time_to_alarm(GB_gameboy_t *gb)
{
    if (gb->cartridge_type->mbc_type != GB_HUC3) return 0;
    if (!gb->huc3.alarm_enabled) return 0;
    if (!(gb->huc3.alarm_days & 0x2000)) return 0;
    unsigned current_time = (gb->huc3.days & 0x1FFF) * 24 * 60 * 60 + gb->huc3.minutes * 60 + (time(NULL) % 60);
    unsigned alarm_time = (gb->huc3.alarm_days & 0x1FFF) * 24 * 60 * 60 + gb->huc3.alarm_minutes * 60;
    if (current_time > alarm_time) return 0;
    return alarm_time - current_time;
}

bool GB_rom_supports_alarms(GB_gameboy_t *gb)
{
    return gb->cartridge_type->mbc_type == GB_HUC3;
}

bool GB_has_accelerometer(GB_gameboy_t *gb)
{
    return gb->cartridge_type->mbc_type == GB_MBC7;
}

void GB_set_accelerometer_values(GB_gameboy_t *gb, double x, double y)
{
    gb->accelerometer_x = x;
    gb->accelerometer_y = y;
}

void GB_set_open_bus_decay_time(GB_gameboy_t *gb, uint32_t decay)
{
    gb->data_bus_decay = decay;
}

void GB_get_rom_title(GB_gameboy_t *gb, char *title)
{
    memset(title, 0, 17);
    if (gb->rom_size >= 0x4000) {
        for (unsigned i = 0; i < 0x10; i++) {
            if (gb->rom[0x134 + i] < 0x20 || gb->rom[0x134 + i] >= 0x80) break;
            title[i] = gb->rom[0x134 + i];
        }
    }
}

uint32_t GB_get_rom_crc32(GB_gameboy_t *gb)
{
    static const uint32_t table[] = {
        0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
        0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
        0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
        0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
        0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
        0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
        0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
        0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
        0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
        0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
        0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
        0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
        0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
        0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
        0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
        0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
        0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
        0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
        0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
        0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
        0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
        0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
        0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
        0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
        0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
        0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
        0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
        0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
        0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
        0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
        0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
        0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
        0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
        0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
        0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
        0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
        0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
        0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
        0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
        0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
        0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
        0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
        0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
    };
    
    const uint8_t *byte = gb->rom;
    uint32_t size = gb->rom_size;
    uint32_t ret = 0xFFFFFFFF;
    while (size--) {
        ret = table[(ret ^ *byte++) & 0xFF] ^ (ret >> 8);
    }
    return ~ret;
}


#ifdef GB_CONTEXT_SAFETY
void *GB_get_thread_id(void)
{
    // POSIX requires errno to be thread local, making errno's address unique per thread
    return &errno;
}

void GB_set_running_thread(GB_gameboy_t *gb)
{
    GB_ASSERT_NOT_RUNNING(gb)
    gb->running_thread_id = GB_get_thread_id();
}

void GB_clear_running_thread(GB_gameboy_t *gb)
{
    assert(gb->running_thread_id == GB_get_thread_id());
    gb->running_thread_id = NULL;
}
#endif
