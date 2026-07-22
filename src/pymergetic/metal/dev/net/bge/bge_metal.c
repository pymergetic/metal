/**
 * @file Polled Broadcom bge driver for Metal (adapted from FreeBSD if_bge.c).
 * SPDX-License-Identifier: BSD-4-Clause
 */
#include "metal_bge.h"

#include "../../bus/pci/pci.h"
#include "../../runtime/mem/arena.h"
#include <pymergetic/metal/log/log.h>
#include <runtime/time/time.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#if !defined(BH_PLATFORM_METAL_BIOS)
#ifndef __UEFI_BOOT_SERVICES_TABLE_LIB_H__
extern EFI_BOOT_SERVICES  *gBS;
#endif
#include <Library/UefiBootServicesTableLib.h>
#endif

#define BRGPHY_MII_BMCR     0x00
#define BRGPHY_MII_BMSR     0x01
#define BRGPHY_MII_AUXSTS   0x19
#define BMSR_LINK           0x0004u
#define BMCR_RESET          0x8000u
#define BMCR_ANENABLE       0x1000u
#define BMCR_ANRESTART      0x0200u
#define BRGPHY_AUXSTS_AN_RES  0x0700u
#define BRGPHY_RES_1000FD     0x0700u
#define BRGPHY_RES_1000HD     0x0600u
#define BRGPHY_RES_100FD      0x0500u
#define BRGPHY_RES_100HD      0x0300u
#define BRGPHY_RES_10FD       0x0200u
#define BRGPHY_RES_10HD       0x0100u

/* PCIe Device Control (cap+8): clear No-Snoop / Relaxed Ordering. */
#define PCIEM_CTL_RELAXED_ORD_ENABLE  0x0010u
#define PCIEM_CTL_NOSNOOP_ENABLE      0x0800u
#define PCIER_DEVICE_CTL              0x08u
#define PCI_CAP_ID_PCIE               0x10u

#define DELAY(us)  pm_metal_time_usleep ((UINT32)(us))

/* Set to 1 for TX/RX/MAC datapath spam. */
#ifndef METAL_BGE_DEBUG
#define METAL_BGE_DEBUG  0
#endif
#if METAL_BGE_DEBUG
#define BGE_DBG(...)  pm_metal_logf (__VA_ARGS__)
STATIC UINT32  s_bge_tx_ok;
STATIC UINT32  s_bge_rx_ok;
STATIC UINT32  s_bge_rx_err;
#else
#define BGE_DBG(...)  do { } while (0)
#endif

/*
 * Host DMA sync for WB memory. MemoryFence alone does not write back cache
 * lines; if PCIe No-Snoop slips through, the NIC reads stale TX / CPU reads
 * stale RX status. clflush + mfence is the portable x86 hammer here.
 */
