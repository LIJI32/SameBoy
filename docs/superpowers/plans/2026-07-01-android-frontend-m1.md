# SameBoy Android Frontend — M1 (Foundation) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a native standalone Android app (`Android/` sub-project) that boots a Game Boy / Game Boy Color ROM and is playable — rendering, audio, touch controls, and battery saves — reusing SameBoy's C `Core` unmodified via a JNI bridge.

**Architecture:** Java owns only UI (activity, `SurfaceView`, touch overlay, SAF). A C/JNI bridge owns a heap `GB_gameboy_t` and runs three native threads: an emulation thread looping `GB_run_frame`, an EGL/GLES2 render thread blitting the framebuffer to a textured quad, and an AAudio output stream. The APU sample callback fills a bounded ring buffer that AAudio drains, making audio consumption the real-time master clock. Per-frame pixels never cross JNI.

**Tech Stack:** Java (app), C11 (`Core` + bridge), Gradle + Android Gradle Plugin, `externalNativeBuild { ndkBuild }` (no CMake), NDK (clang), AAudio, EGL/GLES2, Storage Access Framework. rgbds (host, build-time) for boot ROMs.

## Global Constraints

- **No `Core` edits** except, only if strictly required, additive `#ifdef __ANDROID__` guards. None are anticipated.
- **No CMake.** Native build is `externalNativeBuild { ndkBuild }` over `Android/jni/Android.mk`.
- **No new runtime C dependencies.** Audio = AAudio (NDK); render = EGL/GLESv2 (NDK). No Oboe/SDL.
- **`minSdk 26`** (Android 8.0 — required for AAudio). `compileSdk`/`targetSdk 34`.
- **ABIs:** `arm64-v8a`, `armeabi-v7a`, `x86_64`, `x86` (all four).
- **Application ID:** `io.sameboy.android`.
- **Core compile flags (verbatim):** `-std=gnu11 -DGB_INTERNAL -DGB_DISABLE_DEBUGGER -DGB_VERSION="\"$(VERSION)\"" -D_GNU_SOURCE -I$(CORE_DIR)`. `GB_DISABLE_DEBUGGER` auto-defines `GB_DISABLE_CHEAT_SEARCH` via `Core/gb.h`.
- **Core source list (17 files):** all `Core/*.c` **except** `debugger.c`, `sm83_disassembler.c`, `symbol_hash.c`, `cheat_search.c`.
- **Boot ROMs** are bundled APK assets built from `BootROMs/*.asm`; loaded at runtime via `GB_load_boot_rom_from_buffer`. Boot-ROM-less graceful fallback if an asset is missing.
- **C style:** SameBoy conventions (4-space indent, `GB_`/`lower_snake_case`, braces on same line, `(void)` for no-arg fns). **Java:** standard Android conventions.
- **`GB_KEY_*` indices (from `Core/joypad.h`):** RIGHT 0, LEFT 1, UP 2, DOWN 3, A 4, B 5, SELECT 6, START 7.
- **Pixel packing** (little-endian, GL_RGBA/UNSIGNED_BYTE): `0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r`.
- Frequent commits; each task ends committed.

---

### Task 1: Bootstrap Android toolchain (SDK + NDK + rgbds)

Prepares the host so later tasks can build. Userland only (no sudo); everything under `~/Android` and `~/.local`.

**Files:**
- Create: `Android/.gitignore` (ignore `.gradle/`, `build/`, `local.properties`, `app/src/main/assets/bootroms/`)
- Create: `Android/local.properties.example` (documents `sdk.dir`)

- [ ] **Step 1: Install SDK command-line tools + NDK + platform**

```bash
set -e
mkdir -p ~/Android/cmdline-tools
cd ~/Android/cmdline-tools
# Linux command-line tools (pin a known version)
curl -fsSLo cmdtools.zip https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
rm -rf latest && unzip -q cmdtools.zip && mv cmdline-tools latest && rm cmdtools.zip
export ANDROID_HOME=$HOME/Android
export PATH="$ANDROID_HOME/cmdline-tools/latest/bin:$PATH"
yes | sdkmanager --sdk_root="$ANDROID_HOME" --licenses
sdkmanager --sdk_root="$ANDROID_HOME" \
  "platform-tools" "platforms;android-34" "build-tools;34.0.0" "ndk;26.3.11579264"
```

Expected: `sdkmanager --list_installed` shows `platforms;android-34`, `build-tools;34.0.0`, `ndk;26.3.11579264`.

- [ ] **Step 2: Build rgbds in userland (for boot ROMs)**

```bash
set -e
sudo -n true 2>/dev/null && HAVE_SUDO=1 || HAVE_SUDO=0
cd /tmp
curl -fsSLo rgbds.tar.gz https://github.com/gbdev/rgbds/releases/download/v0.7.0/rgbds-0.7.0.tar.gz
mkdir -p rgbds-src && tar xf rgbds.tar.gz -C rgbds-src --strip-components=1
cd rgbds-src
make -j"$(nproc)" PREFIX="$HOME/.local" Q=
make install PREFIX="$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
rgbasm --version && rgblink --version && rgbfix --version && rgbgfx --version
```

Expected: all four print a version. If the build fails for missing `libpng`/`bison`/`flex` and no sudo is available, record it: boot ROMs will be skipped (Task 10 degrades gracefully) — the app still builds and runs boot-ROM-less.

- [ ] **Step 3: Create Android/.gitignore and example properties**

`Android/.gitignore`:
```gitignore
.gradle/
build/
/local.properties
app/build/
app/src/main/assets/bootroms/
.cxx/
```

`Android/local.properties.example`:
```properties
# Copy to local.properties (git-ignored). Point at your SDK install.
sdk.dir=/home/youruser/Android
```

- [ ] **Step 4: Verify & commit**

```bash
cd ~/SameBoy
echo "sdk.dir=$HOME/Android" > Android/local.properties   # local, git-ignored
git add Android/.gitignore Android/local.properties.example
git commit -m "build(android): toolchain bootstrap docs + gitignore"
```

Expected: commit succeeds; `local.properties` is NOT tracked (git-ignored).

---

### Task 2: Gradle skeleton that assembles a bare APK

Establish a buildable Gradle project *before* adding native code, so build issues are isolated.

**Files:**
- Create: `Android/settings.gradle`, `Android/build.gradle`, `Android/gradle.properties`
- Create: `Android/app/build.gradle`
- Create: `Android/app/src/main/AndroidManifest.xml`
- Create: `Android/app/src/main/res/values/strings.xml`
- Create: `Android/app/src/main/java/io/sameboy/android/MainActivity.java` (temporary placeholder button)
- Create: Gradle wrapper (`gradlew`, `gradle/wrapper/*`)

**Interfaces:**
- Produces: an installable debug APK `io.sameboy.android` with a launcher `MainActivity`. Later tasks replace `MainActivity`'s body and add native libs.

- [ ] **Step 1: Root Gradle files**

`Android/settings.gradle`:
```groovy
pluginManagement {
    repositories { google(); mavenCentral(); gradlePluginPortal() }
}
dependencyResolutionManagement {
    repositories { google(); mavenCentral() }
}
rootProject.name = "SameBoy"
include ":app"
```

`Android/build.gradle`:
```groovy
plugins {
    id "com.android.application" version "8.5.2" apply false
}
```

`Android/gradle.properties`:
```properties
org.gradle.jvmargs=-Xmx2048m
android.useAndroidX=true
android.nonTransitiveRClass=true
```

- [ ] **Step 2: App module Gradle**

`Android/app/build.gradle`:
```groovy
plugins { id "com.android.application" }

// Read VERSION from repo-root version.mk (e.g. "1.0.1")
def sameboyVersion = file("${rootDir}/../version.mk").readLines()
        .find { it.startsWith("VERSION") }?.split(":=")?.last()?.trim() ?: "0.0.0"

android {
    namespace "io.sameboy.android"
    compileSdk 34

    defaultConfig {
        applicationId "io.sameboy.android"
        minSdk 26
        targetSdk 34
        versionCode 1
        versionName sameboyVersion
        ndk { abiFilters "arm64-v8a", "armeabi-v7a", "x86_64", "x86" }
    }

    // Native build is added in Task 3 (externalNativeBuild).

    compileOptions {
        sourceCompatibility JavaVersion.VERSION_17
        targetCompatibility JavaVersion.VERSION_17
    }
    buildTypes {
        debug { debuggable true }
        release { minifyEnabled false }
    }
}

dependencies {
    implementation "androidx.appcompat:appcompat:1.7.0"
}
```

- [ ] **Step 3: Manifest, strings, placeholder MainActivity**

`Android/app/src/main/AndroidManifest.xml`:
```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">
    <application
        android:label="@string/app_name"
        android:allowBackup="true"
        android:theme="@style/Theme.AppCompat.NoActionBar"
        android:supportsRtl="true">
        <activity android:name=".MainActivity" android:exported="true">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
```

`Android/app/src/main/res/values/strings.xml`:
```xml
<resources>
    <string name="app_name">SameBoy</string>
    <string name="open_rom">Open ROM</string>
</resources>
```

