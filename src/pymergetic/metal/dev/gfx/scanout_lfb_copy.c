/** @file
  Iron fallback — chunked shadow → LFB memcpy (no GPU flip).
**/
#include <pymergetic/metal/dev/gfx/scanout.h>
#include "pymergetic/metal/dev/gfx/compat.h"

#include <stddef.h>
#include <stdint.h>

#ifndef PM_METAL_GFX_PRESENT_CHUNK_US
#define PM_METAL_GFX_PRESENT_CHUNK_US 1500u
#endif

static int32_t mJobLive;
static int32_t mJobX;
static int32_t mJobY;
static int32_t mJobW;
static int32_t mJobH;
static int32_t mJobRow;
static int32_t mJobBand;

static int32_t LfbProbe(const pm_metal_scanout_bind_t *b)
{
  if (b == NULL || b->fb == NULL || !b->owned) {
    return -1;
  }

  mJobLive = 0;
  mJobBand = 64;
  return 0;
}

static int32_t LfbPresentRect(int32_t x, int32_t y, int32_t w, int32_t h)
{
  const pm_metal_scanout_bind_t *b;

  b = pm_metal_scanout_bind_info();
  if (b == NULL || b->fb == NULL) {
    return -1;
  }

  /* Guest paces — no busy-wait vblank on the pump path. */
  pm_metal_scanout_copy_rect(b->fb, b->fb_ppsl, x, y, w, h, b);
  return 0;
}

static int32_t LfbJobBegin(int32_t x, int32_t y, int32_t w, int32_t h)
{
  const pm_metal_scanout_bind_t *b;

  b = pm_metal_scanout_bind_info();
  if (b == NULL || b->fb == NULL) {
    return -1;
  }

  /* Small rects: one-shot. Tall rects: chunked job with yields (async). */
  if (h < 96) {
    return (LfbPresentRect(x, y, w, h) == 0) ? 0 : -1;
  }

  mJobX    = x;
  mJobY    = y;
  mJobW    = w;
  mJobH    = h;
  mJobRow  = 0;
  mJobBand = 64;
  if (mJobBand > h) {
    mJobBand = h;
  }

  mJobLive = 1;
  return 1;
}

static int32_t LfbJobStep(void)
{
  const pm_metal_scanout_bind_t *b;
  int32_t                        band;
  int32_t                        y;
  uint64_t                       t0;
  uint64_t                       dt;

  if (!mJobLive) {
    return 0;
  }

  b = pm_metal_scanout_bind_info();
  if (b == NULL || b->fb == NULL) {
    mJobLive = 0;
    return -1;
  }

  band = mJobBand;
  if (band < 16) {
    band = 16;
  }

  if (mJobRow + band > mJobH) {
    band = mJobH - mJobRow;
  }

  if (band <= 0) {
    mJobLive = 0;
    return 0;
  }

  y  = mJobY + mJobRow;
  t0 = pm_metal_time_mono_us();
  pm_metal_scanout_copy_rect(b->fb, b->fb_ppsl, mJobX, y, mJobW, band, b);
  dt = pm_metal_time_mono_us() - t0;
  mJobRow += band;

  if (dt > 0 && band > 0) {
    uint64_t next;

    next = ((uint64_t)band * (uint64_t)PM_METAL_GFX_PRESENT_CHUNK_US) / dt;
    if (next < 16u) {
      next = 16u;
    }

    if (next > 256u) {
      next = 256u;
    }

    mJobBand = (int32_t)next;
  }

  if (mJobRow >= mJobH) {
    mJobLive = 0;
    return 0;
  }

  return 1;
}

static uint32_t LfbCaps(void)
{
  return PM_METAL_SCANOUT_CAP_CHUNKED;
}

static void LfbFini(void)
{
  mJobLive = 0;
}

const pm_metal_scanout_ops_t g_pm_metal_scanout_lfb_copy = {
  "lfb_copy", LfbProbe, LfbPresentRect, LfbJobBegin, LfbJobStep, LfbCaps, NULL, NULL, LfbFini
};
