/** @file
  virtio-gpu scanout — RESOURCE_FLUSH present (QEMU reference GPU path).
**/
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/dev/gfx/scanout.h>
#include <pymergetic/metal/bus/virtio/virtio.h>
#include <pymergetic/metal/runtime/mem/mem.h>
#include <runtime/time/cpu.h>
#include <runtime/time/time.h>

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO        0x0100u
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D      0x0101u
#define VIRTIO_GPU_CMD_RESOURCE_UNREF          0x0102u
#define VIRTIO_GPU_CMD_SET_SCANOUT             0x0103u
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH          0x0104u
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D     0x0105u
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106u

#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM 2u
#define VIRTIO_GPU_FLAG_FENCE            (1u << 0)

#pragma pack(1)
typedef struct {
  uint32_t Type;
  uint32_t Flags;
  uint64_t FenceId;
  uint32_t CtxId;
  uint32_t Padding;
} vgpu_ctrl_hdr_t;

typedef struct {
  vgpu_ctrl_hdr_t Hdr;
  uint32_t        ResourceId;
  uint32_t        Format;
  uint32_t        Width;
  uint32_t        Height;
} vgpu_res_create_2d_t;

typedef struct {
  uint64_t Addr;
  uint32_t Length;
  uint32_t Padding;
} vgpu_mem_entry_t;

typedef struct {
  vgpu_ctrl_hdr_t Hdr;
  uint32_t        ResourceId;
  uint32_t        NrEntries;
} vgpu_attach_backing_t;

typedef struct {
  int32_t  X;
  int32_t  Y;
  uint32_t W;
  uint32_t H;
} vgpu_rect_t;

typedef struct {
  vgpu_ctrl_hdr_t Hdr;
  uint32_t        ScanoutId;
  uint32_t        ResourceId;
  vgpu_rect_t     R;
} vgpu_set_scanout_t;

typedef struct {
  vgpu_ctrl_hdr_t Hdr;
  vgpu_rect_t     R;
  uint64_t        Offset;
  uint32_t        ResourceId;
  uint32_t        Padding;
} vgpu_transfer_to_host_2d_t;

typedef struct {
  vgpu_ctrl_hdr_t Hdr;
  vgpu_rect_t     R;
  uint32_t        ResourceId;
  uint32_t        Padding;
} vgpu_resource_flush_t;

typedef struct {
  vgpu_ctrl_hdr_t Hdr;
} vgpu_resp_t;
#pragma pack()

typedef struct {
  pm_metal_virtio_dev_t Dev;
  int32_t               Ready;
  uint32_t              ResId;
  uint32_t              W;
  uint32_t              H;
  uint8_t              *CmdBuf;
  uint8_t              *RespBuf;
  uint32_t              CmdCap;
} vgpu_t;

static vgpu_t mVg;

static int32_t VgpuCmd(void *cmd, uint32_t cmd_len)
{
  pm_metal_virtq_t *vq;
  uint16_t          head;
  uint64_t          deadline;
  uint32_t          len;

  if (!mVg.Ready || cmd == NULL || cmd_len == 0 || cmd_len > mVg.CmdCap) {
    return -1;
  }

  vq = &mVg.Dev.vqs[0];
  memcpy(mVg.CmdBuf, cmd, cmd_len);
  memset(mVg.RespBuf, 0, sizeof(vgpu_resp_t));
  if (pm_metal_virtq_add2(
        vq, mVg.CmdBuf, cmd_len, 0, mVg.RespBuf, (uint32_t)sizeof(vgpu_resp_t), 1, &head) != 0) {
    return -1;
  }

  pm_metal_virtq_kick(&mVg.Dev, vq);
  deadline = pm_metal_time_mono_us() + 500000u;
  while (pm_metal_time_mono_us() < deadline) {
    uint16_t uh;
    uint32_t ul;

    if (pm_metal_virtq_get_used(vq, &uh, &ul)) {
      pm_metal_virtq_free_chain(vq, uh);
      len = ((vgpu_resp_t *)mVg.RespBuf)->Hdr.Type;
      /* RESP_OK_NODATA = 0x1100 */
      return (len == 0x1100u || (len & 0xff00u) == 0x1100u) ? 0 : -1;
    }

    pm_metal_cpu_pause();
  }

  return -1;
}

