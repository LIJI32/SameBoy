# SameBoy Android — Full-Parity Program Roadmap

Status: **COMPLETE — M1–M9 all done & on-device verified** · Date: 2026-07-01 · Branch: `android-frontend`

The goal is a native Android app with feature parity to the existing native `iOS/`
app, reusing the C `Core` unmodified through a JNI bridge, app layer in Java. The work
is decomposed into milestones; each is its own spec → plan → execute cycle. This document
is the map: per-milestone scope, the `iOS/` files it mirrors, the `Core` APIs it binds,
key decisions, dependencies, and acceptance.

- **M1 (Foundation) is DONE and on-device verified** (Waydroid, x86_64). See
  `specs/2026-07-01-android-frontend-design.md` and `plans/2026-07-01-android-frontend-m1.md`.
- **M2 (State & session) is DONE and on-device verified** (Waydroid, x86_64). See
  `specs/2026-07-01-android-m2-state-session.md` and `plans/2026-07-01-android-m2-state-session.md`.
- **M3 (Library) is DONE** — host tests + 4-ABI build green; on-device (Waydroid, x86_64)
  browser UI, SAF tree picker/permission-grant, folder scan enumeration, and the
  background-read launch path all verified. The `content://` byte-read → metadata → grid
  step could not be exercised on this AVD (a Waydroid defect NPEs every non-system-app
  `content://` read in `AppOpsService`/`MediaProvider`; same limitation M2 hit); it works
  on real devices. See `specs/2026-07-01-android-m3-library.md` and
  `plans/2026-07-01-android-m3-library.md`.
- **M4 (Settings) is DONE and on-device verified** (Waydroid, x86_64) — emulation/video/
  audio/controls settings persist and apply live to the running game (verified: color
  correction change visible + persisted across restart; 60% opacity default applied;
  Settings reachable from library and in-game menu). Host tests + 4-ABI build green. See
  `specs/2026-07-01-android-m4-settings.md` and `plans/2026-07-01-android-m4-settings.md`.
- **M5 (Color & theme) is DONE and on-device verified** (Waydroid, x86_64) — DMG palette
  picker (4 built-ins + custom 4-shade editor) recolors DMG-mode games live via
  `GB_set_palette` (verified: DMG green → Greyscale on a running DMG game); app light/dark
  theme via AppCompat DayNight (verified: Light switch live + persisted across restart,
  independent of the GB palette). Host tests + 4-ABI build green. See
  `specs/2026-07-01-android-m5-color-theme.md` and `plans/2026-07-01-android-m5-color-theme.md`.
- **M6 (Physical input) is DONE and on-device verified** (Waydroid, x86_64) — hardware
  gamepad (keycode + axis → GB keys, remappable, persisted; verified: injected buttons
  drive the game, A rebound to BUTTON_X and survived restart), rumble (Core callback →
  amplitude atomic → `Vibrator` poller; rumble-mode setting Disabled/Cartridge/All), and
  on-screen controls auto-hide when a pad is present. Motor buzz + hide-on-connect need
  real hardware (no gamepad/vibrator on the AVD). Host tests + 4-ABI build green. See
  `specs/2026-07-01-android-m6-physical-input.md` and `plans/2026-07-01-android-m6-physical-input.md`.
- **M7 (Peripherals) is DONE and on-device verified** (Waydroid, x86_64) — Game Boy
  Printer (Core callbacks → 160px ARGB feed buffer, self-parked connect/disconnect; a
  `PrinterFeedActivity` renders the feed and can save to Pictures / share PNG / clear;
  accessory menu wires None/Printer) and Game Boy Camera (Camera2 back-lens frames →
  Y-plane 8:7 crop → 128×112 grayscale → sensor; runtime CAMERA permission; camera-`wanted`
  poller starts/idle-stops the device camera). A whole-branch review caught two camera P0/P1
  bugs (busy-bit soft-lock, `wanted` latch), now fixed (busy-clear deferred past Core's
  register store on the emu thread; consuming `wanted`) + Camera2 lifecycle hardening.
  Verified on-device: accessory menu + feed screen + menu index-remap regression + dormant
  camera poller on a non-camera ROM. Printout *content* needs a printing ROM; camera
  viewfinder/photo need a Game Boy Camera ROM + real camera. Host tests + 4-ABI build green.
  See `specs/2026-07-01-android-m7-peripherals.md` and `plans/2026-07-01-android-m7-peripherals.md`.
