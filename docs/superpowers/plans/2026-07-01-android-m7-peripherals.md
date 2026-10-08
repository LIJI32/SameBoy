# SameBoy Android M7 — Peripherals Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Game Boy Printer feed (connect → images accumulate → save/share) + Game Boy
Camera (live device frames → GB sensor). No new Core files, no new Gradle dependency
(Camera2 is framework). Spec: `specs/2026-07-01-android-m7-peripherals.md`.

**Coverage:** 1. Printer native+append: Task 1 · 2. Camera native+clamp: Task 2 ·
3. Session self-park + passthrough: Task 3 · 4. JNI: Task 4 · 5. Printer feed UI +
FileProvider: Task 5 · 6. Accessory menu: Task 6 · 7. Camera2 capture: Task 7 ·
8. Integration build + host tests: Task 8.

**Conventions:** Work only from your task brief. Never modify `Core/`. No formatters/
linters. Build: `cd Android && JAVA_HOME=$HOME/Android/jdk17 ANDROID_HOME=$HOME/Android
./gradlew :app:assembleDebug`. Host tests: `Android/jni/test/run_host_tests.sh`.

---

## Task 1 — Native printer (feed buffer + callbacks + append helper)

**Files:** `Android/jni/emulator.h`, `Android/jni/emulator.c`.

### emulator.h — add after the rumble getter
```c
/* --- Game Boy Printer (M7) --- */
void     sb_emu_connect_printer(sb_emulator *e);     /* GB_connect_printer; call parked */
void     sb_emu_disconnect_printer(sb_emulator *e);  /* GB_disconnect_serial; call parked */
unsigned sb_emu_printer_generation(sb_emulator *e);  /* atomic; bumps per printed image + done */
/* Copies up to max_rows rows (160 px each) into dst; returns rows currently available.
   dst may be NULL / max_rows 0 to just query the row count. */
unsigned sb_emu_printer_feed(sb_emulator *e, uint32_t *dst, unsigned max_rows);
void     sb_emu_printer_clear(sb_emulator *e);

/* Pure, testable: grow *buf to (*rows + top + height + bottom) rows of 160 px, fill the
   top/bottom margin rows white (0xFFFFFFFF) and copy `height` rows from `image`. */
void     sb_printer_append(uint32_t **buf, unsigned *rows, const uint32_t *image,
                           unsigned height, unsigned top, unsigned bottom);
```

### emulator.c
Add `#include <stdatomic.h>` if not present (rumble already added it — verify). Add fields
to `struct sb_emulator` (after `rumble_amp`):
```c
    pthread_mutex_t printer_mtx;
    uint32_t       *printer_feed;        /* 160 * printer_rows ARGB, malloc/realloc */
    unsigned        printer_rows;
    atomic_uint     printer_generation;
```

Append helper + callbacks (place above `sb_emu_create`):
```c
#define SB_PRINTER_W 160

void sb_printer_append(uint32_t **buf, unsigned *rows, const uint32_t *image,
                       unsigned height, unsigned top, unsigned bottom)
{
    unsigned add = top + height + bottom;
    if (add == 0) return;
    unsigned new_rows = *rows + add;
    uint32_t *grown = realloc(*buf, (size_t)new_rows * SB_PRINTER_W * sizeof(uint32_t));
    if (!grown) return;                 /* keep the old buffer on OOM */
    *buf = grown;
    uint32_t *p = grown + (size_t)*rows * SB_PRINTER_W;
    memset(p, 0xFF, (size_t)top * SB_PRINTER_W * sizeof(uint32_t));   /* white top margin */
    p += (size_t)top * SB_PRINTER_W;
    if (height && image) memcpy(p, image, (size_t)height * SB_PRINTER_W * sizeof(uint32_t));
    p += (size_t)height * SB_PRINTER_W;
    memset(p, 0xFF, (size_t)bottom * SB_PRINTER_W * sizeof(uint32_t)); /* white bottom margin */
    *rows = new_rows;
}

static void print_image_cb(GB_gameboy_t *gb, uint32_t *image, uint8_t height,
                           uint8_t top_margin, uint8_t bottom_margin, uint8_t exposure)
{
    (void)exposure;
    sb_emulator *e = GB_get_user_data(gb);
    pthread_mutex_lock(&e->printer_mtx);
    sb_printer_append(&e->printer_feed, &e->printer_rows, image, height, top_margin, bottom_margin);
    pthread_mutex_unlock(&e->printer_mtx);
    atomic_fetch_add(&e->printer_generation, 1);
}

static void printer_done_cb(GB_gameboy_t *gb)
{
    sb_emulator *e = GB_get_user_data(gb);
    atomic_fetch_add(&e->printer_generation, 1);
}

void sb_emu_connect_printer(sb_emulator *e)
{
    if (e) GB_connect_printer(&e->gb, print_image_cb, printer_done_cb);
}

void sb_emu_disconnect_printer(sb_emulator *e)
{
    if (e) GB_disconnect_serial(&e->gb);
}

unsigned sb_emu_printer_generation(sb_emulator *e)
{
    return e ? atomic_load(&e->printer_generation) : 0;
}

unsigned sb_emu_printer_feed(sb_emulator *e, uint32_t *dst, unsigned max_rows)
{
    if (!e) return 0;
    pthread_mutex_lock(&e->printer_mtx);
    unsigned rows = e->printer_rows;
    if (dst && max_rows) {
        unsigned n = rows < max_rows ? rows : max_rows;
        memcpy(dst, e->printer_feed, (size_t)n * SB_PRINTER_W * sizeof(uint32_t));
    }
    pthread_mutex_unlock(&e->printer_mtx);
    return rows;
}

void sb_emu_printer_clear(sb_emulator *e)
{
    if (!e) return;
    pthread_mutex_lock(&e->printer_mtx);
    free(e->printer_feed);
    e->printer_feed = NULL;
    e->printer_rows = 0;
    pthread_mutex_unlock(&e->printer_mtx);
}
```
In `sb_emu_create`: `pthread_mutex_init(&e->printer_mtx, NULL);` (printer_feed/rows/gen are
zero from calloc). In `sb_emu_destroy`: `free(e->printer_feed); pthread_mutex_destroy(&e->printer_mtx);`.
`GB_connect_printer` / `GB_disconnect_serial` are already visible — `emulator.c` includes
`<Core/gb.h>`, and gb.h includes both `printer.h` and `camera.h`. **No new include needed.**

