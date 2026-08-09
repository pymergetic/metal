/** @file
  Radeon scanout — ThinkPad T43 Mobility X300 / RV370 (PCI 1002:5460).

  Linux/Haiku rules for this chip:
    - MMIO 2D only after CP is off (CP_CSQ_MODE=0, CP_CSQ_CNTL=PRIDIS_INDDIS).
      MMIO GUI while CP/BM is live wedges RBBM (VESA splash freeze).
    - No soft-reset in probe (can kill display / hang host).
    - PCIe GART table lives in VRAM; GTT copies go through CP/DMA (0x720),
      not MMIO BITBLT — GART present is a later step.
    - Firmware CRTC untouched (no pitch/flip).

  Present paths:
    1) GART (fast): WB shadow → PCIe GART → CP blit → front
       Async job_begin submits; job_step fences (no busy-wait).
       UI present_rect uses the same blit, sync-fenced.
    2) Staging (fallback): CPU → VRAM scratch → MMIO BITBLT → front
**/
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/dev/gfx/scanout.h>
#include "pymergetic/metal/dev/gfx/compat.h"
#include "pymergetic/metal/dev/gfx/compat.h"
#include "pymergetic/metal/mem.h"
#include "pymergetic/metal/dev/gfx/compat.h"
#include "pymergetic/metal/dev/gfx/compat.h"
#include "pymergetic/metal/bus/pci.h"
#include "pymergetic/metal/dev/gfx/compat.h"

#include "fw/r300_cp.inc.c"

#define RADO_VENDOR 0x1002u
#define RADO_DEVICE 0x5460u

#define RADEON_PCIE_INDEX                 0x0030u
#define RADEON_PCIE_DATA                  0x0034u
#define RADEON_RV370_BUS_CNTL             0x004cu
#define RADEON_CONFIG_MEMSIZE             0x00f8u
#define RADEON_CLOCK_CNTL_INDEX           0x0008u
#define RADEON_CLOCK_CNTL_DATA            0x000cu
#define RADEON_PLL_WR_EN                  (1u << 7)
#define RADEON_SCLK_PLL                   0x000du /* PLL index */
#define RADEON_SCLK_FORCE_CP              (1u << 16)
#define RADEON_CRTC_GEN_CNTL              0x0050u
#define RADEON_CRTC_EXT_CNTL              0x0054u
#define RADEON_CRTC_DISPLAY_DIS           (1u << 10)
#define RADEON_CRTC_CUR_EN                (1u << 16)
#define RADEON_CRTC_DISP_REQ_EN_B         (1u << 26)
#define RADEON_CRTC2_DISPLAY_DIS          (1u << 23)
#define RADEON_HOST_PATH_CNTL             0x0130u
#define RADEON_MC_FB_LOCATION             0x0148u
#define RADEON_MC_AGP_LOCATION            0x014cu
#define RADEON_MC_STATUS                  0x0150u
#define R300_MC_IDLE                      (1u << 4)
#define RADEON_AGP_BASE                   0x0170u
#define RADEON_AGP_BASE_2                 0x015cu
#define RADEON_CRTC_OFFSET                0x0224u
#define RADEON_DISPLAY_BASE_ADDR          0x023cu
#define RADEON_CUR_OFFSET                 0x0260u
#define RADEON_CUR_LOCK                   (1u << 31)
#define RADEON_CRTC2_DISPLAY_BASE_ADDR    0x033cu
#define RADEON_CUR2_OFFSET                0x0360u
#define RADEON_GENMO_WT                   0x03c2u
#define RADEON_CFG_VGA_RAM_EN             (1u << 8)
#define RADEON_CRTC2_GEN_CNTL             0x03f8u
#define RADEON_OV0_SCALE_CNTL             0x0420u
#define RADEON_CP_RB_BASE                 0x0700u
#define RADEON_CP_RB_CNTL                 0x0704u
#define RADEON_CP_RB_RPTR_ADDR            0x070cu
#define RADEON_CP_RB_RPTR                 0x0710u
#define RADEON_CP_RB_WPTR                 0x0714u
#define RADEON_CP_RB_WPTR_DELAY           0x0718u
#define RADEON_CP_RB_RPTR_WR              0x071cu
#define RADEON_CP_CSQ_CNTL                0x0740u
#define RADEON_CP_CSQ_MODE                0x0744u
#define RADEON_SCRATCH_UMSK               0x0770u
#define RADEON_CP_ME_RAM_ADDR             0x07d4u
#define RADEON_CP_ME_RAM_DATAH            0x07dcu
#define RADEON_CP_ME_RAM_DATAL            0x07e0u
#define RADEON_RBBM_STATUS                0x0e40u
#define RADEON_HDP_READ_BUFFER_INVALIDATE (1u << 27)
#define RADEON_BUS_MASTER_DIS             (1u << 6)
#define RADEON_CSQ_PRIDIS_INDDIS          0u
#define RADEON_CSQ_PRIBM_INDBM            (4u << 28)
#define RADEON_RBBM_CP_CMDSTRM_BUSY       (1u << 16)
#define RADEON_RB_NO_UPDATE               (1u << 27)
#define RADEON_RB_RPTR_WR_ENA             (1u << 31)
#define RADEON_CP_PACKET0                 0x00000000u
#define RADEON_CP_PACKET2                 0x80000000u
#define RADEON_CP_PACKET3                 0xC0000000u
#define RADEON_PACKET3_BITBLT_MULTI       0x9Bu
#define RADEON_SRC_PITCH_OFFSET           0x1428u
#define RADEON_DST_PITCH_OFFSET           0x142cu
#define RADEON_SRC_Y_X                    0x1434u
#define RADEON_DST_Y_X                    0x1438u
#define RADEON_DST_HEIGHT_WIDTH           0x143cu
#define RADEON_DP_GUI_MASTER_CNTL         0x146cu
#define RADEON_DP_CNTL                    0x16c0u
#define RADEON_DP_WRITE_MASK              0x16ccu
#define RADEON_DSTCACHE_CTLSTAT           0x1714u
#define RADEON_RB2D_DSTCACHE_CTLSTAT      0x342cu
#define RADEON_WAIT_UNTIL                 0x1720u
#define RADEON_DEFAULT_SC_BOTTOM_RIGHT    0x16e8u
#define RADEON_SC_TOP_LEFT                0x16ecu
#define RADEON_SC_BOTTOM_RIGHT            0x16f0u
#define RADEON_SRC_SC_BOTTOM_RIGHT        0x16f4u

#define RADEON_CRTC_OFFSET_MASK    0x07ffffffu
#define RADEON_RBBM_FIFOCNT_MASK   0x007fu
#define RADEON_RBBM_ACTIVE         (1u << 31)
#define RADEON_RB2D_DC_FLUSH_ALL   0xfu
#define RADEON_DST_X_LEFT_TO_RIGHT (1u << 0)
#define RADEON_DST_Y_TOP_TO_BOTTOM (1u << 1)
#define RADEON_WAIT_2D_IDLECLEAN   (1u << 16)
#define RADEON_WAIT_HOST_IDLECLEAN (1u << 17)
#define RADEON_WAIT_DMA_GUI_IDLE   (1u << 9)
#define RADEON_DMA_COPY            0x0720u /* PACKET0: src, dst, size|flags */
#define RADEON_SC_MAX              ((0x1fffu << 16) | 0x1fffu)

#define RADEON_GMC_SRC_PITCH_OFFSET_CNTL (1u << 0)
#define RADEON_GMC_DST_PITCH_OFFSET_CNTL (1u << 1)
#define RADEON_GMC_SRC_CLIPPING          (1u << 2)
#define RADEON_GMC_DST_CLIPPING          (1u << 3)
#define RADEON_GMC_BRUSH_NONE            (15u << 4)
#define RADEON_GMC_DST_32BPP             (6u << 8) /* COLOR_FORMAT_ARGB8888 */
#define RADEON_GMC_SRC_DATATYPE_COLOR    (3u << 12)
#define RADEON_ROP3_S                    0x00cc0000u
#define RADEON_DP_SRC_SOURCE_MEMORY      (2u << 24)
#define RADEON_GMC_CLR_CMP_CNTL_DIS      (1u << 28)
#define RADEON_GMC_WR_MSK_DIS            (1u << 30)

/** Pitch/offset reg drops addr[9:0] — surfaces must be 1 KiB aligned. */
#define RADO_MC_ALIGN 1024u

