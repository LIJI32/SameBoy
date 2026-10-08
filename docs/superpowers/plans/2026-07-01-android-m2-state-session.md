# SameBoy Android M2 — State & Session Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** In-game menu with save-state slots (thumbnails), reset, model select, exit; turbo + rewind hold-buttons; periodic battery flush — per `specs/2026-07-01-android-m2-state-session.md`.

**Architecture:** Session-level C11 atomics consumed by the emu thread each loop iteration (turbo/rewind/battery-dirty snapshot); blocking ops (state I/O, model switch, reset, battery clear) self-park via M1's synchronous pause. Java owns menu UI, slot files, thumbnails, and a 2 s battery poller.

**Tech Stack:** C11 (`stdatomic.h`), pthreads, JNI, Java (programmatic UI — no XML resources, matching M1), Gradle + ndk-build.

## Global Constraints

- **Never modify `Core/`** — all Core interaction through existing public APIs.
- Build: `cd ~/SameBoy/Android && JAVA_HOME=$HOME/Android/jdk17 ANDROID_HOME=$HOME/Android ./gradlew :app:assembleDebug`.
- Host tests: `Android/jni/test/run_host_tests.sh` (must stay green end-to-end).
- Turbo = `GB_set_turbo_mode(on, false)` — frame skip ENABLED (2nd arg is `no_frame_skip`; iOS `GBViewController.m:2183` passes false). Never `(on, on)`.
- Rewind loop shape: **pop 2 → run 1 frame**; on pop failure (history empty) **hold** (sleep ~16 ms, skip run) until the button releases. Never resume forward play while held.
- Turbo audio: drop-on-full via `sb_ring_try_push` + one `sb_ring_flush` at turbo-ON. Plain `sb_ring_push` (blocking) whenever turbo is off.
- `GB_get_battery_dirty` may only be called on the emu thread or while it is parked.
- State file layout: `states/<rom>.s<N>` + `states/<rom>.s<N>.png` (N = 0..3), mirroring iOS `GBROMManager stateFile:`.
- Rewind length: 120 s, set in `sb_emu_create` (SDL default, `SDL/configuration.c:45`).
- Java UI stays programmatic (no XML layouts/resources) and Java 8-compatible (M1 convention).
- Commit after every task; message prefix `feat(android):` / `fix(android):` / `test(android):`.

---

### Task 1: `sb_ring_try_push`

**Files:**
- Modify: `Android/jni/ring_buffer.h` (after `sb_ring_push` decl, line 12)
- Modify: `Android/jni/ring_buffer.c` (after `sb_ring_push`, line 50)
- Test: `Android/jni/test/test_ring_buffer.c`

**Interfaces:**
- Produces: `int sb_ring_try_push(sb_ring *r, int16_t left, int16_t right);` — returns 1 = pushed, 0 = dropped (ring full or shut down). Never blocks. Task 2's `audio_cb` and Task 3's turbo path rely on this exact signature.

- [ ] **Step 1: Write the failing test** — append to `test_ring_buffer.c` before `main`, and call it from `main` after the existing tests:

```c
static void test_try_push_drop_on_full(void)
{
    sb_ring *r = sb_ring_create(2);
    assert(sb_ring_try_push(r, 1, -1) == 1);
    assert(sb_ring_try_push(r, 2, -2) == 1);
    assert(sb_ring_try_push(r, 3, -3) == 0);   /* full: dropped, must not block */
    int16_t out[2];
    assert(sb_ring_pop(r, out, 1) == 1);
    assert(out[0] == 1 && out[1] == -1);       /* FIFO intact, drop lost only the new frame */
    assert(sb_ring_try_push(r, 4, -4) == 1);   /* space again */
    assert(sb_ring_pop(r, out, 1) == 1 && out[0] == 2);
    assert(sb_ring_pop(r, out, 1) == 1 && out[0] == 4);
    sb_ring_destroy(r);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `Android/jni/test/run_host_tests.sh`
Expected: FAIL at compile — `implicit declaration of function 'sb_ring_try_push'`.

- [ ] **Step 3: Implement** — `ring_buffer.h` after line 12:

```c
/* Non-blocking push: returns 1 if stored, 0 if dropped (full or shut down). */
int      sb_ring_try_push(sb_ring *r, int16_t left, int16_t right);
```

`ring_buffer.c` after `sb_ring_push`:

```c
int sb_ring_try_push(sb_ring *r, int16_t left, int16_t right)
{
    pthread_mutex_lock(&r->mtx);
    if (r->count == r->cap || r->shutdown) {
        pthread_mutex_unlock(&r->mtx);
        return 0;
    }
    r->buf[r->head * 2]     = left;
    r->buf[r->head * 2 + 1] = right;
    r->head = (r->head + 1) % r->cap;
    r->count++;
    pthread_mutex_unlock(&r->mtx);
    return 1;
}
```

- [ ] **Step 4: Run tests** — `Android/jni/test/run_host_tests.sh` → `ALL HOST TESTS PASSED`.

- [ ] **Step 5: Commit**

```bash
git add Android/jni/ring_buffer.h Android/jni/ring_buffer.c Android/jni/test/test_ring_buffer.c
git commit -m "feat(android): non-blocking sb_ring_try_push for turbo audio drop path"
```

---

### Task 2: Emulator primitives — state, model, rewind, turbo, battery, audio-drop

**Files:**
- Modify: `Android/jni/emulator.h`, `Android/jni/emulator.c`
- Test: `Android/jni/test/test_emulator.c`

**Interfaces:**
- Consumes: `sb_ring_try_push` (Task 1).
- Produces (Task 3 + JNI depend on these exact signatures):

```c
void   sb_emu_set_audio_drop(sb_emulator *e, const atomic_bool *drop_on_full); /* NULL = always block */
size_t sb_emu_save_state(sb_emulator *e, uint8_t **out_malloced);       /* 0 on failure; caller frees */
int    sb_emu_load_state(sb_emulator *e, const uint8_t *buf, size_t n); /* 0 = ok; auto model-switch */
void   sb_emu_switch_model(sb_emulator *e, int model);                  /* GB_model_t value */
void   sb_emu_set_rewind_length(sb_emulator *e, double seconds);
void   sb_emu_set_turbo(sb_emulator *e, int on);                        /* GB_set_turbo_mode(on, false) */
int    sb_emu_rewind_pop(sb_emulator *e);                               /* 1 = popped, 0 = history empty */
int    sb_emu_battery_dirty(sb_emulator *e);                            /* emu thread / parked only */
void   sb_emu_clear_battery_dirty(sb_emulator *e);
```

- All are single-line-ish wrappers; **none** take locks — thread discipline is the caller's (session) job, matching M1's split.

- [ ] **Step 1: Write the failing tests.** `test_emulator.c`: extend `make_rom` to take a battery flag, add four test functions, call them from `main`. Full new content of the additions:

At top, after includes: `#include <string.h>` and `#include <Core/gb.h>` (for `GB_get_state_model_from_buffer` and model enum values in assertions; CFLAGS already define `GB_INTERNAL`).