### Host test — `test/test_emulator.c`: add a standalone `test_printer()` (the file uses
per-test functions), and call it from `main` alongside `test_rumble();`.
```c
static void test_printer(void)
{
    uint32_t *buf = NULL; unsigned rows = 0;
    uint32_t src[160 * 2];
    for (int i = 0; i < 160 * 2; i++) src[i] = 0xFF123456u;  /* sentinel image pixels */
    sb_printer_append(&buf, &rows, src, 2, 1, 3);            /* top 1, img 2, bottom 3 */
    assert(rows == 6);
    for (int x = 0; x < 160; x++) assert(buf[x] == 0xFFFFFFFFu);           /* top white */
    for (int i = 0; i < 160 * 2; i++) assert(buf[160 + i] == 0xFF123456u); /* image */
    for (int x = 0; x < 160 * 3; x++) assert(buf[160 * 3 + x] == 0xFFFFFFFFu); /* bottom white */
    sb_printer_append(&buf, &rows, src, 2, 0, 0);            /* grows, appends after */
    assert(rows == 8);
    assert(buf[160 * 6] == 0xFF123456u);
    free(buf);
}
```
Add `test_printer();` to `main`'s call list (after `test_rumble();`). `sb_printer_append`
is declared in emulator.h (already `#include`d by the test).

**Acceptance:** host tests green; `sb_printer_append` produces correct margins + growth;
build compiles the new symbols.

---

## Task 2 — Native camera (sensor + staging + callbacks + clamp)

**Files:** `Android/jni/emulator.h`, `Android/jni/emulator.c`.

### emulator.h — add after the printer block
```c
/* --- Game Boy Camera (M7) --- 128x112 grayscale sensor window */
#define SB_CAM_W 128
#define SB_CAM_H 112
bool    sb_emu_camera_wanted(sb_emulator *e);                 /* atomic read */
void    sb_emu_camera_deliver(sb_emulator *e, const uint8_t *gray);   /* SB_CAM_W*SB_CAM_H bytes → staging */
/* testable helpers */
void    sb_camera_promote(sb_emulator *e);                    /* staging → sensor under lock */
uint8_t sb_camera_read(const uint8_t *buf, int x, int y);     /* clamp x∈[0,127] y∈[0,111] */
```
(Add `#include <stdbool.h>` if not already via gb.h.)