#define RADEON_PCIE_TX_GART_CNTL                    0x10u
#define RADEON_PCIE_TX_DISCARD_RD_ADDR_LO           0x11u
#define RADEON_PCIE_TX_DISCARD_RD_ADDR_HI           0x12u
#define RADEON_PCIE_TX_GART_BASE                    0x13u
#define RADEON_PCIE_TX_GART_START_LO                0x14u
#define RADEON_PCIE_TX_GART_START_HI                0x15u
#define RADEON_PCIE_TX_GART_END_LO                  0x16u
#define RADEON_PCIE_TX_GART_END_HI                  0x17u
#define RADEON_PCIE_TX_GART_ERROR                   0x18u
#define RADEON_PCIE_TX_GART_EN                      (1u << 0)
#define RADEON_PCIE_TX_GART_UNMAPPED_ACCESS_DISCARD (3u << 1)
#define RADEON_PCIE_TX_GART_CHK_RW_VALID_EN         (1u << 5)
#define RADEON_PCIE_TX_GART_INVALIDATE_TLB          (1u << 8)
/* EN + DISCARD + validate R/W PTE bits (PTE=0 must not map phys 0). */
#define RADO_GART_CNTL_ON                                                 \
  (RADEON_PCIE_TX_GART_EN | RADEON_PCIE_TX_GART_UNMAPPED_ACCESS_DISCARD | \
   RADEON_PCIE_TX_GART_CHK_RW_VALID_EN)
#define RADO_FRONT_TEAL 0xff2e86abu
#define RADO_DISC_PAT   0xd15cd15cu

/* GART/MC probe chatter — off so boot tree stays clean; set 1 to debug. */
#ifndef RADO_PROBE_LOG
#define RADO_PROBE_LOG 0
#endif
#define RADO_LOG(...)                                            \
  do {                                                           \
    if (RADO_PROBE_LOG) {                                        \
      pm_metal_gfx_logf(__VA_ARGS__); \
    }                                                            \
  } while (0)

#define R300_PTE_UNSNOOPED (1u << 0)
#define R300_PTE_WRITEABLE (1u << 2)
#define R300_PTE_READABLE  (1u << 3)

#define RADO_WAIT_SPINS   500000u
#define RADO_PROBE_PIXELS 64u
#define RADO_GART_PAGES   2048u /* 8 MiB GTT */
#define RADO_GART_BYTES   (RADO_GART_PAGES * 4u)
#define RADO_RING_DWORDS  4096u /* 16 KiB ring in VRAM */
#define RADO_RING_BYTES   (RADO_RING_DWORDS * 4u)

#define RADO_PACKET0(reg, n) (RADEON_CP_PACKET0 | (((reg) >> 2) & 0x1fffu) | ((uint32_t)(n) << 16))
#define RADO_PACKET3(op, n) \
  (RADEON_CP_PACKET3 | (((uint32_t)(op) & 0xffu) << 8) | ((uint32_t)(n) << 16))

static int32_t        mReady;
static int32_t        mFaulted;
static int32_t        mGartOk;  /* 1 = CP+GART present path live */
static int32_t        mMcAper;  /* 1 = MC_FB relocated to BAR0 (Linux) */
static int32_t        mJobLive; /* async present fence outstanding */
static uint8_t       *mMmio;
static uint8_t       *mVram;
static uint64_t       mVramBar;
static uint32_t       mVramStart;
static uint32_t       mVramSize;
static uint32_t       mAperOff;
static uint32_t       mPageBytes;
static uint32_t       mPitchBytes;
static uint32_t       mFrontMc;
static uint32_t       mScratchMc; /* offscreen VRAM staging / probe */
static uint32_t       mRingMc;
static uint32_t      *mRing;
static uint32_t       mRingWptr;
static uint32_t       mRingDirtyFrom; /* first dword of uncommitted packet */
static uint32_t       mGttStart;
static uint32_t       mGartTableMc;
static uint32_t      *mGart;
static uint32_t       mShadowPages;
static const uint8_t *mShadowMapped;
static uint32_t       mFwMcFb;        /* firmware MC_FB_LOCATION */
static uint32_t       mFwDisplayBase; /* firmware DISPLAY_BASE_ADDR */
static uint32_t       mFwDisplayBase2;

static uint32_t RadoRead(uint32_t off)
{
  return *(volatile uint32_t *)(uintptr_t)(mMmio + off);
}

static void RadoWrite(uint32_t off, uint32_t val)
{
  *(volatile uint32_t *)(uintptr_t)(mMmio + off) = val;
  pm_metal_mem_fence();
}

static uint32_t RadoPcieRead(uint32_t reg)
{
  RadoWrite(RADEON_PCIE_INDEX, reg & 0xffu);
  return RadoRead(RADEON_PCIE_DATA);
}

static void RadoPcieWrite(uint32_t reg, uint32_t val)
{
  RadoWrite(RADEON_PCIE_INDEX, reg & 0xffu);
  RadoWrite(RADEON_PCIE_DATA, val);
}

static uint32_t RadoBe32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t RadoPte(uintptr_t phys, int32_t snoop)
{
  uint64_t p;
  uint32_t e;

  p = (uint64_t)phys & ~0xfffull;
  e = (uint32_t)((p >> 8) & 0x00ffffffu);
  e |= (uint32_t)(((p >> 32) & 0xffu) << 24);
  e |= R300_PTE_READABLE | R300_PTE_WRITEABLE;
  if (!snoop) {
    e |= R300_PTE_UNSNOOPED;
  }

  return e;
}

static void RadoClflush(const void *addr, uintptr_t len)
{
  const uint8_t *p;
  uintptr_t      off;

  if (addr == NULL || len == 0) {
    return;
  }

  p = (const uint8_t *)addr;
  for (off = 0; off < len; off += 64u) {
    __asm__ __volatile__("clflush (%0)" : : "r"(p + off) : "memory");
  }

  __asm__ __volatile__("mfence" ::: "memory");
}

/** Flush only the dirty pixel span (not full pitch × height). */
static void RadoClflushRect(
  const uint8_t *base, uint32_t pitch, int32_t x, int32_t y, int32_t w, int32_t h)
{
  int32_t   row;
  uintptr_t bytes;

  if (base == NULL || w <= 0 || h <= 0) {
    return;
  }

  bytes = (uintptr_t)w * sizeof(uint32_t);
  for (row = 0; row < h; row++) {
    RadoClflush(base + (uintptr_t)(y + row) * (uintptr_t)pitch + (uintptr_t)x * sizeof(uint32_t),
                bytes);
  }
}

static int32_t RadoGuiIdle(void)
{
  return ((RadoRead(RADEON_RBBM_STATUS) & RADEON_RBBM_ACTIVE) == 0) ? 1 : 0;
}

static int32_t RadoWaitFifo(uint32_t n)
{
  uint32_t i;

  for (i = 0; i < RADO_WAIT_SPINS; i++) {
    if ((RadoRead(RADEON_RBBM_STATUS) & RADEON_RBBM_FIFOCNT_MASK) >= n) {
      return 0;
    }

    pm_metal_cpu_pause();
  }

  return -1;
}

static int32_t RadoWaitGuiIdle(void)
{
  uint32_t i;

  for (i = 0; i < RADO_WAIT_SPINS; i++) {
    if (RadoGuiIdle()) {
      return 0;
    }

    pm_metal_cpu_pause();
  }

  return -1;
}

static void RadoFault(void)
{
  mFaulted = 1;
  mReady   = 0;
}

static uint32_t RadoAlignMc(uint32_t mc)
{
  return (mc + (RADO_MC_ALIGN - 1u)) & ~(RADO_MC_ALIGN - 1u);
}

static int32_t RadoPitchOffset(uint32_t pitch_bytes, uint32_t mc_off, uint32_t *out)
{
  if ((pitch_bytes < 64u) || ((pitch_bytes & 63u) != 0) || ((mc_off & (RADO_MC_ALIGN - 1u)) != 0)) {
    return -1;
  }

  *out = ((pitch_bytes / 64u) << 22) | ((mc_off >> 10) & 0x003fffffu);
  return 0;
}

static void RadoHdpFlush(void)
{
  uint32_t hdp;

  /* GPU→host: invalidate HDP read buffer so CPU sees VRAM writes. */
  hdp = RadoRead(RADEON_HOST_PATH_CNTL);
  RadoWrite(RADEON_HOST_PATH_CNTL, hdp | RADEON_HDP_READ_BUFFER_INVALIDATE);
  RadoWrite(RADEON_HOST_PATH_CNTL, hdp);
}

static void RadoSkip(const char *why)
{
  (void)why;
  RADO_LOG("metal-gfx: radeon_rv370 skip %s", why);
}

/**
 * Put CP into PIO-disabled mode so MMIO 2D is safe.
 * Matches Linux r100_cp_disable / Haiku CP_setup (no soft-reset).
 */
static int32_t RadoCpDisable(void)
{
  uint32_t i;
  uint32_t st;

  for (i = 0; i < RADO_WAIT_SPINS; i++) {
    st = RadoRead(RADEON_RBBM_STATUS);
    if ((st & RADEON_RBBM_CP_CMDSTRM_BUSY) == 0) {
      break;
    }

    pm_metal_cpu_pause();
  }

  RadoWrite(RADEON_CP_CSQ_MODE, 0);
  RadoWrite(RADEON_CP_CSQ_CNTL, RADEON_CSQ_PRIDIS_INDDIS);
  RadoWrite(RADEON_SCRATCH_UMSK, 0);
  (void)RadoRead(RADEON_CP_CSQ_CNTL);
  mGartOk = 0;

  return RadoWaitGuiIdle();
}

