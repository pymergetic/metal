/** @file
  Graphics — shadow compositor. Scanout backends: see scanout.h.
**/
#include <pymergetic/metal/dev/gfx/gfx.h>
#include <pymergetic/metal/dev/gfx/scanout.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/boot/port.h>
#include <pymergetic/metal/bus/io/io.h>
#include <pymergetic/metal/log/log.h>
#include <runtime/mem/mem.h>
#include <runtime/time/time.h>

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>

#include "font_vga8x16.inc.c"

/* Port: bios|efi dev/gfx/gfx_port.c */
int pm_metal_gfx_harvest_port(uint32_t **fb, uint32_t *width, uint32_t *height,
			      uint32_t *ppsl, void **gop);

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL  *mGop;
STATIC UINT32                        *mFb;
STATIC UINT32                         mFbPixelsPerScanLine;
STATIC UINT32                         mHarvestW;
STATIC UINT32                         mHarvestH;
STATIC INT32                          mHarvested;
STATIC pm_metal_gfx_surface_t         mSurf;
STATIC VOID                          *mSurfHeap; /* raw heap; pixels may be 4K-aligned */
STATIC INT32                          mReady;
STATIC INT32                          mDirect; /* 1 = shadow is scanout back buf */
/* Nearest upscale: dest-x → src-x (rebuilt when dw/src_w change). */
STATIC UINT16                        *mXMap;
STATIC UINT32                         mXMapCap;
STATIC INT32                          mXMapDw;
STATIC INT32                          mXMapSw;

#ifndef PM_METAL_GFX_MAX_SURFACES
#define PM_METAL_GFX_MAX_SURFACES  32u
#endif

typedef struct {
  INT32   used;
  INT32   x;
  INT32   y;
  INT32   w;
  INT32   h;
} pm_metal_gfx_surf_slot_t;

/* Slot 0 unused; slot 1 = DEFAULT (full FB). Tab surfaces ≥ 2. */
STATIC pm_metal_gfx_surf_slot_t  mSurfSlots[PM_METAL_GFX_MAX_SURFACES + 1];
STATIC pm_metal_gfx_surface_h    mDrawSurf = PM_METAL_GFX_SURFACE_DEFAULT;

/* Last blit dest — present() kicks this rect (iron: avoid full-FB copy). */
STATIC INT32                     mBlitHintValid;
STATIC INT32                     mBlitHintX;
STATIC INT32                     mBlitHintY;
STATIC INT32                     mBlitHintW;
STATIC INT32                     mBlitHintH;
STATIC pm_metal_gfx_surface_h    mDirtySurf;

/* Shared 60 Hz frame phase (mono µs). */
STATIC UINT64  mFrameNextUs;

STATIC INT32   mJobDone;

/* Rolling present FPS for status tray (~0.5 s windows). */
STATIC UINT64  mFpsWinStartUs;
STATIC UINT64  mFpsLastUs;
STATIC UINT32  mFpsWinCount;
STATIC UINT32  mFpsHz;

void
pm_metal_gfx_note_frame (
  VOID
  )
{
  UINT64  now;
  UINT64  elapsed;

  now         = pm_metal_time_mono_us ();
  mFpsLastUs  = now;
  if (mFpsWinStartUs == 0) {
    mFpsWinStartUs = now;
    mFpsWinCount   = 1;
    return;
  }

  mFpsWinCount++;
  elapsed = now - mFpsWinStartUs;
  if (elapsed >= 500000ull) {
    mFpsHz         = (UINT32)(((UINT64)mFpsWinCount * 1000000ull) / elapsed);
    mFpsWinStartUs = now;
    mFpsWinCount   = 0;
  }
}

uint32_t
pm_metal_gfx_fps (
  VOID
  )
{
  UINT64  now;

  /* No presents for ~1 s → idle (avoid stale "60fps" after a guest exits). */
  if (mFpsHz != 0 && mFpsLastUs != 0) {
    now = pm_metal_time_mono_us ();
    if (now - mFpsLastUs >= 1000000ull) {
      mFpsHz         = 0;
      mFpsWinStartUs = 0;
      mFpsWinCount   = 0;
    }
  }

  return mFpsHz;
}

STATIC
VOID
MetalGfxBlitHintSet (
  INT32  x,
  INT32  y,
  INT32  w,
  INT32  h
  )
{
  INT32  x1;
  INT32  y1;

  if (w <= 0 || h <= 0) {
    return;
  }

  mDirtySurf = (mDrawSurf != 0) ? mDrawSurf : PM_METAL_GFX_SURFACE_DEFAULT;

  if (mBlitHintValid == 0) {
    mBlitHintX     = x;
    mBlitHintY     = y;
    mBlitHintW     = w;
    mBlitHintH     = h;
    mBlitHintValid = 1;
    return;
  }

  x1 = mBlitHintX + mBlitHintW;
  y1 = mBlitHintY + mBlitHintH;
  if (x < mBlitHintX) {
    mBlitHintX = x;
  }

  if (y < mBlitHintY) {
    mBlitHintY = y;
  }

  if (x + w > x1) {
    x1 = x + w;
  }

  if (y + h > y1) {
    y1 = y + h;
  }

  mBlitHintW = x1 - mBlitHintX;
  mBlitHintH = y1 - mBlitHintY;
}

STATIC
INT32
MetalGfxBlitHintTake (
  INT32  *x,
  INT32  *y,
  INT32  *w,
  INT32  *h
  )
{
  if (mBlitHintValid == 0) {
    return 0;
  }

  if (x != NULL) {
    *x = mBlitHintX;
  }

  if (y != NULL) {
    *y = mBlitHintY;
  }

  if (w != NULL) {
    *w = mBlitHintW;
  }

  if (h != NULL) {
    *h = mBlitHintH;
  }

  mBlitHintValid = 0;
  return 1;
}