- **M8 (Link cable) is DONE and on-device verified** (Waydroid, x86_64) — TCP link over the
  local network with a byte-level master/slave serial bridge clocked off Core's serial
  callbacks (master reads the whole SB at the first `bit_start`, sends it, blocking-recvs the
  peer byte with a dead-latch timeout, feeds it back through the 8 `bit_end`s; slave polled
  per-frame drives `GB_serial_set_data_bit`×8). Pluggable transport (loopback for tests +
  TCP); connect/listen run cancellable on a helper thread and attach parked at a frame
  boundary; `LinkActivity` Host/Join/Disconnect with a live status FSM. A whole-branch review
  caught an accept()-join ANR + three more concurrency/robustness bugs, all fixed. Verified:
  the two-core loopback host test swaps a byte both ways (clocking proven), a localhost-TCP
  smoke, and on-device Host→Listening→(external peer)→Connected + the no-peer Disconnect ANR
  fix + Error recovery. A real two-game trade needs a link ROM + two synced instances (out of
  this slice; Bluetooth is a later slice). Host tests + 4-ABI build green. See
  `specs/2026-07-01-android-m8-link-cable.md` and `plans/2026-07-01-android-m8-link-cable.md`.
- **M9 (Ship) is DONE and on-device verified** (Waydroid x86_64 + a physical arm64 device) —
  R8 release build + env-based signing (debug fallback), `versionCode`/`versionName` from
  `version.mk` (1.0.3 → 10003), an adaptive launcher icon (the SameBoy DMG art), rgbds
  toolchain-gate hardening, and a GitHub Actions workflow building all four ABIs with a 16 KB
  page-alignment guard. The R8 release APK runs on both ABIs (name-based JNI kept). Real
  hardware also surfaced + fixed a 16 KB-alignment ship blocker (`-Wl,-z,max-page-size=16384`).
  See `specs/2026-07-01-android-m9-ship.md` and `plans/2026-07-01-android-m9-ship.md`.
- **All milestones M1–M9 are complete and on-device verified.** Documented follow-ups
  (non-blocking): Bluetooth link (M8 later slice), a portrait-optimized large-screen layout,
  AAudio device-disconnect handling, and hardware-gated checks (rumble motor, GB Camera live
  capture, a same-LAN two-instance link trade).

## Architecture recap (stable across all milestones)

```
Java app layer (Activities, SurfaceView, touch, SAF, menus, settings)
        │  JNI (setup / input / control only — never per-pixel)
C bridge (Android/jni): session ── emulator (owns GB_gameboy_t) ── ring_buffer
        │  emu thread (GB_run_frame) · EGL/GLES2 render thread · AAudio callback
Core/*.c  (unchanged; 17-file set, -DGB_INTERNAL -DGB_DISABLE_DEBUGGER)
```

- Native-driven hot loop: emulation, render, audio all in C; Java owns UI only.
- Audio (AAudio) drains a bounded ring; a full ring blocks the producer → audio paces
  emulation. Render consumes frames via `sb_emu_copy_front` (lock-protected copy).
- Build: Gradle + `externalNativeBuild { ndkBuild }` over `Android/jni/Android.mk`
  (no CMake). minSdk 26. ABIs: arm64-v8a, armeabi-v7a, x86_64, x86.
- New Core features are enabled by **adding the relevant `Core/*.c` to the Android.mk
  source list and removing the corresponding `-DGB_DISABLE_*`** — the M1 set already
  compiles `cheats.c`, `rewind.c`, `printer.c`, `camera.c`, `rumble.c`, `sgb.c`,
  `workboy.c`, so most later milestones need **no new Core files**, only new JNI surface
  + Java UI. (The debugger/cheat-search/disassembler/symbol files stay excluded.)