static void RadoGartDisable(void);

static void RadoGartTlbFlush(void)
{
  uint32_t tmp;
  uint32_t i;

  for (i = 0; i < 2u; i++) {
    tmp = RadoPcieRead(RADEON_PCIE_TX_GART_CNTL);
    RadoPcieWrite(RADEON_PCIE_TX_GART_CNTL, tmp | RADEON_PCIE_TX_GART_INVALIDATE_TLB);
    (void)RadoPcieRead(RADEON_PCIE_TX_GART_CNTL);
    RadoPcieWrite(RADEON_PCIE_TX_GART_CNTL, tmp);
  }

  pm_metal_mem_fence();
}

static void RadoGartTableSync(void)
{
  /* WC VRAM: force PTE stores out, then invalidate HDP + TLB (×2). */
  pm_metal_mem_fence();
  RadoClflush(mGart, RADO_GART_BYTES);
  RadoHdpFlush();
  RadoGartTlbFlush();
}

static uint32_t RadoPllRead(uint32_t index)
{
  RadoWrite(RADEON_CLOCK_CNTL_INDEX, index & 0x3fu);
  return RadoRead(RADEON_CLOCK_CNTL_DATA);
}

static void RadoPllWrite(uint32_t index, uint32_t val)
{
  RadoWrite(RADEON_CLOCK_CNTL_INDEX, (index & 0x3fu) | RADEON_PLL_WR_EN);
  RadoWrite(RADEON_CLOCK_CNTL_DATA, val);
  RadoWrite(RADEON_CLOCK_CNTL_INDEX, index & 0x3fu);
}

static void RadoForceCpClock(void)
{
  uint32_t sclk;

  /* Linux r300_clock_startup — CP must be force-on for ring/DMA. */
  sclk = RadoPllRead(RADEON_SCLK_PLL);
  sclk |= RADEON_SCLK_FORCE_CP;
  RadoPllWrite(RADEON_SCLK_PLL, sclk);
}

static void RadoCpLoadFw(void)
{
  uint32_t i;
  uint32_t words;

  (void)RadoWaitGuiIdle();
  words = g_rado_r300_cp_fw_len / 4u;
  RadoWrite(RADEON_CP_ME_RAM_ADDR, 0);
  for (i = 0; i + 1u < words; i += 2u) {
    RadoWrite(RADEON_CP_ME_RAM_DATAH, RadoBe32(&g_rado_r300_cp_fw[i * 4u]));
    RadoWrite(RADEON_CP_ME_RAM_DATAL, RadoBe32(&g_rado_r300_cp_fw[(i + 1u) * 4u]));
  }
}

static int32_t RadoRingWaitSpace(uint32_t ndw)
{
  uint32_t i;
  uint32_t rptr;
  uint32_t free;

  for (i = 0; i < RADO_WAIT_SPINS; i++) {
    rptr = RadoRead(RADEON_CP_RB_RPTR) & (RADO_RING_DWORDS - 1u);
    if (mRingWptr >= rptr) {
      free = RADO_RING_DWORDS - 1u - (mRingWptr - rptr);
    } else {
      free = rptr - mRingWptr - 1u;
    }

    if (free >= ndw) {
      return 0;
    }

    pm_metal_cpu_pause();
  }

  return -1;
}

static void RadoRingMark(void)
{
  mRingDirtyFrom = mRingWptr;
}

static void RadoRingWrite(uint32_t v)
{
  mRing[mRingWptr] = v;
  mRingWptr        = (mRingWptr + 1u) & (RADO_RING_DWORDS - 1u);
}

static void RadoRingCommit(void)
{
  uint32_t from;
  uint32_t to;

  pm_metal_mem_fence();
  from = mRingDirtyFrom;
  to   = mRingWptr;
  if (from != to) {
    if (from < to) {
      RadoClflush(&mRing[from], (uintptr_t)(to - from) * sizeof(uint32_t));
    } else {
      RadoClflush(&mRing[from], (uintptr_t)(RADO_RING_DWORDS - from) * sizeof(uint32_t));
      if (to > 0u) {
        RadoClflush(mRing, (uintptr_t)to * sizeof(uint32_t));
      }
    }
  }

  RadoWrite(RADEON_CP_RB_WPTR, mRingWptr);
  (void)RadoRead(RADEON_CP_RB_WPTR);
}

static int32_t RadoRingIsIdle(void)
{
  return ((RadoRead(RADEON_CP_RB_RPTR) & (RADO_RING_DWORDS - 1u)) == mRingWptr && RadoGuiIdle())
           ? 1
           : 0;
}

static int32_t RadoRingIdle(void)
{
  uint32_t i;

  for (i = 0; i < RADO_WAIT_SPINS; i++) {
    if (RadoRingIsIdle()) {
      return 0;
    }

    pm_metal_cpu_pause();
  }

  return -1;
}

static int32_t RadoRingBlit(uint32_t src_mc,
                            uint32_t dst_mc,
                            uint32_t src_pitch,
                            uint32_t dst_pitch,
                            int32_t  x,
                            int32_t  y,
                            int32_t  w,
                            int32_t  h)
{
  uint32_t gmc;
  uint32_t src_po;
  uint32_t dst_po;

  if (w <= 0 || h <= 0) {
    return 0;
  }

  if (RadoPitchOffset(src_pitch, src_mc, &src_po) != 0 ||
      RadoPitchOffset(dst_pitch, dst_mc, &dst_po) != 0) {
    return -1;
  }

  if (RadoRingWaitSpace(32) != 0) {
    return -1;
  }

  gmc = RADEON_GMC_SRC_PITCH_OFFSET_CNTL | RADEON_GMC_DST_PITCH_OFFSET_CNTL |
        RADEON_GMC_SRC_CLIPPING | RADEON_GMC_DST_CLIPPING | RADEON_GMC_BRUSH_NONE |
        RADEON_GMC_DST_32BPP | RADEON_GMC_SRC_DATATYPE_COLOR | RADEON_ROP3_S |
        RADEON_DP_SRC_SOURCE_MEMORY | RADEON_GMC_CLR_CMP_CNTL_DIS | RADEON_GMC_WR_MSK_DIS;

  /*
   * Same GUI regs as working MMIO blit, submitted as PACKET0 via CP so
   * GTT src addresses are legal (MMIO GTT hung this ASIC).
   * Submit only — caller waits (probe/sync) or job_step (async present).
   */
  RadoRingMark();
  RadoRingWrite(RADO_PACKET0(RADEON_DEFAULT_SC_BOTTOM_RIGHT, 0));
  RadoRingWrite(RADEON_SC_MAX);
  RadoRingWrite(RADO_PACKET0(RADEON_SC_TOP_LEFT, 0));
  RadoRingWrite(0);
  RadoRingWrite(RADO_PACKET0(RADEON_SC_BOTTOM_RIGHT, 0));
  RadoRingWrite(RADEON_SC_MAX);
  RadoRingWrite(RADO_PACKET0(RADEON_SRC_SC_BOTTOM_RIGHT, 0));
  RadoRingWrite(RADEON_SC_MAX);
  RadoRingWrite(RADO_PACKET0(RADEON_DP_GUI_MASTER_CNTL, 0));
  RadoRingWrite(gmc);
  RadoRingWrite(RADO_PACKET0(RADEON_DP_WRITE_MASK, 0));
  RadoRingWrite(0xffffffffu);
  RadoRingWrite(RADO_PACKET0(RADEON_DP_CNTL, 0));
  RadoRingWrite(RADEON_DST_X_LEFT_TO_RIGHT | RADEON_DST_Y_TOP_TO_BOTTOM);
  RadoRingWrite(RADO_PACKET0(RADEON_SRC_PITCH_OFFSET, 0));
  RadoRingWrite(src_po);
  RadoRingWrite(RADO_PACKET0(RADEON_DST_PITCH_OFFSET, 0));
  RadoRingWrite(dst_po);
  RadoRingWrite(RADO_PACKET0(RADEON_SRC_Y_X, 0));
  RadoRingWrite(((uint32_t)y << 16) | (uint32_t)x);
  RadoRingWrite(RADO_PACKET0(RADEON_DST_Y_X, 0));
  RadoRingWrite(((uint32_t)y << 16) | (uint32_t)x);
  RadoRingWrite(RADO_PACKET0(RADEON_DST_HEIGHT_WIDTH, 0));
  RadoRingWrite(((uint32_t)h << 16) | (uint32_t)w);
  RadoRingWrite(RADO_PACKET0(RADEON_DSTCACHE_CTLSTAT, 0));
  RadoRingWrite(RADEON_RB2D_DC_FLUSH_ALL);
  RadoRingWrite(RADO_PACKET0(RADEON_WAIT_UNTIL, 0));
  RadoRingWrite(RADEON_WAIT_2D_IDLECLEAN | RADEON_WAIT_HOST_IDLECLEAN);
  RadoRingCommit();
  return 0;
}

