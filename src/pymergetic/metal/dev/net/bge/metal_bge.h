/** @file Metal port of FreeBSD bge (BSD-4-Clause) for polled lwIP L2. */
#ifndef PYMERGETIC_METAL_DEV_NET_BGE_METAL_BGE_H_
#define PYMERGETIC_METAL_DEV_NET_BGE_METAL_BGE_H_

#include <stdint.h>
#include <stddef.h>

#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN  1234
#endif
#ifndef BYTE_ORDER
#define BYTE_ORDER  LITTLE_ENDIAN
#endif

#include "freebsd/if_bgereg.h"

#undef CSR_READ_4
#undef CSR_WRITE_4
#undef BGE_SETBIT
#undef BGE_CLRBIT
#undef BGE_INC
#undef RCB_WRITE_4
#undef PCI_CLRBIT

#ifndef ETHER_HDR_LEN
#define ETHER_HDR_LEN   14
#endif
#ifndef ETHER_CRC_LEN
#define ETHER_CRC_LEN   4
#endif
#ifndef ETHER_MAX_LEN
#define ETHER_MAX_LEN   1500
#endif

#define BGE_VENDOR_BCOM  0x14E4u

/* ThinkPad T60-class wired NIC and close BCM57xx IDs. */
#define BGE_DEVICE_BCM5755   0x167Bu
#define BGE_DEVICE_BCM5755M  0x1673u
#define BGE_DEVICE_BCM5751M  0x167Du

#define METAL_BGE_MTU        1514u
#define METAL_BGE_RX_BUF_SZ  1536u
#define BGE_TIMEOUT          100000

#define BGE_FLAG_5755_PLUS   0x00100000u
#define BGE_FLAG_575X_PLUS   0x00080000u
#define BGE_FLAG_5705_PLUS  0x00020000u
#define BGE_FLAG_PCIE        0x00000400u
#define BGE_FLAG_4G_BNDRY_BUG  0x02000000u
#define BGE_FLAG_SHORT_DMA_BUG 0x08000000u
#define BGE_FLAG_MBOX_REORDER  0x20000000u
#define BGE_FLAG_EADDR       0x00000008u

#define BGE_IS_5755_PLUS(sc)   (((sc)->flags & BGE_FLAG_5755_PLUS) != 0)
#define BGE_IS_575X_PLUS(sc)   (((sc)->flags & BGE_FLAG_575X_PLUS) != 0)
#define BGE_IS_5705_PLUS(sc)   (((sc)->flags & BGE_FLAG_5705_PLUS) != 0)
#define BGE_IS_JUMBO_CAPABLE(sc)  0

#define BGE_INC(v, c)  do { (v)++; if ((v) >= (c)) (v) = 0; } while (0)

#define CSR_READ_4(sc, off) \
  (*(volatile uint32_t *)((uint8_t *)(sc)->regs + (off)))
#define CSR_WRITE_4(sc, off, val) \
  (CSR_READ_4((sc), (off)) = (uint32_t)(val))

#define BGE_SETBIT(sc, reg, x) \
  CSR_WRITE_4((sc), (reg), CSR_READ_4((sc), (reg)) | (x))
#define BGE_CLRBIT(sc, reg, x) \
  CSR_WRITE_4((sc), (reg), CSR_READ_4((sc), (reg)) & ~(x))

#define RCB_WRITE_4(sc, rcb, field, val) \
  bge_writemem_direct (                                      \
    (sc),                                                    \
    (int)((rcb) + offsetof (struct bge_rcb, field)),         \
    (int)(val)                                               \
    )

#define PCI_CLRBIT(sc, reg, x, sz) \
  bge_pci_write ((sc), (reg), bge_pci_read ((sc), (reg)) & ~(x))

typedef struct metal_bge_softc {
  uint8_t               bus;
  uint8_t               dev;
  uint8_t               func;
  volatile uint8_t     *regs;
  uint32_t              chipid;
  uint32_t              asicrev;
  uint32_t              flags;
  uint16_t              bge_return_ring_cnt;
  uint16_t              std;
  uint16_t              rx_saved_considx;
  uint16_t              tx_prodidx;
  uint16_t              tx_saved_considx;
  int                   txcnt;
  int                   link;
  int                   running;
  uint8_t               mac[6];
  uint8_t               phy_addr;
  uint32_t              mi_mode;
  struct bge_ring_data  ldata;
  uint8_t              *rx_buf[BGE_STD_RX_RING_CNT];
  uint32_t              rx_buf_len[BGE_STD_RX_RING_CNT];
} metal_bge_softc_t;

typedef void (*metal_bge_rx_fn)(void *ctx, const uint8_t *frame, uint32_t len);

int  metal_bge_attach(metal_bge_softc_t *sc, uint8_t bus, uint8_t dev,
                      uint8_t func);
void metal_bge_detach(metal_bge_softc_t *sc);
int  metal_bge_init(metal_bge_softc_t *sc);
void metal_bge_poll(metal_bge_rx_fn fn, void *ctx);
int  metal_bge_tx(metal_bge_softc_t *sc, const void *frame, uint32_t len);

uint32_t bge_pci_read(metal_bge_softc_t *sc, int off);
void     bge_pci_write(metal_bge_softc_t *sc, int off, uint32_t val);

#endif /* PYMERGETIC_METAL_DEV_NET_BGE_METAL_BGE_H_ */
