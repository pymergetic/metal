#if defined(PM_METAL_BOOT_TARGET_BIOS)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_EFI)
#error "boot/bios/gop_stash.c built with PM_METAL_BOOT_TARGET_EFI"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

#include <stdint.h>

#include <pymergetic/metal/boot/platform/gop.h>

void pm_metal_boot_efi_gop_stash(void) {}

int32_t pm_metal_boot_efi_gop_stash_get(
    uint32_t **fb_out,
    uint32_t *w_out,
    uint32_t *h_out,
    uint32_t *ppsl_out,
    void **gop_out)
{
  (void)fb_out;
  (void)w_out;
  (void)h_out;
  (void)ppsl_out;
  (void)gop_out;
  return -1;
}