Replace `make_rom` with:

```c
/* Build a tiny valid ROM. battery=0: ROM-only, busy loop `inc a`.
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
        rom[0x150] = 0x3C; rom[0x151] = 0x18; rom[0x152] = 0xFE; /* inc a; jr -2 */
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
```

(The existing `main` body keeps working — change its `make_rom(&rlen)` call to `make_rom(&rlen, 0)`.)

New test functions:

```c
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
```

Call all five from `main` (before the final `printf`):

```c
    test_state_roundtrip();
    test_rewind();
    test_model_switch_and_state_model();
    test_battery_dirty();
    test_audio_drop_nonblocking();
```

- [ ] **Step 2: Run to verify failure** — `Android/jni/test/run_host_tests.sh` → compile error: `sb_emu_save_state` undeclared.

- [ ] **Step 3: Implement.** `emulator.h`: add `#include <stdatomic.h>` after the existing includes, and the nine declarations from the Interfaces block above (before `SB_AUDIO_SAMPLE_RATE`).

`emulator.c`:

1. Struct: add member `const atomic_bool *audio_drop;` to `struct sb_emulator`.
2. Replace `audio_cb`:

```c
static void audio_cb(GB_gameboy_t *gb, GB_sample_t *sample)
{
    sb_emulator *e = GB_get_user_data(gb);
    if (e->audio_drop && atomic_load_explicit(e->audio_drop, memory_order_relaxed)) {
        sb_ring_try_push(e->audio, sample->left, sample->right);
    }
    else {
        sb_ring_push(e->audio, sample->left, sample->right);
    }
}
```

3. In `sb_emu_create`, after the ROM is loaded (immediately before the final `return e;`): `GB_set_rewind_length(&e->gb, 120);`
4. New functions (bottom of file, before `sb_emu_destroy`):

```c
void sb_emu_set_audio_drop(sb_emulator *e, const atomic_bool *drop_on_full)
{
    e->audio_drop = drop_on_full;
}

size_t sb_emu_save_state(sb_emulator *e, uint8_t **out)
{
    size_t n = GB_get_save_state_size(&e->gb);
    uint8_t *buf = malloc(n);
    if (!buf) return 0;
    GB_save_state_to_buffer(&e->gb, buf);
    *out = buf;
    return n;
}

int sb_emu_load_state(sb_emulator *e, const uint8_t *buf, size_t n)
{
    GB_model_t model = 0;
    if (GB_get_state_model_from_buffer(buf, n, &model) == 0 &&
        GB_get_model(&e->gb) != model) {
        GB_switch_model_and_reset(&e->gb, model);   /* mirrors iOS loadStateFromFile: */
    }
    return GB_load_state_from_buffer(&e->gb, buf, n);
}

void sb_emu_switch_model(sb_emulator *e, int model)
{
    GB_switch_model_and_reset(&e->gb, (GB_model_t)model);
}

void sb_emu_set_rewind_length(sb_emulator *e, double seconds)
{
    GB_set_rewind_length(&e->gb, seconds);
}

void sb_emu_set_turbo(sb_emulator *e, int on)
{
    GB_set_turbo_mode(&e->gb, on != 0, false);   /* frame skip ON in turbo */
}

int sb_emu_rewind_pop(sb_emulator *e)
{
    return GB_rewind_pop(&e->gb) ? 1 : 0;
}

int sb_emu_battery_dirty(sb_emulator *e)
{
    return GB_get_battery_dirty(&e->gb) ? 1 : 0;
}

void sb_emu_clear_battery_dirty(sb_emulator *e)
{
    GB_clear_battery_dirty(&e->gb);
}
```

- [ ] **Step 4: Run tests** — `Android/jni/test/run_host_tests.sh` → `ALL HOST TESTS PASSED`.

- [ ] **Step 5: Commit**

```bash
git add Android/jni/emulator.h Android/jni/emulator.c Android/jni/test/test_emulator.c
git commit -m "feat(android): emulator state/model/rewind/turbo/battery primitives + audio drop path"
```

---

### Task 3: Session — atomics, loop consumption, self-parking ops, concurrency test

**Files:**
- Modify: `Android/jni/session.h`, `Android/jni/session.c`
- Create: `Android/jni/test/shim/android/native_window.h`, `Android/jni/test/shim/android/log.h`, `Android/jni/test/shim_stubs.c`, `Android/jni/test/test_session.c`
- Modify: `Android/jni/test/run_host_tests.sh`

**Interfaces:**
- Consumes: all Task 2 `sb_emu_*` primitives; `sb_ring_flush` (existing).
- Produces (JNI in Task 4 binds exactly these):

