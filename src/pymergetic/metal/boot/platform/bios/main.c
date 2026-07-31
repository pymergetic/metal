#if defined(PM_METAL_BOOT_TARGET_BIOS)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_EFI)
#error "boot/bios/main.c built with PM_METAL_BOOT_TARGET_EFI"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

#include <stdint.h>

#include <pymergetic/metal/boot/platform/power.h>
#include <pymergetic/metal/boot/platform/private/bringup.h>

#include "io.h"

extern int32_t pm_metal_boot_bios_mem_map_ingest(uint32_t magic, const void *info);

static void exit_ok(void)
{
  pm_metal_bios_outw(0x501u, 0u);
  pm_metal_boot_halt();
}

static void exit_fail(void)
{
  pm_metal_bios_outw(0x501u, 1u);
  pm_metal_boot_halt();
}

void pm_metal_bios_main(uint32_t magic, void *mb_info)
{
  if (pm_metal_boot_bios_mem_map_ingest(magic, mb_info) != 0) {
    exit_fail();
  }
  if (pm_metal_boot_bringup() != 0) {
    exit_fail();
  }
  exit_ok();
}
