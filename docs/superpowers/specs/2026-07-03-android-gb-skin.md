# Android GB Skin + Portrait Play — iOS-look Emulator Screen

**Date:** 2026-07-03
**Branch:** android-frontend
**Status:** Approved

## Goal

Make the emulator screen look and behave like SameBoy's official iOS app: a
Game-Boy-style console body in both orientations, using the iOS port's own
artwork (same repo, MIT), replacing the flat-rectangle TouchOverlayView.

## Source of truth (decoded from iOS/)

- `GBVerticalLayout.m` / `GBHorizontalLayout.m` — layout math (ported verbatim,
  points → dp).
- `GBBackgroundView.m` — pressed-state model: swap button sprite to
  `*Pressed`; d-pad shadow sprite rotated 0/90/180/-90° with a diagonal
  variant for two-key combos; hidden when no d-pad key.
- `GBTheme.m` default theme: body gradient #C0C3C7→#AEB0B4 (vertical), bezel
  gradient #353535→#2D2D2D, brand navy #00468D, black screen well.
- Labels drawn on the body, rotated -30°: A/B (bold, 24dp, 40dp from button
  center), SELECT/START (semibold, ~14dp, 24dp), "SAMEBOY" wordmark
  (bold-italic) between screen and controls when space allows.

## Design

1. **Assets**: copy 7 sprites × @2x/@3x from `iOS/` into
   `app/src/main/res/drawable-xhdpi|xxhdpi/` as `gb_dpad`, `gb_dpad_shadow`,
   `gb_dpad_shadow_diag`, `gb_button`, `gb_button_pressed`, `gb_button2`,
   `gb_button2_pressed`. Natural dp sizes: dpad 147×151, button 75×79,
   button2 76×76, shadows 147×147.
2. **`GBLayout.java`** (new): both orientations' geometry — integer-scaled
   160×144 screen rect (portrait: width-filling, top-anchored; landscape:
   height-filling with ≥164dp wings), d-pad/A/B/Select/Start centers,
   rewind/turbo/menu pill centers (portrait: [<<][≡][>>] row between
   Select/Start; landscape: rewind under Select, turbo under Start, menu
   top-right), logo placement flag.
3. **TouchOverlayView rewrite**: draws body gradient + bezel with
   `clipOutRect(screenRect)` (SurfaceView shows through the hole), navy
   rotated labels + wordmark, sprites with pressed-state swaps and rotated
   d-pad shadow. **8-way d-pad** (angle-based, enables diagonals — model
   upgraded from per-pointer key to per-pointer key-mask; refcount semantics
   preserved). Menu/rewind/turbo restyled as translucent dark pills.
   `Button opacity` setting now applies to the controls layer only (body and
   bezel are opaque console); gamepad-connected mode hides controls but keeps
   body + menu pill (was: whole overlay GONE).
4. **EmulatorActivity**: manifest `sensorLandscape` → `fullSensor`; GL
   surface positioned to `GBLayout.screenRect` via FrameLayout margins,
   updated through an overlay layout callback (rotation relayouts under the
   existing `configChanges`, no recreation).

## Verification (OnePlus Pad)

Portrait + landscape screenshots (body, bezel, sprites, labels); press states
(A pressed sprite, d-pad shadow incl. a diagonal); rotation mid-game (no
crash, surface repositions); phone-sim portrait; regression: multi-touch
d-pad slide, save/load menu, gamepad-hide mode, opacity slider.

## Non-goals

Theme variants, swipe-pad mode, iOS gesture menu, tilt/rumble art, per-game
palette-tinted buttons (iOS themes) — default theme only.
