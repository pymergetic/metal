/** @file
  Virtio-blk detector + driver (512-byte sectors). (impl: efi|bios)
**/
#include <pymergetic/metal/dev/blk/blk.h>
#include <pymergetic/metal/dev/blk/blk_ops.h>
#include <pymergetic/metal/bus/virtio/virtio.h>
#include <pymergetic/metal/bus/io/io.h>
#include <runtime/time/time.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
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

typedef struct {
  pm_metal_virtio_dev_t  Dev;
  INT32                  Ready;
  UINT64                 Capacity;
  UINT8                 *DataBuf;
  vblk_req_t            *Req;
  UINT8                 *Status;
} vblk_dev_t;

STATIC vblk_dev_t  mVblk;
STATIC INT32       mPresent;

STATIC
INT32
VblkXfer (
  vblk_dev_t  *v,
  UINT32       Type,
  UINT64       Lba,
  VOID        *Buf,
  UINT32       Nsec
  )
{
  UINT16  head;
  UINT32  len;
  UINT64  deadline;
  UINT32  bytes;
  INT32   data_write;

  if (v == NULL || !v->Ready || Buf == NULL || Nsec == 0 || Nsec > 8) {
    return -1;
  }

  bytes = Nsec * VBLK_SEC;
  ZeroMem (v->Req, sizeof (*v->Req));
  v->Req->Type   = Type;
  v->Req->Sector = Lba;
  *v->Status     = 0xff;
  data_write     = (Type == VIRTIO_BLK_T_IN) ? 1 : 0;

  if (Type == VIRTIO_BLK_T_OUT) {
    CopyMem (v->DataBuf, Buf, bytes);
  } else {
    ZeroMem (v->DataBuf, bytes);
  }

  if (pm_metal_virtq_add3 (
        &v->Dev.vqs[VBLK_Q],
        v->Req,
        sizeof (*v->Req),
        0,
        v->DataBuf,
        bytes,
        data_write,
        v->Status,
        1,
        1,
        &head
        ) != 0)
  {
    return -1;
  }

  pm_metal_virtq_kick (&v->Dev, &v->Dev.vqs[VBLK_Q]);
  deadline = pm_metal_time_mono_us () + 5000000ull;
  while (pm_metal_time_mono_us () < deadline) {
    if (pm_metal_virtq_get_used (&v->Dev.vqs[VBLK_Q], &head, &len)) {
      pm_metal_virtq_free_chain (&v->Dev.vqs[VBLK_Q], head);
      (VOID)len;
      if (*v->Status != VIRTIO_BLK_S_OK) {
        return -1;
      }

      if (Type == VIRTIO_BLK_T_IN) {
        CopyMem (Buf, v->DataBuf, bytes);
      }

      return 0;
    }

    if (gBS != NULL) {
      gBS->Stall (50);
    } else {
      CpuPause ();
    }
    pm_metal_virtio_ack_isr (&v->Dev);
  }

  pm_metal_virtq_free_chain (&v->Dev.vqs[VBLK_Q], head);
  return -1;
}

STATIC
int
VblkReady (
  VOID  *ctx
  )
{
  vblk_dev_t  *v;

  v = (vblk_dev_t *)ctx;
  return (v != NULL && v->Ready) ? 1 : 0;
}

STATIC
uint64_t
VblkCapacity (
  VOID  *ctx
  )
{
  vblk_dev_t  *v;

  v = (vblk_dev_t *)ctx;
  return (v != NULL) ? v->Capacity : 0;
}

STATIC
int
VblkRead (
  VOID      *ctx,
  uint64_t   lba,
  VOID      *buf,
  uint32_t   nsec
  )
{
  return VblkXfer ((vblk_dev_t *)ctx, VIRTIO_BLK_T_IN, lba, buf, nsec);
}

STATIC
int
VblkWrite (
  VOID        *ctx,
  uint64_t     lba,
  CONST VOID  *buf,
  uint32_t     nsec
  )
{
  return VblkXfer ((vblk_dev_t *)ctx, VIRTIO_BLK_T_OUT, lba, (VOID *)buf, nsec);
}

