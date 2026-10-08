/* Session-level concurrency test: real emu thread, headless (stub render/audio).
   alarm() is the deadlock net — any hang kills the test with SIGALRM. */
#include "../session.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint8_t *make_rom(size_t *len)   /* MBC1+RAM+BATTERY, one battery write */
{
    size_t n = 32 * 1024;
    uint8_t *rom = calloc(1, n);
    rom[0x100] = 0x00; rom[0x101] = 0xC3; rom[0x102] = 0x50; rom[0x103] = 0x01;
    static const uint8_t prog[] = {
        0x3E, 0x0A, 0xEA, 0x00, 0x00,   /* enable cart RAM */
        0x3E, 0x42, 0xEA, 0x00, 0xA0,   /* one write to 0xA000 */
        0x18, 0xFE,                     /* jr -2 */
    };
    memcpy(&rom[0x150], prog, sizeof(prog));
    rom[0x147] = 0x03; rom[0x148] = 0x00; rom[0x149] = 0x02;
    uint8_t c = 0;
    for (int i = 0x134; i <= 0x14C; i++) c = c - rom[i] - 1;
    rom[0x14D] = c;
    *len = n;
    return rom;
}

int main(void)
{
    alarm(30);
    size_t rlen; uint8_t *rom = make_rom(&rlen);
    sb_session *s = sb_session_create(0x002, rom, rlen, NULL, 0);
    assert(s);

    /* ops on a not-yet-started session must not deadlock */
    uint8_t *st0 = NULL;
    size_t n0 = sb_session_save_state(s, &st0);
    assert(n0 > 0 && st0);
    free(st0);
    sb_session_reset(s);

    sb_session_start(s, NULL);          /* headless: stub render/audio */
    usleep(150 * 1000);

    /* self-parking save mid-run */
    uint8_t *st1 = NULL;
    size_t n1 = sb_session_save_state(s, &st1);
    assert(n1 > 0 && st1);

    /* turbo on/off while running (drop path + flush must not deadlock) */
    sb_session_set_turbo(s, 1);
    usleep(150 * 1000);
    sb_session_set_turbo(s, 0);
    usleep(50 * 1000);

    /* rewind longer than available history → exercises the hold path */
    sb_session_set_rewinding(s, 1);
    usleep(400 * 1000);
    sb_session_set_rewinding(s, 0);
    usleep(50 * 1000);

    /* load earlier state mid-run */
    assert(sb_session_load_state(s, st1, n1) == 0);
    free(st1);

    /* model switch mid-run */
    sb_session_switch_model(s, 0x205);
    usleep(50 * 1000);

    /* battery snapshot: emu thread publishes it; poll until set */
    int saw_dirty = 0;
    for (int i = 0; i < 200; i++) {
        if (sb_session_battery_dirty(s)) { saw_dirty = 1; break; }
        usleep(20 * 1000);
    }
    assert(saw_dirty);
    sb_session_pause(s, 1);             /* poller sequence: pause → save → clear → unpause */
    uint8_t *sav = NULL;
    size_t nsav = sb_session_save_battery(s, &sav);
    assert(nsav >= 0x2000 && sav && sav[0] == 0x42);
    free(sav);
    sb_session_clear_battery_dirty(s);
    sb_session_pause(s, 0);
    usleep(100 * 1000);
    assert(!sb_session_battery_dirty(s));   /* program wrote only once */

    /* frame copy while running */
    static uint32_t fb[SB_FB_MAX];
    unsigned w = 0, h = 0;
    sb_session_copy_frame(s, fb, &w, &h);
    assert(w >= 160 && h >= 144);
    /* the JNI's geometry query must agree with the frame it describes */
    assert(sb_session_screen_size(s) == ((w << 16) | h));
    assert(sb_session_screen_size(NULL) == 0);

    /* pause/unpause storm interleaved with control flags */
    for (int i = 0; i < 20; i++) {
        sb_session_pause(s, 1);
        sb_session_pause(s, 0);
        sb_session_set_turbo(s, i & 1);
        sb_session_set_rewinding(s, (i >> 1) & 1);
    }
    sb_session_set_turbo(s, 0);
    sb_session_set_rewinding(s, 0);

    /* apply settings mid-run (self-park) + volume set from the control thread */
    sb_settings cfg = {
        .color_correction = 2, .light_temperature = 0.0, .border_mode = 0,
        .highpass = 1, .rtc_mode = 0, .rewind_seconds = 60, .turbo_cap = 0,
        .interference = 0.0,
    };
    sb_session_apply_settings(s, &cfg);
    sb_session_set_volume(s, 128);
    usleep(50 * 1000);
    sb_session_set_volume(s, 256);

    /* set palette mid-run (self-park): a built-in then a custom */
    sb_session_set_palette(s, 2, NULL);          /* MGB */
    usleep(30 * 1000);
    uint32_t pal[4] = { 0x0000FF, 0x0000AA, 0x000055, 0x000000 };
    sb_session_set_palette(s, -1, pal);          /* custom blue */
    usleep(30 * 1000);

    /* rumble amplitude readable mid-run (0..255) */
    int ramp = sb_session_rumble_amplitude(s);
    assert(ramp >= 0 && ramp <= 255);

    /* --- printer connect while running self-parks, no deadlock (M7) --- */
    sb_session_connect_printer(s);
    unsigned g0 = sb_session_printer_generation(s);
    (void)g0;
    sb_session_disconnect_printer(s);
    /* camera passthrough: wanted starts false, deliver doesn't crash */
    assert(!sb_session_camera_wanted(s));
    uint8_t gray[128 * 112];
    memset(gray, 0x80, sizeof(gray));
    sb_session_camera_deliver(s, gray);

    /* cheats add/enable/remove-all while running self-park, no deadlock; accel direct */
    assert(sb_session_add_cheat(s, "01FF16D0", "gs", 1) == 1);
    assert(sb_session_add_cheat(s, "nope", "bad", 1) == 0);
    sb_session_set_cheats_enabled(s, 1);
    usleep(30 * 1000);
    sb_session_set_cheats_enabled(s, 0);
    sb_session_remove_all_cheats(s);
    assert(!sb_session_has_accelerometer(s));    /* MBC1 cart */
    sb_session_set_accelerometer(s, 0.5, -0.5);   /* any-thread, no park */
    assert(sb_session_add_cheat(NULL, "01FF16D0", "gs", 1) == 0);
    assert(!sb_session_has_accelerometer(NULL));

    sb_session_stop(s);
    sb_session_destroy(s);
    free(rom);
    printf("session: all tests passed\n");
    return 0;
}
