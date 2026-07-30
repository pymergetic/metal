/** @file
  Virtio-net L2 driver (frames only). IP stack is lwIP (net_lwip.c).
  (impl: efi|bios)
**/
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "__init__.h"
#include <pymergetic/metal/bus/virtio/__init__.h>

#define VNET_RX  0
#define VNET_TX  1
#define VNET_QSZ 64
#define VNET_MTU 1514
/* Match queue depth: one DMA buffer per in-flight TX/RX descriptor. */
#define VNET_RX_BUFS 64
#define VNET_TX_BUFS 64

#pragma pack(1)
typedef struct {
  uint8_t  Flags;
  uint8_t  GsoType;
  uint16_t HdrLen;
  uint16_t GsoSize;
  uint16_t CsumStart;
  uint16_t CsumOffset;
  uint16_t NumBuffers;
} vnet_hdr_t;
#pragma pack()

#define VNET_FRAME (VNET_MTU + (uint32_t)sizeof(vnet_hdr_t))

typedef struct {
  uint64_t Addr;
  uint32_t Len;
  uint16_t Flags;
  uint16_t Next;
} vnet_desc_t;

static pm_metal_virtio_dev_t mDev;
static int32_t               mReady;
static uint8_t               mMac[6];
static uint8_t              *mRxBufs[VNET_RX_BUFS];
/*
 * One scratch per possible in-flight TX. Busy bits prevent reuse before the
 * device retires the descriptor (the old 2-buffer ping-pong caused ~RTO stalls
 * when ASGI emitted hdr + multi-MSS body in one burst).
 */
static uint8_t  mTxScratch[VNET_TX_BUFS][VNET_MTU + sizeof(vnet_hdr_t)];
static uint8_t  mTxBusy[VNET_TX_BUFS];
static uint32_t mTxFreeCount;

static void vnet_tx_reap(void)
{
  uint16_t head;
  uint32_t ulen;

  while (pm_metal_virtq_get_used(&mDev.vqs[VNET_TX], &head, &ulen)) {
    vnet_desc_t *desc;
    uint8_t     *buf;
    uint32_t     i;

    (void)ulen;
    desc = (vnet_desc_t *)mDev.vqs[VNET_TX].desc;
    buf  = (uint8_t *)(uintptr_t)desc[head].Addr;
    if (buf >= mTxScratch[0] && buf < mTxScratch[0] + (VNET_TX_BUFS * VNET_FRAME)) {
      i = (uint32_t)((buf - mTxScratch[0]) / VNET_FRAME);
      if (i < VNET_TX_BUFS && mTxBusy[i]) {
        mTxBusy[i] = 0;
        mTxFreeCount++;
      }
    }
    pm_metal_virtq_free_chain(&mDev.vqs[VNET_TX], head);
  }
}

static int32_t vnet_tx_alloc(uint32_t *idx_out)
{
  uint32_t i;

  if (idx_out == NULL) {
    return -1;
  }
  vnet_tx_reap();
  if (mTxFreeCount == 0) {
    return -1;
  }
  for (i = 0; i < VNET_TX_BUFS; i++) {
    if (!mTxBusy[i]) {
      mTxBusy[i] = 1;
      mTxFreeCount--;
      *idx_out = i;
      return 0;
    }
  }
  return -1;
}

