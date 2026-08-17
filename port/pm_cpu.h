/* Firmware CPU ticks / pause — x86 rdtsc vs ARMv7 CNTPCT. */
#ifndef PM_METAL_PORT_PM_CPU_H
#define PM_METAL_PORT_PM_CPU_H

#include <stdint.h>

static inline __attribute__((unused)) void pm_cpu_pause(void) {
#if defined(__i386__) || defined(__x86_64__)
    __asm__ volatile("pause");
#elif defined(__aarch64__)
    __asm__ volatile("yield");
#elif defined(__arm__)
    __asm__ volatile("yield");
#else
    __asm__ volatile("" ::: "memory");
#endif
}

static inline __attribute__((unused)) void pm_cpu_load_fence(void) {
#if defined(__i386__) || defined(__x86_64__)
    __asm__ volatile("lfence" ::: "memory");
#else
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
#endif
}

static inline __attribute__((unused)) void pm_cpu_store_fence(void) {
#if defined(__i386__) || defined(__x86_64__)
    __asm__ volatile("sfence" ::: "memory");
#else
    __atomic_thread_fence(__ATOMIC_RELEASE);
#endif
}

static inline __attribute__((unused)) uint64_t pm_cpu_ticks(void) {
#if defined(__i386__) || defined(__x86_64__)
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#elif defined(__arm__) && !defined(__aarch64__)
    uint32_t lo;
    uint32_t hi;
    /* CNTPCT — U-Boot leaves the generic timer running on RV1106. */
    __asm__ volatile("mrrc p15, 0, %0, %1, c14" : "=r"(lo), "=r"(hi));
    return ((uint64_t)hi << 32) | lo;
#else
    static uint64_t s;
    return ++s;
#endif
}

static inline __attribute__((unused)) uint64_t pm_cpu_mono_us(void) {
#if defined(__arm__) && !defined(__aarch64__)
    uint32_t hz;
    uint64_t t;
    __asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(hz));
    if (hz == 0u) {
        hz = 24000000u;
    }
    t = pm_cpu_ticks();
    return (t * 1000000ull) / (uint64_t)hz;
#else
    return pm_cpu_ticks() / 2000ull;
#endif
}

#endif /* PM_METAL_PORT_PM_CPU_H */
