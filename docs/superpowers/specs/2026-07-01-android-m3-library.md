# SameBoy Android — M3: Library (Design)

Status: **proposed** · Date: 2026-07-01 · Branch: `android-frontend`
Parent: `plans/2026-07-01-android-parity-roadmap.md` (M3) · Builds on M1 + M2.

## 1. Goal & scope

Replace the one-shot ROM picker (`MainActivity` = a single "Open ROM" button) with a
persistent **library browser**: import a folder once, see a grid of games with titles
and last-played, favorite/remove via long-press, launch zipped ROMs, and never block the
main thread reading a ROM. Carries the M1/M2 deferred item: **background ROM read**.

### In
- **SAF tree import** (`ACTION_OPEN_DOCUMENT_TREE`) + `takePersistableUriPermission(uri,
  FLAG_GRANT_READ_URI_PERMISSION)` so the library survives restarts without re-picking.
- **Single-file import** (`ACTION_OPEN_DOCUMENT`) that also persists its per-file read
  grant (so a one-off ROM stays openable next launch).
- **Folder scan** (background, recursive, depth-bounded) for `.gb`/`.gbc` and `.zip`
  (zip containing a `.gb`/`.gbc`). Non-ROMs skipped.
- **Metadata + dedup** via a throwaway Core init: `GB_get_rom_title` + `GB_get_rom_crc32`.
  CRC32 is the dedup key (mirrors iOS `GBROMManager`); two paths to the same ROM = one
  library entry.
- **Persistent store**: a JSON index (`library.json`) — lightest option, no Room/DB
  dependency, no schema migration.
- **Grid UI**: title + last-played; a star badge on favorites. Long-press → Play /
  Favorite-toggle / Remove. Sorted favorites-first, then last-played desc, then title.
- **Zip launch**: extract the first `.gb`/`.gbc` entry (`java.util.zip`, no native change).
- **Background ROM read** at launch (worker thread → main-thread `nativeCreate`), fixing
  the M1/M2 ANR risk on slow providers.
- **Header sanity**: library only accepts files whose bytes yield valid ROM info
  (`< 0x150` or bad → skipped/rejected), closing the M-cross-cutting "non-ROM → black
  screen" gap for library launches.

### Out (later milestones)
Box art / cover images (no source in-tree; not a roadmap milestone — tiles are text);
per-game settings; collections/tags; search/filter tabs; cloud or SD auto-detection;
changing the save-file naming scheme (stays display-name based, see §6).

## 2. Key decisions

- **JSON index, not Room.** Roadmap said "prefer the lightest that works." `org.json`
  is in the framework (no dependency); the library is a flat list of small records.
  Room would add an annotation processor, a DB, and migrations for no benefit at this
  scale.
- **No new dependencies.** Folder enumeration uses `DocumentsContract` directly (not
  `androidx.documentfile`, which is a separate artifact). Grid uses framework `GridView`
  + a `BaseAdapter` (not `RecyclerView`, a separate artifact). Programmatic UI, matching
  M1/M2 (no XML layouts).
- **CRC32 dedup, Core-computed.** `GB_get_rom_crc32` runs over the *padded* ROM exactly
  as every other SameBoy frontend — reimplementing CRC/title extraction in Java would
  duplicate Core logic and drift. A throwaway `GB_init`→`GB_load_rom_from_buffer`→read→
  `GB_free` per candidate is acceptable: import is a one-time background operation.
- **Save-key = CRC, not display name.** `.sav`/state files key off a **saveKey** so two
  different ROMs both named `pokemon.gbc` never collide. Library launches pass
  `EXTRA_ROM_KEY` = the entry's `crc32` (8-hex, from `GB_get_rom_crc32`); the CRC is the
  same canonical identity SameBoy uses everywhere. `EmulatorActivity` uses `EXTRA_ROM_KEY`
  when present and falls back to the display-name-derived `romName` only for external
  one-shot opens (`am start` with a data Uri and no extra — the M2/dev path; not
  reachable from the library UI). M1/M2 was never released, so there is no installed
  base of display-name-keyed saves to migrate; the fallback exists to keep external
  opens behaving exactly as M2 did.

## 3. Native surface (one addition)

