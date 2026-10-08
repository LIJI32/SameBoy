#pragma once
/* Host-test shim for <android/log.h> */
#define ANDROID_LOG_WARN 5
static inline int __android_log_print(int prio, const char *tag, const char *fmt, ...)
{
    (void)prio; (void)tag; (void)fmt;
    return 0;
}
