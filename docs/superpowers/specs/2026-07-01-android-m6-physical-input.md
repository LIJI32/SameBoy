# SameBoy Android — M6: Physical Input (Design)

Status: **proposed** · Date: 2026-07-01 · Branch: `android-frontend`
Parent: `plans/2026-07-01-android-parity-roadmap.md` (M6) · Builds on M1–M5.

## 1. Goal & scope

Play with a **hardware gamepad** (buttons + d-pad/stick), **remap** its buttons to the
eight GB inputs, and get **rumble** on rumble-capable devices/pads. On-screen touch
controls auto-hide while a gamepad is connected.

### In
- **Gamepad input** (pure Java, no per-key native change — reuses `nativeSetKey`): map
  hardware `KeyEvent` gamepad keycodes and `MotionEvent` axes (d-pad hat + left stick,
  with deadzone) to the eight GB keys.
- **Button remapping:** a capture screen that binds each GB input to a pressed controller
  keycode; persisted in `SharedPreferences`; "Reset to defaults".
- **Rumble:** Core `GB_set_rumble_callback` → latest amplitude in an `atomic_int` → JNI
  getter → a Java poller drives the system `Vibrator`. **Rumble mode** setting
  (Disabled / Cartridge only / All games) applied via `GB_set_rumble_mode` on the M4/M5
  batch-apply path.
- **On-screen controls auto-hide** when a gamepad is present (roadmap "optional" — cheap,
  included), re-show when none.

### Out (deferred, documented)
- Analog triggers as buttons, rapid-fire/turbo-on-pad, per-controller profiles, multiple
  simultaneous players (single active pad), full keyboard-as-gamepad remap UI (physical
  keyboards still work through the same keycode map, just not surfaced as "keyboard").
- Rumble waveform shaping / per-game strength curve (single amplitude poll is enough).

## 2. Key decisions

- **Input stays Java-only.** Hardware keys/axes → the existing `nativeSetKey(ctx, gbKey,
  pressed)`; no new per-input JNI. `EmulatorActivity` overrides `dispatchKeyEvent` +
  `onGenericMotionEvent`; a `GamepadMapper` does the keycode/axis → GB-key translation.
- **Map by keycode, don't hard-filter source.** The mapper only contains gamepad button
  keycodes; unmapped keys (letters, **BACK**, volume) fall through to `super`. So a
  physical keyboard sending the same keycodes also works, and BACK/system keys are never
  swallowed. Gamepad *presence* (for auto-hide) is detected separately via
  `InputDevice.getSources()`.
- **Rumble = amplitude atomic + poll (mirrors M2 battery-dirty).** The Core rumble
  callback runs on the emu thread; it stores `(int)(amplitude*255)` in an `atomic_int` on
  the emulator. A Java `Handler` (~50 ms) reads `nativeRumbleAmplitude(ctx)` and drives the
  `Vibrator`: `amp>0` → `vibrate(oneShot(70 ms, amp))` (clamped 1–255, or default if the
  device lacks amplitude control), `amp==0` → `cancel()`. No per-callback JNI up-calls.
- **Rumble mode joins the batch.** `sb_settings` gains `int rumble_mode`;
  `nativeApplySettings` grows one trailing `int rumbleMode` argument (our own signature,
  both sides updated together — keeps a single batch-apply). Default **Cartridge only**
  (SameBoy/SDL `GB_RUMBLE_CARTRIDGE_ONLY`).
