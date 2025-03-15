#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>

#define WIIU_SAMPLE_RATE 48000

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define snprintf _snprintf
#endif

#include <Core/gb.h>
#include "libretro.h"
#include "libretro_core_options.inc"

#ifdef _WIN32
static const char slash = '\\';
#else
static const char slash = '/';
#endif

#define MAX_VIDEO_WIDTH 256
#define MAX_VIDEO_HEIGHT 224
#define MAX_VIDEO_PIXELS (MAX_VIDEO_WIDTH * MAX_VIDEO_HEIGHT)

#define RETRO_MEMORY_GAMEBOY_1_SRAM ((1 << 8) | RETRO_MEMORY_SAVE_RAM)
#define RETRO_MEMORY_GAMEBOY_1_RTC ((2 << 8) | RETRO_MEMORY_RTC)
#define RETRO_MEMORY_GAMEBOY_2_SRAM ((3 << 8) | RETRO_MEMORY_SAVE_RAM)
#define RETRO_MEMORY_GAMEBOY_2_RTC ((3 << 8) | RETRO_MEMORY_RTC)

#define RETRO_GAME_TYPE_GAMEBOY_LINK_2P 0x101

enum rom_type {
    ROM_TYPE_INVALID,
    ROM_TYPE_DMG,
    ROM_TYPE_SGB,
    ROM_TYPE_CGB
};

#define GB_MODEL_AUTO (-1)

enum screen_layout {
    LAYOUT_TOP_DOWN,
    LAYOUT_LEFT_RIGHT
};

enum audio_out {
    GB_1,
    GB_2
};

static GB_model_t model[2] = {
    GB_MODEL_DMG_B,
    GB_MODEL_DMG_B
};
static GB_model_t auto_model[2] = {
    GB_MODEL_CGB_E,
    GB_MODEL_CGB_E
};
static GB_model_t auto_sgb_model[2] = {
    GB_MODEL_SGB_NTSC,
    GB_MODEL_SGB_NTSC
};
static bool auto_sgb_enabled[2] = {
    false,
    false
};

static uint32_t *frame_buf = NULL;
static uint32_t *frame_buf_copy = NULL;
static uint32_t retained_frame_1[256 * 224];
static uint32_t retained_frame_2[256 * 224];
static struct retro_log_callback logging;
static retro_log_printf_t log_cb;

static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static bool libretro_supports_bitmasks = false;

static unsigned emulated_devices = 1;
static bool initialized = false;
static unsigned screen_layout = 0;
static unsigned audio_out = 0;

static bool geometry_updated = false;
static bool link_cable_emulation = false;
/*static bool infrared_emulation   = false;*/

static struct {
    int16_t *data;
    int32_t size;
    int32_t capacity;
} output_audio_buffer = {NULL, 0, 0};

char retro_system_directory[4096];

GB_gameboy_t gameboy[2];

extern const unsigned char dmg_boot[], mgb_boot[], cgb0_boot[], cgb_boot[], agb_boot[], sgb_boot[], sgb2_boot[];
extern const unsigned dmg_boot_length, mgb_boot_length, cgb0_boot_length, cgb_boot_length, agb_boot_length, sgb_boot_length, sgb2_boot_length;
bool vblank1_occurred = false, vblank2_occurred = false;

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
    (void)level;
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
}

static struct retro_rumble_interface rumble;

static void GB_update_keys_status(GB_gameboy_t *gb, unsigned port)
{
    uint16_t joypad_bits = 0;

    input_poll_cb();

    if (libretro_supports_bitmasks) {
        joypad_bits = input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
    }
    else {
        unsigned j;

        for (j = 0; j < (RETRO_DEVICE_ID_JOYPAD_R3 + 1); j++) {
            if (input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, j)) {
                joypad_bits |= (1 << j);
            }
        }
    }

    GB_set_key_state_for_player(gb, GB_KEY_RIGHT,  emulated_devices == 1 ? port : 0,
        joypad_bits & (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT));
    GB_set_key_state_for_player(gb, GB_KEY_LEFT,   emulated_devices == 1 ? port : 0,
        joypad_bits & (1 << RETRO_DEVICE_ID_JOYPAD_LEFT));
    GB_set_key_state_for_player(gb, GB_KEY_UP,     emulated_devices == 1 ? port : 0,
        joypad_bits & (1 << RETRO_DEVICE_ID_JOYPAD_UP));
    GB_set_key_state_for_player(gb, GB_KEY_DOWN,   emulated_devices == 1 ? port : 0,
        joypad_bits & (1 << RETRO_DEVICE_ID_JOYPAD_DOWN));
    GB_set_key_state_for_player(gb, GB_KEY_A,      emulated_devices == 1 ? port : 0,
        joypad_bits & (1 << RETRO_DEVICE_ID_JOYPAD_A));
    GB_set_key_state_for_player(gb, GB_KEY_B,      emulated_devices == 1 ? port : 0,
        joypad_bits & (1 << RETRO_DEVICE_ID_JOYPAD_B));
    GB_set_key_state_for_player(gb, GB_KEY_SELECT, emulated_devices == 1 ? port : 0,
        joypad_bits & (1 << RETRO_DEVICE_ID_JOYPAD_SELECT));
    GB_set_key_state_for_player(gb, GB_KEY_START,  emulated_devices == 1 ? port : 0,
        joypad_bits & (1 << RETRO_DEVICE_ID_JOYPAD_START));

}

static void rumble_callback(GB_gameboy_t *gb, double amplitude)
{
    if (!rumble.set_rumble_state) return;

    if (gb == &gameboy[0]) {
        rumble.set_rumble_state(0, RETRO_RUMBLE_STRONG, 65535 * amplitude);
    }
    else if (gb == &gameboy[1]) {
        rumble.set_rumble_state(1, RETRO_RUMBLE_STRONG, 65535 * amplitude);
    }
}

static void ensure_output_audio_buffer_capacity(int32_t capacity)
{
    if (capacity <= output_audio_buffer.capacity) {
        return;
    }
    output_audio_buffer.data = realloc(
        output_audio_buffer.data, capacity * sizeof(*output_audio_buffer.data));
    output_audio_buffer.capacity = capacity;
    log_cb(RETRO_LOG_DEBUG, "Output audio buffer capacity set to %d\n", capacity);
}

static void init_output_audio_buffer(int32_t capacity)
{
    output_audio_buffer.data = NULL;
    output_audio_buffer.size = 0;
    output_audio_buffer.capacity = 0;
    ensure_output_audio_buffer_capacity(capacity);
}

static void free_output_audio_buffer()
{
    free(output_audio_buffer.data);
    output_audio_buffer.data = NULL;
    output_audio_buffer.size = 0;
    output_audio_buffer.capacity = 0;
}

static void upload_output_audio_buffer()
{
    int32_t remaining_frames = output_audio_buffer.size / 2;
    int16_t *buf_pos = output_audio_buffer.data;

    while (remaining_frames > 0) {
        size_t uploaded_frames = audio_batch_cb(buf_pos, remaining_frames);
        buf_pos += uploaded_frames * 2;
        remaining_frames -= uploaded_frames;
    }
    output_audio_buffer.size = 0;
}

static void audio_callback(GB_gameboy_t *gb, GB_sample_t *sample)
{
    if (!(audio_out == GB_1 && gb == &gameboy[0]) &&
        !(audio_out == GB_2 && gb == &gameboy[1])) {
        return;
    }

    if (output_audio_buffer.capacity - output_audio_buffer.size < 2) {
        ensure_output_audio_buffer_capacity(output_audio_buffer.capacity * 1.5);
    }

    output_audio_buffer.data[output_audio_buffer.size++] = sample->left;
    output_audio_buffer.data[output_audio_buffer.size++] = sample->right;
}

