# SameBoy Android M6 — Physical Input Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Hardware gamepad support + button remapping + rumble, per `specs/2026-07-01-android-m6-physical-input.md`.

**Architecture:** Gamepad input is pure Java (`GamepadMapper` → existing `nativeSetKey`). Rumble: Core `GB_set_rumble_callback` stores an amplitude in an `atomic_int` (M2 battery-dirty pattern); a Java poller drives the system `Vibrator`. Rumble mode joins the M4/M5 batch-apply.

**Tech Stack:** C11 atomics, JNI, Java framework (`KeyEvent`/`MotionEvent`/`InputDevice`/`Vibrator`), programmatic UI, no XML layouts, no new deps.

## Global Constraints

- **Never modify `Core/`** — rumble via existing `GB_set_rumble_callback` / `GB_set_rumble_mode` (both reachable through `Core/gb.h`, which includes `rumble.h`).
- No new Gradle dependency; no XML layouts. Java 8-compatible.
- Build: `cd ~/SameBoy/Android && JAVA_HOME=$HOME/Android/jdk17 ANDROID_HOME=$HOME/Android ./gradlew :app:assembleDebug`.
- Host tests: `Android/jni/test/run_host_tests.sh` (must stay green).
- Input stays Java-only over the existing `nativeSetKey(ctx, gbKey, pressed)` — no new per-input JNI.
- Rumble amplitude = one `atomic_int` (0..255) written by the rumble callback (emu thread), read by a Java poller (~50 ms). No per-callback JNI up-call.
- `rumble_mode` added to `sb_settings`; `nativeApplySettings` grows one trailing `int rumbleMode` (clamped 0–2). Default **1 = Cartridge only** (`GB_RUMBLE_CARTRIDGE_ONLY`).
- Map by keycode; unmapped keys (BACK, letters, volume) fall through to `super`. Gamepad presence for auto-hide via `InputDevice.getSources()`.
- GB key indices (M1 `NativeBridge`): RIGHT 0, LEFT 1, UP 2, DOWN 3, A 4, B 5, SELECT 6, START 7.
- `android.permission.VIBRATE` required for the explicit `Vibrator`.
- Commit after each task; prefix `feat(android):` / `fix(android):` / `test(android):`.

## Default gamepad keycode → GB key

DPAD_RIGHT(22)→0, DPAD_LEFT(21)→1, DPAD_UP(19)→2, DPAD_DOWN(20)→3, BUTTON_A(96)→4, BUTTON_B(97)→5, BUTTON_SELECT(109)→6, BUTTON_START(108)→7. Axes: `AXIS_HAT_X`/`AXIS_HAT_Y` + `AXIS_X`/`AXIS_Y` → d-pad (deadzone 0.5).

---

### Task 1: Native rumble (callback + amplitude + rumble_mode) + host test

**Files:**
- Modify: `Android/jni/emulator.h`, `Android/jni/emulator.c`
- Test: `Android/jni/test/test_emulator.c`

**Interfaces:**
- Produces: `int rumble_mode;` added to `sb_settings`; `int sb_emu_rumble_amplitude(sb_emulator *e);` (0..255).

- [ ] **Step 1: Write the failing test** in `test_emulator.c` (call from `main`):

```c
static void test_rumble(void)
{
    size_t rlen; uint8_t *rom = make_rom(&rlen, 0);
    sb_emulator *e = sb_emu_create(0x002, rom, rlen, NULL, 0);
    assert(e);
    sb_emu_reset(e);
    assert(sb_emu_rumble_amplitude(e) == 0);          /* nothing yet */
    sb_settings s = {
        .color_correction = 2, .light_temperature = 0.0, .border_mode = 0,
        .highpass = 1, .rtc_mode = 0, .rewind_seconds = 120, .turbo_cap = 0,
        .interference = 0.0, .rumble_mode = 2,          /* GB_RUMBLE_ALL_GAMES */
    };
    sb_emu_apply_settings(e, &s);
    run_frames(e, 10);                                 /* non-rumble ROM: no crash */
    int amp = sb_emu_rumble_amplitude(e);
    assert(amp >= 0 && amp <= 255);
    sb_emu_destroy(e);
    free(rom);
}
```

