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
# GB_DISABLE_TIMEKEEPING: the blocking audio ring is the only clock. With the Core's own
#   gettimeofday pacer also compiled in, whichever clock ran faster won and the other
#   underran -> periodic click (measured on Pixel 8 Pro). Turbo cap moves to emu_loop.
# NDEBUG: upstream's release CONF sets it; without it GB_CONTEXT_SAFETY and the Core's
#   internal assert()s are live and abort the app instead of being no-ops.
LOCAL_CFLAGS    := -std=gnu11 -DGB_INTERNAL -DGB_DISABLE_DEBUGGER -DGB_DISABLE_TIMEKEEPING \
                   -DNDEBUG -DGB_VERSION=\"$(VERSION)\" -D_GNU_SOURCE \
                   -Wno-multichar -O2 -fvisibility=hidden
LOCAL_LDLIBS    := -landroid -lEGL -lGLESv2 -laaudio -llog
# 16 KB page alignment: Android 15+ / Play require .so LOAD segments 16 KB-aligned.
# NDK r26 (ndkVersion here) still defaults to 4 KB, so request it explicitly.
LOCAL_LDFLAGS   := -Wl,-z,max-page-size=16384
include $(BUILD_SHARED_LIBRARY)
