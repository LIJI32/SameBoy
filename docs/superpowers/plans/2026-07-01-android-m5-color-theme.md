# SameBoy Android M5 — Color & Theme Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** DMG palette picker + custom 4-color editor (live recolor of DMG-mode games) and an app light/dark theme, per `specs/2026-07-01-android-m5-color-theme.md`.

**Architecture:** `GB_set_palette` (Core) recolors DMG rendering; built-ins are `extern const`, custom lives in an emulator-owned `GB_palette_t`. Palette rides the M4 `Settings.apply` batch (self-parking once, at launch + resume). App theme via AndroidX `DayNight` + `AppCompatDelegate.setDefaultNightMode` from an `Application`.

**Tech Stack:** C (Core reuse), JNI, Java (framework widgets + AppCompat DayNight — appcompat already a dep), programmatic UI, no XML layouts.

## Global Constraints

- **Never modify `Core/`** — palette via existing `GB_set_palette` / `GB_PALETTE_*` consts.
- No new Gradle dependency (appcompat 1.7.0 already present); no XML layouts.
- Build: `cd ~/SameBoy/Android && JAVA_HOME=$HOME/Android/jdk17 ANDROID_HOME=$HOME/Android ./gradlew :app:assembleDebug`.
- Host tests: `Android/jni/test/run_host_tests.sh` (must stay green).
- `GB_set_palette` stores the *pointer* (no copy): built-ins point at Core consts; custom points at an emulator-owned `GB_palette_t`. Palette only affects DMG (`!GB_is_cgb`) rendering.
- `GB_palette_t` = 5 colors; the custom editor sets 4 shades (0 darkest .. 3 lightest); `colors[4] = colors[3]`.
- Built-in index map: 0 `GB_PALETTE_GREY`, 1 `GB_PALETTE_DMG`, 2 `GB_PALETTE_MGB`, 3 `GB_PALETTE_GBL`. `-1` = custom.
- Default palette: **DMG (index 1)**. Default theme: **System (0)**.
- Palette apply self-parks (M2/M4 pattern), called from the M4 apply path.
- Commit after each task; prefix `feat(android):` / `fix(android):` / `test(android):`.

## Enum reference (Core `display.h`)

`extern const GB_palette_t GB_PALETTE_GREY, GB_PALETTE_DMG, GB_PALETTE_MGB, GB_PALETTE_GBL;`
`GB_palette_t { struct { uint8_t r,g,b; } colors[5]; }` — index 0 darkest … 3 lightest, 4 = border/blank.
`void GB_set_palette(GB_gameboy_t *gb, const GB_palette_t *palette);`

---

### Task 1: Native palette apply + host test

**Files:**
- Modify: `Android/jni/emulator.h`, `Android/jni/emulator.c`
- Test: `Android/jni/test/test_emulator.c`

**Interfaces:**
- Produces: `void sb_emu_set_palette(sb_emulator *e, int builtin_index, const uint32_t rgb[4]);`
  — `builtin_index` 0–3 selects a Core const; `-1` builds a custom palette from `rgb[4]`
  (`0x00RRGGBB`, index 0 darkest … 3 lightest). Task 2 wraps it.

- [ ] **Step 1: Write the failing test** in `test_emulator.c` (call from `main`):