### emulator.c — fields (after printer fields)
```c
    pthread_mutex_t camera_mtx;
    uint8_t         camera_staging[SB_CAM_W * SB_CAM_H];
    uint8_t         camera_sensor[SB_CAM_W * SB_CAM_H];
    atomic_bool     camera_wanted;
```
Helpers + callbacks (above `sb_emu_create`):
```c
uint8_t sb_camera_read(const uint8_t *buf, int x, int y)
{
    if (x < 0) x = 0; else if (x >= SB_CAM_W) x = SB_CAM_W - 1;
    if (y < 0) y = 0; else if (y >= SB_CAM_H) y = SB_CAM_H - 1;
    return buf[y * SB_CAM_W + x];
}

void sb_camera_promote(sb_emulator *e)
{
    pthread_mutex_lock(&e->camera_mtx);
    memcpy(e->camera_sensor, e->camera_staging, sizeof(e->camera_sensor));
    pthread_mutex_unlock(&e->camera_mtx);
}

static uint8_t cam_get_pixel_cb(GB_gameboy_t *gb, uint8_t x, uint8_t y)
{
    sb_emulator *e = GB_get_user_data(gb);
    return sb_camera_read(e->camera_sensor, x, y);   /* emu thread; sensor only written here-thread */
}

static void cam_update_request_cb(GB_gameboy_t *gb)
{
    sb_emulator *e = GB_get_user_data(gb);
    atomic_store(&e->camera_wanted, true);
    sb_camera_promote(e);            /* pull latest delivered frame into the sensor */
    GB_camera_updated(gb);           /* clear busy immediately (non-blocking) */
}

bool sb_emu_camera_wanted(sb_emulator *e)
{
    return e ? atomic_load(&e->camera_wanted) : false;
}

void sb_emu_camera_deliver(sb_emulator *e, const uint8_t *gray)
{
    if (!e || !gray) return;
    pthread_mutex_lock(&e->camera_mtx);
    memcpy(e->camera_staging, gray, sizeof(e->camera_staging));
    pthread_mutex_unlock(&e->camera_mtx);
}
```
In `sb_emu_create` (after the gb init, near other `GB_set_*_callback`):
```c
    pthread_mutex_init(&e->camera_mtx, NULL);
    GB_set_camera_get_pixel_callback(&e->gb, cam_get_pixel_cb);
    GB_set_camera_update_request_callback(&e->gb, cam_update_request_cb);
```
In `sb_emu_destroy`: `pthread_mutex_destroy(&e->camera_mtx);`.
`GB_set_camera_*` / `GB_camera_updated` are visible via gb.h (includes camera.h). **No new
include needed.**

### Host test — `test/test_emulator.c`: add a standalone `test_camera()`, call from `main`.
`camera_sensor`/`camera_staging` are private, so the meaningful, verifiable contract is the
pure `sb_camera_read` clamp (the risky logic) + deliver/promote don't crash and `wanted`
starts false. (The staging→sensor→get_pixel path is exercised end-to-end on-device.)
```c
static void test_camera(void)
{
    size_t rlen; uint8_t *rom = make_rom(&rlen, 0);
    sb_emulator *e = sb_emu_create(0x002, rom, rlen, NULL, 0);
    assert(e);
    assert(!sb_emu_camera_wanted(e));                 /* nothing requested yet */
    uint8_t frame[SB_CAM_W * SB_CAM_H];
    for (int y = 0; y < SB_CAM_H; y++)
        for (int x = 0; x < SB_CAM_W; x++) frame[y * SB_CAM_W + x] = (uint8_t)(x ^ y);
    sb_emu_camera_deliver(e, frame);                  /* → staging (no crash) */
    sb_camera_promote(e);                             /* staging → sensor (no crash) */
    /* pure clamp helper: exact fetch + out-of-range/negative clamp */
    assert(sb_camera_read(frame, 10, 20) == (uint8_t)(10 ^ 20));
    assert(sb_camera_read(frame, 200, 20) == sb_camera_read(frame, 127, 20)); /* x clamp */
    assert(sb_camera_read(frame, 10, 200) == sb_camera_read(frame, 10, 111)); /* y clamp */
    assert(sb_camera_read(frame, -5, -5) == frame[0]);                        /* neg clamp */
    sb_emu_destroy(e);
    free(rom);
}
```
Add `test_camera();` to `main`'s call list (after `test_printer();`).

**Acceptance:** host tests green; `sb_camera_read` clamp correct (x/y/negative);
deliver/promote run without crash; `sb_emu_camera_wanted` starts false.

---

## Task 3 — Session layer (self-park connect/disconnect + passthroughs)

**Files:** `Android/jni/session.c`, `Android/jni/session.h`, `Android/jni/test/test_session.c`.

### session.h — add after `sb_session_rumble_amplitude`
```c
void     sb_session_connect_printer(sb_session *s);     /* self-parks (GB_connect_printer) */
void     sb_session_disconnect_printer(sb_session *s);  /* self-parks */
unsigned sb_session_printer_generation(sb_session *s);
unsigned sb_session_printer_feed(sb_session *s, uint32_t *dst, unsigned max_rows);
void     sb_session_printer_clear(sb_session *s);
bool     sb_session_camera_wanted(sb_session *s);
void     sb_session_camera_deliver(sb_session *s, const uint8_t *gray);
```
(session.h includes emulator.h → `bool` available.)

