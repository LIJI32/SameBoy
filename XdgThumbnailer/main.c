#include "main.h"

#include <gio/gio.h>
#include <glib-object.h>
#include <glib-unix.h>
#include <glib.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "emulate.h"
#include "tasks.h"
#include "thumbnail.h"

// Auto-generated via `gdbus-codegen` from `interface.xml`.
#include "build/obj/XdgThumbnailer/interface.h"

static char const name_on_bus[] = "com.github.liji32.sameboy.XdgThumbnailer";
static char const object_path[] = "/com/github/liji32/sameboy/XdgThumbnailer";

ThumbnailerSpecializedThumbnailer1 *thumbnailer_interface = NULL;
static unsigned max_nb_worker_threads;

static gboolean handle_queue(void *instance, GDBusMethodInvocation *invocation, char const *uri, char const *mime_type,
                             char const *flavor, gboolean urgent, void *user_data)
{
    ThumbnailerSpecializedThumbnailer1 *skeleton = instance;
    g_info("Received Queue(uri=\"%s\", mime_type=\"%s\", flavor=\"%s\", urgent=%s) request", uri, mime_type, flavor,
           urgent ? "true" : "false");
    g_assert(skeleton == thumbnailer_interface);

    struct NewTaskInfo task_info = new_task(urgent);
    start_thumbnailing(task_info.handle, task_info.cancellable, urgent, uri, mime_type);

    thumbnailer_specialized_thumbnailer1_complete_queue(skeleton, invocation, task_info.handle);
    return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean handle_dequeue(void *instance, GDBusMethodInvocation *invocation, unsigned handle, void *user_data)
{
    ThumbnailerSpecializedThumbnailer1 *skeleton = instance;
    g_info("Received Dequeue(handle=%u) request", handle);
    g_assert(skeleton == thumbnailer_interface);

    cancel_task(handle);

    return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static GMainLoop *main_loop;

static void on_bus_acquired(GDBusConnection *connection, const char *name, void *user_data)
{
    g_assert_cmpstr(name, ==, name_on_bus);
    (void)user_data;
    g_info("Acquired bus");

    // Create the interface, and hook up callbacks for when its methods are called.
    thumbnailer_interface = thumbnailer_specialized_thumbnailer1_skeleton_new();
    g_signal_connect(thumbnailer_interface, "handle-queue", G_CALLBACK(handle_queue), NULL);
    g_signal_connect(thumbnailer_interface, "handle-dequeue", G_CALLBACK(handle_dequeue), NULL);

    // Export the interface on the bus.
    GError *error = NULL;
    GDBusInterfaceSkeleton *interface = G_DBUS_INTERFACE_SKELETON(thumbnailer_interface);
    gboolean res = g_dbus_interface_skeleton_export(interface, connection, object_path, &error);
    g_assert(res);
    g_assert_no_error(error);
    if (error) {
        g_error("Error exporting interface \"%s\" at \"%s\": %s", g_dbus_interface_skeleton_get_info(interface)->name,
                object_path, error->message);
        // NOTREACHED
    }
}

static void on_name_acquired(GDBusConnection *connection, const char *name, void *user_data)
{
    g_assert_cmpstr(name, ==, name_on_bus);
    (void)user_data;

    g_info("Acquired the name \"%s\" on the session bus", name);
}

static void on_name_lost(GDBusConnection *connection, const char *name, void *user_data)
{
    g_assert_cmpstr(name, ==, name_on_bus);
    (void)user_data;

    if (connection != NULL) {
        g_info("Lost the name \"%s\" on the session bus", name);
    }
    else {
        g_error("Failed to connect to session bus");
    }
    g_main_loop_quit(main_loop);
}

static gboolean handle_sigterm(void *user_data)
{
    g_info("SIGTERM received! Quitting...");

    g_main_loop_quit(main_loop);
    return G_SOURCE_CONTINUE; // Do not remove this source ourselves, let the post-main loop do so.
}

int main(int argc, char const *argv[])
{
    max_nb_worker_threads = g_get_num_processors();
    // unsigned active_worker_threads = 0;
    //  Create the task queue *before* starting to accept tasks from D-Bus.
    init_tasks();
    // Likewise, create the main loop before then, so it can be aborted even before entering it.
    main_loop = g_main_loop_new(NULL, false);

    // Refuse to replace the name or be replaced; there should only be one instance of the
    // thumbnailer on the bus at all times. To replace this program, kill it.
    unsigned owner_id = g_bus_own_name(G_BUS_TYPE_SESSION, name_on_bus, G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE,
                                       on_bus_acquired, on_name_acquired, on_name_lost, NULL, NULL);

    unsigned sigterm_source_id = g_unix_signal_add(SIGTERM, handle_sigterm, NULL);
    g_main_loop_run(main_loop);
    // This must be done before destroying the main loop.
    gboolean removed = g_source_remove(sigterm_source_id);
    g_assert(removed);

    g_info("Waiting for outstanding tasks...");
    cleanup_tasks(); // Also waits for any remaining tasks.
    // "Pedantic" cleanup for Valgrind et al.
    g_main_loop_unref(main_loop);
    g_bus_unown_name(owner_id);
    if (thumbnailer_interface) {
        g_dbus_interface_skeleton_unexport(G_DBUS_INTERFACE_SKELETON(thumbnailer_interface));
        g_object_unref(thumbnailer_interface);
    }
    return 0;
}
