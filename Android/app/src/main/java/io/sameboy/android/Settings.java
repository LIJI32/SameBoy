package io.sameboy.android;

import android.content.Context;
import android.content.SharedPreferences;

import androidx.appcompat.app.AppCompatDelegate;

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
    private static final String K_PALETTE = "palette_builtin";     // 0..3, or -1 custom
    private static final String K_CUSTOM0 = "palette_custom0";      // 0xRRGGBB
    private static final String K_CUSTOM1 = "palette_custom1";
    private static final String K_CUSTOM2 = "palette_custom2";
    private static final String K_CUSTOM3 = "palette_custom3";
    private static final String K_THEME = "theme_mode";             // 0 System,1 Light,2 Dark
    private static final String K_RUMBLE = "rumble_mode";  // 0 disabled,1 cartridge,2 all
    private static final String K_CONSOLE = "console_theme";        // 0 SameBoy,1 Dark,2 Follow theme
    private static final String K_SWIPE_DPAD = "swipe_dpad";        // bool
    private static final String K_ONSCREEN = "onscreen_controls";  // 0 auto,1 always,2 never
    private static final String K_FILTER = "filter";               // 0 off,1 LCD

    // default custom = greyscale shades (darkest..lightest)
    private static final int[] CUSTOM_DEFAULT = { 0x000000, 0x555555, 0xAAAAAA, 0xFFFFFF };

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

    /** Model to boot the next launch — validated against the supported models
     *  (a corrupt/tampered prefs value would otherwise reach GB_init unclamped). */
    int modelForLaunch() {
        int m = model();
        return (m == NativeBridge.MODEL_DMG_B || m == NativeBridge.MODEL_SGB
                || m == NativeBridge.MODEL_AGB) ? m : NativeBridge.MODEL_CGB_E;
    }
    /** On-screen control alpha 0..1. */
    float buttonOpacity() { return buttonOpacityPct() / 100f; }
    int rumbleMode()          { return p.getInt(K_RUMBLE, 1); }
    void setRumbleMode(int v) { p.edit().putInt(K_RUMBLE, v).apply(); }
    int consoleTheme()          { return p.getInt(K_CONSOLE, 2); }
    void setConsoleTheme(int v) { p.edit().putInt(K_CONSOLE, v).apply(); }
    boolean swipeDpad()         { return p.getBoolean(K_SWIPE_DPAD, false); }
    void setSwipeDpad(boolean v){ p.edit().putBoolean(K_SWIPE_DPAD, v).apply(); }
    int onscreenControls()          { return p.getInt(K_ONSCREEN, 0); }
    void setOnscreenControls(int v) { p.edit().putInt(K_ONSCREEN, v).apply(); }
    int filter()                    { return p.getInt(K_FILTER, 0); }
    void setFilter(int v)           { p.edit().putInt(K_FILTER, v).apply(); }

    /** Resolved console skin: true = dark body/buttons. Mode 2 follows the app theme.
     *  EmulatorActivity is a plain Activity (no AppCompat night resources), so honor the
     *  in-app theme override (K_THEME) first and fall back to the system uiMode. */
    boolean consoleIsDark(android.content.Context ctx) {
        int mode = consoleTheme();
        if (mode == 0) return false;
        if (mode == 1) return true;
        int theme = themeMode();
        if (theme == 1) return false;   // app forced Light
        if (theme == 2) return true;    // app forced Dark
        int night = ctx.getResources().getConfiguration().uiMode
                    & android.content.res.Configuration.UI_MODE_NIGHT_MASK;
        return night == android.content.res.Configuration.UI_MODE_NIGHT_YES;
    }

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
            interferencePct() / 100.0,
            rumbleMode());
        NativeBridge.nativeSetVolume(ctx, volumePct() * 256 / 100);
        int builtin = paletteBuiltin();
        NativeBridge.nativeSetPalette(ctx, builtin,
            customColor(0), customColor(1), customColor(2), customColor(3));
    }

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
}