static void vblank1(GB_gameboy_t *gb, GB_vblank_type_t type)
{
    if (type == GB_VBLANK_TYPE_REPEAT) {
        memcpy(GB_get_pixels_output(gb),
               retained_frame_1,
               GB_get_screen_width(gb) * GB_get_screen_height(gb) * sizeof(uint32_t));
    }
    vblank1_occurred = true;
}

static void vblank2(GB_gameboy_t *gb, GB_vblank_type_t type)
{
    if (type == GB_VBLANK_TYPE_REPEAT) {
        memcpy(GB_get_pixels_output(gb),
               retained_frame_2,
               GB_get_screen_width(gb) * GB_get_screen_height(gb) * sizeof(uint32_t));
    }
    vblank2_occurred = true;
}

static void lcd_status_change_1(GB_gameboy_t *gb, bool on)
{
    if (!on) {
        memcpy(retained_frame_1,
               GB_get_pixels_output(gb),
               GB_get_screen_width(gb) * GB_get_screen_height(gb) * sizeof(uint32_t));
    }
}

static void lcd_status_change_2(GB_gameboy_t *gb, bool on)
{
    if (!on) {
        memcpy(retained_frame_2,
               GB_get_pixels_output(gb),
               GB_get_screen_width(gb) * GB_get_screen_height(gb) * sizeof(uint32_t));
    }
}

static bool bit_to_send1 = true, bit_to_send2 = true;

static void serial_start1(GB_gameboy_t *gb, bool bit_received)
{
    bit_to_send1 = bit_received;
}

static bool serial_end1(GB_gameboy_t *gb)
{
    bool ret = GB_serial_get_data_bit(&gameboy[1]);
    GB_serial_set_data_bit(&gameboy[1], bit_to_send1);
    return ret;
}

static void serial_start2(GB_gameboy_t *gb, bool bit_received)
{
    bit_to_send2 = bit_received;
}

static bool serial_end2(GB_gameboy_t *gb)
{
    bool ret = GB_serial_get_data_bit(&gameboy[0]);
    GB_serial_set_data_bit(&gameboy[0], bit_to_send2);
    return ret;
}

static void infrared_callback1(GB_gameboy_t *gb, bool output)
{
    GB_set_infrared_input(&gameboy[1], output);
}

static void infrared_callback2(GB_gameboy_t *gb, bool output)
{
    GB_set_infrared_input(&gameboy[0], output);
}

static uint32_t rgb_encode(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{
    return r <<16 | g <<8 | b;
}

static retro_environment_t environ_cb;

static void set_variable_visibility(void)
{
    struct retro_core_option_display option_display_singlecart;
    struct retro_core_option_display option_display_dualcart;

    size_t i;
    size_t num_options = 0;

    // Show/hide options depending on the number of emulated devices
    if (emulated_devices == 1) { 
        option_display_singlecart.visible = true;
        option_display_dualcart.visible = false;
    }
    else if (emulated_devices == 2) { 
        option_display_singlecart.visible = false;
        option_display_dualcart.visible = true;
    }

    // Determine number of options
    while (true) {
        if (!option_defs_us[num_options].key) break;
        num_options++;
    }

    // Copy parameters from option_defs_us array
    for (i = 0; i < num_options; i++) { 
        const char *key  = option_defs_us[i].key;
        if ((strcmp(key, "sameboy_model")                   == 0) ||
            (strcmp(key, "sameboy_auto_sgb_model")          == 0) ||
            (strcmp(key, "sameboy_rtc")                     == 0) ||
            (strcmp(key, "sameboy_scaling_filter")          == 0) ||
            (strcmp(key, "sameboy_mono_palette")            == 0) ||
            (strcmp(key, "sameboy_color_correction_mode")   == 0) ||
            (strcmp(key, "sameboy_light_temperature")       == 0) ||
            (strcmp(key, "sameboy_border")                  == 0) ||
            (strcmp(key, "sameboy_high_pass_filter_mode")   == 0) ||
            (strcmp(key, "sameboy_audio_interference")      == 0) ||
            (strcmp(key, "sameboy_rumble")                  == 0)) {
            option_display_singlecart.key = key;
            environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display_singlecart);
        }
        else if ((strcmp(key, "sameboy_link")                    == 0) ||
                 (strcmp(key, "sameboy_screen_layout")           == 0) ||
                 (strcmp(key, "sameboy_audio_output")            == 0) ||
                 (strcmp(key, "sameboy_model_1")                 == 0) ||
                 (strcmp(key, "sameboy_auto_sgb_model_1")        == 0) ||
                 (strcmp(key, "sameboy_model_2")                 == 0) ||
                 (strcmp(key, "sameboy_auto_sgb_model_2")        == 0) ||
                 (strcmp(key, "sameboy_mono_palette_1")          == 0) ||
                 (strcmp(key, "sameboy_mono_palette_2")          == 0) ||
                 (strcmp(key, "sameboy_color_correction_mode_1") == 0) ||
                 (strcmp(key, "sameboy_color_correction_mode_2") == 0) ||
                 (strcmp(key, "sameboy_light_temperature_1")     == 0) ||
                 (strcmp(key, "sameboy_light_temperature_2")     == 0) ||
                 (strcmp(key, "sameboy_high_pass_filter_mode_1") == 0) ||
                 (strcmp(key, "sameboy_high_pass_filter_mode_2") == 0) ||
                 (strcmp(key, "sameboy_audio_interference_1")    == 0) ||
                 (strcmp(key, "sameboy_audio_interference_2")    == 0) ||
                 (strcmp(key, "sameboy_rumble_1")                == 0) ||
                 (strcmp(key, "sameboy_rumble_2")                == 0)) {
            option_display_dualcart.key = key;
            environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display_dualcart);
        }
    }
}

static const struct retro_subsystem_memory_info gb1_memory[] = {
    { "srm", RETRO_MEMORY_GAMEBOY_1_SRAM },
    { "rtc", RETRO_MEMORY_GAMEBOY_1_RTC },
};

static const struct retro_subsystem_memory_info gb2_memory[] = {
    { "srm", RETRO_MEMORY_GAMEBOY_2_SRAM },
    { "rtc", RETRO_MEMORY_GAMEBOY_2_RTC },
};

static const struct retro_subsystem_rom_info gb_roms[] = {
    { "GameBoy #1", "gb|gbc", false, false, true, gb1_memory, 1 },
    { "GameBoy #2", "gb|gbc", false, false, true, gb2_memory, 1 },
};

static const struct retro_subsystem_info subsystems[] = {
    { "2 Player Game Boy Link", "gb_link_2p", gb_roms, 2, RETRO_GAME_TYPE_GAMEBOY_LINK_2P },
    { NULL },
};

static const struct retro_controller_description controllers[] = {
    { "Nintendo Game Boy", RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0) },
};

static const struct retro_controller_description controllers_sgb[] = {
    { "SNES/SFC Gamepad", RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0) },
};

static struct retro_input_descriptor descriptors_1p[] = {
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Left" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Up" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Down" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
    { 0 },
};

