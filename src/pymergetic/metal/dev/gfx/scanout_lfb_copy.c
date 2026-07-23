/** @file
  Iron fallback — chunked shadow → LFB memcpy (no GPU flip).
**/
#include <pymergetic/metal/dev/gfx/scanout.h>
#include <runtime/time/time.h>

#include <Uefi.h>
#include <Library/BaseLib.h>

#ifndef PM_METAL_GFX_PRESENT_CHUNK_US
#define PM_METAL_GFX_PRESENT_CHUNK_US  1500u
#endif

STATIC INT32  mJobLive;
STATIC INT32  mJobX;
STATIC INT32  mJobY;
STATIC INT32  mJobW;
STATIC INT32  mJobH;
STATIC INT32  mJobRow;
STATIC INT32  mJobBand;

STATIC
INT32
LfbProbe (
  CONST pm_metal_scanout_bind_t  *b
  )
{
  if (b == NULL || b->fb == NULL || !b->owned) {
    return -1;
  }

  mJobLive = 0;
  mJobBand = 64;
  return 0;
}

STATIC
INT32
LfbPresentRect (
  INT32  x,
  INT32  y,
  INT32  w,
  INT32  h
  )
{
  CONST pm_metal_scanout_bind_t  *b;

  b = pm_metal_scanout_bind_info ();
  if (b == NULL || b->fb == NULL) {
    return -1;
  }

  /* Guest paces — no busy-wait vblank on the pump path. */
  pm_metal_scanout_copy_rect (b->fb, b->fb_ppsl, x, y, w, h, b);
  return 0;
}

STATIC
INT32
LfbJobBegin (
  INT32  x,
  INT32  y,
  INT32  w,
  INT32  h
  )
{
  CONST pm_metal_scanout_bind_t  *b;

  b = pm_metal_scanout_bind_info ();
  if (b == NULL || b->fb == NULL) {
    return -1;
  }

  /* Small rects: one-shot. Tall rects: chunked job with yields (async). */
  if (h < 96) {
    return (LfbPresentRect (x, y, w, h) == 0) ? 0 : -1;
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

STATIC
INT32
LfbJobStep (
  VOID
  )
{
  CONST pm_metal_scanout_bind_t  *b;
  INT32                           band;
  INT32                           y;
  UINT64                          t0;
  UINT64                          dt;

  if (!mJobLive) {
    return 0;
  }

  b = pm_metal_scanout_bind_info ();
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
  t0 = pm_metal_time_mono_us ();
  pm_metal_scanout_copy_rect (b->fb, b->fb_ppsl, mJobX, y, mJobW, band, b);
  dt = pm_metal_time_mono_us () - t0;
  mJobRow += band;

  if (dt > 0 && band > 0) {
    UINT64  next;

    next = ((UINT64)band * (UINT64)PM_METAL_GFX_PRESENT_CHUNK_US) / dt;
    if (next < 16u) {
      next = 16u;
    }

    if (next > 256u) {
      next = 256u;
    }

    mJobBand = (INT32)next;
  }

  if (mJobRow >= mJobH) {
    mJobLive = 0;
    return 0;
  }

  return 1;
}

STATIC
UINT32
LfbCaps (
  VOID
  )
{
  return PM_METAL_SCANOUT_CAP_CHUNKED;
}

STATIC
VOID
LfbFini (
  VOID
  )
{
  mJobLive = 0;
}

CONST pm_metal_scanout_ops_t  g_pm_metal_scanout_lfb_copy = {
  "lfb_copy",
  LfbProbe,
  LfbPresentRect,
  LfbJobBegin,
  LfbJobStep,
  LfbCaps,
  NULL,
  NULL,
  LfbFini
};
