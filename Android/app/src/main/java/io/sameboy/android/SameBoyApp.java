package io.sameboy.android;

import android.app.Application;

/** Applies the persisted light/dark theme before any activity is created. */
public class SameBoyApp extends Application {
    @Override public void onCreate() {
        super.onCreate();
        new Settings(this).applyTheme();
    }
}
