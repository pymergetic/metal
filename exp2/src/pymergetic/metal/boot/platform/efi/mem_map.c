#if defined(PM_METAL_BOOT_TARGET_EFI)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_BIOS)
#error "boot/efi/mem_map.c built with PM_METAL_BOOT_TARGET_BIOS"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

/*
 * EFI mem_map — stub until GetMemoryMap is wired via EDK2.
 * get() fails; image_end returns 0. Not linked into the BIOS image.
 */
#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/boot/platform/mem_map.h>

static int32_t efi_mem_map_get(pm_metal_boot_mem_region_t *out, uint32_t max, uint32_t *n_out)
{
  (void)out;
  (void)max;
  if (n_out != NULL) {
    *n_out = 0;
  }
  return -1;
}

static uintptr_t efi_image_end(void)
{
  return 0;
}

static const pm_metal_boot_mem_map_ops_t g_ops = {
  .get = efi_mem_map_get,
  .image_end = efi_image_end,
};

const pm_metal_boot_mem_map_ops_t *pm_metal_boot_mem_map_ops(void)
{
  return &g_ops;
}