/** Linux r300_copy_dma — GTT/VRAM byte copy via CP; uses GART for GTT addrs. */
static int32_t RadoRingDma(uint32_t src_mc, uint32_t dst_mc, uint32_t bytes)
{
  if (bytes == 0 || bytes > 0x1fffffu) {
    return -1;
  }

  if (RadoRingWaitSpace(8) != 0) {
    return -1;
  }

  RadoRingMark();
  RadoRingWrite(RADO_PACKET0(RADEON_WAIT_UNTIL, 0));
  RadoRingWrite(RADEON_WAIT_2D_IDLECLEAN);
  RadoRingWrite(RADO_PACKET0(RADEON_DMA_COPY, 2));
  RadoRingWrite(src_mc);
  RadoRingWrite(dst_mc);
  RadoRingWrite(bytes | (1u << 31) | (1u << 30));
  RadoRingWrite(RADO_PACKET0(RADEON_WAIT_UNTIL, 0));
  RadoRingWrite(RADEON_WAIT_DMA_GUI_IDLE);
  RadoRingCommit();
  return RadoRingIdle();
}

static void RadoGartZeroDst(uint32_t *dst)
{
  uint32_t i;

  for (i = 0; i < RADO_PROBE_PIXELS; i++) {
    dst[i] = 0;
  }

  RadoClflush(dst, RADO_PROBE_PIXELS * sizeof(uint32_t));
  RadoHdpFlush();
}

static int32_t RadoGartDmaFromGtt(uint32_t *dst)
{
  RadoGartZeroDst(dst);
  if (RadoRingDma(mGttStart, mScratchMc, 256u) != 0) {
    return -1;
  }

  RadoHdpFlush();
  RadoClflush(dst, RADO_PROBE_PIXELS * sizeof(uint32_t));
  return 0;
}

/** Linux default TTM path is PACKET3 blit, not DMA. */
static int32_t RadoGartBlitFromGtt(uint32_t *dst)
{
  RadoGartZeroDst(dst);
  if (RadoRingBlit(mGttStart, mScratchMc, 256u, 256u, 0, 0, (int32_t)RADO_PROBE_PIXELS, 1) != 0 ||
      RadoRingIdle() != 0) {
    return -1;
  }

  RadoHdpFlush();
  RadoClflush(dst, RADO_PROBE_PIXELS * sizeof(uint32_t));
  return 0;
}

static uint32_t RadoGartCntlOn(void)
{
  uint32_t tmp;

  tmp = RadoPcieRead(RADEON_PCIE_TX_GART_CNTL);
  tmp |= RADO_GART_CNTL_ON;
  RadoPcieWrite(RADEON_PCIE_TX_GART_CNTL, tmp);
  RadoGartTlbFlush();
  return RadoPcieRead(RADEON_PCIE_TX_GART_CNTL);
}

static int32_t RadoGartHitOk(const uint32_t *dst)
{
  uint32_t i;

  for (i = 0; i < RADO_PROBE_PIXELS; i++) {
    if (dst[i] != (0xA11CE000u | i)) {
      return 0;
    }
  }

  return 1;
}

static int32_t RadoGartDiscOk(const uint32_t *dst)
{
  uint32_t i;

  for (i = 0; i < RADO_PROBE_PIXELS; i++) {
    if (dst[i] != RADO_DISC_PAT) {
      return 0;
    }
  }

  return 1;
}

static int32_t RadoGartProbeOnce(uint32_t gart_base, uint32_t pte, uint32_t *dst, const char *tag)
{
  uint32_t    i;
  uint32_t    got;
  uint32_t    rb_cntl;
  uint32_t    rb_base;
  uint32_t    rb_disc;
  uint32_t    rb_start;
  uint32_t    rb_end;
  const char *how;

  RadoPcieWrite(RADEON_PCIE_TX_GART_CNTL, RADEON_PCIE_TX_GART_UNMAPPED_ACCESS_DISCARD);
  RadoPcieWrite(RADEON_PCIE_TX_GART_BASE, gart_base);
  RadoPcieWrite(RADEON_PCIE_TX_DISCARD_RD_ADDR_LO, mScratchMc + 4096u);
  RadoPcieWrite(RADEON_PCIE_TX_DISCARD_RD_ADDR_HI, 0);
  RadoPcieWrite(RADEON_PCIE_TX_GART_ERROR, 0);

  ((volatile uint32_t *)mGart)[0] = pte;
  for (i = 1; i < RADO_GART_PAGES; i++) {
    ((volatile uint32_t *)mGart)[i] = 0;
  }

  RadoGartTableSync();
  if (mGart[0] != pte) {
    return -1;
  }

  rb_cntl  = RadoGartCntlOn();
  rb_base  = RadoPcieRead(RADEON_PCIE_TX_GART_BASE);
  rb_disc  = RadoPcieRead(RADEON_PCIE_TX_DISCARD_RD_ADDR_LO);
  rb_start = RadoPcieRead(RADEON_PCIE_TX_GART_START_LO);
  rb_end   = RadoPcieRead(RADEON_PCIE_TX_GART_END_LO);

  how = "dma";
  if (RadoGartDmaFromGtt(dst) != 0 || !RadoGartHitOk(dst)) {
    how = "blit";
    if (RadoGartBlitFromGtt(dst) != 0 || !RadoGartHitOk(dst)) {
      got = dst[0];
      RADO_LOG("metal-gfx: radeon_rv370 gart-try %s/%s got=%x base=%x pte=%x"
               " cntl=%x disc=%x start=%x end=%x agp=%x err=%x",
               tag,
               how,
               got,
               rb_base,
               pte,
               rb_cntl,
               rb_disc,
               rb_start,
               rb_end,
               RadoRead(RADEON_MC_AGP_LOCATION),
               RadoPcieRead(RADEON_PCIE_TX_GART_ERROR));
      return -1;
    }
  }

  (void)how;
  return 0;
}

/**
 * Empty PTEs + GART_EN.
 *  1 = discard pattern (GART miss path perfect)
 *  0 = not front teal (GART likely live; try host PTE hits)
 * -1 = front teal (raw MC wrap; GART bypassed)
 */
static int32_t RadoGartMissProbe(uint32_t gart_base, uint32_t *dst, uint32_t disc_cpu0)
{
  uint32_t i;
  uint32_t got;
  uint32_t rb_cntl;

  RadoPcieWrite(RADEON_PCIE_TX_GART_CNTL, RADEON_PCIE_TX_GART_UNMAPPED_ACCESS_DISCARD);
  RadoPcieWrite(RADEON_PCIE_TX_GART_BASE, gart_base);
  RadoPcieWrite(RADEON_PCIE_TX_DISCARD_RD_ADDR_LO, mScratchMc + 4096u);
  RadoPcieWrite(RADEON_PCIE_TX_DISCARD_RD_ADDR_HI, 0);
  RadoPcieWrite(RADEON_PCIE_TX_GART_ERROR, 0);

  for (i = 0; i < RADO_GART_PAGES; i++) {
    ((volatile uint32_t *)mGart)[i] = 0;
  }

  RadoGartTableSync();
  rb_cntl = RadoGartCntlOn();

  if (RadoGartDmaFromGtt(dst) == 0 && RadoGartDiscOk(dst)) {
    goto miss_ok;
  }

  if (RadoGartBlitFromGtt(dst) == 0 && RadoGartDiscOk(dst)) {
    goto miss_ok;
  }

  got = dst[0];
  RADO_LOG("metal-gfx: radeon_rv370 gart-try miss got=%x disc_cpu=%x base=%x"
           " cntl=%x disc=%x start=%x end=%x agp=%x err=%x",
           got,
           disc_cpu0,
           RadoPcieRead(RADEON_PCIE_TX_GART_BASE),
           rb_cntl,
           RadoPcieRead(RADEON_PCIE_TX_DISCARD_RD_ADDR_LO),
           RadoPcieRead(RADEON_PCIE_TX_GART_START_LO),
           RadoPcieRead(RADEON_PCIE_TX_GART_END_LO),
           RadoRead(RADEON_MC_AGP_LOCATION),
           RadoPcieRead(RADEON_PCIE_TX_GART_ERROR));

  if (got == RADO_FRONT_TEAL) {
    return -1;
  }

  /* e.g. f000ff53 — not discard, but no longer front wrap. */
  return 0;

miss_ok:
  RADO_LOG("metal-gfx: radeon_rv370 gart-miss ok (discard) base=%x cntl=%x",
           RadoPcieRead(RADEON_PCIE_TX_GART_BASE),
           rb_cntl);
  return 1;
}

static int32_t RadoMcWaitIdle(void)
{
  uint32_t i;

  for (i = 0; i < RADO_WAIT_SPINS; i++) {
    if ((RadoRead(RADEON_MC_STATUS) & R300_MC_IDLE) != 0) {
      return 0;
    }

    pm_metal_cpu_pause();
  }

  return -1;
}