STATIC
VOID
MetalGfxOutputInit (
  VOID
  )
{
  pm_metal_scanout_bind_t  b;

  ZeroMem (&b, sizeof (b));
  b.shadow       = mSurf.pixels;
  b.shadow_w     = mSurf.width;
  b.shadow_h     = mSurf.height;
  b.shadow_pitch = mSurf.pitch;
  b.fb           = mFb;
  b.fb_ppsl      = mFbPixelsPerScanLine;
  b.mode_w       = mHarvestW;
  b.mode_h       = mHarvestH;
  b.gop          = mGop;
  b.owned        = pm_metal_port_owned () ? 1 : 0;
  if (pm_metal_scanout_bind (&b) == 0) {
    /* Floor tree listed gfx/framebuffer; refine compat to the backend. */
    (VOID)pm_metal_io_dt_set_compat (
            PM_METAL_IO_GFX,
            0,
            pm_metal_scanout_name ()
            );
  }

  /*
   * Never auto-adopt DIRECT as the compositor shadow. Soft cursor
   * save/restore assumes a stable heap; drawing into a flip back page then
   * swapping leaves cursor ghosts on the front.
   */
  mDirect  = 0;
  mJobDone = 1;
}

STATIC
VOID
MetalGfxDrawBounds (
  INT32  *ox,
  INT32  *oy,
  INT32  *ow,
  INT32  *oh
  )
{
  if (ox != NULL) {
    *ox = 0;
  }

  if (oy != NULL) {
    *oy = 0;
  }

  if (ow != NULL) {
    *ow = mReady ? (INT32)mSurf.width : 0;
  }

  if (oh != NULL) {
    *oh = mReady ? (INT32)mSurf.height : 0;
  }

  if (mDrawSurf < 2 || mDrawSurf > PM_METAL_GFX_MAX_SURFACES
      || !mSurfSlots[mDrawSurf].used)
  {
    return;
  }

  if (ox != NULL) {
    *ox = mSurfSlots[mDrawSurf].x;
  }

  if (oy != NULL) {
    *oy = mSurfSlots[mDrawSurf].y;
  }

  if (ow != NULL) {
    *ow = mSurfSlots[mDrawSurf].w;
  }

  if (oh != NULL) {
    *oh = mSurfSlots[mDrawSurf].h;
  }
}

STATIC
VOID
MetalGfxMapGuestRect (
  INT32  *x,
  INT32  *y,
  INT32  *w,
  INT32  *h
  )
{
  INT32  ox;
  INT32  oy;
  INT32  ow;
  INT32  oh;
  INT32  gx;
  INT32  gy;
  INT32  gw;
  INT32  gh;

  if (x == NULL || y == NULL || w == NULL || h == NULL) {
    return;
  }

  MetalGfxDrawBounds (&ox, &oy, &ow, &oh);
  gx = *x + ox;
  gy = *y + oy;
  gw = *w;
  gh = *h;

  if (gx < ox) {
    gw -= (ox - gx);
    gx  = ox;
  }

  if (gy < oy) {
    gh -= (oy - gy);
    gy  = oy;
  }

  if (gx + gw > ox + ow) {
    gw = ox + ow - gx;
  }

  if (gy + gh > oy + oh) {
    gh = oy + oh - gy;
  }

  *x = gx;
  *y = gy;
  *w = gw;
  *h = gh;
}

STATIC
VOID
MetalGfxPut (
  INT32                 x,
  INT32                 y,
  pm_metal_gfx_color_t  color
  )
{
  if (!mReady || mSurf.pixels == NULL) {
    return;
  }

  if (x < 0 || y < 0
      || (UINT32)x >= mSurf.width
      || (UINT32)y >= mSurf.height)
  {
    return;
  }

  mSurf.pixels[(UINT32)y * mSurf.pitch + (UINT32)x] = color;
}

STATIC
VOID
MetalGfxFillClipped (
  INT32                 x0,
  INT32                 y0,
  INT32                 x1,
  INT32                 y1,
  pm_metal_gfx_color_t  color
  )
{
  INT32  x;
  INT32  y;

  if (!mReady || mSurf.pixels == NULL) {
    return;
  }

  if (x0 < 0) {
    x0 = 0;
  }

  if (y0 < 0) {
    y0 = 0;
  }

  if (x1 > (INT32)mSurf.width) {
    x1 = (INT32)mSurf.width;
  }

  if (y1 > (INT32)mSurf.height) {
    y1 = (INT32)mSurf.height;
  }

  for (y = y0; y < y1; y++) {
    UINT32  *row;

    row = &mSurf.pixels[(UINT32)y * mSurf.pitch];
    for (x = x0; x < x1; x++) {
      row[x] = color;
    }
  }
}

int
pm_metal_gfx_harvest (
  VOID
  )
{
  UINT32  *fb;
  UINT32   w;
  UINT32   h;
  UINT32   ppsl;
  VOID    *gop;

  if (mHarvested) {
    return 0;
  }

  fb   = NULL;
  gop  = NULL;
  w    = 0;
  h    = 0;
  ppsl = 0;
  if (pm_metal_gfx_harvest_port (&fb, &w, &h, &ppsl, &gop) != 0) {
    return -1;
  }

  if (w < 320 || h < 200 || fb == NULL) {
    return -1;
  }

  mGop                 = (EFI_GRAPHICS_OUTPUT_PROTOCOL *)gop;
  mHarvestW            = w;
  mHarvestH            = h;
  mFb                  = fb;
  mFbPixelsPerScanLine = ppsl ? ppsl : w;
  mHarvested           = 1;
  return 0;
}

int
pm_metal_gfx_harvested (
  VOID
  )
{
  return mHarvested ? 1 : 0;
}

