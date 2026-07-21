/** @file
  Virtio-blk (512-byte sectors). (impl: efi)
**/
#include <pymergetic/metal/blk/blk.h>
#include <pymergetic/metal/virtio/virtio.h>
#include <pymergetic/metal/io/io.h>
#include <time/time.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define VBLK_Q    0
#define VBLK_QSZ  64
#define VBLK_SEC  512u

#define VIRTIO_BLK_T_IN   0u
#define VIRTIO_BLK_T_OUT  1u
#define VIRTIO_BLK_S_OK   0u

#pragma pack (1)
typedef struct {
  UINT32  Type;
  UINT32  Reserved;
  UINT64  Sector;
} vblk_req_t;
#pragma pack ()

STATIC pm_metal_virtio_dev_t  mDev;
STATIC INT32                  mReady;
STATIC UINT64                 mCapacity;
STATIC UINT8                 *mDataBuf;
STATIC vblk_req_t            *mReq;
STATIC UINT8                 *mStatus;

STATIC
INT32
VblkXfer (
  UINT32  Type,
  UINT64  Lba,
  VOID   *Buf,
  UINT32  Nsec
  )
{
  UINT16  head;
  UINT32  len;
  UINT64  deadline;
  UINT32  bytes;
  INT32   data_write;

  if (!mReady || Buf == NULL || Nsec == 0 || Nsec > 8) {
    return -1;
  }

  bytes = Nsec * VBLK_SEC;
  ZeroMem (mReq, sizeof (*mReq));
  mReq->Type   = Type;
  mReq->Sector = Lba;
  *mStatus     = 0xff;
  data_write   = (Type == VIRTIO_BLK_T_IN) ? 1 : 0;

  if (Type == VIRTIO_BLK_T_OUT) {
    CopyMem (mDataBuf, Buf, bytes);
  } else {
    ZeroMem (mDataBuf, bytes);
  }

  if (pm_metal_virtq_add3 (
        &mDev.vqs[VBLK_Q],
        mReq,
        sizeof (*mReq),
        0,
        mDataBuf,
        bytes,
        data_write,
        mStatus,
        1,
        1,
        &head
        ) != 0)
  {
    return -1;
  }

  pm_metal_virtq_kick (&mDev, &mDev.vqs[VBLK_Q]);
  deadline = pm_metal_time_mono_us () + 5000000ull;
  while (pm_metal_time_mono_us () < deadline) {
    if (pm_metal_virtq_get_used (&mDev.vqs[VBLK_Q], &head, &len)) {
      pm_metal_virtq_free_chain (&mDev.vqs[VBLK_Q], head);
      (VOID)len;
      if (*mStatus != VIRTIO_BLK_S_OK) {
        return -1;
      }

      if (Type == VIRTIO_BLK_T_IN) {
        CopyMem (Buf, mDataBuf, bytes);
      }

      return 0;
    }

    if (gBS != NULL) {
      gBS->Stall (50);
    }
  }

  return -1;
}

STATIC
int
VblkInit (
  VOID
  )
{
  UINT64  feats;
  UINT8   cap[8];

  if (mReady) {
    return 0;
  }

  if (pm_metal_virtio_open (PM_METAL_VIRTIO_DEV_BLK, &mDev) != 0
      && pm_metal_virtio_open (PM_METAL_VIRTIO_DEV_BLK_LEGACY, &mDev) != 0)
  {
    return -1;
  }

  feats = pm_metal_virtio_get_features (&mDev);
  feats &= PM_METAL_VIRTIO_F_VERSION_1;
  if (pm_metal_virtio_set_features (&mDev, feats) != 0) {
    pm_metal_virtio_set_status (&mDev, 0);
    pm_metal_virtio_set_status (
      &mDev,
      (UINT8)(PM_METAL_VIRTIO_S_ACK | PM_METAL_VIRTIO_S_DRIVER)
      );
    if (pm_metal_virtio_set_features (&mDev, 0) != 0) {
      pm_metal_virtio_close (&mDev);
      return -1;
    }
  }

  ZeroMem (cap, sizeof (cap));
  if (pm_metal_virtio_cfg_read (&mDev, 0, cap, 8) != 0) {
    pm_metal_virtio_close (&mDev);
    return -1;
  }

  mCapacity = (UINT64)cap[0] | ((UINT64)cap[1] << 8) | ((UINT64)cap[2] << 16)
              | ((UINT64)cap[3] << 24) | ((UINT64)cap[4] << 32)
              | ((UINT64)cap[5] << 40) | ((UINT64)cap[6] << 48)
              | ((UINT64)cap[7] << 56);

  if (pm_metal_virtio_setup_queue (&mDev, VBLK_Q, VBLK_QSZ) != 0) {
    pm_metal_virtio_close (&mDev);
    return -1;
  }

  mReq     = AllocatePages (1);
  mDataBuf = AllocatePages (EFI_SIZE_TO_PAGES (VBLK_SEC * 8u));
  mStatus  = AllocatePages (1);
  if (mReq == NULL || mDataBuf == NULL || mStatus == NULL) {
    pm_metal_virtio_close (&mDev);
    return -1;
  }

  (VOID)pm_metal_virtio_driver_ok (&mDev);
  mReady = 1;
  return 0;
}

int
pm_metal_blk_virtio_probe (
  VOID
  )
{
  UINT8  sec[VBLK_SEC];

  if (VblkInit () != 0) {
    return -1;
  }

  {
    STATIC pm_metal_io_node_t  Node = {
      PM_METAL_IO_FS, "virtio-blk", 1
    };

    (VOID)pm_metal_io_dt_register (&Node);
  }

  ZeroMem (sec, sizeof (sec));
  if (pm_metal_blk_read (0, sec, 1) == 0
      && CompareMem (sec, "METALBLK1", 9) == 0)
  {
    /* magic ok */
  }

  return 0;
}

int
pm_metal_blk_ready (
  VOID
  )
{
  return mReady ? 1 : 0;
}

uint64_t
pm_metal_blk_capacity_sectors (
  VOID
  )
{
  return mCapacity;
}

int
pm_metal_blk_read (
  uint64_t  lba,
  VOID     *buf,
  uint32_t  nsec
  )
{
  return VblkXfer (VIRTIO_BLK_T_IN, lba, buf, nsec);
}

int
pm_metal_blk_write (
  uint64_t     lba,
  CONST VOID  *buf,
  uint32_t     nsec
  )
{
  return VblkXfer (VIRTIO_BLK_T_OUT, lba, (VOID *)buf, nsec);
}

void
pm_metal_blk_poll (
  VOID
  )
{
}
