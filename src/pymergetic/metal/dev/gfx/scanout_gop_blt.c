/** @file
  Pre-EBS EFI GOP Blt scanout (Boot Services still live).
**/
#include <pymergetic/metal/dev/gfx/scanout.h>
#include <runtime/mem/mem.h>

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>

STATIC UINT32  *mPack;
STATIC UINT32   mPackCap;

STATIC
INT32
GopProbe (
  CONST pm_metal_scanout_bind_t  *b
  )
{
  if (b == NULL || b->gop == NULL || b->owned) {
    return -1;
  }

  return 0;
}

STATIC
INT32
GopPresentRect (
  INT32  x,
  INT32  y,
  INT32  w,
  INT32  h
  )
{
  CONST pm_metal_scanout_bind_t   *b;
  EFI_GRAPHICS_OUTPUT_PROTOCOL    *gop;
  EFI_STATUS                       Status;

  b = pm_metal_scanout_bind_info ();
  if (b == NULL || b->gop == NULL || b->shadow == NULL) {
    return -1;
  }

  gop = (EFI_GRAPHICS_OUTPUT_PROTOCOL *)b->gop;

  if (x == 0 && (UINT32)w == b->shadow_w) {
    Status = gop->Blt (
               gop,
               (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)b->shadow,
               EfiBltBufferToVideo,
               0,
               (UINTN)y,
               0,
               (UINTN)y,
               (UINTN)w,
               (UINTN)h,
               (UINTN)b->shadow_pitch * sizeof (UINT32)
               );
    return EFI_ERROR (Status) ? -1 : 0;
  }

  {
    UINT32  need;
    INT32   row;

    need = (UINT32)w * (UINT32)h;
    if (mPack == NULL || mPackCap < need) {
      if (mPack != NULL) {
        pm_metal_mem_free (mPack);
        mPack    = NULL;
        mPackCap = 0;
      }

      mPack = (UINT32 *)pm_metal_mem_alloc (
                          (UINTN)need * sizeof (UINT32),
                          PM_METAL_MEM_HEAP,
                          PM_METAL_MEM_ID_NONE
                          );
      if (mPack == NULL) {
        return -1;
      }

      mPackCap = need;
    }

    for (row = 0; row < h; row++) {
      CopyMem (
        &mPack[(UINT32)row * (UINT32)w],
        &b->shadow[(UINT32)(y + row) * b->shadow_pitch + (UINT32)x],
        (UINTN)w * sizeof (UINT32)
        );
    }

    Status = gop->Blt (
               gop,
               (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)mPack,
               EfiBltBufferToVideo,
               0,
               0,
               (UINTN)x,
               (UINTN)y,
               (UINTN)w,
               (UINTN)h,
               0
               );
  }

  return EFI_ERROR (Status) ? -1 : 0;
}

STATIC
INT32
GopJobBegin (
  INT32  x,
  INT32  y,
  INT32  w,
  INT32  h
  )
{
  return (GopPresentRect (x, y, w, h) == 0) ? 0 : -1;
}

STATIC
INT32
GopJobStep (
  VOID
  )
{
  return 0;
}

STATIC
UINT32
GopCaps (
  VOID
  )
{
  return 0;
}

STATIC
VOID
GopFini (
  VOID
  )
{
  if (mPack != NULL) {
    pm_metal_mem_free (mPack);
    mPack    = NULL;
    mPackCap = 0;
  }
}

CONST pm_metal_scanout_ops_t  g_pm_metal_scanout_gop_blt = {
  "gop_blt",
  GopProbe,
  GopPresentRect,
  GopJobBegin,
  GopJobStep,
  GopCaps,
  NULL,
  NULL,
  GopFini
};
