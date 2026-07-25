/** @file
  Virtio-blk detector + driver (512-byte sectors). (impl: efi|bios)
**/
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/dev/blk/blk.h>
#include <pymergetic/metal/dev/blk/blk_ops.h>
#include <pymergetic/metal/bus/virtio/virtio.h>
#include <pymergetic/metal/bus/io/io.h>
#include <runtime/time/cpu.h>
#include <runtime/time/time.h>

#define VBLK_Q   0
#define VBLK_QSZ 64
#define VBLK_SEC 512u

#define VIRTIO_BLK_T_IN  0u
#define VIRTIO_BLK_T_OUT 1u
#define VIRTIO_BLK_S_OK  0u

#pragma pack(1)
typedef struct {
  uint32_t Type;
  uint32_t Reserved;
  uint64_t Sector;
} vblk_req_t;
#pragma pack()

typedef struct {
  pm_metal_virtio_dev_t Dev;
  int32_t               Ready;
  uint64_t              Capacity;
  uint8_t              *DataBuf;
  vblk_req_t           *Req;
  uint8_t              *Status;
} vblk_dev_t;

static vblk_dev_t mVblk;
static int32_t    mPresent;

typedef struct {
  uint16_t head;
  uint32_t type;
  uint32_t bytes;
  void    *buf;
  int32_t  used;
} vblk_xfer_cookie_t;

static int VblkXferStart(void *ctx, int write, uint64_t lba, void *buf, uint32_t nsec, void *cookie)
{
  vblk_dev_t         *v;
  vblk_xfer_cookie_t *c;
  uint32_t            type;
  int32_t             data_write;

  v = (vblk_dev_t *)ctx;
  c = (vblk_xfer_cookie_t *)cookie;
  if (v == NULL || c == NULL || !v->Ready || buf == NULL || nsec == 0 || nsec > 8) {
    return -1;
  }

  type     = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
  c->type  = type;
  c->bytes = nsec * VBLK_SEC;
  c->buf   = buf;
  c->used  = 0;
  c->head  = 0;
  memset(v->Req, 0, sizeof(*v->Req));
  v->Req->Type   = type;
  v->Req->Sector = lba;
  *v->Status     = 0xff;
  data_write     = (type == VIRTIO_BLK_T_IN) ? 1 : 0;

  if (type == VIRTIO_BLK_T_OUT) {
    memcpy(v->DataBuf, buf, c->bytes);
  } else {
    memset(v->DataBuf, 0, c->bytes);
  }

  if (pm_metal_virtq_add3(&v->Dev.vqs[VBLK_Q],
                          v->Req,
                          sizeof(*v->Req),
                          0,
                          v->DataBuf,
                          c->bytes,
                          data_write,
                          v->Status,
                          1,
                          1,
                          &c->head) != 0) {
    return -1;
  }

  pm_metal_virtq_kick(&v->Dev, &v->Dev.vqs[VBLK_Q]);
  return 0;
}

static int VblkXferPoll(void *ctx, void *cookie)
{
  vblk_dev_t         *v;
  vblk_xfer_cookie_t *c;
  uint16_t            head;
  uint32_t            len;

  v = (vblk_dev_t *)ctx;
  c = (vblk_xfer_cookie_t *)cookie;
  if (v == NULL || c == NULL) {
    return -1;
  }

  if (c->used) {
    return 1;
  }

  pm_metal_virtio_ack_isr(&v->Dev);
  if (!pm_metal_virtq_get_used(&v->Dev.vqs[VBLK_Q], &head, &len)) {
    return 0;
  }

  (void)len;
  c->head = head;
  c->used = 1;
  return 1;
}

static int VblkXferFinish(void *ctx, void *cookie)
{
  vblk_dev_t         *v;
  vblk_xfer_cookie_t *c;

  v = (vblk_dev_t *)ctx;
  c = (vblk_xfer_cookie_t *)cookie;
  if (v == NULL || c == NULL) {
    return -1;
  }

  if (c->used) {
    pm_metal_virtq_free_chain(&v->Dev.vqs[VBLK_Q], c->head);
  } else {
    pm_metal_virtq_free_chain(&v->Dev.vqs[VBLK_Q], c->head);
    return -1;
  }

  if (*v->Status != VIRTIO_BLK_S_OK) {
    return -1;
  }

  if (c->type == VIRTIO_BLK_T_IN && c->buf != NULL) {
    memcpy(c->buf, v->DataBuf, c->bytes);
  }

  return 0;
}

static int32_t VblkXfer(vblk_dev_t *v, uint32_t Type, uint64_t Lba, void *Buf, uint32_t Nsec)
{
  vblk_xfer_cookie_t cookie;
  uint64_t           deadline;

  memset(&cookie, 0, sizeof(cookie));
  if (VblkXferStart(v, (Type == VIRTIO_BLK_T_OUT) ? 1 : 0, Lba, Buf, Nsec, &cookie) != 0) {
    return -1;
  }

  /* Sync callers (boot smoke) — brief pause only, no Stall. */
  deadline = pm_metal_time_mono_us() + 5000000ull;
  while (pm_metal_time_mono_us() < deadline) {
    int32_t r;

    r = VblkXferPoll(v, &cookie);
    if (r < 0) {
      (void)VblkXferFinish(v, &cookie);
      return -1;
    }

    if (r > 0) {
      return VblkXferFinish(v, &cookie);
    }

    pm_metal_cpu_pause();
  }

  (void)VblkXferFinish(v, &cookie);
  return -1;
}

