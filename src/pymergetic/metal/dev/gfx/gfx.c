/** @file
  Graphics — shadow compositor. Scanout backends: see scanout.h.
**/
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/dev/gfx/gfx.h>
#include <pymergetic/metal/dev/gfx/scanout.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/boot/port.h>
#include <pymergetic/metal/bus/io/io.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/runtime/mem/mem.h>
#include <runtime/time/time.h>
#include <runtime/slot/slot_table.h>

#include "font_vga8x16.inc.c"

/* Port: bios|efi dev/gfx/gfx_port.c */
int pm_metal_gfx_harvest_port(
  uint32_t **fb, uint32_t *width, uint32_t *height, uint32_t *ppsl, void **gop);

/* Opaque EFI_GRAPHICS_OUTPUT_PROTOCOL* — never dereferenced here, only
 * threaded through to the GOP scanout backend (scanout_gop_blt.c). */
static void                  *mGop;
static uint32_t              *mFb;
static uint32_t               mFbPixelsPerScanLine;
static uint32_t               mHarvestW;
static uint32_t               mHarvestH;
static int32_t                mHarvested;
static pm_metal_gfx_surface_t mSurf;
static void                  *mSurfHeap; /* raw heap; pixels may be 4K-aligned */
static int32_t                mReady;
static int32_t                mDirect; /* 1 = shadow is scanout back buf */
/* Nearest upscale: dest-x → src-x (rebuilt when dw/src_w change). */
static uint16_t *mXMap;
static uint32_t  mXMapCap;
static int32_t   mXMapDw;
static int32_t   mXMapSw;

#ifndef PM_METAL_GFX_MAX_SURFACES
#define PM_METAL_GFX_MAX_SURFACES 32u
#endif

typedef struct {
  int32_t used;
  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;
} pm_metal_gfx_surf_slot_t;

/* Slot 0 unused; slot 1 = DEFAULT (full FB). Tab surfaces ≥ 2. */
static pm_metal_gfx_surf_slot_t mSurfSlots[PM_METAL_GFX_MAX_SURFACES + 1];
static pm_metal_gfx_surface_h   mDrawSurf = PM_METAL_GFX_SURFACE_DEFAULT;

/* Last blit dest — present() kicks this rect (iron: avoid full-FB copy). */
static int32_t                mBlitHintValid;
static int32_t                mBlitHintX;
static int32_t                mBlitHintY;
static int32_t                mBlitHintW;
static int32_t                mBlitHintH;
static pm_metal_gfx_surface_h mDirtySurf;

/* Shared 60 Hz frame phase (mono µs). */
static uint64_t mFrameNextUs;

static int32_t mJobDone;

/*
 * Present is now reachable from more than one physical CPU (the sync
 * present_rect() callers — shell pump / UI chrome — vs. an offloaded async
 * present job on another runner; see runtime/async/async_ops.c). The
 * scanout backend (ops + its own internal job cursor) and mJobDone /
 * shadow-bind are singletons that assume exactly one present in flight at
 * a time. This flag enforces that across CPUs; a contended caller just
 * skips (returns -1) rather than corrupting shared state — every existing
 * caller already tolerates a dropped/skipped present.
 */
static volatile uint32_t mPresentBusy;

static int32_t MetalGfxPresentTryAcquire(void)
{
  return (pm_metal_slot_port_cas32(&mPresentBusy, 0, 1) == 0) ? 1 : 0;
}

static void MetalGfxPresentRelease(void)
{
  (void)pm_metal_slot_port_cas32(&mPresentBusy, 1, 0);
}

/* Rolling present FPS for status tray (~0.5 s windows). */
static uint64_t mFpsWinStartUs;
static uint64_t mFpsLastUs;
static uint32_t mFpsWinCount;
static uint32_t mFpsHz;

void pm_metal_gfx_note_frame(void)
{
  uint64_t now;
  uint64_t elapsed;

  now        = pm_metal_time_mono_us();
  mFpsLastUs = now;
  if (mFpsWinStartUs == 0) {
    mFpsWinStartUs = now;
    mFpsWinCount   = 1;
    return;
  }

  mFpsWinCount++;
  elapsed = now - mFpsWinStartUs;
  if (elapsed >= 500000ull) {
    mFpsHz         = (uint32_t)(((uint64_t)mFpsWinCount * 1000000ull) / elapsed);
    mFpsWinStartUs = now;
    mFpsWinCount   = 0;
  }
}

uint32_t pm_metal_gfx_fps(void)
{
  uint64_t now;

  /* No presents for ~1 s → idle (avoid stale "60fps" after a guest exits). */
  if (mFpsHz != 0 && mFpsLastUs != 0) {
    now = pm_metal_time_mono_us();
    if (now - mFpsLastUs >= 1000000ull) {
      mFpsHz         = 0;
      mFpsWinStartUs = 0;
      mFpsWinCount   = 0;
    }
  }

  return mFpsHz;
}

