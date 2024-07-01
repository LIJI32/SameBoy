#include "thumbnail.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <glib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "emulate.h"
#include "main.h"
#include "tasks.h"

#define THUMBNAILING_ERROR_DOMAIN (g_quark_from_static_string("thumbnailing"))

static char const dmg_only_resource_path[] = "/thumbnailer/CartridgeTemplate.png";
static char const dual_resource_path[] = "/thumbnailer/UniversalCartridgeTemplate.png";
static char const cgb_only_resource_path[] = "/thumbnailer/ColorCartridgeTemplate.png";

static char const gb_mime_type[] = "application/x-gameboy-rom";
static char const gbc_mime_type[] = "application/x-gameboy-color-rom";
static char const isx_mime_type[] = "application/x-gameboy-isx";

/* --- */

struct TaskData {
    char *contents;
    size_t length;
    enum FileKind kind;
};

static void destroy_task_data(void *data)
{
    struct TaskData *task_data = data;

    g_free(task_data->contents);
    g_slice_free(struct TaskData, task_data);
}

char const *mime_type(enum FileKind kind)
{
    switch (kind) {
        case KIND_GB:
            return gb_mime_type;
        case KIND_GBC:
            return gbc_mime_type;
        case KIND_ISX:
            return isx_mime_type;
    }
}

/* --- */

#define GB_SCREEN_WIDTH 160
#define GB_SCREEN_HEIGHT 144

static void generate_thumbnail(GTask *task, void *source_object, void *data, GCancellable *cancellable)
{
    struct TaskData *task_data = data;

    uint32_t screen_raw[GB_SCREEN_WIDTH * GB_SCREEN_HEIGHT];
    unsigned cgb_flag =
        emulate(task_data->kind, (unsigned char const *)task_data->contents, task_data->length, screen_raw);

    // Generate the thumbnail from `screen_raw` and `cgb_flag`.

    // `screen_raw` is properly formatted for this operation; see the comment in `rgb_encode` for a
    // discussion of why and how.
    GdkPixbuf *screen = gdk_pixbuf_new_from_data((uint8_t *)screen_raw, GDK_COLORSPACE_RGB,
                                                 true,                                  // Yes, we have alpha!
                                                 8,                                     // bpp
                                                 GB_SCREEN_WIDTH, GB_SCREEN_HEIGHT,     // Size.
                                                 GB_SCREEN_WIDTH * sizeof(*screen_raw), // Row stride.
                                                 NULL, NULL);                           // Do not free the buffer.
    // Scale the screen and position it in the appropriate place for compositing the cartridge templates.
    GdkPixbuf *scaled_screen = gdk_pixbuf_new(GDK_COLORSPACE_RGB, true, 8, 1024, 1024);
    gdk_pixbuf_scale(screen, scaled_screen, 192, 298,           // Match the displacement below.
                     GB_SCREEN_WIDTH * 4, GB_SCREEN_HEIGHT * 4, // Cropping the scaled rectangle...
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
                         255);               // Blending factor of the source.
    g_object_unref(template);

    char file_size[sizeof(G_STRINGIFY(SIZE_MAX))];
    sprintf(file_size, "%zu", task_data->length);
    // TODO: proper file name
    gdk_pixbuf_save(scaled_screen, "/tmp/output.png", "png", &error, // "Base" parameters.
                    "tEXt::Thumb::URI", g_task_get_name(task),       // URI of the file being thumbnailed.
                    // "tEXt::Thumb::MTime", "", // TODO
                    "tEXt::Thumb::Size", file_size,                  // Size (in bytes) of the file being thumbnailed.
                    "tEXt::Thumb::Mimetype", mime_type(task_data->kind), // MIME type of the file being thumbnailed.
                    NULL);
    g_object_unref(scaled_screen);

    g_task_return_boolean(task, true);
    g_object_unref(task);
}

// Callback when an async file operation completes.
static void on_file_ready(GObject *source_object, GAsyncResult *res, void *user_data)
{
    GFile *file = G_FILE(source_object);
    GTask *task = user_data;
    char const *uri = g_task_get_name(task);
    g_debug("File \"%s\" is done being read", uri);
    struct TaskData *task_data = g_task_get_task_data(task);

    GError *error = NULL;
    g_file_load_contents_finish(file, res, &task_data->contents, &task_data->length, NULL, &error);
    g_object_unref(file);

    if (error) {
        g_task_return_new_error(task, THUMBNAILING_ERROR_DOMAIN, ERROR_UNKNOWN_SCHEME_OR_MIME,
                                "Failed to load URI \"%s\": %s", uri, error->message);
        g_object_unref(task);
        return;
    }

    if (g_task_return_error_if_cancelled(task)) {
        g_object_unref(task);
        return;
    }

    // TODO: cap the max number of active threads.
    g_task_run_in_thread(task, generate_thumbnail);
}

/* --- */

static void on_thumbnailing_end(GObject *source_object, GAsyncResult *res, void *user_data)
{
    // TODO: start a new thread if some task is pending.

    g_assert_null(source_object); // The object that was passed to `g_task_new`.
    GTask *task = G_TASK(res);
    g_info("Ending thumbnailing for \"%s\"", g_task_get_name(task));
    unsigned handle = GPOINTER_TO_UINT(user_data);
    char const *uri = g_task_get_name(task);

    GError *error = NULL;
    if (g_task_propagate_boolean(task, &error)) {
        g_signal_emit_by_name(thumbnailer_interface, "ready", handle, uri);
    }
    else if (!g_cancellable_is_cancelled(g_task_get_cancellable(task))) {
        // If the task was cancelled, do not emit an error response.
        g_signal_emit_by_name(thumbnailer_interface, "error", handle, uri, error->code, error->message);
    }
    g_signal_emit_by_name(thumbnailer_interface, "finished", handle);

    finished_task(handle);
}

void start_thumbnailing(unsigned handle, GCancellable *cancellable, gboolean is_urgent, char const *uri,
                        char const *mime_type)
{
    g_signal_emit_by_name(thumbnailer_interface, "started", handle);

    GTask *task = g_task_new(NULL, cancellable, on_thumbnailing_end, GUINT_TO_POINTER(handle));
    g_task_set_priority(task, is_urgent ? G_PRIORITY_HIGH : G_PRIORITY_DEFAULT);
    g_task_set_name(task, uri);

    enum FileKind kind;
    if (g_strcmp0(mime_type, gbc_mime_type) == 0) {
        kind = KIND_GBC;
    }
    else if (g_strcmp0(mime_type, gb_mime_type) == 0) {
        kind = KIND_GB;
    }
    else if (g_strcmp0(mime_type, isx_mime_type) == 0) {
        kind = KIND_ISX;
    }
    else {
        g_task_return_new_error(task, THUMBNAILING_ERROR_DOMAIN, ERROR_UNKNOWN_SCHEME_OR_MIME,
                                "Unsupported MIME type %s", mime_type);
        g_object_unref(task);
        return;
    }

    struct TaskData *task_data = g_slice_new(struct TaskData);
    task_data->contents = NULL;
    task_data->kind = kind;
    g_task_set_task_data(task, task_data, destroy_task_data);

    GFile *file = g_file_new_for_uri(uri);
    g_file_load_contents_async(file, cancellable, on_file_ready, task);
}
