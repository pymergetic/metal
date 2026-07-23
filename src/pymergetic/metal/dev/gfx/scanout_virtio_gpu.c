/** @file
  virtio-gpu scanout — RESOURCE_FLUSH present (QEMU reference GPU path).
**/
#include <pymergetic/metal/dev/gfx/scanout.h>
#include <pymergetic/metal/bus/virtio/virtio.h>
#include <runtime/mem/mem.h>
#include <runtime/time/time.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO        0x0100u
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D      0x0101u
#define VIRTIO_GPU_CMD_RESOURCE_UNREF          0x0102u
#define VIRTIO_GPU_CMD_SET_SCANOUT             0x0103u
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH          0x0104u
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D     0x0105u
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106u

#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM  2u
#define VIRTIO_GPU_FLAG_FENCE             (1u << 0)

#pragma pack (1)
typedef struct {
  UINT32  Type;
  UINT32  Flags;
  UINT64  FenceId;
  UINT32  CtxId;
  UINT32  Padding;
} vgpu_ctrl_hdr_t;

typedef struct {
  vgpu_ctrl_hdr_t  Hdr;
  UINT32           ResourceId;
  UINT32           Format;
  UINT32           Width;
  UINT32           Height;
} vgpu_res_create_2d_t;

typedef struct {
  UINT64  Addr;
  UINT32  Length;
  UINT32  Padding;
} vgpu_mem_entry_t;

typedef struct {
  vgpu_ctrl_hdr_t  Hdr;
  UINT32           ResourceId;
  UINT32           NrEntries;
} vgpu_attach_backing_t;

typedef struct {
  INT32  X;
  INT32  Y;
  UINT32 W;
  UINT32 H;
} vgpu_rect_t;

typedef struct {
  vgpu_ctrl_hdr_t  Hdr;
  UINT32           ScanoutId;
  UINT32           ResourceId;
  vgpu_rect_t      R;
} vgpu_set_scanout_t;

typedef struct {
  vgpu_ctrl_hdr_t  Hdr;
  vgpu_rect_t      R;
  UINT64           Offset;
  UINT32           ResourceId;
  UINT32           Padding;
} vgpu_transfer_to_host_2d_t;

typedef struct {
  vgpu_ctrl_hdr_t  Hdr;
  vgpu_rect_t      R;
  UINT32           ResourceId;
  UINT32           Padding;
} vgpu_resource_flush_t;

typedef struct {
  vgpu_ctrl_hdr_t  Hdr;
} vgpu_resp_t;
#pragma pack ()

typedef struct {
  pm_metal_virtio_dev_t  Dev;
  INT32                  Ready;
  UINT32                 ResId;
  UINT32                 W;
  UINT32                 H;
  UINT8                 *CmdBuf;
  UINT8                 *RespBuf;
  UINT32                 CmdCap;
} vgpu_t;

STATIC vgpu_t  mVg;

STATIC
INT32
VgpuCmd (
  VOID    *cmd,
  UINT32   cmd_len
  )
{
  pm_metal_virtq_t  *vq;
  UINT16             head;
  UINT64             deadline;
  UINT32             len;

  if (!mVg.Ready || cmd == NULL || cmd_len == 0 || cmd_len > mVg.CmdCap) {
    return -1;
  }

  vq = &mVg.Dev.vqs[0];
  CopyMem (mVg.CmdBuf, cmd, cmd_len);
  ZeroMem (mVg.RespBuf, sizeof (vgpu_resp_t));
  if (pm_metal_virtq_add2 (
        vq,
        mVg.CmdBuf,
        cmd_len,
        0,
        mVg.RespBuf,
        (UINT32)sizeof (vgpu_resp_t),
        1,
        &head
        ) != 0)
  {
    return -1;
  }

  pm_metal_virtq_kick (&mVg.Dev, vq);
  deadline = pm_metal_time_mono_us () + 500000u;
  while (pm_metal_time_mono_us () < deadline) {
    UINT16  uh;
    UINT32  ul;

    if (pm_metal_virtq_get_used (vq, &uh, &ul)) {
      pm_metal_virtq_free_chain (vq, uh);
      len = ((vgpu_resp_t *)mVg.RespBuf)->Hdr.Type;
      /* RESP_OK_NODATA = 0x1100 */
      return (len == 0x1100u || (len & 0xff00u) == 0x1100u) ? 0 : -1;
    }

    CpuPause ();
  }

  return -1;
}