```c
static void test_palette(void)
{
    size_t rlen; uint8_t *rom = make_rom(&rlen, 0);
    sb_emulator *e = sb_emu_create(0x002, rom, rlen, NULL, 0);   /* DMG-B: palette applies */
    assert(e);
    sb_emu_reset(e);
    run_frames(e, 2);
    unsigned w = 0, h = 0;
    const uint32_t *fb = sb_emu_front_buffer(e, &w, &h);
    uint32_t grey_px = fb[0];                       /* default GREY: blank screen = colors[3] = white */

    /* built-in DMG (green) recolors */
    sb_emu_set_palette(e, 1, NULL);
    run_frames(e, 2);
    fb = sb_emu_front_buffer(e, &w, &h);
    assert(fb[0] != grey_px);                        /* recolored */
    /* fb[0] must be one of the DMG palette's encoded shades (which shade the blank
       screen maps to depends on BGP; assert membership, not a fixed index). */
    {
        static const uint8_t dmg[4][3] = {
            {0x08,0x18,0x10}, {0x39,0x61,0x39}, {0x84,0xA5,0x63}, {0xC6,0xDE,0x8C} };
        int match = 0;
        for (int i = 0; i < 4; i++)
            if (fb[0] == (0xFF000000u | ((uint32_t)dmg[i][2] << 16) | ((uint32_t)dmg[i][1] << 8) | dmg[i][0])) match = 1;
        assert(match);
    }

    /* custom all-red shades */
    uint32_t red[4] = { 0xFF0000, 0xFF0000, 0xFF0000, 0xFF0000 };
    sb_emu_set_palette(e, -1, red);
    run_frames(e, 2);
    fb = sb_emu_front_buffer(e, &w, &h);
    assert(fb[0] == (0xFF000000u | (0x00u << 16) | (0x00u << 8) | 0xFFu));  /* pure red */

    sb_emu_destroy(e);
    free(rom);
}
```

- [ ] **Step 2: Run to verify failure** — `run_host_tests.sh` → compile error (`sb_emu_set_palette` undeclared).

- [ ] **Step 3: Implement.** `emulator.h`: add after `sb_emu_apply_settings`:

```c
/* builtin_index 0=Grey 1=DMG 2=MGB 3=GBL; -1 => custom from rgb[4] (0x00RRGGBB,
   index 0 darkest .. 3 lightest). */
void sb_emu_set_palette(sb_emulator *e, int builtin_index, const uint32_t rgb[4]);
```

`emulator.c`: add `GB_palette_t custom_palette;` to `struct sb_emulator`, then:

```c
void sb_emu_set_palette(sb_emulator *e, int builtin_index, const uint32_t rgb[4])
{
    static const GB_palette_t *const builtins[] = {
        &GB_PALETTE_GREY, &GB_PALETTE_DMG, &GB_PALETTE_MGB, &GB_PALETTE_GBL,
    };
    if (builtin_index >= 0 && builtin_index < 4) {
        GB_set_palette(&e->gb, builtins[builtin_index]);
        return;
    }
    if (!rgb) return;
    for (int i = 0; i < 4; i++) {
        e->custom_palette.colors[i].r = (rgb[i] >> 16) & 0xFF;
        e->custom_palette.colors[i].g = (rgb[i] >> 8) & 0xFF;
        e->custom_palette.colors[i].b = rgb[i] & 0xFF;
    }
    e->custom_palette.colors[4] = e->custom_palette.colors[3];  /* border = lightest */
    GB_set_palette(&e->gb, &e->custom_palette);
}
```

- [ ] **Step 4: Run tests** — `run_host_tests.sh` → `ALL HOST TESTS PASSED`.

- [ ] **Step 5: Commit**

```bash
git add Android/jni/emulator.h Android/jni/emulator.c Android/jni/test/test_emulator.c
git commit -m "feat(android): sb_emu_set_palette (built-in + custom DMG palette)"
```

---

### Task 2: Session palette (self-park) + session test

**Files:**
- Modify: `Android/jni/session.h`, `Android/jni/session.c`
- Test: `Android/jni/test/test_session.c`

**Interfaces:**
- Consumes: `sb_emu_set_palette` (Task 1).
- Produces: `void sb_session_set_palette(sb_session *s, int builtin_index, const uint32_t rgb[4]);` — self-parks.

- [ ] **Step 1: Extend the concurrency test.** In `test_session.c`, after the apply-settings block (before stop), add:

