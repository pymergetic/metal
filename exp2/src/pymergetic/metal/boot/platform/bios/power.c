#if defined(PM_METAL_BOOT_TARGET_BIOS)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_EFI)
#error "boot/bios/power.c built with PM_METAL_BOOT_TARGET_EFI"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

#include <stdint.h>

#include <pymergetic/metal/boot/platform/power.h>

#include "io.h"

static void bios_halt(void)
{
  for (;;) {
    __asm__ volatile("hlt");
  }
}

static void bios_reset(int32_t reboot)
{
  uint32_t spins;

  if (reboot == 0) {
    /* QEMU isa-debug-exit when present; then fall through to halt. */
    pm_metal_bios_outw(0x501u, 0u);
    bios_halt();
  }
  /* Pulse CPU reset via keyboard controller. */
  for (spins = 0; spins < 100000u; spins++) {
    if ((pm_metal_bios_inb(0x64u) & 0x02u) == 0u) {
      break;
    }
  }
  pm_metal_bios_outb(0x64u, 0xFEu);
  bios_halt();
}

static const pm_metal_boot_power_ops_t g_ops = {
  .halt = bios_halt,
  .reset = bios_reset,
};

const pm_metal_boot_power_ops_t *pm_metal_boot_power_ops(void)
{
  return &g_ops;
}
