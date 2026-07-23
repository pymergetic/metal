/** @file
  Bochs/QEMU stdvga scanout — VBE virt_h page-flip + vblank.
**/
#include <pymergetic/metal/dev/gfx/scanout.h>
#include "../../bus/pci/pci.h"

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/IoLib.h>

#define VBE_DISPI_IOPORT_INDEX  0x01CEu
#define VBE_DISPI_IOPORT_DATA   0x01CFu
#define VBE_DISPI_INDEX_ID           0x0u
#define VBE_DISPI_INDEX_XRES         0x1u
#define VBE_DISPI_INDEX_YRES         0x2u
#define VBE_DISPI_INDEX_ENABLE       0x4u
#define VBE_DISPI_INDEX_VIRT_WIDTH   0x6u
#define VBE_DISPI_INDEX_VIRT_HEIGHT  0x7u
#define VBE_DISPI_INDEX_Y_OFFSET     0x9u
#define VBE_DISPI_ENABLED            0x01u
#define VBE_DISPI_LFB_ENABLED        0x40u
#define VBE_DISPI_NOCLEARMEM         0x80u
#define VBE_DISPI_ID0                0xB0C0u

STATIC INT32   mArmed;
STATIC UINT32  mFront;
STATIC UINT32  mPagePx;

STATIC
VOID
BochsVbeWrite (
  UINT16  index,
  UINT16  value
  )
{
  IoWrite16 (VBE_DISPI_IOPORT_INDEX, index);
  IoWrite16 (VBE_DISPI_IOPORT_DATA, value);
}

STATIC
UINT16
BochsVbeRead (
  UINT16  index
  )
{
  IoWrite16 (VBE_DISPI_IOPORT_INDEX, index);
  return IoRead16 (VBE_DISPI_IOPORT_DATA);
}

STATIC
INT32
BochsProbe (
  CONST pm_metal_scanout_bind_t  *b
  )
{
  UINT8   bus;
  UINT8   dev;
  UINT8   func;
  UINT64  bar;
  UINT16  id;
  UINT16  xres;
  UINT16  yres;
  UINT16  virt_h;
  UINT16  got_h;
  UINT32  page_bytes;

  mArmed = 0;
  mFront = 0;
  if (b == NULL || b->fb == NULL || b->mode_w == 0 || b->mode_h == 0) {
    return -1;
  }

  if (pm_bios_pci_find (0x1234, 0x1111, &bus, &dev, &func) != 0) {
    return -1;
  }

  bar = pm_bios_pci_bar_mmio (bus, dev, func, 0, NULL);
  if (bar == 0 || (UINT32 *)(UINTN)bar != b->fb) {
    return -1;
  }

  BochsVbeWrite (VBE_DISPI_INDEX_ID, 0xB0C5);
  id = BochsVbeRead (VBE_DISPI_INDEX_ID);
  if (id < VBE_DISPI_ID0 || id > (VBE_DISPI_ID0 + 6u)) {
    return -1;
  }

  xres = BochsVbeRead (VBE_DISPI_INDEX_XRES);
  yres = BochsVbeRead (VBE_DISPI_INDEX_YRES);
  if (xres == 0 || yres == 0) {
    xres = (UINT16)b->mode_w;
    yres = (UINT16)b->mode_h;
  }

  if ((UINT32)xres != b->mode_w || (UINT32)yres != b->mode_h) {
    return -1;
  }

  page_bytes = b->mode_h * b->fb_ppsl * sizeof (UINT32);
  if (page_bytes == 0 || (page_bytes * 2u) > (64u * 1024u * 1024u)) {
    return -1;
  }

  virt_h = (UINT16)(b->mode_h * 2u);
  BochsVbeWrite (VBE_DISPI_INDEX_VIRT_WIDTH, (UINT16)b->fb_ppsl);
  BochsVbeWrite (VBE_DISPI_INDEX_VIRT_HEIGHT, virt_h);
  BochsVbeWrite (VBE_DISPI_INDEX_Y_OFFSET, 0);
  BochsVbeWrite (
    VBE_DISPI_INDEX_ENABLE,
    (UINT16)(VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED | VBE_DISPI_NOCLEARMEM)
    );
  got_h = BochsVbeRead (VBE_DISPI_INDEX_VIRT_HEIGHT);
  if (got_h < virt_h) {
    return -1;
  }

  CopyMem (b->fb + (UINTN)b->mode_h * (UINTN)b->fb_ppsl, b->fb, page_bytes);
  mPagePx = b->mode_h * b->fb_ppsl;
  mArmed  = 1;
  mFront  = 0;
  return 0;
}

