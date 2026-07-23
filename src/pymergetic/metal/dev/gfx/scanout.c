/** @file
  Lower half — scanout bind + shared copy. No busy-wait pacing.
**/
#include <pymergetic/metal/dev/gfx/scanout.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>

STATIC CONST pm_metal_scanout_ops_t  *mOps;
STATIC pm_metal_scanout_bind_t        mBind;

STATIC CONST pm_metal_scanout_ops_t *CONST  mProbeOrder[] = {
  &g_pm_metal_scanout_virtio_gpu,
  &g_pm_metal_scanout_bochs,
  &g_pm_metal_scanout_radeon_rv370, /* T43 1002:5460 — PCIe GART+CP / staging */
  &g_pm_metal_scanout_i915_855gm,   /* sample: T42 855GM */
  &g_pm_metal_scanout_gop_blt,
  &g_pm_metal_scanout_lfb_copy,
};

VOID
pm_metal_scanout_copy_rect (
  UINT32                         *dst,
  UINT32                          dst_pitch,
  INT32                           x,
  INT32                           y,
  INT32                           w,
  INT32                           h,
  CONST pm_metal_scanout_bind_t  *b
  )
{
  INT32  row;
  UINTN  bytes;

  if (dst == NULL || b == NULL || b->shadow == NULL || w <= 0 || h <= 0) {
    return;
  }

  bytes = (UINTN)w * sizeof (UINT32);
  if (x == 0 && (UINT32)w == b->shadow_w && dst_pitch == b->shadow_pitch
      && (UINT32)w == dst_pitch)
  {
    CopyMem (
      &dst[(UINT32)y * dst_pitch],
      &b->shadow[(UINT32)y * b->shadow_pitch],
      bytes * (UINTN)h
      );
    return;
  }

  for (row = 0; row < h; row++) {
    CopyMem (
      &dst[(UINT32)(y + row) * dst_pitch + (UINT32)x],
      &b->shadow[(UINT32)(y + row) * b->shadow_pitch + (UINT32)x],
      bytes
      );
  }
}

INT32
pm_metal_scanout_bind (
  CONST pm_metal_scanout_bind_t  *b
  )
{
  UINT32  i;

  if (b == NULL) {
    return -1;
  }

  if (mOps != NULL && mOps->fini != NULL) {
    mOps->fini ();
  }

  mOps = NULL;
  CopyMem (&mBind, b, sizeof (mBind));

  for (i = 0; i < (UINT32)(sizeof (mProbeOrder) / sizeof (mProbeOrder[0])); i++) {
    if (mProbeOrder[i]->probe (&mBind) == 0) {
      mOps = mProbeOrder[i];
      return 0;
    }
  }

  return -1;
}

CONST pm_metal_scanout_ops_t *
pm_metal_scanout_ops (
  VOID
  )
{
  return mOps;
}

CONST CHAR8 *
pm_metal_scanout_name (
  VOID
  )
{
  return (mOps != NULL && mOps->name != NULL) ? mOps->name : "none";
}

UINT32
pm_metal_scanout_caps (
  VOID
  )
{
  if (mOps == NULL || mOps->caps == NULL) {
    return 0;
  }

  return mOps->caps ();
}

VOID
pm_metal_scanout_fini (
  VOID
  )
{
  if (mOps != NULL && mOps->fini != NULL) {
    mOps->fini ();
  }

  mOps = NULL;
}

CONST pm_metal_scanout_bind_t *
pm_metal_scanout_bind_info (
  VOID
  )
{
  return &mBind;
}

VOID
pm_metal_scanout_bind_set_shadow (
  UINT32  *pixels,
  UINT32   pitch
  )
{
  mBind.shadow       = pixels;
  mBind.shadow_pitch = pitch;
}
