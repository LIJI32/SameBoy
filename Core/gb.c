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


#ifdef GB_DISABLE_REWIND
#define GB_rewind_free(...)
#define GB_rewind_push(...)
#endif


void GB_attributed_logv(GB_gameboy_t *gb, GB_log_attributes attributes, const char *fmt, va_list args)
{
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
}

void GB_attributed_log(GB_gameboy_t *gb, GB_log_attributes attributes, const char *fmt, ...)
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
        gb->debug_stopped = true;
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

void GB_init(GB_gameboy_t *gb, GB_model_t model)
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
    
    if (model & GB_MODEL_NO_SFC_BIT) {
        /* Disable time syncing. Timing should be done by the SFC emulator. */
        gb->turbo = true;
    }
    
    GB_reset(gb);
    load_default_border(gb);
}

GB_model_t GB_get_model(GB_gameboy_t *gb)
{
    return gb->model;
}

void GB_free(GB_gameboy_t *gb)
{
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
    if (gb->breakpoints) {
        free(gb->breakpoints);
    }
    if (gb->sgb) {
        free(gb->sgb);
    }
    if (gb->nontrivial_jump_state) {
        free(gb->nontrivial_jump_state);
    }
    if (gb->undo_state) {
        free(gb->undo_state);
    }
#ifndef GB_DISABLE_DEBUGGER
    GB_debugger_clear_symbols(gb);
#endif
    GB_rewind_free(gb);
#ifndef GB_DISABLE_CHEATS
    while (gb->cheats) {
        GB_remove_cheat(gb, gb->cheats[0]);
    }
#endif
    memset(gb, 0, sizeof(*gb));
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

int GB_load_rom(GB_gameboy_t *gb, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        GB_log(gb, "Could not open ROM: %s.\n", strerror(errno));
        return errno;
    }
    fseek(f, 0, SEEK_END);
    gb->rom_size = (ftell(f) + 0x3FFF) & ~0x3FFF; /* Round to bank */
    /* And then round to a power of two */
    while (gb->rom_size & (gb->rom_size - 1)) {
        /* I promise this works. */
        gb->rom_size |= gb->rom_size >> 1;
        gb->rom_size++;
    }
    if (gb->rom_size < 0x8000) {
        gb->rom_size = 0x8000;
    }
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
    GB_write_memory(gb, 0xFF00 + GB_IO_LCDC, 0x80);
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
    if (gb->gbs_header.TAC & 0x40) {
        gb->interrupt_enable = true;
    }
}

int GB_load_gbs_from_buffer(GB_gameboy_t *gb, const uint8_t *buffer, size_t size, GB_gbs_info_t *info)
{
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

    gb->rom_size = (data_size + LE16(gb->gbs_header.load_address) + 0x3FFF) & ~0x3FFF; /* Round to bank */
    /* And then round to a power of two */
    while (gb->rom_size & (gb->rom_size - 1)) {
        /* I promise this works. */
        gb->rom_size |= gb->rom_size >> 1;
        gb->rom_size++;
    }
    
    if (gb->rom_size < 0x8000) {
        gb->rom_size = 0x8000;
    }

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
            gb->rom[i] = 0xc3; // jp $XXXX
            gb->rom[i + 1] = (LE16(gb->gbs_header.load_address) + i);
            gb->rom[i + 2] = (LE16(gb->gbs_header.load_address) + i) >> 8;
        }
        for (unsigned i = has_interrupts? 0x58 : 0x40; i <= 0x60; i += 8) {
            gb->rom[i] = 0xc9; // ret
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
                    bank |= byte << 8;
                }
                
                READ(address);
                address = LE16(address);
                address &= 0x3FFF;

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
                uint16_t bank;
                uint16_t address;
                uint8_t byte;
                READ(count);
                count = LE16(count);
                while (count--) {
                    READ(length);
                    if (fread(name, length, 1, f) != 1) goto error;
                    name[length] = 0;
                    READ(flag); // unused
                    
                    READ(byte);
                    bank = byte;
                    if (byte >= 0x80) {
                        READ(byte);
                        bank |= byte << 8;
                    }
                    
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
    gb->rom_size = (size + 0x3fff) & ~0x3fff;
    while (gb->rom_size & (gb->rom_size - 1)) {
        gb->rom_size |= gb->rom_size >> 1;
        gb->rom_size++;
    }
    if (gb->rom_size == 0) {
        gb->rom_size = 0x8000;
    }
    if (gb->rom) {
        free(gb->rom);
    }
    gb->rom = malloc(gb->rom_size);
    memset(gb->rom, 0xff, gb->rom_size);
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

    errno = 0;
    return errno;
}

