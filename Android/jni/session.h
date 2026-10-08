#pragma once
#include <stdint.h>
#include <stddef.h>
#include <android/native_window.h>
#include "emulator.h"

typedef struct sb_session sb_session;

/* All functions tolerate s == NULL (no-op; save_battery returns 0).
   sb_session_start takes ownership of win even when it bails out early.
   Control calls must come from a single thread; only sb_session_battery_dirty
   is any-thread safe. */

sb_session *sb_session_create(int model, const uint8_t *rom, size_t rom_len,
                              const uint8_t *sav, size_t sav_len);
void        sb_session_set_boot_rom(sb_session *s, int type, const uint8_t *data, size_t len);
void        sb_session_reset(sb_session *s);
void        sb_session_start(sb_session *s, ANativeWindow *win);   /* start threads */
void        sb_session_stop(sb_session *s);                        /* stop threads */
void        sb_session_pause(sb_session *s, int paused);
void        sb_session_set_key(sb_session *s, int idx, int pressed);
size_t      sb_session_save_battery(sb_session *s, uint8_t **out);
void        sb_session_destroy(sb_session *s);

void   sb_session_set_turbo(sb_session *s, int on);
void   sb_session_set_rewinding(sb_session *s, int on);
int    sb_session_battery_dirty(sb_session *s);
void   sb_session_clear_battery_dirty(sb_session *s);
size_t sb_session_save_state(sb_session *s, uint8_t **out);
int    sb_session_load_state(sb_session *s, const uint8_t *buf, size_t n);
void   sb_session_switch_model(sb_session *s, int model);
void   sb_session_apply_settings(sb_session *s, const sb_settings *cfg); /* self-parks */
void   sb_session_set_palette(sb_session *s, int builtin_index, const uint32_t rgb[4]); /* self-parks */
void   sb_session_set_volume(sb_session *s, int volume_256);            /* atomic, any thread */
void   sb_session_set_filter(sb_session *s, int mode);   /* 0 off, 1 LCD; atomic, any thread */
int    sb_session_rumble_amplitude(sb_session *s);                     /* any thread */
int    sb_session_add_cheat(sb_session *s, const char *code, const char *desc, int enabled); /* self-parks; 1 ok, 0 rejected */
void   sb_session_remove_all_cheats(sb_session *s);                    /* self-parks */
void   sb_session_set_cheats_enabled(sb_session *s, int on);           /* self-parks */
bool   sb_session_has_accelerometer(sb_session *s);                    /* any thread */
void   sb_session_set_accelerometer(sb_session *s, double x, double y); /* units of g; any thread */
void     sb_session_connect_printer(sb_session *s);     /* self-parks (GB_connect_printer) */
void     sb_session_disconnect_printer(sb_session *s);  /* self-parks */
unsigned sb_session_printer_generation(sb_session *s);
unsigned sb_session_printer_feed(sb_session *s, uint32_t *dst, unsigned max_rows);
void     sb_session_printer_clear(sb_session *s);
bool     sb_session_camera_wanted(sb_session *s);
void     sb_session_camera_deliver(sb_session *s, const uint8_t *gray);

enum { SB_LINK_IDLE = 0, SB_LINK_LISTENING = 1, SB_LINK_CONNECTING = 2,
       SB_LINK_CONNECTED = 3, SB_LINK_ERROR = 4 };
void sb_session_link_listen(sb_session *s, int port);
void sb_session_link_connect(sb_session *s, const char *host, int port);
void sb_session_link_disconnect(sb_session *s);
int  sb_session_link_status(sb_session *s);
void   sb_session_copy_frame(sb_session *s, uint32_t *dst, unsigned *w, unsigned *h);
/* (w << 16) | h of the current output; 0 if s is NULL. Any thread, running or paused. */
uint32_t sb_session_screen_size(sb_session *s);
