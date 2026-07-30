#if defined(PM_METAL_BOOT_TARGET_EFI)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_BIOS)
#error "boot/efi/power.c built with PM_METAL_BOOT_TARGET_BIOS"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

#include <stdint.h>

#include <pymergetic/metal/boot/platform/power.h>

static void efi_halt(void)
{
  for (;;) {
    __asm__ volatile("hlt");
  }
}

static void efi_reset(int32_t reboot)
{
  (void)reboot;
  efi_halt();
}

static const pm_metal_boot_power_ops_t g_ops = {
  .halt = efi_halt,
  .reset = efi_reset,
};

const pm_metal_boot_power_ops_t *pm_metal_boot_power_ops(void)
{
  return &g_ops;
}
