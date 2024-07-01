#pragma once

#include <gio/gio.h>
#include <glib.h>

void start_thumbnailing(unsigned handle, GCancellable *cancellable, gboolean is_urgent, char const *uri,
                        char const *mime_type);
