#include "../emulator.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Core/gb.h>

/* Build a tiny valid ROM. battery=0: ROM-only; sets BGP, turns the LCD on
   (no boot ROM leaves it off => Core paints colors[4]), then busy-loops.
   battery=1: MBC1+RAM+BATTERY (8KB RAM); program enables cart RAM and writes
   one byte to 0xA000, then busy-loops — a single battery-dirtying write. */
static uint8_t *make_rom(size_t *len, int battery)
{
    size_t n = 32 * 1024;
    uint8_t *rom = calloc(1, n);
    rom[0x100] = 0x00; rom[0x101] = 0xC3; rom[0x102] = 0x50; rom[0x103] = 0x01; /* nop; jp 0x150 */
    if (battery) {
        static const uint8_t prog[] = {
            0x3E, 0x0A,             /* ld a, 0x0A   */
            0xEA, 0x00, 0x00,       /* ld (0x0000), a  ; enable cart RAM */
            0x3E, 0x42,             /* ld a, 0x42   */
            0xEA, 0x00, 0xA0,       /* ld (0xA000), a  ; battery write */
            0x18, 0xFE,             /* jr -2        */
        };
        memcpy(&rom[0x150], prog, sizeof(prog));
        rom[0x147] = 0x03;          /* MBC1+RAM+BATTERY */
        rom[0x149] = 0x02;          /* 8KB cart RAM */
    }
    else {
        static const uint8_t prog[] = {
            0x3E, 0xE4,             /* ld a, 0xE4   */
            0xE0, 0x47,             /* ldh (0x47), a ; BGP = 3,2,1,0 */
            0x3E, 0x91,             /* ld a, 0x91   */
            0xE0, 0x40,             /* ldh (0x40), a ; LCDC: LCD + BG on */
            0x3C,                   /* inc a        */
            0x18, 0xFE,             /* jr -2        */
        };
        memcpy(&rom[0x150], prog, sizeof(prog));
        rom[0x147] = 0x00;
        rom[0x149] = 0x00;
    }
    rom[0x148] = 0x00;
    uint8_t c = 0;
    for (int i = 0x134; i <= 0x14C; i++) c = c - rom[i] - 1;
    rom[0x14D] = c;
    *len = n;
    return rom;
}

static void drain(sb_emulator *e)
{
    static int16_t buf[4096 * 2];
    size_t got;
    while ((got = sb_ring_pop(sb_emu_audio_ring(e), buf, 4096)) > 0) {
        if (got < 4096) break;
    }
}

static void run_frames(sb_emulator *e, int n)
{
    for (int i = 0; i < n; i++) { sb_emu_run_frame(e); drain(e); }
}

/* Save → run on → load → save again must reproduce the identical state.
   (No RTC cart, so no wall-clock in the BESS payload.) */
static void test_state_roundtrip(void)
{
    size_t rlen; uint8_t *rom = make_rom(&rlen, 0);
    sb_emulator *e = sb_emu_create(0x002, rom, rlen, NULL, 0);
    assert(e);
    sb_emu_reset(e);
    run_frames(e, 30);

    uint8_t *a = NULL;
    size_t na = sb_emu_save_state(e, &a);
    assert(na > 0 && a);

    run_frames(e, 60);
    uint8_t *b = NULL;
    size_t nb = sb_emu_save_state(e, &b);
    assert(nb == na);
    assert(memcmp(a, b, na) != 0);          /* 60 frames of `inc a` changed state */
    free(b);

    assert(sb_emu_load_state(e, a, na) == 0);
    uint8_t *c = NULL;
    size_t nc = sb_emu_save_state(e, &c);
    assert(nc == na);
    assert(memcmp(a, c, na) == 0);          /* byte-exact restoration */
    free(a); free(c);
    sb_emu_destroy(e);
    free(rom);
}

static void test_rewind(void)
{
    size_t rlen; uint8_t *rom = make_rom(&rlen, 0);
    sb_emulator *e = sb_emu_create(0x002, rom, rlen, NULL, 0);
    assert(e);
    sb_emu_reset(e);
    /* NB: GB_reset itself pushes one rewind entry (gb.c:1795), so pop can
       succeed even before any frame runs — don't assert emptiness here. */
    run_frames(e, 120);
    assert(sb_emu_rewind_pop(e) == 1);      /* history exists (120s default from create) */
    int pops = 1;
    while (sb_emu_rewind_pop(e)) pops++;    /* must terminate */
    assert(pops > 1);
    run_frames(e, 4);
    assert(sb_emu_rewind_pop(e) == 1);      /* refills after exhaustion */
    sb_emu_destroy(e);
    free(rom);
}

