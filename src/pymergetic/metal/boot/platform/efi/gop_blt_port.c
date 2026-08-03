#if defined(PM_METAL_BOOT_TARGET_EFI)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_BIOS)
#error "boot/efi/gop_blt_port.c built with PM_METAL_BOOT_TARGET_BIOS"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

#include <stdint.h>

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

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
  EFI_GRAPHICS_OUTPUT_PROTOCOL *g;
  EFI_STATUS st;

  if (gop == NULL || src == NULL || w == 0u || h == 0u) {
    return -1;
  }
  g = (EFI_GRAPHICS_OUTPUT_PROTOCOL *)gop;
  st = g->Blt(
      g,
      (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)src,
      EfiBltBufferToVideo,
      (UINTN)src_x,
      (UINTN)src_y,
      (UINTN)dst_x,
      (UINTN)dst_y,
      (UINTN)w,
      (UINTN)h,
      (UINTN)delta);
  return EFI_ERROR(st) ? -1 : 0;
}
