/** @file
  EFI gfx harvest — GOP framebuffer (+ protocol for Blt).
**/

#include <stdint.h>
#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Library/UefiBootServicesTableLib.h>

int
pm_metal_gfx_harvest_port (
  UINT32  **fb,
  UINT32   *width,
  UINT32   *height,
  UINT32   *ppsl,
  VOID    **gop
  )
{
  EFI_STATUS                            Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL         *Gop;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;

  if (fb == NULL || width == NULL || height == NULL || ppsl == NULL || gop == NULL) {
    return -1;
  }

  if (gBS == NULL) {
    return -1;
  }

  Status = gBS->LocateProtocol (
                  &gEfiGraphicsOutputProtocolGuid,
                  NULL,
                  (VOID **)&Gop
                  );
  if (EFI_ERROR (Status) || Gop == NULL || Gop->Mode == NULL) {
    return -1;
  }

  Info = Gop->Mode->Info;
  if (Info == NULL) {
    return -1;
  }

  *width  = Info->HorizontalResolution;
  *height = Info->VerticalResolution;
  *ppsl   = Info->PixelsPerScanLine
              ? Info->PixelsPerScanLine
              : Info->HorizontalResolution;
  *fb     = (UINT32 *)(UINTN)Gop->Mode->FrameBufferBase;
  *gop    = Gop;
  return 0;
}
