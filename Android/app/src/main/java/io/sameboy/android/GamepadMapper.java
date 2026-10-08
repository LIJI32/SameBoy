package io.sameboy.android;

import android.content.Context;
import android.content.SharedPreferences;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;

/** Hardware-gamepad → GB-key mapping (keycode table + prefs) and axis→d-pad translation.
 *  Indices 0..7 are Core keys; MENU and TURBO are frontend actions handled by the activity. */
public final class GamepadMapper {
    // GB key indices (match NativeBridge), plus MENU/TURBO: frontend actions, never sent to the Core.
    static final int RIGHT = 0, LEFT = 1, UP = 2, DOWN = 3, A = 4, B = 5, SELECT = 6, START = 7;
    static final int MENU = 8;
    static final int TURBO = 9;
    static final int KEYS = 10;
    static final String[] GB_NAMES = { "Right", "Left", "Up", "Down", "A", "B", "Select", "Start", "Menu", "Fast forward" };

    private static final int[] DEFAULTS = {
        KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_DPAD_LEFT,
        KeyEvent.KEYCODE_DPAD_UP,    KeyEvent.KEYCODE_DPAD_DOWN,
        KeyEvent.KEYCODE_BUTTON_A,   KeyEvent.KEYCODE_BUTTON_B,
        KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_BUTTON_START,
        // L1: present on every gamepad and unused by the Game Boy, so the in-game menu is
        // always reachable when the on-screen controls are hidden.
        KeyEvent.KEYCODE_BUTTON_L1,
        // R1: likewise on every pad and unused by the Game Boy; mirrors the L1 = Menu choice.
        KeyEvent.KEYCODE_BUTTON_R1,
    };

    private final SharedPreferences p;
    private final int[] keycodeFor = new int[KEYS];

    GamepadMapper(Context ctx) {
        p = ctx.getApplicationContext().getSharedPreferences("sameboy_gamepad", Context.MODE_PRIVATE);
        load();
    }

    /** (Re)load bindings from prefs — call after they may have changed in another instance. */
    void load() {
        for (int i = 0; i < KEYS; i++) keycodeFor[i] = p.getInt("gp_" + i, DEFAULTS[i]);
    }

    /** Key index (0..KEYS-1, incl. MENU/TURBO) bound to this keycode, or -1. */
    int gbKeyForKeycode(int keycode) {
        for (int i = 0; i < KEYS; i++) if (keycodeFor[i] == keycode) return i;
        return -1;
    }

    int keycodeFor(int gbKey) { return keycodeFor[gbKey]; }

    void setBinding(int gbKey, int keycode) {
        // A keycode maps to one GB key: clear any other key currently holding it.
        SharedPreferences.Editor e = p.edit();
        for (int i = 0; i < KEYS; i++) {
            if (i != gbKey && keycodeFor[i] == keycode) { keycodeFor[i] = -1; e.putInt("gp_" + i, -1); }
        }
        keycodeFor[gbKey] = keycode;
        e.putInt("gp_" + gbKey, keycode).apply();
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

    /** True when a physical controller is attached: gamepad, joystick, or a real d-pad device.
     *  SOURCE_DPAD must skip virtual devices — Android's synthetic keyboard reports DPAD on every
     *  device, which would make "Auto" hide the touch controls even with no controller present. */
    static boolean anyGamepadConnected() {
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice d = InputDevice.getDevice(id);
            if (d == null) continue;
            int src = d.getSources();
            if ((src & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (src & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
                || ((src & InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD && !d.isVirtual()))
                return true;
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

    /** Hat/stick d-pad → synthetic KEYCODE_DPAD_* key events for UI navigation (menus, library,
     *  settings). Many pads (and Retroid firmware in "Xbox" mode) report the d-pad only as
     *  AXIS_HAT_X/Y, which View/Compose focus traversal never sees. `state` is the caller's
     *  4-entry {right,left,up,down} latch. Returns true when the event was a joystick move. */
    public static boolean hatToDpadKeys(MotionEvent e, boolean[] state, java.util.function.Consumer<KeyEvent> sink) {
        if ((e.getSource() & InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK
                || e.getAction() != MotionEvent.ACTION_MOVE) return false;
        final int[] codes = { KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_DPAD_LEFT,
                              KeyEvent.KEYCODE_DPAD_UP,    KeyEvent.KEYCODE_DPAD_DOWN };
        boolean[] now = axisDpad(e);
        for (int i = 0; i < 4; i++) {
            if (now[i] == state[i]) continue;
            state[i] = now[i];
            sink.accept(new KeyEvent(e.getDownTime(), e.getEventTime(),
                now[i] ? KeyEvent.ACTION_DOWN : KeyEvent.ACTION_UP, codes[i], 0, e.getMetaState(),
                e.getDeviceId(), 0, 0, InputDevice.SOURCE_DPAD));
        }
        return true;
    }
}
