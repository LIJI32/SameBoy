# SameBoy Android — M5: Color & Theme (Design)

Status: **proposed** · Date: 2026-07-01 · Branch: `android-frontend`
Parent: `plans/2026-07-01-android-parity-roadmap.md` (M5) · Builds on M1–M4.

## 1. Goal & scope

A **DMG palette** picker + custom editor that recolors monochrome (DMG-mode) games live,
and an **app light/dark theme** independent of the GB palette. Both persist and slot into
the existing M4 Settings surface.

### In
- **Built-in DMG palettes:** Greyscale, DMG (green), MGB, GBL — the four Core `extern
  const GB_palette_t` (`display.h`).
- **Custom 4-color palette editor:** four color wells (shades 0=darkest … 3=lightest),
  each an RGB editor; applied via `GB_set_palette`.
- **App theme:** System / Light / Dark via AndroidX `DayNight` + `AppCompatDelegate`,
  independent of the GB palette.
- Persist palette selection (built-in index or custom) + custom colors + theme mode in the
  M4 `SharedPreferences`; palette pushed through the same launch/resume batch-apply path.

### Out (deferred, documented)
- **Per-ROM palette** — roadmap says "optionally per-ROM"; M5 does a single global palette
  (per-ROM keying would fan out the Settings store; defer until there's a per-game settings
  screen).
- **CGB color tweaks / palette for color games** — `GB_set_palette` only affects
  `!GB_is_cgb` (DMG-mode) rendering; CGB games use their own colors. Documented, not a gap.
- **SGB border themes, app accent theming beyond light/dark, custom-palette import/export.**
- **Integer scaling / frame blending** (M4-deferred render items) stay deferred — still M5+
  render-pass work, out of this milestone's color/theme scope.

## 2. Key decisions

- **Palette struct owned by the emulator.** `GB_set_palette` stores the *pointer*
  (`gb->dmg_palette`, display.c:34 — no copy), so the pointee must outlive the call.
  Built-ins are `extern const` (process-lifetime) — safe to point at directly. A custom
  palette lives in an owned `GB_palette_t` on `sb_emulator`, filled before `GB_set_palette`.
- **One native entry, index-or-custom.** `sb_emu_set_palette(e, builtin_index, const
  uint32_t rgb[4])`: `builtin_index` 0–3 selects a Core const; −1 means "use the 4 packed
  `0x00RRGGBB` colors". Keeps built-in resolution in C (no re-hardcoding RGB in Java) and
  is one JNI call.
- **5th palette color derived.** `GB_palette_t` has 5 colors (`colors[4]` = the border/
  blank shade). The custom editor exposes 4 (the shades); the 5th is set equal to the
  lightest (`colors[3]`) — matches the Greyscale built-in's own `colors[3]==colors[4]`.
- **App theme via AppCompat DayNight.** The manifest theme is already `Theme.AppCompat.*`;
  switch it to `Theme.AppCompat.DayNight.NoActionBar` and drive light/dark with
  `AppCompatDelegate.setDefaultNightMode(...)` from an `Application` subclass at process
  start. `MainActivity` and `SettingsActivity` become `AppCompatActivity` (the two screens
  with themed chrome). `EmulatorActivity` stays a plain `Activity` — it is all-black
  gameplay, theme-independent, and converting it risks the SurfaceView path for zero visual
  gain.
- **Palette joins the M4 apply path.** `Settings.apply(ctx)` gains a `nativeSetPalette`
  call, so palette applies at launch and on resume exactly like the other Core settings —
  self-parking once, no new lifecycle.
- **Default palette: DMG (green).** SameBoy switched its default from Greyscale to a green
  DMG-style palette in 0.15.2 (`configuration.c dmg_palette = 1`). Match it.

## 3. Native surface

```c
/* emulator.h */
void sb_emu_set_palette(sb_emulator *e, int builtin_index, const uint32_t rgb[4]);
/* builtin_index 0=Grey 1=DMG 2=MGB 3=GBL; -1 => custom from rgb[4] (0x00RRGGBB,
   index 0 darkest .. 3 lightest). Copies into an owned GB_palette_t for custom;
   points at the Core const for built-ins. */

/* session.h */
void sb_session_set_palette(sb_session *s, int builtin_index, const uint32_t rgb[4]); /* self-parks */
```

`sb_emu_set_palette` custom path: fill `e->custom_palette.colors[i] = {r,g,b}` for i=0..3
from `rgb[i]`, set `colors[4] = colors[3]`, then `GB_set_palette(&e->gb,
&e->custom_palette)`. Built-in path: `GB_set_palette(&e->gb, builtin_const[index])` where
`builtin_const[] = {&GB_PALETTE_GREY, &GB_PALETTE_DMG, &GB_PALETTE_MGB, &GB_PALETTE_GBL}`.

