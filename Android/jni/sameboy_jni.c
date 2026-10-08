#include <jni.h>
#include <android/native_window_jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "session.h"
#include <Core/gb.h>

/* boot rom asset name -> GB_boot_rom_t, matching SDL's table */
static const char *const boot_names[] = {
    [GB_BOOT_ROM_DMG_0] = "dmg0_boot.bin",
    [GB_BOOT_ROM_DMG]   = "dmg_boot.bin",
    [GB_BOOT_ROM_MGB]   = "mgb_boot.bin",
    [GB_BOOT_ROM_SGB]   = "sgb_boot.bin",
    [GB_BOOT_ROM_SGB2]  = "sgb2_boot.bin",
    [GB_BOOT_ROM_CGB_0] = "cgb0_boot.bin",
    [GB_BOOT_ROM_CGB]   = "cgb_boot.bin",
    [GB_BOOT_ROM_CGB_E] = "cgbE_boot.bin",
    [GB_BOOT_ROM_AGB_0] = "agb0_boot.bin",
    [GB_BOOT_ROM_AGB]   = "agb_boot.bin",
};

/* Boot ROMs are tiny; the emulator copies each asset's bytes (sb_emu_set_boot_rom),
   so the temporary buffer is not needed after the call. */
static void load_boot_roms(sb_session *s, AAssetManager *am)
{
    for (int t = 0; t < (int)(sizeof(boot_names) / sizeof(boot_names[0])); t++) {
        if (!boot_names[t]) continue;
        char path[64];
        snprintf(path, sizeof(path), "bootroms/%s", boot_names[t]);
        AAsset *a = AAssetManager_open(am, path, AASSET_MODE_BUFFER);
        if (!a) continue;
        off_t len = AAsset_getLength(a);
        const void *bytes = AAsset_getBuffer(a);
        sb_session_set_boot_rom(s, t, (const uint8_t *)bytes, (size_t)len);  /* emulator copies */
        AAsset_close(a);
    }
}

JNIEXPORT jlong JNICALL
Java_io_sameboy_android_NativeBridge_nativeCreate(JNIEnv *env, jclass c, jint model,
                                                  jbyteArray rom, jbyteArray sav, jobject assets)
{
    (void)c;
    jsize rlen = (*env)->GetArrayLength(env, rom);
    jbyte *rbytes = (*env)->GetByteArrayElements(env, rom, NULL);
    if (!rbytes) return 0;
    jbyte *sbytes = NULL; jsize slen = 0;
    if (sav) {
        slen = (*env)->GetArrayLength(env, sav);
        sbytes = (*env)->GetByteArrayElements(env, sav, NULL);
        if (!sbytes) {
            (*env)->ReleaseByteArrayElements(env, rom, rbytes, JNI_ABORT);
            return 0;
        }
    }

    sb_session *s = sb_session_create(model, (const uint8_t *)rbytes, (size_t)rlen,
                                      (const uint8_t *)sbytes, (size_t)slen);

    (*env)->ReleaseByteArrayElements(env, rom, rbytes, JNI_ABORT);
    if (sbytes) (*env)->ReleaseByteArrayElements(env, sav, sbytes, JNI_ABORT);
    if (!s) return 0;

    AAssetManager *am = AAssetManager_fromJava(env, assets);
    if (am) load_boot_roms(s, am);
    sb_session_reset(s);
    return (jlong)(uintptr_t)s;
}

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeStart(JNIEnv *env, jclass c, jlong ctx, jobject surface)
{
    (void)c;
    sb_session *s = (sb_session *)(uintptr_t)ctx;
    ANativeWindow *win = ANativeWindow_fromSurface(env, surface);  /* +1 ref; released in stop */
    sb_session_start(s, win);
}

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeStop(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; sb_session_stop((sb_session *)(uintptr_t)ctx); }

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativePause(JNIEnv *env, jclass c, jlong ctx, jboolean p)
{ (void)env; (void)c; sb_session_pause((sb_session *)(uintptr_t)ctx, p); }

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeSetKey(JNIEnv *env, jclass c, jlong ctx, jint idx, jboolean pressed)
{
    (void)env; (void)c;
    if (idx < 0 || idx >= 8) return;  /* GB_KEY_MAX; Core assert is compiled out under NDEBUG */
    sb_session_set_key((sb_session *)(uintptr_t)ctx, idx, pressed);
}

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeReset(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; sb_session_reset((sb_session *)(uintptr_t)ctx); }

