#!/usr/bin/env bash
# Host-side unit tests for the SameBoy Android native layer (no device needed).
# Compiles + runs the platform-independent C (ring buffer + emulator-on-a-real-ROM)
# on the build host. Core is compiled with the same feature flags as the NDK build.
set -e
cd "$(dirname "$0")/.."   # -> Android/jni

CORE=../../Core
# Match the NDK build's Core config: internal API, debugger/cheat-search disabled, Core
# timekeeping off (the audio ring paces; see Android.mk), version string defined
# (Core/save_state.c hard-errors without GB_VERSION), multichar constants silenced.
# NDEBUG is deliberately NOT set here: the tests use assert(), and the Core's context-safety
# asserts then also police our thread contracts on the host.
CFLAGS="-I. -I../../Core -I../.. -DGB_INTERNAL -DGB_DISABLE_DEBUGGER -DGB_DISABLE_TIMEKEEPING -DGB_VERSION=\"\\\"host-test\\\"\" -std=gnu11 -O2 -Wno-multichar"

CORE_SRC="$CORE/gb.c $CORE/apu.c $CORE/memory.c $CORE/mbc.c $CORE/timing.c $CORE/display.c \
$CORE/camera.c $CORE/sm83_cpu.c $CORE/joypad.c $CORE/save_state.c $CORE/random.c $CORE/rumble.c \
$CORE/sgb.c $CORE/printer.c $CORE/cheats.c $CORE/rewind.c $CORE/workboy.c"

echo "== ring_buffer =="
eval cc $CFLAGS test/test_ring_buffer.c ring_buffer.c -lpthread -o /tmp/sb_trb
/tmp/sb_trb

echo "== emulator =="
eval cc $CFLAGS test/test_emulator.c emulator.c link.c ring_buffer.c $CORE_SRC -lpthread -lm -o /tmp/sb_temu
/tmp/sb_temu

echo "== session =="
eval cc -Itest/shim $CFLAGS test/test_session.c session.c emulator.c link.c ring_buffer.c sched_hint.c test/shim_stubs.c $CORE_SRC -lpthread -lm -o /tmp/sb_tses
/tmp/sb_tses

echo "== link =="
eval cc $CFLAGS test/test_link.c link.c emulator.c ring_buffer.c $CORE_SRC -lpthread -lm -o /tmp/sb_tlink
/tmp/sb_tlink

echo "ALL HOST TESTS PASSED"