```c
    /* set palette mid-run (self-park): a built-in then a custom */
    sb_session_set_palette(s, 2, NULL);          /* MGB */
    usleep(30 * 1000);
    uint32_t pal[4] = { 0x0000FF, 0x0000AA, 0x000055, 0x000000 };
    sb_session_set_palette(s, -1, pal);          /* custom blue */
    usleep(30 * 1000);
```

- [ ] **Step 2: Run to verify failure** — compile error (`sb_session_set_palette` undeclared).

- [ ] **Step 3: Implement.** `session.h`: add decl near `sb_session_apply_settings`.

`session.c`:

```c
void sb_session_set_palette(sb_session *s, int builtin_index, const uint32_t rgb[4])
{
    if (!s) return;
    int was = park_begin(s);
    sb_emu_set_palette(s->emu, builtin_index, rgb);
    park_end(s, was);
}
```

- [ ] **Step 4: Run tests** — all three suites pass, session re-run stable (no `alarm()` fire).

- [ ] **Step 5: Commit**

```bash
git add Android/jni/session.h Android/jni/session.c Android/jni/test/test_session.c
git commit -m "feat(android): sb_session_set_palette (self-park)"
```

---

### Task 3: JNI nativeSetPalette

**Files:**
- Modify: `Android/jni/sameboy_jni.c`, `Android/app/src/main/java/io/sameboy/android/NativeBridge.java`

**Interfaces:**
- Consumes: Task 2.
- Produces (Java): `public static native void nativeSetPalette(long ctx, int builtinIndex, int c0, int c1, int c2, int c3);` — `c0..c3` = `0x00RRGGBB`, ignored when `builtinIndex >= 0`.

- [ ] **Step 1: Add the Java declaration** to `NativeBridge.java` (after `nativeSetVolume`).

- [ ] **Step 2: Add the C implementation** to `sameboy_jni.c` (after `nativeSetVolume`):

```c
JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeSetPalette(JNIEnv *env, jclass c, jlong ctx,
        jint builtinIndex, jint c0, jint c1, jint c2, jint c3)
{
    (void)env; (void)c;
    uint32_t rgb[4] = { (uint32_t)c0, (uint32_t)c1, (uint32_t)c2, (uint32_t)c3 };
    sb_session_set_palette((sb_session *)(uintptr_t)ctx, builtinIndex,
                           builtinIndex >= 0 ? NULL : rgb);
}
```

- [ ] **Step 3: Build + verify symbol count** —

```bash
cd ~/SameBoy/Android && JAVA_HOME=$HOME/Android/jdk17 ANDROID_HOME=$HOME/Android ./gradlew :app:assembleDebug
D=app/build/intermediates/merged_native_libs/debug/mergeDebugNativeLibs/out/lib/arm64-v8a
nm -D --defined-only $D/libsameboy_core.so | grep -c NativeBridge_native
```
Expected: `BUILD SUCCESSFUL`; count `20` (19 from M4 + `nativeSetPalette`).

- [ ] **Step 4: Commit**

```bash
git add Android/jni/sameboy_jni.c Android/app/src/main/java/io/sameboy/android/NativeBridge.java
git commit -m "feat(android): JNI nativeSetPalette"
```

---

### Task 4: Settings palette + theme; SameBoyApp; manifest DayNight

**Files:**
- Modify: `Android/app/src/main/java/io/sameboy/android/Settings.java`
- Create: `Android/app/src/main/java/io/sameboy/android/SameBoyApp.java`
- Modify: `Android/app/src/main/AndroidManifest.xml`

**Interfaces:**
- Consumes: `nativeSetPalette` (Task 3).
- Produces: `Settings.paletteBuiltin()`, `setPaletteBuiltin(int)`, `customColor(int)`, `setCustomColor(int,int)`, `themeMode()`, `setThemeMode(int)`, `applyTheme()`; `apply(ctx)` also pushes the palette.

- [ ] **Step 1: Extend `Settings.java`.** Add imports `androidx.appcompat.app.AppCompatDelegate;`. Add keys + accessors (place with the other keys/accessors):