### session.c — add near `sb_session_rumble_amplitude`
```c
void sb_session_connect_printer(sb_session *s)
{
    if (!s) return;
    int was = park_begin(s);
    sb_emu_connect_printer(s->emu);
    park_end(s, was);
}

void sb_session_disconnect_printer(sb_session *s)
{
    if (!s) return;
    int was = park_begin(s);
    sb_emu_disconnect_printer(s->emu);
    park_end(s, was);
}

unsigned sb_session_printer_generation(sb_session *s)
{
    return s ? sb_emu_printer_generation(s->emu) : 0;
}

unsigned sb_session_printer_feed(sb_session *s, uint32_t *dst, unsigned max_rows)
{
    return s ? sb_emu_printer_feed(s->emu, dst, max_rows) : 0;
}

void sb_session_printer_clear(sb_session *s)
{
    if (s) sb_emu_printer_clear(s->emu);
}

bool sb_session_camera_wanted(sb_session *s)
{
    return s ? sb_emu_camera_wanted(s->emu) : false;
}

void sb_session_camera_deliver(sb_session *s, const uint8_t *gray)
{
    if (s) sb_emu_camera_deliver(s->emu, gray);
}
```

### Session test — `test/test_session.c`, add in `main` after the model-switch test, before
teardown (the session is started/running there):
```c
    /* --- printer connect while running self-parks, no deadlock (M7) --- */
    sb_session_connect_printer(s);
    unsigned g0 = sb_session_printer_generation(s);
    (void)g0;
    sb_session_disconnect_printer(s);
    /* camera passthrough: wanted starts false, deliver doesn't crash */
    assert(!sb_session_camera_wanted(s));
    uint8_t gray[128 * 112];
    memset(gray, 0x80, sizeof(gray));
    sb_session_camera_deliver(s, gray);
```
(`alarm(30)` at the top already nets any park deadlock.)

**Acceptance:** session test green (no SIGALRM); connect/disconnect self-park cleanly;
camera passthrough safe.

---

## Task 4 — JNI bridge

**Files:** `Android/jni/sameboy_jni.c`, `Android/app/src/main/java/io/sameboy/android/NativeBridge.java`.

### NativeBridge.java — add after `nativeSetPalette` (keep the `(long ctx)` convention)
```java
    public static native void nativeConnectPrinter(long ctx);
    public static native void nativeDisconnectPrinter(long ctx);
    public static native int nativePrinterGeneration(long ctx);
    public static native int[] nativePrinterFeed(long ctx);      // ARGB, length rows*160 (0 if empty)
    public static native void nativePrinterClear(long ctx);
    public static native boolean nativeCameraWanted(long ctx);
    public static native void nativeCameraDeliver(long ctx, byte[] gray);  // 128*112 bytes
```

### sameboy_jni.c — add (match existing `(sb_session *)(uintptr_t)ctx` style)
```c
JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeConnectPrinter(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; sb_session_connect_printer((sb_session *)(uintptr_t)ctx); }

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeDisconnectPrinter(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; sb_session_disconnect_printer((sb_session *)(uintptr_t)ctx); }

JNIEXPORT jint JNICALL
Java_io_sameboy_android_NativeBridge_nativePrinterGeneration(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; return (jint)sb_session_printer_generation((sb_session *)(uintptr_t)ctx); }

JNIEXPORT jintArray JNICALL
Java_io_sameboy_android_NativeBridge_nativePrinterFeed(JNIEnv *env, jclass c, jlong ctx)
{
    (void)c;
    sb_session *s = (sb_session *)(uintptr_t)ctx;
    unsigned rows = sb_session_printer_feed(s, NULL, 0);
    if (rows == 0) return (*env)->NewIntArray(env, 0);
    jsize n = (jsize)rows * 160;
    jintArray arr = (*env)->NewIntArray(env, n);
    if (!arr) return NULL;
    uint32_t *tmp = malloc((size_t)n * sizeof(uint32_t));
    if (!tmp) return arr;
    sb_session_printer_feed(s, tmp, rows);
    (*env)->SetIntArrayRegion(env, arr, 0, n, (const jint *)tmp);
    free(tmp);
    return arr;
}

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativePrinterClear(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; sb_session_printer_clear((sb_session *)(uintptr_t)ctx); }

JNIEXPORT jboolean JNICALL
Java_io_sameboy_android_NativeBridge_nativeCameraWanted(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; return sb_session_camera_wanted((sb_session *)(uintptr_t)ctx) ? JNI_TRUE : JNI_FALSE; }

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeCameraDeliver(JNIEnv *env, jclass c, jlong ctx, jbyteArray gray)
{
    (void)c;
    sb_session *s = (sb_session *)(uintptr_t)ctx;
    if (!gray) return;
    jsize n = (*env)->GetArrayLength(env, gray);
    if (n != 128 * 112) return;
    jbyte *buf = (*env)->GetByteArrayElements(env, gray, NULL);
    if (!buf) return;
    sb_session_camera_deliver(s, (const uint8_t *)buf);
    (*env)->ReleaseByteArrayElements(env, gray, buf, JNI_ABORT);
}
```
`#include <stdlib.h>` is already present. `sb_session_*` decls come via `session.h`.

