package io.sameboy.android;

import android.view.MotionEvent;

import androidx.appcompat.app.AppCompatActivity;

/** AppCompatActivity whose UI can be driven by a hat-switch d-pad: AXIS_HAT_X/Y moves are
 *  turned into DPAD key events so View/Compose focus traversal sees them. Used by every
 *  non-gameplay screen; EmulatorActivity routes the same axes to the Core instead. */
public class DpadActivity extends AppCompatActivity {
    private final boolean[] hat = new boolean[4];

    @Override public boolean dispatchGenericMotionEvent(MotionEvent ev) {
        return GamepadMapper.hatToDpadKeys(ev, hat, this::dispatchKeyEvent) || super.dispatchGenericMotionEvent(ev);
    }
}