int GB_save_battery(GB_gameboy_t *gb, const char *path)
{
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

    errno = 0;
    fclose(f);
    return errno;
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
void GB_load_battery(GB_gameboy_t *gb, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return;
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
        return;
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
        return;
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
    return;
}

unsigned GB_run(GB_gameboy_t *gb)
{
    gb->vblank_just_occured = false;

    if (gb->sgb && gb->sgb->intro_animation < 96) {
        /* On the SGB, the GB is halted after finishing the boot ROM.
           Then, after the boot animation is almost done, it's reset.
           Since the SGB HLE does not perform any header validity checks,
           we just halt the CPU (with hacky code) until the correct time.
           This ensures the Nintendo logo doesn't flash on screen, and
           the game does "run in background" while the animation is playing. */
        GB_display_run(gb, 228, true);
        gb->cycles_since_last_sync += 228;
        return 228;
    }
    
    GB_debugger_run(gb);
    gb->cycles_since_run = 0;
    GB_cpu_run(gb);
    if (gb->vblank_just_occured) {
        GB_debugger_handle_async_commands(gb);
        GB_rewind_push(gb);
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
    gb->turbo = true;
    gb->turbo_dont_skip = true;
    
    gb->cycles_since_last_sync = 0;
    while (true) {
        GB_run(gb);
        if (gb->vblank_just_occured) {
            break;
        }
    }
    gb->turbo = old_turbo;
    gb->turbo_dont_skip = old_dont_skip;
    return gb->cycles_since_last_sync * 1000000000LL / 2 / GB_get_clock_rate(gb); /* / 2 because we use 8MHz units */
}

void GB_set_pixels_output(GB_gameboy_t *gb, uint32_t *output)
{
    gb->screen = output;
}

void GB_set_vblank_callback(GB_gameboy_t *gb, GB_vblank_callback_t callback)
{
    gb->vblank_callback = callback;
}

void GB_set_log_callback(GB_gameboy_t *gb, GB_log_callback_t callback)
{
    gb->log_callback = callback;
}

void GB_set_input_callback(GB_gameboy_t *gb, GB_input_callback_t callback)
{
#ifndef GB_DISABLE_DEBUGGER
    if (gb->input_callback == default_input_callback) {
        gb->async_input_callback = NULL;
    }
    gb->input_callback = callback;
#endif
}

void GB_set_async_input_callback(GB_gameboy_t *gb, GB_input_callback_t callback)
{
#ifndef GB_DISABLE_DEBUGGER
    gb->async_input_callback = callback;
#endif
}

void GB_set_execution_callback(GB_gameboy_t *gb, GB_execution_callback_t callback)
{
    gb->execution_callback = callback;
}

void GB_set_lcd_line_callback(GB_gameboy_t *gb, GB_lcd_line_callback_t callback)
{
    gb->lcd_line_callback = callback;
}

const GB_palette_t GB_PALETTE_GREY = {{{0x00, 0x00, 0x00}, {0x55, 0x55, 0x55}, {0xaa, 0xaa, 0xaa}, {0xff, 0xff, 0xff}, {0xff, 0xff, 0xff}}};
const GB_palette_t GB_PALETTE_DMG  = {{{0x08, 0x18, 0x10}, {0x39, 0x61, 0x39}, {0x84, 0xa5, 0x63}, {0xc6, 0xde, 0x8c}, {0xd2, 0xe6, 0xa6}}};
const GB_palette_t GB_PALETTE_MGB  = {{{0x07, 0x10, 0x0e}, {0x3a, 0x4c, 0x3a}, {0x81, 0x8d, 0x66}, {0xc2, 0xce, 0x93}, {0xcf, 0xda, 0xac}}};
const GB_palette_t GB_PALETTE_GBL  = {{{0x0a, 0x1c, 0x15}, {0x35, 0x78, 0x62}, {0x56, 0xb4, 0x95}, {0x7f, 0xe2, 0xc3}, {0x91, 0xea, 0xd0}}};

static void update_dmg_palette(GB_gameboy_t *gb)
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
    update_dmg_palette(gb);
}

const GB_palette_t *GB_get_palette(GB_gameboy_t *gb)
{
    return gb->dmg_palette;
}

void GB_set_rgb_encode_callback(GB_gameboy_t *gb, GB_rgb_encode_callback_t callback)
{

    gb->rgb_encode_callback = callback;
    update_dmg_palette(gb);
    
    for (unsigned i = 0; i < 32; i++) {
        GB_palette_changed(gb, true, i * 2);
        GB_palette_changed(gb, false, i * 2);
    }
}

void GB_set_infrared_callback(GB_gameboy_t *gb, GB_infrared_callback_t callback)
{
    gb->infrared_callback = callback;
}

void GB_set_infrared_input(GB_gameboy_t *gb, bool state)
{
    gb->infrared_input = state;
}

void GB_set_rumble_callback(GB_gameboy_t *gb, GB_rumble_callback_t callback)
{
    gb->rumble_callback = callback;
}

void GB_set_serial_transfer_bit_start_callback(GB_gameboy_t *gb, GB_serial_transfer_bit_start_callback_t callback)
{
    gb->serial_transfer_bit_start_callback = callback;
}

void GB_set_serial_transfer_bit_end_callback(GB_gameboy_t *gb, GB_serial_transfer_bit_end_callback_t callback)
{
    gb->serial_transfer_bit_end_callback = callback;
}

bool GB_serial_get_data_bit(GB_gameboy_t *gb)
{
    if (gb->io_registers[GB_IO_SC] & 1) {
        /* Internal Clock */
        GB_log(gb, "Serial read request while using internal clock. \n");
        return 0xFF;
    }
    return gb->io_registers[GB_IO_SB] & 0x80;
}

void GB_serial_set_data_bit(GB_gameboy_t *gb, bool data)
{
    if (gb->io_registers[GB_IO_SC] & 1) {
        /* Internal Clock */
        GB_log(gb, "Serial write request while using internal clock. \n");
        return;
    }
    gb->io_registers[GB_IO_SB] <<= 1;
    gb->io_registers[GB_IO_SB] |= data;
    gb->serial_count++;
    if (gb->serial_count == 8) {
        gb->io_registers[GB_IO_IF] |= 8;
        gb->serial_count = 0;
    }
}

void GB_disconnect_serial(GB_gameboy_t *gb)
{
    gb->serial_transfer_bit_start_callback = NULL;
    gb->serial_transfer_bit_end_callback = NULL;
    
    /* Reset any internally-emulated device. */
    memset(&gb->printer, 0, sizeof(gb->printer));
    memset(&gb->workboy, 0, sizeof(gb->workboy));
}

bool GB_is_inited(GB_gameboy_t *gb)
{
    return gb->magic == state_magic();
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
            for (unsigned i = 0; i < sizeof(gb->hram); i++) {
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
            for (unsigned i = 0; i < sizeof(gb->hram); i++) {
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
            for (unsigned i = 0; i < 8; i++) {
                if (i & 2) {
                    gb->oam[i] = GB_random() & GB_random() & GB_random();
                }
                else {
                    gb->oam[i] = GB_random() | GB_random() | GB_random();
                }
            }
            for (unsigned i = 8; i < sizeof(gb->oam); i++) {
                gb->oam[i] = gb->oam[i - 8];
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
            /* Initialized by CGB-A and newer, 0s in CGB-0 */
            break;
        case GB_MODEL_MGB: {
            for (unsigned i = 0; i < GB_IO_WAV_END - GB_IO_WAV_START; i++) {
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
            for (unsigned i = 0; i < GB_IO_WAV_END - GB_IO_WAV_START; i++) {
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
            case GB_MODEL_CGB_E:
                type = GB_BOOT_ROM_CGB;
                break;
            case GB_MODEL_AGB_A:
                type = GB_BOOT_ROM_AGB;
                break;
        }
        gb->boot_rom_load_callback(gb, type);
    }
}

void GB_reset(GB_gameboy_t *gb)
{
    uint32_t mbc_ram_size = gb->mbc_ram_size;
    GB_model_t model = gb->model;
    GB_update_clock_rate(gb);
    uint8_t rtc_section[GB_SECTION_SIZE(rtc)];
    memcpy(rtc_section, GB_GET_SECTION(gb, rtc), sizeof(rtc_section));
    memset(gb, 0, (size_t)GB_GET_SECTION((GB_gameboy_t *) 0, unsaved));
    memcpy(GB_GET_SECTION(gb, rtc), rtc_section, sizeof(rtc_section));
    gb->model = model;
    gb->version = GB_STRUCT_VERSION;
    
    gb->mbc_rom_bank = 1;
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
        
        update_dmg_palette(gb);
    }
    reset_ram(gb);
    
    /* The serial interrupt always occur on the 0xF7th cycle of every 0x100 cycle since boot. */
    gb->serial_cycles = 0x100-0xF7;
    gb->io_registers[GB_IO_SC] = 0x7E;
    
    /* These are not deterministic, but 00 (CGB) and FF (DMG) are the most common initial values by far */
    gb->io_registers[GB_IO_DMA] = gb->io_registers[GB_IO_OBP0] = gb->io_registers[GB_IO_OBP1] = GB_is_cgb(gb)? 0x00 : 0xFF;
    
    gb->accessed_oam_row = -1;
    gb->dma_current_dest = 0xa1;

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

    if (gb->nontrivial_jump_state) {
        free(gb->nontrivial_jump_state);
        gb->nontrivial_jump_state = NULL;
    }
    
    gb->magic = state_magic();
    request_boot_rom(gb);
}

void GB_switch_model_and_reset(GB_gameboy_t *gb, GB_model_t model)
{
    gb->model = model;
    if (GB_is_cgb(gb)) {
        gb->ram = realloc(gb->ram, gb->ram_size = 0x1000 * 8);
        gb->vram = realloc(gb->vram, gb->vram_size = 0x2000 * 2);
    }
    else {
        gb->ram = realloc(gb->ram, gb->ram_size = 0x2000);
        gb->vram = realloc(gb->vram, gb->vram_size = 0x2000);
    }
    if (gb->undo_state) {
        free(gb->undo_state);
        gb->undo_state = NULL;
    }
    GB_rewind_free(gb);
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
            *bank = gb->mbc_rom_bank;
            return gb->rom;
        case GB_DIRECT_ACCESS_RAM:
            *size = gb->ram_size;
            *bank = gb->cgb_ram_bank;
            return gb->ram;
        case GB_DIRECT_ACCESS_CART_RAM:
            *size = gb->mbc_ram_size;
            *bank = gb->mbc_ram_bank;
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
    gb->clock_multiplier = multiplier;
    GB_update_clock_rate(gb);
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
}

void GB_set_border_mode(GB_gameboy_t *gb, GB_border_mode_t border_mode)
{
    if (gb->border_mode > GB_BORDER_ALWAYS) return;
    gb->border_mode = border_mode;
}

unsigned GB_get_screen_width(GB_gameboy_t *gb)
{
    switch (gb->border_mode) {
        default:
        case GB_BORDER_SGB:
            return GB_is_hle_sgb(gb)? 256 : 160;
        case GB_BORDER_NEVER:
            return 160;
        case GB_BORDER_ALWAYS:
            return 256;
    }
}

unsigned GB_get_screen_height(GB_gameboy_t *gb)
{
    switch (gb->border_mode) {
        default:
        case GB_BORDER_SGB:
            return GB_is_hle_sgb(gb)? 224 : 144;
        case GB_BORDER_NEVER:
            return 144;
        case GB_BORDER_ALWAYS:
            return 224;
    }
}

unsigned GB_get_player_count(GB_gameboy_t *gb)
{
    return GB_is_hle_sgb(gb)? gb->sgb->player_count : 1;
}

void GB_set_update_input_hint_callback(GB_gameboy_t *gb, GB_update_input_hint_callback_t callback)
{
    gb->update_input_hint_callback = callback;
}

double GB_get_usual_frame_rate(GB_gameboy_t *gb)
{
    return GB_get_clock_rate(gb) / (double)LCDC_PERIOD;
}

void GB_set_joyp_write_callback(GB_gameboy_t *gb, GB_joyp_write_callback_t callback)
{
    gb->joyp_write_callback = callback;
}

void GB_set_icd_pixel_callback(GB_gameboy_t *gb, GB_icd_pixel_callback_t callback)
{
    gb->icd_pixel_callback = callback;
}

void GB_set_icd_hreset_callback(GB_gameboy_t *gb, GB_icd_hreset_callback_t callback)
{
    gb->icd_hreset_callback = callback;
}


void GB_set_icd_vreset_callback(GB_gameboy_t *gb, GB_icd_vreset_callback_t callback)
{
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

bool GB_has_accelerometer(GB_gameboy_t *gb)
{
    return gb->cartridge_type->mbc_type == GB_MBC7;
}

void GB_set_accelerometer_values(GB_gameboy_t *gb, double x, double y)
{
    gb->accelerometer_x = x;
    gb->accelerometer_y = y;
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
        0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
        0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
        0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
        0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
        0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
        0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
        0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
        0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
        0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
        0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
        0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
        0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
        0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
        0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
        0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
        0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
        0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
        0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
        0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
        0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
        0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
        0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
        0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
        0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
        0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
        0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
        0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
        0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
        0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
        0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
        0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
        0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
        0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
        0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
        0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
        0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
        0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
        0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
        0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
        0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
        0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
        0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
        0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
    };
    
    const uint8_t *byte = gb->rom;
    uint32_t size = gb->rom_size;
    uint32_t ret = 0xFFFFFFFF;
    while (size--) {
        ret = table[(ret ^ *byte++) & 0xFF] ^ (ret >> 8);
    }
    return ~ret;
}
