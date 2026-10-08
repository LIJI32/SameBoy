# SameBoy Android Frontend — Design (Milestone 1: Foundation)

Status: **proposed** · Date: 2026-07-01 · Branch: `android-frontend`

## 1. Goal & context

Add a native standalone **Android** app to SameBoy, structured as a new top-level
sub-project (`Android/`), peer to the existing native `iOS/` app. The portable C
`Core` is reused **unmodified** and driven through a thin C/JNI bridge. The app layer
(UI, activities, touch input, storage) is written in **Java**, mirroring the decision
that iOS uses Objective-C rather than Swift.

This document specifies **Milestone 1 (M1): Foundation** — the smallest slice that
actually boots a ROM and is playable on a phone. Later milestones (state management,
library, settings, palettes, gamepads, peripherals, link cable, packaging) are listed
in the roadmap (§11) and each gets its own spec when reached. M1 is the load-bearing
foundation everything else builds on, so it is designed in full here.

### 1.1 M1 scope (in)

- New `Android/` sub-project + Gradle build that compiles `Core` for 4 ABIs via `ndk-build`.
- C/JNI bridge exposing emulator lifecycle, input, framebuffer, and battery save/load.
- GLES2 rendering of the GB screen (dynamic size; nearest-neighbor integer scaling).
- AAudio output driven by SameBoy's APU sample callback.
- Dedicated native emulation thread, paced by audio.
- ROM loading via the Storage Access Framework (SAF) → in-memory buffer.
- Bundled open-source boot ROMs (built from `BootROMs/*.asm`) loaded from app assets.
- On-screen touch controls: D-pad, A, B, Start, Select.
- Cartridge battery persistence (`.sav`) to app-specific storage.

### 1.2 M1 scope (out — deferred to later milestones)

Save states, in-game menu, rewind/turbo UI, ROM library browser, settings screens,
palette editor, themes, physical gamepads, haptics/rumble UI, printer, camera, link
cable, Play-Store packaging/signing, CI. (Rumble/turbo Core hooks may be wired at the
callback level but have no UI in M1.)

### 1.3 Non-goals / constraints

- **No changes to `Core`** beyond, if strictly required, additive `#ifdef __ANDROID__`
  guards. None are anticipated — the libretro core already builds for Android from the
  same sources.
- **No CMake.** Build uses Gradle + `externalNativeBuild { ndkBuild }` on an
  `Android.mk`, mirroring the existing `libretro/jni` pattern.
- **No new runtime C dependencies.** Audio uses AAudio (NDK system API); rendering uses
  EGL/GLES2 (NDK system libs). No Oboe (C++), no SDL.
- **License:** app code under SameBoy's Expat license; bundled boot ROMs are SameBoy's
  own open-source boot ROMs (already in-tree).

## 2. Architecture overview

```
┌───────────────────────────── Java (app layer) ─────────────────────────────┐
│  EmulatorActivity ── EmulatorSurfaceView (SurfaceView)                      │
│        │                     │ surface lifecycle                            │
│        │ touch               │                                              │
│  TouchOverlayView ──► NativeBridge (JNI) ◄── SAF ROM bytes, .sav bytes      │
└───────────────────────────────┼─────────────────────────────────────────────┘
                                 │ JNI (setup/teardown/input only — NOT per pixel)
┌────────────────────────────── C (Android/jni) ─────────────────────────────┐
│  sameboy_jni.c  →  emulator.c  ── owns GB_gameboy_t                          │
│                         │                                                    │
│      ┌──────────────────┼───────────────────────┐                           │
│  emu thread        render thread (EGL/GLES2)   AAudio callback              │
│  loop GB_run_frame  uploads framebuffer→quad    drains sample ring          │
│      │  vblank cb ─────► double-buffer swap        ▲                         │
│      └─ APU sample cb ─────────────────────────────┘ (fills ring; blocks     │
│                                                       when full = pacing)    │
│                         Core/*.c (unchanged)                                 │
└──────────────────────────────────────────────────────────────────────────┘
```

**Key architectural decision — native-driven hot loop.** The emulation thread, the
AAudio callback, and the EGL/GLES2 render thread all live in C. Java owns only the UI:
activity lifecycle, the `SurfaceView`, the touch overlay, SAF, and (later) menus/settings.

