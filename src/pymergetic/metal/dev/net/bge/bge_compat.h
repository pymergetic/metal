#ifndef PM_METAL_BGE_COMPAT_H_
#define PM_METAL_BGE_COMPAT_H_
#include <stddef.h>
#include <stdint.h>
#include "pymergetic/metal/async/time.h"
#include "pymergetic/metal/mem.h"
static inline void pm_metal_mem_fence(void) { __asm__ volatile("mfence" ::: "memory"); }
static inline void pm_metal_cpu_pause(void) { __asm__ volatile("pause"); }
static inline void pm_metal_time_usleep(uint64_t us) {
  uint64_t t0 = pm_metal_time_mono_us();
  while (pm_metal_time_mono_us() - t0 < us) { pm_metal_cpu_pause(); }
}
#endif