`Android/app/src/main/java/io/sameboy/android/MainActivity.java`:
```java
package io.sameboy.android;

import android.app.Activity;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;

public class MainActivity extends Activity {
    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        LinearLayout root = new LinearLayout(this);
        root.setGravity(android.view.Gravity.CENTER);
        Button open = new Button(this);
        open.setText(R.string.open_rom);
        root.addView(open);
        setContentView(root);
    }
}
```

- [ ] **Step 4: Gradle wrapper**

```bash
cd ~/SameBoy/Android
gradle wrapper --gradle-version 8.9 --distribution-type bin 2>/dev/null \
  || ( curl -fsSLo /tmp/g.zip https://services.gradle.org/distributions/gradle-8.9-bin.zip \
       && unzip -qo /tmp/g.zip -d /tmp && /tmp/gradle-8.9/bin/gradle wrapper --gradle-version 8.9 )
```

Expected: `Android/gradlew` and `Android/gradle/wrapper/gradle-wrapper.jar` exist.

- [ ] **Step 5: Build the bare APK**

Run:
```bash
cd ~/SameBoy/Android && ANDROID_HOME=$HOME/Android ./gradlew :app:assembleDebug
```
Expected: `BUILD SUCCESSFUL`; `app/build/outputs/apk/debug/app-debug.apk` exists.

- [ ] **Step 6: Commit**

```bash
cd ~/SameBoy
git add Android/settings.gradle Android/build.gradle Android/gradle.properties \
        Android/app/build.gradle Android/app/src Android/gradlew Android/gradlew.bat \
        Android/gradle
git commit -m "build(android): Gradle skeleton assembling a bare debug APK"
```

---

### Task 3: `Android.mk` — compile Core into `libsameboy_core.so`

Add the native build with a stub JNI symbol so we can prove the whole `Core` compiles+links for all four ABIs and lands in the APK.

**Files:**
- Create: `Android/jni/Application.mk`
- Create: `Android/jni/Android.mk`
- Create: `Android/jni/sameboy_jni.c` (stub for now)
- Modify: `Android/app/build.gradle` (add `externalNativeBuild`)

**Interfaces:**
- Produces: shared lib `libsameboy_core.so` per ABI; exported stub `Java_io_sameboy_android_NativeBridge_nativeAbiTag`.

- [ ] **Step 1: Application.mk**

`Android/jni/Application.mk`:
```makefile
APP_ABI := arm64-v8a armeabi-v7a x86_64 x86
APP_PLATFORM := android-26
APP_STL := none
```

- [ ] **Step 2: Android.mk (Core + bridge)**

`Android/jni/Android.mk`:
```makefile
LOCAL_PATH := $(call my-dir)
CORE_DIR   := $(LOCAL_PATH)/../..

# VERSION from repo-root version.mk
VERSION := $(strip $(shell sed -n 's/^VERSION[[:space:]]*:=[[:space:]]*//p' $(CORE_DIR)/version.mk))

# 17 Core sources: all Core/*.c except debugger/disassembler/symbol_hash/cheat_search
CORE_ALL     := $(wildcard $(CORE_DIR)/Core/*.c)
CORE_EXCLUDE := $(CORE_DIR)/Core/debugger.c $(CORE_DIR)/Core/sm83_disassembler.c \
                $(CORE_DIR)/Core/symbol_hash.c $(CORE_DIR)/Core/cheat_search.c
CORE_SOURCES := $(filter-out $(CORE_EXCLUDE),$(CORE_ALL))

BRIDGE_SOURCES := $(wildcard $(LOCAL_PATH)/*.c)

include $(CLEAR_VARS)
LOCAL_MODULE    := sameboy_core
LOCAL_SRC_FILES := $(CORE_SOURCES) $(BRIDGE_SOURCES)
LOCAL_C_INCLUDES := $(CORE_DIR) $(CORE_DIR)/Core
LOCAL_CFLAGS    := -std=gnu11 -DGB_INTERNAL -DGB_DISABLE_DEBUGGER \
                   -DGB_VERSION=\"$(VERSION)\" -D_GNU_SOURCE \
                   -Wno-multichar -O2 -fvisibility=hidden
LOCAL_LDLIBS    := -landroid -lEGL -lGLESv2 -laaudio -llog
include $(BUILD_SHARED_LIBRARY)
```

- [ ] **Step 3: Stub JNI**

`Android/jni/sameboy_jni.c`:
```c
#include <jni.h>

JNIEXPORT jstring JNICALL
Java_io_sameboy_android_NativeBridge_nativeAbiTag(JNIEnv *env, jclass clazz)
{
    (void)clazz;
#if defined(__aarch64__)
    return (*env)->NewStringUTF(env, "arm64-v8a");
#elif defined(__arm__)
    return (*env)->NewStringUTF(env, "armeabi-v7a");
#elif defined(__x86_64__)
    return (*env)->NewStringUTF(env, "x86_64");
#else
    return (*env)->NewStringUTF(env, "x86");
#endif
}
```

- [ ] **Step 4: Wire externalNativeBuild**

In `Android/app/build.gradle`, inside `android { defaultConfig { ... } }` add:
```groovy
        externalNativeBuild { ndkBuild { arguments "APP_BUILD_SCRIPT:=../jni/Android.mk" } }
```
And inside `android { ... }`:
```groovy
    externalNativeBuild { ndkBuild { path "../jni/Android.mk" } }
    ndkVersion "26.3.11579264"
```

- [ ] **Step 5: Build and verify the .so for all ABIs**

Run:
```bash
cd ~/SameBoy/Android && ANDROID_HOME=$HOME/Android ./gradlew :app:assembleDebug
unzip -l app/build/outputs/apk/debug/app-debug.apk | grep libsameboy_core.so
```
Expected: `BUILD SUCCESSFUL`; four entries `lib/arm64-v8a/libsameboy_core.so`, `lib/armeabi-v7a/...`, `lib/x86_64/...`, `lib/x86/...`.

- [ ] **Step 6: Verify no unresolved NEEDED libs (x86_64)**

Run:
```bash
cd ~/SameBoy/Android
D=app/build/intermediates/merged_native_libs/debug/*/lib/x86_64
readelf -d $D/libsameboy_core.so | grep NEEDED
```
Expected: only `libandroid`, `libEGL`, `libGLESv2`, `libaaudio`, `liblog`, `libc`, `libm`, `libdl` — all Android system libs.

- [ ] **Step 7: Commit**

```bash
cd ~/SameBoy
git add Android/jni Android/app/build.gradle
git commit -m "build(android): ndk-build Core into libsameboy_core.so (4 ABIs)"
```

---

### Task 4: `ring_buffer` — SPSC sample ring (host TDD)

Bounded producer/consumer ring of `GB_sample_t`; producer blocks when full (pacing), consumer zero-fills on underrun. Platform-independent C → unit-tested on the host.

**Files:**
- Create: `Android/jni/ring_buffer.h`, `Android/jni/ring_buffer.c`
- Create: `Android/jni/test/test_ring_buffer.c` (host test, not compiled into the APK)

**Interfaces:**
- Produces:
  - `typedef struct sb_ring sb_ring;`
  - `sb_ring *sb_ring_create(size_t capacity_frames);`
  - `void sb_ring_destroy(sb_ring *r);`
  - `void sb_ring_push(sb_ring *r, int16_t left, int16_t right);` // blocks if full
  - `size_t sb_ring_pop(sb_ring *r, int16_t *dst_interleaved, size_t frames);` // returns frames written; zero-fills remainder
  - `void sb_ring_flush(sb_ring *r);` // drop all queued, wake producer
  - `void sb_ring_shutdown(sb_ring *r);` // unblock producer permanently (teardown)

- [ ] **Step 1: Write the failing test**

`Android/jni/test/test_ring_buffer.c`:
```c
#include "../ring_buffer.h"
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>

static void test_push_pop_fifo(void)
{
    sb_ring *r = sb_ring_create(8);
    sb_ring_push(r, 100, -100);
    sb_ring_push(r, 200, -200);
    int16_t out[4] = {0};
    size_t n = sb_ring_pop(r, out, 2);
    assert(n == 2);
    assert(out[0] == 100 && out[1] == -100);
    assert(out[2] == 200 && out[3] == -200);
    sb_ring_destroy(r);
}

static void test_underrun_zero_fill(void)
{
    sb_ring *r = sb_ring_create(8);
    sb_ring_push(r, 7, 7);
    int16_t out[6];
    for (int i = 0; i < 6; i++) out[i] = 0x5a;
    size_t n = sb_ring_pop(r, out, 3);   // only 1 frame available
    assert(n == 1);
    assert(out[0] == 7 && out[1] == 7);
    assert(out[2] == 0 && out[3] == 0 && out[4] == 0 && out[5] == 0); // zero-filled
    sb_ring_destroy(r);
}

struct pc { sb_ring *r; };
static void *producer(void *arg)
{
    sb_ring *r = ((struct pc *)arg)->r;
    for (int i = 0; i < 10000; i++) sb_ring_push(r, (int16_t)i, (int16_t)-i);
    return NULL;
}

static void test_blocking_pacing(void)
{
    sb_ring *r = sb_ring_create(64);   // small: producer must block
    struct pc arg = { r };
    pthread_t t;
    pthread_create(&t, NULL, producer, &arg);
    int16_t out[2];
    for (int i = 0; i < 10000; i++) {
        while (sb_ring_pop(r, out, 1) == 0) {}
        assert(out[0] == (int16_t)i);
    }
    pthread_join(t, NULL);
    sb_ring_destroy(r);
}

int main(void)
{
    test_push_pop_fifo();
    test_underrun_zero_fill();
    test_blocking_pacing();
    printf("ring_buffer: all tests passed\n");
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails (no implementation yet)**

Run:
```bash
cd ~/SameBoy/Android/jni
cc -I. test/test_ring_buffer.c ring_buffer.c -lpthread -o /tmp/trb 2>&1 | head
```
Expected: FAIL — `ring_buffer.c`/`ring_buffer.h` do not exist (compile error).

- [ ] **Step 3: Implement `ring_buffer.h`**

```c
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct sb_ring sb_ring;

