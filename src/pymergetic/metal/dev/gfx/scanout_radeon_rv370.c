/** @file
  Radeon scanout — ThinkPad T43 Mobility X300 / RV370 (PCI 1002:5460).

  Working path:
    - firmware CRTC untouched (no pitch/flip)
    - PCIe GART maps shadow DRAM (table in VRAM, CPU clflush)
    - MMIO 2D BITBLT GART → front MC surface
    - probe proves VRAM blit + GART readback before claiming
    - fail → lfb_copy
**/
#include <pymergetic/metal/dev/gfx/scanout.h>
#include <pymergetic/metal/log/log.h>
#include <runtime/mem/mem.h>
#include "../../bus/pci/pci.h"

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>

#define RADO_VENDOR  0x1002u
#define RADO_DEVICE  0x5460u

#define RADEON_PCIE_INDEX            0x0030u
#define RADEON_PCIE_DATA             0x0034u
#define RADEON_RV370_BUS_CNTL        0x004cu
#define RADEON_CRTC_GEN_CNTL         0x0050u
#define RADEON_CONFIG_MEMSIZE        0x00f8u
#define RADEON_CRTC_EXT_CNTL         0x0054u
#define RADEON_HOST_PATH_CNTL        0x0130u
#define RADEON_MC_FB_LOCATION        0x0148u
#define RADEON_MC_AGP_LOCATION       0x014cu
#define RADEON_MC_STATUS             0x0150u
#define RADEON_CRTC_OFFSET           0x0224u
#define RADEON_CP_CSQ_CNTL           0x0740u
#define RADEON_RBBM_SOFT_RESET       0x00f0u
#define RADEON_RBBM_STATUS           0x0e40u
#define RADEON_HDP_READ_BUFFER_INVALIDATE  (1u << 27)
#define RADEON_CRTC_DISPLAY_DIS      (1u << 10)
#define RADEON_BUS_MASTER_DIS        (1u << 6)
#define R300_MC_IDLE                 (1u << 4)
#define RADEON_SRC_PITCH_OFFSET      0x1428u
#define RADEON_DST_PITCH_OFFSET      0x142cu
#define RADEON_SRC_Y_X               0x1434u
#define RADEON_DST_Y_X               0x1438u
#define RADEON_DST_HEIGHT_WIDTH      0x143cu
#define RADEON_DP_GUI_MASTER_CNTL    0x146cu
#define RADEON_DP_CNTL               0x16c0u
#define RADEON_DP_WRITE_MASK         0x16ccu
#define RADEON_DSTCACHE_CTLSTAT      0x1714u
#define RADEON_RB2D_DSTCACHE_CTLSTAT 0x342cu

#define RADEON_CRTC_EN               (1u << 25)
#define RADEON_CRTC_OFFSET_MASK      0x07ffffffu
#define RADEON_RBBM_FIFOCNT_MASK     0x007fu
#define RADEON_RBBM_ACTIVE           (1u << 31)
#define RADEON_SOFT_RESET_SE         (1u << 2)
#define RADEON_SOFT_RESET_RE         (1u << 3)
#define RADEON_SOFT_RESET_PP         (1u << 4)
#define RADEON_SOFT_RESET_RB         (1u << 6)
#define RADEON_RB2D_DC_FLUSH_ALL     0xfu
#define RADEON_DST_X_LEFT_TO_RIGHT   (1u << 0)
#define RADEON_DST_Y_TOP_TO_BOTTOM   (1u << 1)

#define RADEON_GMC_SRC_PITCH_OFFSET_CNTL  (1u << 0)
#define RADEON_GMC_DST_PITCH_OFFSET_CNTL  (1u << 1)
#define RADEON_GMC_BRUSH_NONE             (15u << 4)
#define RADEON_GMC_DST_32BPP              (6u << 8)
#define RADEON_GMC_SRC_DATATYPE_COLOR     (3u << 12)
#define RADEON_ROP3_S                     0x00cc0000u
#define RADEON_DP_SRC_SOURCE_MEMORY       (2u << 24)
#define RADEON_GMC_CLR_CMP_CNTL_DIS       (1u << 28)
#define RADEON_GMC_WR_MSK_DIS             (1u << 30)

