# SameBoy Android — M4: Settings (Design)

Status: **proposed** · Date: 2026-07-01 · Branch: `android-frontend`
Parent: `plans/2026-07-01-android-parity-roadmap.md` (M4) · Builds on M1–M3.

## 1. Goal & scope

A persistent settings surface mirroring iOS's, wired to the running session: emulation,
video, audio, and controls options that persist across restarts and visibly affect the
game. Reachable from the in-game menu ("Settings") and the library top bar.

### In
- **Emulation:** model (applies on next launch — switching reboots the GB), rewind length
  (seconds), RTC mode (sync-to-host / accurate), turbo cap (× multiplier, 0 = uncapped).
- **Video:** color correction (7 modes), light temperature (−1..+1), border mode
  (SGB / Never / Always).
- **Audio:** master volume (0–100 %), high-pass filter (off / accurate / remove-DC),
  interference volume (0–100 %).
- **Controls:** on-screen button opacity (0–100 %), touch haptics (on/off).
- Persist via `SharedPreferences`; **batch-apply** all Core-backed settings to the session
  in one self-parking native call (mirrors SDL's `open_menu` re-apply), at launch and on
  return from the settings screen.

### Out (deferred, documented)
- **Sample rate** selection — changing it means tearing down + rebuilding the AAudio
  stream and the pacing ring mid-session; disproportionate for M4. Fixed at 48 kHz.
- **Integer scaling / fill mode** and **frame blending** — render-thread work (the M1
  renderer already nearest-neighbor integer-scales); belongs with the shader/render pass
  in **M5**.
- **DMG palette** — M5 (palette editor). **Button layout customization** — later; M4 does
  opacity + haptics only, per the roadmap ("minimal here, deepened later").

## 2. Key decisions

- **No new Gradle dependency — hand-rolled settings UI.** The roadmap suggested AndroidX
  `PreferenceScreen`, but M1–M3 hold a strict no-new-dependency / no-XML-layout line
  (framework widgets, programmatic UI). A `ScrollView` of section headers + rows
  (`AlertDialog` list-selects for enums, `SeekBar` for ranges, `Switch` for toggles)
  covers the whole surface with zero new artifacts and matches the existing
  `GameMenuDialog` idiom. This supersedes the roadmap's Preference suggestion; the
  binding constraint (no new deps) wins.
- **One batched apply, not per-setter JNI.** Sliders fire many changes; self-parking each
  setter would stutter audio. Instead a single `sb_session_apply_settings(...)` self-parks
  **once**, applies every Core-backed setting, and resumes. Called at launch (`finishSetup`)
  and on `onResume` after the settings screen closes — never per keystroke.
- **Volume is a frontend scale, not a Core setting.** Core has no master-volume API (SDL
  scales samples itself, `main.c:844`). Mirror it: an `atomic_int volume` (0–256, 256 =
  1.0) on the session, read by `audio_cb` and applied as `sample * vol / 256`. Wired via a
  `const atomic_int *` pointer like M2's `audio_drop` (emulator stays session-agnostic;
  host tests pass a stack atomic or NULL).
- **Live vs. on-launch split (mirrors SDL).** Everything applies **live** except **model**,
  which applies on the next ROM launch (switching model reboots the game). The in-game
  menu's existing model picker (M2) remains the way to switch model mid-session; the
  Settings "model" pref is the default for the *next* launch.
- **Value ranges copied from SDL** so behavior matches upstream: light temperature
  `(t−10)/10` for a 0–20 slider (default 10 → 0.0); interference `v/100`; volume
  `s*vol/100`; turbo cap `cap/4` (`GB_set_turbo_cap`, 0 = uncapped).

## 3. Defaults (match SameBoy / SDL `configuration.c`)

| Setting | Default | Core/frontend |
|---|---|---|
| model | CGB-E | `nativeCreate` model arg (next launch) |
| rewind length | 120 s | `GB_set_rewind_length` |
| RTC mode | sync-to-host | `GB_set_rtc_mode` |
| turbo cap | 0 (uncapped) | `GB_set_turbo_cap` |
| color correction | Modern Balanced | `GB_set_color_correction_mode` |
| light temperature | 0.0 (slider 10/20) | `GB_set_light_temperature` |
| border mode | SGB | `GB_set_border_mode` |
| volume | 100 % | frontend scale |
| high-pass | Accurate | `GB_set_highpass_filter_mode` |
| interference | 0 % | `GB_set_interference_volume` |
| button opacity | 60 % | overlay Paint alpha |
| haptics | on | overlay `performHapticFeedback` |

## 4. Native surface

```c
/* emulator.h */
typedef struct {
    int    color_correction;   /* GB_color_correction_mode_t */
    double light_temperature;  /* -1..1 */
    int    border_mode;        /* GB_border_mode_t */
    int    highpass;           /* GB_highpass_mode_t */
    int    rtc_mode;           /* GB_rtc_mode_t */
    double rewind_seconds;
    double turbo_cap;          /* 0 = uncapped */
    double interference;       /* 0..1 */
} sb_settings;
void sb_emu_apply_settings(sb_emulator *e, const sb_settings *s);
void sb_emu_set_volume_ptr(sb_emulator *e, const atomic_int *volume);  /* 256 = 1.0 */

/* session.h */
void sb_session_apply_settings(sb_session *s, const sb_settings *cfg); /* self-parks */
void sb_session_set_volume(sb_session *s, int volume_256);            /* atomic, any thread */
```

`sb_emu_apply_settings` is a straight sequence of the eight `GB_set_*` calls (no clamping
beyond what Core does; enum ints are range-checked at the JNI boundary). `audio_cb` gains:
`if (vol && (v = atomic_load(vol)) != 256) { l = l*v/256; r = r*v/256; }`.

