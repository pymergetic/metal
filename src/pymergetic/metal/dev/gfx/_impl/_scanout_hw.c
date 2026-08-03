/*
 * Shared HW-scanout helpers used by i915 / radeon ports.
 */
#include "_scanout_hw.h"

void pm_metal_mem_fence(void)
{
  __asm__ volatile("mfence" ::: "memory");
}

void pm_metal_cpu_pause(void)
{
  __asm__ volatile("pause" ::: "memory");
}