#define RADEON_PCIE_TX_GART_CNTL     0x10u
#define RADEON_PCIE_TX_GART_BASE     0x13u
#define RADEON_PCIE_TX_GART_START_LO 0x14u
#define RADEON_PCIE_TX_GART_START_HI 0x15u
#define RADEON_PCIE_TX_GART_END_LO   0x16u
#define RADEON_PCIE_TX_GART_END_HI   0x17u
#define RADEON_PCIE_TX_GART_ERROR    0x18u
#define RADEON_PCIE_TX_DISCARD_RD_ADDR_LO 0x11u
#define RADEON_PCIE_TX_DISCARD_RD_ADDR_HI 0x12u
#define RADEON_PCIE_TX_GART_EN       (1u << 0)
#define RADEON_PCIE_TX_GART_UNMAPPED_ACCESS_DISCARD (3u << 1)
#define RADEON_PCIE_TX_GART_INVALIDATE_TLB          (1u << 8)

#define R300_PTE_UNSNOOPED  (1u << 0)
#define R300_PTE_WRITEABLE  (1u << 2)
#define R300_PTE_READABLE   (1u << 3)

#define RADO_GART_PAGES  2048u          /* 8 MiB GTT — enough for 1080p shadow */
#define RADO_GART_BYTES  (RADO_GART_PAGES * 4u)
#define RADO_WAIT_SPINS  500000u
#define RADO_PROBE_PIXELS  64u

STATIC INT32     mReady;
STATIC INT32     mFaulted;
STATIC UINT8    *mMmio;
STATIC UINT8    *mVram;
STATIC UINT32    mVramStart;
STATIC UINT32    mVramSize;
STATIC UINT32    mAperOff;
STATIC UINT32    mPageBytes;
STATIC UINT32    mPitchBytes;
STATIC UINT32    mFrontMc;
STATIC UINT32    mScratchMc;     /* offscreen VRAM for probe readback */
STATIC UINT32    mGttStart;
STATIC UINT32    mGartTableMc;
STATIC UINT32    mGartTableAper;
STATIC UINT32    mGartTablePci; /* BAR0 + aper — alternate GART_BASE */
STATIC UINT32   *mGart;
STATIC UINT32    mShadowPages;
STATIC CONST UINT8  *mShadowMapped;

STATIC
UINT32
RadoRead (
  UINT32  off
  )
{
  return *(volatile UINT32 *)(UINTN)(mMmio + off);
}

STATIC
VOID
RadoWrite (
  UINT32  off,
  UINT32  val
  )
{
  *(volatile UINT32 *)(UINTN)(mMmio + off) = val;
  MemoryFence ();
}

STATIC
UINT32
RadoPcieRead (
  UINT32  reg
  )
{
  RadoWrite (RADEON_PCIE_INDEX, reg & 0xffu);
  return RadoRead (RADEON_PCIE_DATA);
}

STATIC
VOID
RadoPcieWrite (
  UINT32  reg,
  UINT32  val
  )
{
  RadoWrite (RADEON_PCIE_INDEX, reg & 0xffu);
  RadoWrite (RADEON_PCIE_DATA, val);
}

STATIC
VOID
RadoClflush (
  CONST VOID  *addr,
  UINTN        len
  )
{
  CONST UINT8  *p;
  UINTN         off;

  if (addr == NULL || len == 0) {
    return;
  }

  p = (CONST UINT8 *)addr;
  for (off = 0; off < len; off += 64u) {
    __asm__ __volatile__ ("clflush (%0)" : : "r" (p + off) : "memory");
  }

  __asm__ __volatile__ ("mfence" ::: "memory");
}

STATIC
INT32
RadoGuiIdle (
  VOID
  )
{
  return ((RadoRead (RADEON_RBBM_STATUS) & RADEON_RBBM_ACTIVE) == 0) ? 1 : 0;
}

STATIC
INT32
RadoWaitFifo (
  UINT32  n
  )
{
  UINT32  i;

  for (i = 0; i < RADO_WAIT_SPINS; i++) {
    if ((RadoRead (RADEON_RBBM_STATUS) & RADEON_RBBM_FIFOCNT_MASK) >= n) {
      return 0;
    }

    CpuPause ();
  }

  return -1;
}

STATIC
INT32
RadoWaitGuiIdle (
  VOID
  )
{
  UINT32  i;

  for (i = 0; i < RADO_WAIT_SPINS; i++) {
    if (RadoGuiIdle ()) {
      return 0;
    }

    CpuPause ();
  }

  return -1;
}

STATIC
VOID
RadoFault (
  VOID
  )
{
  mFaulted = 1;
  mReady   = 0;
  if (mMmio != NULL) {
    RadoPcieWrite (
      RADEON_PCIE_TX_GART_CNTL,
      RADEON_PCIE_TX_GART_UNMAPPED_ACCESS_DISCARD
      );
  }
}

STATIC
UINT32
RadoPitchOffset (
  UINT32  pitch_bytes,
  UINT32  mc_off
  )
{
  return ((pitch_bytes / 64u) << 22) | ((mc_off >> 10) & 0x003fffffu);
}