- [ ] **Step 2: Run to verify failure** — `run_host_tests.sh` → compile error (`rumble_mode` / `sb_emu_rumble_amplitude` undeclared).

- [ ] **Step 3: Implement.** `emulator.h`: add `int rumble_mode;` as the last field of `sb_settings` (after `interference`), and after `sb_emu_set_volume_ptr`:

```c
int sb_emu_rumble_amplitude(sb_emulator *e);   /* 0..255, latest from the rumble callback */
```

`emulator.c`:
1. Add `atomic_int rumble_amp;` to `struct sb_emulator`.
2. Add the callback (near the other `*_cb` statics):

```c
static void rumble_cb(GB_gameboy_t *gb, double amplitude)
{
    sb_emulator *e = GB_get_user_data(gb);
    if (amplitude < 0) amplitude = 0;
    if (amplitude > 1) amplitude = 1;
    atomic_store(&e->rumble_amp, (int)(amplitude * 255 + 0.5));
}
```

3. In `sb_emu_create`, alongside the other `GB_set_*_callback` calls (before `GB_load_rom_from_buffer`): `GB_set_rumble_callback(&e->gb, rumble_cb);`
4. In `sb_emu_apply_settings`, add at the end:

```c
    GB_set_rumble_mode(&e->gb, (GB_rumble_mode_t)s->rumble_mode);
```

5. Add the getter (near `sb_emu_set_palette`):

```c
int sb_emu_rumble_amplitude(sb_emulator *e)
{
    return atomic_load(&e->rumble_amp);
}
```

- [ ] **Step 4: Run tests** — `run_host_tests.sh` → `ALL HOST TESTS PASSED`.

- [ ] **Step 5: Commit**

```bash
git add Android/jni/emulator.h Android/jni/emulator.c Android/jni/test/test_emulator.c
git commit -m "feat(android): Core rumble callback -> amplitude atomic + rumble_mode setting"
```

---

### Task 2: Session rumble getter + JNI (rumble amplitude + rumble mode in applySettings)

**Files:**
- Modify: `Android/jni/session.h`, `Android/jni/session.c`, `Android/jni/sameboy_jni.c`, `Android/app/src/main/java/io/sameboy/android/NativeBridge.java`
- Test: `Android/jni/test/test_session.c`

**Interfaces:**
- Consumes: Task 1.
- Produces: `int sb_session_rumble_amplitude(sb_session *s);`; JNI `nativeRumbleAmplitude(ctx) -> int`; `nativeApplySettings(..., int rumbleMode)` (trailing arg).

- [ ] **Step 1: Extend the concurrency test.** In `test_session.c`, after the palette block (before stop):

```c
    /* rumble amplitude readable mid-run (0..255) */
    int ramp = sb_session_rumble_amplitude(s);
    assert(ramp >= 0 && ramp <= 255);
```

- [ ] **Step 2: Run to verify failure** — compile error (`sb_session_rumble_amplitude` undeclared).

- [ ] **Step 3: Implement.**

`session.h`: add `int sb_session_rumble_amplitude(sb_session *s);` (near the other getters).

`session.c`:

```c
int sb_session_rumble_amplitude(sb_session *s)
{
    return s ? sb_emu_rumble_amplitude(s->emu) : 0;
}
```

`sameboy_jni.c`:
1. Extend `nativeApplySettings` — add a trailing `jint rumbleMode`, clamp it, and set it on the struct:

```c
JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeApplySettings(JNIEnv *env, jclass c, jlong ctx,
        jint colorCorrection, jdouble lightTemp, jint border, jint highpass, jint rtcMode,
        jdouble rewindSeconds, jdouble turboCap, jdouble interference, jint rumbleMode)
{
    (void)env; (void)c;
    if (colorCorrection < 0 || colorCorrection > 6) colorCorrection = 2;
    if (border < 0 || border > 2) border = 0;
    if (highpass < 0 || highpass > 2) highpass = 1;
    if (rtcMode < 0 || rtcMode > 1) rtcMode = 0;
    if (rumbleMode < 0 || rumbleMode > 2) rumbleMode = 1;
    sb_settings s = {
        .color_correction = colorCorrection, .light_temperature = lightTemp,
        .border_mode = border, .highpass = highpass, .rtc_mode = rtcMode,
        .rewind_seconds = rewindSeconds, .turbo_cap = turboCap, .interference = interference,
        .rumble_mode = rumbleMode,
    };
    sb_session_apply_settings((sb_session *)(uintptr_t)ctx, &s);
}
```

2. Add (after `nativeSetPalette`):

```c
JNIEXPORT jint JNICALL
Java_io_sameboy_android_NativeBridge_nativeRumbleAmplitude(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; return sb_session_rumble_amplitude((sb_session *)(uintptr_t)ctx); }
```

`NativeBridge.java`: update the `nativeApplySettings` decl to add `int rumbleMode`, and add:

```java
    public static native int nativeRumbleAmplitude(long ctx);
```

- [ ] **Step 4: Build + host tests + symbols** —

```bash
/home/sian/SameBoy/Android/jni/test/run_host_tests.sh
cd ~/SameBoy/Android && JAVA_HOME=$HOME/Android/jdk17 ANDROID_HOME=$HOME/Android ./gradlew :app:assembleDebug
```
Expected: host tests pass. Build will **fail** in `Settings.java` (its `nativeApplySettings` call is now one arg short) — that is fixed in Task 3; if running Task 2 standalone, note it. To keep Task 2 self-contained, update the single caller now: in `Settings.apply`, append `, rumbleMode()` to the `nativeApplySettings(...)` call and add a temporary `int rumbleMode(){ return 1; }` (Task 3 replaces it with the real pref accessor).

Then: `nm -D --defined-only .../arm64-v8a/libsameboy_core.so | grep -c NativeBridge_native` → `21`.

- [ ] **Step 5: Commit**

```bash
git add Android/jni/session.h Android/jni/session.c Android/jni/sameboy_jni.c Android/jni/test/test_session.c Android/app/src/main/java/io/sameboy/android/NativeBridge.java Android/app/src/main/java/io/sameboy/android/Settings.java
git commit -m "feat(android): session rumble amplitude + JNI (nativeRumbleAmplitude, rumbleMode in applySettings)"
```

---

### Task 3: Settings rumble mode + GamepadMapper (mapping + prefs + presence)

**Files:**
- Modify: `Android/app/src/main/java/io/sameboy/android/Settings.java`
- Create: `Android/app/src/main/java/io/sameboy/android/GamepadMapper.java`

**Interfaces:**
- Produces: `Settings.rumbleMode()`/`setRumbleMode(int)` (default 1); `GamepadMapper` (below). Consumed by Tasks 5–6.

- [ ] **Step 1: Settings rumble mode.** Add key + accessors (with the others), and replace the temporary `rumbleMode()` from Task 2 with the real one:

```java
    private static final String K_RUMBLE = "rumble_mode";  // 0 disabled,1 cartridge,2 all
    int rumbleMode()          { return p.getInt(K_RUMBLE, 1); }
    void setRumbleMode(int v) { p.edit().putInt(K_RUMBLE, v).apply(); }
```

(`apply()` already passes `rumbleMode()` to `nativeApplySettings` from Task 2 — leave that call as-is.)

- [ ] **Step 2: Create `GamepadMapper.java`:**