int
pm_metal_gfx_init (
  VOID
  )
{
  UINT32  W;
  UINT32  H;
  UINT32  Pitch;
  UINTN   Bytes;

  if (mReady) {
    return 0;
  }

  if (!mHarvested) {
    if (pm_metal_gfx_harvest () != 0) {
      return -1;
    }
  }

  W     = mHarvestW;
  H     = mHarvestH;
  Pitch = W;
  Bytes = (UINTN)Pitch * (UINTN)H * sizeof (UINT32);
  /* 4K-align pixels — radeon GART PTE low bits are flags. */
  mSurfHeap = pm_metal_mem_alloc (
                Bytes + 4096u,
                PM_METAL_MEM_HEAP,
                PM_METAL_MEM_ID_NONE
                );
  if (mSurfHeap == NULL) {
    return -1;
  }

  mSurf.pixels = (UINT32 *)(UINTN)(((UINTN)mSurfHeap + 4095u) & ~4095u);
  ZeroMem (mSurf.pixels, Bytes);
  mSurf.width  = W;
  mSurf.height = H;
  mSurf.pitch  = Pitch;
  mReady = 1;
  MetalGfxOutputInit ();
  pm_metal_gfx_clear (PM_METAL_GFX_RGB (0x4a, 0x4a, 0x4a));
  /*
   * Pre-EBS: Blt is fine. Post-EBS first present uses FB copy — defer until
   * UI frames (avoids a large silent fault window during bind).
   */
  if (!pm_metal_port_owned ()) {
    (VOID)pm_metal_gfx_present ();
  }

  return 0;
}

void
pm_metal_gfx_fini (
  VOID
  )
{
  pm_metal_scanout_fini ();

  /* DIRECT shadow lives in scanout VRAM — do not heap-free it. */
  if (mSurfHeap != NULL && mDirect == 0) {
    pm_metal_mem_free (mSurfHeap);
  } else if (mSurf.pixels != NULL && mDirect == 0 && mSurfHeap == NULL) {
    pm_metal_mem_free (mSurf.pixels);
  }

  mSurfHeap    = NULL;
  mSurf.pixels = NULL;

  if (mXMap != NULL) {
    pm_metal_mem_free (mXMap);
    mXMap    = NULL;
    mXMapCap = 0;
  }

  mXMapDw = 0;
  mXMapSw = 0;

  mSurf.width        = 0;
  mSurf.height       = 0;
  mSurf.pitch        = 0;
  mGop               = NULL;
  mFb                = NULL;
  mHarvested         = 0;
  mHarvestW          = 0;
  mHarvestH          = 0;
  mReady             = 0;
  mDirect            = 0;
  mJobDone           = 1;
  mDirtySurf         = 0;
  mFrameNextUs       = 0;
  mBlitHintValid     = 0;
}

uint64_t
pm_metal_gfx_frame_next_us (
  VOID
  )
{
  UINT64  now;
  UINT64  period;

  period = 1000000u / (UINT64)PM_METAL_GFX_FRAME_HZ;
  if (period == 0) {
    period = 16667u;
  }

  /* Absolute mono grid — same phase guests compute with mono_us. */
  now          = pm_metal_time_mono_us ();
  mFrameNextUs = (now / period + 1u) * period;
  return mFrameNextUs;
}

pm_metal_gfx_surface_h
pm_metal_gfx_dirty_surface (
  VOID
  )
{
  if (mBlitHintValid == 0) {
    return 0;
  }

  return (mDirtySurf != 0) ? mDirtySurf : PM_METAL_GFX_SURFACE_DEFAULT;
}

int
pm_metal_gfx_ready (
  VOID
  )
{
  return mReady ? 1 : 0;
}

CONST CHAR8 *
pm_metal_gfx_scanout_name (
  VOID
  )
{
  return pm_metal_scanout_name ();
}

pm_metal_gfx_surface_t *
pm_metal_gfx_surface (
  VOID
  )
{
  return mReady ? &mSurf : NULL;
}

void
pm_metal_gfx_clear (
  pm_metal_gfx_color_t  color
  )
{
  INT32  ox;
  INT32  oy;
  INT32  ow;
  INT32  oh;

  MetalGfxDrawBounds (&ox, &oy, &ow, &oh);
  if (ow <= 0 || oh <= 0) {
    return;
  }

  MetalGfxFillClipped (ox, oy, ox + ow, oy + oh, color);
}

void
pm_metal_gfx_fill_rect (
  INT32                 x,
  INT32                 y,
  INT32                 w,
  INT32                 h,
  pm_metal_gfx_color_t  color
  )
{
  if (w <= 0 || h <= 0) {
    return;
  }

  MetalGfxMapGuestRect (&x, &y, &w, &h);
  if (w <= 0 || h <= 0) {
    return;
  }

  MetalGfxFillClipped (x, y, x + w, y + h, color);
}

void
pm_metal_gfx_draw_rect (
  INT32                 x,
  INT32                 y,
  INT32                 w,
  INT32                 h,
  pm_metal_gfx_color_t  color
  )
{
  INT32  i;

  if (w <= 0 || h <= 0) {
    return;
  }

  MetalGfxMapGuestRect (&x, &y, &w, &h);
  if (w <= 0 || h <= 0) {
    return;
  }

  for (i = 0; i < w; i++) {
    MetalGfxPut (x + i, y, color);
    MetalGfxPut (x + i, y + h - 1, color);
  }

  for (i = 0; i < h; i++) {
    MetalGfxPut (x, y + i, color);
    MetalGfxPut (x + w - 1, y + i, color);
  }
}

void
pm_metal_gfx_bevel_rect (
  INT32                 x,
  INT32                 y,
  INT32                 w,
  INT32                 h,
  INT32                 raised,
  pm_metal_gfx_color_t  hi,
  pm_metal_gfx_color_t  lo
  )
{
  pm_metal_gfx_color_t  top;
  pm_metal_gfx_color_t  bot;
  INT32                 i;

  if (w < 2 || h < 2) {
    return;
  }

  MetalGfxMapGuestRect (&x, &y, &w, &h);
  if (w < 2 || h < 2) {
    return;
  }

  top = raised ? hi : lo;
  bot = raised ? lo : hi;

  for (i = 0; i < w; i++) {
    MetalGfxPut (x + i, y, top);
    MetalGfxPut (x + i, y + 1, top);
    MetalGfxPut (x + i, y + h - 1, bot);
    MetalGfxPut (x + i, y + h - 2, bot);
  }

  for (i = 0; i < h; i++) {
    MetalGfxPut (x, y + i, top);
    MetalGfxPut (x + 1, y + i, top);
    MetalGfxPut (x + w - 1, y + i, bot);
    MetalGfxPut (x + w - 2, y + i, bot);
  }
}