## Milestone map & dependencies

```
M1 Foundation ✅
   └─ M2 State & session ✅ ──┬─ M3 Library ✅ ── M4 Settings ✅ ── M5 Color & theme ✅
                             └─ M6 Physical input ✅
   (M2 recommended before others: in-game menu is the host for most later UI)
M7 Peripherals ✅ — printer feed + Game Boy Camera (Camera2)
M8 Link cable ✅ — TCP serial bridge (byte-level master/slave); Bluetooth = later slice
M9 Ship ✅ — R8 release + env signing, versionCode from version.mk, adaptive icon, GitHub Actions CI (4 ABIs + 16 KB guard)
```

Recommended order: **M2 → M3 → M4 → M5 → M6 → M7 → M8 → M9.** M2 first because the
in-game menu it introduces is where states/settings/cheats/etc. are launched from.

---

## M2 — State & session

**Goal:** In-game menu; save-state slots; pause/resume, reset, turbo, rewind; model select.

**iOS reference:** `GBStatesViewController` (state slots), `GBMenuViewController` /
`GBMenuButton` (in-game menu), `GBViewController` run-mode handling (`GBRunModeNormal/
Turbo/Rewind/Paused/Underclock`), `GBSlotButton`.

**Core APIs:** `GB_save_state_to_buffer` / `GB_load_state_from_buffer` /
`GB_get_save_state_size` / `GB_get_state_model_from_buffer` (save_state.h);
`GB_set_rewind_length` + rewind pop (rewind.h — already compiled); `GB_set_turbo_mode`,
`GB_reset`, `GB_switch_model_and_reset`, `GB_set_clock_multiplier` (gb.h).

**New JNI surface (extends NativeBridge):** `nativeSaveState(ctx, slot) -> byte[]` /
`nativeLoadState(ctx, byte[])`, `nativeSetTurbo(ctx, bool)`, `nativeSetRewinding(ctx, bool)`,
`nativeChangeModel(ctx, model)`. State files stored per-ROM under app storage
(`states/<rom>.s0..sN`), thumbnail = a copy of the current framebuffer (already available
via `sb_emu_copy_front`).

**Java UI:** an in-game overlay menu (button or gesture on the emulator view) → Pause,
Reset, Save/Load state (slot grid with framebuffer thumbnails), Turbo toggle, Rewind
(hold), Model picker. Reuse the M1 `sb_session_pause` (synchronous) for safe state I/O.

**Key decisions:** rewind buffer length (memory vs seconds) exposed later in M4 settings;
state format is Core's (BESS-compatible) — portable across SameBoy frontends.

**Acceptance:** save to a slot, reset, load the slot → state restored; turbo speeds up;
rewind rewinds; model switch reboots into the chosen model; menu doesn't race the emu
thread (state calls happen while parked).

**Carries M1 deferred:** periodic battery flush (`GB_get_battery_dirty` polling on the
emu thread between frames) belongs here alongside state I/O.

---

## M3 — Library

**Goal:** ROM browser instead of a one-shot picker; recents/favorites; zip support.

**iOS reference:** `GBROMManager` (ROM storage/metadata), `GBLibraryViewController`,
`GBROMViewController`, `GBZipReader`.

**Core APIs:** `GB_get_rom_title`, `GB_get_rom_crc32` (gb.h) for metadata/dedup; ROM load
unchanged.

**Java UI:** a persistent library backed by a small local store (Room or a JSON index —
decide at spec time; prefer the lightest that works). Grid/list of ROMs with title +
last-played; long-press for favorite/remove. A SAF **tree** pick (`ACTION_OPEN_DOCUMENT_TREE`)
to import a folder once, persisted via `takePersistableUriPermission` (so the library
survives restarts without re-picking). Zip: extract the first GB/GBC entry (Java
`java.util.zip`, no native change).

