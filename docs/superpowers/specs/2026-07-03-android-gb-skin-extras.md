# Android GB Skin Extras — Console Themes, Swipe D-pad, Screen-well Depth

**Date:** 2026-07-03
**Branch:** android-frontend
**Status:** Approved

## Goal

Port the remaining iOS-app niceties onto the Android GB console skin:
built-in console themes (silver + dark), the swipe d-pad input mode, and
screen-well depth shading.

## Decoded from iOS/

- `GBTheme.m initDarkTheme`: body top `#181C23`, bottom = per-channel
  `pow(c/255, 1.125)`; buttons recolored to `#080C12`; brand navy and bezel
  gradient unchanged from default.
- `GBTheme.m _recolorImage`: CIColorMatrix over purple-hued `-tint` sprites;
  rows `R=[r*1.34, 1-r, 0, 0]`, `G=[g*1.34, 1-g, 0, 0]`, `B=[b*1.34, 1-b, 0, 0]`,
  alpha preserved. Android equivalent: `ColorMatrixColorFilter` with
  `[c*1.34, 1-c, 0, 0, 0]` rows, drawn once into a cached bitmap.
- `GBBackgroundView.m` swipe pad: `swipepad` sprite (flat pad); first touch in
  pad region anchors an origin; ≥16px from origin → angle → 8-way mask;
  origin trails the finger on a 24px leash; swipe-pad shadow sprite variants.
  (iOS also flips faux-analog core inputs — not exposed in our JNI, skipped.)
- `GBLayout.m drawScreenBezels`: white 25% shadow below the bezel, soft inner
  shadow in the well.

## Design

1. **Console themes** — `Settings`: `console_theme` int (0 = SameBoy,
   1 = SameBoy Dark, 2 = Follow theme; default 2). Follow theme = dark console
   when the app's night mode resolves dark. SettingsUi Appearance section
   gains a "Console" PickerRow. TouchOverlayView gains
   `setConsoleTheme(boolean dark)`: dark swaps body gradient to the iOS dark
   values and replaces dpad/button/button2 (+pressed, +swipepad) bitmaps with
   `-tint` sprites recolored `#080C12` via ColorMatrixColorFilter (cached).
   Labels/bezel unchanged (identical in both iOS themes).
2. **Swipe d-pad** — `Settings`: `swipe_dpad` bool (default false). Controls
   section gains a ToggleRow. TouchOverlayView `setSwipePad(boolean)`: swaps
   the pad sprite, and the d-pad square's hit logic becomes anchor+leash
   (16dp dead zone, 24dp leash, same 8-way sectors); shadow uses
   `gb_swipepad_shadow[_diag]`.
3. **Screen-well depth** — in onDraw after the bezel: 1.5dp light stroke
   along the bezel's outer bottom edge (white 25%), and an inner ring
   (black→transparent, bezelWidth/2 thick) just inside the screen well.
4. Assets: copy 5 `-tint` pairs + `swipepad` + `swipepadShadow[Diagonal]`
   @2x/@3x from iOS/ (names: `gb_dpad_tint`, `gb_button2_tint`,
   `gb_button2_pressed_tint`, `gb_swipepad`, `gb_swipepad_tint`,
   `gb_swipepad_shadow`, `gb_swipepad_shadow_diag`). A/B buttons recolor the
   BASE `gb_button[_pressed]` sprites (iOS does the same — no button-tint asset).

EmulatorActivity applies both settings at construction and in onResume
(alongside opacity/haptics).

## Verification (OnePlus Pad)

Dark console: launch with Follow theme + dark UI → dark body, near-black
buttons; explicit SameBoy forces silver. Swipe pad: toggle on → flat pad
sprite; swipe from pad center drives 8-way input (game responds); shadow
follows. Depth: bezel highlight + inner ring visible in screenshots.
Regression: standard d-pad still works with the toggle off; opacity slider;
menu pill.

## Non-goals

Custom theme editor, per-game themes, faux-analog inputs, dynamic-speed
screen swipes, iOS's full theme file format.