static void test_model_switch_and_state_model(void)
{
    size_t rlen; uint8_t *rom = make_rom(&rlen, 0);
    sb_emulator *e = sb_emu_create(0x002, rom, rlen, NULL, 0);  /* DMG-B */
    assert(e);
    sb_emu_reset(e);
    run_frames(e, 5);

    sb_emu_switch_model(e, 0x205);          /* GB_MODEL_CGB_E */
    run_frames(e, 5);
    uint8_t *cgb = NULL;
    size_t ncgb = sb_emu_save_state(e, &cgb);
    assert(ncgb > 0);
    GB_model_t m = 0;
    assert(GB_get_state_model_from_buffer(cgb, ncgb, &m) == 0);
    assert(m == 0x205);
    sb_emu_destroy(e);

    /* auto model-switch on load: CGB state into a DMG emulator */
    e = sb_emu_create(0x002, rom, rlen, NULL, 0);
    assert(e);
    sb_emu_reset(e);
    run_frames(e, 5);
    assert(sb_emu_load_state(e, cgb, ncgb) == 0);
    uint8_t *now = NULL;
    size_t nnow = sb_emu_save_state(e, &now);
    assert(GB_get_state_model_from_buffer(now, nnow, &m) == 0);
    assert(m == 0x205);                     /* emulator followed the state's model */
    free(cgb); free(now);
    sb_emu_destroy(e);
    free(rom);
}

static void test_battery_dirty(void)
{
    size_t rlen; uint8_t *rom = make_rom(&rlen, 1);
    sb_emulator *e = sb_emu_create(0x002, rom, rlen, NULL, 0);
    assert(e);
    sb_emu_reset(e);
    run_frames(e, 30);                      /* program has written 0xA000 by now */
    assert(sb_emu_battery_dirty(e) == 1);
    uint8_t *sav = NULL;
    size_t nsav = sb_emu_save_battery(e, &sav);
    assert(nsav >= 0x2000 && sav && sav[0] == 0x42);
    free(sav);
    sb_emu_clear_battery_dirty(e);
    run_frames(e, 10);                      /* program only writes once */
    assert(sb_emu_battery_dirty(e) == 0);
    sb_emu_destroy(e);
    free(rom);
}

/* audio-drop flag: with drop set and nobody draining, run_frame must not block */
static void test_audio_drop_nonblocking(void)
{
    size_t rlen; uint8_t *rom = make_rom(&rlen, 0);
    sb_emulator *e = sb_emu_create(0x002, rom, rlen, NULL, 0);
    assert(e);
    sb_emu_reset(e);
    static atomic_bool drop;
    atomic_init(&drop, true);
    sb_emu_set_audio_drop(e, &drop);
    for (int i = 0; i < 10; i++) sb_emu_run_frame(e);  /* would deadlock without drop */
    sb_emu_destroy(e);
    free(rom);
}

/* Reference CRC32 (zlib/ISO-HDLC) over an exact buffer — matches GB_get_rom_crc32
   when the ROM is already a power-of-two size (no Core padding). */
static uint32_t ref_crc32(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
    }
    return ~crc;
}

static void test_rom_info(void)
{
    size_t rlen; uint8_t *rom = make_rom(&rlen, 0);   /* exactly 0x8000, power of two */
    /* stamp a known title at 0x134 (printable, < 0x10 chars) */
    const char *want = "TESTROM";
    memset(&rom[0x134], 0, 0x10);
    memcpy(&rom[0x134], want, strlen(want));

    char title[17];
    uint32_t crc = 0;
    assert(sb_rom_info(rom, rlen, title, &crc) == 0);
    assert(strcmp(title, want) == 0);
    assert(crc == ref_crc32(rom, rlen));   /* CRC over the unpadded 32KB buffer */

    /* too-small buffer rejected */
    uint8_t tiny[4] = {1,2,3,4};
    assert(sb_rom_info(tiny, sizeof(tiny), title, &crc) == -1);

    free(rom);
}

