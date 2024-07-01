#include "emulate.h"

#include <gio/gio.h>
#include <glib.h>
#include <stdint.h>

#include "Core/gb.h"

// Auto-generated via `glib-compile-resources` from `resources.gresource.xml`.
#include "build/obj/XdgThumbnailer/resources.h"

#define NB_FRAMES_TO_EMULATE (60 * 10)

#define BOOT_ROM_SIZE (0x100 + 0x800) // The two "parts" of it, which are stored contiguously.

/* --- */

static char *async_input_callback(GB_gameboy_t *gb) { return NULL; }

static void log_callback(GB_gameboy_t *gb, const char *string, GB_log_attributes attributes)
{
    // Swallow any logs.
}

static void vblank_callback(GB_gameboy_t *gb, GB_vblank_type_t type)
{
    unsigned nb_frames_left = GPOINTER_TO_UINT(GB_get_user_data(gb));
    nb_frames_left--;
    GB_set_user_data(gb, GUINT_TO_POINTER(nb_frames_left));

    // *Do* render the very last frame.
    if (nb_frames_left == 1) {
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
    unsigned char *bytes = (uint8_t *)&rgba;
    bytes[0] = r;
    bytes[1] = g;
    bytes[2] = b;
    bytes[3] = 0xFF;
    return rgba;
}

unsigned emulate(enum FileKind kind, unsigned char const *rom, size_t rom_size, uint32_t screen[static 160 * 144])
{
    GB_gameboy_t gb;
    GB_init(&gb, GB_MODEL_CGB_E);

    GError *error = NULL;
    GBytes *boot_rom = g_resource_lookup_data(resources_get_resource(), "/thumbnailer/cgb_boot_fast.bin",
                                              G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
    g_assert_no_error(error);
    size_t boot_rom_size;
    unsigned char const *boot_rom_data = g_bytes_get_data(boot_rom, &boot_rom_size);
    g_assert_cmpuint(boot_rom_size, ==, BOOT_ROM_SIZE);
    GB_load_boot_rom_from_buffer(&gb, boot_rom_data, boot_rom_size);
    g_bytes_unref(boot_rom);

    if (kind == KIND_ISX) {
        g_assert_not_reached(); // TODO: implement GB_load_isx_from_buffer
    }
    else {
        GB_load_rom_from_buffer(&gb, rom, rom_size);
    }

    GB_set_user_data(&gb, GUINT_TO_POINTER(NB_FRAMES_TO_EMULATE));

    GB_set_vblank_callback(&gb, vblank_callback);
    GB_set_pixels_output(&gb, screen);
    GB_set_rgb_encode_callback(&gb, rgb_encode);
    GB_set_async_input_callback(&gb, async_input_callback);
    GB_set_log_callback(&gb, log_callback);
    GB_set_color_correction_mode(&gb, GB_COLOR_CORRECTION_MODERN_BALANCED);

    GB_set_rendering_disabled(&gb, true);
    GB_set_turbo_mode(&gb, true, true);
    while (GPOINTER_TO_UINT(GB_get_user_data(&gb))) {
        GB_run(&gb);
    }

    unsigned cgb_flag = GB_read_memory(&gb, 0x143) & 0xC0;
    GB_free(&gb);
    return cgb_flag;
}