STATIC
UINT32
RadoPte (
  UINTN  phys,
  INT32  snoop
  )
{
  UINT64  p;
  UINT32  e;

  /* PTE packs phys[39:12] via >>8; low 4 bits are flags — page align required. */
  p  = (UINT64)phys & ~0xfffull;
  e  = (UINT32)((p >> 8) & 0x00ffffffu);
  e |= (UINT32)(((p >> 32) & 0xffu) << 24);
  e |= R300_PTE_READABLE | R300_PTE_WRITEABLE;
  if (!snoop) {
    e |= R300_PTE_UNSNOOPED;
  }

  return e;
}

STATIC
VOID
RadoHdpFlush (
  VOID
  )
{
  UINT32  hdp;

  /* GPU→host: invalidate HDP read buffer so CPU sees VRAM writes. */
  hdp  = RadoRead (RADEON_HOST_PATH_CNTL);
  RadoWrite (RADEON_HOST_PATH_CNTL, hdp | RADEON_HDP_READ_BUFFER_INVALIDATE);
  RadoWrite (RADEON_HOST_PATH_CNTL, hdp);
}

STATIC
VOID
RadoSkip (
  CONST CHAR8  *why
  )
{
  pm_metal_logf_styled (
    PM_METAL_LOG_STYLE_DIM,
    "metal-gfx: radeon_rv370 skip %a",
    why
    );
}

STATIC
VOID
RadoGuiKick (
  VOID
  )
{
  UINT32  i;

  if (RadoWaitGuiIdle () == 0) {
    return;
  }

  /* Light 2D/3D reset — leave CP alone; CRTC stays up. */
  RadoWrite (
    RADEON_RBBM_SOFT_RESET,
    RADEON_SOFT_RESET_SE | RADEON_SOFT_RESET_RE
    | RADEON_SOFT_RESET_PP | RADEON_SOFT_RESET_RB
    );
  for (i = 0; i < 10000u; i++) {
    CpuPause ();
  }

  RadoWrite (RADEON_RBBM_SOFT_RESET, 0);
  for (i = 0; i < 1000u; i++) {
    CpuPause ();
  }

  (VOID)RadoWaitGuiIdle ();
}

STATIC
VOID
RadoGartTlbFlush (
  VOID
  )
{
  UINT32  tmp;
  UINT32  i;

  for (i = 0; i < 2u; i++) {
    tmp = RadoPcieRead (RADEON_PCIE_TX_GART_CNTL);
    RadoPcieWrite (
      RADEON_PCIE_TX_GART_CNTL,
      tmp | RADEON_PCIE_TX_GART_INVALIDATE_TLB
      );
    (VOID)RadoPcieRead (RADEON_PCIE_TX_GART_CNTL);
    RadoPcieWrite (RADEON_PCIE_TX_GART_CNTL, tmp);
  }

  MemoryFence ();
}

STATIC
VOID
RadoWbinvd (
  VOID
  )
{
  __asm__ __volatile__ ("wbinvd" ::: "memory");
}

STATIC
VOID
RadoGartTableSync (
  VOID
  )
{
  /* GART lives in WC VRAM — GPU must see PTE writes. */
  RadoClflush (mGart, RADO_GART_BYTES);
  RadoHdpFlush ();
  RadoGartTlbFlush ();
}

STATIC
INT32
RadoBlitMc (
  UINT32  src_mc,
  UINT32  dst_mc,
  UINT32  src_pitch,
  UINT32  dst_pitch,
  INT32   x,
  INT32   y,
  INT32   w,
  INT32   h
  )
{
  UINT32  gmc;

  if (w <= 0 || h <= 0) {
    return 0;
  }

  if ((src_pitch < 64u) || (dst_pitch < 64u)
      || ((src_pitch | dst_pitch) & 63u) != 0)
  {
    return -1;
  }

  if (RadoWaitGuiIdle () != 0 || RadoWaitFifo (8) != 0) {
    return -1;
  }

  gmc = RADEON_GMC_SRC_PITCH_OFFSET_CNTL
        | RADEON_GMC_DST_PITCH_OFFSET_CNTL
        | RADEON_GMC_BRUSH_NONE
        | RADEON_GMC_DST_32BPP
        | RADEON_GMC_SRC_DATATYPE_COLOR
        | RADEON_ROP3_S
        | RADEON_DP_SRC_SOURCE_MEMORY
        | RADEON_GMC_CLR_CMP_CNTL_DIS
        | RADEON_GMC_WR_MSK_DIS;

  RadoWrite (RADEON_DP_GUI_MASTER_CNTL, gmc);
  RadoWrite (RADEON_DP_WRITE_MASK, 0xffffffffu);
  RadoWrite (RADEON_DP_CNTL, RADEON_DST_X_LEFT_TO_RIGHT | RADEON_DST_Y_TOP_TO_BOTTOM);
  RadoWrite (RADEON_SRC_PITCH_OFFSET, RadoPitchOffset (src_pitch, src_mc));
  RadoWrite (RADEON_DST_PITCH_OFFSET, RadoPitchOffset (dst_pitch, dst_mc));
  RadoWrite (RADEON_SRC_Y_X, ((UINT32)y << 16) | (UINT32)x);
  RadoWrite (RADEON_DST_Y_X, ((UINT32)y << 16) | (UINT32)x);
  RadoWrite (RADEON_DST_HEIGHT_WIDTH, ((UINT32)h << 16) | (UINT32)w);

  if (RadoWaitFifo (2) != 0) {
    return -1;
  }

  RadoWrite (RADEON_DSTCACHE_CTLSTAT, RADEON_RB2D_DC_FLUSH_ALL);
  RadoWrite (RADEON_RB2D_DSTCACHE_CTLSTAT, RADEON_RB2D_DC_FLUSH_ALL);
  return RadoWaitGuiIdle ();
}

