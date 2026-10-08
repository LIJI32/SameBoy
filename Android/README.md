# SameBoy for Android

A native standalone Android frontend for SameBoy. The portable C `Core` is reused
**unmodified** and driven through a thin C/JNI bridge; the app layer (activities,
`SurfaceView`, touch controls, storage, settings) is Java. It boots Game Boy / Game Boy
Color ROMs and is feature-complete across the parity program (M1–M9):

- **Emulation** — render (EGL/GLES2, integer-scaled) + AAudio + touch controls + battery saves.
- **State & session** — save states (4 slots + thumbnails), turbo, rewind, model switch.
- **Library** — ROM folder scan (SAF), recents, background metadata read.
- **Settings** — color correction, palette, audio, controls; SharedPreferences-backed.
- **Color & theme** — DMG palette picker (built-ins + custom editor), light/dark app theme.
- **Physical input** — hardware gamepad (remappable), rumble, on-screen-control auto-hide.
- **Peripherals** — Game Boy Printer feed (save/share) + Game Boy Camera (Camera2).
- **Link cable** — TCP serial link between two instances (byte-level master/slave bridge).

See `docs/superpowers/plans/2026-07-01-android-parity-roadmap.md` for the full program.

## Layout

```
Android/
  jni/            C/JNI bridge (reuses ../Core, unmodified)
    Android.mk    ndk-build: libsameboy_core.so = Core/*.c (17) + bridge
    Application.mk APP_ABI = all 4, APP_PLATFORM = android-26, APP_STL = none
    ring_buffer.*  SPSC audio sample ring (blocking = audio-paced emulation)
    emulator.*     owns GB_gameboy_t, callbacks, double-buffered framebuffer
    audio_aaudio.* AAudio output stream (drains the ring)
    render_gles.*  EGL/GLES2 render thread (integer-scaled blit from a locked copy)
    session.*      ties emu thread + render + audio into one running session
    sameboy_jni.c  JNI entry points (NativeBridge)
    test/          host unit tests (ring buffer, emulator-on-a-real-ROM)
  app/src/main/
    java/io/sameboy/android/  MainActivity, EmulatorActivity, EmulatorSurfaceView,
                              TouchOverlayView, SaveStore, NativeBridge
    assets/bootroms/*.bin     generated at build time from ../BootROMs/*.asm
```

## Build

Prerequisites:
- Android SDK (platform 34, build-tools 34, NDK 26.3.11579264) — point `local.properties`
  `sdk.dir` at it (see `local.properties.example`).
- A **JDK 17** for Gradle (AGP 8.5.2 / Gradle 8.9 do not run on JDK 25+). If your default
  `java` is newer, set `org.gradle.java.home` in `gradle.properties` or export `JAVA_HOME`.
- **rgbds** on `PATH` (for boot ROMs; already a documented SameBoy build dependency). If
  rgbds is absent, the build still succeeds and the app runs **boot-ROM-less** (no boot
  animation), just without the bundled boot ROMs.

```sh
cd Android
echo "sdk.dir=$HOME/Android" > local.properties      # adjust to your SDK path
./gradlew :app:assembleDebug
```

Output: `app/build/outputs/apk/debug/app-debug.apk` (contains `libsameboy_core.so` for
`arm64-v8a`, `armeabi-v7a`, `x86_64`, `x86`, plus `assets/bootroms/*.bin`).

### Release build

```sh
cd Android
./gradlew :app:assembleRelease
```

`assembleRelease` is minified (R8 + resource shrink) and signed. Signing is sourced from
environment variables so **no keystore is committed**:

| Env var | Meaning |
| --- | --- |
| `SAMEBOY_KEYSTORE` | path to the release keystore (`.jks`) |
| `SAMEBOY_KEYSTORE_PASSWORD` | keystore password |
| `SAMEBOY_KEY_ALIAS` | signing key alias |
| `SAMEBOY_KEY_PASSWORD` | key password |

When `SAMEBOY_KEYSTORE` is unset (or missing) the build **falls back to the debug key** so
the release APK still installs for local testing — that build is not for store upload.
R8 keeps `NativeBridge` + all `native` methods (`proguard-rules.pro`); the JNI binds by
name, so those must not be renamed.

`versionName` and a monotonic `versionCode` both derive from repo-root `version.mk`
(`1.0.3` → `versionCode 10003`). Native libs are 16 KB-page-aligned (Android 15+/Play).

### CI

`.github/workflows/android.yml` (GitHub Actions) runs the host tests, builds the debug APK
for all four ABIs (boot-ROM-less — rgbds isn't installed in CI), and verifies every
`libsameboy_core.so` is 16 KB-aligned. A tag build additionally produces a signed release
APK when the signing secrets are configured.

### Distribution

Sideload the APK (`adb install -r …`), or build from source. The app is **F-Droid-friendly**:
100% open source, no proprietary blobs — the boot ROMs are SameBoy's own open-source ROMs,
built from `../BootROMs/*.asm` at build time.

## Host unit tests (no device required)

```sh
jni/test/run_host_tests.sh
```

Compiles and runs the platform-independent C on the build host:
- **ring_buffer** — FIFO, underrun zero-fill, blocking-pacing producer/consumer, and the
  teardown (shutdown/flush) paths.
- **emulator** — builds a synthetic ROM, runs 60 frames, asserts a 160×144 fully-opaque
  framebuffer and that audio samples were produced (draining the ring each frame).

Expected final line: `ALL HOST TESTS PASSED`.

## Verification

**Host tests** (above) cover the C bridge: ring buffer, emulator-on-a-real-ROM, session
concurrency (self-park, no deadlock), and the link-cable two-core byte-exchange clocking.

**On-device (Waydroid x86_64 + a physical Samsung SM-T505 / arm64 / Android 16):**
- Boots + renders Game Boy / Game Boy Color, live 48 kHz AAudio, touch + hardware input.
- `dmg-acid2` renders pixel-correct on arm64 (Core + GLES pipeline verified).
- Commercial CGB games run in color on arm64 (e.g. Pokémon Crystal — CGB-only, MBC3+RTC+
  battery; Pokémon Pinball — rumble cart; Tetris DX). Save states persist to storage
  (thumbnail + timestamp). R8 release build runs on both ABIs (JNI resolves by name).
- Link cable: two instances reach **Connected** over TCP (verified arm64 tablet ↔ x86_64
  emulator relayed through a host); graceful teardown on peer loss (no crash).

To reproduce: `adb install -r app/build/outputs/apk/debug/app-debug.apk`, launch **SameBoy**,
**Open ROM** or **Import folder**, pick a `.gb`/`.gbc`, and play.

### Known limitations / follow-ups
- **Large-screen orientation:** Android 15+/tablets apply the "ignore orientation request"
  policy, so `sensorLandscape` only takes effect when the device is physically rotated;
  a portrait-optimized large-screen layout is a UX follow-up.
- **Rumble motor** is unverified where no vibrator exists (the SM-T505 has none); the
  callback path is exercised safely. **Game Boy Camera** live capture needs a Camera ROM.
- **Link cable** is TCP-only (Bluetooth is a later slice); real-time throughput is bounded
  by the per-frame slave poll (fine for turn-based trades; a faster pump is a follow-up).
  A full two-instance in-game trade over a high-latency relay is throughput-limited.
- **Audio-device-disconnect** (headphone unplug) reconnection handling is not yet wired.
