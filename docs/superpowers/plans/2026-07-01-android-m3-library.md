# SameBoy Android M3 — Library Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Persistent ROM library browser — SAF folder/file import, background scan with title+CRC metadata, favorites/recents, zip launch, and background ROM reads — per `specs/2026-07-01-android-m3-library.md`.

**Architecture:** A JSON index (`library.json`) in internal storage holds `LibraryEntry` records (uri, zipEntry, displayName, title, crc32, favorite, lastPlayed). `MainActivity` is a `GridView` browser; imports run a `RomScanner` (SAF `DocumentsContract` walk) on a single background executor, computing metadata via a new native `nativeRomInfo` (throwaway Core init). CRC32 dedups. `EmulatorActivity` reads ROM bytes off the main thread and keys saves off the CRC.

**Tech Stack:** C (Core reuse), JNI, Java (framework `GridView`/`BaseAdapter`, `DocumentsContract`, `org.json`, `java.util.zip` — no new Gradle deps), programmatic UI (no XML layouts).

## Global Constraints

- **Never modify `Core/`** — metadata via existing `GB_init`/`GB_load_rom_from_buffer`/`GB_get_rom_title`/`GB_get_rom_crc32`/`GB_free`.
- **No new Gradle dependencies.** No Room, no RecyclerView, no documentfile artifact. `org.json`, `DocumentsContract`, `java.util.zip`, framework `GridView` only.
- **No XML layouts / resources** beyond `strings.xml` (programmatic UI, M1/M2 convention). Java 8-compatible.
- Build: `cd ~/SameBoy/Android && JAVA_HOME=$HOME/Android/jdk17 ANDROID_HOME=$HOME/Android ./gradlew :app:assembleDebug`.
- Host tests: `Android/jni/test/run_host_tests.sh` (must stay green end-to-end).
- `sb_rom_info` rejects only `len < 0x150` (Core load is `void`); extension/zip-entry filtering is the scanner's job before calling it.
- Dedup key = **CRC32** (8-hex uppercase). Save/state key = CRC for library launches, display-name for external one-shot opens (M2 fallback preserved). Never migrate existing saves.
- Import persists the SAF grant: `takePersistableUriPermission(uri, data.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION)`.
- `library.json` written atomically (temp + rename) in `getFilesDir()`.
- Commit after each task; prefix `feat(android):` / `fix(android):` / `test(android):`.

---

### Task 1: `sb_rom_info` native metadata + host test

**Files:**
- Modify: `Android/jni/emulator.h`, `Android/jni/emulator.c`
- Test: `Android/jni/test/test_emulator.c`

**Interfaces:**
- Produces: `int sb_rom_info(const uint8_t *rom, size_t len, char *title, uint32_t *crc32);` — `title` ≥ 17 bytes, NUL-terminated; returns 0 on success, -1 if `len < 0x150`. Task 2's JNI calls this.

- [ ] **Step 1: Write the failing test.** In `test_emulator.c`, add a title-bearing ROM helper and a test; call it from `main`.

Add near the top (after existing includes; `string.h` and `Core/gb.h` are already included from the M2 tasks — if not, add `#include <string.h>`):

```c
/* Reference CRC32 (zlib/ISO-HDLC) over an exact buffer — matches GB_get_rom_crc32
   when the ROM is already a power-of-two size (no Core padding). */
static uint32_t ref_crc32(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
    }
    return ~crc;
}

static void test_rom_info(void)
{
    size_t rlen; uint8_t *rom = make_rom(&rlen, 0);   /* exactly 0x8000, power of two */
    /* stamp a known title at 0x134 (printable, < 0x10 chars) */
    const char *want = "TESTROM";
    memset(&rom[0x134], 0, 0x10);
    memcpy(&rom[0x134], want, strlen(want));

    char title[17];
    uint32_t crc = 0;
    assert(sb_rom_info(rom, rlen, title, &crc) == 0);
    assert(strcmp(title, want) == 0);
    assert(crc == ref_crc32(rom, rlen));   /* CRC over the unpadded 32KB buffer */

    /* too-small buffer rejected */
    uint8_t tiny[4] = {1,2,3,4};
    assert(sb_rom_info(tiny, sizeof(tiny), title, &crc) == -1);

    free(rom);
}
```

Call it from `main` (before the final `printf`): `test_rom_info();`

- [ ] **Step 2: Run to verify failure** — `Android/jni/test/run_host_tests.sh` → compile error: `sb_rom_info` undeclared.

