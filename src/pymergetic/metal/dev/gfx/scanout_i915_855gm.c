/** @file
  Sample i915 scanout — ThinkPad T42 Intel 855GM (PCI 8086:3582) only.

  Reference Gen2 path (GGTT + ring XY_SRC_COPY_BLT + DSPAADDR flip). Kept as
  a worked example; production iron for T43 is radeon_rv370 (1002:5460).
  Probe fails on every other GPU → fall through.
**/
#include <pymergetic/metal/dev/gfx/scanout.h>
#include "../../bus/pci/pci.h"

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>

#define I915_855GM_VENDOR  0x8086u
#define I915_855GM_DEVICE  0x3582u

#define I915_PGTBL_CTL   0x02020u
#define I915_PRB0_TAIL   0x02030u
#define I915_PRB0_HEAD   0x02034u
#define I915_PRB0_START  0x02038u
#define I915_PRB0_CTL    0x0203cu
#define I915_GFX_FLSH    0x02170u
#define I915_DSPACNTR    0x70180u
#define I915_DSPAADDR    0x70184u
#define I915_DSPASTRIDE  0x70188u

#define I915_RING_VALID     0x00000001u
#define I915_RING_NR_PAGES  0x001FF000u
#define I915_PTE_VALID      0x00000001u

#define I915_MI_NOOP   0x00000000u
#define I915_MI_FLUSH  0x02000000u

#define I915_XY_SRC_COPY_BLT  ((2u << 29) | (0x53u << 22) | 6u)
#define I915_XY_SRC_COPY_BLT_WRITE_ALPHA  (1u << 21)
#define I915_XY_SRC_COPY_BLT_WRITE_RGB    (1u << 20)

#define I915_RING_SIZE   16384u
#define I915_DSP_ENABLE  (1u << 31)

STATIC INT32     mReady;
STATIC UINT8    *mMmio;
STATIC UINT32   *mGtt;
STATIC UINT32    mGttEntries;
STATIC UINT64    mAperture;
STATIC UINT64    mApertureSz;
STATIC UINT32    mPagePx;
STATIC UINT32    mPageBytes;
STATIC UINT32    mFrontGtt;
STATIC UINT32    mBackGtt;
STATIC UINT32    mRingGtt;
STATIC UINT32   *mRing;
STATIC UINT32    mRingTail;
STATIC UINT32    mPitchBytes;
STATIC UINT32    mShadowGtt;
STATIC UINT32    mShadowPages;
STATIC CONST UINT8  *mShadowMapped;
STATIC INT32     mJobLive;
STATIC INT32     mJobFlipPending;
STATIC UINT32    mJobExpectTail;

STATIC
UINT32
I915Read (
  UINT32  off
  )
{
  return *(volatile UINT32 *)(UINTN)(mMmio + off);
}

STATIC
VOID
I915Write (
  UINT32  off,
  UINT32  val
  )
{
  *(volatile UINT32 *)(UINTN)(mMmio + off) = val;
  MemoryFence ();
}

STATIC
UINT64
I915BarSize (
  UINT8  bus,
  UINT8  dev,
  UINT8  func,
  UINT8  bar_index
  )
{
  UINT8   off;
  UINT32  lo;
  UINT32  hi;
  UINT32  save_lo;
  UINT32  save_hi;
  UINT64  size;
  INT32   is64;

  off     = (UINT8)(0x10u + bar_index * 4u);
  save_lo = pm_bios_pci_read32 (bus, dev, func, off);
  if ((save_lo & 1u) != 0) {
    return 0;
  }

  is64    = (((save_lo >> 1) & 3u) == 2) ? 1 : 0;
  save_hi = 0;
  if (is64) {
    save_hi = pm_bios_pci_read32 (bus, dev, func, (UINT8)(off + 4));
  }

  pm_bios_pci_write32 (bus, dev, func, off, 0xFFFFFFFFu);
  if (is64) {
    pm_bios_pci_write32 (bus, dev, func, (UINT8)(off + 4), 0xFFFFFFFFu);
  }

  lo = pm_bios_pci_read32 (bus, dev, func, off) & ~0xFu;
  hi = 0;
  if (is64) {
    hi = pm_bios_pci_read32 (bus, dev, func, (UINT8)(off + 4));
  }

  pm_bios_pci_write32 (bus, dev, func, off, save_lo);
  if (is64) {
    pm_bios_pci_write32 (bus, dev, func, (UINT8)(off + 4), save_hi);
  }

  size = (UINT64)lo | ((UINT64)hi << 32);
  if (size == 0) {
    return 0;
  }

  return (~size) + 1u;
}

