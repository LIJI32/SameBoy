# Android GB Skin Extras Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Console themes (silver/dark/follow), swipe d-pad mode, and screen-well depth shading on the Android GB skin, ported from the iOS app.

**Architecture:** New prefs in `Settings`, two new rows in `SettingsUi`, and three additions to `TouchOverlayView` (`setConsoleTheme`, `setSwipePad`, depth draws). Dark buttons are the iOS `-tint` sprites recolored once via `ColorMatrixColorFilter` and cached.

**Tech Stack:** Existing Android canvas APIs. No new dependencies. iOS PNGs (same repo, MIT).

## Global Constraints

- Build with `JAVA_HOME=$HOME/Android/jdk17`; gradle working dir `/home/sian/SameBoy/Android`.
- No new dependencies. `jni/`, NativeBridge untouched. ControlListener unchanged.
- Input semantics preserved: refcounted key masks, ACTION_CANCEL releases all, one-shot menu, haptics on first press. Swipe mode changes ONLY how d-pad direction is derived.
- No test frameworks; per-task verification = `:app:compileDebugJavaWithJavac` BUILD SUCCESSFUL; device verification is Task 3.
- iOS constants: dark body top `#181C23`, bottom channel = `pow(c/255,1.125)*255`; dark button tint `#080C12`; recolor matrix rows `[c*1.34, 1-c, 0, 0, 0]` (RGB), alpha identity.

---

### Task 1: Assets + Settings + SettingsUi rows

**Files:**
- Create: 7 sprite names × @2x/@3x under `Android/app/src/main/res/drawable-xhdpi|xxhdpi/`
- Modify: `Android/app/src/main/java/io/sameboy/android/Settings.java`
- Modify: `Android/app/src/main/java/io/sameboy/android/SettingsUi.kt`

**Interfaces:**
- Produces: `Settings.consoleTheme()` int 0/1/2 default 2, `setConsoleTheme(int)`; `Settings.swipeDpad()` bool default false, `setSwipeDpad(boolean)`; drawables `gb_dpad_tint, gb_button2_tint, gb_button2_pressed_tint, gb_swipepad, gb_swipepad_tint, gb_swipepad_shadow, gb_swipepad_shadow_diag`.

- [ ] **Step 1: Copy sprites**

```bash
cd /home/sian/SameBoy
for pair in "dpad-tint:gb_dpad_tint" "button2-tint:gb_button2_tint" "button2Pressed-tint:gb_button2_pressed_tint" \
            "swipepad:gb_swipepad" "swipepad-tint:gb_swipepad_tint" \
            "swipepadShadow:gb_swipepad_shadow" "swipepadShadowDiagonal:gb_swipepad_shadow_diag"; do
  src="${pair%%:*}"; dst="${pair##*:}"
  cp "iOS/${src}@2x.png" "Android/app/src/main/res/drawable-xhdpi/${dst}.png"
  cp "iOS/${src}@3x.png" "Android/app/src/main/res/drawable-xxhdpi/${dst}.png"
done
```

- [ ] **Step 2: Settings.java — after the `K_RUMBLE` key line add**

```java
    private static final String K_CONSOLE = "console_theme";        // 0 SameBoy,1 Dark,2 Follow theme
    private static final String K_SWIPE_DPAD = "swipe_dpad";        // bool
```

and after `setRumbleMode` add:

```java
    int consoleTheme()          { return p.getInt(K_CONSOLE, 2); }
    void setConsoleTheme(int v) { p.edit().putInt(K_CONSOLE, v).apply(); }
    boolean swipeDpad()         { return p.getBoolean(K_SWIPE_DPAD, false); }
    void setSwipeDpad(boolean v){ p.edit().putBoolean(K_SWIPE_DPAD, v).apply(); }

    /** Resolved console skin: true = dark body/buttons. Mode 2 follows the app theme. */
    boolean consoleIsDark(android.content.Context ctx) {
        int mode = consoleTheme();
        if (mode == 0) return false;
        if (mode == 1) return true;
        int night = ctx.getResources().getConfiguration().uiMode
                    & android.content.res.Configuration.UI_MODE_NIGHT_MASK;
        return night == android.content.res.Configuration.UI_MODE_NIGHT_YES;
    }
```

- [ ] **Step 3: SettingsUi.kt**

State (after the `theme` var, ~line 86): `var console by remember { mutableIntStateOf(s.consoleTheme()) }` and `var swipeDpad by remember { mutableStateOf(s.swipeDpad()) }`.

Controls section: after the Haptics ToggleRow insert

```kotlin
            { ToggleRow("Swipe d-pad", swipeDpad) { swipeDpad = it; s.setSwipeDpad(it) } },
```