- [ ] **Step 3: Implement.** `emulator.h` — add after `sb_emu_*` decls, before `SB_AUDIO_SAMPLE_RATE`:

```c
/* Reads ROM title + CRC32 via a throwaway Core init (no session/emulator).
   title must be >= 17 bytes; it is NUL-terminated. Returns 0 on success,
   -1 if len < 0x150 (too small to be a cartridge). */
int sb_rom_info(const uint8_t *rom, size_t len, char *title, uint32_t *crc32);
```

`emulator.c` — add near the bottom (before `sb_emu_destroy`); `#include <stdlib.h>` is already present:

```c
int sb_rom_info(const uint8_t *rom, size_t len, char *title, uint32_t *crc32)
{
    if (len < 0x150) return -1;
    /* GB_gameboy_t is large and embedded by value elsewhere; heap-allocate it
       rather than risk a worker-thread stack. */
    GB_gameboy_t *gb = malloc(sizeof(GB_gameboy_t));
    if (!gb) return -1;
    GB_init(gb, GB_MODEL_CGB_E);
    GB_load_rom_from_buffer(gb, rom, len);
    GB_get_rom_title(gb, title);
    *crc32 = GB_get_rom_crc32(gb);
    GB_free(gb);
    free(gb);
    return 0;
}
```

- [ ] **Step 4: Run tests** — `Android/jni/test/run_host_tests.sh` → `ALL HOST TESTS PASSED`.

- [ ] **Step 5: Commit**

```bash
git add Android/jni/emulator.h Android/jni/emulator.c Android/jni/test/test_emulator.c
git commit -m "feat(android): sb_rom_info (throwaway-init ROM title + CRC32) for library metadata"
```

---

### Task 2: `nativeRomInfo` JNI + NativeBridge

**Files:**
- Modify: `Android/jni/sameboy_jni.c`, `Android/app/src/main/java/io/sameboy/android/NativeBridge.java`

**Interfaces:**
- Consumes: `sb_rom_info` (Task 1).
- Produces: `public static native String[] nativeRomInfo(byte[] rom);` → `{ title, crc32Hex8Upper }` or `null` when the ROM is invalid. Tasks 5–6 call this.

- [ ] **Step 1: Add the Java declaration** to `NativeBridge.java` (after `nativeCopyFrame`):

```java
    /** Returns { title, crc32Hex8Upper } for a ROM buffer, or null if not a valid ROM. */
    public static native String[] nativeRomInfo(byte[] rom);
```

- [ ] **Step 2: Add the C implementation** to `sameboy_jni.c`. Add `#include <stdio.h>` at the top (for `snprintf`) if absent, then (after `nativeCopyFrame`):

```c
JNIEXPORT jobjectArray JNICALL
Java_io_sameboy_android_NativeBridge_nativeRomInfo(JNIEnv *env, jclass c, jbyteArray rom)
{
    (void)c;
    if (!rom) return NULL;
    jsize n = (*env)->GetArrayLength(env, rom);
    jbyte *bytes = (*env)->GetByteArrayElements(env, rom, NULL);
    if (!bytes) return NULL;
    char title[17];
    uint32_t crc = 0;
    int ret = sb_rom_info((const uint8_t *)bytes, (size_t)n, title, &crc);
    (*env)->ReleaseByteArrayElements(env, rom, bytes, JNI_ABORT);
    if (ret != 0) return NULL;
    char hex[9];
    snprintf(hex, sizeof(hex), "%08X", crc);
    jclass strClass = (*env)->FindClass(env, "java/lang/String");
    jobjectArray arr = (*env)->NewObjectArray(env, 2, strClass, NULL);
    if (!arr) return NULL;
    jstring jtitle = (*env)->NewStringUTF(env, title);
    jstring jcrc = (*env)->NewStringUTF(env, hex);
    (*env)->SetObjectArrayElement(env, arr, 0, jtitle);
    (*env)->SetObjectArrayElement(env, arr, 1, jcrc);
    return arr;
}
```

- [ ] **Step 3: Build + verify symbol** —

```bash
cd ~/SameBoy/Android && JAVA_HOME=$HOME/Android/jdk17 ANDROID_HOME=$HOME/Android ./gradlew :app:assembleDebug
D=app/build/intermediates/merged_native_libs/debug/mergeDebugNativeLibs/out/lib/arm64-v8a
nm -D --defined-only $D/libsameboy_core.so | grep -c NativeBridge_native
```
Expected: `BUILD SUCCESSFUL`; count `17` (16 from M2 + `nativeRomInfo`).