STATIC
VOID
I915ClflushRect (
  CONST UINT8  *base,
  UINT32        pitch_bytes,
  INT32         x,
  INT32         y,
  INT32         w,
  INT32         h
  )
{
  INT32   row;
  UINTN   line;
  UINTN   bytes;
  UINTN   off;

  if (base == NULL || w <= 0 || h <= 0) {
    return;
  }

  bytes = (UINTN)w * sizeof (UINT32);
  for (row = 0; row < h; row++) {
    line = (UINTN)(base + (UINTN)(y + row) * (UINTN)pitch_bytes
                   + (UINTN)x * sizeof (UINT32));
    for (off = 0; off < bytes; off += 64u) {
      __asm__ __volatile__ ("clflush (%0)" : : "r" (line + off) : "memory");
    }
  }

  __asm__ __volatile__ ("mfence" ::: "memory");
}

STATIC
INT32
I915RingIdle (
  VOID
  )
{
  UINT32  head;
  UINT32  tail;

  head = I915Read (I915_PRB0_HEAD) & ~0x3fu;
  tail = I915Read (I915_PRB0_TAIL) & ~0x7u;
  return (head == tail) ? 1 : 0;
}

STATIC
VOID
I915RingWaitIdle (
  VOID
  )
{
  UINT32  spins;

  /* Sync present only — async path yields from job_step. */
  for (spins = 0; spins < 1000000u; spins++) {
    if (I915RingIdle ()) {
      return;
    }

    CpuPause ();
  }
}

STATIC
INT32
I915RingEmit (
  CONST UINT32  *cmds,
  UINT32         n_dwords
  )
{
  UINT32  i;
  UINT32  space;

  if (mRing == NULL || n_dwords == 0 || (n_dwords & 1u) != 0) {
    return -1;
  }

  if (mRingTail + n_dwords * sizeof (UINT32) + 64u > I915_RING_SIZE) {
    I915RingWaitIdle ();
    I915Write (I915_PRB0_TAIL, 0);
    I915Write (I915_PRB0_HEAD, 0);
    mRingTail = 0;
  }

  space = I915_RING_SIZE - mRingTail;
  if (space < n_dwords * sizeof (UINT32) + 8u) {
    return -1;
  }

  for (i = 0; i < n_dwords; i++) {
    mRing[mRingTail / 4u + i] = cmds[i];
  }

  MemoryFence ();
  mRingTail += n_dwords * sizeof (UINT32);
  I915Write (I915_PRB0_TAIL, mRingTail);
  return 0;
}

STATIC
INT32
I915MapShadow (
  CONST pm_metal_scanout_bind_t  *b
  )
{
  UINT32        pages;
  UINT32        i;
  UINT32        gtt_idx;
  UINTN         phys;
  CONST UINT8  *base;

  if (b == NULL || b->shadow == NULL || mGtt == NULL) {
    return -1;
  }

  base  = (CONST UINT8 *)b->shadow;
  pages = (b->shadow_h * b->shadow_pitch * sizeof (UINT32) + 4095u) / 4096u;
  if (pages == 0 || mShadowGtt / 4096u + pages > mGttEntries) {
    return -1;
  }

  if (base == mShadowMapped && pages == mShadowPages) {
    return 0;
  }

  gtt_idx = mShadowGtt / 4096u;
  for (i = 0; i < pages; i++) {
    phys = (UINTN)base + (UINTN)i * 4096u;
    mGtt[gtt_idx + i] = ((UINT32)phys & ~0xfffu) | I915_PTE_VALID;
  }

  MemoryFence ();
  I915Write (I915_GFX_FLSH, 0);
  MemoryFence ();

  mShadowMapped = base;
  mShadowPages  = pages;
  return 0;
}

