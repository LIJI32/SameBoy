#pragma once
#include <android/native_window.h>
#include "emulator.h"

typedef struct sb_renderer sb_renderer;

/* filter: 0 = off (nearest blit), 1 = LCD dot matrix; read every frame, may be NULL. */
sb_renderer *sb_render_start(ANativeWindow *win, sb_emulator *emu, const atomic_int *filter);
void         sb_render_stop(sb_renderer *r);
