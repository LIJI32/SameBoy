# SameBoy Android M9 — Ship Implementation Plan

Executed **inline** (packaging/CI: single-file edits + Pillow-generated binary icon assets +
manifest wiring — tightly coupled, not suited to verbatim-transcription subagents). Review
discipline kept: self-review, a reviewer pass on build.gradle/CI, on-device smoke.
Spec: `specs/2026-07-01-android-m9-ship.md`.

Already landed (real-hardware find): 16 KB alignment `-Wl,-z,max-page-size=16384` (b4b2574).

## Task 1 — build.gradle: versionCode + signing + R8
`Android/app/build.gradle`:
- Derive `versionCode` from `sameboyVersion`: parse `major.minor.patch` →
  `major*10000 + minor*100 + patch` (1.0.3 → 10003; fallback 1 if unpar_seable).
- Add a `signingConfigs.release` from env (`SAMEBOY_KEYSTORE`, `_KEYSTORE_PASSWORD`,
  `_KEY_ALIAS`, `_KEY_PASSWORD`); if `SAMEBOY_KEYSTORE` unset/missing, reuse the debug
  signing config (`signingConfigs.debug`) so `assembleRelease` always signs + installs.
- `release { minifyEnabled true; shrinkResources true; proguardFiles ...; signingConfig ... }`.
- New `Android/app/proguard-rules.pro`: keep `NativeBridge` + all `native <methods>`.

## Task 2 — Launcher icon (Pillow, from iOS/logo@3x.png)
Generate + commit:
- `mipmap-{m,h,xh,xxh,xxxh}dpi/ic_launcher_foreground.png` — logo centered in the adaptive
  108dp canvas, ~60% scale, transparent pad (108/162/216/324/432 px).
- `mipmap-{...}/ic_launcher.png` + `ic_launcher_round.png` — logo pre-composited on the bg
  (48/72/96/144/192 px; round = circle-masked).
- `values/ic_launcher_background.xml` — `<color name="ic_launcher_background">` (deep slate
  that sets off the light-grey DMG).
- `mipmap-anydpi-v26/ic_launcher.xml` + `ic_launcher_round.xml` — `<adaptive-icon>`.
- `app/store/ic_launcher-512.png` — 512² store icon (not in APK).
- Manifest `<application>`: `android:icon="@mipmap/ic_launcher"` +
  `android:roundIcon="@mipmap/ic_launcher_round"`.

## Task 3 — generateBootRoms hardening (build.gradle)
- Probe the full toolchain: `['rgbasm','rgblink','rgbgfx','rgbfix'].every { exe -> path on
  PATH }` (all four), not just `rgbasm`.
- Filter empty PATH entries: `path.split(':').findAll { it }`.
- Unchanged behavior when fully present / fully absent.

## Task 4 — CI (.github/workflows/android.yml)
GitHub Actions (repo uses Actions, not GitLab). Trigger on push/PR touching `Android/**` +
`workflow_dispatch`. `ubuntu-latest`, JDK 17, Android SDK + NDK 26.3.11579264. Steps:
host tests → `assembleDebug` (boot-ROM-less; gate warns+skips) → verify 4 ABIs each
16 KB-aligned (`llvm-readelf -l … | grep LOAD` → 0x4000) → upload APK. Release job guarded
on secrets present (tags) → `assembleRelease` with keystore from secrets.

## Task 5 — Docs (Android/README.md)
Refresh the M1-era header to the full M1–M9 feature set; add release build + signing env
vars + 16 KB note + CI + distribution (sideload; F-Droid-friendly, all OSS, boot ROMs from
source).

## Task 6 — Integration / verify
Host tests green; `assembleDebug` + `assembleRelease` succeed; release APK signed (debug
fallback) + installs/runs on Waydroid; 4 ABIs 16 KB-aligned in both; versionCode 10003 /
versionName 1.0.3 via aapt2; launcher icon shows the DMG art; CI YAML valid.

## Acceptance
Signed release APK/AAB builds for all ABIs, installs + runs; version derives from
version.mk; launcher icon present; CI builds 4 ABIs + guards the 16 KB fix. Core unmodified.
