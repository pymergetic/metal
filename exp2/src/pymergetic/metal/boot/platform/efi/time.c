#if defined(PM_METAL_BOOT_TARGET_EFI)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_BIOS)
#error "boot/efi/time.c built with PM_METAL_BOOT_TARGET_BIOS"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

#include <stdint.h>

#include <Uefi.h>

#include <pymergetic/metal/boot/platform/time.h>

#include "efi_ctx.h"

#define TSC_CAL_SAMPLES 8u

static uint64_t g_tsc_per_us_cached;
static int32_t g_force_remeasure;

static uint64_t rdtsc(void)
{
  uint32_t lo;
  uint32_t hi;

  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static void efi_invalidate(void)
{
  g_force_remeasure = 1;
}

static uint64_t efi_tsc_per_us(void)
{
  uint64_t sum;
  uint32_t i;
  uint32_t ok;
  EFI_BOOT_SERVICES *bs;

  if (g_tsc_per_us_cached != 0 && g_force_remeasure == 0) {
    return g_tsc_per_us_cached;
  }

  bs = NULL;
  if (g_pm_efi_bs_alive && g_pm_efi_st != NULL) {
    bs = g_pm_efi_st->BootServices;
  }
  if (bs == NULL) {
    /* Post-EBS: Stall gone; keep last sample or ~2 GHz fallback. */
    g_force_remeasure = 0;
    if (g_tsc_per_us_cached != 0) {
      return g_tsc_per_us_cached;
    }
    return 2000;
  }

  sum = 0;
  ok = 0;
  for (i = 0; i < TSC_CAL_SAMPLES; i++) {
    uint64_t a = rdtsc();
    (void)bs->Stall(1000); /* 1 ms */
    uint64_t b = rdtsc();
    if (b > a) {
      sum += (b - a) / 1000u;
      ok++;
    }
  }

  g_force_remeasure = 0;
  if (ok == 0u) {
    if (g_tsc_per_us_cached != 0) {
      return g_tsc_per_us_cached;
    }
    return 2000;
  }
  g_tsc_per_us_cached = sum / (uint64_t)ok;
  return g_tsc_per_us_cached;
}

static const pm_metal_boot_time_ops_t g_ops = {
  .tsc_per_us = efi_tsc_per_us,
  .invalidate = efi_invalidate,
};

const pm_metal_boot_time_ops_t *pm_metal_boot_time_ops(void)
{
  return &g_ops;
}
