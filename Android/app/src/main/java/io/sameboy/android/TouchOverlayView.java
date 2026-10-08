package io.sameboy.android;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.PointF;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.view.MotionEvent;
import android.view.View;

/** Game Boy console body + touch controls, iOS-port look (GBBackgroundView).
 *  Draws body gradient, screen bezel (SurfaceView shows through the well),
 *  official sprites with pressed states, rotated navy labels. 8-way d-pad. */
class TouchOverlayView extends View {
    interface ControlListener {
        void onKey(int gbKey, boolean pressed);
        void onSpecial(int what, boolean pressed);
    }
    static final int SPECIAL_REWIND = 8, SPECIAL_TURBO = 9, SPECIAL_MENU = 10;

    // theme (iOS GBTheme default)
    private static final int BODY_TOP = 0xFFC0C3C7, BODY_BOTTOM = 0xFFAEB0B4;
    private static final int BEZEL_TOP = 0xFF353535, BEZEL_BOTTOM = 0xFF2D2D2D;
    private static final int BRAND = 0xFF00468D;

    private final ControlListener listener;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint spritePaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final float dp = getResources().getDisplayMetrics().density;

    private GBLayout layout;
    private Bitmap dpadBmp, dpadShadow, dpadShadowDiag, buttonBmp, buttonPressedBmp, button2Bmp, button2PressedBmp;

    // input state: per-pointer key MASK (bit i = key i held by that pointer; specials use their own bits)
    private final java.util.HashMap<Integer, Integer> pointerMask = new java.util.HashMap<>();
    private final int[] keyCount = new int[11];
    private float opacity = 0.6f;
    private boolean haptics = true;
    private boolean darkConsole = false;
    private boolean swipePad = false;
    private int bodyTop = BODY_TOP, bodyBottom = BODY_BOTTOM;
    private int swipePointerId = -1;
    private float swipeOriginX, swipeOriginY;

