#include "emulate.h"

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "Core/gb.h"

// Auto-generated via `glib-compile-resources` from `resources.gresource.xml`.
#include "build/obj/XdgThumbnailer/resources.h"

#define NB_FRAMES_TO_EMULATE (60 * 10)

#define BOOT_ROM_SIZE (0x100 + 0x800) // The two "parts" of it, which are stored contiguously.

/* --- */

static char *async_input_callback(GB_gameboy_t *gb)
{
    (void)gb;
    return NULL;
}

static void log_callback(GB_gameboy_t *gb, const char *string, GB_log_attributes attributes)
{
    (void)gb, (void)string, (void)attributes; // Swallow any logs.
}

static void vblank_callback(GB_gameboy_t *gb, GB_vblank_type_t type)
{
    (void)type; // Ignore the type, we use VBlank counting as a kind of pacing (and to avoid tearing).

    unsigned *nb_frames_left = GB_get_user_data(gb);
    (*nb_frames_left)--;

    // *Do* render the very last frame.
    if (*nb_frames_left == 1) {
        GB_set_rendering_disabled(gb, false);
    }
}

static uint32_t rgb_encode(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t rgba;
    // The GdkPixbuf that will be created from the screen buffer later, expects components in the
    // order [red, green, blue, alpha], from a uint8_t[] buffer.
    // But SameBoy requires a uint32_t[] buffer, and don't know the endianness of `uint32_t`.
    // So we treat each uint32_t as a 4-byte buffer, and write the bytes accordingly.
    // This is guaranteed to not be UB, because casting a `T*` to any flavour of `char*` accesses
    // and modifies the `T`'s "object representation".
    uint8_t *bytes = (uint8_t *)&rgba;
    bytes[0] = r;
    bytes[1] = g;
    bytes[2] = b;
    bytes[3] = 0xFF;
    return rgba;
}

uint8_t emulate(const char *path, uint32_t screen[static GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT])
{
    GB_gameboy_t gb;
    GB_init(&gb, GB_MODEL_CGB_E);

    const char *last_dot = strrchr(path, '.');
    bool is_isx = last_dot && strcmp(last_dot + 1, "isx") == 0;
    if (is_isx ? GB_load_isx(&gb, path) : GB_load_rom(&gb, path)) {
        exit(EXIT_FAILURE);
    }

    GError *error = NULL;
    GBytes *boot_rom = g_resource_lookup_data(resources_get_resource(), "/thumbnailer/cgb_boot_fast.bin",
                                              G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
    g_assert_no_error(error); // This shouldn't be able to fail.
    size_t boot_rom_size;
    const uint8_t *boot_rom_data = g_bytes_get_data(boot_rom, &boot_rom_size);
    g_assert_cmpuint(boot_rom_size, ==, BOOT_ROM_SIZE);
    GB_load_boot_rom_from_buffer(&gb, boot_rom_data, boot_rom_size);
    g_bytes_unref(boot_rom);

    GB_set_vblank_callback(&gb, vblank_callback);
    GB_set_pixels_output(&gb, screen);
    GB_set_rgb_encode_callback(&gb, rgb_encode);
    GB_set_async_input_callback(&gb, async_input_callback);
    GB_set_log_callback(&gb, log_callback); // Anything bizarre the ROM does during emulation, we don't care about.
    GB_set_color_correction_mode(&gb, GB_COLOR_CORRECTION_MODERN_BALANCED);

    unsigned nb_frames_left = NB_FRAMES_TO_EMULATE;
    GB_set_user_data(&gb, &nb_frames_left);

    GB_set_rendering_disabled(&gb, true);
    GB_set_turbo_mode(&gb, true, true);
    while (nb_frames_left) {
        GB_run(&gb);
    }

    int cgb_flag = GB_read_memory(&gb, 0x143) & 0xC0;
    GB_free(&gb);
    return cgb_flag;
}