STATIC
INT32
VgpuProbe (
  CONST pm_metal_scanout_bind_t  *b
  )
{
  vgpu_res_create_2d_t      create;
  vgpu_attach_backing_t    *attach;
  vgpu_mem_entry_t         *ent;
  vgpu_set_scanout_t        scan;
  UINT32                    attach_bytes;
  UINT8                    *attach_buf;
  UINT32                    pages;
  UINT32                    i;

  ZeroMem (&mVg, sizeof (mVg));
  if (b == NULL || b->shadow == NULL || b->mode_w == 0 || b->mode_h == 0) {
    return -1;
  }

  if (pm_metal_virtio_find (PM_METAL_VIRTIO_DEV_GPU) != 0) {
    return -1;
  }

  if (pm_metal_virtio_open (PM_METAL_VIRTIO_DEV_GPU, &mVg.Dev) != 0) {
    return -1;
  }

  pm_metal_virtio_set_status (&mVg.Dev, PM_METAL_VIRTIO_S_ACK);
  pm_metal_virtio_set_status (&mVg.Dev, PM_METAL_VIRTIO_S_DRIVER);
  (VOID)pm_metal_virtio_set_features (
          &mVg.Dev,
          pm_metal_virtio_get_features (&mVg.Dev) & PM_METAL_VIRTIO_F_VERSION_1
          );
  pm_metal_virtio_set_status (&mVg.Dev, PM_METAL_VIRTIO_S_FEATURES);
  if (pm_metal_virtio_setup_queue (&mVg.Dev, 0, 64) != 0) {
    pm_metal_virtio_close (&mVg.Dev);
    return -1;
  }

  if (pm_metal_virtio_driver_ok (&mVg.Dev) != 0) {
    pm_metal_virtio_close (&mVg.Dev);
    return -1;
  }

  mVg.CmdCap  = 4096u;
  mVg.CmdBuf  = (UINT8 *)pm_metal_virtio_pages_alloc (1);
  mVg.RespBuf = (UINT8 *)pm_metal_virtio_pages_alloc (1);
  if (mVg.CmdBuf == NULL || mVg.RespBuf == NULL) {
    pm_metal_virtio_close (&mVg.Dev);
    return -1;
  }

  mVg.W     = b->mode_w;
  mVg.H     = b->mode_h;
  mVg.ResId = 1;

  ZeroMem (&create, sizeof (create));
  create.Hdr.Type    = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
  create.ResourceId  = mVg.ResId;
  create.Format      = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
  create.Width       = mVg.W;
  create.Height      = mVg.H;
  if (VgpuCmd (&create, (UINT32)sizeof (create)) != 0) {
    goto fail;
  }

  pages        = (mVg.W * mVg.H * 4u + 4095u) / 4096u;
  attach_bytes = (UINT32)(sizeof (*attach) + pages * sizeof (*ent));
  attach_buf   = (UINT8 *)pm_metal_mem_alloc (
                            attach_bytes,
                            PM_METAL_MEM_HEAP,
                            PM_METAL_MEM_ID_NONE
                            );
  if (attach_buf == NULL) {
    goto fail;
  }

  attach = (vgpu_attach_backing_t *)attach_buf;
  ent    = (vgpu_mem_entry_t *)(attach_buf + sizeof (*attach));
  ZeroMem (attach_buf, attach_bytes);
  attach->Hdr.Type    = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
  attach->ResourceId  = mVg.ResId;
  attach->NrEntries   = pages;
  for (i = 0; i < pages; i++) {
    ent[i].Addr   = (UINT64)(UINTN)b->shadow + (UINT64)i * 4096u;
    ent[i].Length = 4096u;
  }

  if (VgpuCmd (attach_buf, attach_bytes) != 0) {
    pm_metal_mem_free (attach_buf);
    goto fail;
  }

  pm_metal_mem_free (attach_buf);

  ZeroMem (&scan, sizeof (scan));
  scan.Hdr.Type     = VIRTIO_GPU_CMD_SET_SCANOUT;
  scan.ScanoutId    = 0;
  scan.ResourceId   = mVg.ResId;
  scan.R.X          = 0;
  scan.R.Y          = 0;
  scan.R.W          = mVg.W;
  scan.R.H          = mVg.H;
  if (VgpuCmd (&scan, (UINT32)sizeof (scan)) != 0) {
    goto fail;
  }

  mVg.Ready = 1;
  return 0;

fail:
  pm_metal_virtio_close (&mVg.Dev);
  ZeroMem (&mVg, sizeof (mVg));
  return -1;
}