```c
/* emulator.h — standalone, no session/emulator instance needed */
/* Fills title[0..16] (NUL-terminated, ≥17 bytes) and *crc32 from a ROM buffer via a
   throwaway Core init. Returns 0 on success, -1 if rom is too small (< 0x150). */
int sb_rom_info(const uint8_t *rom, size_t len, char *title, uint32_t *crc32);
```

Implementation: heap-allocate a `GB_gameboy_t` (the codebase embeds it by value —
`malloc(sizeof(GB_gameboy_t))`, or `GB_alloc()` which is the same size), then
`GB_init(gb, GB_MODEL_CGB_E)` → `GB_load_rom_from_buffer` → `GB_get_rom_title(gb, title)`
`+ *crc32 = GB_get_rom_crc32(gb)` → `GB_free(gb)` → `free(gb)`. No boot ROM, no run —
title/crc read `gb->rom` only; model is irrelevant. (Heap, not stack: `GB_gameboy_t` is
large — the M1 emulator holds it inside a heap struct, never on a thread stack.)

JNI: `nativeRomInfo(byte[] rom) → String[]` = `{ title, crc32Hex8Upper }`, or `null`
when `sb_rom_info` returns -1 (invalid ROM). This is the single new JNI entry point.

## 4. Java architecture

```
MainActivity (library browser)
  ├─ importFolder() → ACTION_OPEN_DOCUMENT_TREE → takePersistableUriPermission
  ├─ importFile()   → ACTION_OPEN_DOCUMENT      → takePersistableUriPermission
  │        │ (both hand the tree/file uri to RomScanner on a background executor)
  ├─ RomScanner ── DocumentsContract walk ── ZipRoms (zip probe) ── nativeRomInfo
  │                                         └─► Library.add(entry)  (CRC dedup)
  ├─ GridView + LibraryAdapter (title, last-played, ★) ── long-press: Play/Fav/Remove
  └─ launch(entry): executor reads bytes (+zip extract) → runOnUiThread → EmulatorActivity
Library (JSON store: load/save library.json; add/dedup/favorite/remove/touch/listSorted)
LibraryEntry (uri, zipEntry?, displayName, title, crc32, favorite, lastPlayed)
EmulatorActivity (background ROM read; new "zip entry" + "name" intent extras)
```

- **`LibraryEntry`**: `String uri; String zipEntry (nullable); String displayName;
  String title; String crc32; boolean favorite; long lastPlayed;` + `toJson()/fromJson()`.