STATIC
INT32
I915EmitBlit (
  INT32  x,
  INT32  y,
  INT32  w,
  INT32  h
  )
{
  CONST pm_metal_scanout_bind_t  *b;
  UINT32                          pitch;
  UINT32                          dst;
  UINT32                          src;
  UINT32                          cmd[8];

  b = pm_metal_scanout_bind_info ();
  if (!mReady || b == NULL || b->shadow == NULL) {
    return -1;
  }

  if (I915MapShadow (b) != 0) {
    return -1;
  }

  I915ClflushRect (
    (CONST UINT8 *)b->shadow,
    b->shadow_pitch * sizeof (UINT32),
    x,
    y,
    w,
    h
    );

  pitch = mPitchBytes;
  dst   = mBackGtt;
  src   = mShadowGtt;

  cmd[0] = I915_XY_SRC_COPY_BLT
           | I915_XY_SRC_COPY_BLT_WRITE_ALPHA
           | I915_XY_SRC_COPY_BLT_WRITE_RGB;
  cmd[1] = (3u << 24) | (0xccu << 16) | (pitch & 0xffffu);
  cmd[2] = ((UINT32)y << 16) | (UINT32)x;
  cmd[3] = ((UINT32)(y + h) << 16) | (UINT32)(x + w);
  cmd[4] = dst;
  cmd[5] = ((UINT32)y << 16) | (UINT32)x;
  cmd[6] = (b->shadow_pitch * sizeof (UINT32)) & 0xffffu;
  cmd[7] = src;

  if (I915RingEmit (cmd, 8) != 0) {
    return -1;
  }

  {
    UINT32  flush[2];

    flush[0] = I915_MI_FLUSH;
    flush[1] = I915_MI_NOOP;
    if (I915RingEmit (flush, 2) != 0) {
      return -1;
    }
  }

  mJobExpectTail = mRingTail;
  return 0;
}

STATIC
VOID
I915Flip (
  VOID
  )
{
  UINT32  tmp;

  I915Write (I915_DSPASTRIDE, mPitchBytes);
  I915Write (I915_DSPAADDR, mBackGtt);
  MemoryFence ();

  tmp       = mFrontGtt;
  mFrontGtt = mBackGtt;
  mBackGtt  = tmp;
}

STATIC
INT32
I915Probe (
  CONST pm_metal_scanout_bind_t  *b
  )
{
  UINT8   bus;
  UINT8   dev;
  UINT8   func;
  UINT8   cons;
  UINT64  mmio_bar;
  UINT64  apert_bar;
  UINT64  apert_sz;
  UINT64  fb_phys;
  UINT32  page_bytes;
  UINT32  need;
  UINT32  fb_gtt;
  UINT32  pgtbl;
  UINT32  dspc;
  UINT32  i;

  mReady          = 0;
  mMmio           = NULL;
  mGtt            = NULL;
  mRing           = NULL;
  mJobLive        = 0;
  mJobFlipPending = 0;
  mShadowMapped   = NULL;
  mShadowPages    = 0;
  mRingTail       = 0;

  if (b == NULL || b->fb == NULL || !b->owned
      || b->mode_w == 0 || b->mode_h == 0 || b->fb_ppsl == 0)
  {
    return -1;
  }

  /* Exact ThinkPad T42 GPU — refuse every other Intel. */
  if (pm_bios_pci_find (
        I915_855GM_VENDOR,
        I915_855GM_DEVICE,
        &bus,
        &dev,
        &func
        ) != 0)
  {
    return -1;
  }

  pm_bios_pci_enable_mem_bm (bus, dev, func);

  mmio_bar = pm_bios_pci_bar_mmio (bus, dev, func, 0, &cons);
  if (mmio_bar == 0) {
    return -1;
  }

  /* GMADR is the large prefetchable aperture (usually BAR2 on 855GM). */
  apert_bar = pm_bios_pci_bar_mmio (bus, dev, func, 2, &cons);
  apert_sz  = I915BarSize (bus, dev, func, 2);
  if (apert_bar == 0 || apert_sz < 8u * 1024u * 1024u) {
    apert_bar = pm_bios_pci_bar_mmio (bus, dev, func, 1, &cons);
    apert_sz  = I915BarSize (bus, dev, func, 1);
  }

  if (apert_bar == 0 || apert_sz < 4u * 1024u * 1024u) {
    return -1;
  }

  fb_phys = (UINT64)(UINTN)b->fb;
  if (fb_phys < apert_bar || fb_phys >= apert_bar + apert_sz) {
    return -1;
  }

  page_bytes = b->mode_h * b->fb_ppsl * sizeof (UINT32);
  fb_gtt     = (UINT32)(fb_phys - apert_bar);
  /* Front (firmware) + back + ring in aperture; shadow is GTT→DRAM only. */
  need       = page_bytes * 2u + I915_RING_SIZE + 4096u;
  if ((UINT64)fb_gtt + (UINT64)need > apert_sz) {
    return -1;
  }

  mMmio       = (UINT8 *)(UINTN)mmio_bar;
  mAperture   = apert_bar;
  mApertureSz = apert_sz;
  mPagePx     = b->mode_h * b->fb_ppsl;
  mPageBytes  = page_bytes;
  mPitchBytes = b->fb_ppsl * sizeof (UINT32);

  dspc = I915Read (I915_DSPACNTR);
  if ((dspc & I915_DSP_ENABLE) == 0) {
    /* Firmware left plane off — not our job to modeset. */
    return -1;
  }

  pgtbl = I915Read (I915_PGTBL_CTL) & ~0xfffu;
  if (pgtbl == 0) {
    return -1;
  }

  mGtt         = (UINT32 *)(UINTN)pgtbl;
  mGttEntries  = (UINT32)(apert_sz / 4096u);
  if (mGttEntries < 1024u) {
    return -1;
  }

  mFrontGtt   = fb_gtt;
  mBackGtt    = fb_gtt + page_bytes;
  mRingGtt    = mBackGtt + page_bytes;
  mRingGtt    = (mRingGtt + 4095u) & ~4095u;
  mShadowGtt  = mRingGtt + I915_RING_SIZE;
  mShadowGtt  = (mShadowGtt + 4095u) & ~4095u;
  mRing       = (UINT32 *)(UINTN)(apert_bar + mRingGtt);

  {
    UINT32  shadow_pages;

    shadow_pages = (b->mode_h * b->fb_ppsl * sizeof (UINT32) + 4095u) / 4096u;
    if (mShadowGtt / 4096u + shadow_pages > mGttEntries) {
      return -1;
    }
  }

  /* Ensure aperture GTT slots for back + ring are valid (identity). */
  for (i = mBackGtt / 4096u; i < (mShadowGtt / 4096u); i++) {
    UINT32  phys;

    phys = (UINT32)(apert_bar + (UINT64)i * 4096u);
    mGtt[i] = (phys & ~0xfffu) | I915_PTE_VALID;
  }

  I915Write (I915_GFX_FLSH, 0);

  /* Clone front → back so first flip is not garbage. */
  CopyMem (
    (VOID *)(UINTN)(apert_bar + mBackGtt),
    (VOID *)(UINTN)(apert_bar + mFrontGtt),
    page_bytes
    );

  ZeroMem (mRing, I915_RING_SIZE);
  MemoryFence ();

  I915Write (I915_PRB0_CTL, 0);
  I915Write (I915_PRB0_HEAD, 0);
  I915Write (I915_PRB0_TAIL, 0);
  I915Write (I915_PRB0_START, mRingGtt);
  I915Write (
    I915_PRB0_CTL,
    ((I915_RING_SIZE - 4096u) & I915_RING_NR_PAGES) | I915_RING_VALID
    );
  mRingTail = 0;

  I915Write (I915_DSPASTRIDE, mPitchBytes);
  I915Write (I915_DSPAADDR, mFrontGtt);

  mReady = 1;
  return 0;
}