STATIC
VOID
MetalGfxGlyph (
  INT32                 x,
  INT32                 y,
  UINT8                 ch,
  pm_metal_gfx_color_t  fg,
  pm_metal_gfx_color_t  bg,
  INT32                 transparent_bg
  )
{
  CONST UINT8  *g;
  INT32         row;
  INT32         col;

#if PM_METAL_GFX_FONT_N < 256
  if (ch >= PM_METAL_GFX_FONT_N) {
    ch = (UINT8)'?';
  }
#endif

  g = mFontGlyphs[ch];
  for (row = 0; row < PM_METAL_GFX_FONT_H; row++) {
    UINT8  bits;

    bits = g[row];
    for (col = 0; col < PM_METAL_GFX_FONT_W; col++) {
      if (bits & (UINT8)(0x80u >> col)) {
        MetalGfxPut (x + col, y + row, fg);
      } else if (!transparent_bg) {
        MetalGfxPut (x + col, y + row, bg);
      }
    }
  }
}

void
pm_metal_gfx_draw_text (
  INT32                 x,
  INT32                 y,
  CONST CHAR8          *text,
  pm_metal_gfx_color_t  fg,
  pm_metal_gfx_color_t  bg,
  INT32                 transparent_bg
  )
{
  INT32  cx;
  INT32  ox;
  INT32  oy;
  INT32  ow;
  INT32  oh;

  if (text == NULL) {
    return;
  }

  MetalGfxDrawBounds (&ox, &oy, &ow, &oh);
  x += ox;
  y += oy;
  if (y + (INT32)PM_METAL_GFX_FONT_H <= oy || y >= oy + oh) {
    return;
  }

  cx = x;
  while (*text != '\0') {
    if (cx + (INT32)PM_METAL_GFX_FONT_W > ox
        && cx < ox + ow)
    {
      MetalGfxGlyph (cx, y, (UINT8)*text, fg, bg, transparent_bg);
    }

    cx += PM_METAL_GFX_FONT_W;
    text++;
  }
}

uint32_t
pm_metal_gfx_font_width (
  VOID
  )
{
  return PM_METAL_GFX_FONT_W;
}

uint32_t
pm_metal_gfx_font_height (
  VOID
  )
{
  return PM_METAL_GFX_FONT_H;
}

/**
 * Primary FB present (DEFAULT). Never follows mDrawSurf — callers that want
 * the current tab slot must use present_surface(slot) explicitly.
 * (present_surface(DEFAULT) used to bounce into present() and get hijacked
 * by a leftover tab draw surface, so `run doom` never hit the full LFB.)
 */
STATIC
INT32
MetalGfxPresentPrimary (
  VOID
  )
{
  INT32  hx;
  INT32  hy;
  INT32  hw;
  INT32  hh;

  if (MetalGfxBlitHintTake (&hx, &hy, &hw, &hh) != 0) {
    return pm_metal_gfx_present_rect (hx, hy, hw, hh);
  }

  return pm_metal_gfx_present_rect (0, 0, (INT32)mSurf.width, (INT32)mSurf.height);
}

int
pm_metal_gfx_present (
  VOID
  )
{
  if (mDrawSurf != PM_METAL_GFX_SURFACE_DEFAULT && mDrawSurf != 0) {
    return pm_metal_gfx_present_surface (mDrawSurf);
  }

  return MetalGfxPresentPrimary ();
}

int
pm_metal_gfx_present_rect (
  INT32  x,
  INT32  y,
  INT32  w,
  INT32  h
  )
{
  CONST pm_metal_scanout_ops_t  *ops;
  INT32                          rc;

  if (!mReady || mSurf.pixels == NULL) {
    return -1;
  }

  if (w <= 0 || h <= 0) {
    return 0;
  }

  if (x < 0) {
    w += x;
    x  = 0;
  }

  if (y < 0) {
    h += y;
    y  = 0;
  }

  if (x >= (INT32)mSurf.width || y >= (INT32)mSurf.height) {
    return 0;
  }

  if (x + w > (INT32)mSurf.width) {
    w = (INT32)mSurf.width - x;
  }

  if (y + h > (INT32)mSurf.height) {
    h = (INT32)mSurf.height - y;
  }

  mBlitHintValid = 0;
  pm_metal_scanout_bind_set_shadow (mSurf.pixels, mSurf.pitch);

  ops = pm_metal_scanout_ops ();
  if (ops == NULL || ops->present_rect == NULL) {
    return -1;
  }

  rc = ops->present_rect (x, y, w, h);
  if (rc == 0 && mDirect != 0 && ops->after_flip != NULL) {
    UINT32  *p;

    p = mSurf.pixels;
    ops->after_flip (&p);
    mSurf.pixels = p;
  }

  /*
   * Count substantial sync presents only (skip cursor / input blinks).
   * Async jobs are counted via pm_metal_async_perf_note_present_frame.
   */
  if (rc == 0
      && mSurf.width > 0
      && mSurf.height > 0
      && ((UINT64)w * (UINT64)h)
         >= ((UINT64)mSurf.width * (UINT64)mSurf.height / 4ull))
  {
    pm_metal_gfx_note_frame ();
  }

  return rc;
}

void
pm_metal_gfx_set_surface (
  pm_metal_gfx_surface_h  s
  )
{
  if (s == 0 || s == PM_METAL_GFX_SURFACE_DEFAULT) {
    mDrawSurf = PM_METAL_GFX_SURFACE_DEFAULT;
    return;
  }

  if (s > PM_METAL_GFX_MAX_SURFACES || !mSurfSlots[s].used) {
    return;
  }

  mDrawSurf = s;
}

