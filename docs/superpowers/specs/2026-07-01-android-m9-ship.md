# SameBoy Android — M9: Ship (Design)

Status: **proposed** · Date: 2026-07-01 · Branch: `android-frontend`
Builds on M1–M8. Parent: `plans/2026-07-01-android-parity-roadmap.md` (M9).

## 1. Goal & scope

Make the Android app **release-ready**: a signed release build, correct versioning, a real
launcher icon, hardened build scripts, and CI that builds all four ABIs. Distribution is
**sideload APK + F-Droid-friendly** (open-source, boot ROMs built from source).

Adjusted to the repo's actual state (differs from the roadmap's aspirational notes):
- CI is **GitHub Actions** (`.github/workflows/libretro.yml`, `sanity.yml`) — **not GitLab**.
  Add a GitHub Actions Android workflow, matching the existing convention.
- The app has **no launcher icon** (`res/` has no `mipmap*`, manifest sets no `android:icon`)
  — it shows the generic default. This milestone adds one.
- `versionCode` is hardcoded `1`; `versionName` already derives from `version.mk` (1.0.3).

Real-hardware finding already fixed (commit b4b2574): **16 KB page alignment**
(`-Wl,-z,max-page-size=16384`) — required by Android 15+/Play. Done; M9 verifies it in CI.

## 2. Release build type + signing (`Android/app/build.gradle`)

### versionCode from version.mk
Derive a monotonic integer from `VERSION` (e.g. `1.0.3` → `10003` via
`major*10000 + minor*100 + patch`). Keep `versionName = sameboyVersion`.

### Signing
A `release` `signingConfig` sourced from **environment variables** (never committed):
`SAMEBOY_KEYSTORE` (path), `SAMEBOY_KEYSTORE_PASSWORD`, `SAMEBOY_KEY_ALIAS`,
`SAMEBOY_KEY_PASSWORD`. **Fallback to the debug keystore** when those are unset, so
`assembleRelease` always yields an installable APK locally and in CI without secrets. (A
debug-signed "release" is clearly not for store upload, but it's testable; CI injects real
secrets when present.)

### R8 / minify
`release { minifyEnabled true; shrinkResources true; proguardFiles getDefaultProguardFile(
'proguard-android-optimize.txt'), 'proguard-rules.pro'; signingConfig ... }`.
**Critical:** SameBoy uses **name-based JNI binding** (`Java_io_sameboy_android_NativeBridge_
native*`), so R8 must NOT rename/remove `NativeBridge` or its native methods. `proguard-
rules.pro`:
```
-keep class io.sameboy.android.NativeBridge { *; }
-keepclasseswithmembernames class * { native <methods>; }
```
Activities/Application are kept via the manifest automatically. **Verify by smoke-testing the
release APK on-device** — if the menu/ROM-load/JNI all work, minify is proven safe; else fall
back to `minifyEnabled false` (documented).

## 3. Launcher icon (`Android/app/src/main/res/`)

Source: `iOS/logo@3x.png` (384×384, the SameBoy DMG illustration). minSdk 26 ⇒ **adaptive
icons** are always available.
- **Foreground** `mipmap-*/ic_launcher_foreground.png`: the DMG logo centered in the 108dp
  canvas within the ~66dp safe zone (scale the 384px art to ~62% and pad transparent), at
  mdpi/hdpi/xhdpi/xxhdpi/xxxhdpi (108/162/216/324/432 px).
- **Background** `values/ic_launcher_background.xml`: a solid brand color (the logo is light
  grey, so a deeper neutral/blue makes it pop — pick a token that passes as a launcher bg).
- **Adaptive XML** `mipmap-anydpi-v26/ic_launcher.xml` + `ic_launcher_round.xml`:
  `<adaptive-icon>` with `@drawable/ic_launcher_background` + `@mipmap/ic_launcher_foreground`.
- **Legacy raster** `mipmap-*/ic_launcher.png` (+ `_round`): the logo on the bg, pre-composited,
  at 48/72/96/144/192 px (some launchers/Recents use the raster).
- **Store icon** `Android/app/store/ic_launcher-512.png` (512×512) for the F-Droid/Play listing
  (not bundled in the APK).
- Manifest: `android:icon="@mipmap/ic_launcher"` + `android:roundIcon="@mipmap/ic_launcher_round"`.

Generated deterministically with Pillow (source art is in-repo). Assets are committed.

## 4. Build-script hardening (`Android/app/build.gradle` generateBootRoms)

Roadmap M9 tech-debt: the rgbds gate probes only `rgbasm`. Harden:
- Probe the **full toolchain** (`rgbasm`, `rgblink`, `rgbgfx`, `rgbfix`) — a partial install
  now skips gracefully instead of failing loudly mid-`make`.
- Skip **empty PATH entries** when searching (`"".split(':')` / trailing `:` → `""` → `new
  File("", exe)` edge cases).
- Behavior unchanged when rgbds is fully present (boot ROMs built) or fully absent (warn +
  skip, app runs boot-ROM-less).

## 5. CI (`.github/workflows/android.yml`)

A GitHub Actions workflow (mirrors the repo's existing YAML style):
- Trigger: `push`/`pull_request` touching `Android/**` (+ manual `workflow_dispatch`).
- `ubuntu-latest`; set up JDK 17 (`actions/setup-java`) + Android SDK/NDK (`android-actions/
  setup-android` or the SDK cmdline-tools) with `ndkVersion 26.3.11579264`.
- Steps:
  1. **Host tests** — `Android/jni/test/run_host_tests.sh` (fast, no SDK; the C bridge suite).
  2. **Build** — `./gradlew :app:assembleDebug` (all 4 ABIs). rgbds absent in CI ⇒ boot-ROM-less
     (the gate warns + skips — acceptance: build still succeeds).
  3. **Verify** — assert 4 `libsameboy_core.so` in the APK and each is **16 KB-aligned**
     (`llvm-readelf -l … | grep LOAD` → `0x4000`), guarding the b4b2574 fix in CI.
  4. **Artifact** — upload the debug APK.
  - **Release job** (only if signing secrets are present, e.g. on tags): `assembleRelease` with
    the keystore from secrets → upload the signed APK. Guarded so forks/PRs without secrets
    still pass (debug job always runs).

## 6. Docs (`Android/README.md`)

Extend the existing Android README with: build (debug/release), the signing env vars, the
16 KB-alignment note, CI overview, and distribution (sideload APK; F-Droid-friendly — 100%
open source, boot ROMs built from source; no proprietary blobs).

## 7. Testing / acceptance

- **Host tests** green (unchanged suite).
- `./gradlew :app:assembleDebug` and `:app:assembleRelease` both succeed; the **release APK is
  signed** (debug-key fallback) and **installs + runs** on-device (Waydroid + the arm64
  tablet), menu/ROM-load/settings all work ⇒ **R8 keep-rules correct**.
- `versionCode` in the built APK = derived value (10003); `versionName` = 1.0.3 (via aapt2).
- All 4 ABIs present and **16 KB-aligned** in both debug and release APKs.
- Launcher icon shows the SameBoy DMG art (not the generic default) in the launcher.
- CI workflow is valid YAML and its steps mirror the local build/verify.
- `Core/` unmodified; no new Gradle dependency (Pillow is host-side asset tooling, not an app dep).

## 8. Out of scope (documented)
- Actual Play Store / F-Droid submission (needs real signing keys + store accounts).
- Bluetooth link (M8 later slice); portrait-optimized large-screen layout (noted from the
  SM-T505 test — a UX polish follow-up).