- [ ] **Step 4: Commit**

```bash
git add Android/jni/sameboy_jni.c Android/app/src/main/java/io/sameboy/android/NativeBridge.java
git commit -m "feat(android): nativeRomInfo JNI returning {title, crc32} for the library"
```

---

### Task 3: LibraryEntry + Library (JSON store)

**Files:**
- Create: `Android/app/src/main/java/io/sameboy/android/LibraryEntry.java`
- Create: `Android/app/src/main/java/io/sameboy/android/Library.java`

**Interfaces:**
- Consumes: `SaveStore.read(File)` (M2, existing).
- Produces (Tasks 5–6 use these):
  - `LibraryEntry(String uri, String zipEntry, String displayName, String title, String crc32, boolean favorite, long lastPlayed)`; fields public-package; `String label()`; `JSONObject toJson()`; `static LibraryEntry fromJson(JSONObject)`.
  - `Library(Context)`; `void load()`; `void save()`; `boolean add(LibraryEntry)` (dedup by crc32, preserves existing favorite/lastPlayed); `void setFavorite(String crc32, boolean)`; `void remove(String crc32)`; `void touch(String crc32)`; `List<LibraryEntry> listSorted()`; `boolean isEmpty()`. **Main-thread only.**

- [ ] **Step 1: Create `LibraryEntry.java`:**

```java
package io.sameboy.android;

import org.json.JSONException;
import org.json.JSONObject;

/** One ROM in the library. crc32 is the identity (dedup + save key). */
final class LibraryEntry {
    final String uri;         // SAF document uri string
    final String zipEntry;    // null unless the ROM lives inside a zip
    final String displayName; // file name (with extension), for display fallback
    final String title;       // GB_get_rom_title, may be empty
    final String crc32;       // 8-hex uppercase
    boolean favorite;
    long lastPlayed;          // epoch millis, 0 = never

    LibraryEntry(String uri, String zipEntry, String displayName, String title,
                 String crc32, boolean favorite, long lastPlayed) {
        this.uri = uri; this.zipEntry = zipEntry; this.displayName = displayName;
        this.title = title; this.crc32 = crc32; this.favorite = favorite;
        this.lastPlayed = lastPlayed;
    }

    /** Grid label: internal ROM title if present, else the file name. */
    String label() {
        return (title != null && !title.isEmpty()) ? title : displayName;
    }

    JSONObject toJson() throws JSONException {
        JSONObject o = new JSONObject();
        o.put("uri", uri);
        if (zipEntry != null) o.put("zipEntry", zipEntry);
        o.put("displayName", displayName);
        o.put("title", title);
        o.put("crc32", crc32);
        o.put("favorite", favorite);
        o.put("lastPlayed", lastPlayed);
        return o;
    }

    static LibraryEntry fromJson(JSONObject o) {
        return new LibraryEntry(
            o.optString("uri", ""),
            o.has("zipEntry") ? o.optString("zipEntry", null) : null,
            o.optString("displayName", "rom"),
            o.optString("title", ""),
            o.optString("crc32", ""),
            o.optBoolean("favorite", false),
            o.optLong("lastPlayed", 0));
    }
}
```

- [ ] **Step 2: Create `Library.java`:**