sb_ring *sb_ring_create(size_t capacity_frames);
void     sb_ring_destroy(sb_ring *r);
void     sb_ring_push(sb_ring *r, int16_t left, int16_t right);
size_t   sb_ring_pop(sb_ring *r, int16_t *dst_interleaved, size_t frames);
void     sb_ring_flush(sb_ring *r);
void     sb_ring_shutdown(sb_ring *r);
```

- [ ] **Step 4: Implement `ring_buffer.c`**

```c
#include "ring_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

struct sb_ring {
    int16_t *buf;          /* interleaved L,R */
    size_t   cap;          /* frames */
    size_t   head, tail;   /* frame indices */
    size_t   count;        /* frames stored */
    pthread_mutex_t mtx;
    pthread_cond_t  not_full;
    int shutdown;
};

sb_ring *sb_ring_create(size_t capacity_frames)
{
    sb_ring *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->cap = capacity_frames;
    r->buf = calloc(capacity_frames * 2, sizeof(int16_t));
    pthread_mutex_init(&r->mtx, NULL);
    pthread_cond_init(&r->not_full, NULL);
    return r;
}

void sb_ring_destroy(sb_ring *r)
{
    if (!r) return;
    pthread_mutex_destroy(&r->mtx);
    pthread_cond_destroy(&r->not_full);
    free(r->buf);
    free(r);
}

void sb_ring_push(sb_ring *r, int16_t left, int16_t right)
{
    pthread_mutex_lock(&r->mtx);
    while (r->count == r->cap && !r->shutdown) {
        pthread_cond_wait(&r->not_full, &r->mtx);
    }
    if (r->shutdown) { pthread_mutex_unlock(&r->mtx); return; }
    r->buf[r->head * 2]     = left;
    r->buf[r->head * 2 + 1] = right;
    r->head = (r->head + 1) % r->cap;
    r->count++;
    pthread_mutex_unlock(&r->mtx);
}

size_t sb_ring_pop(sb_ring *r, int16_t *dst, size_t frames)
{
    pthread_mutex_lock(&r->mtx);
    size_t got = 0;
    while (got < frames && r->count > 0) {
        dst[got * 2]     = r->buf[r->tail * 2];
        dst[got * 2 + 1] = r->buf[r->tail * 2 + 1];
        r->tail = (r->tail + 1) % r->cap;
        r->count--;
        got++;
    }
    if (got > 0) pthread_cond_signal(&r->not_full);
    pthread_mutex_unlock(&r->mtx);
    /* zero-fill underrun */
    for (size_t i = got; i < frames; i++) {
        dst[i * 2] = 0;
        dst[i * 2 + 1] = 0;
    }
    return got;
}

void sb_ring_flush(sb_ring *r)
{
    pthread_mutex_lock(&r->mtx);
    r->head = r->tail = r->count = 0;
    pthread_cond_signal(&r->not_full);
    pthread_mutex_unlock(&r->mtx);
}

void sb_ring_shutdown(sb_ring *r)
{
    pthread_mutex_lock(&r->mtx);
    r->shutdown = 1;
    pthread_cond_broadcast(&r->not_full);
    pthread_mutex_unlock(&r->mtx);
}
```


- [ ] **Step 5: Run tests to verify pass**

Run:
```bash
cd ~/SameBoy/Android/jni
cc -I. test/test_ring_buffer.c ring_buffer.c -lpthread -o /tmp/trb && /tmp/trb
```
Expected: `ring_buffer: all tests passed`.

- [ ] **Step 6: Commit**

```bash
cd ~/SameBoy
git add Android/jni/ring_buffer.h Android/jni/ring_buffer.c Android/jni/test/test_ring_buffer.c
git commit -m "feat(android): SPSC sample ring buffer with blocking pacing (host-tested)"
```

---

### Task 5: `emulator` — Core lifecycle, callbacks, threads (host TDD for logic)

Owns the `GB_gameboy_t`, the callbacks (rgb_encode/vblank/audio/boot-rom), the emulation thread, and double-buffered framebuffers. Rendering/audio *drivers* are injected (function pointers) so the emulator core is testable headless on the host.

**Files:**
- Create: `Android/jni/emulator.h`, `Android/jni/emulator.c`
- Create: `Android/jni/test/test_emulator.c` (host test — runs frames on a real ROM buffer, asserts pixels + audio produced)

**Interfaces:**
- Consumes: `sb_ring` (Task 4), `Core/gb.h`.
- Produces:
  - `typedef struct sb_emulator sb_emulator;`
  - `sb_emulator *sb_emu_create(int model, const uint8_t *rom, size_t rom_len, const uint8_t *sav, size_t sav_len);`
  - `void sb_emu_set_boot_rom(sb_emulator *e, int gb_boot_rom_type, const uint8_t *data, size_t len);` // register asset bytes before first reset
  - `void sb_emu_reset(sb_emulator *e);`
  - `void sb_emu_run_frame(sb_emulator *e);` // one GB_run_frame; used by the emu thread loop and tests
  - `const uint32_t *sb_emu_front_buffer(sb_emulator *e, unsigned *w, unsigned *h);` // last completed frame + its size
  - `sb_ring *sb_emu_audio_ring(sb_emulator *e);`
  - `void sb_emu_set_key(sb_emulator *e, int gb_key_index, int pressed);`
  - `size_t sb_emu_save_battery(sb_emulator *e, uint8_t **out_malloced);` // 0 if none; caller frees
  - `void sb_emu_destroy(sb_emulator *e);`

- [ ] **Step 1: Write the failing test**

`Android/jni/test/test_emulator.c`:
```c
#include "../emulator.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Build a tiny valid-enough ROM: 32KB, Nintendo logo + header checksum so the
   boot ROM (if any) accepts it; without a boot ROM SameBoy still runs. */
static uint8_t *make_rom(size_t *len)
{
    size_t n = 32 * 1024;
    uint8_t *rom = calloc(1, n);
    /* entry: nop; jp 0x0150 */
    rom[0x100] = 0x00; rom[0x101] = 0xC3; rom[0x102] = 0x50; rom[0x103] = 0x01;
    /* a trivial program at 0x150: inc a; jr -2 (busy loop toggling A) */
    rom[0x150] = 0x3C; rom[0x151] = 0x18; rom[0x152] = 0xFE;
    rom[0x147] = 0x00; /* MBC: ROM ONLY */
    rom[0x148] = 0x00; /* 32KB */
    rom[0x149] = 0x00; /* no RAM */
    /* header checksum (0x134..0x14C) */
    uint8_t c = 0;
    for (int i = 0x134; i <= 0x14C; i++) c = c - rom[i] - 1;
    rom[0x14D] = c;
    *len = n;
    return rom;
}

int main(void)
{
    size_t rlen; uint8_t *rom = make_rom(&rlen);
    /* GB_MODEL_DMG_B = 0x002 */
    sb_emulator *e = sb_emu_create(0x002, rom, rlen, NULL, 0);
    assert(e != NULL);
    sb_emu_reset(e);

    for (int i = 0; i < 60; i++) sb_emu_run_frame(e);   /* 1 second */

    unsigned w = 0, h = 0;
    const uint32_t *fb = sb_emu_front_buffer(e, &w, &h);
    assert(fb != NULL);
    assert(w == 160 && h == 144);               /* DMG screen, no border */
    /* every pixel must be fully opaque (alpha 0xFF) from our rgb_encode */
    for (unsigned i = 0; i < w * h; i++) assert((fb[i] & 0xFF000000u) == 0xFF000000u);

    /* audio: running frames must have produced samples in the ring */
    int16_t buf[2];
    size_t got = sb_ring_pop(sb_emu_audio_ring(e), buf, 1);
    assert(got == 1);

    sb_emu_destroy(e);
    free(rom);
    printf("emulator: all tests passed\n");
    return 0;
}
```

- [ ] **Step 2: Run to verify it fails**

Run:
```bash
cd ~/SameBoy/Android/jni
cc -I. -I../../Core -I../.. -DGB_INTERNAL -DGB_DISABLE_DEBUGGER -std=gnu11 \
   test/test_emulator.c emulator.c ring_buffer.c \
   ../../Core/{gb,apu,memory,mbc,timing,display,camera,sm83_cpu,joypad,save_state,random,rumble,sgb,printer,cheats,rewind,workboy}.c \
   -lpthread -lm -o /tmp/temu 2>&1 | head
