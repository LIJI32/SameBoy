#pragma once

/* Realtime-thread scheduling hints for the emulation + render threads.
 *
 * Android's energy-aware scheduler parks default-priority app threads on a
 * little core and lets it drop to low frequency between the emu's bursty
 * per-frame wakeups. On a heterogeneous SoC (e.g. Pixel 8 Pro / Tensor G3)
 * that makes the audio-clocked emu loop wake late — underrunning the audio
 * ring (crackle) and missing the frame deadline (visual hitch) at once. On a
 * homogeneous SoC (all cores identical, e.g. Moto G4) there is no penalty and
 * these hints are a no-op.
 *
 * Both actions are best-effort: failures (EPERM, no cpufreq sysfs, single
 * core) are ignored. Call once from the thread you want boosted.
 *
 * `nice_prio` follows android.os.Process constants, e.g. -19
 * (THREAD_PRIORITY_URGENT_AUDIO) for the emu/audio producer, -4
 * (THREAD_PRIORITY_DISPLAY) for the render thread.
 */
void sb_sched_boost_current_thread(int nice_prio);
