# SameBoy Android — M7: Peripherals (Design)

Status: **proposed** · Date: 2026-07-01 · Branch: `android-frontend`
Builds on M1–M6.
Parent: `plans/2026-07-01-android-parity-roadmap.md` (M7).

## 1. Goal & scope

Two Game Boy accessories, mirroring the iOS app (`GBViewController` printer feed +
camera capture):

- **Game Boy Printer** — connect the emulated printer; printed images accumulate into a
  scrollable **feed** the user can save and share.
- **Game Boy Camera** — feed live device-camera frames (grayscale, downscaled to the GB
  camera sensor) to a Game Boy Camera ROM so the viewfinder is live and photos can be taken.

Both Core files (`printer.c`, `camera.c`) already compile in the M1 `Android.mk`. This
milestone adds **no new Core files and no new Gradle dependencies** — camera uses the
platform **Camera2** API (framework), consistent with iOS using AVFoundation directly.

Reused Core APIs (unmodified):
- Printer: `GB_connect_printer(gb, print_image_cb, printer_done_cb)`, `GB_disconnect_serial(gb)`.
  `print_image_cb(gb, uint32_t *image, height, top_margin, bottom_margin, exposure)` — the
  image is already `rgb_encode`d (our ABGR); printer output is greyscale so channel order
  is immaterial. `GB_connect_printer` asserts not-running → **self-park**.
- Camera: `GB_set_camera_get_pixel_callback` (emu thread, per-pixel read of the sensor),
  `GB_set_camera_update_request_callback` (emu thread, fires when the ROM triggers a shoot),
  `GB_camera_updated(gb)` (clears the busy bit). Callbacks are set once at create.

## 2. Architecture (unchanged invariants)

Java owns UI + device I/O (Camera2, MediaStore/FileProvider); C owns the Core + buffers;
JNI is setup/control only, never per-pixel across the boundary during the hot loop. The
printer feed and camera sensor buffers live in `sb_emulator`, each guarded by its own
mutex/atomics — independent of the emu run state where possible so only true Core mutations
(printer connect/disconnect) self-park.

## 3. Native design (`Android/jni/emulator.c` + `emulator.h`)

### Printer
Add to `struct sb_emulator`:
```
/* printer feed: ARGB rows, 160 px wide, grows as images print */
pthread_mutex_t   printer_mtx;
uint32_t         *printer_feed;      /* malloc/realloc, 160 * printer_rows */
unsigned          printer_rows;
atomic_uint       printer_generation;/* bumped on each append + on done; Java polls */
bool              printer_connected;
```
- `static void print_image_cb(gb, image, height, top_margin, bottom_margin, exposure)`:
  under `printer_mtx`, append `top_margin` white rows, `height` image rows (from `image`),
  `bottom_margin` white rows — matching iOS's `paddedImage` (memset 0xFF, copy image after
  top margin). `atomic_fetch_add(&printer_generation, 1)`.
- `static void printer_done_cb(gb)`: `atomic_fetch_add(&printer_generation, 1)`.
- Pure helper (testable): `void sb_printer_append(uint32_t **buf, unsigned *rows,
  const uint32_t *image, unsigned height, unsigned top, unsigned bottom)` — realloc grow +
  white margins + copy. `print_image_cb` calls it under the lock.

Public API (`emulator.h`):
```
void      sb_emu_connect_printer(sb_emulator *e);    /* GB_connect_printer; must be parked */
void      sb_emu_disconnect_printer(sb_emulator *e); /* GB_disconnect_serial; must be parked */
unsigned  sb_emu_printer_generation(sb_emulator *e); /* atomic read */
unsigned  sb_emu_printer_feed(sb_emulator *e, uint32_t *dst, unsigned max_rows);
                                                     /* copies up to max_rows*160 px; returns rows available */
void      sb_emu_printer_clear(sb_emulator *e);      /* free feed under lock */
```
`printer_feed`/`generation`/`clear` use `printer_mtx` only — no park (buffer, not Core).

### Camera
Add to `struct sb_emulator`:
```
/* camera sensor: 128x112 grayscale (GB camera sensor window) */
pthread_mutex_t   camera_mtx;        /* guards staging */
uint8_t           camera_staging[128 * 112]; /* Java writes latest frame */
uint8_t           camera_sensor[128 * 112];  /* emu-thread read snapshot */
atomic_bool       camera_has_frame;
atomic_bool       camera_wanted;     /* set on update-request; Java polls to run the camera */
```
- `static uint8_t cam_get_pixel_cb(gb, x, y)`: return `sb_camera_read(camera_sensor, x, y)`
  (clamp x∈[0,127], y∈[0,111]); emu thread, lock-free (sensor only mutated in update_request
  on the same thread).
- `static void cam_update_request_cb(gb)`: `atomic_store(&camera_wanted, true)`; under
  `camera_mtx` copy `camera_staging → camera_sensor` (promote latest delivered frame);
  `GB_camera_updated(gb)` immediately (non-blocking; if no frame yet the sensor is 0 = dark).
- Helpers (testable): `void sb_camera_promote(sb_emulator *e)` (staging→sensor under lock);
  `uint8_t sb_camera_read(const uint8_t *buf, int x, int y)` (clamp + fetch).