- **VIBRATE permission.** The explicit `Vibrator` needs `android.permission.VIBRATE`
  (M4/M5 touch haptics used `performHapticFeedback`, which didn't).
- **Remap capture is an Activity, not a dialog.** Gamepad `KeyEvent`s reliably reach an
  Activity's `dispatchKeyEvent`; a plain dialog wouldn't focus them. `GamepadRemapActivity`
  lists the 8 GB inputs; tapping one arms capture, and the next controller keycode binds.

## 3. Default gamepad mapping (Android keycode → GB key index)

| Keycode | const | GB key |
|---|---|---|
| 22 DPAD_RIGHT | KEYCODE_DPAD_RIGHT | 0 RIGHT |
| 21 DPAD_LEFT | KEYCODE_DPAD_LEFT | 1 LEFT |
| 19 DPAD_UP | KEYCODE_DPAD_UP | 2 UP |
| 20 DPAD_DOWN | KEYCODE_DPAD_DOWN | 3 DOWN |
| 96 BUTTON_A | KEYCODE_BUTTON_A | 4 A |
| 97 BUTTON_B | KEYCODE_BUTTON_B | 5 B |
| 109 BUTTON_SELECT | KEYCODE_BUTTON_SELECT | 6 SELECT |
| 108 BUTTON_START | KEYCODE_BUTTON_START | 7 START |

Axes (synthesized d-pad edges, deadzone 0.5, tracked so only transitions call
`nativeSetKey`): `AXIS_HAT_X`/`AXIS_HAT_Y` and `AXIS_X`/`AXIS_Y` (left stick) → LEFT/RIGHT/
UP/DOWN. Remapped buttons override the keycode table; axes are fixed to the d-pad.

## 4. Native surface

```c
/* emulator.h */
typedef struct {
    /* ...existing M4/M5 fields... */
    int rumble_mode;           /* GB_rumble_mode_t: 0 disabled, 1 cartridge, 2 all */
} sb_settings;
int  sb_emu_rumble_amplitude(sb_emulator *e);   /* 0..255, latest from the rumble callback */

/* session.h */
int  sb_session_rumble_amplitude(sb_session *s); /* atomic read, any thread */
```

- `sb_emulator` gains `atomic_int rumble_amp`. In `sb_emu_create`:
  `GB_set_rumble_callback(&e->gb, rumble_cb)` where
  `rumble_cb(gb, amp){ atomic_store(&e->rumble_amp, (int)(amp*255+0.5)); }`.
- `sb_emu_apply_settings` adds `GB_set_rumble_mode(&e->gb, (GB_rumble_mode_t)s->rumble_mode)`.
- JNI: `nativeRumbleAmplitude(ctx) -> int`; `nativeApplySettings(... , int rumbleMode)`
  (trailing arg, clamped 0–2). 21 JNI symbols total.

## 5. Java architecture

```
GamepadMapper (new):
  - defaults + SharedPreferences map keycode→GBkey (8 keys, "gp_<gbkey>" = keycode)
  - int gbKeyForKeycode(int keycode)  → 0..7 or -1
  - void setBinding(int gbKey, int keycode) / resetDefaults() / int keycodeFor(int gbKey)
  - MotionAxis helper: given a MotionEvent, compute the 4 d-pad booleans (deadzone 0.5)
  - static boolean isGamepad(InputDevice) via getSources() & (SOURCE_GAMEPAD|SOURCE_JOYSTICK)
  - static boolean anyGamepadConnected() via InputDevice.getDeviceIds()
Settings: rumbleMode() (default 1); apply(ctx) passes rumbleMode to nativeApplySettings.
EmulatorActivity:
  - dispatchKeyEvent: if mapper.gbKeyForKeycode(kc) >= 0 → nativeSetKey(down/up), return true;
    else super (BACK/system/unmapped pass through).
  - onGenericMotionEvent (SOURCE_JOYSTICK): mapper axis → set the 4 d-pad GB keys by transition.
  - rumble poller (Handler ~50ms): amp = nativeRumbleAmplitude(ctx); drive Vibrator.
  - overlay auto-hide: on resume + InputManager.InputDeviceListener add/remove → if a pad is
    connected, overlay.setVisibility(GONE) else VISIBLE.
GamepadRemapActivity (new): lists Up/Down/Left/Right/A/B/Select/Start with current binding;
  tap a row → "Press a button…" armed; its dispatchKeyEvent captures the next gamepad keycode
  → mapper.setBinding; a Reset action restores defaults.
SettingsActivity: Controls section gains "Rumble" enum (Disabled/Cartridge only/All games)
  and "Gamepad buttons" row → GamepadRemapActivity.
```

## 6. Concurrency

Rumble amplitude is a single `atomic_int` written by the rumble callback (emu thread) and
read by the poller (main thread) — same pattern as M2 `battery_dirty`. No parking needed.
`rumble_mode` rides `sb_session_apply_settings` (self-parks) via the existing batch.
Input `nativeSetKey` is the M1 path (already safe from the UI thread).

## 7. Files

| File | Change |
|---|---|
| `Android/jni/emulator.{h,c}` | + `rumble_amp` atomic, `rumble_cb`, `sb_emu_rumble_amplitude`; `rumble_mode` in `sb_settings` + apply |
| `Android/jni/session.{h,c}` | + `sb_session_rumble_amplitude` |
| `Android/jni/sameboy_jni.c` | + `nativeRumbleAmplitude`; extend `nativeApplySettings` (+rumbleMode) |
| `Android/jni/test/test_emulator.c` | + rumble-mode-set + amplitude-getter-default test |
| `.../NativeBridge.java` | update `nativeApplySettings` sig; + `nativeRumbleAmplitude` |
| `.../GamepadMapper.java` (new) | keycode/axis → GB-key map + prefs + presence detection |
| `.../Settings.java` | + `rumbleMode`; apply passes it |
| `.../EmulatorActivity.java` | gamepad dispatch + rumble poller + overlay auto-hide |
| `.../GamepadRemapActivity.java` (new) | remap capture screen |
| `.../SettingsActivity.java` | Rumble row + Gamepad-buttons row |
| `.../res/values/strings.xml` | rumble / gamepad labels |
| `.../AndroidManifest.xml` | VIBRATE permission + register `GamepadRemapActivity` |

## 8. Acceptance (from roadmap, sharpened)

1. A connected gamepad controls the game: d-pad + A/B/Start/Select drive the GB (verified
   on-device by injecting gamepad key events; the game responds).
2. Remapping sticks: bind (e.g.) GB A to a different controller button in the remap screen;
   that button now presses GB A; persists across app restart.
3. Rumble fires on a rumble-capable device/pad: with rumble mode enabled and a
   rumble-using game, the `Vibrator` is driven from the Core amplitude (the wiring is
   exercised; actual motor buzz is device-dependent).
4. Rumble mode setting persists and gates rumble (Disabled = no vibration).
5. On-screen controls hide when a gamepad is connected and reappear when it's removed.
6. Host tests: setting rumble mode (incl. `GB_RUMBLE_ALL_GAMES`) and running frames doesn't
   crash; `sb_emu_rumble_amplitude` returns 0 by default and stays in 0..255.

## 9. Test strategy

- **Host C:** on a DMG emulator, `sb_emu_apply_settings` with `rumble_mode =
  GB_RUMBLE_ALL_GAMES`, run frames, assert no crash and `sb_emu_rumble_amplitude()` in
  0..255 (a non-rumble ROM leaves it 0). Extend `test_session.c` to read
  `sb_session_rumble_amplitude` mid-run under the `alarm()` net.
- **On-device (Waydroid):** gamepad + remap are `input`-injectable and fully exercisable.
  Inject `adb shell input gamepad keyevent <code>` (or the mapper matches by keycode so a
  keyboard-sourced code works too) → the game reacts. Open the remap screen, arm GB A,
  inject a different keycode → binding changes → that keycode now drives GB A; force-stop +
  relaunch → binding persists. Toggle Rumble mode + verify the poller path runs without
  crashing (Waydroid has no vibrator motor, so the physical buzz is unverifiable — the
  `Vibrator` call is a no-op there; documented, like M3's `content://` limitation). Auto-
  hide: with no pad the overlay shows; connecting one hides it (verified via the
  `InputDeviceListener` path where injectable).