static struct retro_input_descriptor descriptors_2p[] = {
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Left" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Up" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Down" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
    { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Left" },
    { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Up" },
    { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Down" },
    { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
    { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B" },
    { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A" },
    { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
    { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
    { 0 },
};

static struct retro_input_descriptor descriptors_4p[] = {
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Left" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Up" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Down" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
    { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
    { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Left" },
    { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Up" },
    { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Down" },
    { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
    { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B" },
    { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A" },
    { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
    { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
    { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Left" },
    { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Up" },
    { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Down" },
    { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
    { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B" },
    { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A" },
    { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
    { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
    { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Left" },
    { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Up" },
    { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Down" },
    { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
    { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B" },
    { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A" },
    { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
    { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
    { 0 },
};


static void set_link_cable_state(bool state)
{
    if (state && emulated_devices == 2) {
        GB_set_serial_transfer_bit_start_callback(&gameboy[0], serial_start1);
        GB_set_serial_transfer_bit_end_callback(&gameboy[0], serial_end1);
        GB_set_serial_transfer_bit_start_callback(&gameboy[1], serial_start2);
        GB_set_serial_transfer_bit_end_callback(&gameboy[1], serial_end2);
        GB_set_infrared_callback(&gameboy[0], infrared_callback1);
        GB_set_infrared_callback(&gameboy[1], infrared_callback2);
    }
    else if (!state) {
        GB_set_serial_transfer_bit_start_callback(&gameboy[0], NULL);
        GB_set_serial_transfer_bit_end_callback(&gameboy[0], NULL);
        GB_set_serial_transfer_bit_start_callback(&gameboy[1], NULL);
        GB_set_serial_transfer_bit_end_callback(&gameboy[1], NULL);
        GB_set_infrared_callback(&gameboy[0], NULL);
        GB_set_infrared_callback(&gameboy[1], NULL);
    }
}

static void boot_rom_load(GB_gameboy_t *gb, GB_boot_rom_t type)
{
    const char *model_name = (char *[]) {
        [GB_BOOT_ROM_DMG_0] = "dmg0",
        [GB_BOOT_ROM_DMG] = "dmg",
        [GB_BOOT_ROM_MGB] = "mgb",
        [GB_BOOT_ROM_SGB] = "sgb",
        [GB_BOOT_ROM_SGB2] = "sgb2",
        [GB_BOOT_ROM_CGB_0] = "cgb0",
        [GB_BOOT_ROM_CGB] = "cgb",
        [GB_BOOT_ROM_CGB_E] = "cgbE",
        [GB_BOOT_ROM_AGB_0] = "agb0",
        [GB_BOOT_ROM_AGB] = "agb",
    }[type];

    const uint8_t *boot_code = (const unsigned char *[]) {
        [GB_BOOT_ROM_DMG_0] = dmg_boot, // DMG_0 not implemented yet
        [GB_BOOT_ROM_DMG] = dmg_boot,
        [GB_BOOT_ROM_MGB] = mgb_boot,
        [GB_BOOT_ROM_SGB] = sgb_boot,
        [GB_BOOT_ROM_SGB2] = sgb2_boot,
        [GB_BOOT_ROM_CGB_0] = cgb0_boot,
        [GB_BOOT_ROM_CGB] = cgb_boot,
        [GB_BOOT_ROM_AGB] = agb_boot,
    }[type];

    unsigned boot_length = (unsigned []) {
        [GB_BOOT_ROM_DMG_0] = dmg_boot_length, // DMG_0 not implemented yet
        [GB_BOOT_ROM_DMG] = dmg_boot_length,
        [GB_BOOT_ROM_MGB] = mgb_boot_length,
        [GB_BOOT_ROM_SGB] = sgb_boot_length,
        [GB_BOOT_ROM_SGB2] = sgb2_boot_length,
        [GB_BOOT_ROM_CGB_0] = cgb0_boot_length,
        [GB_BOOT_ROM_CGB] = cgb_boot_length,
        [GB_BOOT_ROM_AGB] = agb_boot_length,
    }[type];

    char buf[4096 + 1 + 4 + 9 + 1];
    snprintf(buf, sizeof(buf), "%s%c%s_boot.bin", retro_system_directory, slash, model_name);
    log_cb(RETRO_LOG_INFO, "Initializing as model: %s\n", model_name);
    log_cb(RETRO_LOG_INFO, "Loading boot image: %s\n", buf);

    if (GB_load_boot_rom(gb, buf)) {
        if (type == GB_BOOT_ROM_CGB_E) {
            boot_rom_load(gb, GB_BOOT_ROM_CGB);
            return;
        }
        if (type == GB_BOOT_ROM_AGB_0) {
            boot_rom_load(gb, GB_BOOT_ROM_AGB);
            return;
        }
        GB_load_boot_rom_from_buffer(gb, boot_code, boot_length);
    }
}

static void retro_set_memory_maps(void)
{
    struct retro_memory_descriptor descs[11];
    size_t size;
    uint16_t bank;
    unsigned i;


    /* todo: add netplay awareness for this so achievements can be granted on the respective client */
    i = 0;
    memset(descs, 0, sizeof(descs));

    descs[0].ptr     = GB_get_direct_access(&gameboy[i], GB_DIRECT_ACCESS_IE, &size, &bank);
    descs[0].start   = 0xFFFF;
    descs[0].len     = 1;

    descs[1].ptr     = GB_get_direct_access(&gameboy[i], GB_DIRECT_ACCESS_HRAM, &size, &bank);
    descs[1].start   = 0xFF80;
    descs[1].len     = 0x0080;

    descs[2].ptr     = GB_get_direct_access(&gameboy[i], GB_DIRECT_ACCESS_RAM, &size, &bank);
    descs[2].start   = 0xC000;
    descs[2].len     = 0x1000;

    descs[3].ptr     = descs[2].ptr + 0x1000; /* GB RAM/GBC RAM bank 1 */
    descs[3].start   = 0xD000;
    descs[3].len     = 0x1000;

    descs[4].ptr     = GB_get_direct_access(&gameboy[i], GB_DIRECT_ACCESS_CART_RAM, &size, &bank);
    descs[4].start   = 0xA000;
    descs[4].len     = 0x2000;

    descs[5].ptr     = GB_get_direct_access(&gameboy[i], GB_DIRECT_ACCESS_VRAM, &size, &bank);
    descs[5].start   = 0x8000;
    descs[5].len     = 0x2000;

    descs[6].ptr     = GB_get_direct_access(&gameboy[i], GB_DIRECT_ACCESS_ROM, &size, &bank);
    descs[6].start   = 0x0000;
    descs[6].len     = 0x4000;
    descs[6].flags   = RETRO_MEMDESC_CONST;

    descs[7].ptr     = descs[6].ptr + (bank * 0x4000);
    descs[7].start   = 0x4000;
    descs[7].len     = 0x4000;
    descs[7].flags   = RETRO_MEMDESC_CONST;

    descs[8].ptr   = GB_get_direct_access(&gameboy[i], GB_DIRECT_ACCESS_OAM, &size, &bank);
    descs[8].start = 0xFE00;
    descs[8].len   = 0x00A0;
    descs[8].select = 0xFFFFFF00;

    descs[9].ptr   = descs[2].ptr + 0x2000; /* GBC RAM bank 2 */
    descs[9].start = 0x10000;
    descs[9].len   = GB_is_cgb(&gameboy[i]) ? 0x6000 : 0; /* 0x1000 per bank (2-7), unmapped on GB */
    descs[9].select = 0xFFFF0000;

    descs[10].ptr   = GB_get_direct_access(&gameboy[i], GB_DIRECT_ACCESS_IO, &size, &bank);
    descs[10].start = 0xFF00;
    descs[10].len   = 0x0080;
    descs[10].select = 0xFFFFFF00;

    struct retro_memory_map mmaps;
    mmaps.descriptors = descs;
    mmaps.num_descriptors = sizeof(descs) / sizeof(descs[0]);
    environ_cb(RETRO_ENVIRONMENT_SET_MEMORY_MAPS, &mmaps);
}

static void init_for_current_model(unsigned id)
{
    unsigned i = id;
    GB_model_t effective_model;

    effective_model = model[i];
    if (effective_model == GB_MODEL_AUTO) {
        effective_model = auto_model[i];
    }

    if (GB_is_inited(&gameboy[i])) {
        GB_switch_model_and_reset(&gameboy[i], effective_model);
        retro_set_memory_maps();
    }
    else {
        GB_init(&gameboy[i], effective_model);
    }
    geometry_updated = true;

    GB_set_boot_rom_load_callback(&gameboy[i], boot_rom_load);

    /* When running multiple devices they are assumed to use the same resolution */

    GB_set_pixels_output(&gameboy[i],
                         (uint32_t *)(frame_buf + GB_get_screen_width(&gameboy[0]) * GB_get_screen_height(&gameboy[0]) * i));
    GB_set_rgb_encode_callback(&gameboy[i], rgb_encode);
#ifdef WIIU
    GB_set_sample_rate(&gameboy[i], WIIU_SAMPLE_RATE);
#else
    GB_set_sample_rate(&gameboy[i], GB_get_clock_rate(&gameboy[i]) / 2);
#endif
    GB_apu_set_sample_callback(&gameboy[i], audio_callback);
    GB_set_rumble_callback(&gameboy[i], rumble_callback);

    /* todo: attempt to make these more generic */
    GB_set_vblank_callback(&gameboy[0], (GB_vblank_callback_t) vblank1);
    GB_set_lcd_status_callback(&gameboy[0], lcd_status_change_1);
    if (emulated_devices == 2) {
        GB_set_vblank_callback(&gameboy[1], (GB_vblank_callback_t) vblank2);
        GB_set_lcd_status_callback(&gameboy[1], lcd_status_change_2);
        if (link_cable_emulation) {
            set_link_cable_state(true);
        }
    }

    /* Let's be extremely nitpicky about how devices and descriptors are set */
    if (emulated_devices == 1 && (model[0] == GB_MODEL_SGB_PAL || model[0] == GB_MODEL_SGB_NTSC || model[0] == GB_MODEL_SGB2)) {
        static const struct retro_controller_info ports[] = {
            { controllers_sgb, 1 },
            { controllers_sgb, 1 },
            { controllers_sgb, 1 },
            { controllers_sgb, 1 },
            { NULL, 0 },
        };
        environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);
        environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, descriptors_4p);
    }
    else if (emulated_devices == 1) {
        static const struct retro_controller_info ports[] = {
            { controllers, 1 },
            { NULL, 0 },
        };
        environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);
        environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, descriptors_1p);
    }
    else {
        static const struct retro_controller_info ports[] = {
            { controllers, 1 },
            { controllers, 1 },
            { NULL, 0 },
        };
        environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);
        environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, descriptors_2p);
    }
}

static GB_model_t string_to_model(const char *string)
{
    static const struct {
        const char *name;
        GB_model_t model;
    } models[] = {
        { "Game Boy",             GB_MODEL_DMG_B},
        { "Game Boy Pocket",      GB_MODEL_MGB},
        { "Game Boy Color 0",     GB_MODEL_CGB_0},
        { "Game Boy Color A",     GB_MODEL_CGB_A},
        { "Game Boy Color B",     GB_MODEL_CGB_B},
        { "Game Boy Color C",     GB_MODEL_CGB_C},
        { "Game Boy Color D",     GB_MODEL_CGB_D},
        { "Game Boy Color",       GB_MODEL_CGB_E},
        { "Game Boy Advance",     GB_MODEL_AGB_A},
        { "Game Boy Player",      GB_MODEL_GBP_A},
        { "Super Game Boy",       GB_MODEL_SGB_NTSC},
        { "Super Game Boy PAL",   GB_MODEL_SGB_PAL},
        { "Super Game Boy 2",     GB_MODEL_SGB2},
    };
    for (unsigned i = 0; i < sizeof(models) / sizeof(models[0]); i++) {
        if (strcmp(models[i].name, string) == 0) {
            return models[i].model;
        }
    }
    return GB_MODEL_AUTO;
}

static void check_variables()
{
    struct retro_variable var = {0};
    if (emulated_devices == 1) {

        var.key = "sameboy_model";
        var.value = NULL;

        model[0] = GB_MODEL_AUTO;
        auto_sgb_enabled[0] = false;

        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            GB_model_t new_model = model[0];
            new_model = string_to_model(var.value);
            if (new_model == GB_MODEL_AUTO) {
                if (strcmp(var.value, "Auto (SGB)") == 0) {
                    new_model = GB_MODEL_AUTO;
                    auto_sgb_enabled[0] = true;
                }
            }

            model[0] = new_model;
        }

        var.key = "sameboy_auto_sgb_model";
        var.value = NULL;

        auto_sgb_model[0] = GB_MODEL_SGB_NTSC;

        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            GB_model_t new_model = auto_sgb_model[0];
            if (strcmp(var.value, "Super Game Boy PAL") == 0) {
                new_model = GB_MODEL_SGB_PAL;
            }
            else if (strcmp(var.value, "Super Game Boy 2") == 0) {
                new_model = GB_MODEL_SGB2;
            }
            else {
                new_model = GB_MODEL_SGB_NTSC;
            }

            auto_sgb_model[0] = new_model;
        }

        var.key = "sameboy_rtc";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            if (strcmp(var.value, "sync to system clock") == 0) {
                GB_set_rtc_mode(&gameboy[0], GB_RTC_MODE_SYNC_TO_HOST);
            }
            else if (strcmp(var.value, "accurate") == 0) {
                GB_set_rtc_mode(&gameboy[0], GB_RTC_MODE_ACCURATE);
            }
        }

        var.key = "sameboy_mono_palette";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            if (strcmp(var.value, "greyscale") == 0) {
                GB_set_palette(&gameboy[0], &GB_PALETTE_GREY);
            }
            else if (strcmp(var.value, "lime") == 0) {
                GB_set_palette(&gameboy[0], &GB_PALETTE_DMG);
            }
            else if (strcmp(var.value, "olive") == 0) {
                GB_set_palette(&gameboy[0], &GB_PALETTE_MGB);
            }
            else if (strcmp(var.value, "teal") == 0) {
                GB_set_palette(&gameboy[0], &GB_PALETTE_GBL);
            }
        }

        var.key = "sameboy_color_correction_mode";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            if (strcmp(var.value, "off") == 0) {
                GB_set_color_correction_mode(&gameboy[0], GB_COLOR_CORRECTION_DISABLED);
            }
            else if (strcmp(var.value, "correct curves") == 0) {
                GB_set_color_correction_mode(&gameboy[0], GB_COLOR_CORRECTION_CORRECT_CURVES);
            }
            else if (strcmp(var.value, "emulate hardware") == 0) {
                GB_set_color_correction_mode(&gameboy[0], GB_COLOR_CORRECTION_MODERN_BALANCED);
            }
            else if (strcmp(var.value, "preserve brightness") == 0) {
                GB_set_color_correction_mode(&gameboy[0], GB_COLOR_CORRECTION_MODERN_BOOST_CONTRAST);
            }
            else if (strcmp(var.value, "reduce contrast") == 0) {
                GB_set_color_correction_mode(&gameboy[0], GB_COLOR_CORRECTION_REDUCE_CONTRAST);
            }
            else if (strcmp(var.value, "harsh reality") == 0) {
                GB_set_color_correction_mode(&gameboy[0], GB_COLOR_CORRECTION_LOW_CONTRAST);
            }
            else if (strcmp(var.value, "accurate") == 0) {
                GB_set_color_correction_mode(&gameboy[0], GB_COLOR_CORRECTION_MODERN_ACCURATE);
            }
        }

        var.key = "sameboy_light_temperature";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            GB_set_light_temperature(&gameboy[0], atof(var.value));
        }

        var.key = "sameboy_border";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            if (strcmp(var.value, "never") == 0) {
                GB_set_border_mode(&gameboy[0], GB_BORDER_NEVER);
            }
            else if (strcmp(var.value, "Super Game Boy only") == 0) {
                GB_set_border_mode(&gameboy[0], GB_BORDER_SGB);
            }
            else if (strcmp(var.value, "always") == 0) {
                GB_set_border_mode(&gameboy[0], GB_BORDER_ALWAYS);
            }
            geometry_updated = true;
        }

        var.key = "sameboy_high_pass_filter_mode";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            if (strcmp(var.value, "off") == 0) {
                GB_set_highpass_filter_mode(&gameboy[0], GB_HIGHPASS_OFF);
            }
            else if (strcmp(var.value, "accurate") == 0) {
                GB_set_highpass_filter_mode(&gameboy[0], GB_HIGHPASS_ACCURATE);
            }
            else if (strcmp(var.value, "remove dc offset") == 0) {
                GB_set_highpass_filter_mode(&gameboy[0], GB_HIGHPASS_REMOVE_DC_OFFSET);
            }
        }

        var.key = "sameboy_audio_interference";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            GB_set_interference_volume(&gameboy[0], atoi(var.value) / 100.0);
        }

        var.key = "sameboy_rumble";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            if (strcmp(var.value, "never") == 0) {
                GB_set_rumble_mode(&gameboy[0], GB_RUMBLE_DISABLED);
            }
            else if (strcmp(var.value, "rumble-enabled games") == 0) {
                GB_set_rumble_mode(&gameboy[0], GB_RUMBLE_CARTRIDGE_ONLY);
            }
            else if (strcmp(var.value, "all games") == 0) {
                GB_set_rumble_mode(&gameboy[0], GB_RUMBLE_ALL_GAMES);
            }
        }

    }
    else {
        GB_set_border_mode(&gameboy[0], GB_BORDER_NEVER);
        GB_set_border_mode(&gameboy[1], GB_BORDER_NEVER);
        GB_set_rtc_mode(&gameboy[0], GB_RTC_MODE_ACCURATE);
        GB_set_rtc_mode(&gameboy[1], GB_RTC_MODE_ACCURATE);

        var.key = "sameboy_link";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            bool tmp = link_cable_emulation;
            if (strcmp(var.value, "enabled") == 0) {
                link_cable_emulation = true;
            }
            else {
                link_cable_emulation = false;
            }
            if (link_cable_emulation && link_cable_emulation != tmp) {
                set_link_cable_state(true);
            }
            else if (!link_cable_emulation && link_cable_emulation != tmp) {
                set_link_cable_state(false);
            }
        }

        var.key = "sameboy_screen_layout";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            if (strcmp(var.value, "top-down") == 0) {
                screen_layout = LAYOUT_TOP_DOWN;
            }
            else {
                screen_layout = LAYOUT_LEFT_RIGHT;
            }

            geometry_updated = true;
        }

        var.key = "sameboy_audio_output";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            if (strcmp(var.value, "Game Boy #1") == 0) {
                audio_out = GB_1;
            }
            else {
                audio_out = GB_2;
            }
        }

        var.key = "sameboy_model_1";
        var.value = NULL;

        model[0] = GB_MODEL_AUTO;
        auto_sgb_enabled[0] = false;

        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            GB_model_t new_model = model[0];
            new_model = string_to_model(var.value);
            if (new_model == GB_MODEL_AUTO) {
                if (strcmp(var.value, "Auto (SGB)") == 0) {
                    new_model = GB_MODEL_AUTO;
                    auto_sgb_enabled[0] = true;
                }
            }
            
            model[0] = new_model;
        }


        var.key = "sameboy_auto_sgb_model_1";
        var.value = NULL;

        auto_sgb_model[0] = GB_MODEL_SGB_NTSC;

        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            GB_model_t new_model = auto_sgb_model[0];
            if (strcmp(var.value, "Super Game Boy PAL") == 0) {
                new_model = GB_MODEL_SGB_PAL;
            }
            else if (strcmp(var.value, "Super Game Boy 2") == 0) {
                new_model = GB_MODEL_SGB2;
            }
            else {
                new_model = GB_MODEL_SGB_NTSC;
            }

            auto_sgb_model[0] = new_model;
        }

        var.key = "sameboy_model_2";
        var.value = NULL;

        model[1] = GB_MODEL_AUTO;
        auto_sgb_enabled[1] = false;

        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            GB_model_t new_model = model[1];
            new_model = string_to_model(var.value);
            if (new_model == GB_MODEL_AUTO) {
                if (strcmp(var.value, "Auto (SGB)") == 0) {
                    new_model = GB_MODEL_AUTO;
                    auto_sgb_enabled[0] = true;
                }
            }
            
            model[1] = new_model;
        }

        var.key = "sameboy_auto_sgb_model_2";
        var.value = NULL;

        auto_sgb_model[1] = GB_MODEL_SGB_NTSC;

        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            GB_model_t new_model = auto_sgb_model[1];
            if (strcmp(var.value, "Super Game Boy PAL") == 0) {
                new_model = GB_MODEL_SGB_PAL;
            }
            else if (strcmp(var.value, "Super Game Boy 2") == 0) {
                new_model = GB_MODEL_SGB2;
            }
            else {
                new_model = GB_MODEL_SGB_NTSC;
            }

            auto_sgb_model[1] = new_model;
        }

        var.key = "sameboy_mono_palette_1";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            if (strcmp(var.value, "greyscale") == 0) {
                GB_set_palette(&gameboy[0], &GB_PALETTE_GREY);
            }
            else if (strcmp(var.value, "lime") == 0) {
                GB_set_palette(&gameboy[0], &GB_PALETTE_DMG);
            }
            else if (strcmp(var.value, "olive") == 0) {
                GB_set_palette(&gameboy[0], &GB_PALETTE_MGB);
            }
            else if (strcmp(var.value, "teal") == 0) {
                GB_set_palette(&gameboy[0], &GB_PALETTE_GBL);
            }
        }

        var.key = "sameboy_mono_palette_2";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            if (strcmp(var.value, "greyscale") == 0) {
                GB_set_palette(&gameboy[1], &GB_PALETTE_GREY);
            }
            else if (strcmp(var.value, "lime") == 0) {
                GB_set_palette(&gameboy[1], &GB_PALETTE_DMG);
            }
            else if (strcmp(var.value, "olive") == 0) {
                GB_set_palette(&gameboy[1], &GB_PALETTE_MGB);
            }
            else if (strcmp(var.value, "teal") == 0) {
                GB_set_palette(&gameboy[1], &GB_PALETTE_GBL);
            }
        }

        var.key = "sameboy_color_correction_mode_1";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            if (strcmp(var.value, "off") == 0) {
                GB_set_color_correction_mode(&gameboy[0], GB_COLOR_CORRECTION_DISABLED);
            }
            else if (strcmp(var.value, "correct curves") == 0) {
                GB_set_color_correction_mode(&gameboy[0], GB_COLOR_CORRECTION_CORRECT_CURVES);
            }
            else if (strcmp(var.value, "emulate hardware") == 0) {
                GB_set_color_correction_mode(&gameboy[0], GB_COLOR_CORRECTION_MODERN_BALANCED);
            }
            else if (strcmp(var.value, "preserve brightness") == 0) {
                GB_set_color_correction_mode(&gameboy[0], GB_COLOR_CORRECTION_MODERN_BOOST_CONTRAST);
            }
            else if (strcmp(var.value, "reduce contrast") == 0) {
                GB_set_color_correction_mode(&gameboy[0], GB_COLOR_CORRECTION_REDUCE_CONTRAST);
            }
            else if (strcmp(var.value, "harsh reality") == 0) {
                GB_set_color_correction_mode(&gameboy[0], GB_COLOR_CORRECTION_LOW_CONTRAST);
            }
            else if (strcmp(var.value, "accurate") == 0) {
                GB_set_color_correction_mode(&gameboy[0], GB_COLOR_CORRECTION_MODERN_ACCURATE);
            }
        }

        var.key = "sameboy_color_correction_mode_2";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            if (strcmp(var.value, "off") == 0) {
                GB_set_color_correction_mode(&gameboy[1], GB_COLOR_CORRECTION_DISABLED);
            }
            else if (strcmp(var.value, "correct curves") == 0) {
                GB_set_color_correction_mode(&gameboy[1], GB_COLOR_CORRECTION_CORRECT_CURVES);
            }
            else if (strcmp(var.value, "emulate hardware") == 0) {
                GB_set_color_correction_mode(&gameboy[1], GB_COLOR_CORRECTION_MODERN_BALANCED);
            }
            else if (strcmp(var.value, "preserve brightness") == 0) {
                GB_set_color_correction_mode(&gameboy[1], GB_COLOR_CORRECTION_MODERN_BOOST_CONTRAST);
            }
            else if (strcmp(var.value, "reduce contrast") == 0) {
                GB_set_color_correction_mode(&gameboy[1], GB_COLOR_CORRECTION_REDUCE_CONTRAST);
            }
            else if (strcmp(var.value, "harsh reality") == 0) {
                GB_set_color_correction_mode(&gameboy[1], GB_COLOR_CORRECTION_LOW_CONTRAST);
            }
            else if (strcmp(var.value, "accurate") == 0) {
                GB_set_color_correction_mode(&gameboy[1], GB_COLOR_CORRECTION_MODERN_ACCURATE);
            }
        }

        var.key = "sameboy_light_temperature_1";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            GB_set_light_temperature(&gameboy[0], atof(var.value));
        }

        var.key = "sameboy_light_temperature_2";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            GB_set_light_temperature(&gameboy[1], atof(var.value));
        }

        var.key = "sameboy_high_pass_filter_mode_1";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            if (strcmp(var.value, "off") == 0) {
                GB_set_highpass_filter_mode(&gameboy[0], GB_HIGHPASS_OFF);
            }
            else if (strcmp(var.value, "accurate") == 0) {
                GB_set_highpass_filter_mode(&gameboy[0], GB_HIGHPASS_ACCURATE);
            }
            else if (strcmp(var.value, "remove dc offset") == 0) {
                GB_set_highpass_filter_mode(&gameboy[0], GB_HIGHPASS_REMOVE_DC_OFFSET);
            }
        }

        var.key = "sameboy_high_pass_filter_mode_2";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            if (strcmp(var.value, "off") == 0) {
                GB_set_highpass_filter_mode(&gameboy[1], GB_HIGHPASS_OFF);
            }
            else if (strcmp(var.value, "accurate") == 0) {
                GB_set_highpass_filter_mode(&gameboy[1], GB_HIGHPASS_ACCURATE);
            }
            else if (strcmp(var.value, "remove dc offset") == 0) {
                GB_set_highpass_filter_mode(&gameboy[1], GB_HIGHPASS_REMOVE_DC_OFFSET);
            }
        }

        var.key = "sameboy_audio_interference_1";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            GB_set_interference_volume(&gameboy[0], atoi(var.value) / 100.0);
        }

        var.key = "sameboy_audio_interference_2";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            GB_set_interference_volume(&gameboy[1], atoi(var.value) / 100.0);
        }

        var.key = "sameboy_rumble_1";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            if (strcmp(var.value, "never") == 0) {
                GB_set_rumble_mode(&gameboy[0], GB_RUMBLE_DISABLED);
            }
            else if (strcmp(var.value, "rumble-enabled games") == 0) {
                GB_set_rumble_mode(&gameboy[0], GB_RUMBLE_CARTRIDGE_ONLY);
            }
            else if (strcmp(var.value, "all games") == 0) {
                GB_set_rumble_mode(&gameboy[0], GB_RUMBLE_ALL_GAMES);
            }
        }

