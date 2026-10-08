#pragma once
/* Host-test shim for <android/native_window.h> */
typedef struct ANativeWindow ANativeWindow;
void ANativeWindow_release(ANativeWindow *win);