```java
package io.sameboy.android;

import android.content.Context;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONTokener;

import java.io.File;
import java.io.FileOutputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;

/** Persistent ROM library backed by a JSON index in internal storage.
 *  Not thread-safe — call from the main thread only. */
final class Library {
    private static final String FILE = "library.json";
    private final Context ctx;
    private final List<LibraryEntry> entries = new ArrayList<>();

    Library(Context ctx) { this.ctx = ctx.getApplicationContext(); }

    void load() {
        entries.clear();
        byte[] data = SaveStore.read(new File(ctx.getFilesDir(), FILE));
        if (data == null) return;
        try {
            JSONArray arr = (JSONArray) new JSONTokener(new String(data, "UTF-8")).nextValue();
            for (int i = 0; i < arr.length(); i++) entries.add(LibraryEntry.fromJson(arr.getJSONObject(i)));
        } catch (Exception e) {
            Log.e("SameBoy", "library load failed", e);
        }
    }

    void save() {
        JSONArray arr = new JSONArray();
        try {
            for (LibraryEntry e : entries) arr.put(e.toJson());
        } catch (Exception e) { Log.e("SameBoy", "library serialize failed", e); return; }
        File dir = ctx.getFilesDir();
        File tmp = new File(dir, FILE + ".tmp");
        File dst = new File(dir, FILE);
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(arr.toString().getBytes("UTF-8"));
        } catch (Exception e) { Log.e("SameBoy", "library write failed", e); return; }
        if (!tmp.renameTo(dst)) {
            dst.delete();
            if (!tmp.renameTo(dst)) Log.e("SameBoy", "library rename failed");
        }
    }

    /** Add unless a same-crc32 entry exists. Returns true if newly added.
     *  A duplicate is ignored (existing favorite/lastPlayed preserved). */
    boolean add(LibraryEntry e) {
        if (e.crc32 == null || e.crc32.isEmpty()) return false;
        for (LibraryEntry x : entries) if (e.crc32.equals(x.crc32)) return false;
        entries.add(e);
        return true;
    }

    void setFavorite(String crc32, boolean fav) {
        for (LibraryEntry e : entries) if (e.crc32.equals(crc32)) { e.favorite = fav; return; }
    }

    void remove(String crc32) {
        for (int i = 0; i < entries.size(); i++)
            if (entries.get(i).crc32.equals(crc32)) { entries.remove(i); return; }
    }

    void touch(String crc32) {
        for (LibraryEntry e : entries)
            if (e.crc32.equals(crc32)) { e.lastPlayed = System.currentTimeMillis(); return; }
    }

    List<LibraryEntry> listSorted() {
        List<LibraryEntry> out = new ArrayList<>(entries);
        Collections.sort(out, new Comparator<LibraryEntry>() {
            @Override public int compare(LibraryEntry a, LibraryEntry b) {
                if (a.favorite != b.favorite) return a.favorite ? -1 : 1;
                if (a.lastPlayed != b.lastPlayed) return a.lastPlayed > b.lastPlayed ? -1 : 1;
                return a.label().compareToIgnoreCase(b.label());
            }
        });
        return out;
    }

    boolean isEmpty() { return entries.isEmpty(); }
}
```

- [ ] **Step 3: Build** — `./gradlew :app:assembleDebug` → `BUILD SUCCESSFUL`.

- [ ] **Step 4: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/LibraryEntry.java Android/app/src/main/java/io/sameboy/android/Library.java
git commit -m "feat(android): LibraryEntry + JSON-backed Library store (CRC dedup, favorites, recents)"
```

---

### Task 4: ZipRoms (zip probe + extract)

**Files:**
- Create: `Android/app/src/main/java/io/sameboy/android/ZipRoms.java`

**Interfaces:**
- Produces (Task 5 + 7 use these): `static boolean isRomName(String)`; `static String firstRomEntry(InputStream)` → first `.gb`/`.gbc` entry name or `null`; `static byte[] extract(InputStream, String entryName)` → bytes or `null`.

- [ ] **Step 1: Create `ZipRoms.java`:**

```java
package io.sameboy.android;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Minimal zip support: find and extract the first Game Boy ROM entry. */
final class ZipRoms {
    private ZipRoms() {}

    static boolean isRomName(String name) {
        String n = name.toLowerCase();
        return n.endsWith(".gb") || n.endsWith(".gbc");
    }

    /** First entry (in zip order) whose name ends in .gb/.gbc, or null. */
    static String firstRomEntry(InputStream in) {
        try (ZipInputStream zin = new ZipInputStream(in)) {
            ZipEntry e;
            while ((e = zin.getNextEntry()) != null) {
                if (!e.isDirectory() && isRomName(e.getName())) return e.getName();
            }
        } catch (Exception ignored) {}
        return null;
    }

    /** Bytes of the named entry, or null if absent/unreadable. */
    static byte[] extract(InputStream in, String entryName) {
        try (ZipInputStream zin = new ZipInputStream(in)) {
            ZipEntry e;
            while ((e = zin.getNextEntry()) != null) {
                if (!e.isDirectory() && e.getName().equals(entryName)) {
                    ByteArrayOutputStream out = new ByteArrayOutputStream();
                    byte[] buf = new byte[65536]; int n;
                    while ((n = zin.read(buf)) > 0) out.write(buf, 0, n);
                    return out.toByteArray();
                }
            }
        } catch (Exception ignored) {}
        return null;
    }
}
```

- [ ] **Step 2: Build** — `./gradlew :app:assembleDebug` → `BUILD SUCCESSFUL`.

- [ ] **Step 3: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/ZipRoms.java
git commit -m "feat(android): ZipRoms - first-ROM-entry probe and extract (java.util.zip)"
```

---