JNI: `nativeSetPalette(ctx, int builtinIndex, int c0, int c1, int c2, int c3)` — passes the
four packed colors as ints (ignored when builtinIndex ≥ 0). One new JNI entry (20 total).

## 4. Java architecture

```
Settings (extended):
  paletteBuiltin() : int (0..3, or -1 custom; default 1 = DMG)
  customColor(i)   : int 0xRRGGBB (4 keys)
  themeMode()      : int (0 System, 1 Light, 2 Dark; default 0)
  apply(ctx): ... existing M4 calls ... + nativeSetPalette(ctx, builtin, c0..c3)
  applyTheme(): AppCompatDelegate.setDefaultNightMode(FOLLOW_SYSTEM|NO|YES)
SameBoyApp (new, Application): onCreate → new Settings(this).applyTheme()
SettingsActivity (now AppCompatActivity):
  Video section gains "DMG palette" enum row (Greyscale/DMG/MGB/GBL/Custom…);
    picking Custom… opens PaletteEditorDialog.
  new "Appearance" section: "Theme" enum row (System/Light/Dark) →
    on change: persist + AppCompatDelegate.setDefaultNightMode + recreate().
PaletteEditorDialog (new): 4 color rows (shade 0..3), each opens an RGB editor
  (3 SeekBars R/G/B + a live swatch); on confirm persists the 4 customColor ints,
  sets paletteBuiltin = -1.
MainActivity (now AppCompatActivity): unchanged behavior.
EmulatorActivity (plain Activity): Settings.apply(ctx) at launch + resume now also
  pushes the palette (no code change beyond the existing apply calls).
```

## 5. Concurrency

`sb_session_set_palette` self-parks (M2/M4 `park_begin`/`park_end`), so `GB_set_palette` +
`GB_update_dmg_palette` run while the emu thread is parked — no torn frame. Custom-palette
storage is the emulator-owned `custom_palette`, only written under the park, only read by
Core on the emu thread. Built-in pointers are immutable process globals. Applying before
`nativeStart` (launch) runs with no emu thread. Theme changes are pure Java/UI.

## 6. Files

| File | Change |
|---|---|
| `Android/jni/emulator.{h,c}` | + `custom_palette` field, `sb_emu_set_palette` |
| `Android/jni/session.{h,c}` | + `sb_session_set_palette` (self-park) |
| `Android/jni/sameboy_jni.c` | + `nativeSetPalette` |
| `Android/jni/test/test_emulator.c` | + palette recolors DMG frame (built-in + custom) |
| `Android/jni/test/test_session.c` | + set-palette mid-run (self-park) |
| `.../NativeBridge.java` | + `nativeSetPalette` decl |
| `.../Settings.java` | + palette + theme keys/accessors; `apply` pushes palette; `applyTheme()` |
| `.../SameBoyApp.java` (new) | Application → `applyTheme()` on start |
| `.../PaletteEditorDialog.java` (new) | 4-shade RGB editor |
| `.../SettingsActivity.java` | AppCompatActivity; palette row + Appearance/Theme row |
| `.../MainActivity.java` | AppCompatActivity |
| `.../res/values/strings.xml` | palette/theme labels |
| `.../AndroidManifest.xml` | DayNight theme + register `SameBoyApp` |

## 7. Acceptance (from roadmap, sharpened)

1. On a **DMG-mode** game, selecting a built-in palette (e.g. Greyscale → DMG green)
   recolors the running screen live; persists across restart.
2. The custom editor: set 4 colors, apply → the DMG game shows those colors; persists.
3. App theme switch (Light ↔ Dark) reskins the library + settings screens live; persists
   across restart; is independent of the GB palette.
4. A **CGB** game is unaffected by the DMG palette (documented — `GB_set_palette` is
   DMG-only); no crash when a palette is set with a CGB game running.
5. Host tests: `sb_emu_set_palette` with a built-in and with a custom palette changes the
   DMG-model front buffer (frame bytes differ from the Grey default and match the chosen
   palette's encoded colors); set-palette mid-run doesn't deadlock (session test).

## 8. Test strategy

- **Host C:** on a DMG-model (`0x002`) emulator, capture the front buffer under GREY,
  then `sb_emu_set_palette(e, 1, NULL)` (DMG green), run a frame, assert the buffer differs
  and a sampled pixel equals `rgb_encode` of a DMG-palette entry; repeat for a custom
  palette (e.g. pure red shades) and assert the sampled pixel is red. Extend `test_session.c`
  to call `sb_session_set_palette` mid-run under the `alarm()` net.
- **On-device (Waydroid):** palette + theme are `content://`-independent, fully
  exercisable. Set model → DMG in Settings, launch libbet (DMG-compatible) via the proven
  `file://` path → it renders monochrome; open Settings, pick DMG/MGB/GBL/Custom → the
  running frame recolors on resume; toggle app Theme Light/Dark → library + settings reskin;
  force-stop + relaunch → palette + theme persist.
