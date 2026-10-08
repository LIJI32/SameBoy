# Android Adaptive Width (Tablet + Phone) — Cupertino UI Follow-up

**Date:** 2026-07-03
**Branch:** android-frontend
**Status:** Approved

## Problem

The Cupertino screens are single full-width columns. On tablets (SM-T505 2000px,
OnePlus Pad 2120px portrait) grouped rows stretch absurdly wide (label far left,
value far right). Phone portrait (~360-420dp) has never been exercised; the
Library toolbar's three pills won't fit narrow widths.

## Decision (user-approved)

**iOS readable-width cap** — not an iPad split view.

1. **`ReadableContent` wrapper** (cupertino kit): centers content in a
   `widthIn(max = 640.dp)` column. Applied to the whole screen column (nav bar
   included, so the large title aligns with its content) of the four grouped
   screens: Settings, Link, Gamepad remap, Printer feed. On <640dp screens it is
   a geometric no-op — one mechanism serves phone and tablet, no size classes.
2. **Library**: grid stays full-width (correct for grids). The Settings pill
   moves to `CupertinoNavBar`'s `trailing` slot (existing, unused) as a plain
   text action — iOS nav-action idiom; toolbar keeps two pills, which fit 360dp.
3. **Emulator screen untouched.** Sheets/alerts already width-capped.

## Verification

On the OnePlus Pad (OPD2403, arm64, Android 16):
- Tablet-native: Settings / Link / Remap / Printer centered at 640dp; Library
  grid full-width with Settings in the nav bar. Light + dark screenshots.
- Phone simulation: `wm size 1080x2340` + `wm density 420` (→ ~412dp width);
  walk Library / Settings / game menu; confirm no clipped toolbar, rows usable;
  reset with `wm size reset` / `wm density reset`.
- Feature-parity smoke: import/launch/menu still work after the Library change.

## Non-goals

No split view, no size-class framework, no per-device layouts, no grid changes.