static int VblkReady(void *ctx)
{
  vblk_dev_t *v;

  v = (vblk_dev_t *)ctx;
  return (v != NULL && v->Ready) ? 1 : 0;
}

static uint64_t VblkCapacity(void *ctx)
{
  vblk_dev_t *v;

  v = (vblk_dev_t *)ctx;
  return (v != NULL) ? v->Capacity : 0;
}

static int VblkRead(void *ctx, uint64_t lba, void *buf, uint32_t nsec)
{
  return VblkXfer((vblk_dev_t *)ctx, VIRTIO_BLK_T_IN, lba, buf, nsec);
}

static int VblkWrite(void *ctx, uint64_t lba, const void *buf, uint32_t nsec)
{
  return VblkXfer((vblk_dev_t *)ctx, VIRTIO_BLK_T_OUT, lba, (void *)buf, nsec);
}

static int VblkOpen(vblk_dev_t *v)
{
  uint64_t feats;
  uint8_t  cap[8];

  if (v->Ready) {
    return 0;
  }

  if (pm_metal_virtio_open(PM_METAL_VIRTIO_DEV_BLK, &v->Dev) != 0 &&
      pm_metal_virtio_open(PM_METAL_VIRTIO_DEV_BLK_LEGACY, &v->Dev) != 0) {
    return -1;
  }

  feats = pm_metal_virtio_get_features(&v->Dev);
  feats &= PM_METAL_VIRTIO_F_VERSION_1;
  if (pm_metal_virtio_set_features(&v->Dev, feats) != 0) {
    pm_metal_virtio_set_status(&v->Dev, 0);
    pm_metal_virtio_set_status(&v->Dev,
                               (uint8_t)(PM_METAL_VIRTIO_S_ACK | PM_METAL_VIRTIO_S_DRIVER));
    if (pm_metal_virtio_set_features(&v->Dev, 0) != 0) {
      pm_metal_virtio_close(&v->Dev);
      return -1;
    }
  }

  memset(cap, 0, sizeof(cap));
  if (pm_metal_virtio_cfg_read(&v->Dev, 0, cap, 8) != 0) {
    pm_metal_virtio_close(&v->Dev);
    return -1;
  }

  v->Capacity = (uint64_t)cap[0] | ((uint64_t)cap[1] << 8) | ((uint64_t)cap[2] << 16) |
                ((uint64_t)cap[3] << 24) | ((uint64_t)cap[4] << 32) | ((uint64_t)cap[5] << 40) |
                ((uint64_t)cap[6] << 48) | ((uint64_t)cap[7] << 56);

  if (pm_metal_virtio_setup_queue(&v->Dev, VBLK_Q, VBLK_QSZ) != 0) {
    pm_metal_virtio_close(&v->Dev);
    return -1;
  }

  v->Req     = pm_metal_virtio_pages_alloc(1);
  v->DataBuf = pm_metal_virtio_pages_alloc(PM_METAL_VIRTIO_SIZE_TO_PAGES(VBLK_SEC * 8u));
  v->Status  = pm_metal_virtio_pages_alloc(1);
  if (v->Req == NULL || v->DataBuf == NULL || v->Status == NULL) {
    if (v->Req != NULL) {
      pm_metal_virtio_pages_free(v->Req, 1);
    }

    if (v->DataBuf != NULL) {
      pm_metal_virtio_pages_free(v->DataBuf, PM_METAL_VIRTIO_SIZE_TO_PAGES(VBLK_SEC * 8u));
    }

    if (v->Status != NULL) {
      pm_metal_virtio_pages_free(v->Status, 1);
    }

    pm_metal_virtio_close(&v->Dev);
    return -1;
  }

  (void)pm_metal_virtio_driver_ok(&v->Dev);
  v->Ready = 1;
  return 0;
}

int pm_metal_blk_virtio_resume(void)
{
  if (!mPresent) {
    return -1;
  }

  if (mVblk.Ready) {
    return 0;
  }

  return VblkOpen(&mVblk);
}

int pm_metal_blk_virtio_detect(void)
{
  pm_metal_io_node_t Node;
  pm_metal_blk_ops_t Ops;
  int32_t            dt_id;
  pm_metal_blk_h     h;

  if (mPresent) {
    return 0;
  }

  if (pm_metal_virtio_find(PM_METAL_VIRTIO_DEV_BLK) != 0 &&
      pm_metal_virtio_find(PM_METAL_VIRTIO_DEV_BLK_LEGACY) != 0) {
    return -1;
  }

  mPresent = 1;

  memset(&Node, 0, sizeof(Node));
  Node.class  = PM_METAL_IO_BLK;
  Node.compat = "virtio-blk";
  Node.caps   = 1;
  Node.bus    = PM_METAL_IO_BUS_PCI;
  Node.loc[0] = PM_METAL_VIRTIO_DEV_BLK;
  dt_id       = pm_metal_io_dt_add(&Node);
  if (dt_id < 0) {
    return -1;
  }

  memset(&Ops, 0, sizeof(Ops));
  Ops.compat      = "virtio-blk";
  Ops.dt_id       = (uint32_t)dt_id;
  Ops.ready       = VblkReady;
  Ops.capacity    = VblkCapacity;
  Ops.read        = VblkRead;
  Ops.write       = VblkWrite;
  Ops.xfer_start  = VblkXferStart;
  Ops.xfer_poll   = VblkXferPoll;
  Ops.xfer_finish = VblkXferFinish;
  Ops.ctx         = &mVblk;
  h               = pm_metal_blk_bind(&Ops);
  if (h == PM_METAL_BLK_INVALID) {
    return -1;
  }

  return 0;
}
