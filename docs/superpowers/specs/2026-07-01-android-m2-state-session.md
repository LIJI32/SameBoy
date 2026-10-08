# SameBoy Android — M2: State & Session (Design)

Status: **proposed** · Date: 2026-07-01 · Branch: `android-frontend`
Parent: `plans/2026-07-01-android-parity-roadmap.md` (M2) · Builds on M1
(`specs/2026-07-01-android-frontend-design.md`).

## 1. Goal & scope

Give the running game a session surface: an **in-game menu** hosting **save-state
slots** (with thumbnails), **reset**, **model select**, and **exit**; plus two
hold-buttons on the touch overlay — **turbo** and **rewind**. Carries the M1 deferred
item: **periodic battery flush** while playing.

### In
- In-game menu (overlay button) → Resume · Save state · Load state · Reset · Model · Exit.
- 4 save-state slots per ROM, file layout mirroring iOS (`states/<rom>.s<N>` +
  `.s<N>.png` thumbnail), BESS-portable (Core format).
- Turbo (hold button, unthrottled; `GB_set_turbo_mode(true, false)` — frame skip
  enabled, matching iOS `GBRunModeTurbo`; skipped frames don't fire `vblank_cb`, the
  renderer just re-shows the last published frame).
- Rewind (hold button; Core ring, default length **120 s** — SDL's default).
- Model select (DMG-B / CGB-E / AGB) via `GB_switch_model_and_reset`, and automatic
  model switch on state load when the state was captured on a different model.
- Periodic battery flush driven by `GB_get_battery_dirty`.
- `volatile int` → C11 `stdatomic` for session flags (roadmap tech-debt item).

### Out (later milestones)
Settings persistence for rewind length / turbo cap (M4); slow-motion/underclock and
dynamic-speed analog control (M4/M6); cheats; SGB model option (needs border UI, M4/M5);
state thumbnails in any cloud/export UI.

## 2. Design constraints discovered in M1 code (binding)

These three traps are structural; every design decision below flows from them.

**(a) Turbo vs. the pacing ring.** M1's `audio_cb` → `sb_ring_push` **blocks when the
ring is full**; that block IS the emulation pacing. `GB_set_turbo_mode` alone would
still be capped at ~1× by the ring. Turbo therefore must switch the audio path to
**drop-on-full**: a session-level flag flips the push to non-blocking
(`sb_ring_try_push`), and the ring is **flushed once at turbo-on** (mirrors SDL's
`GB_audio_clear_queue()` on toggle) so latent samples don't replay late.

**(b) Rewind must keep running frames.** `GB_rewind_pop` only restores state — it
never calls the vblank callback (no frame publish) and produces no audio (no pacing).
A pop-only loop would busy-spin and freeze the displayed frame. Mirror iOS
(`GBViewController.m:1683`): per iteration **pop 2, then run 1 frame** — the re-run
frame republishes video through `vblank_cb` and paces through the ring (net −1
frame/iteration ≈ real-time backwards). On ring exhaustion (`GB_rewind_pop` → false),
**hold**: skip the run, sleep ~16 ms, stay parked-in-place until the button releases
(do not resume forward play under the user's finger).

**(c) `battery_dirty` is emu-thread state.** `GB_get_battery_dirty` reads a plain
`bool` written by the emu thread (`memory.c`) — polling it from JNI is a data race.
The **emu thread** copies it between frames into an `atomic_bool` on the session;
Java polls that snapshot (`nativeIsBatteryDirty`), and on true does
pause → `nativeSaveBattery` (existing) → `nativeClearBatteryDirty` (while parked) →
resume. Poll every 2 s on the UI thread; the pause window is a few ms (M1's pause is
already synchronous).

## 3. Architecture

Session-level **atomic controls, consumed by the emu thread** at the top of each loop
iteration — JNI never touches `GB_gameboy_t` while it runs:

```
Java (menu/overlay)      sb_session (atomics)          emu thread (each iteration)
 nativeSetTurbo ───────► turbo: atomic_bool ──────────► apply GB_set_turbo_mode(on,false)
 nativeSetRewinding ───► rewinding: atomic_bool ──────► pop 2 → run 1 (hold on empty)
 (poll) ◄─────────────── battery_dirty: atomic_bool ◄── copy GB_get_battery_dirty()
```

Blocking ops stay on the **parked** path (M1's synchronous `sb_session_pause`):
state save/load, model switch, reset, battery flush. The JNI wrappers for these
**self-park**: `pause(1) → op → pause(restore)`, so they are safe in any call order
(menu code cannot get it wrong). `sb_session_reset` moves onto this path too — M1
called `GB_reset` unguarded from JNI, which becomes a live race once the menu's Reset
button exists.

Audio drop-on-full: plain `int drop_on_full` on the ring, written only with the emu
thread parked or before threads start… **no** — turbo toggles mid-run. It is an
`atomic_bool` on the session read by `audio_cb` (emu thread) each push; the flag flip
plus one `sb_ring_flush` happen in `sb_session_set_turbo` (any thread; flush is
already thread-safe — stop() uses it cross-thread today).

## 4. Native surface (new/changed)

```c
/* ring_buffer.h */
int  sb_ring_try_push(sb_ring *r, int16_t l, int16_t rr); /* 0 = dropped (full) */

/* emulator.h */
size_t sb_emu_save_state(sb_emulator *e, uint8_t **out_malloced);       /* caller frees */
int    sb_emu_load_state(sb_emulator *e, const uint8_t *buf, size_t n); /* 0 = ok; auto model-switch */
void   sb_emu_switch_model(sb_emulator *e, int model);
void   sb_emu_set_rewind_length(sb_emulator *e, double seconds);
void   sb_emu_set_turbo(sb_emulator *e, int on);        /* GB_set_turbo_mode(on, false) */
int    sb_emu_rewind_pop(sb_emulator *e);               /* 0 = ring empty */
int    sb_emu_battery_dirty(sb_emulator *e);            /* emu-thread or parked only */
void   sb_emu_clear_battery_dirty(sb_emulator *e);

/* session.h */
void   sb_session_set_turbo(sb_session *s, int on);
void   sb_session_set_rewinding(sb_session *s, int on);
int    sb_session_battery_dirty(sb_session *s);         /* atomic snapshot, any thread */
void   sb_session_clear_battery_dirty(sb_session *s);   /* self-parks */
size_t sb_session_save_state(sb_session *s, uint8_t **out);             /* self-parks */
int    sb_session_load_state(sb_session *s, const uint8_t *buf, size_t n); /* self-parks */
void   sb_session_switch_model(sb_session *s, int model);               /* self-parks */
/* sb_session_reset: now self-parks (was unguarded) */
```

`sb_emu_load_state` mirrors iOS `loadStateFromFile:`: `GB_get_state_model_from_buffer`
first; if it differs from the current model, `GB_switch_model_and_reset` then
`GB_load_state_from_buffer`. Rewind length is set in `sb_emu_create` (120 s default)
— before threads exist, so no synchronization concern.

JNI additions (`NativeBridge`): `nativeSaveState(ctx) → byte[]`,
`nativeLoadState(ctx, byte[]) → boolean`, `nativeSetTurbo(ctx, boolean)`,
`nativeSetRewinding(ctx, boolean)`, `nativeSwitchModel(ctx, int)`,
`nativeIsBatteryDirty(ctx) → boolean`, `nativeClearBatteryDirty(ctx)`,
`nativeCopyFrame(ctx) → int[]` — `[0]=w, [1]=h`, then `w*h` pixels in one atomic
capture (thumbnail via `sb_emu_copy_front`; native ABGR → Bitmap ARGB swizzle in Java).

## 5. Emu-loop change (session.c, the heart of M2)

```c
while (running) {
    park-if-paused (unchanged M1 machinery);
    bool turbo = atomic_load(&s->turbo);
    if (turbo != applied_turbo) { sb_emu_set_turbo(emu, turbo); applied_turbo = turbo; }
    if (atomic_load(&s->rewinding)) {
        sb_emu_rewind_pop(emu);
        if (!sb_emu_rewind_pop(emu)) { nanosleep(16 ms); continue; }  /* empty: hold */
    }
    sb_emu_run_frame(emu);                    /* publishes frame + paces via ring */
    atomic_store(&s->battery_dirty, sb_emu_battery_dirty(emu));
    if (!s->audio) { flush + 16 ms sleep (unchanged); }
}
```

`audio_cb` becomes: `if (atomic_load(&e->session_drop_flag)) sb_ring_try_push(...)
else sb_ring_push(...)` — wired via a `const atomic_bool *drop_on_full` pointer handed
to the emulator by the session before threads start (emulator stays session-agnostic;
host tests pass a stack atomic or NULL).

## 6. Java UI

- **TouchOverlayView**: two new hold-regions — `⏪` (left edge, above d-pad) and `⏩`
  (right edge, above A/B) — press/release → `nativeSetRewinding` / `nativeSetTurbo`.
  Same hit-testing/refcount machinery as M1 keys (they join the region list with
  pseudo-key ids ≥ 8, routed to a second callback instead of `nativeSetKey`).
- **Menu button** `☰` (top-right corner region) → opens the menu and pauses.
- **GameMenuDialog** (programmatic `Dialog`, no XML — M1 has no resource layer):
  vertical list — Resume · Save state · Load state · Reset · Model ▸ (DMG-B/CGB-E/AGB)
  · Exit. Save/Load open a **slot grid** (4 slots): thumbnail (or "Empty"), timestamp
  ("Just now" / relative, mirroring iOS copy). Load on an empty slot is disabled.
  Dialog opens ⇒ `nativePause(true)`; dismiss ⇒ `nativePause(false)` unless exiting.
- **StateStore** (extends `SaveStore` conventions): `states/` sibling of `saves/`;
  `stateFile(ctx, rom, slot)` = `states/<rom>.s<slot>`, thumbnail `…s<slot>.png`.
  **Fixes the M1 null-fallback bug**: base dir helper falls back to
  `ctx.getFilesDir()` when `getExternalFilesDir(null)` is null (applies to `savFile`
  too — today it would NPE).
- **EmulatorActivity**: hosts menu callbacks; battery-flush poller (`Handler`, 2 s):
  `if (nativeIsBatteryDirty) { pause; write savFile; clearBatteryDirty; resume; }` —
  skipped while the menu holds the pause. Thumbnail on save: `nativeCopyFrame` →
  ABGR→ARGB swizzle → `Bitmap` → PNG.

## 7. Files

| File | Change |
|---|---|
| `Android/jni/ring_buffer.{h,c}` | + `sb_ring_try_push` |
| `Android/jni/emulator.{h,c}` | + state/model/rewind/turbo/battery primitives; audio_cb drop path; rewind length @ create |
| `Android/jni/session.{h,c}` | atomics (`running`/`paused` migrate too); turbo/rewind consumption in loop; self-parking ops |
| `Android/jni/sameboy_jni.c` | + 9 JNI entry points |
| `Android/jni/test/test_emulator.c` | + state roundtrip, model switch, rewind pop, battery-dirty tests |
| `Android/jni/test/test_session.c` (new) | session-level: turbo flag application, rewind hold, self-parking save during run |
| `.../NativeBridge.java` | + native methods |
| `.../SaveStore.java` | base-dir fallback fix; `states/` helpers (or new `StateStore`) |
| `.../TouchOverlayView.java` | rewind/turbo hold regions + menu region |
| `.../GameMenuDialog.java` (new) | menu + slot grid |
| `.../EmulatorActivity.java` | menu wiring, battery poller, thumbnails |

## 8. Acceptance (from roadmap, sharpened)

1. Save to slot N → reset → load slot N ⇒ gameplay state restored (on-device).
2. Hold ⏩ ⇒ visibly faster than 1×; release ⇒ normal speed, audio resumes clean.
3. Hold ⏪ ⇒ gameplay runs backwards; ring exhaustion holds (no forward creep);
   release ⇒ forward play from the rewound point.
4. Model picker: CGB game + DMG-B model ⇒ reboots into DMG rendering (and back).
5. State from model A loads on model B session (auto-switch, mirrors iOS).
6. Battery: with a battery game, play past a save point, wait ≥1 poll tick, kill the
   app (no clean exit) ⇒ relaunch has the save.
7. Menu open ⇒ emulation paused (audio silent); Resume ⇒ continues; no crash/deadlock
   across repeated open/close, save/load, turbo/rewind spam (parked-op discipline).
8. Host tests: state roundtrip, model switch+load, rewind pop, try_push drop, battery
   dirty lifecycle — all pass in `run_host_tests.sh`.

## 9. Test strategy

- **Host C tests** (extend M1 harness): everything platform-independent — state
  roundtrip byte-exactness is verifiable without a device (save → run 60 frames →
  load → `GB_save_state_to_buffer` again ⇒ RAM/registers sections match), rewind
  pop restores an earlier framebuffer, try_push returns 0 when full, model switch
  changes `GB_get_model`, battery dirty sets/clears.
- **New `test_session.c`**: exercises the atomics path with real threads — start a
  headless session (NULL window ⇒ no render; audio may fail ⇒ flush path), toggle
  turbo/rewind while running, save state mid-run (self-park), assert no deadlock and
  state size sane. This is the concurrency net.
- **On-device (Waydroid)**: acceptance 1–7 manually via `adb shell input` +
  `screencap` diffing, per M1's checklist pattern.