        var.key = "sameboy_rumble_2";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            if (strcmp(var.value, "never") == 0) {
                GB_set_rumble_mode(&gameboy[1], GB_RUMBLE_DISABLED);
            }
            else if (strcmp(var.value, "rumble-enabled games") == 0) {
                GB_set_rumble_mode(&gameboy[1], GB_RUMBLE_CARTRIDGE_ONLY);
            }
            else if (strcmp(var.value, "all games") == 0) {
                GB_set_rumble_mode(&gameboy[1], GB_RUMBLE_ALL_GAMES);
            }
        }

    }
    set_variable_visibility();
}

void retro_init(void)
{
    const char *dir = NULL;

    if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir) {
        snprintf(retro_system_directory, sizeof(retro_system_directory), "%s", dir);
    }
    else {
        snprintf(retro_system_directory, sizeof(retro_system_directory), "%s", ".");
    }

    if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging)) {
        log_cb = logging.log;
    }
    else {
        log_cb = fallback_log;
    }

    if (environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL)) {
        libretro_supports_bitmasks = true;
    }

    init_output_audio_buffer(16384);
}

void retro_deinit(void)
{
    free(frame_buf);
    free(frame_buf_copy);
    frame_buf = NULL;
    frame_buf_copy = NULL;

    free_output_audio_buffer();

    libretro_supports_bitmasks = false;
}

unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
    log_cb(RETRO_LOG_INFO, "Connecting device %u into port %u\n", device, port);
}

void retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof(*info));
    info->library_name     = "SameBoy";
#ifdef GIT_VERSION
    info->library_version  = GB_VERSION GIT_VERSION;
#else
    info->library_version  = GB_VERSION;
#endif
    info->need_fullpath    = false;
    info->valid_extensions = "gb|gbc";
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    struct retro_game_geometry geom;
    struct retro_system_timing timing = { GB_get_usual_frame_rate(&gameboy[0]), GB_get_sample_rate(&gameboy[audio_out])};

    if (emulated_devices == 2) {
        if (screen_layout == LAYOUT_TOP_DOWN) {
            geom.base_width = GB_get_screen_width(&gameboy[0]);
            geom.base_height = GB_get_screen_height(&gameboy[0]) * emulated_devices;
            geom.aspect_ratio = (double)GB_get_screen_width(&gameboy[0]) / (emulated_devices * GB_get_screen_height(&gameboy[0]));
        }
        else if (screen_layout == LAYOUT_LEFT_RIGHT) {
            geom.base_width = GB_get_screen_width(&gameboy[0]) * emulated_devices;
            geom.base_height = GB_get_screen_height(&gameboy[0]);
            geom.aspect_ratio = ((double)GB_get_screen_width(&gameboy[0]) * emulated_devices) / GB_get_screen_height(&gameboy[0]);
        }
    }
    else {
        geom.base_width = GB_get_screen_width(&gameboy[0]);
        geom.base_height = GB_get_screen_height(&gameboy[0]);
        geom.aspect_ratio = (double)GB_get_screen_width(&gameboy[0]) / GB_get_screen_height(&gameboy[0]);
    }

    geom.max_width = MAX_VIDEO_WIDTH * emulated_devices;
    geom.max_height = MAX_VIDEO_HEIGHT * emulated_devices;

    info->geometry = geom;
    info->timing   = timing;
}

