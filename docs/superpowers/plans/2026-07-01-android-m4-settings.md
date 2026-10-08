# SameBoy Android M4 — Settings Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Persistent settings surface (emulation/video/audio/controls) wired to the running session, per `specs/2026-07-01-android-m4-settings.md`.

**Architecture:** A `Settings` SharedPreferences helper; a hand-rolled programmatic `SettingsActivity`. Core-backed settings are applied in one self-parking native call `sb_session_apply_settings`; master volume is a frontend `atomic_int` scale in `audio_cb`. Apply happens at launch and on `EmulatorActivity.onResume` (returning from Settings). Frontend-only bits (button opacity, haptics) live on `TouchOverlayView`.

**Tech Stack:** C11 atomics, JNI, Java framework widgets (`ScrollView`/`SeekBar`/`Switch`/`AlertDialog`) — **no new Gradle deps, no XML layouts**.

## Global Constraints

- **Never modify `Core/`** — settings via existing `GB_set_*` (color correction, light temp, border, high-pass, RTC, rewind length, turbo cap, interference).
- **No new Gradle dependency; no XML layouts** — programmatic UI, framework widgets only (M1–M3 line; supersedes the roadmap's AndroidX Preference suggestion).
- Build: `cd ~/SameBoy/Android && JAVA_HOME=$HOME/Android/jdk17 ANDROID_HOME=$HOME/Android ./gradlew :app:assembleDebug`.
- Host tests: `Android/jni/test/run_host_tests.sh` (must stay green).
- **One batched apply**, self-parking once — never one JNI call per slider tick.
- **Volume is frontend**: `atomic_int` (256 = 1.0), `audio_cb` does `s*vol/256`. Core has no master volume.
- **Live vs launch:** all settings apply live except **model**, which applies on next ROM launch (switching model reboots the GB).
- Value ranges copied from SDL: light temp `(slider−10)/10` (slider 0–20, default 10→0.0); interference `v/100`; volume `s*vol/100`; turbo cap `cap/4` (0 = uncapped).
- Defaults (SameBoy/SDL): model CGB-E, rewind 120 s, RTC sync-to-host, turbo cap 0, color correction Modern Balanced (`GB_COLOR_CORRECTION_MODERN_BALANCED`=2), light temp slider 10, border SGB (0), volume 100 %, high-pass Accurate (1), interference 0 %, button opacity 60 %, haptics on.
- Commit after each task; prefix `feat(android):` / `fix(android):` / `test(android):`.

## Enum value reference (Core headers — use verbatim)

- `GB_color_correction_mode_t`: 0 Disabled, 1 Correct Curves, 2 Modern Balanced, 3 Modern Boost Contrast, 4 Reduce Contrast, 5 Low Contrast, 6 Modern Accurate.
- `GB_border_mode_t`: 0 SGB, 1 Never, 2 Always.
- `GB_highpass_mode_t`: 0 Off, 1 Accurate, 2 Remove DC Offset.
- `GB_rtc_mode_t`: 0 Sync to Host, 1 Accurate.
- Models (M2 `NativeBridge`): DMG-B 0x002, CGB-E 0x205, AGB 0x207.

---

### Task 1: Native settings apply + volume scale

**Files:**
- Modify: `Android/jni/emulator.h`, `Android/jni/emulator.c`
- Test: `Android/jni/test/test_emulator.c`

**Interfaces:**
- Produces:

```c
typedef struct {
    int    color_correction;   /* GB_color_correction_mode_t */
    double light_temperature;  /* -1..1 */
    int    border_mode;        /* GB_border_mode_t */
    int    highpass;           /* GB_highpass_mode_t */
    int    rtc_mode;           /* GB_rtc_mode_t */
    double rewind_seconds;
    double turbo_cap;          /* 0 = uncapped */
    double interference;       /* 0..1 */
} sb_settings;
void sb_emu_apply_settings(sb_emulator *e, const sb_settings *s);
void sb_emu_set_volume_ptr(sb_emulator *e, const atomic_int *volume);  /* 256 = 1.0; NULL = full */
```

- [ ] **Step 1: Write the failing tests** in `test_emulator.c` (call both from `main`):

```c
static void test_apply_settings(void)
{
    size_t rlen; uint8_t *rom = make_rom(&rlen, 0);
    sb_emulator *e = sb_emu_create(0x002, rom, rlen, NULL, 0);
    assert(e);
    sb_emu_reset(e);
    sb_settings s = {
        .color_correction = 3, .light_temperature = 0.5, .border_mode = 2,
        .highpass = 2, .rtc_mode = 1, .rewind_seconds = 30, .turbo_cap = 2.0,
        .interference = 0.25,
    };
    sb_emu_apply_settings(e, &s);
    run_frames(e, 10);                 /* still runnable, no crash */
    unsigned w = 0, h = 0;
    assert(sb_emu_front_buffer(e, &w, &h) != NULL);
    sb_emu_destroy(e);
    free(rom);
}

static void test_volume_scale(void)
{
    size_t rlen; uint8_t *rom = make_rom(&rlen, 0);
    sb_emulator *e = sb_emu_create(0x002, rom, rlen, NULL, 0);
    assert(e);
    sb_emu_reset(e);
    static atomic_int vol;
    atomic_init(&vol, 0);              /* silence */
    sb_emu_set_volume_ptr(e, &vol);
    static int16_t buf[4096 * 2];
    int nonzero = 0;
    for (int i = 0; i < 30; i++) {
        sb_emu_run_frame(e);
        size_t got;
        while ((got = sb_ring_pop(sb_emu_audio_ring(e), buf, 4096)) > 0) {
            for (size_t j = 0; j < got * 2; j++) if (buf[j] != 0) nonzero++;
            if (got < 4096) break;
        }
    }
    assert(nonzero == 0);              /* volume 0 => all samples zero */
    sb_emu_destroy(e);
    free(rom);
}
```

- [ ] **Step 2: Run to verify failure** — `run_host_tests.sh` → compile error (`sb_settings` / `sb_emu_apply_settings` undeclared).

- [ ] **Step 3: Implement.** `emulator.h`: add the `sb_settings` struct + two decls after `sb_rom_info` (`stdatomic.h` already included). Include the display/apu/timing headers if not already reachable via `Core/gb.h` — `gb.h` pulls them in, so the enum types are visible.

`emulator.c`:
1. Struct: add `const atomic_int *volume;` to `struct sb_emulator`.
2. `audio_cb`: after computing `left/right` (before push), scale:

```c
static void audio_cb(GB_gameboy_t *gb, GB_sample_t *sample)
{
    sb_emulator *e = GB_get_user_data(gb);
    int16_t l = sample->left, r = sample->right;
    if (e->volume) {
        int v = atomic_load_explicit(e->volume, memory_order_relaxed);
        if (v != 256) { l = (int16_t)(l * v / 256); r = (int16_t)(r * v / 256); }
    }
    if (e->audio_drop && atomic_load_explicit(e->audio_drop, memory_order_relaxed)) {
        sb_ring_try_push(e->audio, l, r);
    }
    else {
        sb_ring_push(e->audio, l, r);
    }
}
```

3. New functions (before `sb_emu_destroy`):

```c
void sb_emu_set_volume_ptr(sb_emulator *e, const atomic_int *volume)
{
    e->volume = volume;
}

void sb_emu_apply_settings(sb_emulator *e, const sb_settings *s)
{
    GB_set_color_correction_mode(&e->gb, (GB_color_correction_mode_t)s->color_correction);
    GB_set_light_temperature(&e->gb, s->light_temperature);
    GB_set_border_mode(&e->gb, (GB_border_mode_t)s->border_mode);
    GB_set_highpass_filter_mode(&e->gb, (GB_highpass_mode_t)s->highpass);
    GB_set_rtc_mode(&e->gb, (GB_rtc_mode_t)s->rtc_mode);
    GB_set_rewind_length(&e->gb, s->rewind_seconds);
    GB_set_turbo_cap(&e->gb, s->turbo_cap);
    GB_set_interference_volume(&e->gb, s->interference);
}
```

(`GB_set_rtc_mode` is in `timing.h`, `GB_set_border_mode` in `gb.h`, the rest in `display.h`/`apu.h` — all included via `Core/gb.h`. If `timing.h` isn't transitively included, add `#include <Core/timing.h>`.)

- [ ] **Step 4: Run tests** — `run_host_tests.sh` → `ALL HOST TESTS PASSED`.

- [ ] **Step 5: Commit**

```bash
git add Android/jni/emulator.h Android/jni/emulator.c Android/jni/test/test_emulator.c
git commit -m "feat(android): sb_emu_apply_settings (Core-backed) + frontend volume scale in audio_cb"
```

---

### Task 2: Session apply-settings (self-park) + volume atomic

**Files:**
- Modify: `Android/jni/session.h`, `Android/jni/session.c`
- Test: `Android/jni/test/test_session.c`

**Interfaces:**
- Consumes: Task 1 (`sb_settings`, `sb_emu_apply_settings`, `sb_emu_set_volume_ptr`).
- Produces:

```c
void sb_session_apply_settings(sb_session *s, const sb_settings *cfg); /* self-parks */
void sb_session_set_volume(sb_session *s, int volume_256);            /* atomic, any thread */
```

- [ ] **Step 1: Extend the concurrency test.** In `test_session.c`, after the turbo/rewind exercise (before stop), add:

```c
    /* apply settings mid-run (self-park) + volume set from the control thread */
    sb_settings cfg = {
        .color_correction = 2, .light_temperature = 0.0, .border_mode = 0,
        .highpass = 1, .rtc_mode = 0, .rewind_seconds = 60, .turbo_cap = 0,
        .interference = 0.0,
    };
    sb_session_apply_settings(s, &cfg);
    sb_session_set_volume(s, 128);
    usleep(50 * 1000);
    sb_session_set_volume(s, 256);
```

- [ ] **Step 2: Run to verify failure** — `run_host_tests.sh` → compile error (`sb_session_apply_settings` undeclared).

- [ ] **Step 3: Implement.** `session.h`: add the two decls (near `sb_session_switch_model`).

`session.c`:
1. Struct: add `atomic_int volume;` to `struct sb_session`.
2. In `sb_session_create`, after `atomic_init(&s->battery_dirty, false);`:

```c
    atomic_init(&s->volume, 256);
    sb_emu_set_volume_ptr(emu, &s->volume);
```

3. New functions (near `sb_session_switch_model`):

```c
void sb_session_apply_settings(sb_session *s, const sb_settings *cfg)
{
    if (!s) return;
    int was = park_begin(s);
    sb_emu_apply_settings(s->emu, cfg);
    park_end(s, was);
}

void sb_session_set_volume(sb_session *s, int volume_256)
{
    if (!s) return;
    if (volume_256 < 0) volume_256 = 0;
    if (volume_256 > 256) volume_256 = 256;
    atomic_store(&s->volume, volume_256);
}
```

- [ ] **Step 4: Run tests** — `run_host_tests.sh` → all three suites pass, `session` re-run stable (no `alarm()` fire).

- [ ] **Step 5: Commit**

```bash
git add Android/jni/session.h Android/jni/session.c Android/jni/test/test_session.c
git commit -m "feat(android): sb_session_apply_settings (self-park) + atomic master volume"
```

---

### Task 3: JNI — nativeApplySettings + nativeSetVolume

**Files:**
- Modify: `Android/jni/sameboy_jni.c`, `Android/app/src/main/java/io/sameboy/android/NativeBridge.java`

**Interfaces:**
- Consumes: Task 2 session API.
- Produces (Java):

```java
public static native void nativeApplySettings(long ctx, int colorCorrection, double lightTemp,
        int border, int highpass, int rtcMode, double rewindSeconds, double turboCap, double interference);
public static native void nativeSetVolume(long ctx, int volume256);
```

- [ ] **Step 1: Add the Java declarations** to `NativeBridge.java` (after `nativeRomInfo`).

- [ ] **Step 2: Add the C implementations** to `sameboy_jni.c` (after `nativeRomInfo`):

```c
JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeApplySettings(JNIEnv *env, jclass c, jlong ctx,
        jint colorCorrection, jdouble lightTemp, jint border, jint highpass, jint rtcMode,
        jdouble rewindSeconds, jdouble turboCap, jdouble interference)
{
    (void)env; (void)c;
    /* clamp enum ints to their Core ranges (Core asserts are compiled out under NDEBUG) */
    if (colorCorrection < 0 || colorCorrection > 6) colorCorrection = 2;
    if (border < 0 || border > 2) border = 0;
    if (highpass < 0 || highpass > 2) highpass = 1;
    if (rtcMode < 0 || rtcMode > 1) rtcMode = 0;
    sb_settings s = {
        .color_correction = colorCorrection, .light_temperature = lightTemp,
        .border_mode = border, .highpass = highpass, .rtc_mode = rtcMode,
        .rewind_seconds = rewindSeconds, .turbo_cap = turboCap, .interference = interference,
    };
    sb_session_apply_settings((sb_session *)(uintptr_t)ctx, &s);
}

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeSetVolume(JNIEnv *env, jclass c, jlong ctx, jint volume256)
{ (void)env; (void)c; sb_session_set_volume((sb_session *)(uintptr_t)ctx, volume256); }
```

- [ ] **Step 3: Build + verify symbol count** —

```bash
cd ~/SameBoy/Android && JAVA_HOME=$HOME/Android/jdk17 ANDROID_HOME=$HOME/Android ./gradlew :app:assembleDebug
D=app/build/intermediates/merged_native_libs/debug/mergeDebugNativeLibs/out/lib/arm64-v8a
nm -D --defined-only $D/libsameboy_core.so | grep -c NativeBridge_native
```
Expected: `BUILD SUCCESSFUL`; count `19` (17 from M3 + 2 new).

- [ ] **Step 4: Commit**

```bash
git add Android/jni/sameboy_jni.c Android/app/src/main/java/io/sameboy/android/NativeBridge.java
git commit -m "feat(android): JNI nativeApplySettings + nativeSetVolume"
```

---

### Task 4: Settings SharedPreferences helper

**Files:**
- Create: `Android/app/src/main/java/io/sameboy/android/Settings.java`

**Interfaces:**
- Produces (Tasks 5–7 use these): `Settings(Context)`; typed getters/setters below; `int modelForLaunch()`; `void apply(long ctx)` (calls `nativeApplySettings` + `nativeSetVolume`); `float buttonOpacity()`; `boolean haptics()`. Values stored as ints/bools in `SharedPreferences("sameboy_settings")`.

- [ ] **Step 1: Create `Settings.java`:**

```java
package io.sameboy.android;

import android.content.Context;
import android.content.SharedPreferences;

/** Persistent user settings + one-shot apply to a running session.
 *  Sliders store ints; the Core mapping mirrors SameBoy's SDL frontend. */
final class Settings {
    private static final String PREFS = "sameboy_settings";

    // keys
    private static final String K_MODEL = "model";                 // GB_model_t int
    private static final String K_REWIND = "rewind_seconds";       // int seconds
    private static final String K_RTC = "rtc_mode";                // 0 sync, 1 accurate
    private static final String K_TURBO_CAP = "turbo_cap_quarters";// int, /4 => multiplier, 0=uncapped
    private static final String K_COLOR = "color_correction";      // 0..6
    private static final String K_LIGHT = "light_slider";          // 0..20, (v-10)/10
    private static final String K_BORDER = "border_mode";          // 0 SGB,1 Never,2 Always
    private static final String K_VOLUME = "volume_pct";           // 0..100
    private static final String K_HIGHPASS = "highpass";           // 0..2
    private static final String K_INTERFERENCE = "interference_pct"; // 0..100
    private static final String K_OPACITY = "button_opacity_pct";  // 0..100
    private static final String K_HAPTICS = "haptics";             // bool

    private final SharedPreferences p;

    Settings(Context ctx) { p = ctx.getApplicationContext().getSharedPreferences(PREFS, Context.MODE_PRIVATE); }

    // --- typed accessors (default = SameBoy/SDL defaults) ---
    int model()             { return p.getInt(K_MODEL, NativeBridge.MODEL_CGB_E); }
    void setModel(int v)    { p.edit().putInt(K_MODEL, v).apply(); }
    int rewindSeconds()     { return p.getInt(K_REWIND, 120); }
    void setRewindSeconds(int v){ p.edit().putInt(K_REWIND, v).apply(); }
    int rtcMode()           { return p.getInt(K_RTC, 0); }
    void setRtcMode(int v)  { p.edit().putInt(K_RTC, v).apply(); }
    int turboCapQuarters()  { return p.getInt(K_TURBO_CAP, 0); }
    void setTurboCapQuarters(int v){ p.edit().putInt(K_TURBO_CAP, v).apply(); }
    int colorCorrection()   { return p.getInt(K_COLOR, 2); }
    void setColorCorrection(int v){ p.edit().putInt(K_COLOR, v).apply(); }
    int lightSlider()       { return p.getInt(K_LIGHT, 10); }
    void setLightSlider(int v){ p.edit().putInt(K_LIGHT, v).apply(); }
    int borderMode()        { return p.getInt(K_BORDER, 0); }
    void setBorderMode(int v){ p.edit().putInt(K_BORDER, v).apply(); }
    int volumePct()         { return p.getInt(K_VOLUME, 100); }
    void setVolumePct(int v){ p.edit().putInt(K_VOLUME, v).apply(); }
    int highpass()          { return p.getInt(K_HIGHPASS, 1); }
    void setHighpass(int v) { p.edit().putInt(K_HIGHPASS, v).apply(); }
    int interferencePct()   { return p.getInt(K_INTERFERENCE, 0); }
    void setInterferencePct(int v){ p.edit().putInt(K_INTERFERENCE, v).apply(); }
    int buttonOpacityPct()  { return p.getInt(K_OPACITY, 60); }
    void setButtonOpacityPct(int v){ p.edit().putInt(K_OPACITY, v).apply(); }
    boolean haptics()       { return p.getBoolean(K_HAPTICS, true); }
    void setHaptics(boolean v){ p.edit().putBoolean(K_HAPTICS, v).apply(); }

    /** Model to boot the next launch. */
    int modelForLaunch() { return model(); }
    /** On-screen control alpha 0..1. */
    float buttonOpacity() { return buttonOpacityPct() / 100f; }

    /** Push every Core-backed setting + volume to a running session. */
    void apply(long ctx) {
        if (ctx == 0) return;
        NativeBridge.nativeApplySettings(ctx,
            colorCorrection(),
            (lightSlider() - 10) / 10.0,
            borderMode(),
            highpass(),
            rtcMode(),
            rewindSeconds(),
            turboCapQuarters() / 4.0,
            interferencePct() / 100.0);
        NativeBridge.nativeSetVolume(ctx, volumePct() * 256 / 100);
    }
}
```

- [ ] **Step 2: Build** — `./gradlew :app:assembleDebug` → `BUILD SUCCESSFUL`.

- [ ] **Step 3: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/Settings.java
git commit -m "feat(android): Settings SharedPreferences helper + one-shot session apply"
```

---

### Task 5: SettingsActivity (programmatic UI) + strings + manifest

**Files:**
- Create: `Android/app/src/main/java/io/sameboy/android/SettingsActivity.java`
- Modify: `Android/app/src/main/res/values/strings.xml`, `Android/app/src/main/AndroidManifest.xml`

**Interfaces:**
- Consumes: `Settings` (Task 4).
- Produces: a launchable `SettingsActivity` (writes prefs only; apply happens in `EmulatorActivity.onResume`).

- [ ] **Step 1: Add strings** to `res/values/strings.xml` (inside `<resources>`):

```xml
    <string name="settings">Settings</string>
```

- [ ] **Step 2: Create `SettingsActivity.java`:**

```java
package io.sameboy.android;

import android.app.Activity;
import android.app.AlertDialog;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;

/** Hand-rolled programmatic settings screen (no XML, no AndroidX Preference).
 *  Writes SharedPreferences via Settings; EmulatorActivity applies on resume. */
public class SettingsActivity extends Activity {
    private Settings s;
    private int dp(int v) { return (int) (getResources().getDisplayMetrics().density * v); }

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        s = new Settings(this);
        setTitle(R.string.settings);

        ScrollView scroll = new ScrollView(this);
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(dp(16), dp(16), dp(16), dp(16));
        scroll.addView(col);

        section(col, "Emulation");
        enumRow(col, "Model (next launch)",
            new String[]{ "Game Boy (DMG)", "Game Boy Color (CGB)", "Game Boy Advance (AGB)" },
            modelToIndex(s.model()), i -> s.setModel(indexToModel(i)));
        sliderRow(col, "Rewind length", 0, 600, s.rewindSeconds(), " s", v -> s.setRewindSeconds(v));
        enumRow(col, "RTC mode", new String[]{ "Sync to host", "Accurate" }, s.rtcMode(), i -> s.setRtcMode(i));
        sliderRow(col, "Turbo cap (0 = uncapped)", 0, 32, s.turboCapQuarters(), " /4x", v -> s.setTurboCapQuarters(v));

        section(col, "Video");
        enumRow(col, "Color correction", new String[]{
            "Disabled", "Correct Curves", "Modern Balanced", "Modern Boost Contrast",
            "Reduce Contrast", "Low Contrast", "Modern Accurate" },
            s.colorCorrection(), i -> s.setColorCorrection(i));
        sliderRow(col, "Light temperature", 0, 20, s.lightSlider(), "", v -> s.setLightSlider(v));
        enumRow(col, "Border", new String[]{ "SGB", "Never", "Always" }, s.borderMode(), i -> s.setBorderMode(i));

        section(col, "Audio");
        sliderRow(col, "Volume", 0, 100, s.volumePct(), " %", v -> s.setVolumePct(v));
        enumRow(col, "High-pass filter", new String[]{ "Off", "Accurate", "Remove DC offset" },
            s.highpass(), i -> s.setHighpass(i));
        sliderRow(col, "Interference", 0, 100, s.interferencePct(), " %", v -> s.setInterferencePct(v));

        section(col, "Controls");
        sliderRow(col, "Button opacity", 0, 100, s.buttonOpacityPct(), " %", v -> s.setButtonOpacityPct(v));
        toggleRow(col, "Haptics", s.haptics(), v -> s.setHaptics(v));

        setContentView(scroll);
    }

    private interface IntSink { void set(int v); }
    private interface BoolSink { void set(boolean v); }

    private void section(LinearLayout col, String title) {
        TextView t = new TextView(this);
        t.setText(title);
        t.setTextSize(18);
        t.setPadding(0, dp(16), 0, dp(8));
        col.addView(t);
    }

    private void enumRow(LinearLayout col, String label, String[] options, int current, IntSink sink) {
        TextView row = new TextView(this);
        row.setPadding(0, dp(10), 0, dp(10));
        final int[] cur = { current };
        Runnable render = () -> row.setText(label + ":  " + options[Math.max(0, Math.min(options.length - 1, cur[0]))]);
        render.run();
        row.setOnClickListener(v -> new AlertDialog.Builder(this)
            .setTitle(label)
            .setSingleChoiceItems(options, cur[0], (d, which) -> {
                cur[0] = which; sink.set(which); render.run(); d.dismiss();
            })
            .show());
        col.addView(row);
    }

    private void sliderRow(LinearLayout col, String label, int min, int max, int current, String unit, IntSink sink) {
        TextView t = new TextView(this);
        t.setPadding(0, dp(10), 0, 0);
        final int[] cur = { current };
        Runnable render = () -> t.setText(label + ":  " + cur[0] + unit);
        render.run();
        col.addView(t);
        SeekBar bar = new SeekBar(this);
        bar.setMax(max - min);
        bar.setProgress(current - min);
        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
                cur[0] = min + progress; render.run();
            }
            @Override public void onStartTrackingTouch(SeekBar sb) {}
            @Override public void onStopTrackingTouch(SeekBar sb) { sink.set(cur[0]); }
        });
        col.addView(bar);
    }

    private void toggleRow(LinearLayout col, String label, boolean current, BoolSink sink) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(10), 0, dp(10));
        TextView t = new TextView(this);
        t.setText(label);
        t.setLayoutParams(new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        Switch sw = new Switch(this);
        sw.setChecked(current);
        sw.setOnCheckedChangeListener((btn, checked) -> sink.set(checked));
        row.addView(t);
        row.addView(sw);
        col.addView(row);
    }

    private static int modelToIndex(int model) {
        if (model == NativeBridge.MODEL_DMG_B) return 0;
        if (model == NativeBridge.MODEL_AGB) return 2;
        return 1; // CGB-E
    }
    private static int indexToModel(int i) {
        if (i == 0) return NativeBridge.MODEL_DMG_B;
        if (i == 2) return NativeBridge.MODEL_AGB;
        return NativeBridge.MODEL_CGB_E;
    }
}
```

- [ ] **Step 3: Register the activity** in `AndroidManifest.xml` (inside `<application>`, after `EmulatorActivity`):

```xml
        <activity android:name=".SettingsActivity" android:exported="false"
                  android:configChanges="orientation|screenSize|keyboardHidden" />