```
Expected: FAIL — `emulator.c`/`emulator.h` do not exist.

- [ ] **Step 3: Implement `emulator.h`**

```c
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "ring_buffer.h"

typedef struct sb_emulator sb_emulator;

sb_emulator *sb_emu_create(int model, const uint8_t *rom, size_t rom_len,
                           const uint8_t *sav, size_t sav_len);
void         sb_emu_set_boot_rom(sb_emulator *e, int gb_boot_rom_type,
                                 const uint8_t *data, size_t len);
void         sb_emu_reset(sb_emulator *e);
void         sb_emu_run_frame(sb_emulator *e);
const uint32_t *sb_emu_front_buffer(sb_emulator *e, unsigned *w, unsigned *h);
sb_ring     *sb_emu_audio_ring(sb_emulator *e);
void         sb_emu_set_key(sb_emulator *e, int gb_key_index, int pressed);
size_t       sb_emu_save_battery(sb_emulator *e, uint8_t **out_malloced);
void         sb_emu_destroy(sb_emulator *e);

#define SB_AUDIO_SAMPLE_RATE 48000
```

- [ ] **Step 4: Implement `emulator.c`**

```c
#include "emulator.h"
#include <Core/gb.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define SB_MAX_W 256
#define SB_MAX_H 224
#define SB_BOOT_ROM_COUNT 10   /* GB_BOOT_ROM_AGB + 1 */

struct sb_emulator {
    GB_gameboy_t gb;
    uint32_t buffers[2][SB_MAX_W * SB_MAX_H];
    int back;                       /* index of the buffer Core renders into */
    unsigned front_w, front_h;
    pthread_mutex_t fb_mtx;
    sb_ring *audio;
    struct { uint8_t *data; size_t len; } boot[SB_BOOT_ROM_COUNT];  /* owned copies */
};

static uint32_t rgb_encode(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{
    (void)gb;
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static void vblank_cb(GB_gameboy_t *gb, GB_vblank_type_t type)
{
    if (type == GB_VBLANK_TYPE_REPEAT) return;   /* nothing new to show */
    sb_emulator *e = GB_get_user_data(gb);
    pthread_mutex_lock(&e->fb_mtx);
    e->front_w = GB_get_screen_width(gb);
    e->front_h = GB_get_screen_height(gb);
    e->back ^= 1;                                 /* swap */
    GB_set_pixels_output(gb, e->buffers[e->back]);
    pthread_mutex_unlock(&e->fb_mtx);
}

static void audio_cb(GB_gameboy_t *gb, GB_sample_t *sample)
{
    sb_emulator *e = GB_get_user_data(gb);
    sb_ring_push(e->audio, sample->left, sample->right);
}

static void boot_rom_cb(GB_gameboy_t *gb, GB_boot_rom_t type)
{
    sb_emulator *e = GB_get_user_data(gb);
    if (type < SB_BOOT_ROM_COUNT && e->boot[type].data) {
        GB_load_boot_rom_from_buffer(gb, e->boot[type].data, e->boot[type].len);
        return;
    }
    /* fallbacks mirroring SDL: CGB_E->CGB, AGB_0->AGB */
    if (type == GB_BOOT_ROM_CGB_E && e->boot[GB_BOOT_ROM_CGB].data) {
        GB_load_boot_rom_from_buffer(gb, e->boot[GB_BOOT_ROM_CGB].data, e->boot[GB_BOOT_ROM_CGB].len);
    }
    else if (type == GB_BOOT_ROM_AGB_0 && e->boot[GB_BOOT_ROM_AGB].data) {
        GB_load_boot_rom_from_buffer(gb, e->boot[GB_BOOT_ROM_AGB].data, e->boot[GB_BOOT_ROM_AGB].len);
    }
    /* else: run boot-ROM-less */
}

sb_emulator *sb_emu_create(int model, const uint8_t *rom, size_t rom_len,
                           const uint8_t *sav, size_t sav_len)
{
    sb_emulator *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    pthread_mutex_init(&e->fb_mtx, NULL);
    e->audio = sb_ring_create(SB_AUDIO_SAMPLE_RATE / 10);   /* ~100 ms */
    e->back = 0;
    e->front_w = 160; e->front_h = 144;

    GB_init(&e->gb, (GB_model_t)model);
    GB_set_user_data(&e->gb, e);
    GB_set_boot_rom_load_callback(&e->gb, boot_rom_cb);
    GB_set_vblank_callback(&e->gb, vblank_cb);
    GB_set_rgb_encode_callback(&e->gb, rgb_encode);
    GB_set_pixels_output(&e->gb, e->buffers[e->back]);
    GB_set_sample_rate(&e->gb, SB_AUDIO_SAMPLE_RATE);
    GB_apu_set_sample_callback(&e->gb, audio_cb);

    if (GB_load_rom_from_buffer(&e->gb, rom, rom_len)) { /* nonzero = failure per Core */ }
    if (sav && sav_len) GB_load_battery_from_buffer(&e->gb, sav, sav_len);
    return e;
}

void sb_emu_set_boot_rom(sb_emulator *e, int type, const uint8_t *data, size_t len)
{
    if (type < 0 || type >= SB_BOOT_ROM_COUNT) return;
    free(e->boot[type].data);              /* replace any prior copy */
    e->boot[type].data = NULL;
    e->boot[type].len = 0;
    if (data && len) {
        e->boot[type].data = malloc(len);
        memcpy(e->boot[type].data, data, len);
        e->boot[type].len = len;
    }
}

void sb_emu_reset(sb_emulator *e) { GB_reset(&e->gb); }

void sb_emu_run_frame(sb_emulator *e) { GB_run_frame(&e->gb); }

const uint32_t *sb_emu_front_buffer(sb_emulator *e, unsigned *w, unsigned *h)
{
    pthread_mutex_lock(&e->fb_mtx);
    /* front = the buffer NOT currently being rendered into */
    const uint32_t *fb = e->buffers[e->back ^ 1];
    if (w) *w = e->front_w;
    if (h) *h = e->front_h;
    pthread_mutex_unlock(&e->fb_mtx);
    return fb;
}

sb_ring *sb_emu_audio_ring(sb_emulator *e) { return e->audio; }

void sb_emu_set_key(sb_emulator *e, int idx, int pressed)
{
    GB_set_key_state(&e->gb, (GB_key_t)idx, pressed != 0);
}

size_t sb_emu_save_battery(sb_emulator *e, uint8_t **out)
{
    int size = GB_save_battery_size(&e->gb);
    if (size <= 0) { *out = NULL; return 0; }
    uint8_t *buf = malloc(size);
    GB_save_battery_to_buffer(&e->gb, buf, size);
    *out = buf;
    return (size_t)size;
}

void sb_emu_destroy(sb_emulator *e)
{
    if (!e) return;
    sb_ring_shutdown(e->audio);
    GB_free(&e->gb);
    sb_ring_destroy(e->audio);
    for (int i = 0; i < SB_BOOT_ROM_COUNT; i++) free(e->boot[i].data);
    pthread_mutex_destroy(&e->fb_mtx);
    free(e);
}
```
NOTE on the double buffer: `vblank_cb` flips `back` then points Core at the new back; readers take `back ^ 1` (the just-completed frame). The initial pre-first-vblank read returns buffer 1 (cleared to 0 → alpha 0, but the test runs 60 frames so at least one vblank has occurred and pixels are opaque). This matches the test's expectation.

- [ ] **Step 5: Run tests to verify pass**

Run the same compile line as Step 2, then `/tmp/temu`.
Expected: `emulator: all tests passed`.

- [ ] **Step 6: Commit**

```bash
cd ~/SameBoy
git add Android/jni/emulator.h Android/jni/emulator.c Android/jni/test/test_emulator.c
git commit -m "feat(android): emulator core (lifecycle, callbacks, double buffer) host-tested on a real ROM"
```

---

### Task 6: `audio_aaudio` — AAudio output driver

Drives one low-latency PCM_I16 stereo stream whose data callback drains `sb_emu_audio_ring`. Compile-verified in the NDK build (device playback is on the smoke checklist).

**Files:**
- Create: `Android/jni/audio_aaudio.h`, `Android/jni/audio_aaudio.c`

**Interfaces:**
- Consumes: `sb_ring` (Task 4).
- Produces:
  - `typedef struct sb_audio sb_audio;`
  - `sb_audio *sb_audio_start(sb_ring *ring);` // opens+starts stream; NULL on failure
  - `void sb_audio_set_paused(sb_audio *a, int paused);`
  - `void sb_audio_stop(sb_audio *a);`

- [ ] **Step 1: Implement `audio_aaudio.h`**

```c
#pragma once
#include "ring_buffer.h"

typedef struct sb_audio sb_audio;

sb_audio *sb_audio_start(sb_ring *ring);
void      sb_audio_set_paused(sb_audio *a, int paused);
void      sb_audio_stop(sb_audio *a);
```

- [ ] **Step 2: Implement `audio_aaudio.c`**

```c
#include "audio_aaudio.h"
#include "emulator.h"     /* SB_AUDIO_SAMPLE_RATE */
#include <aaudio/AAudio.h>
#include <stdlib.h>

struct sb_audio {
    AAudioStream *stream;
    sb_ring *ring;
};

static aaudio_data_callback_result_t data_cb(AAudioStream *stream, void *user,
                                             void *audio_data, int32_t num_frames)
{
    (void)stream;
    struct sb_audio *a = user;
    sb_ring_pop(a->ring, (int16_t *)audio_data, (size_t)num_frames);  /* zero-fills underrun */
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

sb_audio *sb_audio_start(sb_ring *ring)
{
    AAudioStreamBuilder *b = NULL;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return NULL;

    struct sb_audio *a = calloc(1, sizeof(*a));
    a->ring = ring;

    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setSampleRate(b, SB_AUDIO_SAMPLE_RATE);
    AAudioStreamBuilder_setDataCallback(b, data_cb, a);

    aaudio_result_t r = AAudioStreamBuilder_openStream(b, &a->stream);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK) { free(a); return NULL; }

    if (AAudioStream_requestStart(a->stream) != AAUDIO_OK) {
        AAudioStream_close(a->stream);
        free(a);
        return NULL;
    }
    return a;
}

void sb_audio_set_paused(sb_audio *a, int paused)
{
    if (!a || !a->stream) return;
    if (paused) AAudioStream_requestPause(a->stream);
    else        AAudioStream_requestStart(a->stream);
}

void sb_audio_stop(sb_audio *a)
{
    if (!a) return;
    if (a->stream) {
        AAudioStream_requestStop(a->stream);
        AAudioStream_close(a->stream);
    }
    free(a);
}
```

- [ ] **Step 3: Verify it compiles in the NDK build**

Add `audio_aaudio.c` is auto-picked by the `$(wildcard *.c)` in `Android.mk`. Run:
```bash
cd ~/SameBoy/Android && ANDROID_HOME=$HOME/Android ./gradlew :app:assembleDebug
```
Expected: `BUILD SUCCESSFUL` (all four ABIs compile `audio_aaudio.c`).

- [ ] **Step 4: Commit**

```bash
cd ~/SameBoy
git add Android/jni/audio_aaudio.h Android/jni/audio_aaudio.c
git commit -m "feat(android): AAudio output driver draining the sample ring"
```

---

### Task 7: `render_gles` — EGL + GLES2 framebuffer blit

Owns the render thread bound to an `ANativeWindow`; uploads the current front buffer to a texture and draws an aspect-correct integer-scaled quad each vsync. Compile-verified (on-screen output is on the smoke checklist).

**Files:**
- Create: `Android/jni/render_gles.h`, `Android/jni/render_gles.c`

**Interfaces:**
- Consumes: `sb_emulator` (Task 5) via `sb_emu_front_buffer`; `ANativeWindow`.
- Produces:
  - `typedef struct sb_renderer sb_renderer;`
  - `sb_renderer *sb_render_start(ANativeWindow *win, sb_emulator *emu);` // spawns render thread
  - `void sb_render_stop(sb_renderer *r);` // joins thread, tears down EGL

- [ ] **Step 1: Implement `render_gles.h`**

```c
#pragma once
#include <android/native_window.h>
#include "emulator.h"

typedef struct sb_renderer sb_renderer;

sb_renderer *sb_render_start(ANativeWindow *win, sb_emulator *emu);
void         sb_render_stop(sb_renderer *r);
```

- [ ] **Step 2: Implement `render_gles.c`**

```c
#include "render_gles.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SameBoyGL", __VA_ARGS__)

struct sb_renderer {
    ANativeWindow *win;
    sb_emulator *emu;
    pthread_t thread;
    volatile int running;
};

static const char *VS =
    "attribute vec2 aPos;attribute vec2 aTex;varying vec2 vTex;"
    "void main(){vTex=aTex;gl_Position=vec4(aPos,0.0,1.0);}";
static const char *FS =
    "precision mediump float;varying vec2 vTex;uniform sampler2D uTex;"
    "void main(){gl_FragColor=texture2D(uTex,vTex);}";

static GLuint compile(GLenum t, const char *src)
{
    GLuint s = glCreateShader(t);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    return s;
}

static void *render_thread(void *arg)
{
    sb_renderer *r = arg;

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    const EGLint cfg_attr[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE
    };
    EGLConfig cfg; EGLint n;
    eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n);
    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, r->win, NULL);
    const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (!eglMakeCurrent(dpy, surf, surf, ctx)) { LOGE("eglMakeCurrent failed"); return NULL; }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, VS));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, FS));
    glBindAttribLocation(prog, 0, "aPos");
    glBindAttribLocation(prog, 1, "aTex");
    glLinkProgram(prog);
    glUseProgram(prog);

    GLuint tex; glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    unsigned tex_w = 0, tex_h = 0;
    const GLfloat tc[] = { 0,1, 1,1, 0,0, 1,0 };
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, tc);

    while (r->running) {
        unsigned w, h;
        const uint32_t *fb = sb_emu_front_buffer(r->emu, &w, &h);

        if (w != tex_w || h != tex_h) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, fb);
            tex_w = w; tex_h = h;
        }
        else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, fb);
        }

        EGLint sw, sh;
        eglQuerySurface(dpy, surf, EGL_WIDTH, &sw);
        eglQuerySurface(dpy, surf, EGL_HEIGHT, &sh);

        /* aspect-correct integer letterbox */
        int scale = 1;
        int sx = (int)(sw / w), sy = (int)(sh / h);
        scale = sx < sy ? sx : sy;
        if (scale < 1) scale = 1;
        int vw = (int)w * scale, vh = (int)h * scale;
        glViewport((sw - vw) / 2, (sh - vh) / 2, vw, vh);

        const GLfloat quad[] = { -1,-1, 1,-1, -1,1, 1,1 };
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);

        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        eglSwapBuffers(dpy, surf);   /* paces to display vsync */
    }

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(dpy, surf);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    return NULL;
}