### Task 5: RomScanner (SAF tree walk + metadata)

**Files:**
- Create: `Android/app/src/main/java/io/sameboy/android/RomScanner.java`

**Interfaces:**
- Consumes: `ZipRoms` (Task 4), `NativeBridge.nativeRomInfo` (Task 2), `LibraryEntry` (Task 3).
- Produces (Task 6 uses these): `interface Sink { void onRom(LibraryEntry entry); }`; `static void scanTree(Context, Uri treeUri, Sink)`; `static void handleFile(ContentResolver, Uri docUri, String name, Sink)`. **Runs on a worker thread; the Sink is invoked on that worker thread.**

- [ ] **Step 1: Create `RomScanner.java`:**

```java
package io.sameboy.android;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.util.ArrayDeque;

/** Recursively scans a SAF tree for GB/GBC/zip ROMs, computes title+CRC via
 *  NativeBridge.nativeRomInfo, and reports each valid ROM through a Sink.
 *  Blocking; call on a background thread. */
final class RomScanner {
    interface Sink { void onRom(LibraryEntry entry); }

    private RomScanner() {}

    static void scanTree(Context ctx, Uri treeUri, Sink sink) {
        ContentResolver cr = ctx.getContentResolver();
        ArrayDeque<String> stack = new ArrayDeque<>();
        stack.push(DocumentsContract.getTreeDocumentId(treeUri));
        int guard = 0;
        while (!stack.isEmpty() && guard++ < 100000) {
            String docId = stack.pop();
            Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, docId);
            try (Cursor c = cr.query(children, new String[]{
                    DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                    DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                    DocumentsContract.Document.COLUMN_MIME_TYPE }, null, null, null)) {
                if (c == null) continue;
                while (c.moveToNext()) {
                    String childId = c.getString(0);
                    String name = c.getString(1);
                    String mime = c.getString(2);
                    if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                        stack.push(childId);
                    } else {
                        Uri docUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, childId);
                        handleFile(cr, docUri, name, sink);
                    }
                }
            } catch (Exception ignored) {}
        }
    }

    /** Handle one document: add if it is (or contains) a valid GB/GBC ROM. */
    static void handleFile(ContentResolver cr, Uri docUri, String name, Sink sink) {
        if (name == null) return;
        String lower = name.toLowerCase();
        if (lower.endsWith(".gb") || lower.endsWith(".gbc")) {
            addRom(sink, readAll(cr, docUri), docUri.toString(), null, name);
        } else if (lower.endsWith(".zip")) {
            String entry;
            try (InputStream in = cr.openInputStream(docUri)) { entry = ZipRoms.firstRomEntry(in); }
            catch (Exception e) { entry = null; }
            if (entry == null) return;
            byte[] rom;
            try (InputStream in = cr.openInputStream(docUri)) { rom = ZipRoms.extract(in, entry); }
            catch (Exception e) { rom = null; }
            addRom(sink, rom, docUri.toString(), entry, name);
        }
    }

    private static void addRom(Sink sink, byte[] rom, String uri, String zipEntry, String name) {
        if (rom == null || rom.length < 0x150) return;
        String[] info = NativeBridge.nativeRomInfo(rom);
        if (info == null) return;   // not a valid ROM
        sink.onRom(new LibraryEntry(uri, zipEntry, name, info[0], info[1], false, 0));
    }

    private static byte[] readAll(ContentResolver cr, Uri uri) {
        try (InputStream in = cr.openInputStream(uri)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[65536]; int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            return out.toByteArray();
        } catch (Exception e) { return null; }
    }
}
```

- [ ] **Step 2: Build** — `./gradlew :app:assembleDebug` → `BUILD SUCCESSFUL`.

- [ ] **Step 3: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/RomScanner.java
git commit -m "feat(android): RomScanner - SAF tree walk + zip probe + metadata via nativeRomInfo"
```

---

### Task 6: MainActivity library browser + strings

**Files:**
- Modify: `Android/app/src/main/java/io/sameboy/android/MainActivity.java`
- Modify: `Android/app/src/main/res/values/strings.xml`

**Interfaces:**
- Consumes: `Library`, `LibraryEntry` (Task 3), `RomScanner` (Task 5); `EmulatorActivity.EXTRA_ZIP_ENTRY` / `EXTRA_ROM_KEY` (Task 7).
- Produces: the library UI (grid, imports, launch). No outward interface.

- [ ] **Step 1: Add strings** to `res/values/strings.xml` (inside `<resources>`):

```xml
    <string name="import_folder">Import Folder</string>
    <string name="library_empty">No games yet. Import a folder or open a ROM.</string>
    <string name="scanning">Scanning…</string>
    <string name="added_n">Added %d game(s)</string>
    <string name="not_a_rom">Not a Game Boy ROM</string>
    <string name="never">Never played</string>
    <string name="play">Play</string>
    <string name="favorite">Favorite</string>
    <string name="unfavorite">Unfavorite</string>
    <string name="remove">Remove</string>