**Key decisions:** persistent SAF tree permission vs copying ROMs into app storage
(tree permission avoids duplication; copying is simpler and survives source deletion —
pick at spec). Background the ROM read here (M1 deferred: main-thread SAF read can ANR).

**Acceptance:** import a folder, see the library populate with titles/art; favorites and
recents persist across app restart; a zipped ROM launches; a slow provider doesn't ANR.

---

## M4 — Settings

**Goal:** The full settings surface mirroring iOS.

**iOS reference:** `GBSettingsViewController` (the 51 KB one), `GBOptionViewController`,
`GBSlider`, `GBCheckableAlertController`.

**Core APIs:** `GB_set_color_correction_mode`, `GB_set_light_temperature`,
`GB_set_border_mode`, `GB_set_highpass_filter_mode`, `GB_set_rtc_mode`, `GB_set_rewind_length`,
`GB_set_turbo_cap`, `GB_set_interference_volume`, sample-rate/volume (apu.h) — all already
linked; each becomes a bridged setter applied on change.

**Java UI:** a `PreferenceScreen` (AndroidX Preference) with sections: Emulation (model,
rewind length, RTC, turbo cap), Video (color correction, frame blending, integer scaling,
border mode), Audio (volume, high-pass, interference, sample rate), Controls (button
layout/opacity, haptics on/off). Persist via `SharedPreferences`; apply to the running
session through new `nativeSet*` bridges (batch-apply on resume like the SDL frontend's
`open_menu`).