Appearance section: after the Theme PickerRow insert

```kotlin
            {
                PickerRow("Console", listOf("SameBoy", "SameBoy Dark", "Follow theme"), console) {
                    console = it; s.setConsoleTheme(it)
                }
            },
```

- [ ] **Step 4: Compile** → BUILD SUCCESSFUL.
- [ ] **Step 5: Commit** — `feat(android): console-theme + swipe-dpad settings, tint/swipepad sprites`

---

### Task 2: TouchOverlayView themes + swipe pad + depth; activity wiring

**Files:**
- Modify: `Android/app/src/main/java/io/sameboy/android/TouchOverlayView.java`
- Modify: `Android/app/src/main/java/io/sameboy/android/EmulatorActivity.java` (2 small edits)

**Interfaces:**
- Produces on TouchOverlayView: `void setConsoleTheme(boolean dark)`, `void setSwipePad(boolean on)`.

- [ ] **Step 1: TouchOverlayView — theme + sprites**

Replace the constructor's seven `BitmapFactory.decodeResource` lines with a sprite-load helper and theme state (fields next to `controlsHidden`):

```java
    private boolean darkConsole = false;
    private boolean swipePad = false;
    private int bodyTop = BODY_TOP, bodyBottom = BODY_BOTTOM;
```

```java
    TouchOverlayView(Context ctx, ControlListener l, ScreenRectListener r) {
        super(ctx);
        listener = l;
        rectListener = r;
        loadSprites();
    }

    private Bitmap res(int id) { return BitmapFactory.decodeResource(getResources(), id); }

    /** iOS GBTheme._recolorImage: ColorMatrix rows [c*1.34, 1-c, 0, 0, 0] over purple-hued art. */
    private Bitmap recolor(Bitmap src, int color) {
        float r = ((color >> 16) & 0xFF) / 255f, g = ((color >> 8) & 0xFF) / 255f, b = (color & 0xFF) / 255f;
        android.graphics.ColorMatrix m = new android.graphics.ColorMatrix(new float[] {
            r * 1.34f, 1 - r, 0, 0, 0,
            g * 1.34f, 1 - g, 0, 0, 0,
            b * 1.34f, 1 - b, 0, 0, 0,
            0, 0, 0, 1, 0,
        });
        Bitmap out = Bitmap.createBitmap(src.getWidth(), src.getHeight(), Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(out);
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        p.setColorFilter(new android.graphics.ColorMatrixColorFilter(m));
        c.drawBitmap(src, 0, 0, p);
        return out;
    }

    private static final int DARK_BUTTON = 0xFF080C12;
    private static final int DARK_BODY_TOP = 0xFF181C23;

    private void loadSprites() {
        dpadShadow = res(swipePad ? R.drawable.gb_swipepad_shadow : R.drawable.gb_dpad_shadow);
        dpadShadowDiag = res(swipePad ? R.drawable.gb_swipepad_shadow_diag : R.drawable.gb_dpad_shadow_diag);
        if (darkConsole) {
            dpadBmp = recolor(res(swipePad ? R.drawable.gb_swipepad_tint : R.drawable.gb_dpad_tint), DARK_BUTTON);
            buttonBmp = recolor(res(R.drawable.gb_button), DARK_BUTTON);
            buttonPressedBmp = recolor(res(R.drawable.gb_button_pressed), DARK_BUTTON);
            button2Bmp = recolor(res(R.drawable.gb_button2_tint), DARK_BUTTON);
            button2PressedBmp = recolor(res(R.drawable.gb_button2_pressed_tint), DARK_BUTTON);
            bodyTop = DARK_BODY_TOP;
            bodyBottom = darkenPow(DARK_BODY_TOP);
        } else {
            dpadBmp = res(swipePad ? R.drawable.gb_swipepad : R.drawable.gb_dpad);
            buttonBmp = res(R.drawable.gb_button);
            buttonPressedBmp = res(R.drawable.gb_button_pressed);
            button2Bmp = res(R.drawable.gb_button2);
            button2PressedBmp = res(R.drawable.gb_button2_pressed);
            bodyTop = BODY_TOP;
            bodyBottom = BODY_BOTTOM;
        }
    }

    /** iOS setupBackgroundWithColor: bottom = pow(c/255, 1.125) per channel. */
    private static int darkenPow(int color) {
        int r = (int) (Math.pow(((color >> 16) & 0xFF) / 255.0, 1.125) * 255);
        int g = (int) (Math.pow(((color >> 8) & 0xFF) / 255.0, 1.125) * 255);
        int b = (int) (Math.pow((color & 0xFF) / 255.0, 1.125) * 255);
        return 0xFF000000 | (r << 16) | (g << 8) | b;
    }

    void setConsoleTheme(boolean dark) {
        if (dark == darkConsole) return;
        darkConsole = dark; loadSprites(); invalidate();
    }

    void setSwipePad(boolean on) {
        if (on == swipePad) return;
        swipePad = on; loadSprites(); invalidate();
    }
```

