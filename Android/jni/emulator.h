#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "ring_buffer.h"
#include <stdatomic.h>

#define SB_MAX_W 256
#define SB_MAX_H 224
#define SB_FB_MAX (SB_MAX_W * SB_MAX_H)

typedef struct sb_emulator sb_emulator;

sb_emulator *sb_emu_create(int model, const uint8_t *rom, size_t rom_len,
                           const uint8_t *sav, size_t sav_len);
void         sb_emu_set_boot_rom(sb_emulator *e, int gb_boot_rom_type,
                                 const uint8_t *data, size_t len);
void         sb_emu_reset(sb_emulator *e);
void         sb_emu_run_frame(sb_emulator *e);
const uint32_t *sb_emu_front_buffer(sb_emulator *e, unsigned *w, unsigned *h);
/* Copies the last completed frame into dst (must hold >= 256*224 uint32_t)
   under the frame lock, so the render thread never reads a buffer the emu
   thread is mid-overwriting. Writes the frame dimensions to *w,*h. */
void         sb_emu_copy_front(sb_emulator *e, uint32_t *dst, unsigned *w, unsigned *h);
/* Current output size, packed (w << 16) | h: 160x144 normally, 256x224 while a
   border is displayed. Reads the cached front-buffer dimensions under fb_mtx —
   no frame copy, safe from any thread whether running or paused. 0 if e is NULL. */
uint32_t     sb_emu_screen_size(sb_emulator *e);
/* Present-on-produce pacing (render thread): block until a new frame is produced past
   *last_seen or timeout_ms elapses. Returns 1 if a new frame arrived, 0 on timeout.
   sb_emu_wake() unblocks a waiter so it can observe a stop flag. */
int          sb_emu_wait_frame(sb_emulator *e, uint64_t *last_seen, int timeout_ms);
void         sb_emu_wake(sb_emulator *e);
sb_ring     *sb_emu_audio_ring(sb_emulator *e);
void         sb_emu_set_key(sb_emulator *e, int gb_key_index, int pressed);
size_t       sb_emu_save_battery(sb_emulator *e, uint8_t **out_malloced);
/* Precondition: stop and join the emulation/render threads before calling. */
void         sb_emu_destroy(sb_emulator *e);

void   sb_emu_set_audio_drop(sb_emulator *e, const atomic_bool *drop_on_full); /* NULL = always block */
size_t sb_emu_save_state(sb_emulator *e, uint8_t **out_malloced);       /* 0 on failure; caller frees */
int    sb_emu_load_state(sb_emulator *e, const uint8_t *buf, size_t n); /* 0 = ok; auto model-switch */
void   sb_emu_switch_model(sb_emulator *e, int model);                  /* GB_model_t value */
void   sb_emu_set_rewind_length(sb_emulator *e, double seconds);
void   sb_emu_set_turbo(sb_emulator *e, int on);                        /* GB_set_turbo_mode(on, false) */
int    sb_emu_rewind_pop(sb_emulator *e);                               /* 1 = popped, 0 = history empty */
int    sb_emu_battery_dirty(sb_emulator *e);                            /* emu thread / parked only */
void   sb_emu_clear_battery_dirty(sb_emulator *e);

/* --- Cheats --- call parked (Core asserts not-running-other-thread). Cheats persist across
   sb_emu_reset / switch_model / load_state natively (only GB_free drops them). */
int  sb_emu_add_cheat(sb_emulator *e, const char *code, const char *desc, int enabled); /* 1 ok, 0 = Core rejected code */
void sb_emu_remove_all_cheats(sb_emulator *e);
void sb_emu_set_cheats_enabled(sb_emulator *e, int on);                  /* master switch */
size_t sb_emu_cheat_count(sb_emulator *e);                             /* test/debug */

/* --- MBC7 accelerometer --- x,y in g. No thread assert in Core: any thread, running or not. */
bool sb_emu_has_accelerometer(sb_emulator *e);
void sb_emu_set_accelerometer(sb_emulator *e, double x, double y);

/* Reads ROM title + CRC32 via a throwaway Core init (no session/emulator).
   title must be >= 17 bytes; it is NUL-terminated. Returns 0 on success,
   -1 if len < 0x150 (too small to be a cartridge). */
int sb_rom_info(const uint8_t *rom, size_t len, char *title, uint32_t *crc32);

typedef struct {
    int    color_correction;   /* GB_color_correction_mode_t */
    double light_temperature;  /* -1..1 */
    int    border_mode;        /* GB_border_mode_t */
    int    highpass;           /* GB_highpass_mode_t */
    int    rtc_mode;           /* GB_rtc_mode_t */
    double rewind_seconds;
    double turbo_cap;          /* 0 = uncapped */
    double interference;       /* 0..1 */
    int rumble_mode;           /* GB_rumble_mode_t: 0=disabled 1=cart 2=all */
} sb_settings;
void sb_emu_apply_settings(sb_emulator *e, const sb_settings *s);
/* builtin_index 0=Grey 1=DMG 2=MGB 3=GBL; -1 => custom from rgb[4] (0x00RRGGBB,
   index 0 darkest .. 3 lightest). */
void sb_emu_set_palette(sb_emulator *e, int builtin_index, const uint32_t rgb[4]);
void sb_emu_set_volume_ptr(sb_emulator *e, const atomic_int *volume);  /* 256 = 1.0; NULL = full */
int sb_emu_rumble_amplitude(sb_emulator *e);   /* 0..255, latest from the rumble callback */

/* --- Game Boy Printer (M7) --- */
void     sb_emu_connect_printer(sb_emulator *e);     /* GB_connect_printer; call parked */
void     sb_emu_disconnect_printer(sb_emulator *e);  /* GB_disconnect_serial; call parked */
unsigned sb_emu_printer_generation(sb_emulator *e);  /* atomic; bumps per printed image + done */
/* Copies up to max_rows rows (160 px each) into dst; returns rows currently available.
   dst may be NULL / max_rows 0 to just query the row count. */
unsigned sb_emu_printer_feed(sb_emulator *e, uint32_t *dst, unsigned max_rows);
void     sb_emu_printer_clear(sb_emulator *e);

/* Pure, testable: grow *buf to (*rows + top + height + bottom) rows of 160 px, fill the
   top/bottom margin rows white (0xFFFFFFFF) and copy `height` rows from `image`. */
void     sb_printer_append(uint32_t **buf, unsigned *rows, const uint32_t *image,
                           unsigned height, unsigned top, unsigned bottom);

/* --- Game Boy Camera (M7) --- 128x112 grayscale sensor window */
#define SB_CAM_W 128
#define SB_CAM_H 112
bool    sb_emu_camera_wanted(sb_emulator *e);                 /* atomic read */
void    sb_emu_camera_deliver(sb_emulator *e, const uint8_t *gray);   /* SB_CAM_W*SB_CAM_H bytes → staging */
/* testable helpers */
void    sb_camera_promote(sb_emulator *e);                    /* staging → sensor under lock */
uint8_t sb_camera_read(const uint8_t *buf, int x, int y);     /* clamp x∈[0,127] y∈[0,111] */

/* --- Link cable (M8) --- */
struct sb_link;   /* fwd */
void sb_emu_link_set(sb_emulator *e, struct sb_link *link);  /* attach (call parked); replaces any old */
void sb_emu_link_clear(sb_emulator *e);                      /* detach + destroy (call parked) */
bool sb_emu_link_dead(sb_emulator *e);                       /* attached link's transport failed */
uint8_t sb_emu_peek_sb(sb_emulator *e);   /* test/debug: current SB (0xFF01) */

#define SB_AUDIO_SAMPLE_RATE 48000