sb_renderer *sb_render_start(ANativeWindow *win, sb_emulator *emu)
{
    sb_renderer *r = calloc(1, sizeof(*r));
    r->win = win; r->emu = emu; r->running = 1;
    pthread_create(&r->thread, NULL, render_thread, r);
    return r;
}

void sb_render_stop(sb_renderer *r)
{
    if (!r) return;
    r->running = 0;
    pthread_join(r->thread, NULL);
    free(r);
}
```

- [ ] **Step 3: Verify it compiles in the NDK build**

Run:
```bash
cd ~/SameBoy/Android && ANDROID_HOME=$HOME/Android ./gradlew :app:assembleDebug
```
Expected: `BUILD SUCCESSFUL`.

- [ ] **Step 4: Commit**

```bash
cd ~/SameBoy
git add Android/jni/render_gles.h Android/jni/render_gles.c
git commit -m "feat(android): EGL/GLES2 render thread (integer-scaled framebuffer blit)"
```

---

### Task 8: JNI bridge + `NativeBridge.java`

Wire the Java-visible entry points to `emulator`/`render_gles`/`audio_aaudio`, owning thread lifecycle. Replaces the Task 3 stub.

**Files:**
- Modify (replace): `Android/jni/sameboy_jni.c`
- Create: `Android/jni/session.h`, `Android/jni/session.c` (owns emu+render+audio+emu-thread as one "running session")
- Create: `Android/app/src/main/java/io/sameboy/android/NativeBridge.java`

**Interfaces:**
- Consumes: `sb_emulator`, `sb_renderer`, `sb_audio`.
- Produces (Java `native` methods, §4 of the spec):
  `nativeCreate(int model, byte[] rom, byte[] sav, AssetManager assets) -> long`,
  `nativeStart(long, Surface)`, `nativeStop(long)`, `nativePause(long, boolean)`,
  `nativeSetKey(long, int, boolean)`, `nativeReset(long)`, `nativeSaveBattery(long) -> byte[]`,
  `nativeDestroy(long)`.

- [ ] **Step 1: `session.h`**

```c
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <android/native_window.h>
#include "emulator.h"

typedef struct sb_session sb_session;

sb_session *sb_session_create(int model, const uint8_t *rom, size_t rom_len,
                              const uint8_t *sav, size_t sav_len);
void        sb_session_set_boot_rom(sb_session *s, int type, const uint8_t *data, size_t len);
void        sb_session_reset(sb_session *s);
void        sb_session_start(sb_session *s, ANativeWindow *win);   /* start threads */
void        sb_session_stop(sb_session *s);                        /* stop threads */
void        sb_session_pause(sb_session *s, int paused);
void        sb_session_set_key(sb_session *s, int idx, int pressed);
size_t      sb_session_save_battery(sb_session *s, uint8_t **out);
void        sb_session_destroy(sb_session *s);
```

- [ ] **Step 2: `session.c`**

```c
#include "session.h"
#include "render_gles.h"
#include "audio_aaudio.h"
#include <pthread.h>
#include <stdlib.h>