```c
void   sb_session_set_turbo(sb_session *s, int on);
void   sb_session_set_rewinding(sb_session *s, int on);
int    sb_session_battery_dirty(sb_session *s);          /* atomic snapshot, any thread */
void   sb_session_clear_battery_dirty(sb_session *s);    /* self-parks */
size_t sb_session_save_state(sb_session *s, uint8_t **out);              /* self-parks */
int    sb_session_load_state(sb_session *s, const uint8_t *buf, size_t n); /* self-parks; 0 = ok */
void   sb_session_switch_model(sb_session *s, int model);                /* self-parks */
void   sb_session_copy_frame(sb_session *s, uint32_t *dst, unsigned *w, unsigned *h);
/* sb_session_reset and sb_session_save_battery become self-parking */
```

- Control-thread discipline: all `sb_session_*` control calls (pause/reset/state/model/turbo/rewind/battery) come from ONE thread (the JNI/UI thread) — document in `session.h` header comment. `battery_dirty` is safe from any thread.

- [ ] **Step 1: Create host shims.**

`Android/jni/test/shim/android/native_window.h`:

```c
#pragma once
/* Host-test shim for <android/native_window.h> */
typedef struct ANativeWindow ANativeWindow;
void ANativeWindow_release(ANativeWindow *win);
```

`Android/jni/test/shim/android/log.h`:

```c
#pragma once
/* Host-test shim for <android/log.h> */
#define ANDROID_LOG_WARN 5
static inline int __android_log_print(int prio, const char *tag, const char *fmt, ...)
{
    (void)prio; (void)tag; (void)fmt;
    return 0;
}
```

`Android/jni/test/shim_stubs.c`:

```c
/* Host-test stubs for the Android-only pieces session.c links against. */
#include "../render_gles.h"
#include "../audio_aaudio.h"

void ANativeWindow_release(ANativeWindow *win) { (void)win; }

sb_renderer *sb_render_start(ANativeWindow *win, sb_emulator *emu)
{ (void)win; (void)emu; return (sb_renderer *)0; }
void sb_render_stop(sb_renderer *r) { (void)r; }

sb_audio *sb_audio_start(sb_ring *ring) { (void)ring; return (sb_audio *)0; }
void sb_audio_set_paused(sb_audio *a, int paused) { (void)a; (void)paused; }
void sb_audio_stop(sb_audio *a) { (void)a; }
```

- [ ] **Step 2: Write the failing concurrency test.** `Android/jni/test/test_session.c`:

```c
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

    /* pause/unpause storm interleaved with control flags */
    for (int i = 0; i < 20; i++) {
        sb_session_pause(s, 1);
        sb_session_pause(s, 0);
        sb_session_set_turbo(s, i & 1);
        sb_session_set_rewinding(s, (i >> 1) & 1);
    }
    sb_session_set_turbo(s, 0);
    sb_session_set_rewinding(s, 0);

    sb_session_stop(s);
    sb_session_destroy(s);
    free(rom);
    printf("session: all tests passed\n");
    return 0;
}
```

Add to `run_host_tests.sh` before the final `echo`:

```bash
echo "== session =="
eval cc -Itest/shim $CFLAGS test/test_session.c session.c emulator.c ring_buffer.c test/shim_stubs.c $CORE_SRC -lpthread -lm -o /tmp/sb_tses
/tmp/sb_tses
```

- [ ] **Step 3: Run to verify failure** — `run_host_tests.sh` → compile error (`sb_session_set_turbo` undeclared).

- [ ] **Step 4: Implement.** `session.h` — add after the existing declarations (and extend the header comment: "Control calls must come from a single thread; only sb_session_battery_dirty is any-thread safe."):

```c
void   sb_session_set_turbo(sb_session *s, int on);
void   sb_session_set_rewinding(sb_session *s, int on);
int    sb_session_battery_dirty(sb_session *s);
void   sb_session_clear_battery_dirty(sb_session *s);
size_t sb_session_save_state(sb_session *s, uint8_t **out);
int    sb_session_load_state(sb_session *s, const uint8_t *buf, size_t n);
void   sb_session_switch_model(sb_session *s, int model);
void   sb_session_copy_frame(sb_session *s, uint32_t *dst, unsigned *w, unsigned *h);
```

`session.c` — full set of changes:

1. `#include <stdatomic.h>` at the top.
2. Struct: replace `volatile int running; volatile int paused;` with:

```c
    atomic_bool running;
    int paused;                 /* protected by pause_mtx */
    atomic_bool turbo;          /* consumed by emu loop */
    atomic_bool rewinding;      /* consumed by emu loop */
    atomic_bool audio_drop;     /* read by audio_cb via sb_emu_set_audio_drop */
    atomic_bool battery_dirty;  /* published by emu loop, read by JNI */
```

3. `sb_session_create`: after `s->emu = emu;` add:

```c
    atomic_init(&s->running, false);
    atomic_init(&s->turbo, false);
    atomic_init(&s->rewinding, false);
    atomic_init(&s->audio_drop, false);
    atomic_init(&s->battery_dirty, false);
    sb_emu_set_audio_drop(emu, &s->audio_drop);
```

4. `emu_loop` — new body:

```c
static void *emu_loop(void *arg)
{
    sb_session *s = arg;
    int applied_turbo = 0;
    while (atomic_load(&s->running)) {
        pthread_mutex_lock(&s->pause_mtx);
        while (s->paused && atomic_load(&s->running)) {
            s->parked = 1;
            pthread_cond_broadcast(&s->parked_cv);
            pthread_cond_wait(&s->pause_cv, &s->pause_mtx);
        }
        s->parked = 0;
        pthread_mutex_unlock(&s->pause_mtx);
        if (!atomic_load(&s->running)) break;

        int turbo = atomic_load(&s->turbo) ? 1 : 0;
        if (turbo != applied_turbo) {
            sb_emu_set_turbo(s->emu, turbo);
            applied_turbo = turbo;
        }

        if (atomic_load(&s->rewinding)) {
            /* pop 2 → run 1: net one frame backwards per iteration (iOS shape).
               On empty history, hold position — never creep forward under the
               user's finger. */
            sb_emu_rewind_pop(s->emu);
            if (!sb_emu_rewind_pop(s->emu)) {
                struct timespec ts = {0, 16600000};
                nanosleep(&ts, NULL);
                continue;
            }
        }

        sb_emu_run_frame(s->emu);   /* blocks on the audio ring => paced */
        atomic_store(&s->battery_dirty, sb_emu_battery_dirty(s->emu) != 0);
        if (!s->audio) {
            sb_ring_flush(sb_emu_audio_ring(s->emu));
            struct timespec ts = {0, 16600000};
            nanosleep(&ts, NULL);
        }
    }
    return NULL;
}
```