In `onDraw`, the body gradient uses `bodyTop`/`bodyBottom` instead of `BODY_TOP`/`BODY_BOTTOM`.

- [ ] **Step 2: swipe-pad hit logic**

Add fields: `private int swipePointerId = -1; private float swipeOriginX, swipeOriginY;`

In `maskAt`, the d-pad square branch becomes mode-dependent — extract the existing sector math into `private int sectorMask(float dx, float dy, float deadZonePx)` and for swipe mode track the anchor in `onTouchEvent` instead:

- ACTION_DOWN/POINTER_DOWN inside the d-pad square when `swipePad && swipePointerId == -1`: record `swipePointerId = id`, `swipeOrigin = (x,y)`, apply mask 0 (no direction until movement).
- ACTION_MOVE for that pointer: `dx = x - swipeOriginX, dy = y - swipeOriginY`; if `hypot > 16dp` apply `sectorMask(dx, dy, 0)` as that pointer's d-pad mask; if `hypot > 24dp` pull the origin to 24dp behind the finger (iOS leash):
  ```java
  float dist = (float) Math.hypot(dx, dy);
  swipeOriginX = x - dx / dist * 24f * dp;
  swipeOriginY = y - dy / dist * 24f * dp;
  ```
  else if `hypot <= 16dp` apply mask 0.
- ACTION_UP/CANCEL for that pointer: `swipePointerId = -1`, mask 0.
- Non-swipe pointers keep the existing `maskAt` path (buttons/pills work unchanged in swipe mode; only the d-pad square is claimed by the anchor logic — `maskAt`'s d-pad branch returns 0 when `swipePad` is true and the pointer isn't the swipe pointer... simpler: in swipe mode `maskAt` returns 0 for the d-pad square and `onTouchEvent` handles the pad exclusively).

Existing `applyMask` refcounting is reused unchanged — the swipe pointer just applies computed masks.

- [ ] **Step 3: screen-well depth (end of the bezel block in onDraw)**

```java
        // depth: light edge under the bezel + inner shadow ring in the well
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(1.5f * dp);
        paint.setColor(0x40FFFFFF);
        c.drawLine(bez.left + layout.bezelWidth, bez.bottom + 0.75f * dp,
                   bez.right - layout.bezelWidth, bez.bottom + 0.75f * dp, paint);
        paint.setStrokeWidth(layout.bezelWidth / 2f);
        paint.setColor(0x30000000);
        RectF inner = new RectF(layout.screenRect);
        inner.inset(layout.bezelWidth / 4f, layout.bezelWidth / 4f);
        c.drawRect(inner, paint);
        paint.setStyle(Paint.Style.FILL);
```

(The inner ring is drawn while the screen well is still clipped OUT — move these two draws BEFORE `c.restore()` if drawn after, or simply draw the ring after restore since it lies inside the well: draw AFTER restore, it overlays the SurfaceView edge with translucent black — acceptable and simplest: keep both draws after `c.restore()`.)

- [ ] **Step 4: EmulatorActivity — apply settings**

After `overlay.setHaptics(settings.haptics());` (construction, ~line 169) add:

```java
        overlay.setConsoleTheme(settings.consoleIsDark(this));
        overlay.setSwipePad(settings.swipeDpad());
```

And in onResume's overlay line (~line 215) extend to:

```java
            if (overlay != null) {
                overlay.setOpacity(settings.buttonOpacity());
                overlay.setHaptics(settings.haptics());
                overlay.setConsoleTheme(settings.consoleIsDark(this));
                overlay.setSwipePad(settings.swipeDpad());
            }
```

- [ ] **Step 5: Compile** → BUILD SUCCESSFUL.
- [ ] **Step 6: Commit** — `feat(android): dark console theme (iOS recolor), swipe d-pad mode, screen-well depth`

---

### Task 3: OnePlus Pad verification (controller-run)

- [ ] Follow theme + dark UI → dark console (body #181C23-ish, near-black buttons); Console=SameBoy → silver again.
- [ ] Swipe d-pad on → flat pad sprite; swipe right from pad center → game responds; shadow appears while swiping; regular taps on A/B still work.
- [ ] Swipe d-pad off → classic cross + classic behavior (regression).
- [ ] Depth: bezel bottom highlight + inner ring visible in a zoomed crop.
- [ ] Menu pill + opacity slider regression.