static void MetalGfxBlitHintSet(int32_t x, int32_t y, int32_t w, int32_t h)
{
  int32_t x1;
  int32_t y1;

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

static int32_t MetalGfxBlitHintTake(int32_t *x, int32_t *y, int32_t *w, int32_t *h)
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

static void MetalGfxOutputInit(void)
{
  pm_metal_scanout_bind_t b;

  memset(&b, 0, sizeof(b));
  b.shadow       = mSurf.pixels;
  b.shadow_w     = mSurf.width;
  b.shadow_h     = mSurf.height;
  b.shadow_pitch = mSurf.pitch;
  b.fb           = mFb;
  b.fb_ppsl      = mFbPixelsPerScanLine;
  b.mode_w       = mHarvestW;
  b.mode_h       = mHarvestH;
  b.gop          = mGop;
  b.owned        = pm_metal_port_owned() ? 1 : 0;
  if (pm_metal_scanout_bind(&b) == 0) {
    /* Floor tree listed gfx/framebuffer; refine compat to the backend. */
    (void)pm_metal_io_dt_set_compat(PM_METAL_IO_GFX, 0, pm_metal_scanout_name());
  }

  /*
   * Never auto-adopt DIRECT as the compositor shadow. Soft cursor
   * save/restore assumes a stable heap; drawing into a flip back page then
   * swapping leaves cursor ghosts on the front.
   */
  mDirect  = 0;
  mJobDone = 1;
}

static void MetalGfxDrawBounds(int32_t *ox, int32_t *oy, int32_t *ow, int32_t *oh)
{
  if (ox != NULL) {
    *ox = 0;
  }

  if (oy != NULL) {
    *oy = 0;
  }

  if (ow != NULL) {
    *ow = mReady ? (int32_t)mSurf.width : 0;
  }

  if (oh != NULL) {
    *oh = mReady ? (int32_t)mSurf.height : 0;
  }

  if (mDrawSurf < 2 || mDrawSurf > PM_METAL_GFX_MAX_SURFACES || !mSurfSlots[mDrawSurf].used) {
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

static void MetalGfxMapGuestRect(int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
  int32_t ox;
  int32_t oy;
  int32_t ow;
  int32_t oh;
  int32_t gx;
  int32_t gy;
  int32_t gw;
  int32_t gh;

  if (x == NULL || y == NULL || w == NULL || h == NULL) {
    return;
  }

  MetalGfxDrawBounds(&ox, &oy, &ow, &oh);
  gx = *x + ox;
  gy = *y + oy;
  gw = *w;
  gh = *h;

  if (gx < ox) {
    gw -= (ox - gx);
    gx = ox;
  }

  if (gy < oy) {
    gh -= (oy - gy);
    gy = oy;
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

static void MetalGfxPut(int32_t x, int32_t y, pm_metal_gfx_color_t color)
{
  if (!mReady || mSurf.pixels == NULL) {
    return;
  }

  if (x < 0 || y < 0 || (uint32_t)x >= mSurf.width || (uint32_t)y >= mSurf.height) {
    return;
  }

  mSurf.pixels[(uint32_t)y * mSurf.pitch + (uint32_t)x] = color;
}

static void MetalGfxFillClipped(
  int32_t x0, int32_t y0, int32_t x1, int32_t y1, pm_metal_gfx_color_t color)
{
  int32_t x;
  int32_t y;

  if (!mReady || mSurf.pixels == NULL) {
    return;
  }

  if (x0 < 0) {
    x0 = 0;
  }

  if (y0 < 0) {
    y0 = 0;
  }

  if (x1 > (int32_t)mSurf.width) {
    x1 = (int32_t)mSurf.width;
  }

  if (y1 > (int32_t)mSurf.height) {
    y1 = (int32_t)mSurf.height;
  }

  for (y = y0; y < y1; y++) {
    uint32_t *row;

    row = &mSurf.pixels[(uint32_t)y * mSurf.pitch];
    for (x = x0; x < x1; x++) {
      row[x] = color;
    }
  }
}

int pm_metal_gfx_harvest(void)
{
  uint32_t *fb;
  uint32_t  w;
  uint32_t  h;
  uint32_t  ppsl;
  void     *gop;

  if (mHarvested) {
    return 0;
  }

  fb   = NULL;
  gop  = NULL;
  w    = 0;
  h    = 0;
  ppsl = 0;
  if (pm_metal_gfx_harvest_port(&fb, &w, &h, &ppsl, &gop) != 0) {
    return -1;
  }

  if (w < 320 || h < 200 || fb == NULL) {
    return -1;
  }

  mGop                 = gop;
  mHarvestW            = w;
  mHarvestH            = h;
  mFb                  = fb;
  mFbPixelsPerScanLine = ppsl ? ppsl : w;
  mHarvested           = 1;
  return 0;
}

int pm_metal_gfx_harvested(void)
{
  return mHarvested ? 1 : 0;
}

int pm_metal_gfx_init(void)
{
  uint32_t  W;
  uint32_t  H;
  uint32_t  Pitch;
  uintptr_t Bytes;

  if (mReady) {
    return 0;
  }

  if (!mHarvested) {
    if (pm_metal_gfx_harvest() != 0) {
      return -1;
    }
  }

  W     = mHarvestW;
  H     = mHarvestH;
  Pitch = W;
  Bytes = (uintptr_t)Pitch * (uintptr_t)H * sizeof(uint32_t);
  /* 4K-align pixels — radeon GART PTE low bits are flags. */
  mSurfHeap = pm_metal_mem_alloc(Bytes + 4096u, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (mSurfHeap == NULL) {
    return -1;
  }

  mSurf.pixels = (uint32_t *)(uintptr_t)(((uintptr_t)mSurfHeap + 4095u) & ~4095u);
  memset(mSurf.pixels, 0, Bytes);
  mSurf.width  = W;
  mSurf.height = H;
  mSurf.pitch  = Pitch;
  mReady       = 1;
  MetalGfxOutputInit();
  pm_metal_gfx_clear(PM_METAL_GFX_RGB(0x4a, 0x4a, 0x4a));
  /*
   * Pre-EBS: Blt is fine. Post-EBS first present uses FB copy — defer until
   * UI frames (avoids a large silent fault window during bind).
   */
  if (!pm_metal_port_owned()) {
    (void)pm_metal_gfx_present();
  }

  return 0;
}

void pm_metal_gfx_fini(void)
{
  pm_metal_scanout_fini();

  /* DIRECT shadow lives in scanout VRAM — do not heap-free it. */
  if (mSurfHeap != NULL && mDirect == 0) {
    pm_metal_mem_free(mSurfHeap);
  } else if (mSurf.pixels != NULL && mDirect == 0 && mSurfHeap == NULL) {
    pm_metal_mem_free(mSurf.pixels);
  }

  mSurfHeap    = NULL;
  mSurf.pixels = NULL;

  if (mXMap != NULL) {
    pm_metal_mem_free(mXMap);
    mXMap    = NULL;
    mXMapCap = 0;
  }

  mXMapDw = 0;
  mXMapSw = 0;

  mSurf.width    = 0;
  mSurf.height   = 0;
  mSurf.pitch    = 0;
  mGop           = NULL;
  mFb            = NULL;
  mHarvested     = 0;
  mHarvestW      = 0;
  mHarvestH      = 0;
  mReady         = 0;
  mDirect        = 0;
  mJobDone       = 1;
  mDirtySurf     = 0;
  mFrameNextUs   = 0;
  mBlitHintValid = 0;
}

uint64_t pm_metal_gfx_frame_next_us(void)
{
  uint64_t now;
  uint64_t period;

  period = 1000000u / (uint64_t)PM_METAL_GFX_FRAME_HZ;
  if (period == 0) {
    period = 16667u;
  }

  /* Absolute mono grid — same phase guests compute with mono_us. */
  now          = pm_metal_time_mono_us();
  mFrameNextUs = (now / period + 1u) * period;
  return mFrameNextUs;
}

pm_metal_gfx_surface_h pm_metal_gfx_dirty_surface(void)
{
  if (mBlitHintValid == 0) {
    return 0;
  }

  return (mDirtySurf != 0) ? mDirtySurf : PM_METAL_GFX_SURFACE_DEFAULT;
}

int pm_metal_gfx_ready(void)
{
  return mReady ? 1 : 0;
}

const char *pm_metal_gfx_scanout_name(void)
{
  return pm_metal_scanout_name();
}

pm_metal_gfx_surface_t *pm_metal_gfx_surface(void)
{
  return mReady ? &mSurf : NULL;
}

void pm_metal_gfx_clear(pm_metal_gfx_color_t color)
{
  int32_t ox;
  int32_t oy;
  int32_t ow;
  int32_t oh;

  MetalGfxDrawBounds(&ox, &oy, &ow, &oh);
  if (ow <= 0 || oh <= 0) {
    return;
  }

  MetalGfxFillClipped(ox, oy, ox + ow, oy + oh, color);
}

void pm_metal_gfx_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, pm_metal_gfx_color_t color)
{
  if (w <= 0 || h <= 0) {
    return;
  }

  MetalGfxMapGuestRect(&x, &y, &w, &h);
  if (w <= 0 || h <= 0) {
    return;
  }

  MetalGfxFillClipped(x, y, x + w, y + h, color);
}

void pm_metal_gfx_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, pm_metal_gfx_color_t color)
{
  int32_t i;

  if (w <= 0 || h <= 0) {
    return;
  }

  MetalGfxMapGuestRect(&x, &y, &w, &h);
  if (w <= 0 || h <= 0) {
    return;
  }

  for (i = 0; i < w; i++) {
    MetalGfxPut(x + i, y, color);
    MetalGfxPut(x + i, y + h - 1, color);
  }

  for (i = 0; i < h; i++) {
    MetalGfxPut(x, y + i, color);
    MetalGfxPut(x + w - 1, y + i, color);
  }
}

void pm_metal_gfx_bevel_rect(int32_t              x,
                             int32_t              y,
                             int32_t              w,
                             int32_t              h,
                             int32_t              raised,
                             pm_metal_gfx_color_t hi,
                             pm_metal_gfx_color_t lo)
{
  pm_metal_gfx_color_t top;
  pm_metal_gfx_color_t bot;
  int32_t              i;

  if (w < 2 || h < 2) {
    return;
  }

  MetalGfxMapGuestRect(&x, &y, &w, &h);
  if (w < 2 || h < 2) {
    return;
  }

  top = raised ? hi : lo;
  bot = raised ? lo : hi;

  for (i = 0; i < w; i++) {
    MetalGfxPut(x + i, y, top);
    MetalGfxPut(x + i, y + 1, top);
    MetalGfxPut(x + i, y + h - 1, bot);
    MetalGfxPut(x + i, y + h - 2, bot);
  }

  for (i = 0; i < h; i++) {
    MetalGfxPut(x, y + i, top);
    MetalGfxPut(x + 1, y + i, top);
    MetalGfxPut(x + w - 1, y + i, bot);
    MetalGfxPut(x + w - 2, y + i, bot);
  }
}

static void MetalGfxGlyph(int32_t              x,
                          int32_t              y,
                          uint8_t              ch,
                          pm_metal_gfx_color_t fg,
                          pm_metal_gfx_color_t bg,
                          int32_t              transparent_bg)
{
  const uint8_t *g;
  int32_t        row;
  int32_t        col;

#if PM_METAL_GFX_FONT_N < 256
  if (ch >= PM_METAL_GFX_FONT_N) {
    ch = (uint8_t)'?';
  }
#endif

  g = &mFontGlyphs[(unsigned)ch * PM_METAL_GFX_FONT_BYTES_PER_GLYPH];
  for (row = 0; row < PM_METAL_GFX_FONT_H; row++) {
    uint8_t bits;

    bits = g[row];
    for (col = 0; col < PM_METAL_GFX_FONT_W; col++) {
      if (bits & (uint8_t)(0x80u >> col)) {
        MetalGfxPut(x + col, y + row, fg);
      } else if (!transparent_bg) {
        MetalGfxPut(x + col, y + row, bg);
      }
    }
  }
}

void pm_metal_gfx_draw_text(int32_t              x,
                            int32_t              y,
                            const char          *text,
                            pm_metal_gfx_color_t fg,
                            pm_metal_gfx_color_t bg,
                            int32_t              transparent_bg)
{
  int32_t cx;
  int32_t ox;
  int32_t oy;
  int32_t ow;
  int32_t oh;

  if (text == NULL) {
    return;
  }

  MetalGfxDrawBounds(&ox, &oy, &ow, &oh);
  x += ox;
  y += oy;
  if (y + PM_METAL_GFX_FONT_H <= oy || y >= oy + oh) {
    return;
  }

  cx = x;
  while (*text != '\0') {
    if (cx + PM_METAL_GFX_FONT_W > ox && cx < ox + ow) {
      MetalGfxGlyph(cx, y, (uint8_t)*text, fg, bg, transparent_bg);
    }

    cx += PM_METAL_GFX_FONT_W;
    text++;
  }
}

uint32_t pm_metal_gfx_font_width(void)
{
  return PM_METAL_GFX_FONT_W;
}

uint32_t pm_metal_gfx_font_height(void)
{
  return PM_METAL_GFX_FONT_H;
}

/**
 * Primary FB present (DEFAULT). Never follows mDrawSurf — callers that want
 * the current tab slot must use present_surface(slot) explicitly.
 * (present_surface(DEFAULT) used to bounce into present() and get hijacked
 * by a leftover tab draw surface, so `run doom` never hit the full LFB.)
 */
static int32_t MetalGfxPresentPrimary(void)
{
  int32_t hx;
  int32_t hy;
  int32_t hw;
  int32_t hh;

  if (MetalGfxBlitHintTake(&hx, &hy, &hw, &hh) != 0) {
    return pm_metal_gfx_present_rect(hx, hy, hw, hh);
  }

  return pm_metal_gfx_present_rect(0, 0, (int32_t)mSurf.width, (int32_t)mSurf.height);
}

int pm_metal_gfx_present(void)
{
  if (mDrawSurf != PM_METAL_GFX_SURFACE_DEFAULT && mDrawSurf != 0) {
    return pm_metal_gfx_present_surface(mDrawSurf);
  }

  return MetalGfxPresentPrimary();
}

int pm_metal_gfx_present_rect(int32_t x, int32_t y, int32_t w, int32_t h)
{
  const pm_metal_scanout_ops_t *ops;
  int32_t                       rc;

  if (!mReady || mSurf.pixels == NULL) {
    return -1;
  }

  if (w <= 0 || h <= 0) {
    return 0;
  }

  if (x < 0) {
    w += x;
    x = 0;
  }

  if (y < 0) {
    h += y;
    y = 0;
  }

  if (x >= (int32_t)mSurf.width || y >= (int32_t)mSurf.height) {
    return 0;
  }

  if (x + w > (int32_t)mSurf.width) {
    w = (int32_t)mSurf.width - x;
  }

  if (y + h > (int32_t)mSurf.height) {
    h = (int32_t)mSurf.height - y;
  }

  if (!MetalGfxPresentTryAcquire()) {
    /* An offloaded async present job owns the backend right now — skip
       this sync redraw rather than race it; caller already ignores rc. */
    return -1;
  }

  mBlitHintValid = 0;
  pm_metal_scanout_bind_set_shadow(mSurf.pixels, mSurf.pitch);

  ops = pm_metal_scanout_ops();
  if (ops == NULL || ops->present_rect == NULL) {
    MetalGfxPresentRelease();
    return -1;
  }

  rc = ops->present_rect(x, y, w, h);
  if (rc == 0 && mDirect != 0 && ops->after_flip != NULL) {
    uint32_t *p;

    p = mSurf.pixels;
    ops->after_flip(&p);
    mSurf.pixels = p;
  }

  MetalGfxPresentRelease();

  /*
   * Count substantial sync presents only (skip cursor / input blinks).
   * Async jobs are counted via pm_metal_async_perf_note_present_frame.
   */
  if (rc == 0 && mSurf.width > 0 && mSurf.height > 0 &&
      ((uint64_t)w * (uint64_t)h) >= ((uint64_t)mSurf.width * (uint64_t)mSurf.height / 4ull)) {
    pm_metal_gfx_note_frame();
  }

  return rc;
}

void pm_metal_gfx_set_surface(pm_metal_gfx_surface_h s)
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

pm_metal_gfx_surface_h pm_metal_gfx_draw_surface(void)
{
  return mDrawSurf;
}

int pm_metal_gfx_width(void)
{
  int32_t ow;

  MetalGfxDrawBounds(NULL, NULL, &ow, NULL);
  return ow;
}

int pm_metal_gfx_height(void)
{
  int32_t oh;

  MetalGfxDrawBounds(NULL, NULL, NULL, &oh);
  return oh;
}

pm_metal_gfx_surface_h pm_metal_gfx_surface_alloc(void)
{
  uint32_t i;

  for (i = 2; i <= PM_METAL_GFX_MAX_SURFACES; i++) {
    if (!mSurfSlots[i].used) {
      memset(&mSurfSlots[i], 0, sizeof(mSurfSlots[i]));
      mSurfSlots[i].used = 1;
      return (pm_metal_gfx_surface_h)i;
    }
  }

  return PM_METAL_GFX_SURFACE_INVALID;
}

void pm_metal_gfx_surface_free(pm_metal_gfx_surface_h s)
{
  if (s < 2 || s > PM_METAL_GFX_MAX_SURFACES) {
    return;
  }

  memset(&mSurfSlots[s], 0, sizeof(mSurfSlots[s]));
}

void pm_metal_gfx_surface_set_rect(
  pm_metal_gfx_surface_h s, int32_t x, int32_t y, int32_t w, int32_t h)
{
  if (s < 2 || s > PM_METAL_GFX_MAX_SURFACES || !mSurfSlots[s].used) {
    return;
  }

  mSurfSlots[s].x = x;
  mSurfSlots[s].y = y;
  mSurfSlots[s].w = w;
  mSurfSlots[s].h = h;
}

int pm_metal_gfx_present_surface(pm_metal_gfx_surface_h s)
{
  int32_t hx;
  int32_t hy;
  int32_t hw;
  int32_t hh;

  if (s == PM_METAL_GFX_SURFACE_DEFAULT || s == 0) {
    return MetalGfxPresentPrimary();
  }

  if (s > PM_METAL_GFX_MAX_SURFACES || !mSurfSlots[s].used) {
    return -1;
  }

  if (mSurfSlots[s].w <= 0 || mSurfSlots[s].h <= 0) {
    return 0;
  }

  /* Same as DEFAULT present(): honor last blit rect when set. */
  if (MetalGfxBlitHintTake(&hx, &hy, &hw, &hh) != 0) {
    return pm_metal_gfx_present_rect(hx, hy, hw, hh);
  }

  return pm_metal_gfx_present_rect(
    mSurfSlots[s].x, mSurfSlots[s].y, mSurfSlots[s].w, mSurfSlots[s].h);
}

static int32_t MetalGfxPresentResolveRect(
  pm_metal_gfx_surface_h s, int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
  int32_t hx;
  int32_t hy;
  int32_t hw;
  int32_t hh;

  if (x == NULL || y == NULL || w == NULL || h == NULL) {
    return -1;
  }

  if (MetalGfxBlitHintTake(&hx, &hy, &hw, &hh) != 0) {
    *x = hx;
    *y = hy;
    *w = hw;
    *h = hh;
    return 0;
  }

  if (s == PM_METAL_GFX_SURFACE_DEFAULT || s == 0) {
    *x = 0;
    *y = 0;
    *w = (int32_t)mSurf.width;
    *h = (int32_t)mSurf.height;
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

int pm_metal_gfx_present_job_begin(pm_metal_gfx_surface_h s)
{
  const pm_metal_scanout_ops_t *ops;
  int32_t                       x;
  int32_t                       y;
  int32_t                       w;
  int32_t                       h;
  int32_t                       jb;
  uint64_t                      t0;

  mJobDone = 1;
  if (!mReady || mSurf.pixels == NULL) {
    return -1;
  }

  if (MetalGfxPresentResolveRect(s, &x, &y, &w, &h) != 0) {
    return -1;
  }

  if (w <= 0 || h <= 0) {
    return 0;
  }

  if (!MetalGfxPresentTryAcquire()) {
    /* Another present job (sync or an offloaded one) owns the backend. */
    return -1;
  }

  mBlitHintValid = 0;
  pm_metal_scanout_bind_set_shadow(mSurf.pixels, mSurf.pitch);
  ops = pm_metal_scanout_ops();
  if (ops == NULL || ops->job_begin == NULL) {
    MetalGfxPresentRelease();
    return -1;
  }

  /* Flip/copy work often finishes in begin — time it (was invisible before). */
  t0 = pm_metal_time_mono_us();
  jb = ops->job_begin(x, y, w, h);
  pm_metal_async_perf_note_present_us(pm_metal_time_mono_us() - t0);
  if (jb < 0) {
    MetalGfxPresentRelease();
    return -1;
  }

  if (jb == 0) {
    mJobDone = 1;
    if (mDirect != 0 && ops->after_flip != NULL) {
      uint32_t *p;

      p = mSurf.pixels;
      ops->after_flip(&p);
      mSurf.pixels = p;
    }

    /* Finished synchronously inside begin — no job_step coming. */
    MetalGfxPresentRelease();
    return 0;
  }

  mJobDone = 0;
  /* Job still running — job_step() releases mPresentBusy once it's done. */
  return 0;
}

int pm_metal_gfx_present_job_step(void)
{
  const pm_metal_scanout_ops_t *ops;
  int32_t                       st;

  if (mJobDone) {
    return 0;
  }

  ops = pm_metal_scanout_ops();
  if (ops == NULL || ops->job_step == NULL) {
    mJobDone = 1;
    MetalGfxPresentRelease();
    return -1;
  }

  st = ops->job_step();
  if (st <= 0) {
    mJobDone = 1;
    if (st == 0 && mDirect != 0 && ops->after_flip != NULL) {
      uint32_t *p;

      p = mSurf.pixels;
      ops->after_flip(&p);
      mSurf.pixels = p;
    }

    MetalGfxPresentRelease();
    return st;
  }

  return 1;
}

int32_t pm_metal_gfx_surface_width(pm_metal_gfx_surface_h s)
{
  if (s == PM_METAL_GFX_SURFACE_DEFAULT || s == 0) {
    return mReady ? (int32_t)mSurf.width : 0;
  }

  if (s > PM_METAL_GFX_MAX_SURFACES || !mSurfSlots[s].used) {
    return 0;
  }

  return mSurfSlots[s].w;
}

int32_t pm_metal_gfx_surface_height(pm_metal_gfx_surface_h s)
{
  if (s == PM_METAL_GFX_SURFACE_DEFAULT || s == 0) {
    return mReady ? (int32_t)mSurf.height : 0;
  }

  if (s > PM_METAL_GFX_MAX_SURFACES || !mSurfSlots[s].used) {
    return 0;
  }

  return mSurfSlots[s].h;
}

int32_t pm_metal_gfx_surface_origin_x(pm_metal_gfx_surface_h s)
{
  if (s == PM_METAL_GFX_SURFACE_DEFAULT || s == 0) {
    return 0;
  }

  if (s > PM_METAL_GFX_MAX_SURFACES || !mSurfSlots[s].used) {
    return 0;
  }

  return mSurfSlots[s].x;
}

int32_t pm_metal_gfx_surface_origin_y(pm_metal_gfx_surface_h s)
{
  if (s == PM_METAL_GFX_SURFACE_DEFAULT || s == 0) {
    return 0;
  }

  if (s > PM_METAL_GFX_MAX_SURFACES || !mSurfSlots[s].used) {
    return 0;
  }

  return mSurfSlots[s].y;
}

int pm_metal_gfx_blit_bgra(int32_t     dx,
                           int32_t     dy,
                           int32_t     dw,
                           int32_t     dh,
                           const void *pixels,
                           int32_t     src_w,
                           int32_t     src_h,
                           int32_t     src_pitch)
{
  int32_t        x;
  int32_t        y;
  const uint8_t *src_base;
  uint64_t       t0;
  int32_t        rc;

  t0 = pm_metal_time_mono_us();

  if (!mReady || mSurf.pixels == NULL || pixels == NULL) {
    return -1;
  }

  if (src_w <= 0 || src_h <= 0 || src_pitch < src_w * 4 || dw <= 0 || dh <= 0) {
    return -1;
  }

  MetalGfxMapGuestRect(&dx, &dy, &dw, &dh);
  if (dw <= 0 || dh <= 0) {
    return 0;
  }

  if (dx < 0) {
    dw += dx;
    dx = 0;
  }

  if (dy < 0) {
    dh += dy;
    dy = 0;
  }

  if (dx >= (int32_t)mSurf.width || dy >= (int32_t)mSurf.height) {
    return 0;
  }

  if (dx + dw > (int32_t)mSurf.width) {
    dw = (int32_t)mSurf.width - dx;
  }

  if (dy + dh > (int32_t)mSurf.height) {
    dh = (int32_t)mSurf.height - dy;
  }

  src_base = (const uint8_t *)pixels;
  rc       = 0;

  /* Integer scale fast path — avoid per-dest-pixel divides. */
  if ((dw % src_w) == 0 && (dh % src_h) == 0 && (dw / src_w) == (dh / src_h)) {
    int32_t scale;

    scale = dw / src_w;
    if (scale == 1) {
      for (y = 0; y < src_h; y++) {
        const uint32_t *srow;
        uint32_t       *drow;

        srow = (const uint32_t *)(src_base + (uintptr_t)y * (uintptr_t)src_pitch);
        drow = &mSurf.pixels[(uint32_t)(dy + y) * mSurf.pitch + (uint32_t)dx];
        memcpy(drow, srow, (uintptr_t)src_w * sizeof(uint32_t));
      }
    } else if (scale > 1) {
      for (y = 0; y < src_h; y++) {
        const uint32_t *srow;
        int32_t         ry;

        srow = (const uint32_t *)(src_base + (uintptr_t)y * (uintptr_t)src_pitch);
        for (ry = 0; ry < scale; ry++) {
          uint32_t *drow;
          int32_t   sx;
          int32_t   out;

          drow = &mSurf.pixels[(uint32_t)(dy + y * scale + ry) * mSurf.pitch + (uint32_t)dx];
          out  = 0;
          for (sx = 0; sx < src_w; sx++) {
            uint32_t px;
            int32_t  rx;

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

    MetalGfxBlitHintSet(dx, dy, dw, dh);
    pm_metal_async_perf_note_blit_us(pm_metal_time_mono_us() - t0);
    return rc;
  }

nearest:
  /*
   * Stretch-fill (non-integer scale). X map rebuilt only when size changes;
   * inner loop is a gather — no per-pixel divide/add.
   */
  {
    uint32_t y_step;
    uint32_t y_acc;
    uint32_t x_step;
    uint32_t x_acc;
    int32_t  xi;

    if (mXMap == NULL || mXMapCap < (uint32_t)dw || mXMapDw != dw || mXMapSw != src_w) {
      if (mXMap != NULL && mXMapCap < (uint32_t)dw) {
        pm_metal_mem_free(mXMap);
        mXMap    = NULL;
        mXMapCap = 0;
      }

      if (mXMap == NULL) {
        mXMap = (uint16_t *)pm_metal_mem_alloc(
          (uintptr_t)dw * sizeof(uint16_t), PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
        if (mXMap == NULL) {
          return -1;
        }

        mXMapCap = (uint32_t)dw;
      }

      x_step = ((uint32_t)src_w << 16) / (uint32_t)dw;
      x_acc  = 0;
      for (xi = 0; xi < dw; xi++) {
        int32_t sx;

        sx = (int32_t)(x_acc >> 16);
        if (sx >= src_w) {
          sx = src_w - 1;
        }

        mXMap[xi] = (uint16_t)sx;
        x_acc += x_step;
      }

      mXMapDw = dw;
      mXMapSw = src_w;
    }

    /*
     * Vertical integer scale: gather each src row once, replicate. Cuts
     * gather work when dh is a multiple of src_h (common-ish on panels).
     */
    if ((dh % src_h) == 0 && (dh / src_h) > 1) {
      int32_t y_rep;

      y_rep = dh / src_h;
      for (y = 0; y < src_h; y++) {
        const uint32_t *srow;
        uint32_t       *drow0;
        int32_t         ry;

        srow  = (const uint32_t *)(src_base + (uintptr_t)y * (uintptr_t)src_pitch);
        drow0 = &mSurf.pixels[(uint32_t)(dy + y * y_rep) * mSurf.pitch + (uint32_t)dx];
        for (x = 0; x < dw; x++) {
          drow0[x] = srow[mXMap[x]];
        }

        for (ry = 1; ry < y_rep; ry++) {
          memcpy(&mSurf.pixels[(uint32_t)(dy + y * y_rep + ry) * mSurf.pitch + (uint32_t)dx],
                 drow0,
                 (uintptr_t)dw * sizeof(uint32_t));
        }
      }
    } else {
      int32_t prev_sy;

      y_step  = ((uint32_t)src_h << 16) / (uint32_t)dh;
      y_acc   = 0;
      prev_sy = -1;
      for (y = 0; y < dh; y++) {
        int32_t         sy;
        const uint32_t *srow;
        uint32_t       *drow;

        sy = (int32_t)(y_acc >> 16);
        y_acc += y_step;
        if (sy >= src_h) {
          sy = src_h - 1;
        }

        drow = &mSurf.pixels[(uint32_t)(dy + y) * mSurf.pitch + (uint32_t)dx];
        if (sy == prev_sy && y > 0) {
          /* Same src row as previous dest — replicate (1080p/320×200). */
          memcpy(drow,
                 &mSurf.pixels[(uint32_t)(dy + y - 1) * mSurf.pitch + (uint32_t)dx],
                 (uintptr_t)dw * sizeof(uint32_t));
          continue;
        }

        srow = (const uint32_t *)(src_base + (uintptr_t)sy * (uintptr_t)src_pitch);
        for (x = 0; x < dw; x++) {
          drow[x] = srow[mXMap[x]];
        }

        prev_sy = sy;
      }
    }
  }

  MetalGfxBlitHintSet(dx, dy, dw, dh);
  pm_metal_async_perf_note_blit_us(pm_metal_time_mono_us() - t0);
  return rc;
}

#include "wasm_export.h"

static int32_t pm_metal_gfx_width_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return (int32_t)pm_metal_gfx_width();
}

static int32_t pm_metal_gfx_height_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return (int32_t)pm_metal_gfx_height();
}

static void pm_metal_gfx_clear_native(wasm_exec_env_t exec_env, uint32_t color)
{
  (void)exec_env;
  pm_metal_gfx_clear((pm_metal_gfx_color_t)color);
}

static void pm_metal_gfx_fill_rect_native(
  wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
  (void)exec_env;
  pm_metal_gfx_fill_rect(x, y, w, h, (pm_metal_gfx_color_t)color);
}

static void pm_metal_gfx_draw_rect_native(
  wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
  (void)exec_env;
  pm_metal_gfx_draw_rect(x, y, w, h, (pm_metal_gfx_color_t)color);
}

static void pm_metal_gfx_bevel_rect_native(wasm_exec_env_t exec_env,
                                           int32_t         x,
                                           int32_t         y,
                                           int32_t         w,
                                           int32_t         h,
                                           int32_t         raised,
                                           uint32_t        hi,
                                           uint32_t        lo)
{
  (void)exec_env;
  pm_metal_gfx_bevel_rect(
    x, y, w, h, (int32_t)raised, (pm_metal_gfx_color_t)hi, (pm_metal_gfx_color_t)lo);
}

static void pm_metal_gfx_draw_text_native(wasm_exec_env_t exec_env,
                                          int32_t         x,
                                          int32_t         y,
                                          const char     *text,
                                          uint32_t        fg,
                                          uint32_t        bg,
                                          int32_t         transparent_bg)
{
  (void)exec_env;
  pm_metal_gfx_draw_text(
    x, y, text, (pm_metal_gfx_color_t)fg, (pm_metal_gfx_color_t)bg, (int32_t)transparent_bg);
}

static uint32_t pm_metal_gfx_font_width_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_gfx_font_width();
}

static uint32_t pm_metal_gfx_font_height_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_gfx_font_height();
}

static uint32_t pm_metal_gfx_fps_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_gfx_fps();
}

static int32_t pm_metal_gfx_present_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return (int32_t)pm_metal_gfx_present();
}

static int32_t pm_metal_gfx_present_rect_native(
  wasm_exec_env_t exec_env, int32_t x, int32_t y, int32_t w, int32_t h)
{
  (void)exec_env;
  return (int32_t)pm_metal_gfx_present_rect(x, y, w, h);
}

static int32_t pm_metal_gfx_blit_bgra_native(wasm_exec_env_t exec_env,
                                             int32_t         dx,
                                             int32_t         dy,
                                             int32_t         dw,
                                             int32_t         dh,
                                             uint32_t        app_ptr,
                                             int32_t         src_w,
                                             int32_t         src_h,
                                             int32_t         src_pitch)
{
  wasm_module_inst_t inst;
  void              *native;

  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL || app_ptr == 0) {
    return -1;
  }

  if (src_w <= 0 || src_h <= 0 || src_pitch < src_w * 4) {
    return -1;
  }

  if (!wasm_runtime_validate_app_addr(inst, app_ptr, (uint64_t)src_pitch * (uint64_t)src_h)) {
    return -1;
  }

  native = wasm_runtime_addr_app_to_native(inst, app_ptr);
  if (native == NULL) {
    return -1;
  }

  return (int32_t)pm_metal_gfx_blit_bgra(dx, dy, dw, dh, native, src_w, src_h, src_pitch);
}

static int32_t pm_metal_gfx_surface_width_native(wasm_exec_env_t exec_env, uint32_t s)
{
  (void)exec_env;
  return pm_metal_gfx_surface_width(s);
}

static int32_t pm_metal_gfx_surface_height_native(wasm_exec_env_t exec_env, uint32_t s)
{
  (void)exec_env;
  return pm_metal_gfx_surface_height(s);
}

static int32_t pm_metal_gfx_surface_origin_x_native(wasm_exec_env_t exec_env, uint32_t s)
{
  (void)exec_env;
  return pm_metal_gfx_surface_origin_x(s);
}

static int32_t pm_metal_gfx_surface_origin_y_native(wasm_exec_env_t exec_env, uint32_t s)
{
  (void)exec_env;
  return pm_metal_gfx_surface_origin_y(s);
}

static void pm_metal_gfx_set_surface_native(wasm_exec_env_t exec_env, uint32_t s)
{
  (void)exec_env;
  pm_metal_gfx_set_surface(s);
}

static uint32_t pm_metal_gfx_draw_surface_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_gfx_draw_surface();
}

static NativeSymbol g_pm_metal_gfx_native_symbols[] = {
  { "pm_metal_gfx_width", (void *)pm_metal_gfx_width_native, "()i", NULL },
  { "pm_metal_gfx_height", (void *)pm_metal_gfx_height_native, "()i", NULL },
  { "pm_metal_gfx_set_surface", (void *)pm_metal_gfx_set_surface_native, "(i)", NULL },
  { "pm_metal_gfx_draw_surface", (void *)pm_metal_gfx_draw_surface_native, "()i", NULL },
  { "pm_metal_gfx_surface_width", (void *)pm_metal_gfx_surface_width_native, "(i)i", NULL },
  { "pm_metal_gfx_surface_height", (void *)pm_metal_gfx_surface_height_native, "(i)i", NULL },
  { "pm_metal_gfx_surface_origin_x", (void *)pm_metal_gfx_surface_origin_x_native, "(i)i", NULL },
  { "pm_metal_gfx_surface_origin_y", (void *)pm_metal_gfx_surface_origin_y_native, "(i)i", NULL },
  { "pm_metal_gfx_clear", (void *)pm_metal_gfx_clear_native, "(i)", NULL },
  { "pm_metal_gfx_fill_rect", (void *)pm_metal_gfx_fill_rect_native, "(iiiii)", NULL },
  { "pm_metal_gfx_draw_rect", (void *)pm_metal_gfx_draw_rect_native, "(iiiii)", NULL },
  { "pm_metal_gfx_bevel_rect", (void *)pm_metal_gfx_bevel_rect_native, "(iiiiiii)", NULL },
  { "pm_metal_gfx_draw_text", (void *)pm_metal_gfx_draw_text_native, "(ii$iii)", NULL },
  { "pm_metal_gfx_font_width", (void *)pm_metal_gfx_font_width_native, "()i", NULL },
  { "pm_metal_gfx_font_height", (void *)pm_metal_gfx_font_height_native, "()i", NULL },
  { "pm_metal_gfx_fps", (void *)pm_metal_gfx_fps_native, "()i", NULL },
  { "pm_metal_gfx_present", (void *)pm_metal_gfx_present_native, "()i", NULL },
  { "pm_metal_gfx_present_rect", (void *)pm_metal_gfx_present_rect_native, "(iiii)i", NULL },
  { "pm_metal_gfx_blit_bgra", (void *)pm_metal_gfx_blit_bgra_native, "(iiiiiiii)i", NULL },
};

int pm_metal_gfx_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_GFX_WASI_MODULE,
                                     g_pm_metal_gfx_native_symbols,
                                     sizeof(g_pm_metal_gfx_native_symbols) /
                                       sizeof(g_pm_metal_gfx_native_symbols[0]))) {
    return -1;
  }

  return 0;
}