**Acceptance:** clean 4-ABI build; `nm` shows the 7 new `NativeBridge_native*` symbols
(total 28).

---

## Task 5 — Printer feed screen + FileProvider

**Files (new):** `Android/app/src/main/java/io/sameboy/android/PrinterFeedActivity.java`,
`Android/app/src/main/res/xml/file_paths.xml`. **Edit:** `AndroidManifest.xml`,
`res/values/strings.xml`.

### file_paths.xml (new)
```xml
<?xml version="1.0" encoding="utf-8"?>
<paths>
    <cache-path name="shared" path="shared/" />
</paths>
```

### AndroidManifest.xml — inside `<application>`, add the activity + provider
```xml
        <activity android:name=".PrinterFeedActivity" android:exported="false"
                  android:configChanges="orientation|screenSize|keyboardHidden" />
        <provider
            android:name="androidx.core.content.FileProvider"
            android:authorities="${applicationId}.fileprovider"
            android:exported="false"
            android:grantUriPermissions="true">
            <meta-data android:name="android.support.FILE_PROVIDER_PATHS"
                       android:resource="@xml/file_paths" />
        </provider>
```
(`androidx.core.content.FileProvider` ships with appcompat 1.7.0 — no new dep.)

### strings.xml — add
```xml
    <string name="printer_feed">Printer feed</string>
    <string name="printer_empty">No printouts yet.</string>
    <string name="save">Save</string>
    <string name="share">Share</string>
    <string name="clear">Clear</string>
    <string name="saved_to_pictures">Saved to Pictures/SameBoy</string>
    <string name="connect_accessory">Connect accessory</string>
    <string name="accessory_none">None</string>
    <string name="accessory_printer">Game Boy Printer</string>
```

### PrinterFeedActivity.java (new)
```java
package io.sameboy.android;

import android.app.Activity;
import android.content.ContentValues;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.MediaStore;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.FileProvider;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;

/** Shows the accumulated Game Boy Printer feed; save to Pictures / share PNG / clear.
 *  Reads pixels via NativeBridge.nativePrinterFeed on the singleton session ctx. */
public final class PrinterFeedActivity extends AppCompatActivity {
    public static final String EXTRA_CTX = "io.sameboy.ctx";
    private long ctx;
    private Bitmap bitmap;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        ctx = getIntent().getLongExtra(EXTRA_CTX, 0);
        bitmap = buildBitmap();

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.BLACK);

        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        Button save = new Button(this);  save.setText(R.string.save);
        Button share = new Button(this); share.setText(R.string.share);
        Button clear = new Button(this); clear.setText(R.string.clear);
        bar.addView(save); bar.addView(share); bar.addView(clear);
        root.addView(bar);

        ScrollView scroll = new ScrollView(this);
        if (bitmap != null) {
            ImageView iv = new ImageView(this);
            iv.setImageBitmap(bitmap);
            iv.setAdjustViewBounds(true);
            iv.setScaleType(ImageView.ScaleType.FIT_CENTER);
            scroll.addView(iv);
        } else {
            TextView tv = new TextView(this);
            tv.setText(R.string.printer_empty);
            tv.setTextColor(Color.WHITE);
            tv.setGravity(Gravity.CENTER);
            tv.setPadding(0, 64, 0, 0);
            scroll.addView(tv);
        }
        root.addView(scroll, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
        setContentView(root);

        boolean has = bitmap != null;
        save.setEnabled(has); share.setEnabled(has);
        save.setOnClickListener(v -> saveToPictures());
        share.setOnClickListener(v -> sharePng());
        clear.setOnClickListener(v -> { NativeBridge.nativePrinterClear(ctx); finish(); });
    }

    private Bitmap buildBitmap() {
        int[] px = NativeBridge.nativePrinterFeed(ctx);
        if (px == null || px.length < 160) return null;
        int rows = px.length / 160;
        Bitmap bmp = Bitmap.createBitmap(160, rows, Bitmap.Config.ARGB_8888);
        bmp.setPixels(px, 0, 160, 0, 0, 160, rows);
        return bmp;
    }

    private void saveToPictures() {
        if (bitmap == null) return;
        String name = "SameBoy_" + System.currentTimeMillis() + ".png";
        try {
            if (Build.VERSION.SDK_INT >= 29) {
                ContentValues cv = new ContentValues();
                cv.put(MediaStore.Images.Media.DISPLAY_NAME, name);
                cv.put(MediaStore.Images.Media.MIME_TYPE, "image/png");
                cv.put(MediaStore.Images.Media.RELATIVE_PATH, "Pictures/SameBoy");
                Uri uri = getContentResolver().insert(
                        MediaStore.Images.Media.EXTERNAL_CONTENT_URI, cv);
                if (uri != null) {
                    OutputStream os = getContentResolver().openOutputStream(uri);
                    bitmap.compress(Bitmap.CompressFormat.PNG, 100, os);
                    os.close();
                    Toast.makeText(this, R.string.saved_to_pictures, Toast.LENGTH_SHORT).show();
                }
            } else {
                File dir = new File(android.os.Environment.getExternalStoragePublicDirectory(
                        android.os.Environment.DIRECTORY_PICTURES), "SameBoy");
                dir.mkdirs();
                File f = new File(dir, name);
                FileOutputStream os = new FileOutputStream(f);
                bitmap.compress(Bitmap.CompressFormat.PNG, 100, os);
                os.close();
                Toast.makeText(this, R.string.saved_to_pictures, Toast.LENGTH_SHORT).show();
            }
        } catch (Exception e) {
            Toast.makeText(this, "Save failed", Toast.LENGTH_SHORT).show();
        }
    }

    private void sharePng() {
        if (bitmap == null) return;
        try {
            File dir = new File(getCacheDir(), "shared");
            dir.mkdirs();
            File f = new File(dir, "printout.png");
            FileOutputStream os = new FileOutputStream(f);
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, os);
            os.close();
            Uri uri = FileProvider.getUriForFile(this, getPackageName() + ".fileprovider", f);
            Intent i = new Intent(Intent.ACTION_SEND);
            i.setType("image/png");
            i.putExtra(Intent.EXTRA_STREAM, uri);
            i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivity(Intent.createChooser(i, getString(R.string.share)));
        } catch (Exception e) {
            Toast.makeText(this, "Share failed", Toast.LENGTH_SHORT).show();
        }
    }
}
```
**Acceptance:** builds; activity + provider registered; empty feed shows "No printouts
yet." with Save/Share disabled.