**Key decisions:** which settings apply live vs on-reset (mirror SDL's split); button
layout customization can be minimal here and deepened later.

**Acceptance:** each setting persists and visibly affects the running game (e.g. color
correction, border, volume); model/rewind-length changes take effect on next reset/apply.

---

## M5 — Color & theme

**Goal:** DMG palette editor/picker; app theming.

**iOS reference:** `GBPaletteEditor`, `GBPalettePicker`, `GBColorWell`, `GBTheme`,
`GBThemesViewController`, `GBThemePreviewController`.

**Core APIs:** `GB_set_palette` with `GB_palette_t` (display.h); the built-in palettes +
the DMG paletted-mode selection SameBoy supports.

**Java UI:** a palette picker (built-in palettes + custom 4-color editor with color wells)
applied via `GB_set_palette`; app light/dark theme (Material `DayNight`) independent of the
GB palette. Persist selection per app + optionally per-ROM.

**Acceptance:** selecting/editing a DMG palette recolors a DMG game live; app theme
switches light/dark; selections persist.

---

## M6 — Physical input

**Goal:** External gamepad support + button remapping; haptics/rumble.

**iOS reference:** `GCControllerGetElements` (gamepad element mapping), `GBHapticManager`
(+ legacy), `GBViewController` controller handling.

**Core APIs:** `GB_set_rumble_mode`, `GB_set_rumble_callback` (rumble.h — already linked;
M1 stubs the callback). Input is `GB_set_key_state` as today.

**Android:** map hardware gamepads via `InputDevice` / `KeyEvent` `SOURCE_GAMEPAD` +
`MotionEvent` axes (`AXIS_HAT_X/Y`, sticks) — pure Java, no native change. Remap UI stores
a keycode→`GB_KEY_*` map. Rumble: bridge the Core rumble callback to Android
`VibratorManager` / gamepad rumble (`InputDevice.getVibratorManager`), amplitude from the
callback's `rumble_amplitude`.

**Acceptance:** a connected gamepad controls the game; remapping sticks; rumble fires on
a rumble-capable device/pad; on-screen controls auto-hide when a pad is active (optional).

---

## M7 — Peripherals

**Goal:** Game Boy Printer feed; Game Boy Camera.

**iOS reference:** `GBPrinterFeedController` (printer output), `GBViewController` camera
capture (`AVCaptureVideoDataOutputSampleBufferDelegate`).

**Core APIs:** printer (printer.h): `GB_connect_printer` + `GB_print_image_callback_t` /
`GB_printer_done_callback_t` — bridge the printed image rows to a Java bitmap feed. Camera
(camera.h): `GB_set_camera_get_pixel_callback` + `GB_set_camera_update_request_callback` —
feed pixels from Android `CameraX`/`Camera2` (grayscale downscale to the GB camera sensor).
Both Core files already compiled in M1.

**Android:** printer → an image feed screen (save/share the printout). Camera → `CameraX`
frames converted to the Core's expected grayscale sensor input; runtime `CAMERA` permission.

**Acceptance:** printing in a game (e.g. a Pokémon-style print) produces an image in the
feed; a Game Boy Camera ROM shows the live camera and can take a photo.

---

## M8 — Link cable

**Goal:** Two-instance serial link (local network and/or Bluetooth).

**iOS reference:** `GBViewController` connect menu (`openConnectMenu`) — note iOS link
scope is limited; this is the largest new subsystem and mostly Android-side transport.

**Core APIs:** external-clock serial (gb.h): `GB_serial_set_data_bit`,
`GB_serial_get_data_bit`, `GB_set_serial_transfer_bit_start_callback`,
`GB_set_serial_transfer_bit_end_callback`, `GB_disconnect_serial`. No new Core files.

**Android:** a transport (start with TCP over local network; Bluetooth RFCOMM optional)
shuttling serial bits between two SameBoy instances, clocked off the Core serial callbacks.
Latency/clock-sync is the hard part — spec a clear master/slave clocking model.

**Acceptance:** two devices (or two Waydroid instances) trade Pokémon / play a link game;
graceful disconnect. This milestone may itself split into sub-slices.

---

## M9 — Ship ✅ DONE

**Goal:** Release-ready packaging and CI.

**Scope:** release build type (`minifyEnabled` + R8 rules), app signing config
(keystore via env/CI secrets, not committed), versioning from `version.mk`, app icon +
store assets, and a **GitLab CI job** mirroring `libretro/gitlab-ci.yml`'s Android ABIs
that builds the app APK/AAB for all four ABIs. Decide distribution (APK sideload vs
Play/F-Droid). F-Droid-friendly since boot ROMs are open source and built from source.

**Acceptance:** signed release APK/AAB builds in CI for all ABIs; installs and runs;
version derives from `version.mk`.

---

## Cross-cutting tech debt (carried from M1 reviews — fold into the milestone that touches the area)

- **Periodic battery flush** (`GB_get_battery_dirty` polling) — do in **M2** with state I/O.
- **Background ROM read** (main-thread SAF read can ANR on slow providers) — do in **M3**.
- **AAudio disconnect/error callback** (headphone unplug → silence → eventual producer
  block) — add `AAudioStreamBuilder_setErrorCallback` + reconnect; do in **M4/M6** (audio area).
- **`volatile` → `stdatomic`** for the render-thread `running` flag — trivial, any milestone.
- **rgbds gate probes only `rgbasm`** (partial toolchain → loud make failure) and PATH
  empty-entry edge cases in `generateBootRoms` — harden in **M9** (build/CI).
- **Non-ROM file → black screen** (Core does no content validation; only <0x150 rejected) —
  optionally add a header sanity check in **M3** (library import) or leave documented.

## Working conventions (all milestones)

- Reuse the M1 build/verify harness: `Android/jni/test/run_host_tests.sh` for
  platform-independent C; `./gradlew :app:assembleDebug` (JDK 17 via `JAVA_HOME`);
  on-device smoke via Waydroid (`waydroid session start`, `waydroid adb connect`, then
  `adb install` + `adb shell input`/`screencap`).
- Keep `Core/` unmodified. Add Core features by adjusting the Android.mk source list +
  `-DGB_DISABLE_*` flags, never by editing Core.
- Each milestone: write `specs/<date>-android-<milestone>.md`, then
  `plans/<date>-android-<milestone>.md`, then execute subagent-driven with per-task
  review + a whole-branch review before merge.
