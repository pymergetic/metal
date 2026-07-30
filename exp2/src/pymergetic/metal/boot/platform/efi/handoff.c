#if defined(PM_METAL_BOOT_TARGET_EFI)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_BIOS)
#error "boot/efi/handoff.c built with PM_METAL_BOOT_TARGET_BIOS"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

/*
 * EFI handoff — stub until ExitBootServices is wired.
 * leave_firmware returns 0 (no-op success) for compile-ready shape.
 */
#include <stdint.h>

#include <pymergetic/metal/boot/platform/handoff.h>

static int32_t efi_leave_firmware(void)
{
  return 0;
}

static const pm_metal_boot_handoff_ops_t g_ops = {
  .leave_firmware = efi_leave_firmware,
};

const pm_metal_boot_handoff_ops_t *pm_metal_boot_handoff_ops(void)
{
  return &g_ops;
}