pm_metal_gfx_surface_h
pm_metal_gfx_draw_surface (
  VOID
  )
{
  return mDrawSurf;
}

int
pm_metal_gfx_width (
  VOID
  )
{
  INT32  ow;

  MetalGfxDrawBounds (NULL, NULL, &ow, NULL);
  return ow;
}

int
pm_metal_gfx_height (
  VOID
  )
{
  INT32  oh;

  MetalGfxDrawBounds (NULL, NULL, NULL, &oh);
  return oh;
}

pm_metal_gfx_surface_h
pm_metal_gfx_surface_alloc (
  VOID
  )
{
  UINT32  i;

  for (i = 2; i <= PM_METAL_GFX_MAX_SURFACES; i++) {
    if (!mSurfSlots[i].used) {
      ZeroMem (&mSurfSlots[i], sizeof (mSurfSlots[i]));
      mSurfSlots[i].used = 1;
      return (pm_metal_gfx_surface_h)i;
    }
  }

  return PM_METAL_GFX_SURFACE_INVALID;
}

void
pm_metal_gfx_surface_free (
  pm_metal_gfx_surface_h  s
  )
{
  if (s < 2 || s > PM_METAL_GFX_MAX_SURFACES) {
    return;
  }

  ZeroMem (&mSurfSlots[s], sizeof (mSurfSlots[s]));
}

void
pm_metal_gfx_surface_set_rect (
  pm_metal_gfx_surface_h  s,
  int32_t                 x,
  int32_t                 y,
  int32_t                 w,
  int32_t                 h
  )
{
  if (s < 2 || s > PM_METAL_GFX_MAX_SURFACES || !mSurfSlots[s].used) {
    return;
  }

  mSurfSlots[s].x = x;
  mSurfSlots[s].y = y;
  mSurfSlots[s].w = w;
  mSurfSlots[s].h = h;
}

int
pm_metal_gfx_present_surface (
  pm_metal_gfx_surface_h  s
  )
{
  INT32  hx;
  INT32  hy;
  INT32  hw;
  INT32  hh;

  if (s == PM_METAL_GFX_SURFACE_DEFAULT || s == 0) {
    return MetalGfxPresentPrimary ();
  }

  if (s > PM_METAL_GFX_MAX_SURFACES || !mSurfSlots[s].used) {
    return -1;
  }

  if (mSurfSlots[s].w <= 0 || mSurfSlots[s].h <= 0) {
    return 0;
  }

  /* Same as DEFAULT present(): honor last blit rect when set. */
  if (MetalGfxBlitHintTake (&hx, &hy, &hw, &hh) != 0) {
    return pm_metal_gfx_present_rect (hx, hy, hw, hh);
  }

  return pm_metal_gfx_present_rect (
           mSurfSlots[s].x,
           mSurfSlots[s].y,
           mSurfSlots[s].w,
           mSurfSlots[s].h
           );
}

STATIC
INT32
MetalGfxPresentResolveRect (
  pm_metal_gfx_surface_h  s,
  INT32                  *x,
  INT32                  *y,
  INT32                  *w,
  INT32                  *h
  )
{
  INT32  hx;
  INT32  hy;
  INT32  hw;
  INT32  hh;

  if (x == NULL || y == NULL || w == NULL || h == NULL) {
    return -1;
  }

  if (MetalGfxBlitHintTake (&hx, &hy, &hw, &hh) != 0) {
    *x = hx;
    *y = hy;
    *w = hw;
    *h = hh;
    return 0;
  }

  if (s == PM_METAL_GFX_SURFACE_DEFAULT || s == 0) {
    *x = 0;
    *y = 0;
    *w = (INT32)mSurf.width;
    *h = (INT32)mSurf.height;
    return 0;
  }

  if (s > PM_METAL_GFX_MAX_SURFACES || !mSurfSlots[s].used) {
    return -1;
  }

  *x = mSurfSlots[s].x;
  *y = mSurfSlots[s].y;
  *w = mSurfSlots[s].w;
  *h = mSurfSlots[s].h;
  return 0;
}

int
pm_metal_gfx_present_job_begin (
  pm_metal_gfx_surface_h  s
  )
{
  CONST pm_metal_scanout_ops_t  *ops;
  INT32                          x;
  INT32                          y;
  INT32                          w;
  INT32                          h;
  INT32                          jb;
  UINT64                         t0;

  mJobDone = 1;
  if (!mReady || mSurf.pixels == NULL) {
    return -1;
  }

  if (MetalGfxPresentResolveRect (s, &x, &y, &w, &h) != 0) {
    return -1;
  }

  if (w <= 0 || h <= 0) {
    return 0;
  }

  mBlitHintValid = 0;
  pm_metal_scanout_bind_set_shadow (mSurf.pixels, mSurf.pitch);
  ops = pm_metal_scanout_ops ();
  if (ops == NULL || ops->job_begin == NULL) {
    return -1;
  }

  /* Flip/copy work often finishes in begin — time it (was invisible before). */
  t0 = pm_metal_time_mono_us ();
  jb = ops->job_begin (x, y, w, h);
  pm_metal_async_perf_note_present_us (pm_metal_time_mono_us () - t0);
  if (jb < 0) {
    return -1;
  }

  if (jb == 0) {
    mJobDone = 1;
    if (mDirect != 0 && ops->after_flip != NULL) {
      UINT32  *p;

      p = mSurf.pixels;
      ops->after_flip (&p);
      mSurf.pixels = p;
    }

    return 0;
  }

  mJobDone = 0;
  return 0;
}

int
pm_metal_gfx_present_job_step (
  VOID
  )
{
  CONST pm_metal_scanout_ops_t  *ops;
  INT32                          st;

  if (mJobDone) {
    return 0;
  }

  ops = pm_metal_scanout_ops ();
  if (ops == NULL || ops->job_step == NULL) {
    mJobDone = 1;
    return -1;
  }

  st = ops->job_step ();
  if (st <= 0) {
    mJobDone = 1;
    if (st == 0 && mDirect != 0 && ops->after_flip != NULL) {
      UINT32  *p;

      p = mSurf.pixels;
      ops->after_flip (&p);
      mSurf.pixels = p;
    }

    return st;
  }

  return 1;
}