int pm_metal_dev_net_virtio_open(uint8_t mac_out[6])
{
  uint64_t feats;
  uint32_t i;

  if (mReady) {
    if (mac_out != NULL) {
      memcpy(mac_out, mMac, 6);
    }

    return 0;
  }

  if (pm_metal_virtio_open(PM_METAL_VIRTIO_DEV_NET, &mDev) != 0 &&
      pm_metal_virtio_open(PM_METAL_VIRTIO_DEV_NET_LEGACY, &mDev) != 0) {
    return -1;
  }

  feats = pm_metal_virtio_get_features(&mDev);
  feats &= PM_METAL_VIRTIO_F_VERSION_1 | (1ull << 5); /* MAC */
  if (pm_metal_virtio_set_features(&mDev, feats) != 0) {
    pm_metal_virtio_set_status(&mDev, 0);
    pm_metal_virtio_set_status(&mDev, (uint8_t)(PM_METAL_VIRTIO_S_ACK | PM_METAL_VIRTIO_S_DRIVER));
    if (pm_metal_virtio_set_features(&mDev, (1ull << 5)) != 0) {
      pm_metal_virtio_close(&mDev);
      return -1;
    }
  }

  {
    int32_t   mac_ok;
    uintptr_t mi;

    mac_ok = 0;
    if ((feats & (1ull << 5)) != 0 && pm_metal_virtio_cfg_read(&mDev, 0, mMac, 6) == 0 &&
        (mMac[0] & 0x01u) == 0) {
      for (mi = 0; mi < 6; mi++) {
        if (mMac[mi] != 0) {
          mac_ok = 1;
          break;
        }
      }
    }

    if (!mac_ok) {
      /* Locally administered fallback — never ship 00:00:00:00:00:00. */
      memset(mMac, 0x02, 6);
      mMac[5] = 0x15;
    }
  }

  if (pm_metal_virtio_setup_queue(&mDev, VNET_RX, VNET_QSZ) != 0 ||
      pm_metal_virtio_setup_queue(&mDev, VNET_TX, VNET_QSZ) != 0) {
    pm_metal_virtio_close(&mDev);
    return -1;
  }

  for (i = 0; i < VNET_RX_BUFS; i++) {
    mRxBufs[i] =
      pm_metal_virtio_pages_alloc(PM_METAL_VIRTIO_SIZE_TO_PAGES(sizeof(vnet_hdr_t) + VNET_MTU));
    if (mRxBufs[i] == NULL) {
      pm_metal_virtio_close(&mDev);
      return -1;
    }

    memset(mRxBufs[i], 0, sizeof(vnet_hdr_t) + VNET_MTU);
    (void)pm_metal_virtq_add(
      &mDev.vqs[VNET_RX], mRxBufs[i], sizeof(vnet_hdr_t) + VNET_MTU, 1, NULL);
  }

  memset(mTxBusy, 0, sizeof(mTxBusy));
  mTxFreeCount = VNET_TX_BUFS;

  pm_metal_virtq_kick(&mDev, &mDev.vqs[VNET_RX]);
  (void)pm_metal_virtio_driver_ok(&mDev);
  mReady = 1;
  if (mac_out != NULL) {
    memcpy(mac_out, mMac, 6);
  }

  return 0;
}

int pm_metal_dev_net_virtio_ready(void)
{
  return mReady ? 1 : 0;
}

const uint8_t *pm_metal_dev_net_virtio_mac(void)
{
  return mMac;
}

int pm_metal_dev_net_virtio_tx(const void *frame, uint32_t len)
{
  vnet_hdr_t *hdr;
  uint8_t    *scratch;
  uint8_t    *pkt;
  uint16_t    head;
  uint32_t    idx;

  if (!mReady || frame == NULL || len == 0 || len > VNET_MTU) {
    return -1;
  }

  if (vnet_tx_alloc(&idx) != 0) {
    return -1;
  }

  scratch = mTxScratch[idx];
  hdr     = (vnet_hdr_t *)scratch;
  memset(hdr, 0, sizeof(*hdr));
  pkt = scratch + sizeof(*hdr);
  memcpy(pkt, frame, len);

  if (pm_metal_virtq_add(&mDev.vqs[VNET_TX], scratch, sizeof(*hdr) + len, 0, &head) != 0) {
    mTxBusy[idx] = 0;
    mTxFreeCount++;
    return -1;
  }

  pm_metal_virtq_kick(&mDev, &mDev.vqs[VNET_TX]);
  return 0;
}

void pm_metal_dev_net_virtio_poll(pm_metal_dev_net_virtio_rx_fn on_frame, void *ctx)
{
  uint16_t head;
  uint32_t len;
  uint8_t *buf;

  if (!mReady) {
    return;
  }

  while (pm_metal_virtq_get_used(&mDev.vqs[VNET_RX], &head, &len)) {
    vnet_desc_t *desc;

    desc = (vnet_desc_t *)mDev.vqs[VNET_RX].desc;
    buf  = (uint8_t *)(uintptr_t)desc[head].Addr;
    if (on_frame != NULL && len > sizeof(vnet_hdr_t)) {
      on_frame(ctx, buf + sizeof(vnet_hdr_t), len - (uint32_t)sizeof(vnet_hdr_t));
    }

    pm_metal_virtq_free_chain(&mDev.vqs[VNET_RX], head);
    (void)pm_metal_virtq_add(&mDev.vqs[VNET_RX], buf, sizeof(vnet_hdr_t) + VNET_MTU, 1, NULL);
  }

  pm_metal_virtq_kick(&mDev, &mDev.vqs[VNET_RX]);
  vnet_tx_reap();
}