Public API:
```
bool sb_emu_camera_wanted(sb_emulator *e);                 /* atomic read */
void sb_emu_camera_deliver(sb_emulator *e, const uint8_t *gray128x112); /* Java → staging */
```
Callbacks set in `sb_emu_create` via `GB_set_camera_get_pixel_callback` /
`GB_set_camera_update_request_callback` (once, safe pre-threads).

Free `printer_feed` + destroy mutexes in `sb_emu_destroy`.

## 4. Session layer (`session.c`/`.h`)
- `sb_session_connect_printer(s)` / `sb_session_disconnect_printer(s)` — **self-park**
  (`park_begin`/`park_end`, the proven pattern) around the emu connect/disconnect, since
  `GB_connect_printer`/`GB_disconnect_serial` assert not-running.
- Thin passthroughs (no park): `sb_session_printer_generation`, `sb_session_printer_feed`,
  `sb_session_printer_clear`, `sb_session_camera_wanted`, `sb_session_camera_deliver`.
- All tolerate `s == NULL`.

## 5. JNI (`sameboy_jni.c` + `NativeBridge.java`)
```
void      nativeConnectPrinter(long ctx)
void      nativeDisconnectPrinter(long ctx)
int       nativePrinterGeneration(long ctx)
int[]     nativePrinterFeed(long ctx)        /* ARGB ints, length rows*160 (may be 0) */
void      nativePrinterClear(long ctx)
boolean   nativeCameraWanted(long ctx)
void      nativeCameraDeliver(long ctx, byte[] gray)  /* exactly 128*112 bytes */
```
`nativePrinterFeed`: query rows via `sb_session_printer_feed(s, NULL, 0)`, alloc an
`int[rows*160]`, fill via a second call, `SetIntArrayRegion`.

## 6. Java UI

### Accessory menu (`GameMenuDialog`)
Add a **"Connect accessory"** item → a sub-`AlertDialog` (single-choice: **None**,
**Game Boy Printer**), reflecting current state. Selecting Printer →
`nativeConnectPrinter`; None → `nativeDisconnectPrinter`. Mirrors iOS `openConnectMenu`.

### Printer feed (`PrinterFeedActivity` — new)
- Opened from the in-game menu's **"Printer feed"** item (enabled once
  `nativePrinterGeneration() > 0`).
- Builds a `Bitmap` (160 × rows, `ARGB_8888`) from `nativePrinterFeed()` via
  `setPixels`, shown in a scrollable, integer-scaled `ImageView` (black background).
- Actions: **Save** (MediaStore `Pictures/SameBoy`), **Share** (FileProvider PNG),
  **Clear** (`nativePrinterClear` + finish). Empty feed → "No printouts yet."

### Camera (`EmulatorActivity`)
- A poller (like `rumblePoll`) checks `nativeCameraWanted()`; on first want, request the
  `CAMERA` runtime permission, open **Camera2** (back lens, ~352×288 / lowest YUV_420_888),
  and stream frames via `ImageReader`. Each frame: take the **Y plane** (already luma),
  center-crop to the sensor aspect, **downscale to 128×112**, `nativeCameraDeliver`.
- Auto-stop after ~1.5 s with no new want (mirrors iOS's 1 s disable timer). Permission
  denied → toast, leave the ROM to its built-in noise (Core generates noise with no frame).
- Manifest: `<uses-permission android:name="android.permission.CAMERA"/>` +
  `<uses-feature android:name="android.hardware.camera" android:required="false"/>`.

## 7. Testing

**Host tests** (`Android/jni/test`, no device):
- `sb_printer_append`: append synthetic image rows with top/bottom margins → assert
  `rows == top+height+bottom`, margin rows are all-white (`0xFFFFFFFF`), image rows match
  the source; a second append grows and appends after the first.
- Camera: `sb_emu_camera_deliver` a known 128×112 gradient → `sb_camera_promote` →
  `sb_camera_read` returns the delivered values, and clamps out-of-range (`x=200→127`,
  `y=200→111`, negatives→0); `sb_emu_camera_wanted` starts `false`.
- Session: connect printer while running self-parks and returns cleanly (no deadlock),
  `printer_generation` monotonic; camera deliver/wanted passthrough.

**On-device (Waydroid, x86_64):**
- Printer: connect via the accessory menu; the feed screen opens without crash. A ROM that
  actually prints is needed for a real printout — if none is available, verify the feed UI,
  save/share plumbing, and menu wiring with a synthetic/empty state, and **document** the
  printout-content check as needing a printing ROM.
- Camera: **Waydroid has no camera device** — verify the permission prompt appears, the
  poller starts on a Camera ROM's request, no crash on deny/no-camera (ROM falls back to
  Core noise). Live viewfinder + photo capture need **real hardware** — documented, like
  M6's rumble motor.

## 8. Acceptance

1. Printer connects from the in-game accessory menu; printed images accumulate into the
   feed; the feed screen renders them and can save + share; Clear empties it. (Printout
   *content* verified with a printing ROM where available; UI/plumbing always.)
2. A Game Boy Camera ROM triggers the CAMERA permission + camera start; delivered frames
   reach the sensor (live viewfinder on real hardware); no crash when denied / no camera.
3. Host tests green (printer append + margins, camera deliver/clamp, session self-park).
4. Clean 4-ABI `assembleDebug`; JNI symbol count grows by the new methods.
5. `Core/` unmodified; no new Gradle dependency.