- **`Library`**: wraps `files/library.json`. `load(ctx)`, `save()`, `add(entry)` (dedup by
  `crc32`; keep first uri, don't clobber favorite/lastPlayed on re-add), `setFavorite`,
  `remove`, `touch(crc32)` (lastPlayed = now), `listSorted()` (favorites first, then
  lastPlayed desc, then title asc).
- **`RomScanner`**: given a tree `Uri`, walk children via
  `DocumentsContract.buildChildDocumentsUriUsingTree` + `ContentResolver.query`
  (columns: DOCUMENT_ID, DISPLAY_NAME, MIME_TYPE). Recurse into dirs (depth ≤ 8). For a
  `.gb`/`.gbc`: read bytes, `nativeRomInfo`, add. For a `.zip`: `ZipRoms.firstRomEntry` →
  if present, read+extract, `nativeRomInfo`, add with `zipEntry` set. Runs on the
  executor; posts progress + a final "N added" toast to the UI thread.
- **`ZipRoms`**: `firstRomEntry(InputStream) → String?` (first entry name ending
  `.gb`/`.gbc`); `extract(InputStream, String entry) → byte[]?`. Uses `java.util.zip`.
- **`MainActivity`**: header row with **Import Folder** + **Open File** buttons, a
  `GridView` below. Empty-state text when the library is empty. Tap = launch; long-press
  = `AlertDialog` (Play / Favorite⇄Unfavorite / Remove). All ROM byte I/O on the executor.
- **`EmulatorActivity`**: `onCreate` no longer reads bytes inline. It sets a "Loading…"
  view immediately, reads the ROM on the executor (honoring optional extras
  `EXTRA_ZIP_ENTRY` and `EXTRA_ROM_KEY` = CRC save key), then on the main thread does
  `nativeCreate` + wires the surface/overlay. A read failure toasts and finishes. This
  removes the main-thread SAF read (ANR fix).

## 5. Persistence & permissions

- `takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)` on **every**
  import (tree and file), immediately, before scanning — otherwise stored URIs reopen
  fine until the process/host restarts, then throw `SecurityException`.
- `library.json` in `context.getFilesDir()` (internal, always available — same fallback
  discipline as M2's `SaveStore`). Written atomically (temp file + rename) so a crash
  mid-write can't corrupt the index.
- On launch, `getContentResolver().takePersistableUriPermission` is idempotent; re-taking
  a held grant is safe.

## 6. Save-file interaction

`.sav`/state paths key off a **saveKey**: `EXTRA_ROM_KEY` (the entry's 8-hex CRC) when a
library launch supplies it, else the display-name-derived `romName` for external one-shot
opens (exact M2 behavior). Two different ROMs named `pokemon.gbc` get distinct save keys
(their CRCs differ); a ROM opened outside the library keeps its M2 path. There is no
visible title surface in `EmulatorActivity`, so no display-label extra is forwarded.
`EmulatorActivity` is not exported and has no VIEW filter, so every library launch
carries `EXTRA_ROM_KEY`; the display-name path is only hit by an external `am start`
(the M2/dev flow). M1/M2 shipped to no users, so there is nothing to migrate — the
fallback simply preserves M2's exact behavior for external opens.

## 7. Files

| File | Change |
|---|---|
| `Android/jni/emulator.{h,c}` | + `sb_rom_info` (throwaway-init title+crc32) |
| `Android/jni/sameboy_jni.c` | + `nativeRomInfo(byte[]) → String[]` |
| `Android/jni/test/test_emulator.c` | + `sb_rom_info` test (known title + crc32) |
| `.../NativeBridge.java` | + `nativeRomInfo` decl |
| `.../LibraryEntry.java` (new) | entry record + JSON |
| `.../Library.java` (new) | JSON store, dedup, favorite, remove, touch, sort |
| `.../ZipRoms.java` (new) | first-rom-entry probe + extract |
| `.../RomScanner.java` (new) | SAF tree walk + metadata + add |
| `.../MainActivity.java` | rewritten: grid browser + imports + launch prep |
| `.../EmulatorActivity.java` | background ROM read; zip-entry + name extras |
| `.../res/values/strings.xml` | + library labels |

## 8. Acceptance (from roadmap, sharpened)

1. Import a folder of ROMs → grid populates with **titles** (from `GB_get_rom_title`),
   each showing last-played ("Never" until played).
2. Favorite a game (long-press) and play another → kill + relaunch the app ⇒ favorite
   star persists and the played game shows a real last-played time (recents persist).
3. A `.zip` containing a `.gb`/`.gbc` launches and plays.
4. Importing from a slow provider does not ANR (scan + per-launch read are off the main
   thread); the UI stays responsive with a progress indication.
5. The same ROM reachable twice (e.g. imported folder + opened file) appears **once**
   (CRC dedup), and re-import doesn't reset its favorite/last-played.
6. A non-ROM file in the imported folder is skipped (not added, no crash).
7. Host test: `sb_rom_info` returns the expected title and CRC32 for a crafted ROM, and
   -1 for a `< 0x150` buffer.

## 9. Test strategy

- **Host C test** (extend M1 harness): craft a ROM with a known title at 0x134 and assert
  `sb_rom_info` returns that title + a CRC32 matching a Java/`zlib`-independent reference
  (compute the expected CRC over the padded buffer in the test itself), and returns -1 for
  a 4-byte buffer.
- **On-device (Waydroid)**: push a folder with a `.gbc`, a `.gb`, a `.zip`(→.gb), and a
  junk `.txt`; `ACTION_OPEN_DOCUMENT_TREE` import; verify the grid shows 3 titles (junk
  skipped), favorite one, play one, force-stop, relaunch → favorite + last-played
  persisted; launch the zipped ROM → it runs. (Java library/scan logic has no host
  harness; it is verified on-device + by review, per M1/M2 convention.)