static void test_apply_settings(void)
{
    size_t rlen; uint8_t *rom = make_rom(&rlen, 0);
    sb_emulator *e = sb_emu_create(0x002, rom, rlen, NULL, 0);
    assert(e);
    sb_emu_reset(e);
    sb_settings s = {
        .color_correction = 3, .light_temperature = 0.5, .border_mode = 2,
        .highpass = 2, .rtc_mode = 1, .rewind_seconds = 30, .turbo_cap = 2.0,
        .interference = 0.25,
    };
    sb_emu_apply_settings(e, &s);
    run_frames(e, 10);                 /* still runnable, no crash */
    unsigned w = 0, h = 0;
    assert(sb_emu_front_buffer(e, &w, &h) != NULL);
    sb_emu_destroy(e);
    free(rom);
}

/* sb_emu_screen_size must track the live border mode, and the whole 256x224
   output must be produced (no stale/unwritten pixels outside the 160x144 area). */
static void test_screen_size(void)
{
    size_t rlen; uint8_t *rom = make_rom(&rlen, 0);
    sb_emulator *e = sb_emu_create(0x002, rom, rlen, NULL, 0);   /* DMG_B: never HLE-SGB */
    assert(e);
    sb_emu_reset(e);
    assert(sb_emu_screen_size(NULL) == 0);
    assert(sb_emu_screen_size(e) == ((160u << 16) | 144u));      /* pre-frame default */

    sb_settings s = { .rewind_seconds = 120, .turbo_cap = 1.0 };
    s.border_mode = 0;                                           /* GB_BORDER_SGB on a DMG */
    sb_emu_apply_settings(e, &s);
    run_frames(e, 2);
    assert(sb_emu_screen_size(e) == ((160u << 16) | 144u));      /* SGB mode is a no-op off SGB */

    s.border_mode = 2;                                           /* GB_BORDER_ALWAYS */
    sb_emu_apply_settings(e, &s);
    run_frames(e, 2);                                            /* live: no reset needed */
    assert(sb_emu_screen_size(e) == ((256u << 16) | 224u));

    static uint32_t staging[SB_FB_MAX];
    memset(staging, 0, sizeof(staging));
    unsigned w = 0, h = 0;
    sb_emu_copy_front(e, staging, &w, &h);
    assert(w == 256 && h == 224);
    for (unsigned i = 0; i < w * h; i++) assert((staging[i] & 0xFF000000u) == 0xFF000000u);

    /* a save state carries no screen geometry: size survives a roundtrip unchanged */
    uint8_t *st = NULL;
    size_t n = sb_emu_save_state(e, &st);
    assert(n && st);
    assert(sb_emu_load_state(e, st, n) == 0);
    free(st);
    run_frames(e, 2);
    assert(sb_emu_screen_size(e) == ((256u << 16) | 224u));

    s.border_mode = 1;                                           /* GB_BORDER_NEVER */
    sb_emu_apply_settings(e, &s);
    run_frames(e, 2);
    assert(sb_emu_screen_size(e) == ((160u << 16) | 144u));

    sb_emu_destroy(e);
    free(rom);
}

static void test_volume_scale(void)
{
    size_t rlen; uint8_t *rom = make_rom(&rlen, 0);
    sb_emulator *e = sb_emu_create(0x002, rom, rlen, NULL, 0);
    assert(e);
    sb_emu_reset(e);
    static atomic_int vol;
    atomic_init(&vol, 0);              /* silence */
    sb_emu_set_volume_ptr(e, &vol);
    static int16_t buf[4096 * 2];
    int nonzero = 0;
    for (int i = 0; i < 30; i++) {
        sb_emu_run_frame(e);
        size_t got;
        while ((got = sb_ring_pop(sb_emu_audio_ring(e), buf, 4096)) > 0) {
            for (size_t j = 0; j < got * 2; j++) if (buf[j] != 0) nonzero++;
            if (got < 4096) break;
        }
    }
    assert(nonzero == 0);              /* volume 0 => all samples zero */
    sb_emu_destroy(e);
    free(rom);
}