---

## Task 6 — Accessory menu + Printer feed entry in the in-game menu

**Files:** `Android/app/src/main/java/io/sameboy/android/GameMenuDialog.java`,
`Android/app/src/main/java/io/sameboy/android/EmulatorActivity.java`.

### GameMenuDialog.java
Add to `interface Host`:
```java
        void onConnectAccessory();
        void onPrinterFeed();
        boolean printerConnected();
        boolean hasPrintouts();
```
In `show`, extend the items array and the switch. New items list:
```java
        final String[] items = { "Resume", "Save state", "Load state", "Reset", "Model",
                                  "Connect accessory", "Printer feed", "Settings", "Exit" };
```
Update the `switch (which)` cases to the new indices (0 Resume … 4 Model, **5 Connect
accessory → chained[0]=true; showAccessory(a, h)**, **6 Printer feed → chained stays false;
h.onPrinterFeed()** (launches an Activity; menu dismisses, onMenuClosed unpauses like the
Settings case at old idx 5), **7 Settings**, **8 Exit**). Keep the Settings behavior
identical to before (just its index moved 5→7). Add:
```java
    private static void showAccessory(Activity a, Host h) {
        final String[] opts = { a.getString(R.string.accessory_none),
                                a.getString(R.string.accessory_printer) };
        int current = h.printerConnected() ? 1 : 0;
        final boolean[] chained = { false };
        AlertDialog dlg = new AlertDialog.Builder(a)
            .setTitle(R.string.connect_accessory)
            .setSingleChoiceItems(opts, current, (d, which) -> {
                h.onConnectAccessory();       // Activity applies connect/disconnect by `which`
                d.dismiss();
            })
            .create();
        dlg.setOnDismissListener(d -> h.onMenuClosed());
        dlg.show();
    }
```
Simpler: pass the chosen index. Change `onConnectAccessory()` to
`onConnectAccessory(int which)` (0 None, 1 Printer). Wire the single-choice handler to call
`h.onConnectAccessory(which)`. (Keep it minimal — one accessory today, but the dialog
generalizes.)

### EmulatorActivity.java — implement the new Host methods
```java
    @Override public void onConnectAccessory(int which) {
        if (ctx == 0) return;
        if (which == 1) { NativeBridge.nativeConnectPrinter(ctx); printerConnected = true; }
        else            { NativeBridge.nativeDisconnectPrinter(ctx); printerConnected = false; }
    }
    @Override public void onPrinterFeed() {
        Intent i = new Intent(this, PrinterFeedActivity.class);
        i.putExtra(PrinterFeedActivity.EXTRA_CTX, ctx);
        startActivity(i);
    }
    @Override public boolean printerConnected() { return printerConnected; }
    @Override public boolean hasPrintouts() { return ctx != 0 && NativeBridge.nativePrinterGeneration(ctx) > 0; }
```
Add field `private boolean printerConnected = false;`. `onPrinterFeed` launches the feed
Activity — this is a normal navigation (menu dismisses → `onMenuClosed` → unpause), and on
return `onResume` re-applies settings + unpauses, consistent with the Settings path. The
"Printer feed" item may be shown always (feed screen handles the empty state), or gate on
`hasPrintouts()` — gate it: in `GameMenuDialog.show`, if `!h.hasPrintouts()` you may still
show the row but that's fine; keep it always-visible for simplicity (empty → friendly text).