```

- [ ] **Step 4: Build** — `./gradlew :app:assembleDebug` → `BUILD SUCCESSFUL`.

- [ ] **Step 5: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/SettingsActivity.java Android/app/src/main/res/values/strings.xml Android/app/src/main/AndroidManifest.xml
git commit -m "feat(android): programmatic SettingsActivity (emulation/video/audio/controls)"
```

---

### Task 6: TouchOverlayView — opacity + haptics

**Files:**
- Modify: `Android/app/src/main/java/io/sameboy/android/TouchOverlayView.java`

**Interfaces:**
- Produces: `void setOpacity(float alpha01)` and `void setHaptics(boolean on)`; a real key-down fires `performHapticFeedback` when enabled.

- [ ] **Step 1: Implement.**
  1. Add fields: `private float opacity = 0.6f; private boolean haptics = true;`
  2. Add setters (invalidate on opacity change):

```java
    void setOpacity(float a) { opacity = a; invalidate(); }
    void setHaptics(boolean on) { haptics = on; }
```

  3. Replace the whole `onDraw` body so every alpha scales by `opacity` (the current
     alphas are 90 / 120 / 110 / 110 / 200 — multiply each by `opacity`):

```java
    @Override protected void onDraw(Canvas c) {
        paint.setColor(Color.argb((int) (90 * opacity), 255, 255, 255));
        for (RectF r : new RectF[]{up, down, left, right}) if (r != null) c.drawRect(r, paint);
        paint.setColor(Color.argb((int) (120 * opacity), 200, 60, 90));
        if (a != null) c.drawOval(a, paint);
        if (b != null) c.drawOval(b, paint);
        paint.setColor(Color.argb((int) (110 * opacity), 180, 180, 180));
        if (start != null) c.drawRoundRect(start, 12, 12, paint);
        if (select != null) c.drawRoundRect(select, 12, 12, paint);
        if (rewind != null) c.drawRoundRect(rewind, 12, 12, paint);
        if (turbo != null) c.drawRoundRect(turbo, 12, 12, paint);
        if (menu != null) c.drawRoundRect(menu, 12, 12, paint);
        paint.setColor(Color.argb((int) (200 * opacity), 255, 255, 255));
        paint.setTextAlign(Paint.Align.CENTER);
        paint.setTextSize(rewind != null ? rewind.height() * 0.6f : 24);
        if (rewind != null) c.drawText("<<", rewind.centerX(), rewind.centerY() + rewind.height() * 0.2f, paint);
        if (turbo != null) c.drawText(">>", turbo.centerX(), turbo.centerY() + turbo.height() * 0.2f, paint);
        if (menu != null) c.drawText("=", menu.centerX(), menu.centerY() + menu.height() * 0.2f, paint);
    }
```
  4. In `press(pointerId, k)`, on a real key-down transition (the `keyCount[k]++ == 0` branch, and the one-shot menu branch), fire haptics:

```java
        if (haptics) performHapticFeedback(android.view.HapticFeedbackConstants.VIRTUAL_KEY);
```

  (Place it once where a press is first registered — for pooled keys inside the `keyCount[k]++ == 0` block, and in the `SPECIAL_MENU` one-shot block. `performHapticFeedback` needs no permission.)

- [ ] **Step 2: Build** — `./gradlew :app:assembleDebug` → `BUILD SUCCESSFUL`.

- [ ] **Step 3: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/TouchOverlayView.java
git commit -m "feat(android): on-screen control opacity + touch haptics"
```

---

### Task 7: Wire settings entry points + EmulatorActivity apply

**Files:**
- Modify: `Android/app/src/main/java/io/sameboy/android/MainActivity.java`, `GameMenuDialog.java`, `EmulatorActivity.java`

**Interfaces:**
- Consumes: `Settings` (Task 4), `SettingsActivity` (Task 5), overlay setters (Task 6).

- [ ] **Step 1: MainActivity top-bar Settings button.** In `onCreate`, after the `openFile` button is added to `bar`:

```java
        Button settings = new Button(this);
        settings.setText(R.string.settings);
        settings.setOnClickListener(v -> startActivity(new android.content.Intent(this, SettingsActivity.class)));
        bar.addView(settings);
