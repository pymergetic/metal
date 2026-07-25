/** @file
  Sample i915 scanout — ThinkPad T42 Intel 855GM (PCI 8086:3582) only.

  Reference Gen2 path (GGTT + ring XY_SRC_COPY_BLT + DSPAADDR flip). Kept as
  a worked example; production iron for T43 is radeon_rv370 (1002:5460).
  Probe fails on every other GPU → fall through.
**/
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/dev/gfx/scanout.h>
#include <runtime/slot/spin.h>
#include <runtime/time/cpu.h>
#include "../../bus/pci/pci.h"

#define I915_855GM_VENDOR 0x8086u
#define I915_855GM_DEVICE 0x3582u

#define I915_PGTBL_CTL  0x02020u
#define I915_PRB0_TAIL  0x02030u
#define I915_PRB0_HEAD  0x02034u
#define I915_PRB0_START 0x02038u
#define I915_PRB0_CTL   0x0203cu
#define I915_GFX_FLSH   0x02170u
#define I915_DSPACNTR   0x70180u
#define I915_DSPAADDR   0x70184u
#define I915_DSPASTRIDE 0x70188u

#define I915_RING_VALID    0x00000001u
#define I915_RING_NR_PAGES 0x001FF000u
#define I915_PTE_VALID     0x00000001u

#define I915_MI_NOOP  0x00000000u
#define I915_MI_FLUSH 0x02000000u

#define I915_XY_SRC_COPY_BLT             ((2u << 29) | (0x53u << 22) | 6u)
#define I915_XY_SRC_COPY_BLT_WRITE_ALPHA (1u << 21)
#define I915_XY_SRC_COPY_BLT_WRITE_RGB   (1u << 20)

#define I915_RING_SIZE  16384u
#define I915_DSP_ENABLE (1u << 31)

static int32_t        mReady;
static uint8_t       *mMmio;
static uint32_t      *mGtt;
static uint32_t       mGttEntries;
static uint64_t       mAperture;
static uint64_t       mApertureSz;
static uint32_t       mPagePx;
static uint32_t       mPageBytes;
static uint32_t       mFrontGtt;
static uint32_t       mBackGtt;
static uint32_t       mRingGtt;
static uint32_t      *mRing;
static uint32_t       mRingTail;
static uint32_t       mPitchBytes;
static uint32_t       mShadowGtt;
static uint32_t       mShadowPages;
static const uint8_t *mShadowMapped;
static int32_t        mJobLive;
static int32_t        mJobFlipPending;
static uint32_t       mJobExpectTail;

static uint32_t I915Read(uint32_t off)
{
  return *(volatile uint32_t *)(uintptr_t)(mMmio + off);
}

static void I915Write(uint32_t off, uint32_t val)
{
  *(volatile uint32_t *)(uintptr_t)(mMmio + off) = val;
  pm_metal_mem_fence();
}

static uint64_t I915BarSize(uint8_t bus, uint8_t dev, uint8_t func, uint8_t bar_index)
{
  uint8_t  off;
  uint32_t lo;
  uint32_t hi;
  uint32_t save_lo;
  uint32_t save_hi;
  uint64_t size;
  int32_t  is64;

  off     = (uint8_t)(0x10u + bar_index * 4u);
  save_lo = pm_bios_pci_read32(bus, dev, func, off);
  if ((save_lo & 1u) != 0) {
    return 0;
  }

  is64    = (((save_lo >> 1) & 3u) == 2) ? 1 : 0;
  save_hi = 0;
  if (is64) {
    save_hi = pm_bios_pci_read32(bus, dev, func, (uint8_t)(off + 4));
  }

  pm_bios_pci_write32(bus, dev, func, off, 0xFFFFFFFFu);
  if (is64) {
    pm_bios_pci_write32(bus, dev, func, (uint8_t)(off + 4), 0xFFFFFFFFu);
  }

  lo = pm_bios_pci_read32(bus, dev, func, off) & ~0xFu;
  hi = 0;
  if (is64) {
    hi = pm_bios_pci_read32(bus, dev, func, (uint8_t)(off + 4));
  }

  pm_bios_pci_write32(bus, dev, func, off, save_lo);
  if (is64) {
    pm_bios_pci_write32(bus, dev, func, (uint8_t)(off + 4), save_hi);
  }

  size = (uint64_t)lo | ((uint64_t)hi << 32);
  if (size == 0) {
    return 0;
  }

  return (~size) + 1u;
}