/**
 * Linux r300_mc_program / radeon_vram_location: place VRAM at PCI BAR0 in
 * GPU address space, GTT after it. Firmware often leaves MC_FB at 0 — miss
 * probes then see front teal (raw MC wrap) even with PCIE GART_EN.
 * Restored on GART failure so MMIO staging keeps the firmware map.
 */
static int32_t RadoMcAperRelocate(void)
{
  uint32_t new_start;
  uint32_t new_end;
  uint32_t delta;
  uint32_t crtc_ext;
  uint32_t crtc_gen;
  uint32_t crtc2_gen;
  uint32_t cur;
  uint32_t cur2;
  uint8_t  genmo;

  if (mMcAper) {
    return 0;
  }

  if (mVramBar == 0 || (mVramBar >> 32) != 0) {
    return -1;
  }

  new_start = (uint32_t)mVramBar;
  if (new_start == mVramStart) {
    return 0;
  }

  new_end = new_start + mVramSize - 1u;
  delta   = new_start - mVramStart;

  mFwMcFb         = RadoRead(RADEON_MC_FB_LOCATION);
  mFwDisplayBase  = RadoRead(RADEON_DISPLAY_BASE_ADDR);
  mFwDisplayBase2 = RadoRead(RADEON_CRTC2_DISPLAY_BASE_ADDR);

  /* Minimal r100_mc_stop — blank CRTCs while rewriting MC. */
  genmo     = *(volatile uint8_t *)(uintptr_t)(mMmio + RADEON_GENMO_WT);
  crtc_ext  = RadoRead(RADEON_CRTC_EXT_CNTL);
  crtc_gen  = RadoRead(RADEON_CRTC_GEN_CNTL);
  crtc2_gen = RadoRead(RADEON_CRTC2_GEN_CNTL);
  cur       = RadoRead(RADEON_CUR_OFFSET);
  cur2      = RadoRead(RADEON_CUR2_OFFSET);

  *(volatile uint8_t *)(uintptr_t)(mMmio + RADEON_GENMO_WT) =
    (uint8_t)(genmo & (uint8_t)~RADEON_CFG_VGA_RAM_EN);
  RadoWrite(RADEON_CUR_OFFSET, cur | RADEON_CUR_LOCK);
  RadoWrite(RADEON_CRTC_EXT_CNTL, crtc_ext | RADEON_CRTC_DISPLAY_DIS);
  RadoWrite(RADEON_CRTC_GEN_CNTL, (crtc_gen & ~RADEON_CRTC_CUR_EN) | RADEON_CRTC_DISP_REQ_EN_B);
  RadoWrite(RADEON_OV0_SCALE_CNTL, 0);
  RadoWrite(RADEON_CUR_OFFSET, cur & ~RADEON_CUR_LOCK);
  RadoWrite(RADEON_CUR2_OFFSET, cur2 | RADEON_CUR_LOCK);
  RadoWrite(RADEON_CRTC2_GEN_CNTL,
            (crtc2_gen & ~RADEON_CRTC_CUR_EN) | RADEON_CRTC2_DISPLAY_DIS |
              RADEON_CRTC_DISP_REQ_EN_B);
  RadoWrite(RADEON_CUR2_OFFSET, cur2 & ~RADEON_CUR_LOCK);

  (void)RadoMcWaitIdle();

  RadoWrite(RADEON_MC_AGP_LOCATION, 0x0FFFFFFFu);
  RadoWrite(RADEON_AGP_BASE, 0);
  RadoWrite(RADEON_AGP_BASE_2, 0);
  (void)RadoMcWaitIdle();
  RadoWrite(RADEON_MC_FB_LOCATION, ((new_end >> 16) << 16) | (new_start >> 16));

  /* r100_mc_resume: CRTC base follows the new MC VRAM window. */
  RadoWrite(RADEON_DISPLAY_BASE_ADDR, new_start);
  RadoWrite(RADEON_CRTC2_DISPLAY_BASE_ADDR, new_start);
  *(volatile uint8_t *)(uintptr_t)(mMmio + RADEON_GENMO_WT) = genmo;
  RadoWrite(RADEON_CRTC_EXT_CNTL, crtc_ext);
  RadoWrite(RADEON_CRTC_GEN_CNTL, crtc_gen);
  RadoWrite(RADEON_CRTC2_GEN_CNTL, crtc2_gen);

  mVramStart += delta;
  mFrontMc += delta;
  mScratchMc += delta;
  mMcAper = 1;

  RADO_LOG("metal-gfx: radeon_rv370 mc-aper fb=%x->%x size=%x",
           mFwMcFb,
           RadoRead(RADEON_MC_FB_LOCATION),
           mVramSize);
  return 0;
}

static void RadoMcAperRestore(void)
{
  uint32_t delta;
  uint32_t crtc_ext;
  uint32_t crtc_gen;
  uint32_t crtc2_gen;
  uint32_t cur;
  uint32_t cur2;
  uint8_t  genmo;

  if (!mMcAper) {
    return;
  }

  delta = mVramStart - ((mFwMcFb & 0xffffu) << 16);

  genmo     = *(volatile uint8_t *)(uintptr_t)(mMmio + RADEON_GENMO_WT);
  crtc_ext  = RadoRead(RADEON_CRTC_EXT_CNTL);
  crtc_gen  = RadoRead(RADEON_CRTC_GEN_CNTL);
  crtc2_gen = RadoRead(RADEON_CRTC2_GEN_CNTL);
  cur       = RadoRead(RADEON_CUR_OFFSET);
  cur2      = RadoRead(RADEON_CUR2_OFFSET);

  *(volatile uint8_t *)(uintptr_t)(mMmio + RADEON_GENMO_WT) =
    (uint8_t)(genmo & (uint8_t)~RADEON_CFG_VGA_RAM_EN);
  RadoWrite(RADEON_CUR_OFFSET, cur | RADEON_CUR_LOCK);
  RadoWrite(RADEON_CRTC_EXT_CNTL, crtc_ext | RADEON_CRTC_DISPLAY_DIS);
  RadoWrite(RADEON_CRTC_GEN_CNTL, (crtc_gen & ~RADEON_CRTC_CUR_EN) | RADEON_CRTC_DISP_REQ_EN_B);
  RadoWrite(RADEON_CUR_OFFSET, cur & ~RADEON_CUR_LOCK);
  RadoWrite(RADEON_CUR2_OFFSET, cur2 | RADEON_CUR_LOCK);
  RadoWrite(RADEON_CRTC2_GEN_CNTL,
            (crtc2_gen & ~RADEON_CRTC_CUR_EN) | RADEON_CRTC2_DISPLAY_DIS |
              RADEON_CRTC_DISP_REQ_EN_B);
  RadoWrite(RADEON_CUR2_OFFSET, cur2 & ~RADEON_CUR_LOCK);

  (void)RadoMcWaitIdle();
  RadoWrite(RADEON_MC_AGP_LOCATION, 0x0FFFFFFFu);
  RadoWrite(RADEON_MC_FB_LOCATION, mFwMcFb);
  RadoWrite(RADEON_DISPLAY_BASE_ADDR, mFwDisplayBase);
  RadoWrite(RADEON_CRTC2_DISPLAY_BASE_ADDR, mFwDisplayBase2);
  *(volatile uint8_t *)(uintptr_t)(mMmio + RADEON_GENMO_WT) = genmo;
  RadoWrite(RADEON_CRTC_EXT_CNTL, crtc_ext);
  RadoWrite(RADEON_CRTC_GEN_CNTL, crtc_gen);
  RadoWrite(RADEON_CRTC2_GEN_CNTL, crtc2_gen);

  mVramStart -= delta;
  mFrontMc -= delta;
  mScratchMc -= delta;
  mMcAper = 0;
}

/**
 * Enable PCIe GART + CP ring (Linux rv370_pcie_gart_enable + r100_cp_init).
 * Leaves CP off / GART down on failure so MMIO staging stays usable.
 *
 * got=d15c  → GART miss (discard page)
 * got=ff2e86ab → somehow still reading front (VESA teal)
 * got=a11ce… → success
 */