STATIC
INT32
RadoMapShadow (
  CONST pm_metal_scanout_bind_t  *b
  )
{
  UINT32        pages;
  UINT32        i;
  CONST UINT8  *base;

  if (b == NULL || b->shadow == NULL || mGart == NULL) {
    return -1;
  }

  base  = (CONST UINT8 *)b->shadow;
  if (((UINTN)base & 4095u) != 0) {
    return -1;
  }

  pages = (b->shadow_h * b->shadow_pitch * sizeof (UINT32) + 4095u) / 4096u;
  if (pages == 0 || pages > RADO_GART_PAGES) {
    return -1;
  }

  if (base == mShadowMapped && pages == mShadowPages) {
    return 0;
  }

  for (i = 0; i < pages; i++) {
    /* WB shadow — snooped, matching Linux ttm_cached. */
    mGart[i] = RadoPte ((UINTN)base + (UINTN)i * 4096u, 1);
  }

  for (; i < RADO_GART_PAGES; i++) {
    mGart[i] = 0;
  }

  RadoGartTableSync ();
  mShadowMapped = base;
  mShadowPages  = pages;
  return 0;
}

STATIC
INT32
RadoEmitBlit (
  INT32  x,
  INT32  y,
  INT32  w,
  INT32  h
  )
{
  CONST pm_metal_scanout_bind_t  *b;
  UINT32                          src_pitch;

  b = pm_metal_scanout_bind_info ();
  if (!mReady || mFaulted || b == NULL || b->shadow == NULL) {
    return -1;
  }

  if (RadoMapShadow (b) != 0) {
    return -1;
  }

  src_pitch = b->shadow_pitch * sizeof (UINT32);
  RadoClflush (
    (CONST UINT8 *)b->shadow + (UINTN)y * (UINTN)src_pitch
    + (UINTN)x * sizeof (UINT32),
    (UINTN)h * (UINTN)src_pitch
    );

  return RadoBlitMc (
           mGttStart,
           mFrontMc,
           src_pitch,
           mPitchBytes,
           x,
           y,
           w,
           h
           );
}

/**
  Clamp MC_FB to real VRAM; map AGP window over GTT so MC routes those
  addresses to the GART block (PCIE_TX then translates).
**/
STATIC
INT32
RadoMcSetup (
  UINT32  agp_loc
  )
{
  UINT32  ext;
  UINT32  vram_end;
  UINT32  bus;
  UINT32  i;

  vram_end = mVramStart + mVramSize - 1u;
  ext      = RadoRead (RADEON_CRTC_EXT_CNTL);

  /* RV370: BUS_CNTL lives at 0x4c (0x30 is PCIE_INDEX). */
  bus  = RadoRead (RADEON_RV370_BUS_CNTL);
  bus &= ~RADEON_BUS_MASTER_DIS;
  RadoWrite (RADEON_RV370_BUS_CNTL, bus);

  /* MMIO 2D owns GUI — kill any firmware CP CSQ. */
  RadoWrite (RADEON_CP_CSQ_CNTL, 0);

  /* Brief display lock while touching MC (Linux r100_mc_stop lite). */
  RadoWrite (RADEON_CRTC_EXT_CNTL, ext | RADEON_CRTC_DISPLAY_DIS);
  (VOID)RadoWaitGuiIdle ();

  for (i = 0; i < RADO_WAIT_SPINS; i++) {
    if ((RadoRead (RADEON_MC_STATUS) & R300_MC_IDLE) != 0) {
      break;
    }

    CpuPause ();
  }

  RadoWrite (
    RADEON_MC_FB_LOCATION,
    ((vram_end >> 16) << 16) | ((mVramStart >> 16) & 0xffffu)
    );
  RadoWrite (RADEON_MC_AGP_LOCATION, agp_loc);

  for (i = 0; i < RADO_WAIT_SPINS; i++) {
    if ((RadoRead (RADEON_MC_STATUS) & R300_MC_IDLE) != 0) {
      break;
    }

    CpuPause ();
  }

  RadoWrite (RADEON_CRTC_EXT_CNTL, ext);
  return 0;
}