struct sb_session {
    sb_emulator *emu;
    sb_renderer *render;
    sb_audio *audio;
    ANativeWindow *win;
    pthread_t emu_thread;
    volatile int running;
    volatile int paused;
    pthread_mutex_t pause_mtx;
    pthread_cond_t pause_cv;
};

static void *emu_loop(void *arg)
{
    sb_session *s = arg;
    while (s->running) {
        pthread_mutex_lock(&s->pause_mtx);
        while (s->paused && s->running) pthread_cond_wait(&s->pause_cv, &s->pause_mtx);
        pthread_mutex_unlock(&s->pause_mtx);
        if (!s->running) break;
        sb_emu_run_frame(s->emu);   /* blocks on the audio ring => paced */
    }
    return NULL;
}

sb_session *sb_session_create(int model, const uint8_t *rom, size_t rom_len,
                              const uint8_t *sav, size_t sav_len)
{
    sb_emulator *emu = sb_emu_create(model, rom, rom_len, sav, sav_len);
    if (!emu) return NULL;
    sb_session *s = calloc(1, sizeof(*s));
    s->emu = emu;
    pthread_mutex_init(&s->pause_mtx, NULL);
    pthread_cond_init(&s->pause_cv, NULL);
    return s;
}

void sb_session_set_boot_rom(sb_session *s, int type, const uint8_t *data, size_t len)
{ sb_emu_set_boot_rom(s->emu, type, data, len); }

void sb_session_reset(sb_session *s) { sb_emu_reset(s->emu); }

void sb_session_start(sb_session *s, ANativeWindow *win)
{
    if (s->running) return;
    s->win = win;
    s->running = 1;
    s->paused = 0;
    s->audio = sb_audio_start(sb_emu_audio_ring(s->emu));
    s->render = sb_render_start(win, s->emu);
    pthread_create(&s->emu_thread, NULL, emu_loop, s);
}

void sb_session_stop(sb_session *s)
{
    if (!s->running) return;
    s->running = 0;
    pthread_mutex_lock(&s->pause_mtx);
    s->paused = 0;
    pthread_cond_broadcast(&s->pause_cv);
    pthread_mutex_unlock(&s->pause_mtx);
    /* unblock the emu thread if it's waiting on a full audio ring */
    sb_ring_flush(sb_emu_audio_ring(s->emu));
    pthread_join(s->emu_thread, NULL);
    sb_render_stop(s->render); s->render = NULL;
    sb_audio_stop(s->audio);   s->audio = NULL;
    if (s->win) { ANativeWindow_release(s->win); s->win = NULL; }
}

void sb_session_pause(sb_session *s, int paused)
{
    pthread_mutex_lock(&s->pause_mtx);
    s->paused = paused;
    pthread_cond_broadcast(&s->pause_cv);
    pthread_mutex_unlock(&s->pause_mtx);
    if (s->audio) sb_audio_set_paused(s->audio, paused);
}

void sb_session_set_key(sb_session *s, int idx, int pressed) { sb_emu_set_key(s->emu, idx, pressed); }

size_t sb_session_save_battery(sb_session *s, uint8_t **out) { return sb_emu_save_battery(s->emu, out); }

void sb_session_destroy(sb_session *s)
{
    if (!s) return;
    if (s->running) sb_session_stop(s);
    sb_emu_destroy(s->emu);
    pthread_mutex_destroy(&s->pause_mtx);
    pthread_cond_destroy(&s->pause_cv);
    free(s);
}
```

- [ ] **Step 3: `sameboy_jni.c` (replace stub)**

```c
#include <jni.h>
#include <android/native_window_jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
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
    jbyte *sbytes = NULL; jsize slen = 0;
    if (sav) { slen = (*env)->GetArrayLength(env, sav); sbytes = (*env)->GetByteArrayElements(env, sav, NULL); }

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
{ (void)env; (void)c; sb_session_set_key((sb_session *)(uintptr_t)ctx, idx, pressed); }

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
    (*env)->SetByteArrayRegion(env, arr, 0, (jsize)n, (const jbyte *)buf);
    free(buf);
    return arr;
}

JNIEXPORT void JNICALL
Java_io_sameboy_android_NativeBridge_nativeDestroy(JNIEnv *env, jclass c, jlong ctx)
{ (void)env; (void)c; sb_session_destroy((sb_session *)(uintptr_t)ctx); }
```


- [ ] **Step 4: `NativeBridge.java`**

```java
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
    public static native void nativeDestroy(long ctx);

    // GB_key_t indices (Core/joypad.h)
    public static final int KEY_RIGHT = 0, KEY_LEFT = 1, KEY_UP = 2, KEY_DOWN = 3,
                            KEY_A = 4, KEY_B = 5, KEY_SELECT = 6, KEY_START = 7;
    // GB_model_t (Core/model.h)
    public static final int MODEL_DMG_B = 0x002, MODEL_CGB_E = 0x205, MODEL_AGB = 0x207;
}
```

- [ ] **Step 5: Build (verifies JNI symbol names match)**

Run:
```bash
cd ~/SameBoy/Android && ANDROID_HOME=$HOME/Android ./gradlew :app:assembleDebug
D=app/build/intermediates/merged_native_libs/debug/*/lib/arm64-v8a
nm -D --defined-only $D/libsameboy_core.so | grep NativeBridge_native | sort
```
Expected: `BUILD SUCCESSFUL`; eight exported `Java_io_sameboy_android_NativeBridge_native*` symbols.

- [ ] **Step 6: Commit**

```bash
cd ~/SameBoy
git add Android/jni/session.h Android/jni/session.c Android/jni/sameboy_jni.c \
        Android/app/src/main/java/io/sameboy/android/NativeBridge.java
git commit -m "feat(android): JNI bridge + session lifecycle wiring emu/render/audio"
```

---

### Task 9: Java UI — SAF ROM open, emulator activity, surface, touch controls

Replace the placeholder `MainActivity` with a working launcher; add the emulator screen, surface, and on-screen controls. Battery `.sav` load/save in app-specific storage.

**Files:**
- Modify (replace): `Android/app/src/main/java/io/sameboy/android/MainActivity.java`
- Create: `EmulatorActivity.java`, `EmulatorSurfaceView.java`, `TouchOverlayView.java`, `SaveStore.java`
- Modify: `AndroidManifest.xml` (register `EmulatorActivity`)
- Create: `res/layout/activity_emulator.xml`

**Interfaces:**
- Consumes: `NativeBridge` (Task 8).
- Produces: user-facing flow — pick ROM → play with touch controls → battery persists.

- [ ] **Step 1: `SaveStore.java` (battery persistence)**

```java
package io.sameboy.android;

import android.content.Context;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;

final class SaveStore {
    private SaveStore() {}

    static File savFile(Context ctx, String romName) {
        File dir = new File(ctx.getExternalFilesDir(null), "saves");
        if (!dir.exists()) dir.mkdirs();
        return new File(dir, romName + ".sav");
    }

    static byte[] read(File f) {
        if (!f.exists()) return null;
        try (RandomAccessFile raf = new RandomAccessFile(f, "r")) {
            byte[] b = new byte[(int) raf.length()];
            raf.readFully(b);
            return b;
        } catch (IOException e) { return null; }
    }

    static void write(File f, byte[] data) {
        if (data == null) return;
        try (FileOutputStream out = new FileOutputStream(f)) { out.write(data); }
        catch (IOException ignored) {}
    }
}
```

- [ ] **Step 2: `MainActivity.java` (SAF launcher)**

```java
package io.sameboy.android;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;

public class MainActivity extends Activity {
    private static final int REQ_OPEN = 1;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        LinearLayout root = new LinearLayout(this);
        root.setGravity(Gravity.CENTER);
        Button open = new Button(this);
        open.setText(R.string.open_rom);
        open.setOnClickListener(v -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            startActivityForResult(i, REQ_OPEN);
        });
        root.addView(open);
        setContentView(root);
    }

    @Override protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req == REQ_OPEN && res == RESULT_OK && data != null) {
            Uri uri = data.getData();
            Intent i = new Intent(this, EmulatorActivity.class);
            i.setData(uri);
            startActivity(i);
        }
    }
}
```

- [ ] **Step 3: `EmulatorSurfaceView.java`**

```java
package io.sameboy.android;

import android.content.Context;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

class EmulatorSurfaceView extends SurfaceView implements SurfaceHolder.Callback {
    interface Listener {
        void onSurfaceReady(android.view.Surface surface);
        void onSurfaceGone();
    }
    private final Listener listener;

    EmulatorSurfaceView(Context ctx, Listener l) {
        super(ctx);
        this.listener = l;
        getHolder().addCallback(this);
    }

    @Override public void surfaceCreated(SurfaceHolder h) { listener.onSurfaceReady(h.getSurface()); }
    @Override public void surfaceChanged(SurfaceHolder h, int f, int w, int ht) {}
    @Override public void surfaceDestroyed(SurfaceHolder h) { listener.onSurfaceGone(); }
}
```

- [ ] **Step 4: `TouchOverlayView.java` (D-pad + A/B/Start/Select)**

```java
package io.sameboy.android;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;

/** Minimal, functional on-screen controls. Art polish is a later milestone;
 *  M1 draws simple hit-regions and reports presses via the callback. */