    TouchOverlayView(Context ctx, ControlListener l) {
        super(ctx);
        listener = l;
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

    void setOpacity(float a) { opacity = a; invalidate(); }
    void setHaptics(boolean on) { haptics = on; }

    /** Core output size; 256x224 once an SGB border shows. EmulatorActivity owns the value. */
    private int srcW = 160, srcH = 144;

    void setSourceSize(int w, int h) {
        if (w == srcW && h == srcH) return;
        srcW = w; srcH = h;
        if (getWidth() > 0 && getHeight() > 0) relayout();
    }

    @Override protected void onSizeChanged(int w, int h, int ow, int oh) { relayout(); }

    private void relayout() {
        layout = new GBLayout(getWidth(), getHeight(), dp, false, srcW, srcH);
        invalidate();
    }

    // ---------- hit testing ----------

    private static final int MASK_REWIND = 1 << SPECIAL_REWIND, MASK_TURBO = 1 << SPECIAL_TURBO,
                             MASK_MENU = 1 << SPECIAL_MENU;

    /** 8-way d-pad + round buttons -> key mask for a touch point. */
    private int maskAt(float x, float y) {
        if (layout == null) return 0;
        if (dist2(x, y, layout.menu) < sq(30f * dp)) return MASK_MENU;
        if (dist2(x, y, layout.rewind) < sq(34f * dp)) return MASK_REWIND;
        if (dist2(x, y, layout.turbo) < sq(34f * dp)) return MASK_TURBO;
        if (dist2(x, y, layout.a) < sq(44f * dp)) return 1 << NativeBridge.KEY_A;
        if (dist2(x, y, layout.b) < sq(44f * dp)) return 1 << NativeBridge.KEY_B;
        if (dist2(x, y, layout.select) < sq(38f * dp)) return 1 << NativeBridge.KEY_SELECT;
        if (dist2(x, y, layout.start) < sq(38f * dp)) return 1 << NativeBridge.KEY_START;
        float dx = x - layout.dpad.x, dy = y - layout.dpad.y;
        float half = 92f * dp;                          // generous dpad square
        if (Math.abs(dx) < half && Math.abs(dy) < half) {
            if (swipePad) return 0;                     // swipe mode: onTouchEvent owns the pad
            return sectorMask(dx, dy, 14f * dp);        // 14dp dead center
        }
        return 0;
    }

    /** 8-way sector mask for an offset from the pad anchor; 0 inside the dead zone. */
    private int sectorMask(float dx, float dy, float deadZonePx) {
        if (dx * dx + dy * dy < deadZonePx * deadZonePx) return 0;
        int m = 0;
        double ang = Math.toDegrees(Math.atan2(-dy, dx));        // 0=right, CCW
        if (ang < 0) ang += 360;
        if (ang < 67.5 || ang > 292.5) m |= 1 << NativeBridge.KEY_RIGHT;
        if (ang > 112.5 && ang < 247.5) m |= 1 << NativeBridge.KEY_LEFT;
        if (ang > 22.5 && ang < 157.5) m |= 1 << NativeBridge.KEY_UP;
        if (ang > 202.5 && ang < 337.5) m |= 1 << NativeBridge.KEY_DOWN;
        return m;
    }

    private boolean inDpadSquare(float x, float y) {
        if (layout == null) return false;
        float half = 92f * dp;
        return Math.abs(x - layout.dpad.x) < half && Math.abs(y - layout.dpad.y) < half;
    }

    private static float sq(float v) { return v * v; }
    private static float dist2(float x, float y, PointF p) {
        float dx = x - p.x, dy = y - p.y; return dx * dx + dy * dy;
    }

    // ---------- press bookkeeping (mask-based; refcount semantics preserved) ----------

    private void applyMask(int pointerId, int newMask) {
        Integer boxed = pointerMask.get(pointerId);
        int oldMask = boxed == null ? 0 : boxed;
        if (oldMask == newMask) return;
        int added = newMask & ~oldMask, removed = oldMask & ~newMask;
        boolean buzzed = false;
        for (int k = 0; k <= SPECIAL_MENU; k++) {
            int bit = 1 << k;
            if ((added & bit) != 0) {
                if (k == SPECIAL_MENU) {
                    if (haptics && !buzzed) { performHapticFeedback(android.view.HapticFeedbackConstants.VIRTUAL_KEY); buzzed = true; }
                    listener.onSpecial(SPECIAL_MENU, true);      // one-shot, not refcounted
                } else if (keyCount[k]++ == 0) {
                    if (haptics && !buzzed) { performHapticFeedback(android.view.HapticFeedbackConstants.VIRTUAL_KEY); buzzed = true; }
                    if (k < 8) listener.onKey(k, true); else listener.onSpecial(k, true);
                }
            }
            if ((removed & bit) != 0 && k != SPECIAL_MENU) {
                if (--keyCount[k] == 0) {
                    if (k < 8) listener.onKey(k, false); else listener.onSpecial(k, false);
                }
            }
        }
        if (newMask == 0) pointerMask.remove(pointerId); else pointerMask.put(pointerId, newMask);
        invalidate();
    }

    /** Drop every held touch key (also called when the controls are hidden mid-press). */
    void releaseAll() {
        for (Integer id : new java.util.ArrayList<>(pointerMask.keySet())) applyMask(id, 0);
        for (int i = 0; i < keyCount.length; i++) keyCount[i] = 0;
        invalidate();
    }

    @Override public boolean onTouchEvent(MotionEvent e) {
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int idx = e.getActionIndex();
                int id = e.getPointerId(idx);
                float x = e.getX(idx), y = e.getY(idx);
                if (swipePad && swipePointerId == -1 && inDpadSquare(x, y)) {
                    swipePointerId = id;
                    swipeOriginX = x; swipeOriginY = y;
                    applyMask(id, 0);                    // no direction until movement
                } else {
                    applyMask(id, maskAt(x, y));
                }
                break;
            }
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < e.getPointerCount(); i++) {
                    int id = e.getPointerId(i);
                    if (id == swipePointerId) {
                        float dx = e.getX(i) - swipeOriginX, dy = e.getY(i) - swipeOriginY;
                        float dist = (float) Math.hypot(dx, dy);
                        if (dist > 16f * dp) {
                            applyMask(id, sectorMask(dx, dy, 0));
                            if (dist > 24f * dp) {       // iOS leash: origin trails 24dp behind the finger
                                swipeOriginX = e.getX(i) - dx / dist * 24f * dp;
                                swipeOriginY = e.getY(i) - dy / dist * 24f * dp;
                            }
                        } else {
                            applyMask(id, 0);
                        }
                        continue;
                    }
                    int m = maskAt(e.getX(i), e.getY(i));
                    Integer old = pointerMask.get(id);
                    // menu is one-shot: never re-fire it on move
                    if (old != null && (old & MASK_MENU) != 0) m |= MASK_MENU;
                    applyMask(id, m);
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                int id = e.getPointerId(e.getActionIndex());
                if (id == swipePointerId) swipePointerId = -1;
                applyMask(id, 0);
                break;
            }
            case MotionEvent.ACTION_CANCEL:
                swipePointerId = -1;
                releaseAll();
                break;
        }
        return true;
    }

    // ---------- drawing ----------

    private int heldMask() {
        int m = 0;
        for (int k = 0; k < 8; k++) if (keyCount[k] > 0) m |= 1 << k;
        return m;
    }

    @Override protected void onDraw(Canvas c) {
        if (layout == null) return;
        int w = getWidth(), h = getHeight();

        // body gradient around the screen well
        paint.setShader(new LinearGradient(0, 0, 0, h, bodyTop, bodyBottom, Shader.TileMode.CLAMP));
        c.save();
        c.clipOutRect(layout.screenRect);
        c.drawRect(0, 0, w, h, paint);
        paint.setShader(null);

        // bezel: rounded rect ring around the screen
        RectF bez = new RectF(layout.screenRect);
        bez.inset(-layout.bezelWidth, -layout.bezelWidth);
        paint.setShader(new LinearGradient(0, bez.top, 0, bez.bottom, BEZEL_TOP, BEZEL_BOTTOM, Shader.TileMode.CLAMP));
        c.drawRoundRect(bez, layout.bezelWidth, layout.bezelWidth, paint);
        paint.setShader(null);
        c.restore();

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

        // labels + wordmark on the body
        paint.setColor(BRAND);
        if (layout.drawLogo) {
            paint.setTypeface(Typeface.create(Typeface.DEFAULT_BOLD, Typeface.BOLD_ITALIC));
            paint.setTextSize(layout.logoSize);
            paint.setTextAlign(Paint.Align.CENTER);
            c.drawText("SAMEBOY", w / 2f, layout.logoY + layout.logoSize, paint);
        }
        drawRotatedLabel(c, "A", layout.a, 24f * dp, 40f * dp, Typeface.DEFAULT_BOLD);
        drawRotatedLabel(c, "B", layout.b, 24f * dp, 40f * dp, Typeface.DEFAULT_BOLD);
        float selDist = (layout.compact ? 22f : 30f) * dp;   // clear the pills below in compact
        drawRotatedLabel(c, "SELECT", layout.select, 14f * dp, selDist, Typeface.DEFAULT_BOLD);
        drawRotatedLabel(c, "START", layout.start, 14f * dp, selDist, Typeface.DEFAULT_BOLD);

        // controls layer (respects opacity setting)
        int alpha = (int) (opacity * 255);
        spritePaint.setAlpha(alpha);
        int held = heldMask();
        // d-pad + rotated shadow
        drawSpriteCentered(c, dpadBmp, layout.dpad, 147f * dp, 151f * dp);
        int dmask = held & (1 << NativeBridge.KEY_RIGHT | 1 << NativeBridge.KEY_LEFT
                          | 1 << NativeBridge.KEY_UP | 1 << NativeBridge.KEY_DOWN);
        float rot = Float.NaN; boolean diag = false;
        if (dmask == (1 << NativeBridge.KEY_RIGHT)) rot = 0;
        else if (dmask == (1 << NativeBridge.KEY_RIGHT | 1 << NativeBridge.KEY_DOWN)) { rot = 0; diag = true; }
        else if (dmask == (1 << NativeBridge.KEY_DOWN)) rot = 90;
        else if (dmask == (1 << NativeBridge.KEY_LEFT | 1 << NativeBridge.KEY_DOWN)) { rot = 90; diag = true; }
        else if (dmask == (1 << NativeBridge.KEY_LEFT)) rot = 180;
        else if (dmask == (1 << NativeBridge.KEY_LEFT | 1 << NativeBridge.KEY_UP)) { rot = 180; diag = true; }
        else if (dmask == (1 << NativeBridge.KEY_UP)) rot = -90;
        else if (dmask == (1 << NativeBridge.KEY_RIGHT | 1 << NativeBridge.KEY_UP)) { rot = -90; diag = true; }
        if (!Float.isNaN(rot)) {
            c.save();
            c.rotate(rot, layout.dpad.x, layout.dpad.y);
            drawSpriteCentered(c, diag ? dpadShadowDiag : dpadShadow, layout.dpad, 147f * dp, 147f * dp);
            c.restore();
        }
        // A/B + Select/Start with pressed swaps
        drawSpriteCentered(c, (held & 1 << NativeBridge.KEY_A) != 0 ? buttonPressedBmp : buttonBmp, layout.a, 75f * dp, 79f * dp);
        drawSpriteCentered(c, (held & 1 << NativeBridge.KEY_B) != 0 ? buttonPressedBmp : buttonBmp, layout.b, 75f * dp, 79f * dp);
        drawSpriteCentered(c, (held & 1 << NativeBridge.KEY_SELECT) != 0 ? button2PressedBmp : button2Bmp, layout.select, 76f * dp, 76f * dp);
        drawSpriteCentered(c, (held & 1 << NativeBridge.KEY_START) != 0 ? button2PressedBmp : button2Bmp, layout.start, 76f * dp, 76f * dp);
        // rewind/turbo pills
        drawPill(c, layout.rewind, "<<", keyCount[SPECIAL_REWIND] > 0, alpha);
        drawPill(c, layout.turbo, ">>", keyCount[SPECIAL_TURBO] > 0, alpha);
        drawPill(c, layout.menu, "\u2261", false, alpha);
        spritePaint.setAlpha(255);
    }

    private void drawSpriteCentered(Canvas c, Bitmap bmp, PointF center, float wDp, float hDp) {
        RectF dst = new RectF(center.x - wDp / 2, center.y - hDp / 2, center.x + wDp / 2, center.y + hDp / 2);
        c.drawBitmap(bmp, null, dst, spritePaint);
    }

    private void drawPill(Canvas c, PointF center, String glyph, boolean pressed, int alpha) {
        float rw = 28f * dp, rh = 18f * dp;
        RectF r = new RectF(center.x - rw, center.y - rh, center.x + rw, center.y + rh);
        paint.setColor(Color.argb((int) (alpha * (pressed ? 0.85f : 0.55f)), 45, 45, 45));
        c.drawRoundRect(r, rh, rh, paint);
        paint.setColor(Color.argb(alpha, 192, 195, 199));
        paint.setTypeface(Typeface.DEFAULT_BOLD);
        paint.setTextAlign(Paint.Align.CENTER);
        paint.setTextSize(rh);
        c.drawText(glyph, center.x, center.y + rh * 0.35f, paint);
    }

    private void drawRotatedLabel(Canvas c, String text, PointF origin, float sizePx, float distPx, Typeface face) {
        c.save();
        c.rotate(-30f, origin.x, origin.y);
        paint.setColor(BRAND);
        paint.setTypeface(face);
        paint.setTextSize(sizePx);
        paint.setTextAlign(Paint.Align.CENTER);
        c.drawText(text, origin.x, origin.y + distPx + sizePx, paint);
        c.restore();
    }
}