static void I915ClflushRect(
  const uint8_t *base, uint32_t pitch_bytes, int32_t x, int32_t y, int32_t w, int32_t h)
{
  int32_t   row;
  uintptr_t line;
  uintptr_t bytes;
  uintptr_t off;

  if (base == NULL || w <= 0 || h <= 0) {
    return;
  }

  bytes = (uintptr_t)w * sizeof(uint32_t);
  for (row = 0; row < h; row++) {
    line = (uintptr_t)(base + (uintptr_t)(y + row) * (uintptr_t)pitch_bytes +
                       (uintptr_t)x * sizeof(uint32_t));
    for (off = 0; off < bytes; off += 64u) {
      __asm__ __volatile__("clflush (%0)" : : "r"(line + off) : "memory");
    }
  }

  __asm__ __volatile__("mfence" ::: "memory");
}

static int32_t I915RingIdle(void)
{
  uint32_t head;
  uint32_t tail;

  head = I915Read(I915_PRB0_HEAD) & ~0x3fu;
  tail = I915Read(I915_PRB0_TAIL) & ~0x7u;
  return (head == tail) ? 1 : 0;
}

static void I915RingWaitIdle(void)
{
  uint32_t spins;

  /* Sync present only — async path yields from job_step. */
  for (spins = 0; spins < 1000000u; spins++) {
    if (I915RingIdle()) {
      return;
    }

    pm_metal_cpu_pause();
  }
}

static int32_t I915RingEmit(const uint32_t *cmds, uint32_t n_dwords)
{
  uint32_t i;
  uint32_t space;

  if (mRing == NULL || n_dwords == 0 || (n_dwords & 1u) != 0) {
    return -1;
  }

  if (mRingTail + n_dwords * sizeof(uint32_t) + 64u > I915_RING_SIZE) {
    I915RingWaitIdle();
    I915Write(I915_PRB0_TAIL, 0);
    I915Write(I915_PRB0_HEAD, 0);
    mRingTail = 0;
  }

  space = I915_RING_SIZE - mRingTail;
  if (space < n_dwords * sizeof(uint32_t) + 8u) {
    return -1;
  }

  for (i = 0; i < n_dwords; i++) {
    mRing[mRingTail / 4u + i] = cmds[i];
  }

  pm_metal_mem_fence();
  mRingTail += n_dwords * sizeof(uint32_t);
  I915Write(I915_PRB0_TAIL, mRingTail);
  return 0;
}

static int32_t I915MapShadow(const pm_metal_scanout_bind_t *b)
{
  uint32_t       pages;
  uint32_t       i;
  uint32_t       gtt_idx;
  uintptr_t      phys;
  const uint8_t *base;

  if (b == NULL || b->shadow == NULL || mGtt == NULL) {
    return -1;
  }

  base  = (const uint8_t *)b->shadow;
  pages = (b->shadow_h * b->shadow_pitch * sizeof(uint32_t) + 4095u) / 4096u;
  if (pages == 0 || mShadowGtt / 4096u + pages > mGttEntries) {
    return -1;
  }

  if (base == mShadowMapped && pages == mShadowPages) {
    return 0;
  }

  gtt_idx = mShadowGtt / 4096u;
  for (i = 0; i < pages; i++) {
    phys              = (uintptr_t)base + (uintptr_t)i * 4096u;
    mGtt[gtt_idx + i] = ((uint32_t)phys & ~0xfffu) | I915_PTE_VALID;
  }

  pm_metal_mem_fence();
  I915Write(I915_GFX_FLSH, 0);
  pm_metal_mem_fence();

  mShadowMapped = base;
  mShadowPages  = pages;
  return 0;
}