```

- [ ] **Step 2: GameMenuDialog Settings item.** Add `void onOpenSettings();` to the `Host` interface. Change the items array and switch in `show`:

```java
        final String[] items = { "Resume", "Save state", "Load state", "Reset", "Model", "Settings", "Exit" };
        ...
                    case 4: chained[0] = true; showModels(a, h); break;
                    case 5: h.onOpenSettings(); return;   // leaves menu; EmulatorActivity re-applies on resume
                    case 6: h.onExitGame(); return;
```

  (Index 5 = Settings uses `return` — the host closes the menu and launches SettingsActivity; no unpause here, `onResume` handles it. Index 6 = Exit as before.)

- [ ] **Step 3: EmulatorActivity — model, apply, overlay, menu host.**
  1. Add fields: `private Settings settings; private TouchOverlayView overlay;`
  2. In `onCreate`, before the `io.execute`: `settings = new Settings(this);` and use the model pref: keep `romName` logic, but the model comes from settings at `nativeCreate` time (in `finishSetup`).
  3. In `finishSetup`, replace the fixed model + add apply + overlay wiring. Change `nativeCreate` model arg and after `ctx` is created:

```java
        ctx = NativeBridge.nativeCreate(settings.modelForLaunch(), rom, sav, getAssets());
        if (ctx == 0) { ... }   // unchanged
        settings.apply(ctx);    // batch Core settings + volume before threads start
        FrameLayout root = new FrameLayout(this);
        EmulatorSurfaceView surface = new EmulatorSurfaceView(this, this);
        overlay = new TouchOverlayView(this, new TouchOverlayView.ControlListener() { ... unchanged ... });
        overlay.setOpacity(settings.buttonOpacity());
        overlay.setHaptics(settings.haptics());
        root.addView(surface);
        root.addView(overlay);
        setContentView(root);
