/* Host-test stubs for the Android-only pieces session.c links against. */
#include "../render_gles.h"
#include "../audio_aaudio.h"

void ANativeWindow_release(ANativeWindow *win) { (void)win; }

sb_renderer *sb_render_start(ANativeWindow *win, sb_emulator *emu, const atomic_int *filter)
{ (void)win; (void)emu; (void)filter; return (sb_renderer *)0; }
void sb_render_stop(sb_renderer *r) { (void)r; }

sb_audio *sb_audio_start(sb_ring *ring, atomic_bool *dead) { (void)ring; (void)dead; return (sb_audio *)0; }
void sb_audio_set_paused(sb_audio *a, int paused) { (void)a; (void)paused; }
void sb_audio_stop(sb_audio *a) { (void)a; }