```java
    private static final String K_PALETTE = "palette_builtin";     // 0..3, or -1 custom
    private static final String K_CUSTOM0 = "palette_custom0";      // 0xRRGGBB
    private static final String K_CUSTOM1 = "palette_custom1";
    private static final String K_CUSTOM2 = "palette_custom2";
    private static final String K_CUSTOM3 = "palette_custom3";
    private static final String K_THEME = "theme_mode";             // 0 System,1 Light,2 Dark

    // default custom = greyscale shades (darkest..lightest)
    private static final int[] CUSTOM_DEFAULT = { 0x000000, 0x555555, 0xAAAAAA, 0xFFFFFF };

    int paletteBuiltin()        { return p.getInt(K_PALETTE, 1); }   // default DMG
    void setPaletteBuiltin(int v){ p.edit().putInt(K_PALETTE, v).apply(); }
    int customColor(int i) {
        String[] keys = { K_CUSTOM0, K_CUSTOM1, K_CUSTOM2, K_CUSTOM3 };
        return p.getInt(keys[i], CUSTOM_DEFAULT[i]);
    }
    void setCustomColor(int i, int rgb) {
        String[] keys = { K_CUSTOM0, K_CUSTOM1, K_CUSTOM2, K_CUSTOM3 };
        p.edit().putInt(keys[i], rgb & 0xFFFFFF).apply();
    }
    int themeMode()             { return p.getInt(K_THEME, 0); }
    void setThemeMode(int v)    { p.edit().putInt(K_THEME, v).apply(); }
```

Extend `apply(long ctx)` — after the existing `nativeSetVolume` line:

```java
        int builtin = paletteBuiltin();
        NativeBridge.nativeSetPalette(ctx, builtin,
            customColor(0), customColor(1), customColor(2), customColor(3));
```

Add `applyTheme()`:

```java
    /** Apply the app light/dark theme globally (call at process start + on change). */
    void applyTheme() {
        int mode;
        switch (themeMode()) {
            case 1:  mode = AppCompatDelegate.MODE_NIGHT_NO; break;
            case 2:  mode = AppCompatDelegate.MODE_NIGHT_YES; break;
            default: mode = AppCompatDelegate.MODE_NIGHT_FOLLOW_SYSTEM; break;
        }
        AppCompatDelegate.setDefaultNightMode(mode);
    }
```

- [ ] **Step 2: Create `SameBoyApp.java`:**

```java
package io.sameboy.android;

import android.app.Application;

/** Applies the persisted light/dark theme before any activity is created. */
public class SameBoyApp extends Application {
    @Override public void onCreate() {
        super.onCreate();
        new Settings(this).applyTheme();
    }
}
```

- [ ] **Step 3: Manifest** — set the DayNight theme and register the Application:

Change the `<application>` open tag's theme and add `android:name`:

```xml
    <application
        android:name=".SameBoyApp"
        android:label="@string/app_name"
        android:allowBackup="true"
        android:theme="@style/Theme.AppCompat.DayNight.NoActionBar"
        android:supportsRtl="true">
```

- [ ] **Step 4: Build** — `./gradlew :app:assembleDebug` → `BUILD SUCCESSFUL`.

- [ ] **Step 5: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/Settings.java Android/app/src/main/java/io/sameboy/android/SameBoyApp.java Android/app/src/main/AndroidManifest.xml
git commit -m "feat(android): palette + theme prefs, SameBoyApp night-mode init, DayNight theme"
```

---

### Task 5: PaletteEditorDialog

**Files:**
- Create: `Android/app/src/main/java/io/sameboy/android/PaletteEditorDialog.java`

**Interfaces:**
- Consumes: `Settings` (Task 4).
- Produces: `PaletteEditorDialog.show(Activity, Settings, Runnable onApplied)` — edits the 4 custom colors, sets `paletteBuiltin = -1`, calls `onApplied` on confirm.

- [ ] **Step 1: Create `PaletteEditorDialog.java`:**

```java
package io.sameboy.android;