STATIC
int
VblkOpen (
  vblk_dev_t  *v
  )
{
  UINT64  feats;
  UINT8   cap[8];

  if (v->Ready) {
    return 0;
  }

  if (pm_metal_virtio_open (PM_METAL_VIRTIO_DEV_BLK, &v->Dev) != 0
      && pm_metal_virtio_open (PM_METAL_VIRTIO_DEV_BLK_LEGACY, &v->Dev) != 0)
  {
    return -1;
  }

  feats = pm_metal_virtio_get_features (&v->Dev);
  feats &= PM_METAL_VIRTIO_F_VERSION_1;
  if (pm_metal_virtio_set_features (&v->Dev, feats) != 0) {
    pm_metal_virtio_set_status (&v->Dev, 0);
    pm_metal_virtio_set_status (
      &v->Dev,
      (UINT8)(PM_METAL_VIRTIO_S_ACK | PM_METAL_VIRTIO_S_DRIVER)
      );
    if (pm_metal_virtio_set_features (&v->Dev, 0) != 0) {
      pm_metal_virtio_close (&v->Dev);
      return -1;
    }
  }

  ZeroMem (cap, sizeof (cap));
  if (pm_metal_virtio_cfg_read (&v->Dev, 0, cap, 8) != 0) {
    pm_metal_virtio_close (&v->Dev);
    return -1;
  }

  v->Capacity = (UINT64)cap[0] | ((UINT64)cap[1] << 8) | ((UINT64)cap[2] << 16)
                | ((UINT64)cap[3] << 24) | ((UINT64)cap[4] << 32)
                | ((UINT64)cap[5] << 40) | ((UINT64)cap[6] << 48)
                | ((UINT64)cap[7] << 56);

  if (pm_metal_virtio_setup_queue (&v->Dev, VBLK_Q, VBLK_QSZ) != 0) {
    pm_metal_virtio_close (&v->Dev);
    return -1;
  }

  v->Req     = pm_metal_virtio_pages_alloc (1);
  v->DataBuf = pm_metal_virtio_pages_alloc (EFI_SIZE_TO_PAGES (VBLK_SEC * 8u));
  v->Status  = pm_metal_virtio_pages_alloc (1);
  if (v->Req == NULL || v->DataBuf == NULL || v->Status == NULL) {
    if (v->Req != NULL) {
      pm_metal_virtio_pages_free (v->Req, 1);
    }

    if (v->DataBuf != NULL) {
      pm_metal_virtio_pages_free (v->DataBuf, EFI_SIZE_TO_PAGES (VBLK_SEC * 8u));
    }

    if (v->Status != NULL) {
      pm_metal_virtio_pages_free (v->Status, 1);
    }

    pm_metal_virtio_close (&v->Dev);
    return -1;
  }

  (VOID)pm_metal_virtio_driver_ok (&v->Dev);
  v->Ready = 1;
  return 0;
}

int
pm_metal_blk_virtio_resume (
  VOID
  )
{
  if (!mPresent) {
    return -1;
  }

  if (mVblk.Ready) {
    return 0;
  }

  return VblkOpen (&mVblk);
}

int
pm_metal_blk_virtio_detect (
  VOID
  )
{
  pm_metal_io_node_t  Node;
  pm_metal_blk_ops_t  Ops;
  INT32               dt_id;
  pm_metal_blk_h      h;

  if (mPresent) {
    return 0;
  }

  if (pm_metal_virtio_find (PM_METAL_VIRTIO_DEV_BLK) != 0
      && pm_metal_virtio_find (PM_METAL_VIRTIO_DEV_BLK_LEGACY) != 0)
  {
    return -1;
  }

  mPresent = 1;

  ZeroMem (&Node, sizeof (Node));
  Node.class  = PM_METAL_IO_BLK;
  Node.compat = "virtio-blk";
  Node.caps   = 1;
  Node.bus    = PM_METAL_IO_BUS_PCI;
  Node.loc[0] = PM_METAL_VIRTIO_DEV_BLK;
  dt_id       = pm_metal_io_dt_add (&Node);
  if (dt_id < 0) {
    return -1;
  }

  ZeroMem (&Ops, sizeof (Ops));
  Ops.compat   = "virtio-blk";
  Ops.dt_id    = (UINT32)dt_id;
  Ops.ready    = VblkReady;
  Ops.capacity = VblkCapacity;
  Ops.read     = VblkRead;
  Ops.write    = VblkWrite;
  Ops.ctx      = &mVblk;
  h            = pm_metal_blk_bind (&Ops);
  if (h == PM_METAL_BLK_INVALID) {
    return -1;
  }

  return 0;
}