void retro_set_environment(retro_environment_t cb)
{
    bool categories_supported;

    environ_cb = cb;

    libretro_set_core_options(environ_cb, &categories_supported);

    environ_cb(RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO, (void*)subsystems);
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
    (void)cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
    audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
    input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
    input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
    video_cb = cb;
}

void retro_reset(void)
{
    check_variables();

    for (int i = 0; i < emulated_devices; i++) {
        init_for_current_model(i);
        GB_reset(&gameboy[i]);
    }

    geometry_updated = true;
}

void retro_run(void)
{

    bool updated = false;

    if (!initialized) {
        geometry_updated = false;
    }

    if (geometry_updated) {
        struct retro_system_av_info info;
        retro_get_system_av_info(&info);
        environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &info.geometry);
        geometry_updated = false;
    }

    if (!frame_buf) {
        return;
    }

    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) {
        check_variables();
    }

    if (emulated_devices == 2) {
        GB_update_keys_status(&gameboy[0], 0);
        GB_update_keys_status(&gameboy[1], 1);
    }
    else if (emulated_devices == 1 && (model[0] == GB_MODEL_SGB_PAL || model[0] == GB_MODEL_SGB_NTSC || model[0] == GB_MODEL_SGB2)) {
        for (unsigned i = 0; i < 4; i++) {
            GB_update_keys_status(&gameboy[0], i);
        }
    }
    else {
        GB_update_keys_status(&gameboy[0], 0);
    }

    vblank1_occurred = vblank2_occurred = false;
    signed delta = 0;
    if (emulated_devices == 2) {
    while (!vblank1_occurred || !vblank2_occurred) {
            if (delta >= 0) {
                delta -= GB_run(&gameboy[0]);
            }
            else {
                delta += GB_run(&gameboy[1]);
            }
        }
    }
    else {
        GB_run_frame(&gameboy[0]);
    }

    if (emulated_devices == 2) {
        if (screen_layout == LAYOUT_TOP_DOWN) {
            video_cb(frame_buf,
                     GB_get_screen_width(&gameboy[0]),
                     GB_get_screen_height(&gameboy[0]) * emulated_devices,
                     GB_get_screen_width(&gameboy[0]) * sizeof(uint32_t));
        }
        else if (screen_layout == LAYOUT_LEFT_RIGHT) {
            unsigned pitch = GB_get_screen_width(&gameboy[0]) * emulated_devices;
            unsigned pixels_per_device = GB_get_screen_width(&gameboy[0]) * GB_get_screen_height(&gameboy[0]);
            for (int y = 0; y < GB_get_screen_height(&gameboy[0]); y++) {
                for (unsigned i = 0; i < emulated_devices; i++) {
                    memcpy(frame_buf_copy + y * pitch + GB_get_screen_width(&gameboy[0]) * i,
                           frame_buf + pixels_per_device * i + y * GB_get_screen_width(&gameboy[0]),
                           GB_get_screen_width(&gameboy[0]) * sizeof(uint32_t));
                }
            }

            video_cb(frame_buf_copy, GB_get_screen_width(&gameboy[0]) * emulated_devices, GB_get_screen_height(&gameboy[0]), GB_get_screen_width(&gameboy[0]) * emulated_devices * sizeof(uint32_t));
        }
    }
    else {
        video_cb(frame_buf,
                 GB_get_screen_width(&gameboy[0]),
                 GB_get_screen_height(&gameboy[0]),
                 GB_get_screen_width(&gameboy[0]) * sizeof(uint32_t));
    }

    upload_output_audio_buffer();
    initialized = true;
}