```java
package io.sameboy.android;

import android.content.Context;
import android.content.SharedPreferences;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;

/** Hardware-gamepad → GB-key mapping (keycode table + prefs) and axis→d-pad translation. */
final class GamepadMapper {
    // GB key indices (match NativeBridge)
    static final int RIGHT = 0, LEFT = 1, UP = 2, DOWN = 3, A = 4, B = 5, SELECT = 6, START = 7;
    static final int KEYS = 8;
    static final String[] GB_NAMES = { "Right", "Left", "Up", "Down", "A", "B", "Select", "Start" };

    private static final int[] DEFAULTS = {
        KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_DPAD_LEFT,
        KeyEvent.KEYCODE_DPAD_UP,    KeyEvent.KEYCODE_DPAD_DOWN,
        KeyEvent.KEYCODE_BUTTON_A,   KeyEvent.KEYCODE_BUTTON_B,
        KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_BUTTON_START,
    };

    private final SharedPreferences p;
    private final int[] keycodeFor = new int[KEYS];

    GamepadMapper(Context ctx) {
        p = ctx.getApplicationContext().getSharedPreferences("sameboy_gamepad", Context.MODE_PRIVATE);
        load();
    }

    private void load() {
        for (int i = 0; i < KEYS; i++) keycodeFor[i] = p.getInt("gp_" + i, DEFAULTS[i]);
    }

    /** GB key (0..7) bound to this keycode, or -1. */
    int gbKeyForKeycode(int keycode) {
        for (int i = 0; i < KEYS; i++) if (keycodeFor[i] == keycode) return i;
        return -1;
    }

    int keycodeFor(int gbKey) { return keycodeFor[gbKey]; }

    void setBinding(int gbKey, int keycode) {
        keycodeFor[gbKey] = keycode;
        p.edit().putInt("gp_" + gbKey, keycode).apply();
    }

    void resetDefaults() {
        SharedPreferences.Editor e = p.edit();
        for (int i = 0; i < KEYS; i++) { keycodeFor[i] = DEFAULTS[i]; e.putInt("gp_" + i, DEFAULTS[i]); }
        e.apply();
    }

    /** True for a gamepad/joystick keycode we might bind (used to arm capture + ignore stray keys). */
    static boolean isGamepadKeycode(int keycode) {
        return KeyEvent.isGamepadButton(keycode)
            || keycode == KeyEvent.KEYCODE_DPAD_UP || keycode == KeyEvent.KEYCODE_DPAD_DOWN
            || keycode == KeyEvent.KEYCODE_DPAD_LEFT || keycode == KeyEvent.KEYCODE_DPAD_RIGHT;
    }

    static boolean anyGamepadConnected() {
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice d = InputDevice.getDevice(id);
            if (d == null) continue;
            int src = d.getSources();
            if ((src & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (src & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) return true;
        }
        return false;
    }

    /** D-pad edges from a joystick MotionEvent (hat + left stick), deadzone 0.5.
     *  Returns {right,left,up,down} booleans. */
    static boolean[] axisDpad(MotionEvent e) {
        float hx = e.getAxisValue(MotionEvent.AXIS_HAT_X);
        float hy = e.getAxisValue(MotionEvent.AXIS_HAT_Y);
        float sx = e.getAxisValue(MotionEvent.AXIS_X);
        float sy = e.getAxisValue(MotionEvent.AXIS_Y);
        float x = Math.abs(hx) > Math.abs(sx) ? hx : sx;
        float y = Math.abs(hy) > Math.abs(sy) ? hy : sy;
        return new boolean[]{ x > 0.5f, x < -0.5f, y < -0.5f, y > 0.5f };  // right,left,up,down
    }
}
```

- [ ] **Step 3: Build** — `./gradlew :app:assembleDebug` → `BUILD SUCCESSFUL`.