```

- [ ] **Step 2: Rewrite `MainActivity.java`:**

```java
package io.sameboy.android;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.text.format.DateUtils;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.GridView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class MainActivity extends Activity {
    private static final int REQ_TREE = 1, REQ_FILE = 2;
    private final ExecutorService io = Executors.newSingleThreadExecutor();
    private final Handler ui = new Handler(Looper.getMainLooper());
    private Library library;
    private GridView grid;
    private LibraryAdapter adapter;
    private TextView empty;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        library = new Library(this);
        library.load();

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);

        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        Button importFolder = new Button(this);
        importFolder.setText(R.string.import_folder);
        importFolder.setOnClickListener(v -> pickTree());
        Button openFile = new Button(this);
        openFile.setText(R.string.open_rom);
        openFile.setOnClickListener(v -> pickFile());
        bar.addView(importFolder);
        bar.addView(openFile);
        root.addView(bar);

        empty = new TextView(this);
        empty.setText(R.string.library_empty);
        empty.setGravity(Gravity.CENTER);
        empty.setPadding(0, 64, 0, 0);
        root.addView(empty);

        grid = new GridView(this);
        grid.setNumColumns(GridView.AUTO_FIT);
        int cell = (int) (getResources().getDisplayMetrics().density * 150);
        grid.setColumnWidth(cell);
        grid.setStretchMode(GridView.STRETCH_COLUMN_WIDTH);
        adapter = new LibraryAdapter();
        grid.setAdapter(adapter);
        grid.setOnItemClickListener((p, view, pos, id) -> launch(adapter.items.get(pos)));
        grid.setOnItemLongClickListener((p, view, pos, id) -> { showContext(adapter.items.get(pos)); return true; });
        root.addView(grid, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        setContentView(root);
    }

    @Override protected void onResume() { super.onResume(); refresh(); }

    private void refresh() {
        adapter.items.clear();
        adapter.items.addAll(library.listSorted());
        adapter.notifyDataSetChanged();
        boolean e = adapter.items.isEmpty();
        empty.setVisibility(e ? View.VISIBLE : View.GONE);
        grid.setVisibility(e ? View.GONE : View.VISIBLE);
    }

    private void pickTree() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(i, REQ_TREE);
    }
    private void pickFile() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(i, REQ_FILE);
    }

    @Override protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (res != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        int grant = data.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION;
        try { getContentResolver().takePersistableUriPermission(uri, grant); } catch (Exception ignored) {}

        if (req == REQ_TREE) {
            Toast.makeText(this, R.string.scanning, Toast.LENGTH_SHORT).show();
            io.execute(() -> {
                int[] added = {0};
                RomScanner.scanTree(this, uri, entry ->
                    ui.post(() -> { if (library.add(entry)) added[0]++; }));
                ui.post(() -> {
                    library.save();
                    refresh();
                    Toast.makeText(this, getString(R.string.added_n, added[0]), Toast.LENGTH_SHORT).show();
                });
            });
        } else { // REQ_FILE
            String name = queryName(uri);
            io.execute(() -> {
                boolean[] got = {false};
                RomScanner.handleFile(getContentResolver(), uri, name, entry -> {
                    got[0] = true;
                    ui.post(() -> { library.add(entry); library.save(); refresh(); launch(entry); });
                });
                if (!got[0]) ui.post(() -> Toast.makeText(this, R.string.not_a_rom, Toast.LENGTH_SHORT).show());
            });
        }
    }

    private void launch(LibraryEntry entry) {
        library.touch(entry.crc32);
        library.save();
        Intent i = new Intent(this, EmulatorActivity.class);
        i.setData(Uri.parse(entry.uri));
        if (entry.zipEntry != null) i.putExtra(EmulatorActivity.EXTRA_ZIP_ENTRY, entry.zipEntry);
        i.putExtra(EmulatorActivity.EXTRA_ROM_KEY, entry.crc32);
        startActivity(i);
    }

    private void showContext(LibraryEntry entry) {
        String fav = entry.favorite ? getString(R.string.unfavorite) : getString(R.string.favorite);
        String[] actions = { getString(R.string.play), fav, getString(R.string.remove) };
        new AlertDialog.Builder(this)
            .setTitle(entry.label())
            .setItems(actions, (d, which) -> {
                switch (which) {
                    case 0: launch(entry); break;
                    case 1: library.setFavorite(entry.crc32, !entry.favorite); library.save(); refresh(); break;
                    case 2: library.remove(entry.crc32); library.save(); refresh(); break;
                }
            }).show();
    }

    private String queryName(Uri uri) {
        String name = null;
        try (Cursor c = getContentResolver().query(uri,
                new String[]{ OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                int i = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (i >= 0) name = c.getString(i);
            }
        } catch (Exception ignored) {}
        if (name == null) name = uri.getLastPathSegment();
        return name == null ? "rom" : name;
    }

    private final class LibraryAdapter extends BaseAdapter {
        final List<LibraryEntry> items = new ArrayList<>();
        @Override public int getCount() { return items.size(); }
        @Override public Object getItem(int p) { return items.get(p); }
        @Override public long getItemId(int p) { return p; }
        @Override public View getView(int pos, View convert, ViewGroup parent) {
            LinearLayout cell;
            if (convert instanceof LinearLayout) {
                cell = (LinearLayout) convert;
            } else {
                cell = new LinearLayout(MainActivity.this);
                cell.setOrientation(LinearLayout.VERTICAL);
                int pad = (int) (getResources().getDisplayMetrics().density * 8);
                cell.setPadding(pad, pad, pad, pad);
                TextView title = new TextView(MainActivity.this); title.setId(1);
                TextView sub = new TextView(MainActivity.this); sub.setId(2);
                cell.addView(title);
                cell.addView(sub);
            }
            LibraryEntry e = items.get(pos);
            ((TextView) cell.findViewById(1)).setText((e.favorite ? "\u2605 " : "") + e.label());
            ((TextView) cell.findViewById(2)).setText(e.lastPlayed == 0
                ? getString(R.string.never)
                : DateUtils.getRelativeTimeSpanString(e.lastPlayed).toString());
            return cell;
        }
    }
}
```

- [ ] **Step 3: Build** — `./gradlew :app:assembleDebug`. Expected FAIL until Task 7 adds `EmulatorActivity.EXTRA_ZIP_ENTRY`/`EXTRA_ROM_KEY`. **If this task is built before Task 7,** temporarily reference the string literals? No — do Task 7 first if building standalone. Under subagent-driven execution Tasks 6 and 7 are reviewed together at build time; the implementer of Task 6 MUST also add the two `public static final String` constants to `EmulatorActivity` (a 2-line stub) so Task 6 compiles, and Task 7 wires their behavior. Add now to `EmulatorActivity.java` (top of class):

```java
    public static final String EXTRA_ZIP_ENTRY = "io.sameboy.zipEntry";
    public static final String EXTRA_ROM_KEY = "io.sameboy.romKey";
