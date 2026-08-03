#if defined(PM_METAL_BOOT_TARGET_BIOS)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_EFI)
#error "boot/bios/gop_blt_port.c built with PM_METAL_BOOT_TARGET_EFI"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

#include <stdint.h>

#include <pymergetic/metal/boot/platform/gop.h>

int32_t pm_metal_boot_gop_port_blt(
    void *gop,
    const uint32_t *src,
    uint32_t src_x,
    uint32_t src_y,
    uint32_t dst_x,
    uint32_t dst_y,
    uint32_t w,
    uint32_t h,
    uint32_t delta)
{
  (void)gop;
  (void)src;
  (void)src_x;
  (void)src_y;
  (void)dst_x;
  (void)dst_y;
  (void)w;
  (void)h;
  (void)delta;
  return -1;
}