- [ ] **Step 4: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/Settings.java Android/app/src/main/java/io/sameboy/android/GamepadMapper.java
git commit -m "feat(android): rumble-mode pref + GamepadMapper (keycode/axis map, prefs, presence)"
```

---

### Task 4: EmulatorActivity — gamepad dispatch + rumble poller + overlay auto-hide

**Files:**
- Modify: `Android/app/src/main/java/io/sameboy/android/EmulatorActivity.java`
- Modify: `Android/app/src/main/AndroidManifest.xml` (VIBRATE permission)

**Interfaces:**
- Consumes: `GamepadMapper` (Task 3), `nativeRumbleAmplitude` (Task 2), existing `nativeSetKey`, `overlay` field (M4).

- [ ] **Step 1: Manifest VIBRATE permission** — add before `<application>`:

```xml
    <uses-permission android:name="android.permission.VIBRATE" />
```

- [ ] **Step 2: EmulatorActivity.** Add imports: `android.view.KeyEvent`, `android.view.MotionEvent`, `android.view.InputDevice`, `android.os.Vibrator`, `android.os.VibratorManager`, `android.os.VibrationEffect`, `android.hardware.input.InputManager`, `android.view.View`.

Add fields:

```java
    private GamepadMapper pad;
    private Vibrator vibrator;
    private boolean rumbling = false;
    private final boolean[] axisState = new boolean[4];   // right,left,up,down
    private final Runnable rumblePoll = new Runnable() {
        @Override public void run() {
            if (ctx != 0) {
                int amp = NativeBridge.nativeRumbleAmplitude(ctx);
                driveRumble(amp);
            }
            handler.postDelayed(this, 50);
        }
    };
    private final InputManager.InputDeviceListener padListener = new InputManager.InputDeviceListener() {
        @Override public void onInputDeviceAdded(int id) { refreshOverlayVisibility(); }
        @Override public void onInputDeviceRemoved(int id) { refreshOverlayVisibility(); }
        @Override public void onInputDeviceChanged(int id) { refreshOverlayVisibility(); }
    };
```

In `onCreate` (after `settings = new Settings(this);`): `pad = new GamepadMapper(this);` and resolve the vibrator:

```java
        if (android.os.Build.VERSION.SDK_INT >= 31) {
            VibratorManager vm = (VibratorManager) getSystemService(VIBRATOR_MANAGER_SERVICE);
            vibrator = vm != null ? vm.getDefaultVibrator() : null;
        } else {
            vibrator = (Vibrator) getSystemService(VIBRATOR_SERVICE);
        }
