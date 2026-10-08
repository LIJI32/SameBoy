package io.sameboy.android;

import android.os.Bundle;
import android.view.KeyEvent;

import java.util.ArrayList;
import java.util.List;

/** Bind each GB input to a controller button: tap a row to arm, then press a button. */
public class GamepadRemapActivity extends DpadActivity {
    private GamepadMapper pad;
    private int capturing = -1;                 // GB key awaiting a keycode, or -1
    private RemapUi.Model model;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        pad = new GamepadMapper(this);
        model = RemapUi.bind(this, new RemapUi.Callbacks() {
            @Override public void onArm(int index) { capturing = index; push(); }
            @Override public void onReset() { pad.resetDefaults(); capturing = -1; push(); }
            @Override public void onBack() { finish(); }
        });
        push();
    }

    private void push() {
        List<String> names = new ArrayList<>();
        for (int i = 0; i < GamepadMapper.KEYS; i++)
            names.add(KeyEvent.keyCodeToString(pad.keycodeFor(i)));
        model.update(capturing, names);
    }

    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if (capturing >= 0 && event.getAction() == KeyEvent.ACTION_DOWN
                && GamepadMapper.isGamepadKeycode(event.getKeyCode())) {
            pad.setBinding(capturing, event.getKeyCode());
            capturing = -1;
            push();
            return true;
        }
        return super.dispatchKeyEvent(event);
    }
}