STATIC void
bge_dma_sync (
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

STATIC void *
metal_bge_dma_alloc (
  UINTN  bytes
  )
{
#if !defined(BH_PLATFORM_METAL_BIOS)
  if (gBS != NULL) {
    EFI_PHYSICAL_ADDRESS  pa;
    EFI_STATUS            st;
    UINTN                 pages;

    pages = EFI_SIZE_TO_PAGES (bytes);
    st    = gBS->AllocatePages (
                   AllocateAnyPages,
                   EfiLoaderData,
                   pages,
                   &pa
                   );
    if (!EFI_ERROR (st)) {
      ZeroMem ((VOID *)(UINTN)pa, bytes);
      bge_dma_sync ((VOID *)(UINTN)pa, bytes);
      return (VOID *)(UINTN)pa;
    }
  }
#endif
  {
    VOID  *p;

    p = pm_metal_arena_map (bytes);
    if (p != NULL) {
      bge_dma_sync (p, bytes);
    }

    return p;
  }
}

STATIC UINT64
metal_bge_vtop (
  CONST VOID  *p
  )
{
  return (UINT64)(UINTN)p;
}

uint32_t
bge_pci_read (
  metal_bge_softc_t  *sc,
  int                 off
  )
{
  /* BGE_PCI_* macros are PCI config offsets (see if_bgereg.h). */
  if (off == BGE_PCI_CMD) {
    return pm_bios_pci_read16 (sc->bus, sc->dev, sc->func, (UINT8)off);
  }

  return pm_bios_pci_read32 (sc->bus, sc->dev, sc->func, (UINT8)off);
}

void
bge_pci_write (
  metal_bge_softc_t  *sc,
  int                 off,
  uint32_t            val
  )
{
  if (off == BGE_PCI_CMD) {
    pm_bios_pci_write16 (sc->bus, sc->dev, sc->func, (UINT8)off, (UINT16)val);
    return;
  }

  pm_bios_pci_write32 (sc->bus, sc->dev, sc->func, (UINT8)off, val);
}

STATIC uint32_t
bge_readmem_ind (
  metal_bge_softc_t  *sc,
  int                 off
  )
{
  bge_pci_write (sc, BGE_PCI_MEMWIN_BASEADDR, (uint32_t)off);
  return bge_pci_read (sc, BGE_PCI_MEMWIN_DATA);
}

STATIC void
bge_writemem_ind (
  metal_bge_softc_t  *sc,
  int                 off,
  int                 val
  )
{
  bge_pci_write (sc, BGE_PCI_MEMWIN_BASEADDR, (uint32_t)off);
  bge_pci_write (sc, BGE_PCI_MEMWIN_DATA, (uint32_t)val);
}

void
bge_writemem_direct (
  metal_bge_softc_t  *sc,
  int                 off,
  int                 val
  )
{
  CSR_WRITE_4 (sc, off, val);
}

STATIC void
bge_writereg_ind (
  metal_bge_softc_t  *sc,
  int                 off,
  int                 val
  )
{
  bge_pci_write (sc, BGE_PCI_REG_BASEADDR, (uint32_t)off);
  bge_pci_write (sc, BGE_PCI_REG_DATA, (uint32_t)val);
}

STATIC void
bge_writembx (
  metal_bge_softc_t  *sc,
  int                 off,
  int                 val
  )
{
  CSR_WRITE_4 (sc, off, val);
  if ((sc->flags & BGE_FLAG_MBOX_REORDER) != 0) {
    (VOID)CSR_READ_4 (sc, off);
  }
}

STATIC uint32_t
bge_chipid_read (
  metal_bge_softc_t  *sc,
  UINT16              did
  )
{
  uint32_t  id;

  id = bge_pci_read (sc, BGE_PCI_MISC_CTL) >> BGE_PCIMISCCTL_ASICREV_SHIFT;
  if (BGE_ASICREV (id) == BGE_ASICREV_USE_PRODID_REG) {
    id = bge_pci_read (sc, BGE_PCI_PRODID_ASICREV);
  }

  (VOID)did;
  return id;
}

STATIC UINT8
bge_pcie_cap_off (
  metal_bge_softc_t  *sc
  )
{
  UINT8  ptr;
  UINT8  id;
  INT32  guard;

  ptr = (UINT8)(pm_bios_pci_read8 (sc->bus, sc->dev, sc->func, BGE_PCI_CAPPTR)
                & ~0x3u);
  for (guard = 0; ptr != 0 && guard < 48; guard++) {
    id = pm_bios_pci_read8 (sc->bus, sc->dev, sc->func, ptr);
    if (id == PCI_CAP_ID_PCIE) {
      return ptr;
    }

    ptr = (UINT8)(pm_bios_pci_read8 (sc->bus, sc->dev, sc->func, ptr + 1u)
                  & ~0x3u);
  }

  /* BCM575x often advertises PCIe at 0xD0. */
  if (pm_bios_pci_read8 (sc->bus, sc->dev, sc->func, BGE_PCIE_CAPID_REG)
      == BGE_PCIE_CAPID)
  {
    return (UINT8)BGE_PCIE_CAPID_REG;
  }

  return 0;
}

STATIC void
bge_pcie_clear_nosnoop (
  metal_bge_softc_t  *sc
  )
{
  UINT8   cap;
  UINT16  devctl;

  if ((sc->flags & BGE_FLAG_PCIE) == 0) {
    return;
  }

  cap = bge_pcie_cap_off (sc);
  if (cap == 0) {
    return;
  }

  devctl  = pm_bios_pci_read16 (
              sc->bus,
              sc->dev,
              sc->func,
              (UINT8)(cap + PCIER_DEVICE_CTL)
              );
  devctl &= (UINT16)~(PCIEM_CTL_RELAXED_ORD_ENABLE | PCIEM_CTL_NOSNOOP_ENABLE);
  pm_bios_pci_write16 (
    sc->bus,
    sc->dev,
    sc->func,
    (UINT8)(cap + PCIER_DEVICE_CTL),
    devctl
    );
}

STATIC void
bge_set_family_flags (
  metal_bge_softc_t  *sc
  )
{
  switch (sc->asicrev) {
    case BGE_ASICREV_BCM5755:
    case BGE_ASICREV_BCM5761:
    case BGE_ASICREV_BCM5784:
    case BGE_ASICREV_BCM5785:
    case BGE_ASICREV_BCM5787:
    case BGE_ASICREV_BCM57780:
      sc->flags |= BGE_FLAG_5755_PLUS | BGE_FLAG_575X_PLUS |
                   BGE_FLAG_5705_PLUS;
      break;
    case BGE_ASICREV_BCM5750:
    case BGE_ASICREV_BCM5752:
    case BGE_ASICREV_BCM5906:
    case BGE_ASICREV_BCM5705:
      sc->flags |= BGE_FLAG_575X_PLUS | BGE_FLAG_5705_PLUS;
      break;
    default:
      sc->flags |= BGE_FLAG_5705_PLUS;
      break;
  }

  sc->flags |= BGE_FLAG_PCIE | BGE_FLAG_4G_BNDRY_BUG | BGE_FLAG_EADDR;
  if (BGE_IS_5755_PLUS (sc)) {
    sc->flags |= BGE_FLAG_SHORT_DMA_BUG | BGE_FLAG_MBOX_REORDER;
  }

  sc->bge_return_ring_cnt = BGE_IS_5705_PLUS (sc) ? BGE_RETURN_RING_CNT_5705
                                                  : BGE_RETURN_RING_CNT;
  sc->phy_addr = 1;
  sc->mi_mode  = BGE_MIMODE_BASE;
}

STATIC int
bge_miibus_readreg (
  metal_bge_softc_t  *sc,
  int                 reg
  )
{
  uint32_t  val;
  int       i;

  CSR_WRITE_4 (
    sc,
    BGE_MI_COMM,
    BGE_MICMD_READ | BGE_MICOMM_BUSY | BGE_MIPHY (sc->phy_addr) |
      BGE_MIREG (reg)
    );
  for (i = 0; i < BGE_TIMEOUT; i++) {
    DELAY (10);
    val = CSR_READ_4 (sc, BGE_MI_COMM);
    if ((val & BGE_MICOMM_BUSY) == 0) {
      break;
    }
  }

  if (i == BGE_TIMEOUT) {
    return 0;
  }

  if (val & BGE_MICOMM_READFAIL) {
    return 0;
  }

  return (int)(val & 0xffffu);
}

STATIC void
bge_miibus_writereg (
  metal_bge_softc_t  *sc,
  int                 reg,
  int                 val
  )
{
  int  i;

  CSR_WRITE_4 (
    sc,
    BGE_MI_COMM,
    BGE_MICMD_WRITE | BGE_MICOMM_BUSY | BGE_MIPHY (sc->phy_addr) |
      BGE_MIREG (reg) | (uint32_t)val
    );
  for (i = 0; i < BGE_TIMEOUT; i++) {
    DELAY (10);
    if ((CSR_READ_4 (sc, BGE_MI_COMM) & BGE_MICOMM_BUSY) == 0) {
      break;
    }
  }
}

STATIC void
bge_mac_update_link (
  metal_bge_softc_t  *sc
  )
{
  uint32_t  mac_mode;
  int       aux;
  int       res;
  int       gig;
  int       fdx;

  /*
   * Match FreeBSD bge_miibus_statchg: 1000BASE-T needs GMII; 10/100 use MII.
   * Leaving MII at gigabit link-up is a common “link=1 but no packets” bug.
   */
  mac_mode = CSR_READ_4 (sc, BGE_MAC_MODE)
             & ~(BGE_MACMODE_PORTMODE | BGE_MACMODE_HALF_DUPLEX);
  gig      = 0;
  fdx      = 1;
  aux      = 0;

  if (sc->link) {
    aux = bge_miibus_readreg (sc, BRGPHY_MII_AUXSTS);
    res = aux & BRGPHY_AUXSTS_AN_RES;
    switch (res) {
      case BRGPHY_RES_1000FD:
        gig = 1;
        fdx = 1;
        break;
      case BRGPHY_RES_1000HD:
        gig = 1;
        fdx = 0;
        break;
      case BRGPHY_RES_100FD:
      case BRGPHY_RES_10FD:
        fdx = 1;
        break;
      case BRGPHY_RES_100HD:
      case BRGPHY_RES_10HD:
        fdx = 0;
        break;
      default:
        /* Unresolved AN — stay MII; wrong GMII guess kills 10/100. */
        gig = 0;
        break;
    }
  }

  if (gig) {
    mac_mode |= BGE_PORTMODE_GMII;
  } else {
    mac_mode |= BGE_PORTMODE_MII;
  }

  if (!fdx) {
    mac_mode |= BGE_MACMODE_HALF_DUPLEX;
  }

  CSR_WRITE_4 (sc, BGE_MAC_MODE, mac_mode);
  DELAY (40);
  BGE_DBG (
    "metal-bge: macmode %a %a aux=0x%x",
    gig ? "gmii" : "mii",
    fdx ? "fdx" : "hdx",
    (UINT32)aux
    );
}

STATIC int
bge_phy_init (
  metal_bge_softc_t  *sc
  )
{
  int  i;
  int  bmcr;
  int  bmsr;

  bge_miibus_writereg (sc, BRGPHY_MII_BMCR, BMCR_RESET);
  for (i = 0; i < 500; i++) {
    DELAY (1000);
    bmcr = bge_miibus_readreg (sc, BRGPHY_MII_BMCR);
    if ((bmcr & BMCR_RESET) == 0) {
      break;
    }
  }

  bge_miibus_writereg (
    sc,
    BRGPHY_MII_BMCR,
    BMCR_ANENABLE | BMCR_ANRESTART
    );
  DELAY (100000);

  for (i = 0; i < 5000; i++) {
    /* BMSR link is latched — read twice. */
    (void)bge_miibus_readreg (sc, BRGPHY_MII_BMSR);
    bmsr = bge_miibus_readreg (sc, BRGPHY_MII_BMSR);
    if ((bmsr & BMSR_LINK) != 0) {
      sc->link = 1;
      bge_mac_update_link (sc);
      return 0;
    }

    DELAY (1000);
  }

  sc->link = 0;
  bge_mac_update_link (sc);
  return -1;
}

STATIC int
bge_mac_ok (
  CONST UINT8  *mac
  )
{
  UINTN  i;
  INT32  nonzero;

  if (mac == NULL || (mac[0] & 0x01u) != 0) {
    return 0;
  }

  nonzero = 0;
  for (i = 0; i < 6; i++) {
    if (mac[i] != 0) {
      nonzero = 1;
    }
  }

  return nonzero;
}

STATIC void
bge_mac_synthesize (
  metal_bge_softc_t  *sc
  )
{
  sc->mac[0] = 0x02;
  sc->mac[1] = 0x00;
  sc->mac[2] = 0x57;
  sc->mac[3] = (uint8_t)sc->bus;
  sc->mac[4] = (uint8_t)sc->dev;
  sc->mac[5] = (uint8_t)sc->func;
}

/* FreeBSD-style auto EEPROM access; 0 on success. */
STATIC int
bge_eeprom_getbyte (
  metal_bge_softc_t  *sc,
  int                 addr,
  UINT8              *dest
  )
{
  int       i;
  uint32_t  byte;

  if (dest == NULL) {
    return -1;
  }

  BGE_SETBIT (sc, BGE_MISC_LOCAL_CTL, BGE_MLC_AUTO_EEPROM);
  CSR_WRITE_4 (sc, BGE_EE_ADDR, BGE_EEADDR_RESET | BGE_EEHALFCLK (BGE_HALFCLK_384SCL));
  DELAY (20);
  CSR_WRITE_4 (sc, BGE_EE_ADDR, BGE_EE_READCMD | (uint32_t)addr);
  for (i = 0; i < BGE_TIMEOUT * 10; i++) {
    DELAY (10);
    if (CSR_READ_4 (sc, BGE_EE_ADDR) & BGE_EEADDR_DONE) {
      break;
    }
  }

  if (i == BGE_TIMEOUT * 10) {
    return -1;
  }

  byte   = CSR_READ_4 (sc, BGE_EE_DATA);
  *dest  = (UINT8)((byte >> ((addr % 4) * 8)) & 0xffu);
  return 0;
}

STATIC int
bge_get_eaddr_mem (
  metal_bge_softc_t  *sc,
  UINT8               mac[6]
  )
{
  uint32_t  hi;
  uint32_t  lo;

  hi = bge_readmem_ind (sc, BGE_SRAM_MAC_ADDR_HIGH_MB);
  if ((hi >> 16) != 0x484bu) {
    return -1;
  }

  lo     = bge_readmem_ind (sc, BGE_SRAM_MAC_ADDR_LOW_MB);
  mac[0] = (UINT8)(hi >> 8);
  mac[1] = (UINT8)hi;
  mac[2] = (UINT8)(lo >> 24);
  mac[3] = (UINT8)(lo >> 16);
  mac[4] = (UINT8)(lo >> 8);
  mac[5] = (UINT8)lo;
  return bge_mac_ok (mac) ? 0 : -1;
}

STATIC int
bge_get_eaddr_eeprom (
  metal_bge_softc_t  *sc,
  UINT8               mac[6]
  )
{
  int  i;
  int  off;

  /* Station address lives at BGE_EE_MAC_OFFSET+2 (not at byte 0). */
  off = BGE_EE_MAC_OFFSET + 2;
  for (i = 0; i < 6; i++) {
    if (bge_eeprom_getbyte (sc, off + i, &mac[i]) != 0) {
      return -1;
    }
  }

  return bge_mac_ok (mac) ? 0 : -1;
}

STATIC int
bge_get_eaddr_regs (
  metal_bge_softc_t  *sc,
  UINT8               mac[6]
  )
{
  uint32_t  lo;
  uint32_t  hi;

  /* Inverse of FreeBSD htons() programming into MAC_ADDR1_*. */
  lo     = CSR_READ_4 (sc, BGE_MAC_ADDR1_LO);
  hi     = CSR_READ_4 (sc, BGE_MAC_ADDR1_HI);
  mac[0] = (UINT8)((lo >> 8) & 0xffu);
  mac[1] = (UINT8)(lo & 0xffu);
  mac[2] = (UINT8)((hi >> 24) & 0xffu);
  mac[3] = (UINT8)((hi >> 16) & 0xffu);
  mac[4] = (UINT8)((hi >> 8) & 0xffu);
  mac[5] = (UINT8)(hi & 0xffu);
  return bge_mac_ok (mac) ? 0 : -1;
}

STATIC UINT16
bge_htons (
  UINT16  v
  )
{
  return (UINT16)(((v & 0xffu) << 8) | ((v >> 8) & 0xffu));
}

STATIC void
bge_program_mac (
  metal_bge_softc_t  *sc
  )
{
  UINT16  *m;

  m = (UINT16 *)sc->mac;
  CSR_WRITE_4 (sc, BGE_MAC_ADDR1_LO, bge_htons (m[0]));
  CSR_WRITE_4 (
    sc,
    BGE_MAC_ADDR1_HI,
    ((uint32_t)bge_htons (m[1]) << 16) | bge_htons (m[2])
    );
}

STATIC void
bge_read_mac (
  metal_bge_softc_t  *sc
  )
{
  /*
   * Same priority as FreeBSD bge_get_eaddr: firmware SRAM mailbox, then
   * EEPROM at the MAC offset, then whatever PXE left in MAC_ADDR1.
   */
  if (bge_get_eaddr_mem (sc, sc->mac) == 0) {
    return;
  }

  if (bge_get_eaddr_eeprom (sc, sc->mac) == 0) {
    return;
  }

  if (bge_get_eaddr_regs (sc, sc->mac) == 0) {
    return;
  }

  bge_mac_synthesize (sc);
}

STATIC uint32_t
bge_dma_swap_options (
  metal_bge_softc_t  *sc
  )
{
  uint32_t  dma_options;

  (VOID)sc;
  /*
   * Match FreeBSD bge_dma_swap_options exactly. BYTESWAP_NONFRAME is for
   * big-endian hosts only — enabling it on LE scrambles status-block /
   * BD fields so rx_prod stays 0 and RX never surfaces.
   */
  dma_options = BGE_MODECTL_WORDSWAP_NONFRAME |
                BGE_MODECTL_BYTESWAP_DATA | BGE_MODECTL_WORDSWAP_DATA;
#if BYTE_ORDER == BIG_ENDIAN
  dma_options |= BGE_MODECTL_BYTESWAP_NONFRAME;
#endif
  return dma_options;
}

STATIC int
bge_reset (
  metal_bge_softc_t  *sc
  )
{
  void (*write_op)(metal_bge_softc_t *, int, int);
  uint32_t  cachesize;
  uint32_t  command;
  uint32_t  mac_mode;
  uint32_t  mac_mode_mask;
  uint32_t  reset;
  uint32_t  val;
  int       i;

  mac_mode_mask = BGE_MACMODE_HALF_DUPLEX | BGE_MACMODE_PORTMODE;
  mac_mode      = CSR_READ_4 (sc, BGE_MAC_MODE) & mac_mode_mask;

  if (BGE_IS_575X_PLUS (sc)) {
    write_op = bge_writemem_direct;
  } else {
    write_op = bge_writereg_ind;
  }

  CSR_WRITE_4 (
    sc,
    BGE_NVRAM_SWARB,
    BGE_NVRAMSWARB_SET1
    );
  for (i = 0; i < 8000; i++) {
    if (CSR_READ_4 (sc, BGE_NVRAM_SWARB) & BGE_NVRAMSWARB_GNT1) {
      break;
    }

    DELAY (20);
  }

  cachesize = bge_pci_read (sc, BGE_PCI_CACHESZ);
  command   = bge_pci_read (sc, BGE_PCI_CMD);
  bge_pci_write (
    sc,
    BGE_PCI_MISC_CTL,
    BGE_PCIMISCCTL_INDIRECT_ACCESS | BGE_PCIMISCCTL_MASK_PCI_INTR |
      BGE_HIF_SWAP_OPTIONS | BGE_PCIMISCCTL_PCISTATE_RW
    );

  if (BGE_IS_5755_PLUS (sc)) {
    CSR_WRITE_4 (sc, BGE_FASTBOOT_PC, 0);
  }

  bge_writemem_ind (sc, BGE_SRAM_FW_MB, BGE_SRAM_FW_MB_MAGIC);
  reset = BGE_MISCCFG_RESET_CORE_CLOCKS | BGE_32BITTIME_66MHZ;
  if (sc->flags & BGE_FLAG_PCIE) {
    /* PCIE 1.0 train quirk (Broadcom / FreeBSD). */
    if (CSR_READ_4 (sc, 0x7E2C) == 0x60u) {
      CSR_WRITE_4 (sc, 0x7E2C, 0x20);
    }

    CSR_WRITE_4 (sc, BGE_MISC_CFG, 1u << 29);
    reset |= 1u << 29;
  }

  /* Keep GPHY powered across D0 reset (5705+ without CPMU). */
  if (BGE_IS_5705_PLUS (sc)) {
    reset |= BGE_MISCCFG_GPHY_PD_OVERRIDE;
  }

  write_op (sc, BGE_MISC_CFG, (int)reset);
  DELAY (100 * 1000);

  if (sc->flags & BGE_FLAG_PCIE) {
    bge_pcie_clear_nosnoop (sc);
#if METAL_BGE_DEBUG
    {
      UINT8   cap;
      UINT16  devctl;

      cap = bge_pcie_cap_off (sc);
      if (cap != 0) {
        devctl = pm_bios_pci_read16 (
                   sc->bus,
                   sc->dev,
                   sc->func,
                   (UINT8)(cap + PCIER_DEVICE_CTL)
                   );
        BGE_DBG (
          "metal-bge: pcie cap=0x%x devctl=0x%x",
          (UINT32)cap,
          (UINT32)devctl
          );
      } else {
        BGE_DBG ("metal-bge: pcie cap not found");
      }
    }
#endif
  }

  bge_pci_write (
    sc,
    BGE_PCI_MISC_CTL,
    BGE_PCIMISCCTL_INDIRECT_ACCESS | BGE_PCIMISCCTL_MASK_PCI_INTR |
      BGE_HIF_SWAP_OPTIONS | BGE_PCIMISCCTL_PCISTATE_RW
    );
  val = BGE_PCISTATE_ROM_ENABLE | BGE_PCISTATE_ROM_RETRY_ENABLE;
  bge_pci_write (sc, BGE_PCI_PCISTATE, val);
  bge_pci_write (sc, BGE_PCI_CACHESZ, cachesize);
  bge_pci_write (sc, BGE_PCI_CMD, command);

  /* Memory arbiter must be up before rings/DMA engines. */
  CSR_WRITE_4 (sc, BGE_MARB_MODE, BGE_MARBMODE_ENABLE);
  CSR_WRITE_4 (sc, BGE_MODE_CTL, bge_dma_swap_options (sc));

  val = (CSR_READ_4 (sc, BGE_MAC_MODE) & ~mac_mode_mask) | mac_mode;
  CSR_WRITE_4 (sc, BGE_MAC_MODE, val);
  DELAY (40);

  for (i = 0; i < BGE_TIMEOUT; i++) {
    DELAY (10);
    val = bge_readmem_ind (sc, BGE_SRAM_FW_MB);
    if (val == (uint32_t)(~BGE_SRAM_FW_MB_MAGIC)) {
      break;
    }
  }

  return 0;
}

STATIC int
bge_chipinit (
  metal_bge_softc_t  *sc
  )
{
  uint32_t  dma_rw_ctl;
  uint32_t  mode_ctl;

  bge_pci_write (sc, BGE_PCI_MISC_CTL, BGE_INIT);
  dma_rw_ctl = BGE_PCIDMARWCTL_RD_CMD_SHIFT (6) |
               BGE_PCIDMARWCTL_WR_CMD_SHIFT (7);
  if (sc->flags & BGE_FLAG_PCIE) {
    dma_rw_ctl |= BGE_PCIDMARWCTL_WR_WAT_SHIFT (3);
  }

  bge_pci_write (sc, BGE_PCI_DMA_RW_CTL, dma_rw_ctl);
  mode_ctl = bge_dma_swap_options (sc);
  mode_ctl |= BGE_MODECTL_MAC_ATTN_INTR | BGE_MODECTL_HOST_SEND_BDS |
              BGE_MODECTL_TX_NO_PHDR_CSUM | BGE_MODECTL_STACKUP;
  CSR_WRITE_4 (sc, BGE_MODE_CTL, mode_ctl);
  PCI_CLRBIT (sc, BGE_PCI_CMD, PCIM_CMD_MWIEN, 4);
  CSR_WRITE_4 (sc, BGE_MISC_CFG, BGE_32BITTIME_66MHZ);
  return 0;
}

STATIC int
bge_dma_setup (
  metal_bge_softc_t  *sc
  )
{
  sc->ldata.bge_rx_std_ring = metal_bge_dma_alloc (BGE_STD_RX_RING_SZ);
  sc->ldata.bge_rx_return_ring =
    metal_bge_dma_alloc (BGE_RX_RTN_RING_SZ (sc));
  sc->ldata.bge_tx_ring     = metal_bge_dma_alloc (BGE_TX_RING_SZ);
  sc->ldata.bge_status_block = metal_bge_dma_alloc (BGE_STATUS_BLK_SZ);
  sc->ldata.bge_stats        = metal_bge_dma_alloc (BGE_STATS_SZ);

  if (sc->ldata.bge_rx_std_ring == NULL || sc->ldata.bge_rx_return_ring == NULL
      || sc->ldata.bge_tx_ring == NULL || sc->ldata.bge_status_block == NULL
      || sc->ldata.bge_stats == NULL)
  {
    return -1;
  }

  sc->ldata.bge_rx_std_ring_paddr =
    metal_bge_vtop (sc->ldata.bge_rx_std_ring);
  sc->ldata.bge_rx_return_ring_paddr =
    metal_bge_vtop (sc->ldata.bge_rx_return_ring);
  sc->ldata.bge_tx_ring_paddr = metal_bge_vtop (sc->ldata.bge_tx_ring);
  sc->ldata.bge_status_block_paddr =
    metal_bge_vtop (sc->ldata.bge_status_block);
  sc->ldata.bge_stats_paddr = metal_bge_vtop (sc->ldata.bge_stats);

  for (UINTN i = 0; i < BGE_STD_RX_RING_CNT; i++) {
    sc->rx_buf[i] = metal_bge_dma_alloc (METAL_BGE_RX_BUF_SZ);
    if (sc->rx_buf[i] == NULL) {
      return -1;
    }

    sc->rx_buf_len[i] = METAL_BGE_RX_BUF_SZ;
  }

  return 0;
}

STATIC int
bge_newbuf_std (
  metal_bge_softc_t  *sc,
  int                 i
  )
{
  struct bge_rx_bd  *r;

  r = &sc->ldata.bge_rx_std_ring[sc->std];
  BGE_HOSTADDR (
    r->bge_addr,
    metal_bge_vtop (sc->rx_buf[i])
    );
  r->bge_flags = BGE_RXBDFLAG_END;
  r->bge_len   = sc->rx_buf_len[i];
  r->bge_idx   = (uint16_t)i;
  BGE_INC (sc->std, BGE_STD_RX_RING_CNT);
  return 0;
}

STATIC int
bge_init_rx_ring_std (
  metal_bge_softc_t  *sc
  )
{
  ZeroMem (sc->ldata.bge_rx_std_ring, BGE_STD_RX_RING_SZ);
  sc->std = 0;
  for (int i = 0; i < BGE_STD_RX_RING_CNT; i++) {
    if (bge_newbuf_std (sc, i) != 0) {
      return -1;
    }
  }

  sc->std = 0;
  bge_dma_sync (sc->ldata.bge_rx_std_ring, BGE_STD_RX_RING_SZ);
  bge_writembx (sc, BGE_MBX_RX_STD_PROD_LO, BGE_STD_RX_RING_CNT - 1);
  return 0;
}

STATIC int
bge_init_tx_ring (
  metal_bge_softc_t  *sc
  )
{
  sc->txcnt              = 0;
  sc->tx_saved_considx   = 0;
  sc->tx_prodidx         = 0;
  ZeroMem (sc->ldata.bge_tx_ring, BGE_TX_RING_SZ);
  bge_writembx (sc, BGE_MBX_TX_HOST_PROD0_LO, 0);
  bge_writembx (sc, BGE_MBX_TX_NIC_PROD0_LO, 0);
  return 0;
}

STATIC int
bge_blockinit (
  metal_bge_softc_t  *sc
  )
{
  struct bge_rcb  *rcb;
  bge_hostaddr     taddr;
  bus_size_t       vrcb;
  uint32_t         val;
  int              i;
  int              limit;

  CSR_WRITE_4 (sc, BGE_PCI_MEMWIN_BASEADDR, 0);
  CSR_WRITE_4 (sc, BGE_BMAN_MBUFPOOL_READDMA_LOWAT, 0);
  CSR_WRITE_4 (sc, BGE_BMAN_MBUFPOOL_MACRX_LOWAT, 0x10);
  CSR_WRITE_4 (sc, BGE_BMAN_MBUFPOOL_HIWAT, 0x60);
  CSR_WRITE_4 (sc, BGE_BMAN_DMA_DESCPOOL_LOWAT, 5);
  CSR_WRITE_4 (sc, BGE_BMAN_DMA_DESCPOOL_HIWAT, 10);
  CSR_WRITE_4 (sc, BGE_BMAN_MODE, BGE_BMANMODE_ENABLE | BGE_BMANMODE_LOMBUF_ATTN);
  for (i = 0; i < BGE_TIMEOUT; i++) {
    DELAY (10);
    if (CSR_READ_4 (sc, BGE_BMAN_MODE) & BGE_BMANMODE_ENABLE) {
      break;
    }
  }

  if (i == BGE_TIMEOUT) {
    return -1;
  }

  CSR_WRITE_4 (sc, BGE_FTQ_RESET, 0xffffffffu);
  CSR_WRITE_4 (sc, BGE_FTQ_RESET, 0);
  for (i = 0; i < BGE_TIMEOUT; i++) {
    DELAY (10);
    if (CSR_READ_4 (sc, BGE_FTQ_RESET) == 0) {
      break;
    }
  }

  if (i == BGE_TIMEOUT) {
    return -1;
  }

  rcb = &sc->ldata.bge_info.bge_std_rx_rcb;
  rcb->bge_hostaddr.bge_addr_lo =
    (uint32_t)sc->ldata.bge_rx_std_ring_paddr;
  rcb->bge_hostaddr.bge_addr_hi =
    (uint32_t)(sc->ldata.bge_rx_std_ring_paddr >> 32);
  /* 5705+: RCB high word is host ring size (512/256/128/64/32). */
  rcb->bge_maxlen_flags = BGE_RCB_MAXLEN_FLAGS (BGE_STD_RX_RING_CNT, 0);
  rcb->bge_nicaddr      = BGE_STD_RX_RINGS;
  CSR_WRITE_4 (sc, BGE_RX_STD_RCB_HADDR_HI, rcb->bge_hostaddr.bge_addr_hi);
  CSR_WRITE_4 (sc, BGE_RX_STD_RCB_HADDR_LO, rcb->bge_hostaddr.bge_addr_lo);
  CSR_WRITE_4 (sc, BGE_RX_STD_RCB_MAXLEN_FLAGS, rcb->bge_maxlen_flags);
  CSR_WRITE_4 (sc, BGE_RX_STD_RCB_NICADDR, rcb->bge_nicaddr);
  bge_writembx (sc, BGE_MBX_RX_STD_PROD_LO, 0);

  /* FreeBSD forces 8 for all 5705+ (not CNT/8). */
  CSR_WRITE_4 (sc, BGE_RBDI_STD_REPL_THRESH, 8);

  limit = 1;
  vrcb  = BGE_MEMWIN_START + BGE_SEND_RING_RCB;
  for (i = 0; i < limit; i++) {
    RCB_WRITE_4 (
      sc,
      vrcb,
      bge_maxlen_flags,
      BGE_RCB_MAXLEN_FLAGS (0, BGE_RCB_FLAG_RING_DISABLED)
      );
    RCB_WRITE_4 (sc, vrcb, bge_nicaddr, 0);
    vrcb += sizeof (struct bge_rcb);
  }

  vrcb = BGE_MEMWIN_START + BGE_SEND_RING_RCB;
  BGE_HOSTADDR (taddr, sc->ldata.bge_tx_ring_paddr);
  RCB_WRITE_4 (sc, vrcb, bge_hostaddr.bge_addr_hi, taddr.bge_addr_hi);
  RCB_WRITE_4 (sc, vrcb, bge_hostaddr.bge_addr_lo, taddr.bge_addr_lo);
  RCB_WRITE_4 (
    sc,
    vrcb,
    bge_nicaddr,
    BGE_NIC_TXRING_ADDR (0, BGE_TX_RING_CNT)
    );
  RCB_WRITE_4 (
    sc,
    vrcb,
    bge_maxlen_flags,
    BGE_RCB_MAXLEN_FLAGS (BGE_TX_RING_CNT, 0)
    );

  if (sc->asicrev == BGE_ASICREV_BCM5755) {
    limit = 4;
  } else {
    limit = 1;
  }

  vrcb = BGE_MEMWIN_START + BGE_RX_RETURN_RING_RCB;
  for (i = 0; i < limit; i++) {
    RCB_WRITE_4 (sc, vrcb, bge_hostaddr.bge_addr_hi, 0);
    RCB_WRITE_4 (sc, vrcb, bge_hostaddr.bge_addr_lo, 0);
    RCB_WRITE_4 (sc, vrcb, bge_maxlen_flags, BGE_RCB_FLAG_RING_DISABLED);
    RCB_WRITE_4 (sc, vrcb, bge_nicaddr, 0);
    bge_writembx (sc, BGE_MBX_RX_CONS0_LO + (i * (int)sizeof (uint64_t)), 0);
    vrcb += sizeof (struct bge_rcb);
  }

  vrcb = BGE_MEMWIN_START + BGE_RX_RETURN_RING_RCB;
  BGE_HOSTADDR (taddr, sc->ldata.bge_rx_return_ring_paddr);
  RCB_WRITE_4 (sc, vrcb, bge_hostaddr.bge_addr_hi, taddr.bge_addr_hi);
  RCB_WRITE_4 (sc, vrcb, bge_hostaddr.bge_addr_lo, taddr.bge_addr_lo);
  RCB_WRITE_4 (sc, vrcb, bge_nicaddr, 0);
  RCB_WRITE_4 (
    sc,
    vrcb,
    bge_maxlen_flags,
    BGE_RCB_MAXLEN_FLAGS (sc->bge_return_ring_cnt, 0)
    );

  CSR_WRITE_4 (sc, BGE_TX_RANDOM_BACKOFF, sc->mac[5] & BGE_TX_BACKOFF_SEED_MASK);
  CSR_WRITE_4 (sc, BGE_TX_LENGTHS, 0x2620);
  CSR_WRITE_4 (sc, BGE_RX_RULES_CFG, 0x08);
  CSR_WRITE_4 (sc, BGE_RXLP_CFG, 0x181);
  CSR_WRITE_4 (sc, BGE_RXLP_STATS_ENABLE_MASK, 0x007fffff);
  CSR_WRITE_4 (sc, BGE_RXLP_STATS_CTL, 0x1);
  CSR_WRITE_4 (sc, BGE_HCC_MODE, 0);

  for (i = 0; i < BGE_TIMEOUT; i++) {
    DELAY (10);
    if ((CSR_READ_4 (sc, BGE_HCC_MODE) & BGE_HCCMODE_ENABLE) == 0) {
      break;
    }
  }

  if (i == BGE_TIMEOUT) {
    return -1;
  }

  /* FreeBSD defaults — ticks=0 has been flaky for status-block updates. */
  CSR_WRITE_4 (sc, BGE_HCC_RX_COAL_TICKS, 150);
  CSR_WRITE_4 (sc, BGE_HCC_TX_COAL_TICKS, 150);
  CSR_WRITE_4 (sc, BGE_HCC_RX_MAX_COAL_BDS, 10);
  CSR_WRITE_4 (sc, BGE_HCC_TX_MAX_COAL_BDS, 10);
  CSR_WRITE_4 (sc, BGE_HCC_RX_MAX_COAL_BDS_INT, 1);
  CSR_WRITE_4 (sc, BGE_HCC_TX_MAX_COAL_BDS_INT, 1);
  CSR_WRITE_4 (
    sc,
    BGE_HCC_STATUSBLK_ADDR_HI,
    (uint32_t)(sc->ldata.bge_status_block_paddr >> 32)
    );
  CSR_WRITE_4 (
    sc,
    BGE_HCC_STATUSBLK_ADDR_LO,
    (uint32_t)sc->ldata.bge_status_block_paddr
    );
  ZeroMem (sc->ldata.bge_status_block, 32);
  CSR_WRITE_4 (sc, BGE_HCC_MODE, BGE_STATBLKSZ_32BYTE | BGE_HCCMODE_ENABLE);
  CSR_WRITE_4 (sc, BGE_RBDC_MODE, BGE_RBDCMODE_ENABLE | BGE_RBDCMODE_ATTN);
  CSR_WRITE_4 (sc, BGE_RXLP_MODE, BGE_RXLPMODE_ENABLE);

  val = BGE_MACMODE_TXDMA_ENB | BGE_MACMODE_RXDMA_ENB |
        BGE_MACMODE_RX_STATS_CLEAR | BGE_MACMODE_TX_STATS_CLEAR |
        BGE_MACMODE_RX_STATS_ENB | BGE_MACMODE_TX_STATS_ENB |
        BGE_MACMODE_FRMHDR_DMA_ENB | BGE_PORTMODE_MII;
  CSR_WRITE_4 (sc, BGE_MAC_MODE, val);
  DELAY (40);
  BGE_SETBIT (sc, BGE_MISC_LOCAL_CTL, BGE_MLC_INTR_ONATTN);

  val = BGE_WDMAMODE_ENABLE | BGE_WDMAMODE_ALL_ATTNS;
  if (BGE_IS_5755_PLUS (sc)) {
    val |= BGE_WDMAMODE_STATUS_TAG_FIX;
  }

  CSR_WRITE_4 (sc, BGE_WDMA_MODE, val);
  DELAY (40);

  /* Read DMA — without this, TX never pulls host buffers. */
  val = BGE_RDMAMODE_ENABLE | BGE_RDMAMODE_ALL_ATTNS;
  if ((sc->flags & BGE_FLAG_PCIE) != 0) {
    val |= BGE_RDMAMODE_FIFO_LONG_BURST;
  }

  CSR_WRITE_4 (sc, BGE_RDMA_MODE, val);
  DELAY (40);

  /* RX/TX ring state machines (FreeBSD bge_blockinit tail). */
  CSR_WRITE_4 (sc, BGE_RDC_MODE, BGE_RDCMODE_ENABLE);
  CSR_WRITE_4 (sc, BGE_RBDI_MODE, BGE_RBDIMODE_ENABLE);
  CSR_WRITE_4 (sc, BGE_RDBDI_MODE, BGE_RDBDIMODE_ENABLE);
  CSR_WRITE_4 (sc, BGE_SBDC_MODE, BGE_SBDCMODE_ENABLE);
  CSR_WRITE_4 (sc, BGE_SDC_MODE, BGE_SDCMODE_ENABLE);
  CSR_WRITE_4 (sc, BGE_SDI_MODE, BGE_SDIMODE_ENABLE);
  CSR_WRITE_4 (sc, BGE_SBDI_MODE, BGE_SBDIMODE_ENABLE);
  CSR_WRITE_4 (sc, BGE_SRS_MODE, BGE_SRSMODE_ENABLE);

  CSR_WRITE_4 (sc, BGE_RX_MTU, BGE_FRAMELEN);
  for (i = 0; i < 4; i++) {
    CSR_WRITE_4 (sc, BGE_MAR0 + (i * 4), 0xffffffffu);
  }

  /* Data FIFO protection (Broadcom / FreeBSD, non-5717 PCIe). */
  if ((sc->flags & BGE_FLAG_PCIE) != 0) {
    val = CSR_READ_4 (sc, 0x7C00);
    CSR_WRITE_4 (sc, 0x7C00, val | (1u << 25));
  }

  CSR_WRITE_4 (
    sc,
    BGE_MAC_STS,
    BGE_MACSTAT_SYNC_CHANGED | BGE_MACSTAT_CFG_CHANGED |
      BGE_MACSTAT_MI_COMPLETE | BGE_MACSTAT_LINK_CHANGED
    );

  return 0;
}

int
metal_bge_attach (
  metal_bge_softc_t  *sc,
  uint8_t             bus,
  uint8_t             dev,
  uint8_t             func
  )
{
  UINT64  bar;

  ZeroMem (sc, sizeof (*sc));
  sc->bus  = bus;
  sc->dev  = dev;
  sc->func = func;
  pm_bios_pci_enable_mem_bm (bus, dev, func);
  bar = pm_bios_pci_bar_mmio (bus, dev, func, 0, NULL);
  if (bar == 0) {
    return -1;
  }

  sc->regs = (volatile uint8_t *)(UINTN)bar;
  sc->chipid =
    bge_chipid_read (sc, pm_bios_pci_read16 (bus, dev, func, 0x02));
  sc->asicrev = BGE_ASICREV (sc->chipid);
  bge_set_family_flags (sc);
  bge_read_mac (sc);
  return 0;
}

void
metal_bge_detach (
  metal_bge_softc_t  *sc
  )
{
  sc->running = 0;
  sc->regs    = NULL;
}

int
metal_bge_init (
  metal_bge_softc_t  *sc
  )
{
  uint32_t  mode;

  if (bge_dma_setup (sc) != 0) {
    return -1;
  }

  if (bge_reset (sc) != 0) {
    return -1;
  }

  if (bge_chipinit (sc) != 0) {
    return -1;
  }

  if (bge_blockinit (sc) != 0) {
    return -1;
  }

  bge_program_mac (sc);

  if (bge_init_rx_ring_std (sc) != 0) {
    return -1;
  }

  sc->rx_saved_considx = 0;
  bge_init_tx_ring (sc);

  mode = CSR_READ_4 (sc, BGE_TX_MODE);
  if (BGE_IS_5755_PLUS (sc)) {
    mode |= BGE_TXMODE_MBUF_LOCKUP_FIX;
  }

  CSR_WRITE_4 (sc, BGE_TX_MODE, mode | BGE_TXMODE_ENABLE);
  DELAY (100);
  mode = CSR_READ_4 (sc, BGE_RX_MODE);
  if (BGE_IS_5755_PLUS (sc)) {
    mode |= BGE_RXMODE_IPV6_ENABLE;
  }

  /* Promisc until station address path is iron-proven (DHCP Offer is unicast). */
  mode |= BGE_RXMODE_RX_PROMISC;
  CSR_WRITE_4 (sc, BGE_RX_MODE, mode | BGE_RXMODE_ENABLE);
  DELAY (10);
  CSR_WRITE_4 (sc, BGE_MAX_RX_FRAME_LOWAT, 2);

  BGE_SETBIT (sc, BGE_PCI_MISC_CTL, BGE_PCIMISCCTL_MASK_PCI_INTR);
  bge_writembx (sc, BGE_MBX_IRQ0_LO, 1);
  BGE_SETBIT (sc, BGE_MODE_CTL, BGE_MODECTL_STACKUP);

  if (bge_phy_init (sc) != 0) {
    /* Still bring the if up — AN may complete later; poll keeps running. */
    sc->link = 0;
    bge_mac_update_link (sc);
  }

  sc->running = 1;
  return 0;
}

STATIC void
bge_rxreuse_std (
  metal_bge_softc_t  *sc,
  int                 i
  )
{
  struct bge_rx_bd  *r;

  r = &sc->ldata.bge_rx_std_ring[sc->std];
  BGE_HOSTADDR (r->bge_addr, metal_bge_vtop (sc->rx_buf[i]));
  r->bge_flags = BGE_RXBDFLAG_END;
  r->bge_len   = sc->rx_buf_len[i];
  r->bge_idx   = (uint16_t)i;
  bge_dma_sync (r, sizeof (*r));
  BGE_INC (sc->std, BGE_STD_RX_RING_CNT);
}

STATIC void
bge_rxeof (
  metal_bge_softc_t  *sc,
  uint16_t            rx_prod,
  metal_bge_rx_fn     fn,
  void               *ctx
  )
{
  uint16_t  rx_cons = sc->rx_saved_considx;

  while (rx_cons != rx_prod) {
    struct bge_rx_bd  *cur;
    uint16_t           idx;
    uint32_t           len;

    cur = &sc->ldata.bge_rx_return_ring[rx_cons];
    idx = cur->bge_idx;
    BGE_INC (rx_cons, sc->bge_return_ring_cnt);
    if ((cur->bge_flags & BGE_RXBDFLAG_ERROR) != 0) {
#if METAL_BGE_DEBUG
      s_bge_rx_err++;
#endif
      bge_rxreuse_std (sc, idx);
      continue;
    }

    len = cur->bge_len;
    if (len > 14 && fn != NULL) {
      bge_dma_sync (sc->rx_buf[idx], len);
      fn (ctx, sc->rx_buf[idx], len - 4);
#if METAL_BGE_DEBUG
      s_bge_rx_ok++;
      if (s_bge_rx_ok <= 4u || (s_bge_rx_ok % 32u) == 0u) {
        BGE_DBG (
          "metal-bge: rx=%u tx=%u err=%u len=%u",
          s_bge_rx_ok,
          s_bge_tx_ok,
          s_bge_rx_err,
          len
          );
      }
#endif
    }

    bge_rxreuse_std (sc, idx);
  }

  sc->rx_saved_considx = rx_cons;
  MemoryFence ();
  bge_writembx (sc, BGE_MBX_RX_CONS0_LO, sc->rx_saved_considx);
  bge_writembx (
    sc,
    BGE_MBX_RX_STD_PROD_LO,
    (sc->std - 1) & (BGE_STD_RX_RING_CNT - 1)
    );
}

STATIC void
bge_txeof (
  metal_bge_softc_t  *sc,
  uint16_t            tx_cons
  )
{
  while (sc->tx_saved_considx != tx_cons) {
    BGE_INC (sc->tx_saved_considx, BGE_TX_RING_CNT);
    sc->txcnt--;
  }
}

void
metal_bge_poll (
  metal_bge_softc_t  *sc,
  metal_bge_rx_fn     fn,
  void               *ctx
  )
{
  uint16_t     rx_prod;
  uint16_t     tx_cons;
  STATIC UINT32  s_link_ticks;

  if (sc == NULL || !sc->running || sc->ldata.bge_status_block == NULL) {
    return;
  }

  /*
   * AN can finish after init — refresh link + GMII/MII.
   * Throttle MII (slow MDIO) so RX polling stays hot during DHCP.
   */
  if ((++s_link_ticks % 64u) == 0u || !sc->link) {
    int  bmsr;
    int  link;

    (void)bge_miibus_readreg (sc, BRGPHY_MII_BMSR);
    bmsr = bge_miibus_readreg (sc, BRGPHY_MII_BMSR);
    link = ((bmsr & BMSR_LINK) != 0) ? 1 : 0;
    if (link != sc->link) {
      sc->link = link;
      bge_mac_update_link (sc);
      BGE_DBG ("metal-bge: link %d", link);
    }
  }

  /*
   * Force a status-block refresh. Do NOT store into the status block: on WB
   * memory, writing bge_status dirties the same cache line as rx_prod/tx_cons
   * and a later writeback can erase NIC DMA updates (RX forever looks empty).
   */
  BGE_SETBIT (sc, BGE_HCC_MODE, BGE_HCCMODE_COAL_NOW);
  {
    volatile struct bge_status_block  *sb;

    bge_dma_sync (sc->ldata.bge_status_block, 32);
    sb      = (volatile struct bge_status_block *)sc->ldata.bge_status_block;
    rx_prod = sb->bge_idx[0].bge_rx_prod_idx;
    tx_cons = sb->bge_idx[0].bge_tx_cons_idx;
  }
  if (rx_prod != sc->rx_saved_considx) {
    bge_dma_sync (
      sc->ldata.bge_rx_return_ring,
      (UINTN)sc->bge_return_ring_cnt * sizeof (struct bge_rx_bd)
      );
  }

  bge_rxeof (sc, rx_prod, fn, ctx);
  bge_txeof (sc, tx_cons);

#if METAL_BGE_DEBUG
  if (s_bge_tx_ok != 0u && s_bge_rx_ok == 0u && (s_link_ticks % 256u) == 0u) {
    BGE_DBG (
      "metal-bge: poll tx=%u rx=0 err=%u prod=%u txcons=%u link=%d"
      " macTX=%u/%u/%u macRX=%u/%u/%u mode=0x%x",
      s_bge_tx_ok,
      s_bge_rx_err,
      (UINT32)rx_prod,
      (UINT32)tx_cons,
      sc->link,
      CSR_READ_4 (sc, BGE_TX_MAC_STATS_UCAST),
      CSR_READ_4 (sc, BGE_TX_MAC_STATS_MCAST),
      CSR_READ_4 (sc, BGE_TX_MAC_STATS_BCAST),
      CSR_READ_4 (sc, BGE_RX_MAC_STATS_UCAST),
      CSR_READ_4 (sc, BGE_RX_MAC_STATS_MCAST),
      CSR_READ_4 (sc, BGE_RX_MAC_STATS_BCAST),
      CSR_READ_4 (sc, BGE_MODE_CTL)
      );
  }
#endif
}

int
metal_bge_tx (
  metal_bge_softc_t  *sc,
  CONST VOID         *frame,
  UINT32              len
  )
{
  struct bge_tx_bd  *d;
  VOID              *buf;
  UINT32             idx;

  if (!sc->running || frame == NULL || len == 0 || len > METAL_BGE_MTU) {
    return -1;
  }

  if (sc->txcnt >= BGE_TX_RING_CNT - 4) {
    return -1;
  }

  buf = metal_bge_dma_alloc (len);
  if (buf == NULL) {
    return -1;
  }

  CopyMem (buf, frame, len);
  bge_dma_sync (buf, len);
  idx = sc->tx_prodidx;
  d   = &sc->ldata.bge_tx_ring[idx];
  BGE_HOSTADDR (d->bge_addr, metal_bge_vtop (buf));
  d->bge_len      = (uint16_t)len;
  /* Normal frames: END only (CPU_PRE/POST_DMA are for TSO). */
  d->bge_flags    = BGE_TXBDFLAG_END;
  d->bge_vlan_tag = 0;
  d->bge_mss      = 0;
  bge_dma_sync (d, sizeof (*d));
  BGE_INC (sc->tx_prodidx, BGE_TX_RING_CNT);
  sc->txcnt++;
  bge_writembx (sc, BGE_MBX_TX_HOST_PROD0_LO, sc->tx_prodidx);
#if METAL_BGE_DEBUG
  s_bge_tx_ok++;
  if (s_bge_tx_ok <= 8u) {
    CONST UINT8  *f;
    UINT16        etype;

    f     = (CONST UINT8 *)frame;
    etype = (UINT16)(((UINT16)f[12] << 8) | f[13]);
    BGE_DBG (
      "metal-bge: tx=%u len=%u dst=%02x:%02x:%02x:%02x:%02x:%02x et=0x%x",
      s_bge_tx_ok,
      len,
      f[0],
      f[1],
      f[2],
      f[3],
      f[4],
      f[5],
      (UINT32)etype
      );
  }
#endif

  return 0;
}