```

  (Assign to the `overlay` field, not a local.)
  4. `onResume` — re-apply settings and refresh the overlay when returning (e.g. from SettingsActivity):

```java
    @Override protected void onResume() {
        super.onResume();
        if (ctx != 0 && !menuOpen) {
            settings.apply(ctx);                       // self-parks once; picks up Settings changes
            if (overlay != null) { overlay.setOpacity(settings.buttonOpacity()); overlay.setHaptics(settings.haptics()); }
            NativeBridge.nativePause(ctx, false);
        }
        handler.postDelayed(batteryPoll, 2000);
    }
```

  5. Menu host — add `onOpenSettings`, and make it clear `menuOpen` so `onResume` re-applies + unpauses:

```java
            @Override public void onOpenSettings() {
                menuOpen = false;   // menu is closing; SettingsActivity takes over, onResume re-applies
                startActivity(new android.content.Intent(EmulatorActivity.this, SettingsActivity.class));
            }
```

- [ ] **Step 4: Build** — `./gradlew :app:assembleDebug` → `BUILD SUCCESSFUL`.

- [ ] **Step 5: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/MainActivity.java Android/app/src/main/java/io/sameboy/android/GameMenuDialog.java Android/app/src/main/java/io/sameboy/android/EmulatorActivity.java
git commit -m "feat(android): wire Settings entry points + launch/resume apply + model pref"
```

---

### Task 8: Integration check — full build + host tests

**Files:** none — verification only.

- [ ] **Step 1: Host suite** — `Android/jni/test/run_host_tests.sh` → `ALL HOST TESTS PASSED` (ring, emulator incl. apply-settings + volume, session incl. mid-run apply).

- [ ] **Step 2: Clean build + ABI/symbol check** —

```bash
cd ~/SameBoy/Android && JAVA_HOME=$HOME/Android/jdk17 ANDROID_HOME=$HOME/Android ./gradlew clean :app:assembleDebug
APK=app/build/outputs/apk/debug/app-debug.apk
unzip -l $APK | grep -c libsameboy_core.so     # expect 4
D=app/build/intermediates/merged_native_libs/debug/mergeDebugNativeLibs/out/lib/arm64-v8a
nm -D --defined-only $D/libsameboy_core.so | grep -c NativeBridge_native   # expect 19
```
Expected: `BUILD SUCCESSFUL`, `4`, `19`.

- [ ] **Step 3: Report status.** On-device Waydroid acceptance (spec §8, items 1–7) is run by the controller after the final review, per M1–M3 pattern. Settings persistence + live re-apply are `content://`-independent, so unlike M3's import they are fully exercisable on the AVD.
