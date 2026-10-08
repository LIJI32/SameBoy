package io.sameboy.android;

import android.os.Bundle;

/** Thin shell: all UI lives in SettingsUi (Compose, Cupertino look).
 *  Writes SharedPreferences via Settings; EmulatorActivity applies on resume. */
public class SettingsActivity extends DpadActivity {
    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        SettingsUi.bind(this, new Settings(this));
    }
}