import android.app.Activity;
import android.app.AlertDialog;
import android.graphics.Color;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

/** Custom 4-shade DMG palette editor. Shade 0 = darkest .. 3 = lightest. */
final class PaletteEditorDialog {
    private PaletteEditorDialog() {}

    static void show(Activity a, Settings s, Runnable onApplied) {
        int dp = (int) (a.getResources().getDisplayMetrics().density * 12);
        LinearLayout col = new LinearLayout(a);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(dp, dp, dp, dp);

        final int[] colors = new int[4];
        final View[] swatches = new View[4];
        final String[] labels = { "Shade 0 (darkest)", "Shade 1", "Shade 2", "Shade 3 (lightest)" };
        for (int i = 0; i < 4; i++) {
            colors[i] = s.customColor(i);
            final int idx = i;
            TextView label = new TextView(a);
            label.setText(labels[i]);
            label.setPadding(0, dp, 0, 0);
            col.addView(label);

            LinearLayout row = new LinearLayout(a);
            row.setOrientation(LinearLayout.HORIZONTAL);
            row.setGravity(Gravity.CENTER_VERTICAL);
            View sw = new View(a);
            int sz = (int) (a.getResources().getDisplayMetrics().density * 40);
            sw.setLayoutParams(new LinearLayout.LayoutParams(sz, sz));
            sw.setBackgroundColor(0xFF000000 | colors[i]);
            swatches[i] = sw;
            row.addView(sw);

            LinearLayout sliders = new LinearLayout(a);
            sliders.setOrientation(LinearLayout.VERTICAL);
            sliders.setPadding(dp, 0, 0, 0);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
            sliders.setLayoutParams(lp);
            addChannel(a, sliders, colors, idx, 16, swatches[i]);  // R shift 16
            addChannel(a, sliders, colors, idx, 8, swatches[i]);   // G shift 8
            addChannel(a, sliders, colors, idx, 0, swatches[i]);   // B shift 0
            row.addView(sliders);
            col.addView(row);
        }

        new AlertDialog.Builder(a)
            .setTitle("Custom palette")
            .setView(wrapScroll(a, col))
            .setPositiveButton("Apply", (d, w) -> {
                for (int i = 0; i < 4; i++) s.setCustomColor(i, colors[i]);
                s.setPaletteBuiltin(-1);
                if (onApplied != null) onApplied.run();
            })
            .setNegativeButton("Cancel", null)
            .show();
    }

    private static void addChannel(Activity a, LinearLayout parent, int[] colors, int idx, int shift, View swatch) {
        SeekBar bar = new SeekBar(a);
        bar.setMax(255);
        bar.setProgress((colors[idx] >> shift) & 0xFF);
        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
                colors[idx] = (colors[idx] & ~(0xFF << shift)) | (progress << shift);
                swatch.setBackgroundColor(0xFF000000 | colors[idx]);
            }
            @Override public void onStartTrackingTouch(SeekBar sb) {}
            @Override public void onStopTrackingTouch(SeekBar sb) {}
        });
        parent.addView(bar);
    }

    private static View wrapScroll(Activity a, View content) {
        android.widget.ScrollView sv = new android.widget.ScrollView(a);
        sv.addView(content);
        return sv;
    }
}
```

- [ ] **Step 2: Build** — `./gradlew :app:assembleDebug` → `BUILD SUCCESSFUL`.

- [ ] **Step 3: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/PaletteEditorDialog.java
git commit -m "feat(android): custom 4-shade DMG palette editor dialog"
```

---

### Task 6: SettingsActivity palette + theme rows; AppCompat conversion

**Files:**
- Modify: `Android/app/src/main/java/io/sameboy/android/SettingsActivity.java`, `MainActivity.java`, `res/values/strings.xml`