static enum rom_type check_rom_header(const uint8_t *data, size_t size)
{
    enum rom_type type;
    uint8_t cgb_flag;
    uint8_t sgb_flag;

    if (!data || (size < 0x146 + 1)) {
        return ROM_TYPE_INVALID;
    }

    type     = ROM_TYPE_DMG;
    cgb_flag = data[0x143];
    sgb_flag = data[0x146];

    if ((cgb_flag == 0x80) || (cgb_flag == 0xC0)) {
        type = ROM_TYPE_CGB;
    }

    if ((type == ROM_TYPE_DMG) && (sgb_flag == 0x03)) {
        type = ROM_TYPE_SGB;
    }

    return type;
}

bool retro_load_game(const struct retro_game_info *info)
{
    enum rom_type content_type  = ROM_TYPE_INVALID;
    const uint8_t *content_data = NULL;
    size_t content_size;

    if (info) {
        content_data = (const uint8_t *)info->data;
        content_size = info->size;
        content_type = check_rom_header(content_data, content_size);
    }

    check_variables();

    switch (content_type) {
        case ROM_TYPE_DMG:
            auto_model[0] = GB_MODEL_DMG_B;
            auto_model[1] = GB_MODEL_DMG_B;
            break;
        case ROM_TYPE_SGB:
            auto_model[0] = auto_sgb_enabled[0] ? auto_sgb_model[0] : GB_MODEL_DMG_B;
            auto_model[1] = auto_sgb_enabled[1] ? auto_sgb_model[1] : GB_MODEL_DMG_B;
            break;
        case ROM_TYPE_CGB:
            auto_model[0] = GB_MODEL_CGB_E;
            auto_model[1] = GB_MODEL_CGB_E;
            break;
        case ROM_TYPE_INVALID:
        default:
            log_cb(RETRO_LOG_ERROR, "Invalid content\n");
            return false;
    }

    frame_buf = (uint32_t *)malloc(MAX_VIDEO_PIXELS * emulated_devices * sizeof(uint32_t));
    memset(frame_buf, 0, MAX_VIDEO_PIXELS * emulated_devices * sizeof(uint32_t));

    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
        log_cb(RETRO_LOG_ERROR, "XRGB8888 is not supported\n");
        return false;
    }

    for (int i = 0; i < emulated_devices; i++) {
        init_for_current_model(i);
        GB_load_rom_from_buffer(&gameboy[i], content_data, content_size);
    }

    bool achievements = true;
    environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS, &achievements);

    if (environ_cb(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumble)) {
        log_cb(RETRO_LOG_INFO, "Rumble environment supported\n");
    }
    else {
        log_cb(RETRO_LOG_INFO, "Rumble environment not supported\n");
    }

    check_variables();

    retro_set_memory_maps();

    return true;
}

void retro_unload_game(void)
{
    for (int i = 0; i < emulated_devices; i++) {
        log_cb(RETRO_LOG_INFO, "Unloading GB: %d\n", emulated_devices);
        GB_free(&gameboy[i]);
    }
}

