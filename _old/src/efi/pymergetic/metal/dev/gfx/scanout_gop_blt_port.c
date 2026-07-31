/** @file
  EFI body for the one EDK2 primitive scanout_gop_blt.c needs: real
  EFI_GRAPHICS_OUTPUT_PROTOCOL::Blt (EfiBltBufferToVideo), pre-ExitBootServices.
**/

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

#include <stdint.h>

int pm_metal_gop_port_blt(void          *gop,
                           const uint32_t *src,
                           uint32_t        src_x,
                           uint32_t        src_y,
                           uint32_t        dst_x,
                           uint32_t        dst_y,
                           uint32_t        w,
                           uint32_t        h,
                           uint32_t        delta)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_STATUS                    Status;

  Gop    = (EFI_GRAPHICS_OUTPUT_PROTOCOL *)gop;
  Status = Gop->Blt(Gop,
                     (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)src,
                     EfiBltBufferToVideo,
                     (UINTN)src_x,
                     (UINTN)src_y,
                     (UINTN)dst_x,
                     (UINTN)dst_y,
                     (UINTN)w,
                     (UINTN)h,
                     (UINTN)delta);
  return EFI_ERROR(Status) ? -1 : 0;
}