static int32_t RadoGartCpEnable(void)
{
  uint32_t  tmp;
  uint32_t  gtt_end;
  uint32_t  i;
  uint32_t  bi;
  uint32_t  pte;
  uint32_t  aper_off;
  uint32_t  bases[3];
  uint32_t  vram_src_mc;
  uint32_t  vram_src_pci;
  uint32_t *dst;
  uint32_t *discard_cpu;
  uint32_t *vram_src_cpu;
  uint8_t  *host_page = NULL;

  /* Match Linux GPU address map before programming GART. */
  if (RadoMcAperRelocate() != 0) {
    return -1;
  }

  mRingMc = RadoAlignMc(mScratchMc + mPageBytes);
  /* Table: 4 KiB aligned (Linux PAGE_SIZE BO). Need +8K for disc+vram-src. */
  mGartTableMc = (mRingMc + RADO_RING_BYTES + 4095u) & ~4095u;
  if ((mGartTableMc - mVramStart) + RADO_GART_BYTES + 8192u > mVramSize) {
    RadoMcAperRestore();
    return -1;
  }

  mRing = (uint32_t *)(uintptr_t)(mVram + (mRingMc - mVramStart));
  mGart = (uint32_t *)(uintptr_t)(mVram + (mGartTableMc - mVramStart));
  memset(mRing, 0, RADO_RING_BYTES);
  memset(mGart, 0, RADO_GART_BYTES);
  RadoClflush(mRing, RADO_RING_BYTES);
  RadoClflush(mGart, RADO_GART_BYTES);
  RadoHdpFlush();

  /* GTT window sits just after VRAM in GPU address space. */
  mGttStart = mVramStart + mVramSize;
  if ((mGttStart & 0xffffu) != 0) {
    mGttStart = (mGttStart + 0xffffu) & ~0xffffu;
  }

  gtt_end  = mGttStart + (RADO_GART_PAGES * 4096u) - 1u;
  aper_off = mGartTableMc - mVramStart;

  /*
   * Linux r300_mc_program: non-AGP (PCIe) sets MC_AGP_LOCATION=0x0FFFFFFF.
   * (Also done in RadoMcAperRelocate; keep explicit here.)
   */
  RadoWrite(RADEON_MC_AGP_LOCATION, 0x0FFFFFFFu);
  RadoWrite(RADEON_AGP_BASE, 0);
  RadoWrite(RADEON_AGP_BASE_2, 0);

  bases[0] = mGartTableMc;                  /* MC (Linux table_addr) */
  bases[1] = (uint32_t)mVramBar + aper_off; /* PCI */
  bases[2] = aper_off;                      /* VRAM offset */

  RadoForceCpClock();
  RadoCpLoadFw();

  tmp = (11u << 0) | (9u << 8) | (1u << 18) | RADEON_RB_NO_UPDATE;
  RadoWrite(RADEON_CP_RB_CNTL, tmp | RADEON_RB_NO_UPDATE);
  RadoWrite(RADEON_CP_RB_BASE, mRingMc);
  RadoWrite(RADEON_CP_RB_CNTL, tmp | RADEON_RB_RPTR_WR_ENA | RADEON_RB_NO_UPDATE);
  RadoWrite(RADEON_CP_RB_RPTR_WR, 0);
  mRingWptr = 0;
  RadoWrite(RADEON_CP_RB_WPTR, 0);
  RadoWrite(RADEON_CP_RB_RPTR_ADDR, 0);
  RadoWrite(RADEON_SCRATCH_UMSK, 0);
  RadoWrite(RADEON_CP_RB_CNTL, tmp | RADEON_RB_NO_UPDATE);
  RadoWrite(RADEON_CP_RB_WPTR_DELAY, 0);
  RadoWrite(RADEON_CP_CSQ_MODE, 0x00004D4Du);
  RadoWrite(RADEON_CP_CSQ_CNTL, RADEON_CSQ_PRIBM_INDBM);

  /* Program GTT window once. */
  RadoPcieWrite(RADEON_PCIE_TX_GART_START_LO, mGttStart);
  RadoPcieWrite(RADEON_PCIE_TX_GART_END_LO, gtt_end & ~0xfffu);
  RadoPcieWrite(RADEON_PCIE_TX_GART_START_HI, 0);
  RadoPcieWrite(RADEON_PCIE_TX_GART_END_HI, 0);
  RadoPcieWrite(RADEON_PCIE_TX_DISCARD_RD_ADDR_LO, mScratchMc + 4096u);
  RadoPcieWrite(RADEON_PCIE_TX_DISCARD_RD_ADDR_HI, 0);

  dst          = (uint32_t *)(uintptr_t)(mVram + (mScratchMc - mVramStart));
  discard_cpu  = (uint32_t *)(uintptr_t)(mVram + (mScratchMc - mVramStart) + 4096u);
  vram_src_mc  = mScratchMc + 8192u;
  vram_src_cpu = (uint32_t *)(uintptr_t)(mVram + (vram_src_mc - mVramStart));
  vram_src_pci = (uint32_t)mVramBar + (vram_src_mc - mVramStart);

  for (i = 0; i < 1024u; i++) {
    discard_cpu[i] = RADO_DISC_PAT;
  }

  for (i = 0; i < RADO_PROBE_PIXELS; i++) {
    vram_src_cpu[i] = 0xA11CE000u | i;
    dst[i]          = 0;
  }

  RadoClflush(discard_cpu, 4096u);
  RadoClflush(vram_src_cpu, RADO_PROBE_PIXELS * sizeof(uint32_t));
  RadoClflush(dst, RADO_PROBE_PIXELS * sizeof(uint32_t));
  RadoHdpFlush();

  if (discard_cpu[0] != RADO_DISC_PAT) {
    RADO_LOG("metal-gfx: radeon_rv370 disc-cpu fail got=%x", discard_cpu[0]);
    goto fail_cp;
  }

  /* Preflight: VRAM→VRAM DMA must work before GTT experiments. */
  if (RadoRingDma(vram_src_mc, mScratchMc, 256u) != 0 || dst[0] != 0xA11CE000u) {
    RADO_LOG("metal-gfx: radeon_rv370 dma-vram fail got=%x", dst[0]);
    goto fail_cp;
  }

  /*
   * Miss probe: abort only if every BASE still returns front teal
   * (GART bypass). Soft miss (e.g. got=f000ff53) → continue to hits.
   */
  {
    int32_t  miss;
    int32_t  any_live = 0;
    uint32_t bj;

    for (bi = 0; bi < 3u; bi++) {
      for (bj = 0; bj < bi; bj++) {
        if (bases[bj] == bases[bi]) {
          break;
        }
      }

      if (bj < bi) {
        continue;
      }

      miss = RadoGartMissProbe(bases[bi], dst, discard_cpu[0]);
      if (miss > 0) {
        any_live = 1;
        break;
      }

      if (miss == 0) {
        any_live = 1;
      }
    }

    if (!any_live) {
      RADO_LOG("metal-gfx: radeon_rv370 gart-fail got=%x (bypass/front)"
               " tbl=%x gtt=%x agp=%x",
               dst[0],
               mGartTableMc,
               mGttStart,
               RadoRead(RADEON_MC_AGP_LOCATION));
      goto fail_cp;
    }
  }

  /*
   * Host RAM first (real GART use). vram-via-PCI loopback is secondary —
   * on this ASIC empty-PTE reads looked like bus junk (f000ff53), not disc.
   */
  host_page = (uint8_t *)pm_metal_mem_map(4096u);
  if (host_page != NULL && (((uintptr_t)host_page & 4095u) == 0u)) {
    for (i = 0; i < RADO_PROBE_PIXELS; i++) {
      ((volatile uint32_t *)(uintptr_t)host_page)[i] = 0xA11CE000u | i;
    }

    RadoClflush(host_page, 4096u);
    pm_metal_mem_fence();

    pte = RadoPte((uintptr_t)host_page, 1);
    for (bi = 0; bi < 3u; bi++) {
      if (bi > 0 && bases[bi] == bases[0]) {
        continue;
      }

      if (bi == 2 && bases[2] == bases[1]) {
        continue;
      }

      if (RadoGartProbeOnce(bases[bi], pte, dst, "host-snp") == 0) {
        goto gart_ok;
      }

      pte = RadoPte((uintptr_t)host_page, 0);
      if (RadoGartProbeOnce(bases[bi], pte, dst, "host-uns") == 0) {
        goto gart_ok;
      }

      pte = RadoPte((uintptr_t)host_page, 1);
    }
  }

  pte = RadoPte((uintptr_t)vram_src_pci, 0);
  for (bi = 0; bi < 3u; bi++) {
    if (bi > 0 && bases[bi] == bases[0]) {
      continue;
    }

    if (bi == 2 && bases[2] == bases[1]) {
      continue;
    }

    if (RadoGartProbeOnce(bases[bi], pte, dst, "vram-pci") == 0) {
      goto gart_ok;
    }
  }

  RADO_LOG("metal-gfx: radeon_rv370 gart-fail got=%x (d15c=disc ff2e86ab=front)"
           " tbl=%x gtt=%x agp=%x",
           dst[0],
           mGartTableMc,
           mGttStart,
           RadoRead(RADEON_MC_AGP_LOCATION));

fail_cp:
  (void)RadoCpDisable();
  RadoGartDisable();
  RadoMcAperRestore();
  mRing = NULL;
  mGart = NULL;
  return -1;

gart_ok:
  mShadowMapped = NULL;
  mShadowPages  = 0;
  mGartOk       = 1;
  RADO_LOG("metal-gfx: radeon_rv370 gart ok base=%x pte=%x gtt=%x",
           RadoPcieRead(RADEON_PCIE_TX_GART_BASE),
           mGart[0],
           mGttStart);
  return 0;
}