unsigned retro_get_region(void)
{
    return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num_info)
{
    if ((type == RETRO_GAME_TYPE_GAMEBOY_LINK_2P) && (num_info >= 2)) {
        emulated_devices = 2;
    }
    else {
        return false; /* all other types are unhandled for now */
    }

    check_variables();

    frame_buf = (uint32_t*)malloc(emulated_devices * MAX_VIDEO_PIXELS * sizeof(uint32_t));
    frame_buf_copy = (uint32_t*)malloc(emulated_devices * MAX_VIDEO_PIXELS * sizeof(uint32_t));

    memset(frame_buf, 0, emulated_devices * MAX_VIDEO_PIXELS * sizeof(uint32_t));
    memset(frame_buf_copy, 0, emulated_devices * MAX_VIDEO_PIXELS * sizeof(uint32_t));

    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
        log_cb(RETRO_LOG_ERROR, "XRGB8888 is not supported\n");
        return false;
    }

    for (int i = 0; i < emulated_devices; i++) {
        enum rom_type content_type  = ROM_TYPE_INVALID;
        const uint8_t *content_data = info[i].data;
        size_t content_size         = info[i].size;

        content_type = check_rom_header(content_data, content_size);

        switch (content_type) {
            case ROM_TYPE_DMG:
                auto_model[i] = GB_MODEL_DMG_B;
                break;
            case ROM_TYPE_SGB:
                auto_model[i] = auto_sgb_enabled[i] ? auto_sgb_model[i] : GB_MODEL_DMG_B;
                break;
            case ROM_TYPE_CGB:
                auto_model[i] = GB_MODEL_CGB_E;
                break;
            case ROM_TYPE_INVALID:
            default:
                log_cb(RETRO_LOG_ERROR, "Invalid content\n");
                return false;
        }

        init_for_current_model(i);
        GB_load_rom_from_buffer(&gameboy[i], content_data, content_size);
    }

    bool achievements = true;
    environ_cb(RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS, &achievements);

    if (environ_cb(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumble)) {
        log_cb(RETRO_LOG_INFO, "Rumble environment supported\n");
    }
    else {
        log_cb(RETRO_LOG_INFO, "Rumble environment not supported\n");
    }

    check_variables();
    return true;
}

size_t retro_serialize_size(void)
{
    static size_t maximum_save_size = 0;
    if (maximum_save_size) {
        return maximum_save_size * 2;
    }

    GB_gameboy_t temp;

    GB_init(&temp, GB_MODEL_DMG_B);
    maximum_save_size = GB_get_save_state_size(&temp);
    GB_free(&temp);

    GB_init(&temp, GB_MODEL_CGB_E);
    maximum_save_size = MAX(maximum_save_size, GB_get_save_state_size(&temp));
    GB_free(&temp);

    GB_init(&temp, GB_MODEL_SGB2);
    maximum_save_size = MAX(maximum_save_size, GB_get_save_state_size(&temp));
    GB_free(&temp);

    return maximum_save_size * 2;
}

bool retro_serialize(void *data, size_t size)
{

    if (!initialized || !data) {
        return false;
    }

    size_t offset = 0;

    for (int i = 0; i < emulated_devices; i++)  {
        size_t state_size = GB_get_save_state_size(&gameboy[i]);
        if (state_size > size) {
            return false;
        }

        GB_save_state_to_buffer(&gameboy[i], ((uint8_t *) data) + offset);
        offset += state_size;
        size -= state_size;
    }

    return true;
}

bool retro_unserialize(const void *data, size_t size)
{
    for (int i = 0; i < emulated_devices; i++) {
        size_t state_size = GB_get_save_state_size(&gameboy[i]);
        if (state_size > size) {
            return false;
        }

        if (GB_load_state_from_buffer(&gameboy[i], data, state_size)) {
            return false;
        }

        size -= state_size;
        data = ((uint8_t *)data) + state_size;
    }

    return true;

}

void *retro_get_memory_data(unsigned type)
{
    void *data = NULL;
    if (emulated_devices == 1) {
        switch (type) {
            case RETRO_MEMORY_SYSTEM_RAM:
                data = gameboy[0].ram;
                break;
            case RETRO_MEMORY_SAVE_RAM:
                if (gameboy[0].cartridge_type->has_battery && gameboy[0].mbc_ram_size != 0) {
                    data = gameboy[0].mbc_ram;
                }
                else {
                    data = NULL;
                }
                break;
            case RETRO_MEMORY_VIDEO_RAM:
                data = gameboy[0].vram;
                break;
            case RETRO_MEMORY_RTC:
                if (gameboy[0].cartridge_type->has_battery) {
                    data = GB_GET_SECTION(&gameboy[0], rtc);
                }
                else {
                    data = NULL;
                }
                break;
            default:
                break;
        }
    }
    else {
        switch (type) {
            case RETRO_MEMORY_GAMEBOY_1_SRAM:
                if (gameboy[0].cartridge_type->has_battery && gameboy[0].mbc_ram_size != 0) {
                    data = gameboy[0].mbc_ram;
                }
                else {
                    data = NULL;
                }
                break;
            case RETRO_MEMORY_GAMEBOY_2_SRAM:
                if (gameboy[1].cartridge_type->has_battery && gameboy[1].mbc_ram_size != 0) {
                    data = gameboy[1].mbc_ram;
                }
                else {
                    data = NULL;
                }
                break;
            case RETRO_MEMORY_GAMEBOY_1_RTC:
                if (gameboy[0].cartridge_type->has_battery) {
                    data = GB_GET_SECTION(&gameboy[0], rtc);
                }
                else {
                    data = NULL;
                }
                break;
            case RETRO_MEMORY_GAMEBOY_2_RTC:
                if (gameboy[1].cartridge_type->has_battery) {
                    data = GB_GET_SECTION(&gameboy[1], rtc);
                }
                else {
                    data = NULL;
                }
                break;
            default:
                break;
        }
    }

    return data;
}

size_t retro_get_memory_size(unsigned type)
{
    size_t size = 0;
    if (emulated_devices == 1) {
        switch (type) {
            case RETRO_MEMORY_SYSTEM_RAM:
                size = gameboy[0].ram_size;
                break;
            case RETRO_MEMORY_SAVE_RAM:
                if (gameboy[0].cartridge_type->has_battery && gameboy[0].mbc_ram_size != 0) {
                    size = gameboy[0].mbc_ram_size;
                }
                else {
                    size = 0;
                }
                break;
            case RETRO_MEMORY_VIDEO_RAM:
                size = gameboy[0].vram_size;
                break;
            case RETRO_MEMORY_RTC:
                if (gameboy[0].cartridge_type->has_battery) {
                    size = GB_SECTION_SIZE(rtc);
                }
                else {
                    size =  0;
                }
                break;
            default:
                break;
        }
    }
    else {
        switch (type) {
            case RETRO_MEMORY_GAMEBOY_1_SRAM:
                if (gameboy[0].cartridge_type->has_battery && gameboy[0].mbc_ram_size != 0) {
                    size = gameboy[0].mbc_ram_size;
                }
                else {
                    size = 0;
                }
                break;
            case RETRO_MEMORY_GAMEBOY_2_SRAM:
                if (gameboy[1].cartridge_type->has_battery && gameboy[1].mbc_ram_size != 0) {
                    size = gameboy[1].mbc_ram_size;
                }
                else {
                    size = 0;
                }
                break;
            case RETRO_MEMORY_GAMEBOY_1_RTC:
                if (gameboy[0].cartridge_type->has_battery) {
                    size = GB_SECTION_SIZE(rtc);
                }
                break;
            case RETRO_MEMORY_GAMEBOY_2_RTC:
                if (gameboy[1].cartridge_type->has_battery) {
                    size = GB_SECTION_SIZE(rtc);
                }
                break;
            default:
                break;
        }
    }

    return size;
}

void retro_cheat_reset(void)
{}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
    (void)index;
    (void)enabled;
    (void)code;
}