5. All existing reads/writes of `s->running` switch to `atomic_load`/`atomic_store` (in `sb_session_start`, `sb_session_stop`, `sb_session_pause`'s parked-wait loop).
6. Self-park helpers + ops (place above `sb_session_reset`):

```c
/* Park the emu thread for a blocking op; restore the previous pause state after.
   Nested-safe with an outer explicit pause (menu open): if already paused, the
   op runs parked and park_end leaves it paused. */
static int park_begin(sb_session *s)
{
    pthread_mutex_lock(&s->pause_mtx);
    int was = s->paused;
    pthread_mutex_unlock(&s->pause_mtx);
    if (!was) sb_session_pause(s, 1);
    return was;
}

static void park_end(sb_session *s, int was_paused)
{
    if (!was_paused) sb_session_pause(s, 0);
}
```

Replace `sb_session_reset` and `sb_session_save_battery`, and add the new ops:

```c
void sb_session_reset(sb_session *s)
{
    if (!s) return;
    int was = park_begin(s);
    sb_emu_reset(s->emu);
    park_end(s, was);
}

size_t sb_session_save_battery(sb_session *s, uint8_t **out)
{
    if (!s) return 0;
    int was = park_begin(s);
    size_t n = sb_emu_save_battery(s->emu, out);
    park_end(s, was);
    return n;
}

void sb_session_set_turbo(sb_session *s, int on)
{
    if (!s) return;
    if (on) {
        atomic_store(&s->audio_drop, true);
        sb_ring_flush(sb_emu_audio_ring(s->emu));   /* unblock producer + drop latency (SDL clears its queue too) */
        atomic_store(&s->turbo, true);
    }
    else {
        atomic_store(&s->turbo, false);
        atomic_store(&s->audio_drop, false);
    }
}

void sb_session_set_rewinding(sb_session *s, int on)
{
    if (!s) return;
    atomic_store(&s->rewinding, on != 0);
}

int sb_session_battery_dirty(sb_session *s)
{
    return s ? (atomic_load(&s->battery_dirty) ? 1 : 0) : 0;
}

void sb_session_clear_battery_dirty(sb_session *s)
{
    if (!s) return;
    int was = park_begin(s);
    sb_emu_clear_battery_dirty(s->emu);
    atomic_store(&s->battery_dirty, false);
    park_end(s, was);
}

size_t sb_session_save_state(sb_session *s, uint8_t **out)
{
    if (!s) return 0;
    int was = park_begin(s);
    size_t n = sb_emu_save_state(s->emu, out);
    park_end(s, was);
    return n;
}

int sb_session_load_state(sb_session *s, const uint8_t *buf, size_t n)
{
    if (!s) return -1;
    int was = park_begin(s);
    int ret = sb_emu_load_state(s->emu, buf, n);
    park_end(s, was);
    return ret;
}

void sb_session_switch_model(sb_session *s, int model)
{
    if (!s) return;
    int was = park_begin(s);
    sb_emu_switch_model(s->emu, model);
    park_end(s, was);
}

void sb_session_copy_frame(sb_session *s, uint32_t *dst, unsigned *w, unsigned *h)
{
    if (!s) { *w = *h = 0; return; }
    sb_emu_copy_front(s->emu, dst, w, h);   /* fb_mtx-protected copy */
}
```

Note: `sb_session_pause(s, 1)` on a **stopped** session sets the flag and returns immediately (the parked-wait loop exits because `running` is false) — so self-parking ops are safe before `start()` and after `stop()`.

- [ ] **Step 5: Run tests** — `run_host_tests.sh` → all three suites pass.

- [ ] **Step 6: Commit**

```bash
git add Android/jni/session.h Android/jni/session.c Android/jni/test/shim Android/jni/test/shim_stubs.c Android/jni/test/test_session.c Android/jni/test/run_host_tests.sh
git commit -m "feat(android): session turbo/rewind/battery atomics + self-parking state ops + concurrency test"
```

---

### Task 4: JNI entry points + NativeBridge

**Files:**
- Modify: `Android/jni/sameboy_jni.c`, `Android/app/src/main/java/io/sameboy/android/NativeBridge.java`

**Interfaces:**
- Consumes: Task 3 session API.
- Produces (Java callers in Tasks 7–8 use exactly these):

```java
public static native byte[]  nativeSaveState(long ctx);
public static native boolean nativeLoadState(long ctx, byte[] state);
public static native void    nativeSetTurbo(long ctx, boolean on);
public static native void    nativeSetRewinding(long ctx, boolean on);
public static native void    nativeSwitchModel(long ctx, int model);
public static native boolean nativeIsBatteryDirty(long ctx);
public static native void    nativeClearBatteryDirty(long ctx);
/** [0]=width, [1]=height, then width*height ABGR pixels. null on failure. */
public static native int[]   nativeCopyFrame(long ctx);
```

- [ ] **Step 1: Add the Java declarations** to `NativeBridge.java` (after `nativeSaveBattery`), exactly as above.

- [ ] **Step 2: Add the C implementations** to `sameboy_jni.c` (after `nativeSaveBattery`):

```c
JNIEXPORT jbyteArray JNICALL
Java_io_sameboy_android_NativeBridge_nativeSaveState(JNIEnv *env, jclass c, jlong ctx)
{
    (void)c;
    uint8_t *buf = NULL;
    size_t n = sb_session_save_state((sb_session *)(uintptr_t)ctx, &buf);
    if (n == 0) return NULL;
    jbyteArray arr = (*env)->NewByteArray(env, (jsize)n);
    if (arr) (*env)->SetByteArrayRegion(env, arr, 0, (jsize)n, (const jbyte *)buf);
    free(buf);
    return arr;
}

JNIEXPORT jboolean JNICALL
Java_io_sameboy_android_NativeBridge_nativeLoadState(JNIEnv *env, jclass c, jlong ctx, jbyteArray data)
{
    (void)c;
    if (!data) return JNI_FALSE;
    jsize n = (*env)->GetArrayLength(env, data);
    jbyte *bytes = (*env)->GetByteArrayElements(env, data, NULL);
    if (!bytes) return JNI_FALSE;
    int ret = sb_session_load_state((sb_session *)(uintptr_t)ctx, (const uint8_t *)bytes, (size_t)n);
    (*env)->ReleaseByteArrayElements(env, data, bytes, JNI_ABORT);
    return ret == 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeSetTurbo(JNIEnv *env, jclass c, jlong ctx, jboolean on)
{ (void)env; (void)c; sb_session_set_turbo((sb_session *)(uintptr_t)ctx, on); }

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeSetRewinding(JNIEnv *env, jclass c, jlong ctx, jboolean on)
{ (void)env; (void)c; sb_session_set_rewinding((sb_session *)(uintptr_t)ctx, on); }

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeSwitchModel(JNIEnv *env, jclass c, jlong ctx, jint model)
{ (void)env; (void)c; sb_session_switch_model((sb_session *)(uintptr_t)ctx, model); }

JNIEXPORT jboolean JNICALL
Java_io_sameboy_android_NativeBridge_nativeIsBatteryDirty(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; return sb_session_battery_dirty((sb_session *)(uintptr_t)ctx) ? JNI_TRUE : JNI_FALSE; }

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeClearBatteryDirty(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; sb_session_clear_battery_dirty((sb_session *)(uintptr_t)ctx); }

JNIEXPORT jintArray JNICALL
Java_io_sameboy_android_NativeBridge_nativeCopyFrame(JNIEnv *env, jclass c, jlong ctx)
{
    (void)c;
    sb_session *s = (sb_session *)(uintptr_t)ctx;
    if (!s) return NULL;
    uint32_t *px = malloc(SB_FB_MAX * sizeof(uint32_t));
    if (!px) return NULL;
    unsigned w = 0, h = 0;
    sb_session_copy_frame(s, px, &w, &h);
    if (w == 0 || h == 0) { free(px); return NULL; }
    jintArray arr = (*env)->NewIntArray(env, (jsize)(2 + w * h));
    if (arr) {
        jint header[2] = { (jint)w, (jint)h };
        (*env)->SetIntArrayRegion(env, arr, 0, 2, header);
        (*env)->SetIntArrayRegion(env, arr, 2, (jsize)(w * h), (const jint *)px);
    }
    free(px);
    return arr;
}
```

- [ ] **Step 3: Build**

Run: `cd ~/SameBoy/Android && JAVA_HOME=$HOME/Android/jdk17 ANDROID_HOME=$HOME/Android ./gradlew :app:assembleDebug`
Expected: `BUILD SUCCESSFUL`. Then verify symbols:

```bash
D=app/build/intermediates/merged_native_libs/debug/*/lib/arm64-v8a
nm -D --defined-only $D/libsameboy_core.so | grep -c NativeBridge_native
```
Expected: `16` (8 M1 + 8 new).

- [ ] **Step 4: Commit**

```bash
git add Android/jni/sameboy_jni.c Android/app/src/main/java/io/sameboy/android/NativeBridge.java
git commit -m "feat(android): JNI surface for state, turbo, rewind, model, battery-dirty, frame copy"
```

---

### Task 5: SaveStore — base-dir fallback fix + state file helpers

**Files:**
- Modify: `Android/app/src/main/java/io/sameboy/android/SaveStore.java`

**Interfaces:**
- Produces: `SaveStore.stateFile(Context, String rom, int slot)` → `states/<rom>.s<slot>`; `SaveStore.stateThumb(Context, String rom, int slot)` → same + `.png`. Existing `savFile`/`read`/`write` keep their signatures.

- [ ] **Step 1: Rewrite `SaveStore.java`:**

```java
package io.sameboy.android;

import android.content.Context;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;

final class SaveStore {
    private SaveStore() {}

    /** External app storage when mounted, else internal — never null (fixes M1 NPE). */
    private static File subDir(Context ctx, String name) {
        File base = ctx.getExternalFilesDir(null);
        if (base == null) base = ctx.getFilesDir();
        File dir = new File(base, name);
        if (!dir.exists()) dir.mkdirs();
        return dir;
    }

    static File savFile(Context ctx, String romName) {
        return new File(subDir(ctx, "saves"), romName + ".sav");
    }

    /** Save-state slot file: states/<rom>.s<slot> (mirrors iOS GBROMManager). */
    static File stateFile(Context ctx, String romName, int slot) {
        return new File(subDir(ctx, "states"), romName + ".s" + slot);
    }

    /** Slot thumbnail PNG, sibling of the state file. */
    static File stateThumb(Context ctx, String romName, int slot) {
        return new File(subDir(ctx, "states"), romName + ".s" + slot + ".png");
    }

    static byte[] read(File f) {
        if (!f.exists()) return null;
        try (RandomAccessFile raf = new RandomAccessFile(f, "r")) {
            byte[] b = new byte[(int) raf.length()];
            raf.readFully(b);
            return b;
        } catch (IOException e) { return null; }
    }

    static void write(File f, byte[] data) {
        if (data == null) return;
        try (FileOutputStream out = new FileOutputStream(f)) { out.write(data); }
        catch (IOException e) { android.util.Log.e("SameBoy", "write failed: " + f, e); }
    }
}
```

- [ ] **Step 2: Build** — `./gradlew :app:assembleDebug` → `BUILD SUCCESSFUL`.

- [ ] **Step 3: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/SaveStore.java
git commit -m "fix(android): SaveStore internal-storage fallback (M1 NPE) + state slot file helpers"
```

---

### Task 6: TouchOverlayView — rewind/turbo hold buttons + menu button

**Files:**
- Modify: `Android/app/src/main/java/io/sameboy/android/TouchOverlayView.java`

**Interfaces:**
- Produces: constructor takes a widened listener; pseudo-key constants used by Task 8:

```java
interface ControlListener {
    void onKey(int gbKeyIndex, boolean pressed);      // 0..7, unchanged semantics
    void onSpecial(int what, boolean pressed);        // SPECIAL_* below
}
static final int SPECIAL_REWIND = 8, SPECIAL_TURBO = 9, SPECIAL_MENU = 10;
```

- Consumes: nothing new. All existing hit-testing/refcount machinery is reused; the pseudo-keys ride the same per-pointer maps.

- [ ] **Step 1: Implement.**
  1. Replace `interface KeyListener` with `ControlListener` above; rename the field; update the constructor.
  2. Widen `keyCount` to `new int[11]`.
  3. Add region fields `RectF rewind, turbo, menu;` and in `onSizeChanged` (after the existing regions):

```java
        rewind = new RectF(dpadCx - u, dpadCy - u*3.4f, dpadCx + u, dpadCy - u*2.4f);
        turbo  = new RectF(w - u*2.9f, dpadCy - u*3.0f, w - u*0.9f, dpadCy - u*2.0f);
        menu   = new RectF(w - u*1.5f, u*0.4f, w - u*0.4f, u*1.5f);
```

  4. Extend `keyAt` — insert before the final `return -1;`:

```java
        if (rewind.contains(x, y)) return SPECIAL_REWIND;
        if (turbo.contains(x, y)) return SPECIAL_TURBO;
        if (menu.contains(x, y)) return SPECIAL_MENU;
```
  5. **Menu is one-shot, not refcounted.** The opened dialog steals the rest of the
     touch stream, so the overlay may never see the matching ACTION_UP — a
     refcounted MENU key would stick at count 1 and never fire again. At the top
     of `press()`:

```java
        if (k == SPECIAL_MENU) {                 // one-shot: fire on touch, don't track
            listener.onSpecial(SPECIAL_MENU, true);
            return;
        }
```

  6. In the press/release delivery points, route by index:

```java
        if (k < 8) listener.onKey(k, pressed);
        else listener.onSpecial(k, pressed);
```

  (Wherever M1 called `listener.onKey(...)` — press, releasePointer, releaseAll. The refcount machinery itself is index-agnostic once the array is widened; REWIND/TURBO ride it unchanged, giving clean hold semantics.)
  7. In `onDraw`, append after the existing draws (text paint style matching the M1 aesthetic):

```java
        paint.setColor(Color.argb(110, 180, 180, 180));
        if (rewind != null) c.drawRoundRect(rewind, 12, 12, paint);
        if (turbo != null) c.drawRoundRect(turbo, 12, 12, paint);
        if (menu != null) c.drawRoundRect(menu, 12, 12, paint);
        paint.setColor(Color.argb(200, 255, 255, 255));
        paint.setTextAlign(Paint.Align.CENTER);
        paint.setTextSize(rewind != null ? rewind.height() * 0.6f : 24);
        if (rewind != null) c.drawText("<<", rewind.centerX(), rewind.centerY() + rewind.height() * 0.2f, paint);
        if (turbo != null) c.drawText(">>", turbo.centerX(), turbo.centerY() + turbo.height() * 0.2f, paint);
        if (menu != null) c.drawText("=", menu.centerX(), menu.centerY() + menu.height() * 0.2f, paint);
```

- [ ] **Step 2: Build** — `./gradlew :app:assembleDebug`. Expected: FAILS in `EmulatorActivity` (still constructs with the old lambda shape) **only if** signatures diverged — the M1 call site passes `(k, pressed) -> ...` which matches a 2-method interface ambiguously; to keep this task self-contained, update the `EmulatorActivity` construction site minimally:

```java
        TouchOverlayView overlay = new TouchOverlayView(this, new TouchOverlayView.ControlListener() {
            @Override public void onKey(int k, boolean pressed) {
                if (ctx != 0) NativeBridge.nativeSetKey(ctx, k, pressed);
            }
            @Override public void onSpecial(int what, boolean pressed) {
                // wired fully in the activity task
            }
        });
```

Expected after that: `BUILD SUCCESSFUL`.

- [ ] **Step 3: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/TouchOverlayView.java Android/app/src/main/java/io/sameboy/android/EmulatorActivity.java
git commit -m "feat(android): rewind/turbo hold buttons + menu button on the touch overlay"
```

---

### Task 7: GameMenuDialog — in-game menu + save/load slot picker

**Files:**
- Create: `Android/app/src/main/java/io/sameboy/android/GameMenuDialog.java`

**Interfaces:**
- Produces: `GameMenuDialog.show(Activity, Host)` and the `Host` interface Task 8 implements:

```java
interface Host {
    void onMenuClosed();            // dialog fully dismissed → unpause
    void onSaveSlot(int slot);      // write state + thumbnail
    void onLoadSlot(int slot);
    void onResetGame();
    void onSwitchModel(int model);  // NativeBridge.MODEL_*
    void onExitGame();
    File stateFile(int slot);       // for exists/timestamp
    Bitmap thumbnail(int slot);     // null if none
}
```

- Consumes: `NativeBridge.MODEL_DMG_B/MODEL_CGB_E/MODEL_AGB` constants (M1).

- [ ] **Step 1: Implement** — complete file:

```java
package io.sameboy.android;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.text.format.DateUtils;
import android.view.Gravity;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;

/** In-game menu + save/load slot picker. Programmatic UI (no XML), M1 convention.
 *  The host pauses emulation before show() and unpauses in onMenuClosed(). */
final class GameMenuDialog {
    static final int SLOTS = 4;

    interface Host {
        void onMenuClosed();
        void onSaveSlot(int slot);
        void onLoadSlot(int slot);
        void onResetGame();
        void onSwitchModel(int model);
        void onExitGame();
        File stateFile(int slot);
        Bitmap thumbnail(int slot);
    }

    private GameMenuDialog() {}

    static void show(Activity a, Host h) {
        final String[] items = { "Resume", "Save state", "Load state", "Reset", "Model", "Exit" };
        final boolean[] chained = { false };   // a submenu took over; don't unpause yet
        AlertDialog dlg = new AlertDialog.Builder(a)
            .setTitle("SameBoy")
            .setItems(items, (d, which) -> {
                switch (which) {
                    case 1: chained[0] = true; showSlots(a, h, true); break;
                    case 2: chained[0] = true; showSlots(a, h, false); break;
                    case 3: h.onResetGame(); break;
                    case 4: chained[0] = true; showModels(a, h); break;
                    case 5: h.onExitGame(); return;   // activity finishes; no unpause
                    default: break;                   // 0 = Resume: just dismiss
                }
            })
            .create();
        dlg.setOnDismissListener(d -> { if (!chained[0]) h.onMenuClosed(); });
        dlg.show();
    }

    private static void showModels(Activity a, Host h) {
        final String[] names = { "Game Boy (DMG)", "Game Boy Color (CGB)", "Game Boy Advance (AGB)" };
        final int[] models = { NativeBridge.MODEL_DMG_B, NativeBridge.MODEL_CGB_E, NativeBridge.MODEL_AGB };
        AlertDialog dlg = new AlertDialog.Builder(a)
            .setTitle("Model (reboots the game)")
            .setItems(names, (d, which) -> h.onSwitchModel(models[which]))
            .create();
        dlg.setOnDismissListener(d -> h.onMenuClosed());
        dlg.show();
    }

    private static void showSlots(Activity a, Host h, boolean forSave) {
        LinearLayout col = new LinearLayout(a);
        col.setOrientation(LinearLayout.VERTICAL);
        int pad = (int) (a.getResources().getDisplayMetrics().density * 12);
        col.setPadding(pad, pad, pad, pad);

        AlertDialog dlg = new AlertDialog.Builder(a)
            .setTitle(forSave ? "Save to slot" : "Load from slot")
            .setView(col)
            .setNegativeButton("Cancel", null)
            .create();

        for (int i = 0; i < SLOTS; i++) {
            final int slot = i;
            File f = h.stateFile(slot);
            boolean exists = f.exists();

            LinearLayout row = new LinearLayout(a);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(0, pad / 2, 0, pad / 2);

            ImageView thumb = new ImageView(a);
            int tw = (int) (a.getResources().getDisplayMetrics().density * 64);
            thumb.setLayoutParams(new LinearLayout.LayoutParams(tw, tw * 144 / 160));
            thumb.setScaleType(ImageView.ScaleType.FIT_CENTER);
            thumb.setBackgroundColor(Color.DKGRAY);
            Bitmap bmp = exists ? h.thumbnail(slot) : null;
            if (bmp != null) thumb.setImageBitmap(bmp);
            row.addView(thumb);

            LinearLayout text = new LinearLayout(a);
            text.setOrientation(LinearLayout.VERTICAL);
            text.setPadding(pad, 0, 0, 0);
            TextView title = new TextView(a);
            title.setText("Slot " + (slot + 1));
            TextView sub = new TextView(a);
            sub.setText(exists
                ? DateUtils.getRelativeTimeSpanString(f.lastModified()).toString()
                : "Empty");
            text.addView(title);
            text.addView(sub);
            row.addView(text);

            boolean enabled = forSave || exists;
            row.setEnabled(enabled);
            title.setEnabled(enabled);
            row.setAlpha(enabled ? 1f : 0.4f);
            if (enabled) {
                row.setOnClickListener(v -> {
                    if (forSave) h.onSaveSlot(slot);
                    else h.onLoadSlot(slot);
                    dlg.dismiss();
                });
            }
            col.addView(row);
        }

        dlg.setOnDismissListener(d -> h.onMenuClosed());
        dlg.show();
    }
}
```

- [ ] **Step 2: Build** — `./gradlew :app:assembleDebug` → `BUILD SUCCESSFUL`.

- [ ] **Step 3: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/GameMenuDialog.java
git commit -m "feat(android): in-game menu dialog with save/load slot picker and model select"
```

---

### Task 8: EmulatorActivity — menu wiring, thumbnails, battery poller

**Files:**
- Modify: `Android/app/src/main/java/io/sameboy/android/EmulatorActivity.java`

**Interfaces:**
- Consumes: everything from Tasks 4–7.

- [ ] **Step 1: Implement.** Changes to `EmulatorActivity`:

1. New imports: `android.graphics.Bitmap`, `android.os.Handler`, `android.os.Looper`.
2. New fields:

```java
    private boolean menuOpen = false;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable batteryPoll = new Runnable() {
        @Override public void run() {
            if (ctx != 0 && !menuOpen && NativeBridge.nativeIsBatteryDirty(ctx)) {
                NativeBridge.nativePause(ctx, true);   // save+clear as one parked unit
                SaveStore.write(savFile, NativeBridge.nativeSaveBattery(ctx));
                NativeBridge.nativeClearBatteryDirty(ctx);
                NativeBridge.nativePause(ctx, false);
            }
            handler.postDelayed(this, 2000);
        }
    };
```

3. Overlay listener (replacing the Task 6 placeholder):

```java
        TouchOverlayView overlay = new TouchOverlayView(this, new TouchOverlayView.ControlListener() {
            @Override public void onKey(int k, boolean pressed) {
                if (ctx != 0) NativeBridge.nativeSetKey(ctx, k, pressed);
            }
            @Override public void onSpecial(int what, boolean pressed) {
                if (ctx == 0) return;
                switch (what) {
                    case TouchOverlayView.SPECIAL_REWIND:
                        NativeBridge.nativeSetRewinding(ctx, pressed); break;
                    case TouchOverlayView.SPECIAL_TURBO:
                        NativeBridge.nativeSetTurbo(ctx, pressed); break;
                    case TouchOverlayView.SPECIAL_MENU:
                        if (pressed && !menuOpen) openMenu(); break;
                }
            }
        });
```

4. Menu host:

```java
    private void openMenu() {
        menuOpen = true;
        NativeBridge.nativePause(ctx, true);
        GameMenuDialog.show(this, new GameMenuDialog.Host() {
            @Override public void onMenuClosed() {
                menuOpen = false;
                if (ctx != 0) NativeBridge.nativePause(ctx, false);
            }
            @Override public void onSaveSlot(int slot) { saveStateToSlot(slot); }
            @Override public void onLoadSlot(int slot) { loadStateFromSlot(slot); }
            @Override public void onResetGame() { if (ctx != 0) NativeBridge.nativeReset(ctx); }
            @Override public void onSwitchModel(int model) { if (ctx != 0) NativeBridge.nativeSwitchModel(ctx, model); }
            @Override public void onExitGame() { finish(); }
            @Override public java.io.File stateFile(int slot) {
                return SaveStore.stateFile(EmulatorActivity.this, romName, slot);
            }
            @Override public Bitmap thumbnail(int slot) {
                java.io.File t = SaveStore.stateThumb(EmulatorActivity.this, romName, slot);
                return t.exists() ? android.graphics.BitmapFactory.decodeFile(t.getPath()) : null;
            }
        });
    }

    private void saveStateToSlot(int slot) {
        byte[] state = NativeBridge.nativeSaveState(ctx);
        if (state == null) {
            Toast.makeText(this, "Save failed", Toast.LENGTH_SHORT).show();
            return;
        }
        SaveStore.write(SaveStore.stateFile(this, romName, slot), state);
        int[] f = NativeBridge.nativeCopyFrame(ctx);
        if (f != null && f.length >= 2) {
            int w = f[0], h = f[1];
            int[] px = new int[w * h];
            for (int i = 0; i < w * h; i++) {
                int p = f[2 + i];   // native ABGR → Bitmap ARGB
                px[i] = (p & 0xFF00FF00) | ((p & 0xFF) << 16) | ((p >>> 16) & 0xFF);
            }
            Bitmap bmp = Bitmap.createBitmap(px, w, h, Bitmap.Config.ARGB_8888);
            try (java.io.FileOutputStream out =
                     new java.io.FileOutputStream(SaveStore.stateThumb(this, romName, slot))) {
                bmp.compress(Bitmap.CompressFormat.PNG, 100, out);
            } catch (java.io.IOException ignored) {}
        }
    }

    private void loadStateFromSlot(int slot) {
        byte[] state = SaveStore.read(SaveStore.stateFile(this, romName, slot));
        if (state == null || !NativeBridge.nativeLoadState(ctx, state)) {
            Toast.makeText(this, "Load failed", Toast.LENGTH_SHORT).show();
        }
    }
```

5. Lifecycle interplay:
   - `onResume`: `if (ctx != 0 && !menuOpen) NativeBridge.nativePause(ctx, false);` then `handler.postDelayed(batteryPoll, 2000);`
   - `onPause`: `handler.removeCallbacks(batteryPoll);` before the existing pause+save.
   - `onSurfaceReady`: after `nativeStart`, add `if (menuOpen) NativeBridge.nativePause(ctx, true);` (surface recreation resets nothing else — `start()` unpauses natively).

- [ ] **Step 2: Build** — `./gradlew :app:assembleDebug` → `BUILD SUCCESSFUL`.

- [ ] **Step 3: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/EmulatorActivity.java
git commit -m "feat(android): wire in-game menu, save-state slots with thumbnails, battery flush poller"
```

---

### Task 9: Integration check — full build + host tests

**Files:** none new — verification only.

- [ ] **Step 1: Full host suite** — `Android/jni/test/run_host_tests.sh` → `ALL HOST TESTS PASSED` (ring, emulator, session).

- [ ] **Step 2: Clean build** —

```bash
cd ~/SameBoy/Android && JAVA_HOME=$HOME/Android/jdk17 ANDROID_HOME=$HOME/Android ./gradlew :app:assembleDebug
unzip -l app/build/outputs/apk/debug/app-debug.apk | grep -c libsameboy_core.so
```
Expected: `BUILD SUCCESSFUL`, `4` ABIs.

- [ ] **Step 3: Commit anything outstanding** (there should be nothing) and report status.

On-device Waydroid acceptance (spec §8, items 1–7) is run by the controller after the final review, per M1's checklist pattern.