STATIC
INT32
BochsPresentRect (
  INT32  x,
  INT32  y,
  INT32  w,
  INT32  h
  )
{
  CONST pm_metal_scanout_bind_t  *b;
  UINT32                          back;
  UINT32                         *back_base;
  UINT32                         *front_base;
  INT32                           full;

  b = pm_metal_scanout_bind_info ();
  if (!mArmed || b == NULL || b->fb == NULL) {
    return -1;
  }

  back       = 1u - mFront;
  back_base  = b->fb + (UINTN)back * (UINTN)mPagePx;
  front_base = b->fb + (UINTN)mFront * (UINTN)mPagePx;
  full       = (x == 0 && y == 0 && w == (INT32)b->shadow_w
                && h == (INT32)b->shadow_h) ? 1 : 0;

  /* DIRECT: compositor already draws into back — skip shadow→VRAM copy. */
  if (b->shadow != back_base) {
    if (full == 0) {
      CopyMem (back_base, front_base, (UINTN)mPagePx * sizeof (UINT32));
    }

    pm_metal_scanout_copy_rect (back_base, b->fb_ppsl, x, y, w, h, b);
  }

  MemoryFence ();

  BochsVbeWrite (VBE_DISPI_INDEX_Y_OFFSET, (UINT16)(back * b->mode_h));
  mFront = back;
  return 0;
}

STATIC
INT32
BochsJobBegin (
  INT32  x,
  INT32  y,
  INT32  w,
  INT32  h
  )
{
  return (BochsPresentRect (x, y, w, h) == 0) ? 0 : -1;
}

STATIC
INT32
BochsJobStep (
  VOID
  )
{
  return 0;
}

STATIC
UINT32
BochsCaps (
  VOID
  )
{
  return PM_METAL_SCANOUT_CAP_TEAR_FREE | PM_METAL_SCANOUT_CAP_DIRECT;
}

STATIC
INT32
BochsAdoptShadow (
  UINT32  **pixels,
  UINT32   *pitch
  )
{
  CONST pm_metal_scanout_bind_t  *b;
  UINT32                         *back;

  b = pm_metal_scanout_bind_info ();
  if (!mArmed || b == NULL || b->fb == NULL || pixels == NULL) {
    return -1;
  }

  back = b->fb + (UINTN)(1u - mFront) * (UINTN)mPagePx;
  *pixels = back;
  if (pitch != NULL) {
    *pitch = b->fb_ppsl;
  }

  pm_metal_scanout_bind_set_shadow (back, b->fb_ppsl);
  return 0;
}

STATIC
VOID
BochsAfterFlip (
  UINT32  **pixels
  )
{
  CONST pm_metal_scanout_bind_t  *b;
  UINT32                         *back;

  b = pm_metal_scanout_bind_info ();
  if (!mArmed || b == NULL || b->fb == NULL || pixels == NULL) {
    return;
  }

  back     = b->fb + (UINTN)(1u - mFront) * (UINTN)mPagePx;
  *pixels  = back;
  pm_metal_scanout_bind_set_shadow (back, b->fb_ppsl);
}

STATIC
VOID
BochsFini (
  VOID
  )
{
  mArmed = 0;
}

CONST pm_metal_scanout_ops_t  g_pm_metal_scanout_bochs = {
  "bochs_flip",
  BochsProbe,
  BochsPresentRect,
  BochsJobBegin,
  BochsJobStep,
  BochsCaps,
  BochsAdoptShadow,
  BochsAfterFlip,
  BochsFini
};