int32_t
pm_metal_gfx_surface_width (
  pm_metal_gfx_surface_h  s
  )
{
  if (s == PM_METAL_GFX_SURFACE_DEFAULT || s == 0) {
    return mReady ? (INT32)mSurf.width : 0;
  }

  if (s > PM_METAL_GFX_MAX_SURFACES || !mSurfSlots[s].used) {
    return 0;
  }

  return mSurfSlots[s].w;
}

int32_t
pm_metal_gfx_surface_height (
  pm_metal_gfx_surface_h  s
  )
{
  if (s == PM_METAL_GFX_SURFACE_DEFAULT || s == 0) {
    return mReady ? (INT32)mSurf.height : 0;
  }

  if (s > PM_METAL_GFX_MAX_SURFACES || !mSurfSlots[s].used) {
    return 0;
  }

  return mSurfSlots[s].h;
}

int32_t
pm_metal_gfx_surface_origin_x (
  pm_metal_gfx_surface_h  s
  )
{
  if (s == PM_METAL_GFX_SURFACE_DEFAULT || s == 0) {
    return 0;
  }

  if (s > PM_METAL_GFX_MAX_SURFACES || !mSurfSlots[s].used) {
    return 0;
  }

  return mSurfSlots[s].x;
}

int32_t
pm_metal_gfx_surface_origin_y (
  pm_metal_gfx_surface_h  s
  )
{
  if (s == PM_METAL_GFX_SURFACE_DEFAULT || s == 0) {
    return 0;
  }

  if (s > PM_METAL_GFX_MAX_SURFACES || !mSurfSlots[s].used) {
    return 0;
  }

  return mSurfSlots[s].y;
}

int
pm_metal_gfx_blit_bgra (
  INT32        dx,
  INT32        dy,
  INT32        dw,
  INT32        dh,
  CONST VOID  *pixels,
  INT32        src_w,
  INT32        src_h,
  INT32        src_pitch
  )
{
  INT32         x;
  INT32         y;
  CONST UINT8  *src_base;
  UINT64        t0;
  INT32         rc;

  t0 = pm_metal_time_mono_us ();

  if (!mReady || mSurf.pixels == NULL || pixels == NULL) {
    return -1;
  }

  if (src_w <= 0 || src_h <= 0 || src_pitch < src_w * 4 || dw <= 0 || dh <= 0) {
    return -1;
  }

  MetalGfxMapGuestRect (&dx, &dy, &dw, &dh);
  if (dw <= 0 || dh <= 0) {
    return 0;
  }

  if (dx < 0) {
    dw += dx;
    dx  = 0;
  }

  if (dy < 0) {
    dh += dy;
    dy  = 0;
  }

  if (dx >= (INT32)mSurf.width || dy >= (INT32)mSurf.height) {
    return 0;
  }

  if (dx + dw > (INT32)mSurf.width) {
    dw = (INT32)mSurf.width - dx;
  }

  if (dy + dh > (INT32)mSurf.height) {
    dh = (INT32)mSurf.height - dy;
  }

  src_base = (CONST UINT8 *)pixels;
  rc       = 0;

  /* Integer scale fast path — avoid per-dest-pixel divides. */
  if ((dw % src_w) == 0 && (dh % src_h) == 0
      && (dw / src_w) == (dh / src_h))
  {
    INT32  scale;

    scale = dw / src_w;
    if (scale == 1) {
      for (y = 0; y < src_h; y++) {
        CONST UINT32 *srow;
        UINT32       *drow;

        srow = (CONST UINT32 *)(src_base + (UINTN)y * (UINTN)src_pitch);
        drow = &mSurf.pixels[(UINT32)(dy + y) * mSurf.pitch + (UINT32)dx];
        CopyMem (drow, srow, (UINTN)src_w * sizeof (UINT32));
      }
    } else if (scale > 1) {
      for (y = 0; y < src_h; y++) {
        CONST UINT32 *srow;
        INT32         ry;

        srow = (CONST UINT32 *)(src_base + (UINTN)y * (UINTN)src_pitch);
        for (ry = 0; ry < scale; ry++) {
          UINT32 *drow;
          INT32   sx;
          INT32   out;

          drow = &mSurf.pixels[(UINT32)(dy + y * scale + ry) * mSurf.pitch
                               + (UINT32)dx];
          out  = 0;
          for (sx = 0; sx < src_w; sx++) {
            UINT32  px;
            INT32   rx;

            px = srow[sx];
            for (rx = 0; rx < scale; rx++) {
              drow[out++] = px;
            }
          }
        }
      }
    } else {
      goto nearest;
    }

    MetalGfxBlitHintSet (dx, dy, dw, dh);
    pm_metal_async_perf_note_blit_us (pm_metal_time_mono_us () - t0);
    return rc;
  }

nearest:
  /*
   * Stretch-fill (non-integer scale). X map rebuilt only when size changes;
   * inner loop is a gather — no per-pixel divide/add.
   */
  {
    UINT32  y_step;
    UINT32  y_acc;
    UINT32  x_step;
    UINT32  x_acc;
    INT32   xi;

    if (mXMap == NULL || mXMapCap < (UINT32)dw
        || mXMapDw != dw || mXMapSw != src_w)
    {
      if (mXMap != NULL && mXMapCap < (UINT32)dw) {
        pm_metal_mem_free (mXMap);
        mXMap    = NULL;
        mXMapCap = 0;
      }

      if (mXMap == NULL) {
        mXMap = (UINT16 *)pm_metal_mem_alloc (
                            (UINTN)dw * sizeof (UINT16),
                            PM_METAL_MEM_HEAP,
                            PM_METAL_MEM_ID_NONE
                            );
        if (mXMap == NULL) {
          return -1;
        }

        mXMapCap = (UINT32)dw;
      }

      x_step = ((UINT32)src_w << 16) / (UINT32)dw;
      x_acc  = 0;
      for (xi = 0; xi < dw; xi++) {
        INT32  sx;

        sx = (INT32)(x_acc >> 16);
        if (sx >= src_w) {
          sx = src_w - 1;
        }

        mXMap[xi] = (UINT16)sx;
        x_acc    += x_step;
      }

      mXMapDw = dw;
      mXMapSw = src_w;
    }

    /*
     * Vertical integer scale: gather each src row once, replicate. Cuts
     * gather work when dh is a multiple of src_h (common-ish on panels).
     */
    if ((dh % src_h) == 0 && (dh / src_h) > 1) {
      INT32  y_rep;

      y_rep = dh / src_h;
      for (y = 0; y < src_h; y++) {
        CONST UINT32 *srow;
        UINT32       *drow0;
        INT32         ry;

        srow  = (CONST UINT32 *)(src_base + (UINTN)y * (UINTN)src_pitch);
        drow0 = &mSurf.pixels[(UINT32)(dy + y * y_rep) * mSurf.pitch
                              + (UINT32)dx];
        for (x = 0; x < dw; x++) {
          drow0[x] = srow[mXMap[x]];
        }

        for (ry = 1; ry < y_rep; ry++) {
          CopyMem (
            &mSurf.pixels[(UINT32)(dy + y * y_rep + ry) * mSurf.pitch
                          + (UINT32)dx],
            drow0,
            (UINTN)dw * sizeof (UINT32)
            );
        }
      }
    } else {
      INT32  prev_sy;

      y_step  = ((UINT32)src_h << 16) / (UINT32)dh;
      y_acc   = 0;
      prev_sy = -1;
      for (y = 0; y < dh; y++) {
        INT32         sy;
        CONST UINT32 *srow;
        UINT32       *drow;

        sy    = (INT32)(y_acc >> 16);
        y_acc += y_step;
        if (sy >= src_h) {
          sy = src_h - 1;
        }

        drow = &mSurf.pixels[(UINT32)(dy + y) * mSurf.pitch + (UINT32)dx];
        if (sy == prev_sy && y > 0) {
          /* Same src row as previous dest — replicate (1080p/320×200). */
          CopyMem (
            drow,
            &mSurf.pixels[(UINT32)(dy + y - 1) * mSurf.pitch + (UINT32)dx],
            (UINTN)dw * sizeof (UINT32)
            );
          continue;
        }

        srow = (CONST UINT32 *)(src_base + (UINTN)sy * (UINTN)src_pitch);
        for (x = 0; x < dw; x++) {
          drow[x] = srow[mXMap[x]];
        }

        prev_sy = sy;
      }
    }
  }

  MetalGfxBlitHintSet (dx, dy, dw, dh);
  pm_metal_async_perf_note_blit_us (pm_metal_time_mono_us () - t0);
  return rc;
}