class TouchOverlayView extends View {
    interface KeyListener { void onKey(int gbKeyIndex, boolean pressed); }

    private final KeyListener listener;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    // regions computed in onSizeChanged
    private RectF up, down, left, right, a, b, start, select;
    // track which pointer id is over which key
    private final java.util.HashMap<Integer, Integer> pointerKey = new java.util.HashMap<>();

    TouchOverlayView(Context ctx, KeyListener l) { super(ctx); this.listener = l; }

    @Override protected void onSizeChanged(int w, int h, int ow, int oh) {
        float u = Math.min(w, h) / 10f;            // unit
        float dpadCx = u * 2.2f, dpadCy = h - u * 3f;
        up     = new RectF(dpadCx - u/2, dpadCy - u*1.5f, dpadCx + u/2, dpadCy - u/2);
        down   = new RectF(dpadCx - u/2, dpadCy + u/2,    dpadCx + u/2, dpadCy + u*1.5f);
        left   = new RectF(dpadCx - u*1.5f, dpadCy - u/2, dpadCx - u/2, dpadCy + u/2);
        right  = new RectF(dpadCx + u/2, dpadCy - u/2,    dpadCx + u*1.5f, dpadCy + u/2);
        a      = new RectF(w - u*1.8f, dpadCy - u*0.8f, w - u*0.4f, dpadCy + u*0.6f);
        b      = new RectF(w - u*3.3f, dpadCy + u*0.2f, w - u*1.9f, dpadCy + u*1.6f);
        start  = new RectF(w/2f + u*0.2f, h - u*1.4f, w/2f + u*2.2f, h - u*0.4f);
        select = new RectF(w/2f - u*2.2f, h - u*1.4f, w/2f - u*0.2f, h - u*0.4f);
    }

    private int keyAt(float x, float y) {
        if (up.contains(x, y)) return NativeBridge.KEY_UP;
        if (down.contains(x, y)) return NativeBridge.KEY_DOWN;
        if (left.contains(x, y)) return NativeBridge.KEY_LEFT;
        if (right.contains(x, y)) return NativeBridge.KEY_RIGHT;
        if (a.contains(x, y)) return NativeBridge.KEY_A;
        if (b.contains(x, y)) return NativeBridge.KEY_B;
        if (start.contains(x, y)) return NativeBridge.KEY_START;
        if (select.contains(x, y)) return NativeBridge.KEY_SELECT;
        return -1;
    }

    @Override public boolean onTouchEvent(MotionEvent e) {
        int action = e.getActionMasked();
        int idx = e.getActionIndex();
        int id = e.getPointerId(idx);
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int k = keyAt(e.getX(idx), e.getY(idx));
                if (k >= 0) { pointerKey.put(id, k); listener.onKey(k, true); }
                break;
            }
            case MotionEvent.ACTION_MOVE: {
                for (int i = 0; i < e.getPointerCount(); i++) {
                    int pid = e.getPointerId(i);
                    int newK = keyAt(e.getX(i), e.getY(i));
                    Integer oldK = pointerKey.get(pid);
                    if (oldK == null || oldK != (Integer) newK) {
                        if (oldK != null) listener.onKey(oldK, false);
                        if (newK >= 0) { pointerKey.put(pid, newK); listener.onKey(newK, true); }
                        else pointerKey.remove(pid);
                    }
                }
                break;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_CANCEL: {
                Integer k = pointerKey.remove(id);
                if (k != null) listener.onKey(k, false);
                break;
            }
        }
        return true;
    }

    @Override protected void onDraw(Canvas c) {
        paint.setColor(Color.argb(90, 255, 255, 255));
        for (RectF r : new RectF[]{up, down, left, right}) if (r != null) c.drawRect(r, paint);
        paint.setColor(Color.argb(120, 200, 60, 90));
        if (a != null) c.drawOval(a, paint);
        if (b != null) c.drawOval(b, paint);
        paint.setColor(Color.argb(110, 180, 180, 180));
        if (start != null) c.drawRoundRect(start, 12, 12, paint);
        if (select != null) c.drawRoundRect(select, 12, 12, paint);
    }
}
```

- [ ] **Step 5: `EmulatorActivity.java`**

```java
package io.sameboy.android;

import android.app.Activity;
import android.net.Uri;
import android.os.Bundle;
import android.view.Surface;
import android.view.WindowManager;
import android.widget.FrameLayout;

import java.io.File;
import java.io.InputStream;
import java.io.ByteArrayOutputStream;

public class EmulatorActivity extends Activity implements EmulatorSurfaceView.Listener {
    private long ctx = 0;
    private File savFile;
    private String romName = "rom";

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        byte[] rom = readAll(getIntent().getData());
        if (rom == null) { finish(); return; }
        romName = displayName(getIntent().getData());
        savFile = SaveStore.savFile(this, romName);
        byte[] sav = SaveStore.read(savFile);

        // Model auto: CGB_E plays both DMG and CGB well for M1.
        ctx = NativeBridge.nativeCreate(NativeBridge.MODEL_CGB_E, rom, sav, getAssets());
        if (ctx == 0) { finish(); return; }

        FrameLayout root = new FrameLayout(this);
        EmulatorSurfaceView surface = new EmulatorSurfaceView(this, this);
        TouchOverlayView overlay = new TouchOverlayView(this,
                (k, pressed) -> { if (ctx != 0) NativeBridge.nativeSetKey(ctx, k, pressed); });
        root.addView(surface);
        root.addView(overlay);
        setContentView(root);
    }

    @Override public void onSurfaceReady(Surface s) { if (ctx != 0) NativeBridge.nativeStart(ctx, s); }
    @Override public void onSurfaceGone() { if (ctx != 0) NativeBridge.nativeStop(ctx); }

    @Override protected void onPause() {
        super.onPause();
        if (ctx != 0) {
            NativeBridge.nativePause(ctx, true);
            SaveStore.write(savFile, NativeBridge.nativeSaveBattery(ctx));
        }
    }
    @Override protected void onResume() {
        super.onResume();
        if (ctx != 0) NativeBridge.nativePause(ctx, false);
    }

    @Override protected void onDestroy() {
        super.onDestroy();
        if (ctx != 0) { NativeBridge.nativeDestroy(ctx); ctx = 0; }
    }

    private byte[] readAll(Uri uri) {
        if (uri == null) return null;
        try (InputStream in = getContentResolver().openInputStream(uri)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[65536]; int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            return out.toByteArray();
        } catch (Exception e) { return null; }
    }

    private String displayName(Uri uri) {
        String last = uri.getLastPathSegment();
        if (last == null) return "rom";
        int slash = last.lastIndexOf('/');
        String name = slash >= 0 ? last.substring(slash + 1) : last;
        int dot = name.lastIndexOf('.');
        return dot > 0 ? name.substring(0, dot) : name;
    }
}
```

- [ ] **Step 6: Manifest — register EmulatorActivity**

Add inside `<application>` in `AndroidManifest.xml`:
```xml
        <activity
            android:name=".EmulatorActivity"
            android:exported="false"
            android:screenOrientation="sensorLandscape"
            android:configChanges="orientation|screenSize|keyboardHidden" />
