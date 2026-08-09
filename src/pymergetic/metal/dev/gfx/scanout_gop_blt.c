/** @file
  Pre-EBS EFI GOP Blt scanout (Boot Services still live).
**/
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/dev/gfx/scanout.h>
#include "pymergetic/metal/boot/platform/gop.h"
#include "pymergetic/metal/dev/gfx/compat.h"
#include "pymergetic/metal/mem.h"


static uint32_t *mPack;
static uint32_t  mPackCap;

static int32_t GopProbe(const pm_metal_scanout_bind_t *b)
{
  if (b == NULL || b->gop == NULL || b->owned) {
    return -1;
  }

  return 0;
}

static int32_t GopPresentRect(int32_t x, int32_t y, int32_t w, int32_t h)
{
  const pm_metal_scanout_bind_t *b;

  b = pm_metal_scanout_bind_info();
  if (b == NULL || b->gop == NULL || b->shadow == NULL) {
    return -1;
  }

  if (x == 0 && (uint32_t)w == b->shadow_w) {
    return pm_metal_boot_gop_port_blt(b->gop,
                                 b->shadow,
                                 0,
                                 (uint32_t)y,
                                 0,
                                 (uint32_t)y,
                                 (uint32_t)w,
                                 (uint32_t)h,
                                 b->shadow_pitch * (uint32_t)sizeof(uint32_t));
  }

  {
    uint32_t need;
    int32_t  row;

    need = (uint32_t)w * (uint32_t)h;
    if (mPack == NULL || mPackCap < need) {
      if (mPack != NULL) {
        pm_metal_mem_free((uint8_t *)mPack);
        mPack    = NULL;
        mPackCap = 0;
      }

      mPack = (uint32_t *)pm_metal_mem_alloc(
        (uintptr_t)need * sizeof(uint32_t));
      if (mPack == NULL) {
        return -1;
      }

      mPackCap = need;
    }

    for (row = 0; row < h; row++) {
      memcpy(&mPack[(uint32_t)row * (uint32_t)w],
             &b->shadow[(uint32_t)(y + row) * b->shadow_pitch + (uint32_t)x],
             (uintptr_t)w * sizeof(uint32_t));
    }

    return pm_metal_boot_gop_port_blt(
      b->gop, mPack, 0, 0, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, 0);
  }
}

static int32_t GopJobBegin(int32_t x, int32_t y, int32_t w, int32_t h)
{
  return (GopPresentRect(x, y, w, h) == 0) ? 0 : -1;
}

static int32_t GopJobStep(void)
{
  return 0;
}

static uint32_t GopCaps(void)
{
  return 0;
}

static void GopFini(void)
{
  if (mPack != NULL) {
    pm_metal_mem_free((uint8_t *)mPack);
    mPack    = NULL;
    mPackCap = 0;
  }
}

const pm_metal_scanout_ops_t g_pm_metal_scanout_gop_blt = { "gop_blt",   GopProbe,   GopPresentRect,
                                                            GopJobBegin, GopJobStep, GopCaps,
                                                            NULL,        NULL,       GopFini };
