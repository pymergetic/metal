#if defined(PM_METAL_BOOT_TARGET_EFI)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_BIOS)
#error "boot/efi/main.c built with PM_METAL_BOOT_TARGET_BIOS"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

/*
 * Thin EFI entry shape — same bringup as BIOS once mem_map is real.
 * Not linked yet (see exp2/scripts/build efi).
 */
#include <stdint.h>

#include <pymergetic/metal/boot/platform/power.h>
#include <pymergetic/metal/boot/platform/private/bringup.h>

int32_t pm_metal_efi_main(void)
{
  if (pm_metal_boot_bringup() != 0) {
    pm_metal_boot_halt();
  }
  pm_metal_boot_halt();
  return 0;
}
