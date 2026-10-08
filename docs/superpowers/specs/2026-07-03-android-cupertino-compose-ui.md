# Android Cupertino Compose UI Redesign

**Date:** 2026-07-03
**Branch:** android-frontend
**Status:** Approved

## Goal

Rework every non-game screen of the Android port to look like iOS, implemented in
Jetpack Compose with a hand-rolled Cupertino theme. All existing Java logic
(SAF handling, JNI bridge, library store, scanner, settings persistence) stays.

## Decisions (user-approved)

| Question | Decision |
|---|---|
| iOS look source | Hand-rolled Cupertino theme (no dependency). `compose-cupertino` is unmaintained since early 2024; only niche forks survive — rejected. |
| Scope | All non-game screens: Library, Settings, game menu + slots, palette editor, Link, Printer feed, Gamepad remap. EmulatorActivity / TouchOverlayView / render surface untouched. |
| Light/dark | Follow system, both modes (iOS system-color semantics). |

## Build changes

- Kotlin 2.0.x + `org.jetbrains.kotlin.plugin.compose` plugin.
- Compose BOM 2024.09 (last BOM line compatible with compileSdk 34), `activity-compose`.
- `compose.foundation` + `compose.ui` + animation only — **no Material3**; we draw
  iOS idioms directly and Material would fight the look.
- Java stays Java. Each activity keeps its logic and calls a Kotlin
  `XxxUi.bind(activity, callbacks)` bridge that does `setContent`. No rewrite of
  SAF/executor/JNI code. Build stays on JDK 17 (`~/Android/jdk17`), AGP 8.5.2, Gradle 8.9.

## Cupertino kit (2 Kotlin files)

### `cupertino/Theme.kt`
- iOS system colors, light + dark variants, resolved from system dark mode (and the
  app's existing theme override in `Settings`): `systemBackground`,
  `secondarySystemGroupedBackground`, `systemGroupedBackground`, `label`,
  `secondaryLabel`, `tertiaryLabel`, `separator`, `systemBlue`, `systemRed`,
  `systemGreen`, fill colors.
- iOS type scale on the system font (SF Pro is not licensable for bundling; system
  font with iOS metrics): LargeTitle 34sp bold, Title2 22sp, Headline 17sp semibold,
  Body 17sp, Subheadline 15sp, Footnote 13sp, Caption 12sp.
- Exposed via CompositionLocals + a `CupertinoTheme { }` wrapper.

### `cupertino/Components.kt`
- **CupertinoNavBar** — large-title style; optional trailing text/icon buttons.
- **InsetGroupedSection** — 10dp-radius card, hairline (0.5dp) separators inset to
  text, uppercase footnote header, optional footer text.
- **NavRow** — label + value + chevron (›).
- **ToggleRow** — label + iOS switch: 51×31dp track, animated thumb, green-on.
- **SliderRow** — label + value readout + slider tinted systemBlue.
- **PickerRow** — label + current value; tap opens action sheet of options.
- **CupertinoActionSheet** — bottom sheet: rounded stacked buttons, hairline
  separators, destructive rows in systemRed, detached Cancel button.
- **CupertinoAlert** — centered rounded alert with vertical/horizontal buttons.
- **CupertinoButton** — filled / tinted(gray pill) / plain text styles.

## Screens

- **Library (MainActivity)** — large title "Library"; adaptive grid of rounded game
  tiles (title, relative last-played, ★ favorite badge); pill toolbar buttons
  (Import Folder / Open ROM / Settings); long-press tile → action sheet
  (Play / Favorite / Remove); empty-state placeholder. Same `Library`/`RomScanner`
  logic and SAF flows.
- **Settings (SettingsActivity)** — grouped list, same sections and order
  (Emulation / Video / Audio / Controls / Appearance), writing through the existing
  `Settings` class. Enum rows → PickerRow, sliders → SliderRow, haptics → ToggleRow,
  gamepad remap → NavRow.
- **Game menu + save slots (GameMenuDialog)** — Dialog-hosted ComposeView;
  action-sheet menu; slot picker = 2×2 grid of thumbnail cards with timestamps.
  `Host` interface unchanged; pause/unpause contract unchanged.
- **Palette editor (PaletteEditorDialog)** — alert-style sheet, grouped color rows.
- **Link (LinkActivity)** — grouped list: Host / Join + IP field / status footer.
- **Printer feed (PrinterFeedActivity)** — nav bar + feed image card + Save/Share buttons.
- **Gamepad remap (GamepadRemapActivity)** — grouped list of binding rows; armed row
  highlighted systemBlue; key events keep flowing through the activity.

## Non-goals

- No navigation library (activities already navigate via Intents).
- No Material3, no icon pack dependency.
- No changes to EmulatorActivity, TouchOverlayView, EmulatorSurfaceView, JNI, or
  anything under `jni/`.
- No new features — visual rework only, feature parity preserved.

## Error handling

Unchanged from today: toasts for scan results / not-a-rom, existing try/catch
around SAF permissions. Compose screens render whatever state the Java side hands
them; no new failure modes introduced.

## Testing / verification

1. `assembleDebug` with JDK 17 must pass (CI parity: 4 ABIs).
2. Install on tablet (`adb -s R9TR20NR6YJ`) or Waydroid; walk every reworked
   screen in light and dark mode; screenshot each.
3. Feature-parity smoke: import folder, open ROM, launch game, in-game menu
   save/load slot, settings persist across relaunch, gamepad remap arms and binds.
