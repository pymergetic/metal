#if defined(PM_METAL_BOOT_TARGET_BIOS)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_EFI)
#error "boot/bios/handoff.c built with PM_METAL_BOOT_TARGET_EFI"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

#include <stdint.h>

#include <pymergetic/metal/boot/platform/handoff.h>

static int32_t bios_leave_firmware(void)
{
  /* Multiboot path is already "owned"; nothing to drop. */
  return 0;
}

static const pm_metal_boot_handoff_ops_t g_ops = {
  .leave_firmware = bios_leave_firmware,
};

const pm_metal_boot_handoff_ops_t *pm_metal_boot_handoff_ops(void)
{
  return &g_ops;
}