```

Then `./gradlew :app:assembleDebug` → `BUILD SUCCESSFUL`.

- [ ] **Step 4: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/MainActivity.java Android/app/src/main/res/values/strings.xml Android/app/src/main/java/io/sameboy/android/EmulatorActivity.java
git commit -m "feat(android): library browser MainActivity (grid, folder/file import, favorites, recents)"
```

---

### Task 7: EmulatorActivity background ROM read + launch extras

**Files:**
- Modify: `Android/app/src/main/java/io/sameboy/android/EmulatorActivity.java`

**Interfaces:**
- Consumes: `EXTRA_ZIP_ENTRY` / `EXTRA_ROM_KEY` (constants added in Task 6), `ZipRoms.extract` (Task 4).
- Produces: background ROM load; CRC-keyed saves. No outward interface.

- [ ] **Step 1: Rewrite the load path.** Add imports: `android.os.Looper` is not needed; add `import android.widget.TextView;` and `import java.util.concurrent.ExecutorService; import java.util.concurrent.Executors;`. Add a field and replace `onCreate`'s body from the ROM read down, extracting setup into `finishSetup`. The `EXTRA_*` constants were added in Task 6.

Field:
```java
    private final ExecutorService io = Executors.newSingleThreadExecutor();
```

`onCreate`:
```java
    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        TextView loading = new TextView(this);
        loading.setText("Loading…");
        loading.setGravity(android.view.Gravity.CENTER);
        setContentView(loading);

        Uri data = getIntent().getData();
        String zipEntry = getIntent().getStringExtra(EXTRA_ZIP_ENTRY);
        String keyExtra = getIntent().getStringExtra(EXTRA_ROM_KEY);
        romName = (keyExtra != null && !keyExtra.isEmpty()) ? keyExtra : displayName(data);
        savFile = SaveStore.savFile(this, romName);

        io.execute(() -> {
            byte[] rom = readRom(data, zipEntry);
            byte[] sav = SaveStore.read(savFile);
            runOnUiThread(() -> finishSetup(rom, sav));
        });
    }

    private void finishSetup(byte[] rom, byte[] sav) {
        if (isFinishing() || isDestroyed()) return;
        if (rom == null || rom.length < 0x150) {
            Toast.makeText(this, "Could not read ROM", Toast.LENGTH_LONG).show();
            finish();
            return;
        }
        ctx = NativeBridge.nativeCreate(NativeBridge.MODEL_CGB_E, rom, sav, getAssets());
        if (ctx == 0) {
            Toast.makeText(this, "Could not load ROM", Toast.LENGTH_LONG).show();
            finish();
            return;
        }
        FrameLayout root = new FrameLayout(this);
        EmulatorSurfaceView surface = new EmulatorSurfaceView(this, this);
        TouchOverlayView overlay = new TouchOverlayView(this, new TouchOverlayView.ControlListener() {
            @Override public void onKey(int k, boolean pressed) {
                if (ctx != 0) NativeBridge.nativeSetKey(ctx, k, pressed);
            }
            @Override public void onSpecial(int what, boolean pressed) {
                if (ctx == 0) return;
                switch (what) {
                    case TouchOverlayView.SPECIAL_REWIND: NativeBridge.nativeSetRewinding(ctx, pressed); break;
                    case TouchOverlayView.SPECIAL_TURBO:  NativeBridge.nativeSetTurbo(ctx, pressed); break;
                    case TouchOverlayView.SPECIAL_MENU:   if (pressed && !menuOpen) openMenu(); break;
                }
            }
        });
        root.addView(surface);
        root.addView(overlay);
        setContentView(root);
    }

    private byte[] readRom(Uri uri, String zipEntry) {
        if (uri == null) return null;
        if (zipEntry != null) {
            try (InputStream in = getContentResolver().openInputStream(uri)) {
                return ZipRoms.extract(in, zipEntry);
            } catch (Exception e) { return null; }
        }
        return readAll(uri);
    }
```