**Acceptance:** builds; menu shows Connect accessory + Printer feed; selecting Printer
connects (subsequent reopen shows it checked); Printer feed opens the Activity.

---

## Task 7 — Camera2 capture in EmulatorActivity

**Files:** `Android/app/src/main/java/io/sameboy/android/EmulatorActivity.java`,
`AndroidManifest.xml`.

### Manifest — add at top (after VIBRATE)
```xml
    <uses-permission android:name="android.permission.CAMERA" />
    <uses-feature android:name="android.hardware.camera" android:required="false" />
```

### EmulatorActivity.java — Camera2 driven by `nativeCameraWanted`
Add fields:
```java
    private static final int REQ_CAMERA = 42;
    private android.hardware.camera2.CameraDevice cameraDevice;
    private android.hardware.camera2.CameraCaptureSession cameraSession;
    private android.media.ImageReader cameraReader;
    private android.os.HandlerThread cameraThread;
    private android.os.Handler cameraHandler;
    private boolean cameraRunning = false;
    private long lastCameraWant = 0;
    private final byte[] cameraGray = new byte[128 * 112];
    private final Runnable cameraPoll = new Runnable() {
        @Override public void run() {
            if (ctx != 0 && NativeBridge.nativeCameraWanted(ctx)) {
                lastCameraWant = android.os.SystemClock.uptimeMillis();
                if (!cameraRunning) ensureCameraPermissionAndStart();
            } else if (cameraRunning &&
                       android.os.SystemClock.uptimeMillis() - lastCameraWant > 1500) {
                stopCamera();   // idle → release the camera (mirrors iOS 1s disable timer)
            }
            handler.postDelayed(this, 200);
        }
    };
```
Start/stop the poller in `onResume`/`onPause` alongside `rumblePoll`
(`handler.postDelayed(cameraPoll, 500);` / `handler.removeCallbacks(cameraPoll); stopCamera();`).