**Interfaces:**
- Consumes: `Settings` (Task 4), `PaletteEditorDialog` (Task 5).

- [ ] **Step 1: Add strings** to `res/values/strings.xml`:

```xml
    <string name="palette">DMG palette</string>
    <string name="theme">Theme</string>
```

- [ ] **Step 2: Convert `MainActivity` to AppCompat.** Change the import `import android.app.Activity;` → `import androidx.appcompat.app.AppCompatActivity;` and the class `extends Activity` → `extends AppCompatActivity`. No other change.

- [ ] **Step 3: `SettingsActivity`** — convert to AppCompat and add the palette + theme rows.
  1. Import `import androidx.appcompat.app.AppCompatActivity;`; class `extends Activity` → `extends AppCompatActivity`.
  2. In the **Video** section (after the color-correction / light / border rows), add a palette row. Palette is an enum with a trailing "Custom…" entry that opens the editor:

```java
        paletteRow(col);
```

  and add the method:

```java
    private void paletteRow(LinearLayout col) {
        final String[] names = { "Greyscale", "DMG", "MGB", "GBL", "Custom…" };
        TextView row = new TextView(this);
        row.setPadding(0, dp(10), 0, dp(10));
        Runnable render = () -> {
            int b = s.paletteBuiltin();
            row.setText(getString(R.string.palette) + ":  " + (b < 0 ? "Custom" : names[b]));
        };
        render.run();
        row.setOnClickListener(v -> {
            int current = s.paletteBuiltin() < 0 ? 4 : s.paletteBuiltin();
            new AlertDialog.Builder(this)
                .setTitle(R.string.palette)
                .setSingleChoiceItems(names, current, (d, which) -> {
                    d.dismiss();
                    if (which == 4) {
                        PaletteEditorDialog.show(this, s, render);
                    } else {
                        s.setPaletteBuiltin(which);
                        render.run();
                    }
                })
                .show();
        });
        col.addView(row);
    }
```

  3. Add an **Appearance** section at the end (after Controls):

```java
        section(col, "Appearance");
        enumRow(col, getString(R.string.theme), new String[]{ "System", "Light", "Dark" },
            s.themeMode(), i -> { s.setThemeMode(i); s.applyTheme(); recreate(); });
```

- [ ] **Step 4: Build** — `./gradlew :app:assembleDebug` → `BUILD SUCCESSFUL`.

- [ ] **Step 5: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/SettingsActivity.java Android/app/src/main/java/io/sameboy/android/MainActivity.java Android/app/src/main/res/values/strings.xml
git commit -m "feat(android): SettingsActivity palette picker + theme row; AppCompat conversion"
```

---

### Task 7: Integration check — full build + host tests

**Files:** none — verification only.

- [ ] **Step 1: Host suite** — `Android/jni/test/run_host_tests.sh` → `ALL HOST TESTS PASSED` (ring, emulator incl. palette, session incl. set-palette mid-run).

- [ ] **Step 2: Clean build + ABI/symbol check** —

```bash
cd ~/SameBoy/Android && JAVA_HOME=$HOME/Android/jdk17 ANDROID_HOME=$HOME/Android ./gradlew clean :app:assembleDebug
APK=app/build/outputs/apk/debug/app-debug.apk
unzip -l $APK | grep -c libsameboy_core.so     # expect 4
D=app/build/intermediates/merged_native_libs/debug/mergeDebugNativeLibs/out/lib/arm64-v8a
nm -D --defined-only $D/libsameboy_core.so | grep -c NativeBridge_native   # expect 20
```
Expected: `BUILD SUCCESSFUL`, `4`, `20`.

- [ ] **Step 3: Report status.** On-device Waydroid acceptance (spec §7) is run by the controller after the final review, per M1–M4 pattern. Palette + theme are `content://`-independent, fully exercisable (set model→DMG, launch libbet via file://, verify recolor + theme switch + persistence).