JNIEXPORT jbyteArray JNICALL
Java_io_sameboy_android_NativeBridge_nativeSaveBattery(JNIEnv *env, jclass c, jlong ctx)
{
    (void)c;
    uint8_t *buf = NULL;
    size_t n = sb_session_save_battery((sb_session *)(uintptr_t)ctx, &buf);
    if (n == 0) return NULL;
    jbyteArray arr = (*env)->NewByteArray(env, (jsize)n);
    if (!arr) { free(buf); return NULL; }
    (*env)->SetByteArrayRegion(env, arr, 0, (jsize)n, (const jbyte *)buf);
    free(buf);
    return arr;
}

JNIEXPORT jbyteArray JNICALL
Java_io_sameboy_android_NativeBridge_nativeSaveState(JNIEnv *env, jclass c, jlong ctx)
{
    (void)c;
    uint8_t *buf = NULL;
    size_t n = sb_session_save_state((sb_session *)(uintptr_t)ctx, &buf);
    if (n == 0) return NULL;
    jbyteArray arr = (*env)->NewByteArray(env, (jsize)n);
    if (arr) (*env)->SetByteArrayRegion(env, arr, 0, (jsize)n, (const jbyte *)buf);
    free(buf);
    return arr;
}

JNIEXPORT jboolean JNICALL
Java_io_sameboy_android_NativeBridge_nativeLoadState(JNIEnv *env, jclass c, jlong ctx, jbyteArray data)
{
    (void)c;
    if (!data) return JNI_FALSE;
    jsize n = (*env)->GetArrayLength(env, data);
    jbyte *bytes = (*env)->GetByteArrayElements(env, data, NULL);
    if (!bytes) return JNI_FALSE;
    int ret = sb_session_load_state((sb_session *)(uintptr_t)ctx, (const uint8_t *)bytes, (size_t)n);
    (*env)->ReleaseByteArrayElements(env, data, bytes, JNI_ABORT);
    return ret == 0 ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeSetTurbo(JNIEnv *env, jclass c, jlong ctx, jboolean on)
{ (void)env; (void)c; sb_session_set_turbo((sb_session *)(uintptr_t)ctx, on); }

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeSetRewinding(JNIEnv *env, jclass c, jlong ctx, jboolean on)
{ (void)env; (void)c; sb_session_set_rewinding((sb_session *)(uintptr_t)ctx, on); }

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeSwitchModel(JNIEnv *env, jclass c, jlong ctx, jint model)
{ (void)env; (void)c; sb_session_switch_model((sb_session *)(uintptr_t)ctx, model); }

JNIEXPORT jboolean JNICALL
Java_io_sameboy_android_NativeBridge_nativeIsBatteryDirty(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; return sb_session_battery_dirty((sb_session *)(uintptr_t)ctx) ? JNI_TRUE : JNI_FALSE; }

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeClearBatteryDirty(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; sb_session_clear_battery_dirty((sb_session *)(uintptr_t)ctx); }

JNIEXPORT jintArray JNICALL
Java_io_sameboy_android_NativeBridge_nativeCopyFrame(JNIEnv *env, jclass c, jlong ctx)
{
    (void)c;
    sb_session *s = (sb_session *)(uintptr_t)ctx;
    if (!s) return NULL;
    uint32_t *px = malloc(SB_FB_MAX * sizeof(uint32_t));
    if (!px) return NULL;
    unsigned w = 0, h = 0;
    sb_session_copy_frame(s, px, &w, &h);
    if (w == 0 || h == 0) { free(px); return NULL; }
    jintArray arr = (*env)->NewIntArray(env, (jsize)(2 + w * h));
    if (arr) {
        jint header[2] = { (jint)w, (jint)h };
        (*env)->SetIntArrayRegion(env, arr, 0, 2, header);
        (*env)->SetIntArrayRegion(env, arr, 2, (jsize)(w * h), (const jint *)px);
    }
    free(px);
    return arr;
}

JNIEXPORT jint JNICALL
Java_io_sameboy_android_NativeBridge_nativeScreenSize(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; return (jint)sb_session_screen_size((sb_session *)(uintptr_t)ctx); }

JNIEXPORT jobjectArray JNICALL
Java_io_sameboy_android_NativeBridge_nativeRomInfo(JNIEnv *env, jclass c, jbyteArray rom)
{
    (void)c;
    if (!rom) return NULL;
    jsize n = (*env)->GetArrayLength(env, rom);
    jbyte *bytes = (*env)->GetByteArrayElements(env, rom, NULL);
    if (!bytes) return NULL;
    char title[17];
    uint32_t crc = 0;
    int ret = sb_rom_info((const uint8_t *)bytes, (size_t)n, title, &crc);
    (*env)->ReleaseByteArrayElements(env, rom, bytes, JNI_ABORT);
    if (ret != 0) return NULL;
    char hex[9];
    snprintf(hex, sizeof(hex), "%08X", crc);
    jclass strClass = (*env)->FindClass(env, "java/lang/String");
    jobjectArray arr = (*env)->NewObjectArray(env, 2, strClass, NULL);
    if (!arr) return NULL;
    jstring jtitle = (*env)->NewStringUTF(env, title);
    jstring jcrc = (*env)->NewStringUTF(env, hex);
    (*env)->SetObjectArrayElement(env, arr, 0, jtitle);
    (*env)->SetObjectArrayElement(env, arr, 1, jcrc);
    return arr;
}

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeApplySettings(JNIEnv *env, jclass c, jlong ctx,
        jint colorCorrection, jdouble lightTemp, jint border, jint highpass, jint rtcMode,
        jdouble rewindSeconds, jdouble turboCap, jdouble interference, jint rumbleMode)
{
    (void)env; (void)c;
    /* clamp enum ints to their Core ranges (Core asserts are compiled out under NDEBUG) */
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

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeSetVolume(JNIEnv *env, jclass c, jlong ctx, jint volume256)
{ (void)env; (void)c; sb_session_set_volume((sb_session *)(uintptr_t)ctx, volume256); }

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeSetFilter(JNIEnv *env, jclass c, jlong ctx, jint mode)
{ (void)env; (void)c; sb_session_set_filter((sb_session *)(uintptr_t)ctx, mode); }

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeSetPalette(JNIEnv *env, jclass c, jlong ctx,
        jint builtinIndex, jint c0, jint c1, jint c2, jint c3)
{
    (void)env; (void)c;
    uint32_t rgb[4] = { (uint32_t)c0, (uint32_t)c1, (uint32_t)c2, (uint32_t)c3 };
    sb_session_set_palette((sb_session *)(uintptr_t)ctx, builtinIndex,
                           builtinIndex >= 0 ? NULL : rgb);
}

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeConnectPrinter(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; sb_session_connect_printer((sb_session *)(uintptr_t)ctx); }

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeDisconnectPrinter(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; sb_session_disconnect_printer((sb_session *)(uintptr_t)ctx); }

JNIEXPORT jint JNICALL
Java_io_sameboy_android_NativeBridge_nativePrinterGeneration(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; return (jint)sb_session_printer_generation((sb_session *)(uintptr_t)ctx); }

JNIEXPORT jintArray JNICALL
Java_io_sameboy_android_NativeBridge_nativePrinterFeed(JNIEnv *env, jclass c, jlong ctx)
{
    (void)c;
    sb_session *s = (sb_session *)(uintptr_t)ctx;
    unsigned rows = sb_session_printer_feed(s, NULL, 0);
    if (rows == 0) return (*env)->NewIntArray(env, 0);
    jsize n = (jsize)rows * 160;
    jintArray arr = (*env)->NewIntArray(env, n);
    if (!arr) { (*env)->ExceptionClear(env); return NULL; }   /* OOM: clear pending exc, Java sees null */
    uint32_t *tmp = malloc((size_t)n * sizeof(uint32_t));
    if (!tmp) { (*env)->DeleteLocalRef(env, arr); return NULL; }  /* avoid an all-zero (black) feed */
    sb_session_printer_feed(s, tmp, rows);
    (*env)->SetIntArrayRegion(env, arr, 0, n, (const jint *)tmp);
    free(tmp);
    return arr;
}

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativePrinterClear(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; sb_session_printer_clear((sb_session *)(uintptr_t)ctx); }

JNIEXPORT jboolean JNICALL
Java_io_sameboy_android_NativeBridge_nativeCameraWanted(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; return sb_session_camera_wanted((sb_session *)(uintptr_t)ctx) ? JNI_TRUE : JNI_FALSE; }

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeCameraDeliver(JNIEnv *env, jclass c, jlong ctx, jbyteArray gray)
{
    (void)c;
    sb_session *s = (sb_session *)(uintptr_t)ctx;
    if (!gray) return;
    jsize n = (*env)->GetArrayLength(env, gray);
    if (n != 128 * 112) return;
    jbyte *buf = (*env)->GetByteArrayElements(env, gray, NULL);
    if (!buf) return;
    sb_session_camera_deliver(s, (const uint8_t *)buf);
    (*env)->ReleaseByteArrayElements(env, gray, buf, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeLinkListen(JNIEnv *env, jclass c, jlong ctx, jint port)
{ (void)env; (void)c; sb_session_link_listen((sb_session *)(uintptr_t)ctx, (int)port); }

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeLinkConnect(JNIEnv *env, jclass c, jlong ctx, jstring host, jint port)
{
    (void)c;
    sb_session *s = (sb_session *)(uintptr_t)ctx;
    const char *h = host ? (*env)->GetStringUTFChars(env, host, NULL) : NULL;
    sb_session_link_connect(s, h ? h : "", (int)port);
    if (h) (*env)->ReleaseStringUTFChars(env, host, h);
}

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeLinkDisconnect(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; sb_session_link_disconnect((sb_session *)(uintptr_t)ctx); }

JNIEXPORT jint JNICALL
Java_io_sameboy_android_NativeBridge_nativeLinkStatus(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; return (jint)sb_session_link_status((sb_session *)(uintptr_t)ctx); }

JNIEXPORT jint JNICALL
Java_io_sameboy_android_NativeBridge_nativeRumbleAmplitude(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; return sb_session_rumble_amplitude((sb_session *)(uintptr_t)ctx); }

JNIEXPORT jboolean JNICALL
Java_io_sameboy_android_NativeBridge_nativeAddCheat(JNIEnv *env, jclass c, jlong ctx, jstring code, jstring desc, jboolean enabled)
{
    (void)c;
    sb_session *s = (sb_session *)(uintptr_t)ctx;
    if (!s || !code) return JNI_FALSE;
    const char *cs = (*env)->GetStringUTFChars(env, code, NULL);
    const char *ds = desc ? (*env)->GetStringUTFChars(env, desc, NULL) : NULL;
    int ok = sb_session_add_cheat(s, cs, ds ? ds : "", enabled == JNI_TRUE);
    if (ds) (*env)->ReleaseStringUTFChars(env, desc, ds);
    (*env)->ReleaseStringUTFChars(env, code, cs);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeRemoveAllCheats(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; sb_session_remove_all_cheats((sb_session *)(uintptr_t)ctx); }

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeSetCheatsEnabled(JNIEnv *env, jclass c, jlong ctx, jboolean on)
{ (void)env; (void)c; sb_session_set_cheats_enabled((sb_session *)(uintptr_t)ctx, on == JNI_TRUE); }

JNIEXPORT jboolean JNICALL
Java_io_sameboy_android_NativeBridge_nativeHasAccelerometer(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; return sb_session_has_accelerometer((sb_session *)(uintptr_t)ctx) ? JNI_TRUE : JNI_FALSE; }

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeSetAccelerometer(JNIEnv *env, jclass c, jlong ctx, jdouble x, jdouble y)
{ (void)env; (void)c; sb_session_set_accelerometer((sb_session *)(uintptr_t)ctx, x, y); }

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeDestroy(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; sb_session_destroy((sb_session *)(uintptr_t)ctx); }