static void test_palette(void)
{
    size_t rlen; uint8_t *rom = make_rom(&rlen, 0);
    sb_emulator *e = sb_emu_create(0x002, rom, rlen, NULL, 0);   /* DMG-B: palette applies */
    assert(e);
    sb_emu_reset(e);
    run_frames(e, 2);
    unsigned w = 0, h = 0;
    const uint32_t *fb = sb_emu_front_buffer(e, &w, &h);
    uint32_t grey_px = fb[0];                       /* default GREY: blank screen = colors[3] = white */

    /* built-in DMG (green) recolors */
    sb_emu_set_palette(e, 1, NULL);
    run_frames(e, 2);
    fb = sb_emu_front_buffer(e, &w, &h);
    assert(fb[0] != grey_px);                        /* recolored */
    /* fb[0] must be one of the DMG palette's encoded shades (which shade the blank
       screen maps to depends on BGP; assert membership, not a fixed index). */
    {
        static const uint8_t dmg[4][3] = {
            {0x08,0x18,0x10}, {0x39,0x61,0x39}, {0x84,0xA5,0x63}, {0xC6,0xDE,0x8C} };
        int match = 0;
        for (int i = 0; i < 4; i++)
            if (fb[0] == (0xFF000000u | ((uint32_t)dmg[i][2] << 16) | ((uint32_t)dmg[i][1] << 8) | dmg[i][0])) match = 1;
        assert(match);
    }

    /* custom all-red shades */
    uint32_t red[4] = { 0xFF0000, 0xFF0000, 0xFF0000, 0xFF0000 };
    sb_emu_set_palette(e, -1, red);
    run_frames(e, 2);
    fb = sb_emu_front_buffer(e, &w, &h);
    assert(fb[0] == (0xFF000000u | (0x00u << 16) | (0x00u << 8) | 0xFFu));  /* pure red */

    sb_emu_destroy(e);
    free(rom);
}

static void test_printer(void)
{
    uint32_t *buf = NULL; unsigned rows = 0;
    uint32_t src[160 * 2];
    for (int i = 0; i < 160 * 2; i++) src[i] = 0xFF123456u;  /* sentinel image pixels */
    sb_printer_append(&buf, &rows, src, 2, 1, 3);            /* top 1, img 2, bottom 3 */
    assert(rows == 6);
    for (int x = 0; x < 160; x++) assert(buf[x] == 0xFFFFFFFFu);           /* top white */
    for (int i = 0; i < 160 * 2; i++) assert(buf[160 + i] == 0xFF123456u); /* image */
    for (int x = 0; x < 160 * 3; x++) assert(buf[160 * 3 + x] == 0xFFFFFFFFu); /* bottom white */
    sb_printer_append(&buf, &rows, src, 2, 0, 0);            /* grows, appends after */
    assert(rows == 8);
    assert(buf[160 * 6] == 0xFF123456u);
    free(buf);
}

static void test_camera(void)
{
    size_t rlen; uint8_t *rom = make_rom(&rlen, 0);
    sb_emulator *e = sb_emu_create(0x002, rom, rlen, NULL, 0);
    assert(e);
    assert(!sb_emu_camera_wanted(e));                 /* nothing requested yet */
    uint8_t frame[SB_CAM_W * SB_CAM_H];
    for (int y = 0; y < SB_CAM_H; y++)
        for (int x = 0; x < SB_CAM_W; x++) frame[y * SB_CAM_W + x] = (uint8_t)(x ^ y);
    sb_emu_camera_deliver(e, frame);                  /* → staging (no crash) */
    sb_camera_promote(e);                             /* staging → sensor (no crash) */
    /* pure clamp helper: exact fetch + out-of-range/negative clamp */
    assert(sb_camera_read(frame, 10, 20) == (uint8_t)(10 ^ 20));
    assert(sb_camera_read(frame, 200, 20) == sb_camera_read(frame, 127, 20)); /* x clamp */
    assert(sb_camera_read(frame, 10, 200) == sb_camera_read(frame, 10, 111)); /* y clamp */
    assert(sb_camera_read(frame, -5, -5) == frame[0]);                        /* neg clamp */
    sb_emu_destroy(e);
    free(rom);
}

static void test_rumble(void)
{
    size_t rlen; uint8_t *rom = make_rom(&rlen, 0);
    sb_emulator *e = sb_emu_create(0x002, rom, rlen, NULL, 0);
    assert(e);
    sb_emu_reset(e);
    assert(sb_emu_rumble_amplitude(e) == 0);          /* nothing yet */
    sb_settings s = {
        .color_correction = 2, .light_temperature = 0.0, .border_mode = 0,
        .highpass = 1, .rtc_mode = 0, .rewind_seconds = 120, .turbo_cap = 0,
        .interference = 0.0, .rumble_mode = 2,          /* GB_RUMBLE_ALL_GAMES */
    };
    sb_emu_apply_settings(e, &s);
    run_frames(e, 10);                                 /* non-rumble ROM: no crash */
    int amp = sb_emu_rumble_amplitude(e);
    assert(amp >= 0 && amp <= 255);
    sb_emu_destroy(e);
    free(rom);
}

