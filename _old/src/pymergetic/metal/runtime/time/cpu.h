/** @file
  x86 CpuPause()/rdtsc — plain compiler intrinsics, not EDK2 API, so no
  port-split needed (unlike genuine firmware calls, these are the same
  two instructions on every x86/x86_64 target this repo builds for).

  Internal implementation header; nothing outside this runtime's own
  .c files should include it.
**/
#ifndef PM_METAL_RUNTIME_TIME_CPU_H
#define PM_METAL_RUNTIME_TIME_CPU_H

#include <stdint.h>

static inline void pm_metal_cpu_pause(void)
{
  __asm__ volatile("pause");
}

static inline uint64_t pm_metal_cpu_rdtsc(void)
{
  uint32_t lo;
  uint32_t hi;

  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)hi << 32) | lo;
}

#endif /* PM_METAL_RUNTIME_TIME_CPU_H */
