/**
 * @file Polled Broadcom bge driver for Metal (adapted from FreeBSD if_bge.c).
 * SPDX-License-Identifier: BSD-4-Clause
 */
#include "metal_bge.h"

#include "../../bus/pci/pci.h"
#include "../../runtime/mem/arena.h"

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#if !defined(BH_PLATFORM_METAL_BIOS)
#ifndef __UEFI_BOOT_SERVICES_TABLE_LIB_H__
extern EFI_BOOT_SERVICES  *gBS;
#endif
#include <Library/UefiBootServicesTableLib.h>
#endif

#define BRGPHY_MII_BMCR  0x00
#define BRGPHY_MII_BMSR  0x01
#define BMSR_LINK        0x0004u
#define BMCR_RESET       0x8000u
#define BMCR_ANENABLE    0x1000u

STATIC metal_bge_softc_t  mSc;

STATIC void
metal_bge_delay (
  UINTN  us
  )
{
  while (us-- > 0) {
    __asm__ volatile ("pause");
  }
}

#define DELAY(us)  metal_bge_delay (us)

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
      return (VOID *)(UINTN)pa;
    }
  }
#endif
  return pm_metal_arena_map (bytes);
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
  switch (off) {
    case BGE_PCI_MISC_CTL:
      return pm_bios_pci_read32 (sc->bus, sc->dev, sc->func, 0x7c);
    case BGE_PCI_CMD:
      return pm_bios_pci_read16 (sc->bus, sc->dev, sc->func, 0x04);
    case BGE_PCI_CACHESZ:
      return pm_bios_pci_read32 (sc->bus, sc->dev, sc->func, 0x0c);
    case BGE_PCI_DMA_RW_CTL:
      return pm_bios_pci_read32 (sc->bus, sc->dev, sc->func, 0x64);
    case BGE_PCI_PCISTATE:
      return pm_bios_pci_read32 (sc->bus, sc->dev, sc->func, 0x74);
    case BGE_PCI_MEMWIN_BASEADDR:
      return pm_bios_pci_read32 (sc->bus, sc->dev, sc->func, 0x80);
    case BGE_PCI_MEMWIN_DATA:
      return pm_bios_pci_read32 (sc->bus, sc->dev, sc->func, 0x84);
    case BGE_PCI_REG_BASEADDR:
      return pm_bios_pci_read32 (sc->bus, sc->dev, sc->func, 0x78);
    case BGE_PCI_REG_DATA:
      return pm_bios_pci_read32 (sc->bus, sc->dev, sc->func, 0x7c);
    case BGE_PCI_PRODID_ASICREV:
      return pm_bios_pci_read32 (sc->bus, sc->dev, sc->func, 0xa4);
    default:
      return pm_bios_pci_read32 (sc->bus, sc->dev, sc->func, (UINT8)off);
  }
}

void
bge_pci_write (
  metal_bge_softc_t  *sc,
  int                 off,
  uint32_t            val
  )
{
  switch (off) {
    case BGE_PCI_MISC_CTL:
      pm_bios_pci_write32 (sc->bus, sc->dev, sc->func, 0x7c, val);
      break;
    case BGE_PCI_CMD:
      pm_bios_pci_write16 (sc->bus, sc->dev, sc->func, 0x04, (UINT16)val);
      break;
    case BGE_PCI_CACHESZ:
      pm_bios_pci_write32 (sc->bus, sc->dev, sc->func, 0x0c, val);
      break;
    case BGE_PCI_DMA_RW_CTL:
      pm_bios_pci_write32 (sc->bus, sc->dev, sc->func, 0x64, val);
      break;
    case BGE_PCI_PCISTATE:
      pm_bios_pci_write32 (sc->bus, sc->dev, sc->func, 0x74, val);
      break;
    case BGE_PCI_MEMWIN_BASEADDR:
      pm_bios_pci_write32 (sc->bus, sc->dev, sc->func, 0x80, val);
      break;
    case BGE_PCI_MEMWIN_DATA:
      pm_bios_pci_write32 (sc->bus, sc->dev, sc->func, 0x84, val);
      break;
    case BGE_PCI_REG_BASEADDR:
      pm_bios_pci_write32 (sc->bus, sc->dev, sc->func, 0x78, val);
      break;
    case BGE_PCI_REG_DATA:
      pm_bios_pci_write32 (sc->bus, sc->dev, sc->func, 0x7c, val);
      break;
    default:
      pm_bios_pci_write32 (sc->bus, sc->dev, sc->func, (UINT8)off, val);
      break;
  }
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

  bge_miibus_writereg (sc, BRGPHY_MII_BMCR, BMCR_ANENABLE | BMCR_RESET);
  DELAY (100000);

  for (i = 0; i < 5000; i++) {
    bmsr = bge_miibus_readreg (sc, BRGPHY_MII_BMSR);
    if ((bmsr & BMSR_LINK) != 0) {
      sc->link = 1;
      return 0;
    }

    DELAY (1000);
  }

  sc->link = 0;
  return -1;
}