/* GameShark + Game Genie codes import; garbage rejected; cheats survive reset, model
   switch and state load (Core keeps them in gb->cheats, only GB_free drops them);
   ROM-only cart has no accelerometer. */
static void test_cheats_and_accel(void)
{
    size_t rlen; uint8_t *rom = make_rom(&rlen, 0);
    sb_emulator *e = sb_emu_create(0x002, rom, rlen, NULL, 0);
    assert(e);
    sb_emu_reset(e);
    assert(sb_emu_cheat_count(e) == 0);
    assert(sb_emu_add_cheat(e, "01FF16D0", "gs", 1) == 1);        /* GameShark */
    assert(sb_emu_add_cheat(e, "ZZZ-ZZZ", NULL, 1) == 0);         /* Game Genie: bad hex */
    assert(sb_emu_add_cheat(e, "hello", "junk", 1) == 0);
    assert(sb_emu_add_cheat(e, "", "empty", 1) == 0);
    assert(sb_emu_cheat_count(e) == 1);
    sb_emu_set_cheats_enabled(e, 1);
    run_frames(e, 3);
    sb_emu_reset(e);
    assert(sb_emu_cheat_count(e) == 1);
    sb_emu_switch_model(e, 0x205);                                /* CGB-E */
    assert(sb_emu_cheat_count(e) == 1);
    uint8_t *st = NULL; size_t n = sb_emu_save_state(e, &st);
    assert(n > 0);
    assert(sb_emu_load_state(e, st, n) == 0);
    free(st);
    assert(sb_emu_cheat_count(e) == 1);
    sb_emu_remove_all_cheats(e);
    assert(sb_emu_cheat_count(e) == 0);
    assert(!sb_emu_has_accelerometer(e));
    sb_emu_set_accelerometer(e, 1.0, 0.0);                         /* harmless on non-MBC7 */
    sb_emu_destroy(e);
    free(rom);
}

int main(void)
{
    size_t rlen; uint8_t *rom = make_rom(&rlen, 0);
    /* GB_MODEL_DMG_B = 0x002 */
    sb_emulator *e = sb_emu_create(0x002, rom, rlen, NULL, 0);
    assert(e != NULL);
    sb_emu_reset(e);

    /* Step frames while draining the audio ring each frame, exactly as the
       real AAudio consumer would. With the ~100 ms runtime ring, NOT draining
       would block the (single-threaded) producer once it fills. */
    size_t total_samples = 0;
    int16_t drain[4096 * 2];
    for (int i = 0; i < 60; i++) {          /* ~1 second */
        sb_emu_run_frame(e);
        size_t got;
        while ((got = sb_ring_pop(sb_emu_audio_ring(e), drain, 4096)) > 0) {
            total_samples += got;
            if (got < 4096) break;          /* ring drained for this frame */
        }
    }

    unsigned w = 0, h = 0;
    const uint32_t *fb = sb_emu_front_buffer(e, &w, &h);
    assert(fb != NULL);
    assert(w == 160 && h == 144);               /* DMG screen, no border */
    /* every pixel must be fully opaque (alpha 0xFF) from our rgb_encode */
    for (unsigned i = 0; i < w * h; i++) assert((fb[i] & 0xFF000000u) == 0xFF000000u);

    /* render-path API: copy_front must agree with front_buffer */
    static uint32_t staging[256 * 224];
    unsigned cw = 0, ch = 0;
    sb_emu_copy_front(e, staging, &cw, &ch);
    assert(cw == w && ch == h);
    for (unsigned i = 0; i < cw * ch; i++) assert((staging[i] & 0xFF000000u) == 0xFF000000u);

    /* running ~1 s must have produced roughly SAMPLE_RATE stereo frames */
    assert(total_samples > 0);

    sb_emu_destroy(e);
    free(rom);
    test_state_roundtrip();
    test_rewind();
    test_model_switch_and_state_model();
    test_battery_dirty();
    test_audio_drop_nonblocking();
    test_rom_info();
    test_apply_settings();
    test_screen_size();
    test_volume_scale();
    test_palette();
    test_rumble();
    test_printer();
    test_camera();
    test_cheats_and_accel();
    printf("emulator: all tests passed\n");
    return 0;
}
