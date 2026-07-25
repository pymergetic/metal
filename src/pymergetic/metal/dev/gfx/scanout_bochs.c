/** @file
  Bochs/QEMU stdvga scanout — VBE virt_h page-flip + vblank.
**/
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/dev/gfx/scanout.h>
#include <runtime/io/io.h>
#include <runtime/slot/spin.h>
#include "../../bus/pci/pci.h"

#define VBE_DISPI_IOPORT_INDEX      0x01CEu
#define VBE_DISPI_IOPORT_DATA       0x01CFu
#define VBE_DISPI_INDEX_ID          0x0u
#define VBE_DISPI_INDEX_XRES        0x1u
#define VBE_DISPI_INDEX_YRES        0x2u
#define VBE_DISPI_INDEX_ENABLE      0x4u
#define VBE_DISPI_INDEX_VIRT_WIDTH  0x6u
#define VBE_DISPI_INDEX_VIRT_HEIGHT 0x7u
#define VBE_DISPI_INDEX_Y_OFFSET    0x9u
#define VBE_DISPI_ENABLED           0x01u
#define VBE_DISPI_LFB_ENABLED       0x40u
#define VBE_DISPI_NOCLEARMEM        0x80u
#define VBE_DISPI_ID0               0xB0C0u

static int32_t  mArmed;
static uint32_t mFront;
static uint32_t mPagePx;

static void BochsVbeWrite(uint16_t index, uint16_t value)
{
  pm_metal_io_out16(VBE_DISPI_IOPORT_INDEX, index);
  pm_metal_io_out16(VBE_DISPI_IOPORT_DATA, value);
}

static uint16_t BochsVbeRead(uint16_t index)
{
  pm_metal_io_out16(VBE_DISPI_IOPORT_INDEX, index);
  return pm_metal_io_in16(VBE_DISPI_IOPORT_DATA);
}

static int32_t BochsProbe(const pm_metal_scanout_bind_t *b)
{
  uint8_t  bus;
  uint8_t  dev;
  uint8_t  func;
  uint64_t bar;
  uint16_t id;
  uint16_t xres;
  uint16_t yres;
  uint16_t virt_h;
  uint16_t got_h;
  uint32_t page_bytes;

  mArmed = 0;
  mFront = 0;
  if (b == NULL || b->fb == NULL || b->mode_w == 0 || b->mode_h == 0) {
    return -1;
  }

  if (pm_bios_pci_find(0x1234, 0x1111, &bus, &dev, &func) != 0) {
    return -1;
  }

  bar = pm_bios_pci_bar_mmio(bus, dev, func, 0, NULL);
  if (bar == 0 || (uint32_t *)(uintptr_t)bar != b->fb) {
    return -1;
  }

  BochsVbeWrite(VBE_DISPI_INDEX_ID, 0xB0C5);
  id = BochsVbeRead(VBE_DISPI_INDEX_ID);
  if (id < VBE_DISPI_ID0 || id > (VBE_DISPI_ID0 + 6u)) {
    return -1;
  }

  xres = BochsVbeRead(VBE_DISPI_INDEX_XRES);
  yres = BochsVbeRead(VBE_DISPI_INDEX_YRES);
  if (xres == 0 || yres == 0) {
    xres = (uint16_t)b->mode_w;
    yres = (uint16_t)b->mode_h;
  }

  if ((uint32_t)xres != b->mode_w || (uint32_t)yres != b->mode_h) {
    return -1;
  }

  page_bytes = b->mode_h * b->fb_ppsl * sizeof(uint32_t);
  if (page_bytes == 0 || (page_bytes * 2u) > (64u * 1024u * 1024u)) {
    return -1;
  }

  virt_h = (uint16_t)(b->mode_h * 2u);
  BochsVbeWrite(VBE_DISPI_INDEX_VIRT_WIDTH, (uint16_t)b->fb_ppsl);
  BochsVbeWrite(VBE_DISPI_INDEX_VIRT_HEIGHT, virt_h);
  BochsVbeWrite(VBE_DISPI_INDEX_Y_OFFSET, 0);
  BochsVbeWrite(VBE_DISPI_INDEX_ENABLE,
                (uint16_t)(VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED | VBE_DISPI_NOCLEARMEM));
  got_h = BochsVbeRead(VBE_DISPI_INDEX_VIRT_HEIGHT);
  if (got_h < virt_h) {
    return -1;
  }

  memcpy(b->fb + (uintptr_t)b->mode_h * (uintptr_t)b->fb_ppsl, b->fb, page_bytes);
  mPagePx = b->mode_h * b->fb_ppsl;
  mArmed  = 1;
  mFront  = 0;
  return 0;
}

/*
 * Preserve undirty pixels on the back page from the current front.
 * Old path did a full-page memcpy (~4 MiB @ 1280x800) on every partial
 * present — that alone spiked present_us to 10–14 ms under QEMU/VNC and
 * collapsed Doom's 35 Hz lock. Copy only the bands outside (x,y,w,h).
 */