static int32_t VgpuProbe(const pm_metal_scanout_bind_t *b)
{
  vgpu_res_create_2d_t   create;
  vgpu_attach_backing_t *attach;
  vgpu_mem_entry_t      *ent;
  vgpu_set_scanout_t     scan;
  uint32_t               attach_bytes;
  uint8_t               *attach_buf;
  uint32_t               pages;
  uint32_t               i;

  memset(&mVg, 0, sizeof(mVg));
  if (b == NULL || b->shadow == NULL || b->mode_w == 0 || b->mode_h == 0) {
    return -1;
  }

  if (pm_metal_virtio_find(PM_METAL_VIRTIO_DEV_GPU) != 0) {
    return -1;
  }

  if (pm_metal_virtio_open(PM_METAL_VIRTIO_DEV_GPU, &mVg.Dev) != 0) {
    return -1;
  }

  pm_metal_virtio_set_status(&mVg.Dev, PM_METAL_VIRTIO_S_ACK);
  pm_metal_virtio_set_status(&mVg.Dev, PM_METAL_VIRTIO_S_DRIVER);
  (void)pm_metal_virtio_set_features(
    &mVg.Dev, pm_metal_virtio_get_features(&mVg.Dev) & PM_METAL_VIRTIO_F_VERSION_1);
  pm_metal_virtio_set_status(&mVg.Dev, PM_METAL_VIRTIO_S_FEATURES);
  if (pm_metal_virtio_setup_queue(&mVg.Dev, 0, 64) != 0) {
    pm_metal_virtio_close(&mVg.Dev);
    return -1;
  }

  if (pm_metal_virtio_driver_ok(&mVg.Dev) != 0) {
    pm_metal_virtio_close(&mVg.Dev);
    return -1;
  }

  mVg.CmdCap  = 4096u;
  mVg.CmdBuf  = (uint8_t *)pm_metal_virtio_pages_alloc(1);
  mVg.RespBuf = (uint8_t *)pm_metal_virtio_pages_alloc(1);
  if (mVg.CmdBuf == NULL || mVg.RespBuf == NULL) {
    pm_metal_virtio_close(&mVg.Dev);
    return -1;
  }

  mVg.W     = b->mode_w;
  mVg.H     = b->mode_h;
  mVg.ResId = 1;

  memset(&create, 0, sizeof(create));
  create.Hdr.Type   = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
  create.ResourceId = mVg.ResId;
  create.Format     = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
  create.Width      = mVg.W;
  create.Height     = mVg.H;
  if (VgpuCmd(&create, (uint32_t)sizeof(create)) != 0) {
    goto fail;
  }

  pages        = (mVg.W * mVg.H * 4u + 4095u) / 4096u;
  attach_bytes = (uint32_t)(sizeof(*attach) + pages * sizeof(*ent));
  attach_buf = (uint8_t *)pm_metal_mem_alloc(attach_bytes, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (attach_buf == NULL) {
    goto fail;
  }

  attach = (vgpu_attach_backing_t *)attach_buf;
  ent    = (vgpu_mem_entry_t *)(attach_buf + sizeof(*attach));
  memset(attach_buf, 0, attach_bytes);
  attach->Hdr.Type   = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
  attach->ResourceId = mVg.ResId;
  attach->NrEntries  = pages;
  for (i = 0; i < pages; i++) {
    ent[i].Addr   = (uint64_t)(uintptr_t)b->shadow + (uint64_t)i * 4096u;
    ent[i].Length = 4096u;
  }

  if (VgpuCmd(attach_buf, attach_bytes) != 0) {
    pm_metal_mem_free(attach_buf);
    goto fail;
  }

  pm_metal_mem_free(attach_buf);

  memset(&scan, 0, sizeof(scan));
  scan.Hdr.Type   = VIRTIO_GPU_CMD_SET_SCANOUT;
  scan.ScanoutId  = 0;
  scan.ResourceId = mVg.ResId;
  scan.R.X        = 0;
  scan.R.Y        = 0;
  scan.R.W        = mVg.W;
  scan.R.H        = mVg.H;
  if (VgpuCmd(&scan, (uint32_t)sizeof(scan)) != 0) {
    goto fail;
  }

  mVg.Ready = 1;
  return 0;

fail:
  pm_metal_virtio_close(&mVg.Dev);
  memset(&mVg, 0, sizeof(mVg));
  return -1;
}

static int32_t VgpuPresentRect(int32_t x, int32_t y, int32_t w, int32_t h)
{
  vgpu_transfer_to_host_2d_t     xfer;
  vgpu_resource_flush_t          flush;
  const pm_metal_scanout_bind_t *b;

  if (!mVg.Ready) {
    return -1;
  }

  b = pm_metal_scanout_bind_info();
  if (b == NULL) {
    return -1;
  }

  if (x < 0) {
    w += x;
    x = 0;
  }

  if (y < 0) {
    h += y;
    y = 0;
  }

  if (w <= 0 || h <= 0) {
    return 0;
  }

  memset(&xfer, 0, sizeof(xfer));
  xfer.Hdr.Type   = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
  xfer.R.X        = x;
  xfer.R.Y        = y;
  xfer.R.W        = (uint32_t)w;
  xfer.R.H        = (uint32_t)h;
  xfer.Offset     = ((uint64_t)y * b->shadow_pitch + (uint64_t)x) * 4u;
  xfer.ResourceId = mVg.ResId;
  if (VgpuCmd(&xfer, (uint32_t)sizeof(xfer)) != 0) {
    return -1;
  }

  memset(&flush, 0, sizeof(flush));
  flush.Hdr.Type   = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
  flush.R          = xfer.R;
  flush.ResourceId = mVg.ResId;
  return VgpuCmd(&flush, (uint32_t)sizeof(flush));
}

static int32_t VgpuJobBegin(int32_t x, int32_t y, int32_t w, int32_t h)
{
  return (VgpuPresentRect(x, y, w, h) == 0) ? 0 : -1;
}

static int32_t VgpuJobStep(void)
{
  return 0;
}

static uint32_t VgpuCaps(void)
{
  return PM_METAL_SCANOUT_CAP_TEAR_FREE | PM_METAL_SCANOUT_CAP_DIRECT;
}

static int32_t VgpuAdoptShadow(uint32_t **pixels, uint32_t *pitch)
{
  const pm_metal_scanout_bind_t *b;

  /* Backing is already the compositor shadow — nothing to swap. */
  b = pm_metal_scanout_bind_info();
  if (!mVg.Ready || b == NULL || pixels == NULL) {
    return -1;
  }

  *pixels = b->shadow;
  if (pitch != NULL) {
    *pitch = b->shadow_pitch;
  }

  return 0;
}

static void VgpuFini(void)
{
  if (mVg.CmdBuf != NULL) {
    pm_metal_virtio_pages_free(mVg.CmdBuf, 1);
  }

  if (mVg.RespBuf != NULL) {
    pm_metal_virtio_pages_free(mVg.RespBuf, 1);
  }

  if (mVg.Ready) {
    pm_metal_virtio_close(&mVg.Dev);
  }

  memset(&mVg, 0, sizeof(mVg));
}

const pm_metal_scanout_ops_t g_pm_metal_scanout_virtio_gpu = {
  "virtio_gpu",    VgpuProbe, VgpuPresentRect, VgpuJobBegin, VgpuJobStep, VgpuCaps,
  VgpuAdoptShadow, NULL,      VgpuFini
};