STATIC
INT32
VgpuPresentRect (
  INT32  x,
  INT32  y,
  INT32  w,
  INT32  h
  )
{
  vgpu_transfer_to_host_2d_t  xfer;
  vgpu_resource_flush_t       flush;
  CONST pm_metal_scanout_bind_t  *b;

  if (!mVg.Ready) {
    return -1;
  }

  b = pm_metal_scanout_bind_info ();
  if (b == NULL) {
    return -1;
  }

  if (x < 0) {
    w += x;
    x  = 0;
  }

  if (y < 0) {
    h += y;
    y  = 0;
  }

  if (w <= 0 || h <= 0) {
    return 0;
  }

  ZeroMem (&xfer, sizeof (xfer));
  xfer.Hdr.Type     = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
  xfer.R.X          = x;
  xfer.R.Y          = y;
  xfer.R.W          = (UINT32)w;
  xfer.R.H          = (UINT32)h;
  xfer.Offset       = ((UINT64)y * b->shadow_pitch + (UINT64)x) * 4u;
  xfer.ResourceId   = mVg.ResId;
  if (VgpuCmd (&xfer, (UINT32)sizeof (xfer)) != 0) {
    return -1;
  }

  ZeroMem (&flush, sizeof (flush));
  flush.Hdr.Type    = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
  flush.R           = xfer.R;
  flush.ResourceId  = mVg.ResId;
  return VgpuCmd (&flush, (UINT32)sizeof (flush));
}

STATIC
INT32
VgpuJobBegin (
  INT32  x,
  INT32  y,
  INT32  w,
  INT32  h
  )
{
  return (VgpuPresentRect (x, y, w, h) == 0) ? 0 : -1;
}

STATIC
INT32
VgpuJobStep (
  VOID
  )
{
  return 0;
}

STATIC
UINT32
VgpuCaps (
  VOID
  )
{
  return PM_METAL_SCANOUT_CAP_TEAR_FREE | PM_METAL_SCANOUT_CAP_DIRECT;
}

STATIC
INT32
VgpuAdoptShadow (
  UINT32  **pixels,
  UINT32   *pitch
  )
{
  CONST pm_metal_scanout_bind_t  *b;

  /* Backing is already the compositor shadow — nothing to swap. */
  b = pm_metal_scanout_bind_info ();
  if (!mVg.Ready || b == NULL || pixels == NULL) {
    return -1;
  }

  *pixels = b->shadow;
  if (pitch != NULL) {
    *pitch = b->shadow_pitch;
  }

  return 0;
}

STATIC
VOID
VgpuFini (
  VOID
  )
{
  if (mVg.CmdBuf != NULL) {
    pm_metal_virtio_pages_free (mVg.CmdBuf, 1);
  }

  if (mVg.RespBuf != NULL) {
    pm_metal_virtio_pages_free (mVg.RespBuf, 1);
  }

  if (mVg.Ready) {
    pm_metal_virtio_close (&mVg.Dev);
  }

  ZeroMem (&mVg, sizeof (mVg));
}

CONST pm_metal_scanout_ops_t  g_pm_metal_scanout_virtio_gpu = {
  "virtio_gpu",
  VgpuProbe,
  VgpuPresentRect,
  VgpuJobBegin,
  VgpuJobStep,
  VgpuCaps,
  VgpuAdoptShadow,
  NULL,
  VgpuFini
};