STATIC uint8_t
bge_eeprom_getbyte (
  metal_bge_softc_t  *sc,
  int                 addr
  )
{
  int       i;
  uint32_t  byte;

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

  byte = CSR_READ_4 (sc, BGE_EE_DATA);
  return (uint8_t)((byte >> ((addr % 4) * 8)) & 0xffu);
}

STATIC void
bge_read_mac (
  metal_bge_softc_t  *sc
  )
{
  int  i;

  for (i = 0; i < 6; i++) {
    sc->mac[i] = bge_eeprom_getbyte (sc, i);
  }

  if (sc->mac[0] == 0xff && sc->mac[1] == 0xff) {
    sc->mac[0] = 0x02;
    sc->mac[1] = 0x00;
    sc->mac[2] = 0x57;
    sc->mac[3] = (uint8_t)sc->bus;
    sc->mac[4] = (uint8_t)sc->dev;
    sc->mac[5] = (uint8_t)sc->func;
  }
}

STATIC uint32_t
bge_dma_swap_options (
  metal_bge_softc_t  *sc
  )
{
  (VOID)sc;
  return BGE_MODECTL_BYTESWAP_DATA | BGE_MODECTL_WORDSWAP_DATA |
         BGE_MODECTL_BYTESWAP_NONFRAME | BGE_MODECTL_WORDSWAP_NONFRAME;
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
    CSR_WRITE_4 (sc, BGE_MISC_CFG, 1u << 29);
    reset |= 1u << 29;
  }

  write_op (sc, BGE_MISC_CFG, (int)reset);
  DELAY (100 * 1000);

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

  for (i = 0; i < BGE_TIMEOUT; i++) {
    DELAY (10);
    val = bge_readmem_ind (sc, BGE_SRAM_FW_MB);
    if (val == (uint32_t)(~BGE_SRAM_FW_MB_MAGIC)) {
      break;
    }
  }

  val = (CSR_READ_4 (sc, BGE_MAC_MODE) & ~mac_mode_mask) | mac_mode;
  CSR_WRITE_4 (sc, BGE_MAC_MODE, val);
  DELAY (40);
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
  rcb->bge_maxlen_flags = BGE_RCB_MAXLEN_FLAGS (512, 0);
  rcb->bge_nicaddr      = BGE_STD_RX_RINGS;
  CSR_WRITE_4 (sc, BGE_RX_STD_RCB_HADDR_HI, rcb->bge_hostaddr.bge_addr_hi);
  CSR_WRITE_4 (sc, BGE_RX_STD_RCB_HADDR_LO, rcb->bge_hostaddr.bge_addr_lo);
  CSR_WRITE_4 (sc, BGE_RX_STD_RCB_MAXLEN_FLAGS, rcb->bge_maxlen_flags);
  CSR_WRITE_4 (sc, BGE_RX_STD_RCB_NICADDR, rcb->bge_nicaddr);
  bge_writembx (sc, BGE_MBX_RX_STD_PROD_LO, 0);

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

  CSR_WRITE_4 (sc, BGE_HCC_RX_COAL_TICKS, 0);
  CSR_WRITE_4 (sc, BGE_HCC_TX_COAL_TICKS, 0);
  CSR_WRITE_4 (sc, BGE_HCC_RX_MAX_COAL_BDS, 1);
  CSR_WRITE_4 (sc, BGE_HCC_TX_MAX_COAL_BDS, 1);
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
  CSR_WRITE_4 (sc, BGE_RX_MTU, BGE_FRAMELEN);
  for (i = 0; i < 4; i++) {
    CSR_WRITE_4 (sc, BGE_MAR0 + (i * 4), 0xffffffffu);
  }

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
  uint16_t  *m;
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

  m = (uint16_t *)sc->mac;
  CSR_WRITE_4 (sc, BGE_MAC_ADDR1_LO, (uint32_t)m[0]);
  CSR_WRITE_4 (sc, BGE_MAC_ADDR1_HI, ((uint32_t)m[1] << 16) | m[2]);

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

  CSR_WRITE_4 (sc, BGE_RX_MODE, mode | BGE_RXMODE_ENABLE);
  DELAY (10);
  CSR_WRITE_4 (sc, BGE_MAX_RX_FRAME_LOWAT, 2);

  BGE_SETBIT (sc, BGE_PCI_MISC_CTL, BGE_PCIMISCCTL_MASK_PCI_INTR);
  bge_writembx (sc, BGE_MBX_IRQ0_LO, 1);
  BGE_SETBIT (sc, BGE_MODE_CTL, BGE_MODECTL_STACKUP);

  (VOID)bge_phy_init (sc);
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

  r            = &sc->ldata.bge_rx_std_ring[sc->std];
  r->bge_flags = BGE_RXBDFLAG_END;
  r->bge_len   = sc->rx_buf_len[i];
  r->bge_idx   = (uint16_t)i;
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
      bge_rxreuse_std (sc, idx);
      continue;
    }

    len = cur->bge_len;
    if (len > 14 && fn != NULL) {
      fn (ctx, sc->rx_buf[idx], len - 4);
    }

    bge_rxreuse_std (sc, idx);
    bge_writembx (
      sc,
      BGE_MBX_RX_STD_PROD_LO,
      (sc->std - 1) & (BGE_STD_RX_RING_CNT - 1)
      );
  }

  sc->rx_saved_considx = rx_cons;
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
  metal_bge_rx_fn  fn,
  void            *ctx
  )
{
  metal_bge_softc_t  *sc = &mSc;
  uint16_t            rx_prod;
  uint16_t            tx_cons;

  if (!sc->running) {
    return;
  }

  rx_prod = sc->ldata.bge_status_block->bge_idx[0].bge_rx_prod_idx;
  tx_cons = sc->ldata.bge_status_block->bge_idx[0].bge_tx_cons_idx;
  sc->ldata.bge_status_block->bge_status = 0;
  bge_rxeof (sc, rx_prod, fn, ctx);
  bge_txeof (sc, tx_cons);
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
  idx = sc->tx_prodidx;
  d   = &sc->ldata.bge_tx_ring[idx];
  d->bge_addr.bge_addr_lo = (uint32_t)metal_bge_vtop (buf);
  d->bge_addr.bge_addr_hi = (uint32_t)(metal_bge_vtop (buf) >> 32);
  d->bge_len              = (uint16_t)len;
  d->bge_flags            = BGE_TXBDFLAG_END | BGE_TXBDFLAG_CPU_PRE_DMA |
                 BGE_TXBDFLAG_CPU_POST_DMA;
  d->bge_vlan_tag         = 0;
  d->bge_mss              = 0;
  BGE_INC (sc->tx_prodidx, BGE_TX_RING_CNT);
  sc->txcnt++;
  bge_writembx (sc, BGE_MBX_TX_HOST_PROD0_LO, sc->tx_prodidx);
  return 0;
}