static int32_t I915EmitBlit(int32_t x, int32_t y, int32_t w, int32_t h)
{
  const pm_metal_scanout_bind_t *b;
  uint32_t                       pitch;
  uint32_t                       dst;
  uint32_t                       src;
  uint32_t                       cmd[8];

  b = pm_metal_scanout_bind_info();
  if (!mReady || b == NULL || b->shadow == NULL) {
    return -1;
  }

  if (I915MapShadow(b) != 0) {
    return -1;
  }

  I915ClflushRect((const uint8_t *)b->shadow, b->shadow_pitch * sizeof(uint32_t), x, y, w, h);

  pitch = mPitchBytes;
  dst   = mBackGtt;
  src   = mShadowGtt;

  cmd[0] = I915_XY_SRC_COPY_BLT | I915_XY_SRC_COPY_BLT_WRITE_ALPHA | I915_XY_SRC_COPY_BLT_WRITE_RGB;
  cmd[1] = (3u << 24) | (0xccu << 16) | (pitch & 0xffffu);
  cmd[2] = ((uint32_t)y << 16) | (uint32_t)x;
  cmd[3] = ((uint32_t)(y + h) << 16) | (uint32_t)(x + w);
  cmd[4] = dst;
  cmd[5] = ((uint32_t)y << 16) | (uint32_t)x;
  cmd[6] = (b->shadow_pitch * sizeof(uint32_t)) & 0xffffu;
  cmd[7] = src;

  if (I915RingEmit(cmd, 8) != 0) {
    return -1;
  }

  {
    uint32_t flush[2];

    flush[0] = I915_MI_FLUSH;
    flush[1] = I915_MI_NOOP;
    if (I915RingEmit(flush, 2) != 0) {
      return -1;
    }
  }

  mJobExpectTail = mRingTail;
  return 0;
}

static void I915Flip(void)
{
  uint32_t tmp;

  I915Write(I915_DSPASTRIDE, mPitchBytes);
  I915Write(I915_DSPAADDR, mBackGtt);
  pm_metal_mem_fence();

  tmp       = mFrontGtt;
  mFrontGtt = mBackGtt;
  mBackGtt  = tmp;
}

static int32_t I915Probe(const pm_metal_scanout_bind_t *b)
{
  uint8_t  bus;
  uint8_t  dev;
  uint8_t  func;
  uint8_t  cons;
  uint64_t mmio_bar;
  uint64_t apert_bar;
  uint64_t apert_sz;
  uint64_t fb_phys;
  uint32_t page_bytes;
  uint32_t need;
  uint32_t fb_gtt;
  uint32_t pgtbl;
  uint32_t dspc;
  uint32_t i;

  mReady          = 0;
  mMmio           = NULL;
  mGtt            = NULL;
  mRing           = NULL;
  mJobLive        = 0;
  mJobFlipPending = 0;
  mShadowMapped   = NULL;
  mShadowPages    = 0;
  mRingTail       = 0;

  if (b == NULL || b->fb == NULL || !b->owned || b->mode_w == 0 || b->mode_h == 0 ||
      b->fb_ppsl == 0) {
    return -1;
  }

  /* Exact ThinkPad T42 GPU — refuse every other Intel. */
  if (pm_bios_pci_find(I915_855GM_VENDOR, I915_855GM_DEVICE, &bus, &dev, &func) != 0) {
    return -1;
  }

  pm_bios_pci_enable_mem_bm(bus, dev, func);

  mmio_bar = pm_bios_pci_bar_mmio(bus, dev, func, 0, &cons);
  if (mmio_bar == 0) {
    return -1;
  }

  /* GMADR is the large prefetchable aperture (usually BAR2 on 855GM). */
  apert_bar = pm_bios_pci_bar_mmio(bus, dev, func, 2, &cons);
  apert_sz  = I915BarSize(bus, dev, func, 2);
  if (apert_bar == 0 || apert_sz < 8u * 1024u * 1024u) {
    apert_bar = pm_bios_pci_bar_mmio(bus, dev, func, 1, &cons);
    apert_sz  = I915BarSize(bus, dev, func, 1);
  }

  if (apert_bar == 0 || apert_sz < 4u * 1024u * 1024u) {
    return -1;
  }

  fb_phys = (uint64_t)(uintptr_t)b->fb;
  if (fb_phys < apert_bar || fb_phys >= apert_bar + apert_sz) {
    return -1;
  }

  page_bytes = b->mode_h * b->fb_ppsl * sizeof(uint32_t);
  fb_gtt     = (uint32_t)(fb_phys - apert_bar);
  /* Front (firmware) + back + ring in aperture; shadow is GTT→DRAM only. */
  need = page_bytes * 2u + I915_RING_SIZE + 4096u;
  if ((uint64_t)fb_gtt + (uint64_t)need > apert_sz) {
    return -1;
  }

  mMmio       = (uint8_t *)(uintptr_t)mmio_bar;
  mAperture   = apert_bar;
  mApertureSz = apert_sz;
  mPagePx     = b->mode_h * b->fb_ppsl;
  mPageBytes  = page_bytes;
  mPitchBytes = b->fb_ppsl * sizeof(uint32_t);

  dspc = I915Read(I915_DSPACNTR);
  if ((dspc & I915_DSP_ENABLE) == 0) {
    /* Firmware left plane off — not our job to modeset. */
    return -1;
  }

  pgtbl = I915Read(I915_PGTBL_CTL) & ~0xfffu;
  if (pgtbl == 0) {
    return -1;
  }

  mGtt        = (uint32_t *)(uintptr_t)pgtbl;
  mGttEntries = (uint32_t)(apert_sz / 4096u);
  if (mGttEntries < 1024u) {
    return -1;
  }

  mFrontGtt  = fb_gtt;
  mBackGtt   = fb_gtt + page_bytes;
  mRingGtt   = mBackGtt + page_bytes;
  mRingGtt   = (mRingGtt + 4095u) & ~4095u;
  mShadowGtt = mRingGtt + I915_RING_SIZE;
  mShadowGtt = (mShadowGtt + 4095u) & ~4095u;
  mRing      = (uint32_t *)(uintptr_t)(apert_bar + mRingGtt);

  {
    uint32_t shadow_pages;

    shadow_pages = (b->mode_h * b->fb_ppsl * sizeof(uint32_t) + 4095u) / 4096u;
    if (mShadowGtt / 4096u + shadow_pages > mGttEntries) {
      return -1;
    }
  }

  /* Ensure aperture GTT slots for back + ring are valid (identity). */
  for (i = mBackGtt / 4096u; i < (mShadowGtt / 4096u); i++) {
    uint32_t phys;

    phys    = (uint32_t)(apert_bar + (uint64_t)i * 4096u);
    mGtt[i] = (phys & ~0xfffu) | I915_PTE_VALID;
  }

  I915Write(I915_GFX_FLSH, 0);

  /* Clone front → back so first flip is not garbage. */
  memcpy((void *)(uintptr_t)(apert_bar + mBackGtt),
         (void *)(uintptr_t)(apert_bar + mFrontGtt),
         page_bytes);

  memset(mRing, 0, I915_RING_SIZE);
  pm_metal_mem_fence();

  I915Write(I915_PRB0_CTL, 0);
  I915Write(I915_PRB0_HEAD, 0);
  I915Write(I915_PRB0_TAIL, 0);
  I915Write(I915_PRB0_START, mRingGtt);
  I915Write(I915_PRB0_CTL, ((I915_RING_SIZE - 4096u) & I915_RING_NR_PAGES) | I915_RING_VALID);
  mRingTail = 0;

  I915Write(I915_DSPASTRIDE, mPitchBytes);
  I915Write(I915_DSPAADDR, mFrontGtt);

  mReady = 1;
  return 0;
}