Permission + open:
```java
    private void ensureCameraPermissionAndStart() {
        if (checkSelfPermission(android.Manifest.permission.CAMERA)
                != android.content.pm.PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{ android.Manifest.permission.CAMERA }, REQ_CAMERA);
            return;   // onRequestPermissionsResult starts it if granted
        }
        openCamera();
    }

    @Override public void onRequestPermissionsResult(int req, String[] p, int[] r) {
        super.onRequestPermissionsResult(req, p, r);
        if (req == REQ_CAMERA) {
            if (r.length > 0 && r[0] == android.content.pm.PackageManager.PERMISSION_GRANTED) openCamera();
            else Toast.makeText(this, "Camera denied; using noise", Toast.LENGTH_SHORT).show();
        }
    }

    @SuppressWarnings("MissingPermission")
    private void openCamera() {
        if (cameraRunning) return;
        android.hardware.camera2.CameraManager cm =
                (android.hardware.camera2.CameraManager) getSystemService(CAMERA_SERVICE);
        try {
            String pick = null;
            for (String id : cm.getCameraIdList()) {
                Integer f = cm.getCameraCharacteristics(id).get(
                        android.hardware.camera2.CameraCharacteristics.LENS_FACING);
                if (pick == null) pick = id;
                if (f != null && f == android.hardware.camera2.CameraCharacteristics.LENS_FACING_BACK) { pick = id; break; }
            }
            if (pick == null) return;   // no camera (e.g. Waydroid) → ROM keeps its noise
            cameraThread = new android.os.HandlerThread("sb-cam");
            cameraThread.start();
            cameraHandler = new android.os.Handler(cameraThread.getLooper());
            cameraReader = android.media.ImageReader.newInstance(
                    176, 144, android.graphics.ImageFormat.YUV_420_888, 2);
            cameraReader.setOnImageAvailableListener(reader -> {
                android.media.Image img = reader.acquireLatestImage();
                if (img != null) { deliverFrame(img); img.close(); }
            }, cameraHandler);
            cameraRunning = true;
            cm.openCamera(pick, new android.hardware.camera2.CameraDevice.StateCallback() {
                @Override public void onOpened(android.hardware.camera2.CameraDevice d) {
                    cameraDevice = d;
                    try {
                        final android.hardware.camera2.CaptureRequest.Builder rb =
                            d.createCaptureRequest(android.hardware.camera2.CameraDevice.TEMPLATE_PREVIEW);
                        rb.addTarget(cameraReader.getSurface());
                        d.createCaptureSession(
                            java.util.Collections.singletonList(cameraReader.getSurface()),
                            new android.hardware.camera2.CameraCaptureSession.StateCallback() {
                                @Override public void onConfigured(android.hardware.camera2.CameraCaptureSession s) {
                                    cameraSession = s;
                                    try { s.setRepeatingRequest(rb.build(), null, cameraHandler); }
                                    catch (Exception ignored) {}
                                }
                                @Override public void onConfigureFailed(android.hardware.camera2.CameraCaptureSession s) {}
                            }, cameraHandler);
                    } catch (Exception ignored) {}
                }
                @Override public void onDisconnected(android.hardware.camera2.CameraDevice d) { d.close(); cameraDevice = null; }
                @Override public void onError(android.hardware.camera2.CameraDevice d, int e) { d.close(); cameraDevice = null; }
            }, cameraHandler);
        } catch (Exception e) { cameraRunning = false; }
    }

    /** Y plane → center-cropped 128x112 grayscale → native. */
    private void deliverFrame(android.media.Image img) {
        android.media.Image.Plane y = img.getPlanes()[0];
        java.nio.ByteBuffer buf = y.getBuffer();
        int rowStride = y.getRowStride();
        int w = img.getWidth(), h = img.getHeight();
        int cropX = Math.max(0, (w - 128) / 2);
        int cropY = Math.max(0, (h - 112) / 2);
        for (int ry = 0; ry < 112; ry++) {
            int sy = Math.min(h - 1, cropY + ry);
            for (int rx = 0; rx < 128; rx++) {
                int sx = Math.min(w - 1, cropX + rx);
                cameraGray[ry * 128 + rx] = buf.get(sy * rowStride + sx);
            }
        }
        if (ctx != 0) NativeBridge.nativeCameraDeliver(ctx, cameraGray);
    }

    private void stopCamera() {
        cameraRunning = false;
        try { if (cameraSession != null) cameraSession.close(); } catch (Exception ignored) {}
        try { if (cameraDevice != null) cameraDevice.close(); } catch (Exception ignored) {}
        try { if (cameraReader != null) cameraReader.close(); } catch (Exception ignored) {}
        cameraSession = null; cameraDevice = null; cameraReader = null;
        if (cameraThread != null) { cameraThread.quitSafely(); cameraThread = null; cameraHandler = null; }
    }
```
Note `176x144` (QCIF) is a near-universal YUV size; if the crop underflows (smaller than
128x112) the `Math.min` clamps keep it in-bounds (image degrades gracefully). `getSystemService(CAMERA_SERVICE)`
constant is `Context.CAMERA_SERVICE`.

**Acceptance:** builds; on a Camera ROM's request the CAMERA permission is requested; frames
deliver when granted; deny / no-camera → no crash (ROM shows Core noise); camera released on
idle + `onPause`.

---

## Task 8 — Integration: build + host tests + symbol/permission check

**Files:** none (verification only).

Run:
```
Android/jni/test/run_host_tests.sh
cd Android && JAVA_HOME=$HOME/Android/jdk17 ANDROID_HOME=$HOME/Android ./gradlew clean :app:assembleDebug
# 4 ABIs present:
unzip -l app/build/outputs/apk/debug/app-debug.apk | grep -c 'libsameboy_core.so'
# 28 JNI symbols:
nm -D --defined-only $(find app/build -name libsameboy_core.so | head -1) | grep -c NativeBridge_native
# CAMERA permission in the built APK:
$HOME/Android/build-tools/*/aapt2 dump permissions app/build/outputs/apk/debug/app-debug.apk | grep -i camera
```
**Acceptance:** host tests pass; clean build; 4 `.so`; 28 `NativeBridge_native*` symbols;
CAMERA (+VIBRATE) permissions present.

---

## Task ordering & concurrency
- Task 1, Task 2 both edit `emulator.c`/`.h` + `test_emulator.c` — **serialize** (same files);
  do Task 1 then Task 2.
- Task 3 depends on 1+2 (calls `sb_emu_*`). Task 4 depends on 3.
- Task 5 (new files + manifest/strings) is independent of native — can run parallel to 1–4.
- Task 6 depends on 5 (launches `PrinterFeedActivity`) + 4 (JNI). Task 7 depends on 4 (JNI)
  + edits `EmulatorActivity`/manifest — **serialize with Task 6** (same file + manifest).
- Task 8 last.

Suggested waves: **[1] → [2] → [3,5] → [4] → [6] → [7] → [8]** (5 can overlap 2/3).