STATIC
INT32
RadoPcieGartAccessible (
  VOID
  )
{
  UINT32  save;
  UINT32  rd;

  /*
   * GART_ERROR is status/W1C — not a scratch pad (always read 0).
   * START_LO is real R/W config; prove PCIE_INDEX/DATA with that.
   */
  save = RadoPcieRead (RADEON_PCIE_TX_GART_START_LO);
  RadoPcieWrite (RADEON_PCIE_TX_GART_START_LO, 0x12345000u);
  rd = RadoPcieRead (RADEON_PCIE_TX_GART_START_LO);
  RadoPcieWrite (RADEON_PCIE_TX_GART_START_LO, save);
  if (rd != 0x12345000u) {
    pm_metal_logf_styled (
      PM_METAL_LOG_STYLE_DIM,
      "metal-gfx: radeon_rv370 skip pcie-idx got=%x",
      rd
      );
    return -1;
  }

  return 0;
}

STATIC
VOID
RadoGartDisable (
  VOID
  )
{
  if (mMmio == NULL) {
    return;
  }

  RadoPcieWrite (
    RADEON_PCIE_TX_GART_CNTL,
    RADEON_PCIE_TX_GART_UNMAPPED_ACCESS_DISCARD
    );
  RadoPcieWrite (RADEON_PCIE_TX_GART_START_LO, 0);
  RadoPcieWrite (RADEON_PCIE_TX_GART_END_LO, 0);
}

STATIC
INT32
RadoGartEnableWithBase (
  UINT32  table_base
  )
{
  UINT32  tmp;

  SetMem (mGart, RADO_GART_BYTES, 0);
  RadoClflush (mGart, RADO_GART_BYTES);
  RadoHdpFlush ();

  tmp = RADEON_PCIE_TX_GART_UNMAPPED_ACCESS_DISCARD;
  RadoPcieWrite (RADEON_PCIE_TX_GART_CNTL, tmp);
  RadoPcieWrite (RADEON_PCIE_TX_GART_START_LO, mGttStart);
  RadoPcieWrite (
    RADEON_PCIE_TX_GART_END_LO,
    mGttStart + RADO_GART_PAGES * 4096u - 4096u
    );
  RadoPcieWrite (RADEON_PCIE_TX_GART_START_HI, 0);
  RadoPcieWrite (RADEON_PCIE_TX_GART_END_HI, 0);
  RadoPcieWrite (RADEON_PCIE_TX_GART_BASE, table_base);
  RadoPcieWrite (RADEON_PCIE_TX_DISCARD_RD_ADDR_LO, mVramStart);
  RadoPcieWrite (RADEON_PCIE_TX_DISCARD_RD_ADDR_HI, 0);
  RadoPcieWrite (RADEON_PCIE_TX_GART_ERROR, 0);

  tmp  = RadoPcieRead (RADEON_PCIE_TX_GART_CNTL);
  tmp |= RADEON_PCIE_TX_GART_EN | RADEON_PCIE_TX_GART_UNMAPPED_ACCESS_DISCARD;
  RadoPcieWrite (RADEON_PCIE_TX_GART_CNTL, tmp);
  RadoGartTlbFlush ();

  tmp = RadoPcieRead (RADEON_PCIE_TX_GART_CNTL);
  if ((tmp & RADEON_PCIE_TX_GART_EN) == 0) {
    return -1;
  }

  return 0;
}

/** CONFIG_MEMSIZE is bytes on r100–r500; 0 means 8 MiB (Linux quirk). */
STATIC
UINT32
RadoNormMemsize (
  UINT32  raw
  )
{
  if (raw == 0) {
    return 8u * 1024u * 1024u;
  }

  if (raw >= 8u * 1024u * 1024u && raw <= 512u * 1024u * 1024u) {
    return raw;
  }

  /* Some firmwares leave size in MiB in the low byte. */
  if (raw >= 8u && raw <= 512u) {
    return raw * 1024u * 1024u;
  }

  return 0;
}