JNI additions (`NativeBridge`):
`nativeApplySettings(ctx, int colorCorrection, double lightTemp, int border, int highpass,
int rtcMode, double rewindSeconds, double turboCap, double interference)` and
`nativeSetVolume(ctx, int volume256)`.

## 5. Java architecture

```
Settings (SharedPreferences helper: typed get/set + defaults + KEY_* constants)
  └─ applyTo(ctx, session-context): reads all prefs →
       NativeBridge.nativeApplySettings(...) + nativeSetVolume(...)   (Core/audio)
       returns the Java-side bits (button opacity, haptics) to the caller
SettingsActivity (programmatic ScrollView; sections Emulation/Video/Audio/Controls)
  ├─ enum row  → AlertDialog single-choice, persists index
  ├─ range row → SeekBar + live value label, persists int
  └─ toggle row → Switch, persists bool
MainActivity: top-bar "Settings" button → SettingsActivity
GameMenuDialog: new "Settings" item → Host.onOpenSettings() → SettingsActivity
EmulatorActivity:
  - finishSetup: model = Settings.model(); nativeCreate(model,...); then
    Settings.apply(this, ctx) (native batch + volume) before nativeStart;
    overlay.setOpacity(Settings.buttonOpacity()); overlay.setHaptics(Settings.haptics()).
  - onResume: if ctx != 0, re-apply (returning from Settings) + refresh overlay opacity/haptics.
TouchOverlayView: `alpha` (Paint) + `haptics` fields; a real key-down (refcount 0→1)
  calls performHapticFeedback(VIRTUAL_KEY) when haptics on (no VIBRATE permission needed).
```

Model pref stores a `GB_model_t` int (DMG-B / CGB-E / AGB — the three M2 constants).
`SettingsActivity` writes prefs; nothing is applied to a running game from the settings
screen directly — apply happens on `onResume` back in `EmulatorActivity`, so the settings
screen needs no session handle (it may be opened from the library with no active session).

## 6. Concurrency

`sb_session_apply_settings` self-parks (M2 pattern) → the eight `GB_set_*` run while the
emu thread is parked → resume. Safe from the JNI/UI thread. `nativeSetVolume` writes an
atomic read by `audio_cb`; no parking (like turbo's flags). Border/rewind reallocation
happens parked, so no torn frame or ring race. Applying before `nativeStart` (launch) runs
with no emu thread at all.

## 7. Files

| File | Change |
|---|---|
| `Android/jni/emulator.{h,c}` | + `sb_settings`, `sb_emu_apply_settings`, `sb_emu_set_volume_ptr`; volume scale in `audio_cb` |
| `Android/jni/session.{h,c}` | + `atomic_int volume`; `sb_session_apply_settings` (self-park), `sb_session_set_volume`; wire volume ptr in create |
| `Android/jni/sameboy_jni.c` | + `nativeApplySettings`, `nativeSetVolume` |
| `Android/jni/test/test_emulator.c` | + apply-settings-no-crash + volume-scaling test |
| `Android/jni/test/test_session.c` | + apply-settings mid-run (self-park) in the concurrency test |
| `.../NativeBridge.java` | + two native decls |
| `.../Settings.java` (new) | SharedPreferences helper: keys, defaults, typed get/set, `apply()` |
| `.../SettingsActivity.java` (new) | programmatic settings screen |
| `.../MainActivity.java` | top-bar Settings button |
| `.../GameMenuDialog.java` | "Settings" menu item + `Host.onOpenSettings()` |
| `.../EmulatorActivity.java` | read model pref; apply at launch + onResume; overlay opacity/haptics |
| `.../TouchOverlayView.java` | opacity + haptics |
| `.../res/values/strings.xml` | settings labels |
| `.../AndroidManifest.xml` | register `SettingsActivity` |

## 8. Acceptance (from roadmap, sharpened)

1. Change color correction (e.g. Disabled ↔ Modern Balanced) → the running game's colors
   visibly change after returning from Settings; persists across app restart.
2. Change master volume to 0 % → audio silences; back to 100 % → returns. Persists.
3. Change border mode / high-pass / interference / light temperature → each persists and
   applies live on resume (border/interference/light visible or audible where the game
   supports it).
4. Change rewind length → persists; a subsequent rewind honors the new length.
5. Change model in Settings → next ROM launch boots that model (DMG shows DMG rendering);
   already-running game is unaffected until relaunch/reset.
6. Button opacity slider changes on-screen control transparency live; haptics toggle
   enables/disables touch vibration.
7. Settings open from both the library top bar and the in-game menu; opening from the
   library (no active session) doesn't crash.
8. Host tests: `sb_emu_apply_settings` runs without crashing and leaves the emulator
   runnable; volume 0 zeroes samples, 256 leaves them unchanged; apply-settings mid-run
   doesn't deadlock (session concurrency test).

## 9. Test strategy

- **Host C:** extend `test_emulator.c` — build an emulator, `sb_emu_apply_settings` with a
  representative `sb_settings`, run frames, assert still produces frames/audio (no crash,
  runnable). Volume: set `atomic_int` to 0 → assert drained samples are all 0; to 256 →
  assert unchanged vs. a no-volume-ptr baseline. Extend `test_session.c` to call
  `sb_session_apply_settings` + `sb_session_set_volume` mid-run under the `alarm()`
  deadlock net.
- **On-device (Waydroid):** acceptance 1–7 via the library launch path already proven in
  M2/M3 (the settings screen, prefs persistence, and live re-apply are all `content://`-
  independent, so unlike M3's import they are fully exercisable on the AVD): open Settings,
  change color correction + volume + opacity, return, observe the running game change,
  force-stop, relaunch, confirm persistence.
