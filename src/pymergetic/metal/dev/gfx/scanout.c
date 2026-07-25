/** @file
  Lower half — scanout bind + shared copy. No busy-wait pacing.
**/
#include <pymergetic/metal/dev/gfx/scanout.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const pm_metal_scanout_ops_t *mOps;
static pm_metal_scanout_bind_t       mBind;

static const pm_metal_scanout_ops_t *const mProbeOrder[] = {
  &g_pm_metal_scanout_virtio_gpu,   &g_pm_metal_scanout_bochs,
  &g_pm_metal_scanout_radeon_rv370, /* T43 1002:5460 — PCIe GART+CP / staging */
  &g_pm_metal_scanout_i915_855gm,   /* sample: T42 855GM */
  &g_pm_metal_scanout_gop_blt,      &g_pm_metal_scanout_lfb_copy,
};

void pm_metal_scanout_copy_rect(uint32_t                      *dst,
                                uint32_t                       dst_pitch,
                                int32_t                        x,
                                int32_t                        y,
                                int32_t                        w,
                                int32_t                        h,
                                const pm_metal_scanout_bind_t *b)
{
  int32_t row;
  size_t  bytes;

  if (dst == NULL || b == NULL || b->shadow == NULL || w <= 0 || h <= 0) {
    return;
  }

  bytes = (size_t)w * sizeof(uint32_t);
  if (x == 0 && (uint32_t)w == b->shadow_w && dst_pitch == b->shadow_pitch &&
      (uint32_t)w == dst_pitch) {
    memcpy(
      &dst[(uint32_t)y * dst_pitch], &b->shadow[(uint32_t)y * b->shadow_pitch], bytes * (size_t)h);
    return;
  }

  for (row = 0; row < h; row++) {
    memcpy(&dst[(uint32_t)(y + row) * dst_pitch + (uint32_t)x],
           &b->shadow[(uint32_t)(y + row) * b->shadow_pitch + (uint32_t)x],
           bytes);
  }
}

int32_t pm_metal_scanout_bind(const pm_metal_scanout_bind_t *b)
{
  uint32_t i;

  if (b == NULL) {
    return -1;
  }

  if (mOps != NULL && mOps->fini != NULL) {
    mOps->fini();
  }

  mOps = NULL;
  memcpy(&mBind, b, sizeof(mBind));

  for (i = 0; i < (uint32_t)(sizeof(mProbeOrder) / sizeof(mProbeOrder[0])); i++) {
    if (mProbeOrder[i]->probe(&mBind) == 0) {
      mOps = mProbeOrder[i];
      return 0;
    }
  }

  return -1;
}

const pm_metal_scanout_ops_t *pm_metal_scanout_ops(void)
{
  return mOps;
}

const char *pm_metal_scanout_name(void)
{
  return (mOps != NULL && mOps->name != NULL) ? mOps->name : "none";
}

uint32_t pm_metal_scanout_caps(void)
{
  if (mOps == NULL || mOps->caps == NULL) {
    return 0;
  }

  return mOps->caps();
}

void pm_metal_scanout_fini(void)
{
  if (mOps != NULL && mOps->fini != NULL) {
    mOps->fini();
  }

  mOps = NULL;
}

const pm_metal_scanout_bind_t *pm_metal_scanout_bind_info(void)
{
  return &mBind;
}

void pm_metal_scanout_bind_set_shadow(uint32_t *pixels, uint32_t pitch)
{
  mBind.shadow       = pixels;
  mBind.shadow_pitch = pitch;
}