Keep the existing `readAll`, `displayName`, `onSurfaceReady`, `onSurfaceGone`, `onPause`, `onResume`, `openMenu`, `saveStateToSlot`, `loadStateFromSlot`, `onDestroy`, and the battery poller unchanged. Remove the old inline ROM-read block from `onCreate` (now in `finishSetup`).

Note on lifecycle: `onResume` runs before the async load finishes; its `nativePause`/battery-poll are already `ctx != 0`-guarded, so a not-yet-created session is a no-op. `nativeStart` still happens only in `onSurfaceReady`, which fires after `finishSetup` calls `setContentView(root)`.

- [ ] **Step 2: Build** — `./gradlew :app:assembleDebug` → `BUILD SUCCESSFUL`.

- [ ] **Step 3: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/EmulatorActivity.java
git commit -m "fix(android): background ROM read (ANR fix) + zip-entry/CRC-key launch extras"
```

---

### Task 8: Integration check — full build + host tests

**Files:** none — verification only.

- [ ] **Step 1: Host suite** — `Android/jni/test/run_host_tests.sh` → `ALL HOST TESTS PASSED` (ring, emulator incl. `test_rom_info`, session).

- [ ] **Step 2: Clean build + ABI/symbol check** —

```bash
cd ~/SameBoy/Android && JAVA_HOME=$HOME/Android/jdk17 ANDROID_HOME=$HOME/Android ./gradlew clean :app:assembleDebug
APK=app/build/outputs/apk/debug/app-debug.apk
unzip -l $APK | grep -c libsameboy_core.so     # expect 4
D=app/build/intermediates/merged_native_libs/debug/mergeDebugNativeLibs/out/lib/arm64-v8a
nm -D --defined-only $D/libsameboy_core.so | grep -c NativeBridge_native   # expect 17
```
Expected: `BUILD SUCCESSFUL`, `4`, `17`.

- [ ] **Step 3: Report status.** On-device Waydroid acceptance (spec §8, items 1–6) is run by the controller after the final review, per M1/M2 pattern.