Rationale:
- Real-time threads (audio, emulation) never touch the JVM, so GC pauses cannot cause
  audio underruns or frame stutter.
- Per-frame pixel data never crosses JNI.
- Matches SameBoy's C-centric design; the Java layer is genuinely the "app/UI layer".

The native render thread binds to the `Surface` handed down from the `SurfaceView` via
`ANativeWindow_fromSurface`. (Alternative considered: a Java `GLSurfaceView.Renderer`
that copies pixels over JNI each frame — simpler but puts the render loop on the JVM and
crosses JNI ~92 KB/frame. Rejected for M1; the native path is the correct long-term base
and is required anyway to reuse SameBoy's GLSL shaders in a later milestone.)

## 3. Component design

### 3.1 `emulator.c` — Core lifecycle & callbacks

Owns a heap `GB_gameboy_t` and the emulation thread. Mirrors the SDL frontend's proven
setup (`SDL/main.c:1162–1226`). Init sequence:

```c
GB_init(&e->gb, model);                 // model default: GB_MODEL_CGB_E (auto)
GB_set_boot_rom_load_callback(&e->gb, load_boot_rom_cb);
GB_set_vblank_callback(&e->gb, vblank_cb);
GB_set_pixels_output(&e->gb, e->back_buffer);
GB_set_rgb_encode_callback(&e->gb, rgb_encode_cb);
GB_set_sample_rate(&e->gb, AUDIO_SAMPLE_RATE);      // 48000
GB_apu_set_sample_callback(&e->gb, audio_sample_cb);
GB_set_rumble_callback(&e->gb, rumble_cb);          // no-op UI in M1
GB_load_rom_from_buffer(&e->gb, rom, rom_size);
GB_load_battery_from_buffer(&e->gb, sav, sav_size); // if a .sav exists
GB_reset(&e->gb);
```

Callbacks (bodies mirror `SDL/main.c`):

- **`rgb_encode_cb(gb, r, g, b)`** → packs one pixel for a GL_RGBA/UNSIGNED_BYTE texture.
  On little-endian ARM/x86 the in-memory byte order must be R,G,B,A, so:
  `return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;`
- **`vblank_cb(gb, type)`** → marks a completed frame: under a mutex, swap
  `front_buffer`⇄`back_buffer` and re-point `GB_set_pixels_output` at the new back buffer;
  set a `frame_dirty` flag the render thread consumes. `type` (`GB_VBLANK_TYPE_*`)
  distinguishes real vs repeated/LCD-off frames; M1 renders on any non-repeat frame.
- **`audio_sample_cb(gb, sample)`** → push one `GB_sample_t {int16 left,right}` into the
  sample ring buffer (§3.3). This is the pacing point.
- **`load_boot_rom_cb(gb, type)`** → look up the asset name for `type` (same table as
  `SDL/main.c:1023–1034`) and call `GB_load_boot_rom_from_buffer(gb, bytes, len)` from the
  asset bytes loaded at init (§3.6). CGB_E→CGB and AGB_0→AGB fallbacks preserved.

**Framebuffer sizing.** `GB_get_screen_width/height()` are dynamic (160×144 normally,
256×224 with an SGB border). Both buffers are allocated at the max `256*224` `uint32_t`
(as SDL does) so a model/border change never reallocates; the render thread reads the
current width/height each frame.

### 3.2 Emulation thread & pacing

```
while (running) {
    if (paused) { wait_on_cond(); continue; }
    GB_run_frame(&e->gb);   // produces one frame + ~(48000/59.7) samples
}
```

`GB_run_frame` invokes `audio_sample_cb` per generated sample. When the sample ring is
full, `audio_sample_cb` **blocks** on a condition variable until the AAudio callback
drains it. Thus audio consumption is the master clock → correct real-time pacing without
a manual sleep/vsync loop. (This is the inverse of SDL's "drop if queue too long" policy;
blocking is preferred on Android because AAudio pulls at a hardware-locked rate.)

Pause/resume (activity `onPause`/`onResume`) sets `paused` and pauses the AAudio stream.

### 3.3 `ring_buffer.c` — sample ring

Single-producer (emu thread) / single-consumer (AAudio callback) ring of `GB_sample_t`.
Capacity ≈ 0.1 s (`4800` frames) to bound latency. Producer blocks when full; consumer
zero-fills on underrun. Guarded by a mutex + condition variable (M1 correctness first; a
lock-free SPSC variant is a possible later optimization, not needed for 2-ch/48 kHz).

### 3.4 `audio_aaudio.c` — AAudio driver

Builds one output stream: `AAUDIO_FORMAT_PCM_I16`, 2 channels, `AUDIO_SAMPLE_RATE`,
`AAUDIO_PERFORMANCE_MODE_LOW_LATENCY`, data-callback mode. The callback copies
`numFrames` samples from the ring into AAudio's buffer (zero-fill on underrun), then
signals the producer CV. Exposes `start`/`pause`/`stop`. Chosen over Java `AudioTrack`
(higher latency, JVM thread) and OpenSL ES (deprecated); AAudio is a pure-C NDK API
available from **API 26**, which sets `minSdk`.

### 3.5 `render_gles.c` — EGL + GLES2 blit

On a dedicated render thread bound to the `ANativeWindow`:
- Create EGL context (GLES2), one `GL_RGBA` texture sized to the current GB screen.
- Each iteration: if `frame_dirty`, lock, `glTexSubImage2D` from `front_buffer`, unlock,
  draw a full-screen textured quad, `eglSwapBuffers`.
- Scaling: `GL_NEAREST`, aspect-correct integer letterbox (compute viewport from surface
  size vs GB size). Frame-blending and SameBoy's OmniScale/AA-Scale2x GLSL shaders are a
  later milestone; the texture pipeline is designed so they slot in without rework.

Render pacing follows `eglSwapBuffers` (display vsync). Emulation is paced independently
by audio; the double buffer decouples the two rates.

### 3.6 ROM, boot ROM & battery I/O

- **ROM:** Java opens the SAF `Uri`, reads all bytes, passes `byte[]` to
  `nativeInit(...)`; native calls `GB_load_rom_from_buffer`. No filesystem path needed
  (SAF-friendly). `.gb/.gbc/.isx` accepted; `.zip` deferred to the library milestone.
- **Boot ROMs:** Built from `BootROMs/*.asm` at build time (§7.3), bundled under
  `assets/bootroms/*.bin`. At init the app reads them via `AAssetManager` (passed to JNI)
  into memory; `load_boot_rom_cb` feeds the right one per `GB_boot_rom_t`. If a boot ROM
  is missing, the Core runs boot-ROM-less (no boot animation) — graceful degradation.
- **Battery (.sav):** Persisted to app-specific storage
  (`Context.getExternalFilesDir("saves")/<romName>.sav`). Loaded at init via
  `GB_load_battery_from_buffer`; saved via `GB_save_battery_to_buffer` (size from
  `GB_save_battery_size`) on pause and periodically when `GB_get_battery_dirty()` is set
  (mirrors SDL's `battery_dirty` polling).

### 3.7 Java UI layer

- **`EmulatorActivity`** — hosts the surface + overlay; wires lifecycle to native
  start/stop/pause; triggers battery flush on pause.
- **`EmulatorSurfaceView`** (`SurfaceView` + `SurfaceHolder.Callback`) — on
  `surfaceCreated` passes `holder.getSurface()` to `nativeStart`; on `surfaceDestroyed`
  calls `nativeStop`.
- **`TouchOverlayView`** — draws D-pad + A/B/Start/Select (reusing iOS art assets where
  suitable), multi-touch, maps press/release to `nativeSetKey(keyIndex, pressed)` using
  the `GB_KEY_*` indices. Overlaid via a `FrameLayout`.
- **`MainActivity`** — launcher; a single "Open ROM" button firing an
  `ACTION_OPEN_DOCUMENT` SAF intent, then starts `EmulatorActivity`. (A real library is a
  later milestone.)
- **`NativeBridge`** — `static { System.loadLibrary("sameboy_core"); }` and all `native`
  declarations (§4).

## 4. JNI bridge API (M1)

`io.sameboy.android.NativeBridge`. `long ctx` is an opaque native `Emulator*`.

```java
long   nativeCreate(int model, byte[] rom, byte[] sav, AssetManager assets);
void   nativeStart(long ctx, Surface surface);   // start render+audio+emu threads
void   nativeStop(long ctx);                      // stop threads, keep state
void   nativePause(long ctx, boolean paused);
void   nativeSetKey(long ctx, int gbKeyIndex, boolean pressed);
void   nativeReset(long ctx);
byte[] nativeSaveBattery(long ctx);               // null if none
void   nativeDestroy(long ctx);
```

Threading contract: `nativeSetKey` calls `GB_set_key_state`, which is safe to call from
the UI thread while `GB_run_frame` executes on the emu thread for *input* registers
(matches SDL, which sets keys from its event thread). All other native entry points are
invoked only when the emu thread is paused/stopped or not yet started.

`GB_KEY_*` index mapping (from `Core/joypad.h`): RIGHT 0, LEFT 1, UP 2, DOWN 3, A 4, B 5,
SELECT 6, START 7.

## 5. Directory layout

```
Android/
├── build.gradle              # root Gradle (AGP, ndkBuild wiring, boot-ROM task)
├── settings.gradle
├── gradle/ + gradlew         # wrapper (pinned Gradle version)
├── jni/
│   ├── Android.mk            # libsameboy_core.so = Core/*.c + bridge/*.c
│   ├── Application.mk        # APP_ABI := all ; APP_PLATFORM := android-26
│   ├── sameboy_jni.c         # JNI entry points (§4)
│   ├── emulator.c/.h         # GB lifecycle, threads, callbacks
│   ├── render_gles.c/.h      # EGL/GLES2 blit
│   ├── audio_aaudio.c/.h     # AAudio driver
│   └── ring_buffer.c/.h
└── app/
    └── src/main/
        ├── AndroidManifest.xml
        ├── java/io/sameboy/android/*.java
        ├── assets/bootroms/*.bin   # generated at build time
        └── res/                    # icons, layouts, control art
```

`Android.mk` includes the same Core source list the top-level build/libretro uses,
compiled with `-DGB_INTERNAL -DGB_VERSION=... -DANDROID` and linked against
`-lEGL -lGLESv2 -laaudio -landroid -llog`.

## 6. Data flow (one frame)

1. Emu thread: `GB_run_frame` renders into `back_buffer`, emits samples → ring (blocks if full).
2. On the frame's vblank: `vblank_cb` swaps buffers, sets `frame_dirty`.
3. Render thread: sees `frame_dirty`, uploads `front_buffer` to the texture, draws, swaps (vsync).
4. AAudio callback: pulls samples from the ring → device; signals emu producer to continue.
5. Touch: overlay press → `nativeSetKey` → `GB_set_key_state` (reflected next `GB_run_frame`).

## 7. Build system

### 7.1 Gradle + ndk-build

`app/build.gradle` uses `externalNativeBuild { ndkBuild { path "../jni/Android.mk" } }`,
`ndk { abiFilters "arm64-v8a","armeabi-v7a","x86_64","x86" }`, `minSdk 26`, a recent
`compileSdk`/`targetSdk`. No CMake. The NDK is invoked by AGP; no manual `ndk-build` calls.

### 7.2 `VERSION`

`GB_VERSION` is passed to the compiler from `version.mk` (read by a Gradle task or hard
wired via `buildConfigField` + `-DGB_VERSION`), matching how the top-level Makefile injects it.

### 7.3 Boot ROM generation

A Gradle task `generateBootRoms` runs **before** `mergeAssets`:
- Invokes the repo-root `make bootroms` (produces `build/bin/BootROMs/*.bin`).
- Copies the `*.bin` into `app/src/main/assets/bootroms/`.
- Requires **rgbds** on the build host (already a documented SameBoy build dependency,
  per `README.md`). If rgbds is absent, the task warns and skips; the app then runs
  boot-ROM-less (still playable). This keeps `assembleDebug` from hard-failing on a box
  without rgbds while making authentic boot ROMs the default when the tool is present.

## 8. Error handling

- `GB_load_rom_from_buffer` failure (bad/empty file) → JNI returns 0 `ctx`; Java shows a
  dialog and returns to the launcher.
- Missing boot ROM asset → log + boot-ROM-less run (no crash).
- EGL/AAudio init failure → surfaced via `__android_log_print` (`GB_log` bridged to
  logcat) and a Java error dialog; the activity finishes cleanly.
- Surface destroyed mid-run → `nativeStop` joins threads before returning so no thread
  touches a freed `ANativeWindow`.
- Battery flush is best-effort; failures logged, never crash gameplay.

## 9. Testing & verification strategy

What I can verify in this environment (Linux, NDK/SDK bootstrapped into `~/Android`,
no attached device):

- **Builds:** `./gradlew assembleDebug` produces an APK; `libsameboy_core.so` is present
  and loads (`unzip -l`, `readelf -d` for the four ABIs, no missing `NEEDED`).
- **Core compiles for Android** across all four ABIs via ndk-build (compile + link).
- **Boot ROM task** produces the expected `*.bin` assets (with rgbds built/fetched in
  userland for the host).
- **JNI symbol table:** `nativeCreate`/`nativeStart`/… are exported with correct mangled
  names (`nm`), so `RegisterNatives`/name-mangling mismatches are caught at build.
- **Host-side unit checks** where feasible: `rgb_encode` packing and ring-buffer
  producer/consumer logic compiled and exercised as a tiny host C test (x86_64), since
  they're platform-independent C.

What requires **your** hardware (documented as a smoke checklist I hand over):

- Install on a device/AVD (`adb install`), open a `.gb`/`.gbc` ROM, confirm: picture
  renders, audio plays, touch controls move the game, `.sav` persists across relaunch.
  (No device/emulator is available here, so "pixels on screen" is verified by you.)

## 10. Resolved decisions & residual risks

Decisions (confirmed 2026-07-01):
- **Bundle boot ROMs: YES.** SameBoy's own Expat-licensed boot ROMs (built from
  `BootROMs/*.asm`) ship in the APK. Authentic boot animation by default.
- **minSdk 26 + AAudio: YES.** Pure-C audio, no new deps, ~97% device coverage.
- **Application ID: `io.sameboy.android`.**

Residual risks:
- **rgbds on the build host** — required to build the bundled boot ROMs. Mitigated by
  building/fetching rgbds in userland at build time + graceful skip (boot-ROM-less run)
  if it is genuinely unavailable, so `assembleDebug` never hard-fails on its absence.
- **`GB_set_key_state` cross-thread** — SDL sets keys from a non-emu thread already; if a
  data race is observed under TSAN we route key events through an atomic bitmask applied
  at the top of `GB_run_frame`. Low risk.

## 11. Parity roadmap (context; each later item = its own spec)

| M  | Milestone         | Delivers |
|----|-------------------|----------|
| M1 | Foundation        | This document — boots a ROM, playable. |
| M2 | State & session   | Save-state slots, in-game menu, pause/turbo/rewind, model select. |
| M3 | Library           | ROM browser (`GBROMManager` port), zip reader, recents/favorites. |
| M4 | Settings          | Full settings surface mirroring `GBSettingsViewController`. |
| M5 | Color & theme     | DMG palette editor/picker, app themes. |
| M6 | Physical input    | External gamepad mapping, haptics/rumble. |
| M7 | Peripherals       | Game Boy Printer feed, Camera (`Camera2`→`GB_camera`). |
| M8 | Link cable        | Serial over local network/Bluetooth. |
| M9 | Ship              | Packaging, signing, GitLab CI ABIs mirroring `libretro/gitlab-ci.yml`. |

## 12. Definition of done (M1)

- `Android/` sub-project builds a debug APK via Gradle for all four ABIs with no Core edits
  (or only additive `#ifdef` guards).
- APK contains `libsameboy_core.so` (×4 ABIs) that loads with all `NEEDED` libs resolved,
  plus boot-ROM assets.
- Host C tests for `rgb_encode` + ring buffer pass.
- A written smoke checklist is delivered for on-device verification (render/audio/input/save).
- Design and code follow SameBoy's C style (`Core`/bridge) and standard Java conventions
  (app layer); no CMake, no new runtime deps.