static int32_t I915PresentRect(int32_t x, int32_t y, int32_t w, int32_t h)
{
  if (!mReady || w <= 0 || h <= 0) {
    return -1;
  }

  if (I915EmitBlit(x, y, w, h) != 0) {
    return -1;
  }

  I915RingWaitIdle();
  I915Flip();
  return 0;
}

static int32_t I915JobBegin(int32_t x, int32_t y, int32_t w, int32_t h)
{
  if (!mReady || w <= 0 || h <= 0) {
    return -1;
  }

  if (mJobLive) {
    return -1;
  }

  if (I915EmitBlit(x, y, w, h) != 0) {
    return -1;
  }

  mJobLive        = 1;
  mJobFlipPending = 1;
  (void)mJobExpectTail;
  return 1;
}

static int32_t I915JobStep(void)
{
  if (!mJobLive) {
    return 0;
  }

  if (!I915RingIdle()) {
    return 1;
  }

  if (mJobFlipPending) {
    I915Flip();
    mJobFlipPending = 0;
  }

  mJobLive = 0;
  return 0;
}

static uint32_t I915Caps(void)
{
  return PM_METAL_SCANOUT_CAP_TEAR_FREE;
}

static void I915Fini(void)
{
  if (mMmio != NULL && mReady) {
    I915RingWaitIdle();
    I915Write(I915_PRB0_CTL, 0);
    if (mFrontGtt != 0) {
      I915Write(I915_DSPAADDR, mFrontGtt);
    }
  }

  mReady          = 0;
  mMmio           = NULL;
  mGtt            = NULL;
  mRing           = NULL;
  mJobLive        = 0;
  mJobFlipPending = 0;
  mShadowMapped   = NULL;
}

const pm_metal_scanout_ops_t g_pm_metal_scanout_i915_855gm = {
  "i915_855gm", I915Probe, I915PresentRect, I915JobBegin, I915JobStep, I915Caps,
  NULL,         NULL,      I915Fini
};