STATIC
INT32
I915PresentRect (
  INT32  x,
  INT32  y,
  INT32  w,
  INT32  h
  )
{
  if (!mReady || w <= 0 || h <= 0) {
    return -1;
  }

  if (I915EmitBlit (x, y, w, h) != 0) {
    return -1;
  }

  I915RingWaitIdle ();
  I915Flip ();
  return 0;
}

STATIC
INT32
I915JobBegin (
  INT32  x,
  INT32  y,
  INT32  w,
  INT32  h
  )
{
  if (!mReady || w <= 0 || h <= 0) {
    return -1;
  }

  if (mJobLive) {
    return -1;
  }

  if (I915EmitBlit (x, y, w, h) != 0) {
    return -1;
  }

  mJobLive        = 1;
  mJobFlipPending = 1;
  (VOID)mJobExpectTail;
  return 1;
}

STATIC
INT32
I915JobStep (
  VOID
  )
{
  if (!mJobLive) {
    return 0;
  }

  if (!I915RingIdle ()) {
    return 1;
  }

  if (mJobFlipPending) {
    I915Flip ();
    mJobFlipPending = 0;
  }

  mJobLive = 0;
  return 0;
}

STATIC
UINT32
I915Caps (
  VOID
  )
{
  return PM_METAL_SCANOUT_CAP_TEAR_FREE;
}

STATIC
VOID
I915Fini (
  VOID
  )
{
  if (mMmio != NULL && mReady) {
    I915RingWaitIdle ();
    I915Write (I915_PRB0_CTL, 0);
    if (mFrontGtt != 0) {
      I915Write (I915_DSPAADDR, mFrontGtt);
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

CONST pm_metal_scanout_ops_t  g_pm_metal_scanout_i915_855gm = {
  "i915_855gm",
  I915Probe,
  I915PresentRect,
  I915JobBegin,
  I915JobStep,
  I915Caps,
  NULL,
  NULL,
  I915Fini
};