#include "wasm_export.h"

STATIC INT32
pm_metal_gfx_width_native (
  wasm_exec_env_t  exec_env
  )
{
  (VOID)exec_env;
  return (INT32)pm_metal_gfx_width ();
}

STATIC INT32
pm_metal_gfx_height_native (
  wasm_exec_env_t  exec_env
  )
{
  (VOID)exec_env;
  return (INT32)pm_metal_gfx_height ();
}

STATIC VOID
pm_metal_gfx_clear_native (
  wasm_exec_env_t       exec_env,
  UINT32                color
  )
{
  (VOID)exec_env;
  pm_metal_gfx_clear ((pm_metal_gfx_color_t)color);
}

STATIC VOID
pm_metal_gfx_fill_rect_native (
  wasm_exec_env_t  exec_env,
  INT32            x,
  INT32            y,
  INT32            w,
  INT32            h,
  UINT32           color
  )
{
  (VOID)exec_env;
  pm_metal_gfx_fill_rect (x, y, w, h, (pm_metal_gfx_color_t)color);
}

STATIC VOID
pm_metal_gfx_draw_rect_native (
  wasm_exec_env_t  exec_env,
  INT32            x,
  INT32            y,
  INT32            w,
  INT32            h,
  UINT32           color
  )
{
  (VOID)exec_env;
  pm_metal_gfx_draw_rect (x, y, w, h, (pm_metal_gfx_color_t)color);
}

STATIC VOID
pm_metal_gfx_bevel_rect_native (
  wasm_exec_env_t  exec_env,
  INT32            x,
  INT32            y,
  INT32            w,
  INT32            h,
  INT32            raised,
  UINT32           hi,
  UINT32           lo
  )
{
  (VOID)exec_env;
  pm_metal_gfx_bevel_rect (
    x,
    y,
    w,
    h,
    (INT32)raised,
    (pm_metal_gfx_color_t)hi,
    (pm_metal_gfx_color_t)lo
    );
}

STATIC VOID
pm_metal_gfx_draw_text_native (
  wasm_exec_env_t  exec_env,
  INT32            x,
  INT32            y,
  CONST CHAR8     *text,
  UINT32           fg,
  UINT32           bg,
  INT32            transparent_bg
  )
{
  (VOID)exec_env;
  pm_metal_gfx_draw_text (
    x,
    y,
    text,
    (pm_metal_gfx_color_t)fg,
    (pm_metal_gfx_color_t)bg,
    (INT32)transparent_bg
    );
}

STATIC UINT32
pm_metal_gfx_font_width_native (
  wasm_exec_env_t  exec_env
  )
{
  (VOID)exec_env;
  return pm_metal_gfx_font_width ();
}

STATIC UINT32
pm_metal_gfx_font_height_native (
  wasm_exec_env_t  exec_env
  )
{
  (VOID)exec_env;
  return pm_metal_gfx_font_height ();
}

STATIC UINT32
pm_metal_gfx_fps_native (
  wasm_exec_env_t  exec_env
  )
{
  (VOID)exec_env;
  return pm_metal_gfx_fps ();
}

