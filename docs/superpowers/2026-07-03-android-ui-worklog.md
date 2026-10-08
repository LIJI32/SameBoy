# SameBoy Android — UI Overhaul Work Log (2026-07-03)

Branch `android-frontend`, 23 code commits `9170cbe..b5fe1cb` (+ 6 spec/plan docs).
Base was `298f9bf` (M1–M9 parity, 1.0.3). Executed via subagent-driven-development
(fresh implementer + reviewer per task); ledger at `.superpowers/sdd/progress.md`.
Everything below is device-verified.

## 1. Cupertino UI redesign (all non-game screens)

Replaced the pure-Java programmatic views with Jetpack Compose in a hand-rolled
iOS look. No Material — foundation only.

- Toolchain: Kotlin 2.0.21 + Compose BOM 2024.09 (`9170cbe`).
- `cupertino/Theme.kt` — iOS system colors (light+dark) + type scale (`8b74cc9`).
- `cupertino/Components.kt` — nav bar, inset grouped sections, iOS switch,
  hand-rolled slider, action sheet, alert, pill buttons (`7bd208d`, `e5d2748`).
- Screens, each a thin Kotlin `bind()` over the original activity's logic
  (SAF/JNI/executors untouched): Library grid (`8dc2bd4`), Settings + palette
  editor (`1fb43c7`), in-game menu + save slots (`d90ed7f`, `0223b8d`),
  Link (`f636aea`), Printer feed (`a03999b`), Gamepad remap (`d18caa9`).
- Review/on-device fixes: favorite tile staleness — snapshot state needs fresh
  copies (`6ea3d3a`); game-menu submenus unreachable — non-dismissing sheet
  actions (`4c19195`); dialog backdrop opaque — themed variant (`0223b8d`);
  menu sheet width cap (`2b004a9`); action-sheet body tap-through (`e5d2748`).
- Verified on SM-T505 (arm64), light+dark, incl. save/load slot round-trip.

## 2. Adaptive width (tablet + phone)

- `ReadableContent` — centers grouped screens in a ≤640dp column (no-op on
  phones); Library Settings pill moved to the nav-bar action (`5beda43`).
- Action sheet scrolls its action group when taller than the screen —
  landscape-phone fix (`7b574c4`).
- Verified on OnePlus Pad (OPD2403) tablet-native + phone-sim, both orientations.

## 3. GB console skin + portrait play

- Ported the iOS app's control artwork (MIT, same repo) + layout math
  (`GBVerticalLayout`/`GBHorizontalLayout`) into `GBLayout.java` (`0cbe504`).
- `TouchOverlayView` rewritten: body gradient, screen bezel (GL surface shows
  through the well), sprites with pressed states, rotated navy labels,
  8-way d-pad; surface positioned by the layout; `fullSensor` orientation
  (`3e0dadf`).
- Extras: console themes silver/dark/follow (iOS `ColorMatrix` recolor of
  `-tint` sprites), swipe d-pad mode, screen-well depth shading
  (`2050269`, `ce440a8`); Follow-theme honors the in-app theme override
  (`5c5f65a`).
- Verified on OnePlus Pad: portrait + landscape, pressed sprites, d-pad shadow,
  dark console, swipe input drives the core, mid-game rotation.

## 4. Short-screen portrait fix (Moto G4, Android 10, 360×580dp)

- Portrait d-pad overlapped the game screen on short phones — shrink-to-fit
  loop added (`054052b`); over-shrank to 4×, so a two-pass roomy/compact fit
  now keeps the largest integer scale (5× on 1080p 5.5″) with a compact control
  stack + centered menu pill (`b5fe1cb`).
- Verified on the physical Moto G4.

## Tester builds

`~/sameboy-builds/` — `sameboy-android-1.0.3-cupertino-beta2.apk`
(SHA256 `23aadb93…`), `SHA256SUMS`, `TESTERS.md`. R8 release, 4 ABIs,
debug-key signed (no release keystore configured). Installed on G4 + tablet.

## Known / deferred (not blocking)

- Two `@file:Suppress("EXPOSED_PARAMETER_TYPE")` — retire by making
  `LibraryEntry`/`Settings` public.
- Nav title doesn't collapse/pin on scroll (large-title behavior, YAGNI'd).
- Hardcoded English strings (Library title, remap hints).
- No library box art yet.
- Version still 1.0.3 (`version.mk` shared with desktop) — bump at ship time;
  M9 CI (tag → 4-ABI signed release) is already wired.