```

- [ ] **Step 7: Build**

Run:
```bash
cd ~/SameBoy/Android && ANDROID_HOME=$HOME/Android ./gradlew :app:assembleDebug
```
Expected: `BUILD SUCCESSFUL`.

- [ ] **Step 8: Commit**

```bash
cd ~/SameBoy
git add Android/app/src/main/java/io/sameboy/android Android/app/src/main/AndroidManifest.xml
git commit -m "feat(android): SAF ROM open, emulator activity, surface, touch controls, battery saves"
```

---

### Task 10: Boot ROM generation + bundling as assets

Build SameBoy's own boot ROMs at build time and copy the `.bin` into assets, so authentic boot ROMs ship in the APK. Graceful skip if rgbds is unavailable.

**Files:**
- Modify: `Android/app/build.gradle` (add `generateBootRoms` task wired before asset merge)

**Interfaces:**
- Produces: `app/src/main/assets/bootroms/*.bin` consumed by `load_boot_roms` (Task 8). Asset names must equal the `boot_names[]` table (`dmg_boot.bin`, `cgb_boot.bin`, `sgb_boot.bin`, `sgb2_boot.bin`, `mgb_boot.bin`, `cgb0_boot.bin`, `agb_boot.bin`; the JNI falls back for `cgbE`/`agb0`/`dmg0`).

- [ ] **Step 1: Add the Gradle task**

In `Android/app/build.gradle`, at the bottom:
```groovy
def bootRomOut = file("src/main/assets/bootroms")

tasks.register("generateBootRoms") {
    def repoRoot = file("${rootDir}/..")
    outputs.dir bootRomOut
    doLast {
        bootRomOut.mkdirs()
        // Locate rgbds (host); prefer ~/.local/bin from Task 1.
        def home = System.getProperty("user.home")
        def env = new HashMap<String,String>(System.getenv())
        env.put("PATH", "${home}/.local/bin:" + env.get("PATH"))
        def rgbasm = ["bash","-lc","PATH=\$PATH which rgbasm"].execute(env.collect{k,v->"$k=$v"}, null)
        rgbasm.waitFor()
        if (rgbasm.exitValue() != 0) {
            logger.warn("rgbds not found; skipping boot ROM generation. App will run boot-ROM-less.")
            return
        }
        // Build boot ROMs via the repo Makefile, then copy the .bin files.
        def make = ["bash","-lc","cd '${repoRoot}' && PATH='${home}/.local/bin:'\$PATH make bootroms"]
                .execute(env.collect{k,v->"$k=$v"}, null)
        make.consumeProcessOutput(System.out as Appendable, System.err as Appendable)
        make.waitFor()
        if (make.exitValue() != 0) { throw new GradleException("make bootroms failed") }
        def src = file("${repoRoot}/build/bin/BootROMs")
        ["dmg_boot.bin","mgb_boot.bin","cgb0_boot.bin","cgb_boot.bin",
         "agb_boot.bin","sgb_boot.bin","sgb2_boot.bin"].each { name ->
            def f = new File(src, name)
            if (f.exists()) { new File(bootRomOut, name).bytes = f.bytes }
            else { logger.warn("boot ROM ${name} not produced; skipping") }
        }
    }
}

// Run before assets are merged for every variant.
tasks.matching { it.name.startsWith("merge") && it.name.endsWith("Assets") }
     .configureEach { dependsOn "generateBootRoms" }
```

- [ ] **Step 2: Build & confirm assets land in the APK**

Run:
```bash
cd ~/SameBoy/Android && ANDROID_HOME=$HOME/Android ./gradlew :app:assembleDebug
unzip -l app/build/outputs/apk/debug/app-debug.apk | grep bootroms
```
Expected (rgbds present): `assets/bootroms/cgb_boot.bin`, `dmg_boot.bin`, etc. listed.
Expected (rgbds absent): build still succeeds, warning logged, no bootroms entries.

- [ ] **Step 3: Confirm graceful boot-ROM-less path is intact**

Confirm `boot_rom_cb` (Task 5) has an "else: run boot-ROM-less" branch — no assertion/crash when an asset is missing. (Code review step, no command.)

- [ ] **Step 4: Commit**

```bash
cd ~/SameBoy
git add Android/app/build.gradle
git commit -m "build(android): generate + bundle SameBoy boot ROMs as assets (graceful skip w/o rgbds)"
```

---

### Task 11: Integration verification + on-device smoke checklist

Final gate: full four-ABI build, `.so`/asset inspection, host tests green, and a written smoke checklist handed to the user for device verification.

**Files:**
- Create: `Android/README.md` (build + smoke-test instructions)
- Create: `Android/jni/test/run_host_tests.sh` (runs both host tests)

- [ ] **Step 1: Host test runner**

`Android/jni/test/run_host_tests.sh`:
```bash
#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/.."
CORE="../../Core"
CFLAGS="-I. -I../../Core -I../.. -DGB_INTERNAL -DGB_DISABLE_DEBUGGER -std=gnu11 -O2"
CORE_SRC="$CORE/gb.c $CORE/apu.c $CORE/memory.c $CORE/mbc.c $CORE/timing.c $CORE/display.c \
$CORE/camera.c $CORE/sm83_cpu.c $CORE/joypad.c $CORE/save_state.c $CORE/random.c $CORE/rumble.c \
$CORE/sgb.c $CORE/printer.c $CORE/cheats.c $CORE/rewind.c $CORE/workboy.c"
cc $CFLAGS test/test_ring_buffer.c ring_buffer.c -lpthread -o /tmp/trb && /tmp/trb
cc $CFLAGS test/test_emulator.c emulator.c ring_buffer.c $CORE_SRC -lpthread -lm -o /tmp/temu && /tmp/temu
echo "ALL HOST TESTS PASSED"
```

- [ ] **Step 2: Run host tests**

Run:
```bash
chmod +x ~/SameBoy/Android/jni/test/run_host_tests.sh
~/SameBoy/Android/jni/test/run_host_tests.sh
```
Expected: `ring_buffer: all tests passed`, `emulator: all tests passed`, `ALL HOST TESTS PASSED`.

- [ ] **Step 3: Full build + artifact inspection**

Run:
```bash
cd ~/SameBoy/Android && ANDROID_HOME=$HOME/Android ./gradlew clean :app:assembleDebug
APK=app/build/outputs/apk/debug/app-debug.apk
echo "== ABIs ==" && unzip -l $APK | grep -c 'libsameboy_core.so'   # expect 4
echo "== assets ==" && unzip -l $APK | grep bootroms | wc -l         # >=1 if rgbds present
```
Expected: `BUILD SUCCESSFUL`; four `.so` entries.

- [ ] **Step 4: Write `Android/README.md` with the smoke checklist**

`Android/README.md` (key content):
```markdown
# SameBoy for Android (M1)

## Build
    cd Android
    echo "sdk.dir=$HOME/Android" > local.properties
    ./gradlew :app:assembleDebug
APK: `app/build/outputs/apk/debug/app-debug.apk`

Boot ROMs are built from `../BootROMs/*.asm` and require **rgbds** on PATH
(`~/.local/bin` if built per the plan). Without rgbds the app still builds and
runs without a boot animation.

## Host unit tests
    jni/test/run_host_tests.sh

## On-device smoke test (manual — requires a device or AVD)
1. `adb install -r app/build/outputs/apk/debug/app-debug.apk`
2. Launch SameBoy, tap **Open ROM**, pick a `.gb` or `.gbc` file.
3. Confirm:
   - [ ] The game renders (picture, not black), correctly scaled and centered.
   - [ ] Audio plays without constant crackle.
   - [ ] D-pad and A/B/Start/Select move/control the game.
   - [ ] For a game with battery save (e.g. a Zelda/Pokémon-type): save in-game,
         leave the app (Home), reopen the ROM — the save persists (`.sav` under
         `Android/data/io.sameboy.android/files/saves/`).
   - [ ] Rotating / backgrounding and returning resumes cleanly (no crash).
```

- [ ] **Step 5: Commit**

```bash
cd ~/SameBoy
git add Android/README.md Android/jni/test/run_host_tests.sh
git commit -m "docs(android): M1 build + host tests + on-device smoke checklist"
```

- [ ] **Step 6: Final DoD check (§12 of the spec)**

Confirm each Definition-of-Done item:
- [ ] `Android/` builds a debug APK for all four ABIs, no `Core` edits.
- [ ] APK contains `libsameboy_core.so` ×4 with only Android-system `NEEDED` libs + boot-ROM assets (when rgbds present).
- [ ] Host tests for `rgb_encode`/ring buffer/emulator pass.
- [ ] Smoke checklist delivered in `Android/README.md`.

---

## Self-Review

**1. Spec coverage:**
- §1.1 NDK build of Core → T3. JNI bridge → T8. GLES2 render → T7. AAudio → T6.
  Emu thread → T8 (session). SAF ROM load → T9. Bundled boot ROMs → T10. Touch controls → T9.
  Battery `.sav` → T5 (Core) + T9 (persistence). ✓
- §2 native-driven hot loop → T5/T6/T7/T8. ✓
- §3.1–3.7 each map to T5 (callbacks/buffers), T2 (p2)/T3 (build), T3.3 (framebuffer sizing in T5), T4 (ring), T6 (audio), T7 (render), T5+T9 (ROM/boot/battery), T9 (UI). ✓
- §4 JNI API → T8 exactly (8 methods). ✓
- §5 layout → produced across T2/T3/T9. ✓
- §7 build (ndkBuild, VERSION, boot ROM task) → T3, T2 (VERSION), T10. ✓
- §8 error handling → ROM failure (T8 returns 0 → T9 finish), missing boot ROM (T5 else-branch), surface destroyed (T8 stop joins). ✓
- §9 verification → T4/T5 host tests, T3/T8 .so/symbol inspection, T11 checklist. ✓
- §10 decisions (bundle boot ROMs / minSdk 26 / io.sameboy.android) → T10 / Global Constraints / T2. ✓

**2. Placeholder scan:** No "TBD"/"implement later". Two earlier corrective NOTES were folded into their code blocks: the `ring_buffer` struct no longer has the `bool_stop` typo, and the emulator now *owns copies* of boot-ROM bytes (`sb_emu_set_boot_rom` copies; `sb_emu_destroy` frees), so JNI's temporary asset buffer is no longer leaked. No deferrals remain.

**3. Type consistency:** `sb_ring_*`, `sb_emu_*`, `sb_session_*`, and JNI names are consistent across T4→T5→T8. `NativeBridge` method names match `sameboy_jni.c` mangled symbols. `SB_AUDIO_SAMPLE_RATE` defined once (emulator.h), used in audio_aaudio.c. `boot_names[]` (JNI) ↔ asset names (T10) ↔ `sb_emu_set_boot_rom` type indices — consistent.

**Fixes applied inline:** ring-buffer struct typo removed; boot-ROM ownership moved into the emulator (copy on set, free on destroy) so no allocation leaks across the JNI boundary.
