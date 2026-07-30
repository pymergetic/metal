#if defined(PM_METAL_BOOT_TARGET_BIOS)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_EFI)
#error "boot/bios/time.c built with PM_METAL_BOOT_TARGET_EFI"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

#include <stdint.h>

#include <pymergetic/metal/boot/platform/time.h>

#include "io.h"

#define TSC_CAL_SAMPLES 8u

static void cpu_pause(void)
{
  __asm__ volatile("pause");
}

static uint64_t rdtsc(void)
{
  uint32_t lo;
  uint32_t hi;

  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static void pit_delay_ms(uint32_t ms)
{
  /* Channel 2, mode 0; gate via port 0x61. Bounded — OUT2 may stick. */
  while (ms--) {
    uint32_t count = 1193u; /* ~1ms at 1.193182 MHz */
    uint32_t spins = 0;

    pm_metal_bios_outb(0x61u, (uint8_t)((pm_metal_bios_inb(0x61u) & ~0x02u) | 0x01u));
    pm_metal_bios_outb(0x43u, 0xB0u);
    pm_metal_bios_outb(0x42u, (uint8_t)(count & 0xffu));
    pm_metal_bios_outb(0x42u, (uint8_t)(count >> 8));
    while ((pm_metal_bios_inb(0x61u) & 0x20u) == 0u) {
      if (++spins > 2000000u) {
        return;
      }
      cpu_pause();
    }
  }
}

static void bios_invalidate(void) {}

static uint64_t bios_tsc_per_us(void)
{
  uint64_t sum = 0;
  uint32_t ok = 0;
  uint32_t i;

  for (i = 0; i < TSC_CAL_SAMPLES; i++) {
    uint64_t a = rdtsc();
    pit_delay_ms(1u);
    uint64_t b = rdtsc();
    if (b > a) {
      sum += (b - a) / 1000u;
      ok++;
    }
  }
  if (ok == 0u) {
    return 0;
  }
  return sum / (uint64_t)ok;
}

static const pm_metal_boot_time_ops_t g_ops = {
  .tsc_per_us = bios_tsc_per_us,
  .invalidate = bios_invalidate,
};

const pm_metal_boot_time_ops_t *pm_metal_boot_time_ops(void)
{
  return &g_ops;
}
