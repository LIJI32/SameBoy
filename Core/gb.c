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


static inline uint32_t state_magic(void)
{
    if (sizeof(bool) == 1) return 'SAME';
    return 'S4ME';
}

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

void GB_init(GB_gameboy_t *gb, GB_model_t model)
{
    model = GB_MODEL_DMG_B;
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
    gb->clock_multiplier = 1.0;
    
    if (model & GB_MODEL_NO_SFC_BIT) {
        /* Disable time syncing. Timing should be done by the SFC emulator. */
        gb->turbo = true;
    }
    
    GB_reset(gb);
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
    if (gb->rom) {
        free(gb->rom);
    }
    if (gb->breakpoints) {
        free(gb->breakpoints);
    }
    if (gb->nontrivial_jump_state) {
        free(gb->nontrivial_jump_state);
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
    return 0;
}

void GB_load_boot_rom_from_buffer(GB_gameboy_t *gb, const unsigned char *buffer, size_t size)
{
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
    if (gb->rom_size == 0) {
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
    return 0;
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
    
#ifdef GB_BIG_ENDIAN
    bool extended = *(uint32_t *)&magic == 'ISX ';
#else
    bool extended = *(uint32_t *)&magic == __builtin_bswap32('ISX ');
#endif
    
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
#ifdef GB_BIG_ENDIAN
                address = __builtin_bswap16(address);
#endif
                address &= 0x3FFF;

                READ(length);
#ifdef GB_BIG_ENDIAN
                length = __builtin_bswap16(length);
#endif

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
#ifdef GB_BIG_ENDIAN
                address = __builtin_bswap32(address);
#endif
                
                READ(length);
#ifdef GB_BIG_ENDIAN
                length = __builtin_bswap32(length);
#endif
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
#ifdef GB_BIG_ENDIAN
                count = __builtin_bswap16(count);
#endif
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
#ifdef GB_BIG_ENDIAN
                    address = __builtin_bswap16(address);
#endif
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
#ifdef GB_BIG_ENDIAN
                count = __builtin_bswap16(count);
#endif
                while (count--) {
                    READ(length);
                    if (fread(name, length + 1, 1, f) != 1) goto error;
                    name[length] = 0;
                    READ(flag); // unused
                    
                    READ(address);
#ifdef GB_BIG_ENDIAN
                    address = __builtin_bswap32(address);
#endif
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
} GB_vba_rtc_time_t;

typedef struct __attribute__((packed)) {
    uint64_t last_rtc_second;
    uint16_t minutes;
    uint16_t days;
    uint16_t alarm_minutes, alarm_days;
    uint8_t alarm_enabled;
} GB_huc3_rtc_time_t;

typedef union {
    struct __attribute__((packed)) {
        GB_rtc_time_t rtc_real;
        time_t last_rtc_second; /* Platform specific endianess and size */
    } sameduck_legacy;
    struct {
        /* Used by VBA versions with 32-bit timestamp*/
        GB_vba_rtc_time_t rtc_real, rtc_latched;
        uint32_t last_rtc_second; /* Always little endian */
    } vba32;
    struct {
        /* Used by BGB and VBA versions with 64-bit timestamp*/
        GB_vba_rtc_time_t rtc_real, rtc_latched;
        uint64_t last_rtc_second; /* Always little endian */
    } vba64;
} GB_rtc_save_t;

int GB_save_battery_size(GB_gameboy_t *gb)
{
    return 0;
}

int GB_save_battery_to_buffer(GB_gameboy_t *gb, uint8_t *buffer, size_t size)
{
    return 0;
}

int GB_save_battery(GB_gameboy_t *gb, const char *path)
{
    return 0;
}

void GB_load_battery_from_buffer(GB_gameboy_t *gb, const uint8_t *buffer, size_t size)
{
    return;
}

/* Loading will silently stop if the format is incomplete */
void GB_load_battery(GB_gameboy_t *gb, const char *path)
{
    return;
}

uint8_t GB_run(GB_gameboy_t *gb)
{
    gb->vblank_just_occured = false;
    
    GB_debugger_run(gb);
    gb->cycles_since_run = 0;
    GB_cpu_run(gb);
    if (gb->vblank_just_occured) {
        GB_debugger_handle_async_commands(gb);
        GB_rewind_push(gb);
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

const GB_palette_t GB_PALETTE_GREY = {{{0x00, 0x00, 0x00}, {0x55, 0x55, 0x55}, {0xaa, 0xaa, 0xaa}, {0xff, 0xff, 0xff}, {0xff, 0xff, 0xff}}};
const GB_palette_t GB_PALETTE_DMG  = {{{0x08, 0x18, 0x10}, {0x39, 0x61, 0x39}, {0x84, 0xa5, 0x63}, {0xc6, 0xde, 0x8c}, {0xd2, 0xe6, 0xa6}}};
const GB_palette_t GB_PALETTE_MGB  = {{{0x07, 0x10, 0x0e}, {0x3a, 0x4c, 0x3a}, {0x81, 0x8d, 0x66}, {0xc2, 0xce, 0x93}, {0xcf, 0xda, 0xac}}};
const GB_palette_t GB_PALETTE_GBL  = {{{0x0a, 0x1c, 0x15}, {0x35, 0x78, 0x62}, {0x56, 0xb4, 0x95}, {0x7f, 0xe2, 0xc3}, {0x91, 0xea, 0xd0}}};

static void update_dmg_palette(GB_gameboy_t *gb)
{
    const GB_palette_t *palette = gb->dmg_palette ?: &GB_PALETTE_GREY;
    if (gb->rgb_encode_callback && !GB_is_cgb(gb)) {
        gb->sprite_palettes_rgb[4] = gb->sprite_palettes_rgb[0] = gb->background_palettes_rgb[0] =
        gb->rgb_encode_callback(gb, palette->colors[3].r, palette->colors[3].g, palette->colors[3].b);
        gb->sprite_palettes_rgb[5] = gb->sprite_palettes_rgb[1] = gb->background_palettes_rgb[1] =
        gb->rgb_encode_callback(gb, palette->colors[2].r, palette->colors[2].g, palette->colors[2].b);
        gb->sprite_palettes_rgb[6] = gb->sprite_palettes_rgb[2] = gb->background_palettes_rgb[2] =
        gb->rgb_encode_callback(gb, palette->colors[1].r, palette->colors[1].g, palette->colors[1].b);
        gb->sprite_palettes_rgb[7] = gb->sprite_palettes_rgb[3] = gb->background_palettes_rgb[3] =
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
    gb->cycles_since_input_ir_change = 0;
    gb->ir_queue_length = 0;
}

void GB_queue_infrared_input(GB_gameboy_t *gb, bool state, uint64_t cycles_after_previous_change)
{
    if (gb->ir_queue_length == GB_MAX_IR_QUEUE) {
        GB_log(gb, "IR Queue is full\n");
        return;
    }
    gb->ir_queue[gb->ir_queue_length++] = (GB_ir_queue_item_t){state, cycles_after_previous_change};
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
        case GB_MODEL_CGB_E:
        case GB_MODEL_AGB: /* Unverified */
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
        
        case GB_MODEL_CGB_C:
            for (unsigned i = 0; i < gb->ram_size; i++) {
                if ((i & 0x808) == 0x800 || (i & 0x808) == 0x008) {
                    gb->ram[i] = 0;
                }
                else {
                    gb->ram[i] = GB_random() | GB_random() | GB_random() | GB_random();
                }
            }
            break;
    }
    
    /* HRAM */
    switch (gb->model) {
        case GB_MODEL_CGB_C:
        // case GB_MODEL_CGB_D:
        case GB_MODEL_CGB_E:
        case GB_MODEL_AGB:
            for (unsigned i = 0; i < sizeof(gb->hram); i++) {
                gb->hram[i] = GB_random();
            }
            break;
            
        case GB_MODEL_DMG_B:
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
        case GB_MODEL_CGB_C:
        case GB_MODEL_CGB_E:
        case GB_MODEL_AGB:
            /* Zero'd out by boot ROM anyway*/
            break;
            
        case GB_MODEL_DMG_B:
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
        case GB_MODEL_CGB_C:
        case GB_MODEL_CGB_E:
        case GB_MODEL_AGB:
            /* Initialized by CGB-A and newer, 0s in CGB-0*/
            break;
            
        case GB_MODEL_DMG_B:
        case GB_MODEL_SGB_NTSC: /* Unverified*/
        case GB_MODEL_SGB_PAL: /* Unverified */
        case GB_MODEL_SGB_NTSC_NO_SFC: /* Unverified */
        case GB_MODEL_SGB_PAL_NO_SFC: /* Unverified */
        case GB_MODEL_SGB2:
        case GB_MODEL_SGB2_NO_SFC: {
            uint8_t temp;
            for (unsigned i = 0; i < GB_IO_WAV_END - GB_IO_WAV_START; i++) {
                if (i & 1) {
                    temp = GB_random() & GB_random() & GB_random();
                }
                else {
                    temp = GB_random() | GB_random() | GB_random();
                }
                gb->apu.wave_channel.wave_form[i * 2]     = temp >> 4;
                gb->apu.wave_channel.wave_form[i * 2 + 1] = temp & 0xF;
                gb->io_registers[GB_IO_WAV_START + i] = temp;

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
            gb->sprite_palettes_data[i] = GB_random();
        }
        for (unsigned i = 0; i < 32; i++) {
            GB_palette_changed(gb, true, i * 2);
            GB_palette_changed(gb, false, i * 2);
        }
    }
}

static void request_boot_rom(GB_gameboy_t *gb)
{

}

void GB_reset(GB_gameboy_t *gb)
{
    GB_model_t model = gb->model;
    memset(gb, 0, (size_t)GB_GET_SECTION((GB_gameboy_t *) 0, unsaved));
    gb->model = model;
    gb->version = GB_STRUCT_VERSION;
    
    gb->mbc_rom_bank = 1;
    gb->last_rtc_second = time(NULL);
    gb->cgb_ram_bank = 1;
    gb->io_registers[GB_IO_JOYP] = 0xCF;
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
    
    /* Todo: Ugly, fixme, see comment in the timer state machine */
    gb->div_state = 3;

    GB_apu_update_cycles_per_sample(gb);
    
    if (gb->nontrivial_jump_state) {
        free(gb->nontrivial_jump_state);
        gb->nontrivial_jump_state = NULL;
    }
    
    gb->magic = state_magic();
    request_boot_rom(gb);
}

void GB_switch_model_and_reset(GB_gameboy_t *gb, GB_model_t model)
{
    model = GB_MODEL_DMG_B;
    gb->model = model;
    if (GB_is_cgb(gb)) {
        gb->ram = realloc(gb->ram, gb->ram_size = 0x1000 * 8);
        gb->vram = realloc(gb->vram, gb->vram_size = 0x2000 * 2);
    }
    else {
        gb->ram = realloc(gb->ram, gb->ram_size = 0x2000);
        gb->vram = realloc(gb->vram, gb->vram_size = 0x2000);
    }
    GB_rewind_free(gb);
    GB_reset(gb);
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
            *size = 0;
            *bank = 0;
            return NULL;
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
            *size = 0;
            *bank = 0;
            return NULL;
        case GB_DIRECT_ACCESS_OAM:
            *size = sizeof(gb->oam);
            *bank = 0;
            return &gb->oam;
        case GB_DIRECT_ACCESS_BGP:
            *size = sizeof(gb->background_palettes_data);
            *bank = 0;
            return &gb->background_palettes_data;
        case GB_DIRECT_ACCESS_OBP:
            *size = sizeof(gb->sprite_palettes_data);
            *bank = 0;
            return &gb->sprite_palettes_data;
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

void GB_set_clock_multiplier(GB_gameboy_t *gb, double multiplier)
{
    gb->clock_multiplier = multiplier;
    GB_apu_update_cycles_per_sample(gb);
}

uint32_t GB_get_clock_rate(GB_gameboy_t *gb)
{
    if (gb->model & GB_MODEL_PAL_BIT) {
        return SGB_PAL_FREQUENCY * gb->clock_multiplier;
    }
    if ((gb->model & ~GB_MODEL_NO_SFC_BIT) == GB_MODEL_SGB) {
        return SGB_NTSC_FREQUENCY * gb->clock_multiplier;
    }
    return CPU_FREQUENCY * gb->clock_multiplier;
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
    return 1;
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
    return 0;
}