/** Find MMIO BAR: CONFIG_MEMSIZE must look like a real VRAM size. */
STATIC
UINT64
RadoFindMmio (
  UINT8   bus,
  UINT8   dev,
  UINT8   func,
  UINT64  vram_bar
  )
{
  UINT8   bars[4] = { 2, 0, 1, 5 };
  UINT32  i;
  UINT8   cons;
  UINT64  base;
  UINT32  mem;
  UINT32  status;

  for (i = 0; i < 4u; i++) {
    base = pm_bios_pci_bar_mmio (bus, dev, func, bars[i], &cons);
    if (base == 0 || base == vram_bar) {
      continue;
    }

    mem    = *(volatile UINT32 *)(UINTN)(base + RADEON_CONFIG_MEMSIZE);
    status = *(volatile UINT32 *)(UINTN)(base + RADEON_RBBM_STATUS);
    if (status == 0xffffffffu) {
      continue;
    }

    if (RadoNormMemsize (mem) != 0) {
      return base;
    }
  }

  return 0;
}

STATIC
INT32
RadoProbe (
  CONST pm_metal_scanout_bind_t  *b
  )
{
  UINT8    bus;
  UINT8    dev;
  UINT8    func;
  UINT8    cons;
  UINT64   vram_bar;
  UINT64   mmio_bar;
  UINT64   fb_phys;
  UINT32   memsize;
  UINT32   page_bytes;
  UINT32   front_off;
  UINT32   mc_fb;
  UINT32   fw_off;
  UINT32   i;
  UINT32  *scratch_cpu;
  UINT32  *sys_page;
  INT32    snoop;
  INT32    ok;

  mReady        = 0;
  mFaulted      = 0;
  mMmio         = NULL;
  mVram         = NULL;
  mGart         = NULL;
  mShadowMapped = NULL;
  mShadowPages  = 0;

  if (b == NULL || b->fb == NULL || !b->owned
      || b->mode_w == 0 || b->mode_h == 0 || b->fb_ppsl == 0)
  {
    return -1;
  }

  if (pm_bios_pci_find (RADO_VENDOR, RADO_DEVICE, &bus, &dev, &func) != 0) {
    return -1; /* quiet — not this machine */
  }

  /* Never size-probe live BARs. */
  pm_bios_pci_enable_mem_bm (bus, dev, func);
  vram_bar = pm_bios_pci_bar_mmio (bus, dev, func, 0, &cons);
  if (vram_bar == 0) {
    RadoSkip ("bar0");
    return -1;
  }

  mmio_bar = RadoFindMmio (bus, dev, func, vram_bar);
  if (mmio_bar == 0) {
    RadoSkip ("mmio");
    return -1;
  }

  mMmio = (UINT8 *)(UINTN)mmio_bar;
  mVram = (UINT8 *)(UINTN)vram_bar;

  memsize = RadoNormMemsize (RadoRead (RADEON_CONFIG_MEMSIZE));
  if (memsize == 0) {
    RadoSkip ("memsize");
    goto fail;
  }

  mc_fb      = RadoRead (RADEON_MC_FB_LOCATION);
  mVramStart = (mc_fb & 0xffffu) << 16;
  mVramSize  = memsize;

  fb_phys = (UINT64)(UINTN)b->fb;
  if (fb_phys >= vram_bar && (fb_phys - vram_bar) < (UINT64)mVramSize) {
    mAperOff = (UINT32)(fb_phys - vram_bar);
  } else {
    /* VESA PhysBasePtr can differ from BAR0; still use BAR0 for CPU. */
    mAperOff = 0;
  }

  /* 2D pitch/offset uses MC addresses. CRTC_OFFSET may be aper-relative. */
  fw_off = RadoRead (RADEON_CRTC_OFFSET) & RADEON_CRTC_OFFSET_MASK;
  if (fw_off >= mVramStart) {
    mFrontMc = fw_off;
  } else {
    mFrontMc = mVramStart + ((fw_off != 0) ? fw_off : mAperOff);
  }

  front_off = mFrontMc - mVramStart;

  mPitchBytes = b->fb_ppsl * sizeof (UINT32);
  if ((mPitchBytes < 64u) || ((mPitchBytes & 63u) != 0)) {
    RadoSkip ("pitch");
    goto fail;
  }

  page_bytes = b->mode_h * mPitchBytes;
  mPageBytes = page_bytes;

  /*
   * Layout in proven VRAM: front | scratch | disc page | gart table
   * (not at aperture end — CONFIG_MEMSIZE can overstate).
   */
  mScratchMc     = mFrontMc + page_bytes;
  mGartTableAper = (front_off + page_bytes * 2u + 4096u + 4095u) & ~4095u;
  if (mGartTableAper + RADO_GART_BYTES + 4096u > mVramSize
      || mGartTableAper < 4096u)
  {
    RadoSkip ("vram");
    goto fail;
  }

  mGartTableMc  = mVramStart + mGartTableAper;
  mGartTablePci = (UINT32)vram_bar + mGartTableAper;
  mGttStart     = ((mVramStart + mVramSize) + 0xfffffu) & ~0xfffffu;
  if (mGttStart < mVramStart + mVramSize) {
    mGttStart = mVramStart + mVramSize;
  }

  mGart = (UINT32 *)(UINTN)(mVram + mGartTableAper);

  RadoGuiKick ();

  /* 1) Engine alive: VRAM→scratch (not scanned out). */
  if (RadoBlitMc (
        mFrontMc,
        mScratchMc,
        mPitchBytes,
        mPitchBytes,
        0,
        0,
        (INT32)RADO_PROBE_PIXELS,
        1
        ) != 0)
  {
    RadoSkip ("blit-vram");
    goto fail;
  }

  scratch_cpu = (UINT32 *)(UINTN)(mVram + (mScratchMc - mVramStart));

  /* CPU↔VRAM path must work before trusting GART. */
  for (i = 0; i < RADO_PROBE_PIXELS; i++) {
    scratch_cpu[i] = 0xC0DE0000u | i;
  }

  RadoClflush (scratch_cpu, RADO_PROBE_PIXELS * sizeof (UINT32));
  RadoHdpFlush ();
  for (i = 0; i < RADO_PROBE_PIXELS; i++) {
    if (scratch_cpu[i] != (0xC0DE0000u | i)) {
      RadoSkip ("cpu-vram");
      goto fail;
    }
  }

  /* GART table must be real VRAM (not phantom aperture). */
  mGart[0] = 0x41525431u; /* 'ART1' */
  RadoClflush (mGart, sizeof (UINT32));
  RadoHdpFlush ();
  if (mGart[0] != 0x41525431u) {
    RadoSkip ("gart-mem");
    goto fail;
  }

  if (RadoPcieGartAccessible () != 0) {
    goto fail;
  }

  /* 2) GART ← system RAM. */
  sys_page = (UINT32 *)pm_metal_mem_map (4096u);
  if (sys_page == NULL || (((UINTN)sys_page) & 4095u) != 0) {
    RadoSkip ("sys-page");
    goto fail;
  }

  for (i = 0; i < RADO_PROBE_PIXELS; i++) {
    sys_page[i] = 0xA5A50000u | i;
  }

  RadoWbinvd ();
  ok = 0;
  {
    UINT32  bases[2];
    UINT32  agps[2];
    UINT32  gtt_end;
    UINT32  bi;
    UINT32  ai;
    UINT32  discard_pat;
    UINT32  disc_mc;
    UINT32  *disc_cpu;

    gtt_end  = mGttStart + RADO_GART_PAGES * 4096u - 1u;
    /* Prefer AGP window = GTT (MC routing); Linux 0x0FFFFFFF as fallback. */
    agps[0]  = ((gtt_end >> 16) << 16) | ((mGttStart >> 16) & 0xffffu);
    agps[1]  = 0x0FFFFFFFu;
    bases[0] = mGartTableMc;
    bases[1] = mGartTablePci;
    discard_pat = 0xD15C0001u;
    disc_mc     = mGartTableMc - 4096u;
    disc_cpu    = (UINT32 *)(UINTN)(mVram + (disc_mc - mVramStart));

    for (ai = 0; ai < 2u && !ok; ai++) {
      if (RadoMcSetup (agps[ai]) != 0) {
        continue;
      }

      for (bi = 0; bi < 2u && !ok; bi++) {
        if (RadoGartEnableWithBase (bases[bi]) != 0) {
          continue;
        }

        /*
         * Discard path: PTE=0, DISCARD_RD → known VRAM dword.
         * Proves GTT hits GART without system DMA.
         */
        disc_cpu[0] = discard_pat;
        RadoClflush (disc_cpu, sizeof (UINT32));
        RadoHdpFlush ();
        RadoPcieWrite (RADEON_PCIE_TX_DISCARD_RD_ADDR_LO, disc_mc);
        RadoPcieWrite (RADEON_PCIE_TX_DISCARD_RD_ADDR_HI, 0);

        for (i = 0; i < RADO_PROBE_PIXELS; i++) {
          scratch_cpu[i] = 0xFFFFFFFFu;
        }

        RadoClflush (scratch_cpu, RADO_PROBE_PIXELS * sizeof (UINT32));
        mGart[0] = 0; /* unmapped → discard */
        RadoGartTableSync ();

        if (RadoBlitMc (
              mGttStart,
              mScratchMc,
              256u * sizeof (UINT32),
              mPitchBytes,
              0,
              0,
              1,
              1
              ) != 0)
        {
          RadoGartDisable ();
          continue;
        }

        RadoHdpFlush ();
        RadoClflush (scratch_cpu, sizeof (UINT32));
        if (scratch_cpu[0] != discard_pat) {
          pm_metal_logf_styled (
            PM_METAL_LOG_STYLE_DIM,
            "metal-gfx: radeon_rv370 gart-disc got=%x agp=%x base=%x cntl=%x",
            scratch_cpu[0],
            agps[ai],
            bases[bi],
            RadoPcieRead (RADEON_PCIE_TX_GART_CNTL)
            );
          RadoGartDisable ();
          continue;
        }

        /* System RAM PTE. */
        for (snoop = 1; snoop >= 0 && !ok; snoop--) {
          for (i = 0; i < RADO_PROBE_PIXELS; i++) {
            scratch_cpu[i] = 0xFFFFFFFFu;
          }

          RadoClflush (scratch_cpu, RADO_PROBE_PIXELS * sizeof (UINT32));
          RadoClflush (sys_page, 4096u);
          RadoWbinvd ();
          mGart[0] = RadoPte ((UINTN)sys_page, snoop);
          RadoGartTableSync ();

          if (RadoBlitMc (
                mGttStart,
                mScratchMc,
                256u * sizeof (UINT32),
                mPitchBytes,
                0,
                0,
                (INT32)RADO_PROBE_PIXELS,
                1
                ) != 0)
          {
            continue;
          }

          RadoHdpFlush ();
          RadoClflush (scratch_cpu, RADO_PROBE_PIXELS * sizeof (UINT32));
          ok = 1;
          for (i = 0; i < RADO_PROBE_PIXELS; i++) {
            if (scratch_cpu[i] != sys_page[i]) {
              ok = 0;
              break;
            }
          }
        }

        if (!ok) {
          RadoGartDisable ();
        }
      }
    }
  }

  if (!ok) {
    pm_metal_logf_styled (
      PM_METAL_LOG_STYLE_DIM,
      "metal-gfx: radeon_rv370 skip readback got=%x err=%x fb=%x gtt=%x cntl=%x",
      scratch_cpu[0],
      RadoPcieRead (RADEON_PCIE_TX_GART_ERROR),
      RadoRead (RADEON_MC_FB_LOCATION),
      mGttStart,
      RadoPcieRead (RADEON_PCIE_TX_GART_CNTL)
      );
    (VOID)pm_metal_mem_unmap (sys_page, 4096u);
    goto fail_gart;
  }

  (VOID)pm_metal_mem_unmap (sys_page, 4096u);
  mReady = 1;
  return 0;

fail_gart:
  RadoGartDisable ();
fail:
  mMmio = NULL;
  mVram = NULL;
  mGart = NULL;
  return -1;
}

