#ifndef _GNU_SOURCE
#define _GNU_SOURCE   /* CPU_SET / sched_setaffinity (must precede <sched.h>) */
#endif
#include "sched_hint.h"

#include <sched.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/syscall.h>

/* Read cpuN's cpufreq max frequency (kHz); 0 if unavailable. */
static long cpu_max_freq(int cpu)
{
    char path[128];
    snprintf(path, sizeof(path),
             "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
    FILE *f = fopen(path, "re");
    if (!f) return 0;
    long khz = 0;
    if (fscanf(f, "%ld", &khz) != 1) khz = 0;
    fclose(f);
    return khz;
}

/* Pin the calling thread off the slowest core tier: allow every core whose max
 * frequency is strictly above the minimum tier. No-op when all cores share one
 * frequency (homogeneous SoC) or the topology can't be read. */
static void bias_off_little_cores(void)
{
    long n = sysconf(_SC_NPROCESSORS_CONF);
    if (n <= 1 || n > CPU_SETSIZE) return;

    long freq[CPU_SETSIZE];
    long slowest = 0;
    long fastest = 0;
    for (int i = 0; i < n; i++) {
        freq[i] = cpu_max_freq(i);
        if (freq[i] <= 0) return;                 /* incomplete data → don't guess */
        if (slowest == 0 || freq[i] < slowest) slowest = freq[i];
        if (freq[i] > fastest) fastest = freq[i];
    }
    if (fastest == slowest) return;               /* homogeneous → nothing to bias */

    cpu_set_t set;
    CPU_ZERO(&set);
    int allowed = 0;
    for (int i = 0; i < n; i++) {
        if (freq[i] > slowest) { CPU_SET(i, &set); allowed++; }
    }
    if (allowed > 0) {
        /* gettid()-targeted; the cgroup cpuset still bounds this when backgrounded. */
        sched_setaffinity((pid_t)syscall(SYS_gettid), sizeof(set), &set);
    }
}

void sb_sched_boost_current_thread(int nice_prio)
{
    /* Raise priority so the scheduler runs this thread promptly and treats it
     * as heavy enough to place on a faster core. Best-effort (may EPERM). */
    setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), nice_prio);
    bias_off_little_cores();
}
