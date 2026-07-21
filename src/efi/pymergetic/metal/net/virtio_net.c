/** @file
  Virtio-net L2 driver (frames only). IP stack is lwIP (net_lwip.c).
  (impl: efi)
**/
#include "virtio_netif.h"
#include <pymergetic/metal/virtio/virtio.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseLib.h>

#define VNET_RX    0
#define VNET_TX    1
#define VNET_QSZ   64
#define VNET_MTU   1514
#define VNET_RX_BUFS 32

#pragma pack (1)
typedef struct {
  UINT8   Flags;
  UINT8   GsoType;
  UINT16  HdrLen;
  UINT16  GsoSize;
  UINT16  CsumStart;
  UINT16  CsumOffset;
  UINT16  NumBuffers;
} vnet_hdr_t;
#pragma pack ()

STATIC pm_metal_virtio_dev_t  mDev;
STATIC INT32                  mReady;
STATIC UINT8                  mMac[6];
STATIC UINT8                 *mRxBufs[VNET_RX_BUFS];
STATIC UINT8                  mTxScratch[VNET_MTU + sizeof (vnet_hdr_t)];

int
pm_metal_virtio_netif_open (
  UINT8  mac_out[6]
  )
{
  UINT64  feats;
  UINT32  i;

  if (mReady) {
    if (mac_out != NULL) {
      CopyMem (mac_out, mMac, 6);
    }

    return 0;
  }

  if (pm_metal_virtio_open (PM_METAL_VIRTIO_DEV_NET, &mDev) != 0
      && pm_metal_virtio_open (PM_METAL_VIRTIO_DEV_NET_LEGACY, &mDev) != 0)
  {
    return -1;
  }

  feats = pm_metal_virtio_get_features (&mDev);
  feats &= PM_METAL_VIRTIO_F_VERSION_1 | (1ull << 5); /* MAC */
  if (pm_metal_virtio_set_features (&mDev, feats) != 0) {
    pm_metal_virtio_set_status (&mDev, 0);
    pm_metal_virtio_set_status (
      &mDev,
      (UINT8)(PM_METAL_VIRTIO_S_ACK | PM_METAL_VIRTIO_S_DRIVER)
      );
    if (pm_metal_virtio_set_features (&mDev, (1ull << 5)) != 0) {
      pm_metal_virtio_close (&mDev);
      return -1;
    }
  }

  if (pm_metal_virtio_cfg_read (&mDev, 0, mMac, 6) != 0) {
    SetMem (mMac, 6, 0x02);
    mMac[5] = 0x15;
  }

  if (pm_metal_virtio_setup_queue (&mDev, VNET_RX, VNET_QSZ) != 0
      || pm_metal_virtio_setup_queue (&mDev, VNET_TX, VNET_QSZ) != 0)
  {
    pm_metal_virtio_close (&mDev);
    return -1;
  }

  for (i = 0; i < VNET_RX_BUFS; i++) {
    mRxBufs[i] = AllocatePages (
                   EFI_SIZE_TO_PAGES (sizeof (vnet_hdr_t) + VNET_MTU)
                   );
    if (mRxBufs[i] == NULL) {
      pm_metal_virtio_close (&mDev);
      return -1;
    }

    ZeroMem (mRxBufs[i], sizeof (vnet_hdr_t) + VNET_MTU);
    (VOID)pm_metal_virtq_add (
            &mDev.vqs[VNET_RX],
            mRxBufs[i],
            sizeof (vnet_hdr_t) + VNET_MTU,
            1,
            NULL
            );
  }

  pm_metal_virtq_kick (&mDev, &mDev.vqs[VNET_RX]);
  (VOID)pm_metal_virtio_driver_ok (&mDev);
  mReady = 1;
  if (mac_out != NULL) {
    CopyMem (mac_out, mMac, 6);
  }

  return 0;
}

int
pm_metal_virtio_netif_ready (
  VOID
  )
{
  return mReady ? 1 : 0;
}

CONST UINT8 *
pm_metal_virtio_netif_mac (
  VOID
  )
{
  return mMac;
}

int
pm_metal_virtio_netif_tx (
  CONST VOID  *frame,
  UINT32       len
  )
{
  vnet_hdr_t  *hdr;
  UINT8       *pkt;
  UINT16       head;
  UINT32       ulen;

  if (!mReady || frame == NULL || len == 0 || len > VNET_MTU) {
    return -1;
  }

  hdr = (vnet_hdr_t *)mTxScratch;
  ZeroMem (hdr, sizeof (*hdr));
  pkt = mTxScratch + sizeof (*hdr);
  CopyMem (pkt, frame, len);

  if (pm_metal_virtq_add (
        &mDev.vqs[VNET_TX],
        mTxScratch,
        sizeof (*hdr) + len,
        0,
        &head
        ) != 0)
  {
    return -1;
  }

  pm_metal_virtq_kick (&mDev, &mDev.vqs[VNET_TX]);
  while (pm_metal_virtq_get_used (&mDev.vqs[VNET_TX], &head, &ulen)) {
    pm_metal_virtq_free_chain (&mDev.vqs[VNET_TX], head);
    (VOID)ulen;
  }

  return 0;
}

void
pm_metal_virtio_netif_poll (
  pm_metal_virtio_netif_rx_fn  on_frame,
  VOID                        *ctx
  )
{
  UINT16  head;
  UINT32  len;
  UINT8  *buf;

  if (!mReady) {
    return;
  }

  while (pm_metal_virtq_get_used (&mDev.vqs[VNET_RX], &head, &len)) {
    typedef struct {
      UINT64  Addr;
      UINT32  Len;
      UINT16  Flags;
      UINT16  Next;
    } desc_t;

    desc_t  *desc;

    desc = (desc_t *)mDev.vqs[VNET_RX].desc;
    buf  = (UINT8 *)(UINTN)desc[head].Addr;
    if (on_frame != NULL && len > sizeof (vnet_hdr_t)) {
      on_frame (
        ctx,
        buf + sizeof (vnet_hdr_t),
        len - (UINT32)sizeof (vnet_hdr_t)
        );
    }

    pm_metal_virtq_free_chain (&mDev.vqs[VNET_RX], head);
    (VOID)pm_metal_virtq_add (
            &mDev.vqs[VNET_RX],
            buf,
            sizeof (vnet_hdr_t) + VNET_MTU,
            1,
            NULL
            );
  }

  pm_metal_virtq_kick (&mDev, &mDev.vqs[VNET_RX]);

  while (pm_metal_virtq_get_used (&mDev.vqs[VNET_TX], &head, &len)) {
    pm_metal_virtq_free_chain (&mDev.vqs[VNET_TX], head);
    (VOID)len;
  }
}