STATIC INT32
pm_metal_gfx_present_native (
  wasm_exec_env_t  exec_env
  )
{
  (VOID)exec_env;
  return (INT32)pm_metal_gfx_present ();
}

STATIC INT32
pm_metal_gfx_present_rect_native (
  wasm_exec_env_t  exec_env,
  INT32            x,
  INT32            y,
  INT32            w,
  INT32            h
  )
{
  (VOID)exec_env;
  return (INT32)pm_metal_gfx_present_rect (x, y, w, h);
}

STATIC INT32
pm_metal_gfx_blit_bgra_native (
  wasm_exec_env_t  exec_env,
  INT32            dx,
  INT32            dy,
  INT32            dw,
  INT32            dh,
  UINT32           app_ptr,
  INT32            src_w,
  INT32            src_h,
  INT32            src_pitch
  )
{
  wasm_module_inst_t  inst;
  VOID               *native;

  inst = wasm_runtime_get_module_inst (exec_env);
  if (inst == NULL || app_ptr == 0) {
    return -1;
  }

  if (src_w <= 0 || src_h <= 0 || src_pitch < src_w * 4) {
    return -1;
  }

  if (!wasm_runtime_validate_app_addr (
         inst,
         app_ptr,
         (UINT64)src_pitch * (UINT64)src_h
         ))
  {
    return -1;
  }

  native = wasm_runtime_addr_app_to_native (inst, app_ptr);
  if (native == NULL) {
    return -1;
  }

  return (INT32)pm_metal_gfx_blit_bgra (
                  dx,
                  dy,
                  dw,
                  dh,
                  native,
                  src_w,
                  src_h,
                  src_pitch
                  );
}

STATIC INT32
pm_metal_gfx_surface_width_native (
  wasm_exec_env_t  exec_env,
  UINT32           s
  )
{
  (VOID)exec_env;
  return pm_metal_gfx_surface_width (s);
}

STATIC INT32
pm_metal_gfx_surface_height_native (
  wasm_exec_env_t  exec_env,
  UINT32           s
  )
{
  (VOID)exec_env;
  return pm_metal_gfx_surface_height (s);
}

STATIC INT32
pm_metal_gfx_surface_origin_x_native (
  wasm_exec_env_t  exec_env,
  UINT32           s
  )
{
  (VOID)exec_env;
  return pm_metal_gfx_surface_origin_x (s);
}

STATIC INT32
pm_metal_gfx_surface_origin_y_native (
  wasm_exec_env_t  exec_env,
  UINT32           s
  )
{
  (VOID)exec_env;
  return pm_metal_gfx_surface_origin_y (s);
}

STATIC VOID
pm_metal_gfx_set_surface_native (
  wasm_exec_env_t  exec_env,
  UINT32           s
  )
{
  (VOID)exec_env;
  pm_metal_gfx_set_surface (s);
}

STATIC UINT32
pm_metal_gfx_draw_surface_native (
  wasm_exec_env_t  exec_env
  )
{
  (VOID)exec_env;
  return pm_metal_gfx_draw_surface ();
}

STATIC NativeSymbol g_pm_metal_gfx_native_symbols[] = {
  { "pm_metal_gfx_width", (VOID *)pm_metal_gfx_width_native, "()i", NULL },
  { "pm_metal_gfx_height", (VOID *)pm_metal_gfx_height_native, "()i", NULL },
  { "pm_metal_gfx_set_surface", (VOID *)pm_metal_gfx_set_surface_native, "(i)", NULL },
  { "pm_metal_gfx_draw_surface", (VOID *)pm_metal_gfx_draw_surface_native, "()i", NULL },
  { "pm_metal_gfx_surface_width", (VOID *)pm_metal_gfx_surface_width_native, "(i)i", NULL },
  { "pm_metal_gfx_surface_height", (VOID *)pm_metal_gfx_surface_height_native, "(i)i", NULL },
  { "pm_metal_gfx_surface_origin_x", (VOID *)pm_metal_gfx_surface_origin_x_native, "(i)i", NULL },
  { "pm_metal_gfx_surface_origin_y", (VOID *)pm_metal_gfx_surface_origin_y_native, "(i)i", NULL },
  { "pm_metal_gfx_clear", (VOID *)pm_metal_gfx_clear_native, "(i)", NULL },
  { "pm_metal_gfx_fill_rect", (VOID *)pm_metal_gfx_fill_rect_native, "(iiiii)", NULL },
  { "pm_metal_gfx_draw_rect", (VOID *)pm_metal_gfx_draw_rect_native, "(iiiii)", NULL },
  { "pm_metal_gfx_bevel_rect", (VOID *)pm_metal_gfx_bevel_rect_native, "(iiiiiii)", NULL },
  { "pm_metal_gfx_draw_text", (VOID *)pm_metal_gfx_draw_text_native, "(ii$iii)", NULL },
  { "pm_metal_gfx_font_width", (VOID *)pm_metal_gfx_font_width_native, "()i", NULL },
  { "pm_metal_gfx_font_height", (VOID *)pm_metal_gfx_font_height_native, "()i", NULL },
  { "pm_metal_gfx_fps", (VOID *)pm_metal_gfx_fps_native, "()i", NULL },
  { "pm_metal_gfx_present", (VOID *)pm_metal_gfx_present_native, "()i", NULL },
  { "pm_metal_gfx_present_rect", (VOID *)pm_metal_gfx_present_rect_native, "(iiii)i", NULL },
  { "pm_metal_gfx_blit_bgra", (VOID *)pm_metal_gfx_blit_bgra_native, "(iiiiiiii)i", NULL },
};

int
pm_metal_gfx_native_register (
  VOID
  )
{
  if (!wasm_runtime_register_natives (
         PM_METAL_GFX_WASI_MODULE,
         g_pm_metal_gfx_native_symbols,
         sizeof (g_pm_metal_gfx_native_symbols)
           / sizeof (g_pm_metal_gfx_native_symbols[0])
         ))
  {
    return -1;
  }

  return 0;
}
