package io.sameboy.android;

import android.content.res.AssetManager;
import android.view.Surface;

public final class NativeBridge {
    static { System.loadLibrary("sameboy_core"); }
    private NativeBridge() {}

    public static native long nativeCreate(int model, byte[] rom, byte[] sav, AssetManager assets);
    public static native void nativeStart(long ctx, Surface surface);
    public static native void nativeStop(long ctx);
    public static native void nativePause(long ctx, boolean paused);
    public static native void nativeSetKey(long ctx, int gbKeyIndex, boolean pressed);
    public static native void nativeReset(long ctx);
    public static native byte[] nativeSaveBattery(long ctx);
    public static native byte[]  nativeSaveState(long ctx);
    public static native boolean nativeLoadState(long ctx, byte[] state);
    public static native void    nativeSetTurbo(long ctx, boolean on);
    public static native void    nativeSetRewinding(long ctx, boolean on);
    public static native void    nativeSwitchModel(long ctx, int model);
    public static native boolean nativeIsBatteryDirty(long ctx);
    public static native void    nativeClearBatteryDirty(long ctx);
    /** [0]=width, [1]=height, then width*height ABGR pixels. null on failure. */
    public static native int[]   nativeCopyFrame(long ctx);
    /** Current Core output size packed as (width << 16) | height — 160x144, or 256x224 while
     *  an SGB border is displayed. 0 when ctx is invalid or no frame has been produced yet.
     *  Cheap (reads the cached front-buffer size); safe on the main thread while paused. */
    public static native int nativeScreenSize(long ctx);
    /** Returns { title, crc32Hex8Upper } for a ROM buffer, or null if not a valid ROM. */
    public static native String[] nativeRomInfo(byte[] rom);
    public static native void nativeApplySettings(long ctx, int colorCorrection, double lightTemp,
            int border, int highpass, int rtcMode, double rewindSeconds, double turboCap, double interference,
            int rumbleMode);
    public static native void nativeSetVolume(long ctx, int volume256);
    public static native void nativeSetPalette(long ctx, int builtinIndex, int c0, int c1, int c2, int c3);
    /** Screen filter: 0 = off (nearest blit), 1 = LCD dot-matrix. */
    public static native void nativeSetFilter(long ctx, int mode);
    public static native void nativeConnectPrinter(long ctx);
    public static native void nativeDisconnectPrinter(long ctx);
    public static native int nativePrinterGeneration(long ctx);
    public static native int[] nativePrinterFeed(long ctx);      // ARGB, length rows*160 (0 if empty)
    public static native void nativePrinterClear(long ctx);
    public static native boolean nativeCameraWanted(long ctx);
    public static native void nativeCameraDeliver(long ctx, byte[] gray);  // 128*112 bytes
    public static native void nativeLinkListen(long ctx, int port);
    public static native void nativeLinkConnect(long ctx, String host, int port);
    public static native void nativeLinkDisconnect(long ctx);
    public static native int nativeLinkStatus(long ctx);   // 0 idle 1 listen 2 connecting 3 connected 4 error
    public static native int nativeRumbleAmplitude(long ctx);
    /** Cheats. code: GameShark (8 hex digits, e.g. "01FF16D0") or Game Genie ("xxx-xxx" / "xxx-xxx-xxx").
     *  Returns false if the Core rejects the code (nothing added). Cheats persist across reset,
     *  model switch and state load; they're dropped only when the session is destroyed.
     *  All three park the emu thread internally — call from the main thread. */
    public static native boolean nativeAddCheat(long ctx, String code, String desc, boolean enabled);
    public static native void    nativeRemoveAllCheats(long ctx);
    public static native void    nativeSetCheatsEnabled(long ctx, boolean on);   // master switch (GB_set_cheats_enabled)
    /** MBC7 tilt (e.g. Kirby Tilt 'n' Tumble). x,y in units of g (±4 recommended); setter is safe from
     *  any thread while running, no parking. */
    public static native boolean nativeHasAccelerometer(long ctx);
    public static native void    nativeSetAccelerometer(long ctx, double x, double y);
    public static native void nativeDestroy(long ctx);

    // GB_key_t indices (Core/joypad.h)
    public static final int KEY_RIGHT = 0, KEY_LEFT = 1, KEY_UP = 2, KEY_DOWN = 3,
                            KEY_A = 4, KEY_B = 5, KEY_SELECT = 6, KEY_START = 7;
    // GB_model_t (Core/model.h)
    public static final int MODEL_DMG_B = 0x002, MODEL_SGB = 0x004, MODEL_CGB_E = 0x205, MODEL_AGB = 0x207;
}
