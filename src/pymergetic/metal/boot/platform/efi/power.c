#if defined(PM_METAL_BOOT_TARGET_EFI)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_BIOS)
#error "boot/efi/power.c built with PM_METAL_BOOT_TARGET_BIOS"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

#include <stdint.h>

#include <Uefi.h>

#include <pymergetic/metal/boot/platform/power.h>

#include "efi_ctx.h"

static void efi_halt(void)
{
  /* QEMU isa-debug-exit when present (same port as BIOS peer). */
  __asm__ volatile("outw %0, %1" : : "a"((uint16_t)0), "Nd"((uint16_t)0x501));
  for (;;) {
    __asm__ volatile("hlt");
  }
}

static void efi_reset(int32_t reboot)
{
  if (g_pm_efi_st != NULL && g_pm_efi_st->RuntimeServices != NULL) {
    (void)g_pm_efi_st->RuntimeServices->ResetSystem(
        reboot ? EfiResetCold : EfiResetShutdown, EFI_SUCCESS, 0, NULL);
  }
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