STATIC
INT32
RadoPresentRect (
  INT32  x,
  INT32  y,
  INT32  w,
  INT32  h
  )
{
  if (!mReady || mFaulted) {
    return -1;
  }

  if (RadoEmitBlit (x, y, w, h) != 0) {
    RadoFault ();
    return -1;
  }

  return 0;
}

STATIC
INT32
RadoJobBegin (
  INT32  x,
  INT32  y,
  INT32  w,
  INT32  h
  )
{
  if (!mReady || mFaulted) {
    return -1;
  }

  if (RadoEmitBlit (x, y, w, h) != 0) {
    RadoFault ();
    return -1;
  }

  return 0; /* finished — no async spin on RBBM */
}

STATIC
INT32
RadoJobStep (
  VOID
  )
{
  return 0;
}

STATIC
UINT32
RadoCaps (
  VOID
  )
{
  return 0;
}

STATIC
VOID
RadoFini (
  VOID
  )
{
  if (mMmio != NULL) {
    (VOID)RadoWaitGuiIdle ();
    RadoGartDisable ();
  }

  mReady        = 0;
  mFaulted      = 0;
  mMmio         = NULL;
  mVram         = NULL;
  mGart         = NULL;
  mShadowMapped = NULL;
}

CONST pm_metal_scanout_ops_t  g_pm_metal_scanout_radeon_rv370 = {
  "radeon_rv370",
  RadoProbe,
  RadoPresentRect,
  RadoJobBegin,
  RadoJobStep,
  RadoCaps,
  NULL,
  NULL,
  RadoFini
};