```

Add the input overrides + helpers:

```java
    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        int gb = (ctx != 0) ? pad.gbKeyForKeycode(event.getKeyCode()) : -1;
        if (gb >= 0 && event.getRepeatCount() == 0) {
            if (event.getAction() == KeyEvent.ACTION_DOWN) { NativeBridge.nativeSetKey(ctx, gb, true); return true; }
            if (event.getAction() == KeyEvent.ACTION_UP)   { NativeBridge.nativeSetKey(ctx, gb, false); return true; }
        } else if (gb >= 0) {
            return true;   // swallow auto-repeat for a held mapped key
        }
        return super.dispatchKeyEvent(event);
    }

    @Override public boolean onGenericMotionEvent(MotionEvent event) {
        if (ctx != 0 && (event.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
                && event.getAction() == MotionEvent.ACTION_MOVE) {
            boolean[] now = GamepadMapper.axisDpad(event);
            int[] gbForAxis = { GamepadMapper.RIGHT, GamepadMapper.LEFT, GamepadMapper.UP, GamepadMapper.DOWN };
            for (int i = 0; i < 4; i++) {
                if (now[i] != axisState[i]) { NativeBridge.nativeSetKey(ctx, gbForAxis[i], now[i]); axisState[i] = now[i]; }
            }
            return true;
        }
        return super.onGenericMotionEvent(event);
    }

    private void driveRumble(int amp) {
        if (vibrator == null || !vibrator.hasVibrator()) return;
        if (amp > 0) {
            int a = Math.max(1, Math.min(255, amp));
            try {
                if (vibrator.hasAmplitudeControl()) vibrator.vibrate(VibrationEffect.createOneShot(70, a));
                else vibrator.vibrate(VibrationEffect.createOneShot(70, VibrationEffect.DEFAULT_AMPLITUDE));
                rumbling = true;
            } catch (Exception ignored) {}
        } else if (rumbling) {
            vibrator.cancel();
            rumbling = false;
        }
    }

    private void refreshOverlayVisibility() {
        if (overlay != null) overlay.setVisibility(GamepadMapper.anyGamepadConnected() ? View.GONE : View.VISIBLE);
    }
```

In `onResume`: register the listener + start the poller + refresh overlay:

```java
        InputManager im = (InputManager) getSystemService(INPUT_SERVICE);
        if (im != null) im.registerInputDeviceListener(padListener, handler);
        handler.postDelayed(rumblePoll, 50);
        refreshOverlayVisibility();
```

In `onPause`: unregister + stop poller + stop any buzz:

```java
        handler.removeCallbacks(rumblePoll);
        InputManager im = (InputManager) getSystemService(INPUT_SERVICE);
        if (im != null) im.unregisterInputDeviceListener(padListener);
        if (vibrator != null) { try { vibrator.cancel(); } catch (Exception ignored) {} }
        rumbling = false;
```

(Place these alongside the existing `onResume`/`onPause` bodies — do not remove the battery poller / pause logic already there.)

- [ ] **Step 3: Build** — `./gradlew :app:assembleDebug` → `BUILD SUCCESSFUL`.

- [ ] **Step 4: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/EmulatorActivity.java Android/app/src/main/AndroidManifest.xml
git commit -m "feat(android): gamepad key/axis input + rumble poller + overlay auto-hide"
```

---

### Task 5: GamepadRemapActivity (capture screen)

**Files:**
- Create: `Android/app/src/main/java/io/sameboy/android/GamepadRemapActivity.java`
- Modify: `Android/app/src/main/AndroidManifest.xml` (register)

**Interfaces:**
- Consumes: `GamepadMapper` (Task 3).

- [ ] **Step 1: Create `GamepadRemapActivity.java`:**

```java
package io.sameboy.android;

import androidx.appcompat.app.AppCompatActivity;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/** Bind each GB input to a controller button: tap a row to arm, then press a button. */
public class GamepadRemapActivity extends AppCompatActivity {
    private GamepadMapper pad;
    private int capturing = -1;                 // GB key awaiting a keycode, or -1
    private final TextView[] rows = new TextView[GamepadMapper.KEYS];

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        setTitle(R.string.gamepad_buttons);
        pad = new GamepadMapper(this);
        int dp = (int) (getResources().getDisplayMetrics().density * 12);

        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(dp, dp, dp, dp);

        TextView hint = new TextView(this);
        hint.setText("Tap an input, then press a controller button.");
        hint.setPadding(0, 0, 0, dp);
        col.addView(hint);

        for (int i = 0; i < GamepadMapper.KEYS; i++) {
            final int gb = i;
            TextView row = new TextView(this);
            row.setPadding(0, dp, 0, dp);
            row.setOnClickListener(v -> { capturing = gb; renderAll(); });
            rows[i] = row;
            col.addView(row);
        }

        Button reset = new Button(this);
        reset.setText(R.string.reset_defaults);
        reset.setOnClickListener(v -> { pad.resetDefaults(); capturing = -1; renderAll(); });
        col.addView(reset);

        ScrollView sv = new ScrollView(this);
        sv.addView(col);
        setContentView(sv);
        renderAll();
    }

    private void renderAll() {
        for (int i = 0; i < GamepadMapper.KEYS; i++) {
            String key = KeyEvent.keyCodeToString(pad.keycodeFor(i));
            rows[i].setText(GamepadMapper.GB_NAMES[i] + ":  "
                + (capturing == i ? "press a button…" : key));
        }
    }

    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if (capturing >= 0 && event.getAction() == KeyEvent.ACTION_DOWN
                && GamepadMapper.isGamepadKeycode(event.getKeyCode())) {
            pad.setBinding(capturing, event.getKeyCode());
            capturing = -1;
            renderAll();
            return true;
        }
        return super.dispatchKeyEvent(event);
    }
}
```

- [ ] **Step 2: Register** in `AndroidManifest.xml` (after `SettingsActivity`):

```xml
        <activity android:name=".GamepadRemapActivity" android:exported="false"
                  android:configChanges="orientation|screenSize|keyboardHidden" />
```

- [ ] **Step 3: Build** — `./gradlew :app:assembleDebug` → `BUILD SUCCESSFUL`.

- [ ] **Step 4: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/GamepadRemapActivity.java Android/app/src/main/AndroidManifest.xml
git commit -m "feat(android): gamepad button remap capture screen"
```

---

### Task 6: SettingsActivity — Rumble row + Gamepad-buttons row + strings

**Files:**
- Modify: `Android/app/src/main/java/io/sameboy/android/SettingsActivity.java`, `res/values/strings.xml`

**Interfaces:**
- Consumes: `Settings.rumbleMode` (Task 3), `GamepadRemapActivity` (Task 5).

- [ ] **Step 1: Strings** — add to `res/values/strings.xml`:

```xml
    <string name="rumble">Rumble</string>
    <string name="gamepad_buttons">Gamepad buttons</string>
    <string name="reset_defaults">Reset to defaults</string>
```

- [ ] **Step 2: SettingsActivity.** In the **Controls** section (after Haptics), add a Rumble enum row and a Gamepad-buttons launcher row:

```java
        enumRow(col, getString(R.string.rumble),
            new String[]{ "Disabled", "Cartridge only", "All games" },
            s.rumbleMode(), i -> s.setRumbleMode(i));
        TextView gp = new TextView(this);
        gp.setText(getString(R.string.gamepad_buttons));
        gp.setPadding(0, dp(10), 0, dp(10));
        gp.setOnClickListener(v -> startActivity(new android.content.Intent(this, GamepadRemapActivity.class)));
        col.addView(gp);
```

- [ ] **Step 3: Build** — `./gradlew :app:assembleDebug` → `BUILD SUCCESSFUL`.

- [ ] **Step 4: Commit**

```bash
git add Android/app/src/main/java/io/sameboy/android/SettingsActivity.java Android/app/src/main/res/values/strings.xml
git commit -m "feat(android): Settings rumble mode + gamepad remap entry"
```

---

### Task 7: Integration check — full build + host tests

**Files:** none — verification only.

- [ ] **Step 1: Host suite** — `Android/jni/test/run_host_tests.sh` → `ALL HOST TESTS PASSED` (ring, emulator incl. rumble, session incl. rumble amplitude).

- [ ] **Step 2: Clean build + ABI/symbol check** —

```bash
cd ~/SameBoy/Android && JAVA_HOME=$HOME/Android/jdk17 ANDROID_HOME=$HOME/Android ./gradlew clean :app:assembleDebug
APK=app/build/outputs/apk/debug/app-debug.apk
unzip -l $APK | grep -c libsameboy_core.so     # expect 4
D=app/build/intermediates/merged_native_libs/debug/mergeDebugNativeLibs/out/lib/arm64-v8a
nm -D --defined-only $D/libsameboy_core.so | grep -c NativeBridge_native   # expect 21
```
Expected: `BUILD SUCCESSFUL`, `4`, `21`.

- [ ] **Step 3: Report status.** On-device Waydroid acceptance (spec §8) is run by the controller after the final review: `adb shell input gamepad keyevent <code>` drives the game (mapper matches by keycode); remap capture + persistence; rumble poller runs without crashing (no motor on the AVD); overlay auto-hide.