static int32_t RadoMapShadow(const pm_metal_scanout_bind_t *b)
{
  uint32_t       pages;
  uint32_t       i;
  const uint8_t *base;

  if (b == NULL || b->shadow == NULL || mGart == NULL || !mGartOk) {
    return -1;
  }

  base = (const uint8_t *)b->shadow;
  if (((uintptr_t)base & 4095u) != 0) {
    return -1;
  }

  pages = (b->shadow_h * b->shadow_pitch * sizeof(uint32_t) + 4095u) / 4096u;
  if (pages == 0 || pages > RADO_GART_PAGES) {
    return -1;
  }

  if (base == mShadowMapped && pages == mShadowPages) {
    return 0;
  }

  for (i = 0; i < pages; i++) {
    mGart[i] = RadoPte((uintptr_t)base + (uintptr_t)i * 4096u, 1);
  }

  for (; i < RADO_GART_PAGES; i++) {
    mGart[i] = 0;
  }

  RadoGartTableSync();
  mShadowMapped = base;
  mShadowPages  = pages;
  return 0;
}

static int32_t RadoBlitMc(uint32_t src_mc,
                          uint32_t dst_mc,
                          uint32_t src_pitch,
                          uint32_t dst_pitch,
                          int32_t  x,
                          int32_t  y,
                          int32_t  w,
                          int32_t  h)
{
  uint32_t gmc;
  uint32_t src_po;
  uint32_t dst_po;

  if (w <= 0 || h <= 0) {
    return 0;
  }

  if (RadoPitchOffset(src_pitch, src_mc, &src_po) != 0 ||
      RadoPitchOffset(dst_pitch, dst_mc, &dst_po) != 0) {
    return -1;
  }

  if (RadoWaitGuiIdle() != 0 || RadoWaitFifo(12) != 0) {
    return -1;
  }

  /* Linux r100_copy_blit: open scissors + pitch/offset + ARGB8888 src copy. */
  RadoWrite(RADEON_DEFAULT_SC_BOTTOM_RIGHT, RADEON_SC_MAX);
  RadoWrite(RADEON_SC_TOP_LEFT, 0);
  RadoWrite(RADEON_SC_BOTTOM_RIGHT, RADEON_SC_MAX);
  RadoWrite(RADEON_SRC_SC_BOTTOM_RIGHT, RADEON_SC_MAX);

  gmc = RADEON_GMC_SRC_PITCH_OFFSET_CNTL | RADEON_GMC_DST_PITCH_OFFSET_CNTL |
        RADEON_GMC_SRC_CLIPPING | RADEON_GMC_DST_CLIPPING | RADEON_GMC_BRUSH_NONE |
        RADEON_GMC_DST_32BPP | RADEON_GMC_SRC_DATATYPE_COLOR | RADEON_ROP3_S |
        RADEON_DP_SRC_SOURCE_MEMORY | RADEON_GMC_CLR_CMP_CNTL_DIS | RADEON_GMC_WR_MSK_DIS;

  RadoWrite(RADEON_DP_GUI_MASTER_CNTL, gmc);
  RadoWrite(RADEON_DP_WRITE_MASK, 0xffffffffu);
  RadoWrite(RADEON_DP_CNTL, RADEON_DST_X_LEFT_TO_RIGHT | RADEON_DST_Y_TOP_TO_BOTTOM);
  RadoWrite(RADEON_SRC_PITCH_OFFSET, src_po);
  RadoWrite(RADEON_DST_PITCH_OFFSET, dst_po);
  RadoWrite(RADEON_SRC_Y_X, ((uint32_t)y << 16) | (uint32_t)x);
  RadoWrite(RADEON_DST_Y_X, ((uint32_t)y << 16) | (uint32_t)x);
  RadoWrite(RADEON_DST_HEIGHT_WIDTH, ((uint32_t)h << 16) | (uint32_t)w);

  if (RadoWaitFifo(3) != 0) {
    return -1;
  }

  RadoWrite(RADEON_DSTCACHE_CTLSTAT, RADEON_RB2D_DC_FLUSH_ALL);
  RadoWrite(RADEON_RB2D_DSTCACHE_CTLSTAT, RADEON_RB2D_DC_FLUSH_ALL);
  RadoWrite(RADEON_WAIT_UNTIL, RADEON_WAIT_2D_IDLECLEAN | RADEON_WAIT_HOST_IDLECLEAN);
  return RadoWaitGuiIdle();
}

/**
 * Emit present blit. wait_idle=1 for sync present_rect / staging;
 * wait_idle=0 for async job_begin (fence in job_step).
 */
static int32_t RadoEmitBlit(int32_t x, int32_t y, int32_t w, int32_t h, int32_t wait_idle)
{
  const pm_metal_scanout_bind_t *b;
  uint8_t                       *dst;
  const uint8_t                 *src;
  int32_t                        row;
  uintptr_t                      bytes;
  uint32_t                       src_pitch;

  b = pm_metal_scanout_bind_info();
  if (!mReady || mFaulted || b == NULL || b->shadow == NULL || mVram == NULL) {
    return -1;
  }

  if (x < 0 || y < 0 || w <= 0 || h <= 0) {
    return 0;
  }

  if (mGartOk) {
    if (RadoMapShadow(b) != 0) {
      return -1;
    }

    src_pitch = b->shadow_pitch * sizeof(uint32_t);
    if ((src_pitch < 64u) || ((src_pitch & 63u) != 0)) {
      return -1;
    }

    RadoClflushRect((const uint8_t *)b->shadow, src_pitch, x, y, w, h);

    if (RadoRingBlit(mGttStart, mFrontMc, src_pitch, mPitchBytes, x, y, w, h) != 0) {
      return -1;
    }

    if (wait_idle) {
      return RadoRingIdle();
    }

    return 0;
  }

  /* Staging fallback: CPU → VRAM scratch → MMIO BITBLT → front. */
  bytes = (uintptr_t)w * sizeof(uint32_t);
  dst   = mVram + (mScratchMc - mVramStart);
  src   = (const uint8_t *)b->shadow;

  for (row = 0; row < h; row++) {
    memcpy(dst + (uintptr_t)(y + row) * (uintptr_t)mPitchBytes + (uintptr_t)x * sizeof(uint32_t),
           src + (uintptr_t)(y + row) * (uintptr_t)b->shadow_pitch * sizeof(uint32_t) +
             (uintptr_t)x * sizeof(uint32_t),
           bytes);
  }

  RadoClflushRect(dst, mPitchBytes, x, y, w, h);
  RadoHdpFlush();

  return RadoBlitMc(mScratchMc, mFrontMc, mPitchBytes, mPitchBytes, x, y, w, h);
}

static void RadoGartDisable(void)
{
  if (mMmio == NULL) {
    return;
  }

  RadoPcieWrite(RADEON_PCIE_TX_GART_CNTL, RADEON_PCIE_TX_GART_UNMAPPED_ACCESS_DISCARD);
  RadoPcieWrite(RADEON_PCIE_TX_GART_START_LO, 0);
  RadoPcieWrite(RADEON_PCIE_TX_GART_END_LO, 0);
  RadoPcieWrite(RADEON_PCIE_TX_GART_BASE, 0);
}

/** CONFIG_MEMSIZE is bytes on r100–r500; 0 means 8 MiB (Linux quirk). */
static uint32_t RadoNormMemsize(uint32_t raw)
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
static uint64_t RadoFindMmio(uint8_t bus, uint8_t dev, uint8_t func, uint64_t vram_bar)
{
  uint8_t  bars[4] = { 2, 0, 1, 5 };
  uint32_t i;
  uint8_t  cons;
  uint64_t base;
  uint32_t mem;
  uint32_t status;

  for (i = 0; i < 4u; i++) {
    base = pm_metal_bus_pci_bar_mmio(bus, dev, func, bars[i], &cons);
    if (base == 0 || base == vram_bar) {
      continue;
    }

    mem    = *(volatile uint32_t *)(uintptr_t)(base + RADEON_CONFIG_MEMSIZE);
    status = *(volatile uint32_t *)(uintptr_t)(base + RADEON_RBBM_STATUS);
    if (status == 0xffffffffu) {
      continue;
    }

    if (RadoNormMemsize(mem) != 0) {
      return base;
    }
  }

  return 0;
}