static void BochsCopyOutside(uint32_t       *back,
                             const uint32_t *front,
                             uint32_t        pitch,
                             uint32_t        page_h,
                             int32_t         x,
                             int32_t         y,
                             int32_t         w,
                             int32_t         h)
{
  uint32_t  row;
  uint32_t  y0;
  uint32_t  y1;
  uintptr_t row_bytes;
  uintptr_t left_bytes;
  uintptr_t right_off;
  uintptr_t right_bytes;

  if (back == NULL || front == NULL || pitch == 0 || page_h == 0) {
    return;
  }

  if (x < 0) {
    w += x;
    x = 0;
  }

  if (y < 0) {
    h += y;
    y = 0;
  }

  if (w <= 0 || h <= 0 || (uint32_t)x >= pitch || (uint32_t)y >= page_h) {
    memcpy(back, front, (uintptr_t)page_h * (uintptr_t)pitch * sizeof(uint32_t));
    return;
  }

  if ((uint32_t)x + (uint32_t)w > pitch) {
    w = (int32_t)(pitch - (uint32_t)x);
  }

  if ((uint32_t)y + (uint32_t)h > page_h) {
    h = (int32_t)(page_h - (uint32_t)y);
  }

  y0        = (uint32_t)y;
  y1        = (uint32_t)y + (uint32_t)h;
  row_bytes = (uintptr_t)pitch * sizeof(uint32_t);

  if (y0 > 0) {
    memcpy(back, front, (uintptr_t)y0 * row_bytes);
  }

  if (y1 < page_h) {
    memcpy(back + (uintptr_t)y1 * (uintptr_t)pitch,
           front + (uintptr_t)y1 * (uintptr_t)pitch,
           (uintptr_t)(page_h - y1) * row_bytes);
  }

  left_bytes = (uintptr_t)x * sizeof(uint32_t);
  right_off  = (uintptr_t)x + (uintptr_t)w;
  right_bytes =
    (right_off < pitch) ? (uintptr_t)(pitch - (uint32_t)right_off) * sizeof(uint32_t) : 0;

  for (row = y0; row < y1; row++) {
    uint32_t       *db;
    const uint32_t *sb;

    db = back + (uintptr_t)row * (uintptr_t)pitch;
    sb = front + (uintptr_t)row * (uintptr_t)pitch;
    if (left_bytes > 0) {
      memcpy(db, sb, left_bytes);
    }

    if (right_bytes > 0) {
      memcpy(db + right_off, sb + right_off, right_bytes);
    }
  }
}

static int32_t BochsPresentRect(int32_t x, int32_t y, int32_t w, int32_t h)
{
  const pm_metal_scanout_bind_t *b;
  uint32_t                       back;
  uint32_t                      *back_base;
  uint32_t                      *front_base;
  int32_t                        full;
  uint32_t                       page_h;

  b = pm_metal_scanout_bind_info();
  if (!mArmed || b == NULL || b->fb == NULL) {
    return -1;
  }

  back       = 1u - mFront;
  back_base  = b->fb + (uintptr_t)back * (uintptr_t)mPagePx;
  front_base = b->fb + (uintptr_t)mFront * (uintptr_t)mPagePx;
  page_h     = (b->fb_ppsl != 0) ? (mPagePx / b->fb_ppsl) : 0;
  full       = (x == 0 && y == 0 && w == (int32_t)b->shadow_w && h == (int32_t)b->shadow_h) ? 1 : 0;

  /* DIRECT: compositor already draws into back — skip shadow→VRAM copy. */
  if (b->shadow != back_base) {
    if (full == 0) {
      BochsCopyOutside(back_base, front_base, b->fb_ppsl, page_h, x, y, w, h);
    }

    pm_metal_scanout_copy_rect(back_base, b->fb_ppsl, x, y, w, h, b);
  }

  pm_metal_mem_fence();

  BochsVbeWrite(VBE_DISPI_INDEX_Y_OFFSET, (uint16_t)(back * b->mode_h));
  mFront = back;
  return 0;
}

static int32_t BochsJobBegin(int32_t x, int32_t y, int32_t w, int32_t h)
{
  return (BochsPresentRect(x, y, w, h) == 0) ? 0 : -1;
}

static int32_t BochsJobStep(void)
{
  return 0;
}

static uint32_t BochsCaps(void)
{
  return PM_METAL_SCANOUT_CAP_TEAR_FREE | PM_METAL_SCANOUT_CAP_DIRECT;
}

static int32_t BochsAdoptShadow(uint32_t **pixels, uint32_t *pitch)
{
  const pm_metal_scanout_bind_t *b;
  uint32_t                      *back;

  b = pm_metal_scanout_bind_info();
  if (!mArmed || b == NULL || b->fb == NULL || pixels == NULL) {
    return -1;
  }

  back    = b->fb + (uintptr_t)(1u - mFront) * (uintptr_t)mPagePx;
  *pixels = back;
  if (pitch != NULL) {
    *pitch = b->fb_ppsl;
  }

  pm_metal_scanout_bind_set_shadow(back, b->fb_ppsl);
  return 0;
}

static void BochsAfterFlip(uint32_t **pixels)
{
  const pm_metal_scanout_bind_t *b;
  uint32_t                      *back;

  b = pm_metal_scanout_bind_info();
  if (!mArmed || b == NULL || b->fb == NULL || pixels == NULL) {
    return;
  }

  back    = b->fb + (uintptr_t)(1u - mFront) * (uintptr_t)mPagePx;
  *pixels = back;
  pm_metal_scanout_bind_set_shadow(back, b->fb_ppsl);
}

static void BochsFini(void)
{
  mArmed = 0;
}

const pm_metal_scanout_ops_t g_pm_metal_scanout_bochs = {
  "bochs_flip", BochsProbe,       BochsPresentRect, BochsJobBegin, BochsJobStep,
  BochsCaps,    BochsAdoptShadow, BochsAfterFlip,   BochsFini
};
