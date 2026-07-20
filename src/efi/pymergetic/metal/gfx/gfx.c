/** @file
  Graphics — GOP shadow framebuffer + Blt present. (impl: efi)
**/
#include <pymergetic/metal/gfx.h>
#include <mem/mem.h>

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>

#include "font_vga8x16.inc.c"

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL  *mGop;
STATIC pm_metal_gfx_surface_t         mSurf;
STATIC INT32                          mReady;
/* Tight row-pack scratch for sub-rect Blt (some GOP/QEMU ignore Delta). */
STATIC UINT32                        *mPack;
STATIC UINT32                         mPackCap;

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
pm_metal_gfx_init (
  VOID
  )
{
  EFI_STATUS                           Status;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info;
  UINT32                               W;
  UINT32                               H;
  UINT32                               Pitch;
  UINTN                                Bytes;

  if (mReady) {
    return 0;
  }

  if (gBS == NULL) {
    return -1;
  }

  Status = gBS->LocateProtocol (
                  &gEfiGraphicsOutputProtocolGuid,
                  NULL,
                  (VOID **)&mGop
                  );
  if (EFI_ERROR (Status) || mGop == NULL || mGop->Mode == NULL) {
    return -1;
  }

  Info = mGop->Mode->Info;
  if (Info == NULL) {
    return -1;
  }

  W = Info->HorizontalResolution;
  H = Info->VerticalResolution;
  if (W < 320 || H < 200) {
    return -1;
  }

  Pitch = W;
  Bytes = (UINTN)Pitch * (UINTN)H * sizeof (UINT32);
  mSurf.pixels = (UINT32 *)pm_metal_mem_alloc (
                             Bytes,
                             PM_METAL_MEM_HEAP,
                             PM_METAL_MEM_ID_NONE
                             );
  if (mSurf.pixels == NULL) {
    return -1;
  }

  ZeroMem (mSurf.pixels, Bytes);
  mSurf.width  = W;
  mSurf.height = H;
  mSurf.pitch  = Pitch;
  mReady       = 1;

  pm_metal_gfx_clear (PM_METAL_GFX_RGB (0x4a, 0x4a, 0x4a));
  (VOID)pm_metal_gfx_present ();
  return 0;
}

void
pm_metal_gfx_fini (
  VOID
  )
{
  if (mSurf.pixels != NULL) {
    pm_metal_mem_free (mSurf.pixels);
    mSurf.pixels = NULL;
  }

  if (mPack != NULL) {
    pm_metal_mem_free (mPack);
    mPack    = NULL;
    mPackCap = 0;
  }

  mSurf.width  = 0;
  mSurf.height = 0;
  mSurf.pitch  = 0;
  mGop         = NULL;
  mReady       = 0;
}

int
pm_metal_gfx_ready (
  VOID
  )
{
  return mReady ? 1 : 0;
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
  MetalGfxFillClipped (0, 0, (INT32)mSurf.width, (INT32)mSurf.height, color);
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

  if (text == NULL) {
    return;
  }

  cx = x;
  while (*text != '\0') {
    MetalGfxGlyph (cx, y, (UINT8)*text, fg, bg, transparent_bg);
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

int
pm_metal_gfx_present (
  VOID
  )
{
  return pm_metal_gfx_present_rect (0, 0, (INT32)mSurf.width, (INT32)mSurf.height);
}

int
pm_metal_gfx_present_rect (
  INT32  x,
  INT32  y,
  INT32  w,
  INT32  h
  )
{
  EFI_STATUS  Status;

  if (!mReady || mGop == NULL || mSurf.pixels == NULL) {
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

  /*
   * Full-width rows: Blt in place with Delta = pitch.
   * Sub-rects: pack into a tight buffer and Blt with Delta=0 — QEMU/OVMF GOP
   * often corrupts the top of the dest when SrcX!=0 / Delta=stride.
   */
  if (x == 0 && (UINT32)w == mSurf.width) {
    Status = mGop->Blt (
                     mGop,
                     (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)mSurf.pixels,
                     EfiBltBufferToVideo,
                     0,
                     (UINTN)y,
                     0,
                     (UINTN)y,
                     (UINTN)w,
                     (UINTN)h,
                     (UINTN)mSurf.pitch * sizeof (UINT32)
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
        &mSurf.pixels[(UINT32)(y + row) * mSurf.pitch + (UINT32)x],
        (UINTN)w * sizeof (UINT32)
        );
    }

    Status = mGop->Blt (
                     mGop,
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

int
pm_metal_gfx_width (
  VOID
  )
{
  return mReady ? (INT32)mSurf.width : 0;
}

int
pm_metal_gfx_height (
  VOID
  )
{
  return mReady ? (INT32)mSurf.height : 0;
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

  if (!mReady || mSurf.pixels == NULL || pixels == NULL) {
    return -1;
  }

  if (src_w <= 0 || src_h <= 0 || src_pitch < src_w * 4 || dw <= 0 || dh <= 0) {
    return -1;
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

  /* Integer scale fast path (doom 2× etc.) — avoid per-dest-pixel divides. */
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

          drow = &mSurf.pixels[(UINT32)(dy + y * scale + ry) * mSurf.pitch
                               + (UINT32)dx];
          for (sx = 0; sx < src_w; sx++) {
            UINT32  px;
            INT32   rx;

            px = srow[sx];
            for (rx = 0; rx < scale; rx++) {
              drow[sx * scale + rx] = px;
            }
          }
        }
      }
    } else {
      goto nearest;
    }

    return pm_metal_gfx_present_rect (dx, dy, dw, dh);
  }

nearest:
  for (y = 0; y < dh; y++) {
    INT32         sy;
    CONST UINT32 *srow;
    UINT32       *drow;

    sy   = (y * src_h) / dh;
    srow = (CONST UINT32 *)(src_base + (UINTN)sy * (UINTN)src_pitch);
    drow = &mSurf.pixels[(UINT32)(dy + y) * mSurf.pitch + (UINT32)dx];
    for (x = 0; x < dw; x++) {
      INT32  sx;

      sx      = (x * src_w) / dw;
      drow[x] = srow[sx];
    }
  }

  return pm_metal_gfx_present_rect (dx, dy, dw, dh);
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

STATIC NativeSymbol g_pm_metal_gfx_native_symbols[] = {
  { "pm_metal_gfx_width", (VOID *)pm_metal_gfx_width_native, "()i", NULL },
  { "pm_metal_gfx_height", (VOID *)pm_metal_gfx_height_native, "()i", NULL },
  { "pm_metal_gfx_clear", (VOID *)pm_metal_gfx_clear_native, "(i)", NULL },
  { "pm_metal_gfx_fill_rect", (VOID *)pm_metal_gfx_fill_rect_native, "(iiiii)", NULL },
  { "pm_metal_gfx_draw_rect", (VOID *)pm_metal_gfx_draw_rect_native, "(iiiii)", NULL },
  { "pm_metal_gfx_bevel_rect", (VOID *)pm_metal_gfx_bevel_rect_native, "(iiiiiii)", NULL },
  { "pm_metal_gfx_draw_text", (VOID *)pm_metal_gfx_draw_text_native, "(ii$iii)", NULL },
  { "pm_metal_gfx_font_width", (VOID *)pm_metal_gfx_font_width_native, "()i", NULL },
  { "pm_metal_gfx_font_height", (VOID *)pm_metal_gfx_font_height_native, "()i", NULL },
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