static int32_t RadoProbe(const pm_metal_scanout_bind_t *b)
{
  uint8_t   bus;
  uint8_t   dev;
  uint8_t   func;
  uint8_t   cons;
  uint64_t  vram_bar;
  uint64_t  mmio_bar;
  uint64_t  fb_phys;
  uint32_t  memsize;
  uint32_t  page_bytes;
  uint32_t  front_off;
  uint32_t  mc_fb;
  uint32_t  fw_off;
  uint32_t  i;
  uint32_t  bus_cntl;
  uint32_t *scratch_cpu;
  uint32_t *probe_dst;

  mReady        = 0;
  mFaulted      = 0;
  mGartOk       = 0;
  mMcAper       = 0;
  mJobLive      = 0;
  mMmio         = NULL;
  mVram         = NULL;
  mRing         = NULL;
  mGart         = NULL;
  mShadowMapped = NULL;
  mShadowPages  = 0;

  if (b == NULL || b->fb == NULL || !b->owned || b->mode_w == 0 || b->mode_h == 0 ||
      b->fb_ppsl == 0) {
    return -1;
  }

  if (pm_metal_bus_pci_find(RADO_VENDOR, RADO_DEVICE, &bus, &dev, &func) != 0) {
    return -1; /* quiet — not this machine */
  }

  /* Never size-probe live BARs. */
  pm_metal_bus_pci_enable_mem_bm(bus, dev, func);
  vram_bar = pm_metal_bus_pci_bar_mmio(bus, dev, func, 0, &cons);
  if (vram_bar == 0) {
    RadoSkip("bar0");
    return -1;
  }

  mmio_bar = RadoFindMmio(bus, dev, func, vram_bar);
  if (mmio_bar == 0) {
    RadoSkip("mmio");
    return -1;
  }

  mMmio    = (uint8_t *)(uintptr_t)mmio_bar;
  mVram    = (uint8_t *)(uintptr_t)vram_bar;
  mVramBar = vram_bar;

  memsize = RadoNormMemsize(RadoRead(RADEON_CONFIG_MEMSIZE));
  if (memsize == 0) {
    RadoSkip("memsize");
    goto fail;
  }

  mc_fb      = RadoRead(RADEON_MC_FB_LOCATION);
  mVramStart = (mc_fb & 0xffffu) << 16;
  mVramSize  = memsize;

  fb_phys = (uint64_t)(uintptr_t)b->fb;
  if (fb_phys >= vram_bar && (fb_phys - vram_bar) < (uint64_t)mVramSize) {
    mAperOff = (uint32_t)(fb_phys - vram_bar);
  } else {
    /* VESA PhysBasePtr can differ from BAR0; still use BAR0 for CPU. */
    mAperOff = 0;
  }

  /* 2D pitch/offset uses MC addresses. CRTC_OFFSET may be aper-relative. */
  fw_off = RadoRead(RADEON_CRTC_OFFSET) & RADEON_CRTC_OFFSET_MASK;
  if (fw_off >= mVramStart) {
    mFrontMc = fw_off;
  } else {
    mFrontMc = mVramStart + ((fw_off != 0) ? fw_off : mAperOff);
  }

  front_off = mFrontMc - mVramStart;
  if ((mFrontMc & (RADO_MC_ALIGN - 1u)) != 0) {
    RadoSkip("front-align");
    goto fail;
  }

  mPitchBytes = b->fb_ppsl * sizeof(uint32_t);
  if ((mPitchBytes < 64u) || ((mPitchBytes & 63u) != 0)) {
    RadoSkip("pitch");
    goto fail;
  }

  page_bytes = b->mode_h * mPitchBytes;
  mPageBytes = page_bytes;

  /*
   * Staging after front. Pitch/offset regs force 1 KiB surface align —
   * fb pitch on 1400x1050 is often 5632 (not 1K-aligned), so a
   * "next scanline" probe dst was silently truncated → fake readback fail.
   */
  mScratchMc = RadoAlignMc(mFrontMc + page_bytes);
  /* front | scratch | ring | gart-table */
  if (front_off + (mScratchMc - mFrontMc) + page_bytes + RADO_RING_BYTES + RADO_GART_BYTES + 8192u >
      mVramSize) {
    RadoSkip("vram");
    goto fail;
  }

  RadoGartDisable();

  /*
   * RV370: clear bus-master disable on the PCIe bus cntl (0x4c), then
   * kill CP so MMIO 2D is legal (Linux r100_cp_disable).
   */
  bus_cntl = RadoRead(RADEON_RV370_BUS_CNTL);
  if ((bus_cntl & RADEON_BUS_MASTER_DIS) != 0) {
    RadoWrite(RADEON_RV370_BUS_CNTL, bus_cntl & ~RADEON_BUS_MASTER_DIS);
  }

  if (RadoCpDisable() != 0) {
    RadoSkip("cp");
    goto fail;
  }

  /*
   * Self-test on two 1 KiB–aligned lines with pitch=256 (always legal),
   * not the mode pitch — avoids the 1400-wide truncation bug.
   */
  {
    uint32_t test_pitch = 256u;
    uint32_t src_mc     = mScratchMc;
    uint32_t dst_mc     = mScratchMc + RADO_MC_ALIGN;
    uint32_t got;

    scratch_cpu = (uint32_t *)(uintptr_t)(mVram + (src_mc - mVramStart));
    probe_dst   = (uint32_t *)(uintptr_t)(mVram + (dst_mc - mVramStart));

    for (i = 0; i < RADO_PROBE_PIXELS; i++) {
      scratch_cpu[i] = 0xC0DE0000u | i;
      probe_dst[i]   = 0;
    }

    RadoClflush(scratch_cpu, RADO_PROBE_PIXELS * sizeof(uint32_t));
    RadoClflush(probe_dst, RADO_PROBE_PIXELS * sizeof(uint32_t));
    RadoHdpFlush();

    for (i = 0; i < RADO_PROBE_PIXELS; i++) {
      if (scratch_cpu[i] != (0xC0DE0000u | i)) {
        RadoSkip("cpu-vram");
        goto fail;
      }
    }

    if (RadoBlitMc(src_mc, dst_mc, test_pitch, test_pitch, 0, 0, (int32_t)RADO_PROBE_PIXELS, 1) !=
        0) {
      RadoSkip("blit-vram");
      goto fail;
    }

    RadoHdpFlush();
    RadoClflush(probe_dst, RADO_PROBE_PIXELS * sizeof(uint32_t));

    got = probe_dst[0];
    for (i = 0; i < RADO_PROBE_PIXELS; i++) {
      if (probe_dst[i] != (0xC0DE0000u | i)) {
        RADO_LOG("metal-gfx: radeon_rv370 skip readback got=%x want=%x"
                 " src=%x dst=%x pitch=%x",
                 got,
                 0xC0DE0000u,
                 src_mc,
                 dst_mc,
                 test_pitch);
        goto fail;
      }
    }
  }

  mReady = 1;

  /* Prefer GART+CP (GPU pulls WB shadow). Fall back to staging if it fails. */
  if (RadoGartCpEnable() == 0) {
    RADO_LOG("metal-gfx: radeon_rv370 gart fb=%x gtt=%x ring=%x pitch=%x",
             mFrontMc,
             mGttStart,
             mRingMc,
             mPitchBytes);
  } else {
    RADO_LOG("metal-gfx: radeon_rv370 staging fb=%x scratch=%x pitch=%x",
             mFrontMc,
             mScratchMc,
             mPitchBytes);
  }

  return 0;

fail:
  mMmio = NULL;
  mVram = NULL;
  return -1;
}

static int32_t RadoDrainJob(void)
{
  if (!mJobLive) {
    return 0;
  }

  if (RadoRingIdle() != 0) {
    return -1;
  }

  mJobLive = 0;
  return 0;
}

static int32_t RadoPresentRect(int32_t x, int32_t y, int32_t w, int32_t h)
{
  if (!mReady || mFaulted) {
    return -1;
  }

  if (RadoDrainJob() != 0) {
    RadoFault();
    return -1;
  }

  /* Sync path (UI chrome / cursor): submit + wait. */
  if (RadoEmitBlit(x, y, w, h, 1) != 0) {
    RadoFault();
    return -1;
  }

  return 0;
}

static int32_t RadoJobBegin(int32_t x, int32_t y, int32_t w, int32_t h)
{
  if (!mReady || mFaulted) {
    return -1;
  }

  if (RadoDrainJob() != 0) {
    RadoFault();
    return -1;
  }

  if (!mGartOk) {
    /* Staging has no useful async fence — finish sync. */
    if (RadoEmitBlit(x, y, w, h, 1) != 0) {
      RadoFault();
      return -1;
    }

    return 0;
  }

  /* GART: submit CP blit; fence in job_step (i915-style, no busy-wait). */
  if (RadoEmitBlit(x, y, w, h, 0) != 0) {
    RadoFault();
    return -1;
  }

  mJobLive = 1;
  return 1;
}

static int32_t RadoJobStep(void)
{
  if (!mJobLive) {
    return 0;
  }

  if (!RadoRingIsIdle()) {
    return 1;
  }

  mJobLive = 0;
  return 0;
}

static uint32_t RadoCaps(void)
{
  return mGartOk ? PM_METAL_SCANOUT_CAP_CHUNKED : 0u;
}

static void RadoFini(void)
{
  if (mMmio != NULL) {
    (void)RadoCpDisable();
    RadoGartDisable();
  }

  mReady        = 0;
  mFaulted      = 0;
  mGartOk       = 0;
  mMmio         = NULL;
  mVram         = NULL;
  mRing         = NULL;
  mGart         = NULL;
  mShadowMapped = NULL;
}

const pm_metal_scanout_ops_t g_pm_metal_scanout_radeon_rv370 = {
  "radeon_rv370", RadoProbe, RadoPresentRect, RadoJobBegin, RadoJobStep, RadoCaps,
  NULL,           NULL,      RadoFini
};
