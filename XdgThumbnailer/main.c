#include <errno.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emulate.h"

static const char dmg_only_resource_path[] = "/thumbnailer/CartridgeTemplate.png";
static const char dual_resource_path[] = "/thumbnailer/UniversalCartridgeTemplate.png";
static const char cgb_only_resource_path[] = "/thumbnailer/ColorCartridgeTemplate.png";

static GdkPixbuf *generate_thumbnail(const char *input_path)
{
    uint32_t screen_raw[GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT];
    uint8_t cgb_flag = emulate(input_path, screen_raw);

    // Generate the thumbnail from `screen_raw` and `cgb_flag`.

    // `screen_raw` is properly formatted for this operation; see the comment in `rgb_encode` for a
    // discussion of why and how.
    GdkPixbuf *screen = gdk_pixbuf_new_from_data((uint8_t *)screen_raw, GDK_COLORSPACE_RGB,
                                                 true,                                    // Yes, we have alpha!
                                                 8,                                       // bpp
                                                 GB_SCREEN_WIDTH, GB_SCREEN_HEIGHT,       // Size.
                                                 GB_SCREEN_WIDTH * sizeof(screen_raw[0]), // Row stride.
                                                 NULL, NULL);                             // Do not free the buffer.
    // Scale the screen and position it in the appropriate place for compositing the cartridge templates.
    GdkPixbuf *scaled_screen = gdk_pixbuf_new(GDK_COLORSPACE_RGB, true, 8, 1024, 1024);
    gdk_pixbuf_scale(screen,                                    // Source.
                     scaled_screen,                             // Destination.
                     192, 298,                                  // Match the displacement below.
                     GB_SCREEN_WIDTH * 4, GB_SCREEN_HEIGHT * 4, // How the scaled rectangle should be cropped.
                     192, 298, // Displace the scaled screen so it lines up with the template.
                     4, 4,     // Scaling factors.
                     GDK_INTERP_NEAREST);
    g_object_unref(screen);

    GError *error = NULL;
    GdkPixbuf *template;
    switch (cgb_flag) {
        case 0xC0:
            template = gdk_pixbuf_new_from_resource(cgb_only_resource_path, &error);
            break;
        case 0x80:
            template = gdk_pixbuf_new_from_resource(dual_resource_path, &error);
            break;
        default:
            template = gdk_pixbuf_new_from_resource(dmg_only_resource_path, &error);
            break;
    }
    g_assert_no_error(error);
    g_assert_cmpint(gdk_pixbuf_get_width(template), ==, 1024);
    g_assert_cmpint(gdk_pixbuf_get_height(template), ==, 1024);
    gdk_pixbuf_composite(template,           // Source.
                         scaled_screen,      // Destination.
                         0, 0,               // Match the displacement below.
                         1024, 1024,         // Crop of the scaled rectangle.
                         0, 0,               // Displacement of the scaled rectangle.
                         1, 1,               // Scaling factors.
                         GDK_INTERP_NEAREST, // Doesn't really matter, but should be a little faster.
                         255);               // Blending factor of the source onto the destination.
    g_object_unref(template);

    return scaled_screen;
}

static GdkPixbuf *enforce_max_size(GdkPixbuf *thumbnail, unsigned max_size)
{
    g_assert_cmpuint(gdk_pixbuf_get_width(thumbnail), ==, gdk_pixbuf_get_height(thumbnail));
    g_assert_cmpuint(gdk_pixbuf_get_width(thumbnail), ==, 1024);
    // This is only a *max* size; don't bother scaling up.
    // (This also prevents any overflow errors—notice that the scale function takes `int` size parameters!)
    if (max_size > 1024) return thumbnail;
    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(thumbnail, max_size, max_size, GDK_INTERP_BILINEAR);
    g_object_unref(thumbnail);
    return scaled;
}

static void write_thumbnail(GdkPixbuf *thumbnail, const char *output_path)
{
    GError *error = NULL;
    // Intentionally be "not a good citizen":
    // - Write directly to the provided path, instead of atomically replacing it with a fully-formed file;
    //   this is necessary for at least Tumbler (XFCE's thumbnailer daemon), which creates the file **and** keeps the
    //   returned FD—which keeps pointing to the deleted file... which is still empty!
    // - Do not save any metadata to the PNG, since the thumbnailer daemon (again, at least XFCE's, the only one I have
    //   tested with) appears to read the PNG's pixels, and write a new one with the appropriate metadata.
    //   (Thank you! Saves me all that work.)
    gdk_pixbuf_save(thumbnail, output_path, "png", &error, NULL);
    if (error) {
        g_error("Failed to save thumbnail: %s", error->message);
        // NOTREACHED
    }
}

int main(int argc, char *argv[])
{
    if (argc != 3 && argc != 4) {
        g_error("Usage: %s <input path> <output path> [<size>]", argv[0] ? argv[0] : "sameboy-thumbnailer");
        // NOTREACHED
    }
    const char *input_path = argv[1];
    char *output_path = argv[2];    // Gets mutated in-place.
    const char *max_size = argv[3]; // May be NULL.

    g_debug("%s -> %s [%s]", input_path, output_path, max_size ? max_size : "(none)");

    GdkPixbuf *thumbnail = generate_thumbnail(input_path);
    if (max_size) {
        char *endptr;
        errno = 0;
        /* This will implicitly truncate, but enforce_max_size will cap size to 1024 anyway.
           (Not that 4 billion pixels wide icons make sense to begin with)*/
        unsigned size = strtoul(max_size, &endptr, 10);
        if (errno != 0 || *max_size == '\0' || *endptr != '\0') {
            g_error("Invalid size parameter \"%s\": %s", max_size, strerror(errno == 0 ? EINVAL : errno));
            // NOTREACHED
        }

        thumbnail = enforce_max_size(thumbnail, size);
    }
    write_thumbnail(thumbnail, output_path);
    g_object_unref(thumbnail);

    return 0;
}
