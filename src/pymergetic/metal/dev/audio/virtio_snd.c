/** @file
  Virtio-snd PCM output (S16LE stereo 22050). (impl: efi|bios)
**/
#include <pymergetic/metal/dev/audio/audio_ops.h>
#include <pymergetic/metal/bus/virtio/virtio.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/bus/io/io.h>
#include <runtime/coro/coro.h>
#include <runtime/mem/mem.h>
#include <runtime/time/time.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define VSND_CTRL  0
#define VSND_EVENT 1
#define VSND_TX    2
#define VSND_QSZ   64
#define VSND_TX_BUFS 16
#define VSND_PERIOD  2048 /* bytes per TX period */

#define VIRTIO_SND_R_PCM_SET_PARAMS  0x0101u
#define VIRTIO_SND_R_PCM_PREPARE     0x0102u
#define VIRTIO_SND_R_PCM_START       0x0104u
#define VIRTIO_SND_R_PCM_STOP        0x0105u
#define VIRTIO_SND_S_OK              0x8000u

#define VIRTIO_SND_PCM_FMT_S16       5u
#define VIRTIO_SND_PCM_RATE_22050    10u

#pragma pack (1)
typedef struct {
  UINT32  Code;
} vsnd_hdr_t;

typedef struct {
  vsnd_hdr_t  Hdr;
  UINT32      StreamId;
  UINT32      BufferBytes;
  UINT32      PeriodBytes;
  UINT32      Features;
  UINT8       Channels;
  UINT8       Format;
  UINT8       Rate;
  UINT8       Padding;
} vsnd_pcm_set_params_t;

typedef struct {
  vsnd_hdr_t  Hdr;
  UINT32      StreamId;
} vsnd_pcm_hdr_t;

typedef struct {
  UINT32  StreamId;
} vsnd_pcm_xfer_t;
#pragma pack ()

typedef struct {
  INT32   used;
  UINT32  format;
  UINT32  queued;
  UINT32  consumed;
} vsnd_stream_t;

STATIC pm_metal_virtio_dev_t  mDev;
STATIC INT32                  mReady;
STATIC INT32                  mMuted;
STATIC INT32                  mStarted;
STATIC vsnd_stream_t          mStreams[4];
STATIC UINT8                 *mTxBufs[VSND_TX_BUFS];
STATIC UINT32                 mTxLens[VSND_TX_BUFS];
STATIC UINT32                 mTxFree; /* bitmap */
STATIC UINT8                  mCtrlReq[64];
STATIC UINT8                  mCtrlResp[64];

STATIC
INT32
VsndCtrl (
  CONST VOID  *req,
  UINT32       req_len,
  VOID        *resp,
  UINT32       resp_len
  )
{
  UINT16  head;
  UINT32  len;
  UINT64  deadline;

  if (req_len > sizeof (mCtrlReq) || resp_len > sizeof (mCtrlResp)) {
    return -1;
  }

  CopyMem (mCtrlReq, req, req_len);
  ZeroMem (mCtrlResp, resp_len);
  if (pm_metal_virtq_add2 (
        &mDev.vqs[VSND_CTRL],
        mCtrlReq,
        req_len,
        0,
        mCtrlResp,
        resp_len,
        1,
        &head
        ) != 0)
  {
    return -1;
  }

  pm_metal_virtq_kick (&mDev, &mDev.vqs[VSND_CTRL]);
  deadline = pm_metal_time_mono_us () + 2000000ull;
  while (pm_metal_time_mono_us () < deadline) {
    if (pm_metal_virtq_get_used (&mDev.vqs[VSND_CTRL], &head, &len)) {
      pm_metal_virtq_free_chain (&mDev.vqs[VSND_CTRL], head);
      CopyMem (resp, mCtrlResp, resp_len);
      (VOID)len;
      return 0;
    }

    if (gBS != NULL) {
      gBS->Stall (100);
    }
  }

  return -1;
}

STATIC
VOID
VsndPollTx (
  VOID
  )
{
  UINT16  head;
  UINT32  len;
  UINT32  i;

  if (!mReady) {
    return;
  }

  while (pm_metal_virtq_get_used (&mDev.vqs[VSND_TX], &head, &len)) {
    /* Match buffer by descriptor addr */
    {
      typedef struct {
        UINT64  Addr;
        UINT32  Len;
        UINT16  Flags;
        UINT16  Next;
      } desc_t;

      desc_t  *d;
      UINT8   *addr;

      d    = (desc_t *)mDev.vqs[VSND_TX].desc;
      addr = (UINT8 *)(UINTN)d[head].Addr;
      for (i = 0; i < VSND_TX_BUFS; i++) {
        if (mTxBufs[i] == addr) {
          if (mStreams[1].used && mTxLens[i] > sizeof (vsnd_pcm_xfer_t)) {
            mStreams[1].consumed += mTxLens[i] - sizeof (vsnd_pcm_xfer_t);
          }

          mTxFree |= (1u << i);
          mTxLens[i] = 0;
          break;
        }
      }
    }

    pm_metal_virtq_free_chain (&mDev.vqs[VSND_TX], head);
    (VOID)len;
  }
}

STATIC
int
VsndInit (
  VOID
  )
{
  UINT64               feats;
  vsnd_pcm_set_params_t  sp;
  vsnd_pcm_hdr_t         ph;
  vsnd_hdr_t             resp;
  UINT32                 i;

  if (mReady) {
    return 0;
  }

  if (pm_metal_virtio_open (PM_METAL_VIRTIO_DEV_SOUND, &mDev) != 0) {
    return -1;
  }

  feats = pm_metal_virtio_get_features (&mDev);
  feats &= PM_METAL_VIRTIO_F_VERSION_1;
  if (pm_metal_virtio_set_features (&mDev, feats) != 0) {
    pm_metal_virtio_close (&mDev);
    return -1;
  }

  if (pm_metal_virtio_setup_queue (&mDev, VSND_CTRL, VSND_QSZ) != 0
      || pm_metal_virtio_setup_queue (&mDev, VSND_EVENT, VSND_QSZ) != 0
      || pm_metal_virtio_setup_queue (&mDev, VSND_TX, VSND_QSZ) != 0)
  {
    pm_metal_virtio_close (&mDev);
    return -1;
  }

  (VOID)pm_metal_virtio_driver_ok (&mDev);

  ZeroMem (&sp, sizeof (sp));
  sp.Hdr.Code     = VIRTIO_SND_R_PCM_SET_PARAMS;
  sp.StreamId     = 0;
  sp.BufferBytes  = VSND_PERIOD * 4u;
  sp.PeriodBytes  = VSND_PERIOD;
  sp.Features     = 0;
  sp.Channels     = 2;
  sp.Format       = VIRTIO_SND_PCM_FMT_S16;
  sp.Rate         = VIRTIO_SND_PCM_RATE_22050;
  ZeroMem (&resp, sizeof (resp));
  if (VsndCtrl (&sp, sizeof (sp), &resp, sizeof (resp)) != 0
      || resp.Code != VIRTIO_SND_S_OK)
  {
    pm_metal_virtio_close (&mDev);
    return -1;
  }

  ZeroMem (&ph, sizeof (ph));
  ph.Hdr.Code = VIRTIO_SND_R_PCM_PREPARE;
  ph.StreamId = 0;
  ZeroMem (&resp, sizeof (resp));
  if (VsndCtrl (&ph, sizeof (ph), &resp, sizeof (resp)) != 0
      || resp.Code != VIRTIO_SND_S_OK)
  {
    pm_metal_virtio_close (&mDev);
    return -1;
  }

  ZeroMem (&ph, sizeof (ph));
  ph.Hdr.Code = VIRTIO_SND_R_PCM_START;
  ph.StreamId = 0;
  ZeroMem (&resp, sizeof (resp));
  if (VsndCtrl (&ph, sizeof (ph), &resp, sizeof (resp)) != 0
      || resp.Code != VIRTIO_SND_S_OK)
  {
    pm_metal_virtio_close (&mDev);
    return -1;
  }

  mTxFree = (1u << VSND_TX_BUFS) - 1u;
  for (i = 0; i < VSND_TX_BUFS; i++) {
    mTxBufs[i] = pm_metal_virtio_pages_alloc (
                   EFI_SIZE_TO_PAGES (sizeof (vsnd_pcm_xfer_t) + VSND_PERIOD)
                   );
    if (mTxBufs[i] == NULL) {
      pm_metal_virtio_close (&mDev);
      return -1;
    }
  }

  mStarted = 1;
  mReady   = 1;
  
  return 0;
}

STATIC
VOID
VsndPoll (
  VOID
  )
{
  VsndPollTx ();
}

STATIC
int32_t
VsndReady (
  VOID
  )
{
  return mReady ? 1 : 0;
}

STATIC
pm_metal_audio_stream_h
VsndOpen (
  uint32_t  format,
  uint32_t  frames
  )
{
  (VOID)frames;
  if (!mReady || format != PM_METAL_AUDIO_FMT_S16LE_STEREO_22050) {
    return PM_METAL_AUDIO_STREAM_INVALID;
  }

  if (mStreams[1].used) {
    return PM_METAL_AUDIO_STREAM_INVALID;
  }

  mStreams[1].used     = 1;
  mStreams[1].format   = format;
  mStreams[1].queued   = 0;
  mStreams[1].consumed = 0;
  return 1;
}

STATIC
VOID
VsndClose (
  pm_metal_audio_stream_h  s
  )
{
  if (s != 1) {
    return;
  }

  ZeroMem (&mStreams[1], sizeof (mStreams[1]));
}

STATIC
uint32_t
VsndQueue (
  pm_metal_audio_stream_h  s,
  CONST VOID              *pcm,
  uint32_t                 nbytes
  )
{
  UINT32  i;
  UINT32  n;
  UINT32  placed;

  if (s != 1 || !mStreams[1].used || pcm == NULL || nbytes == 0 || mMuted) {
    return 0;
  }

  placed = 0;
  while (placed < nbytes) {
    VsndPollTx ();
    for (i = 0; i < VSND_TX_BUFS; i++) {
      if ((mTxFree & (1u << i)) != 0) {
        break;
      }
    }

    if (i >= VSND_TX_BUFS) {
      break;
    }

    n = nbytes - placed;
    if (n > VSND_PERIOD) {
      n = VSND_PERIOD;
    }

    {
      vsnd_pcm_xfer_t  *x;

      x = (vsnd_pcm_xfer_t *)mTxBufs[i];
      x->StreamId = 0;
      CopyMem (mTxBufs[i] + sizeof (*x), (CONST UINT8 *)pcm + placed, n);
      mTxLens[i] = sizeof (*x) + n;
      if (pm_metal_virtq_add (
            &mDev.vqs[VSND_TX],
            mTxBufs[i],
            mTxLens[i],
            0,
            NULL
            ) != 0)
      {
        break;
      }

      mTxFree &= ~(1u << i);
      placed             += n;
      mStreams[1].queued += n;
    }
  }

  if (placed > 0) {
    pm_metal_virtq_kick (&mDev, &mDev.vqs[VSND_TX]);
  }

  return placed;
}

typedef struct {
  pm_metal_coro_t          coro;
  pm_metal_audio_stream_h  s;
  UINT32                   need;
  UINT64                   deadline;
} vsnd_drain_t;

STATIC
pm_metal_status_t
VsndDrainFn (
  pm_metal_coro_t  *self
  )
{
  vsnd_drain_t  *c;

  c = (vsnd_drain_t *)self;
  VsndPollTx ();
  if (c->s == 1 && mStreams[1].used
      && mStreams[1].consumed >= c->need)
  {
    return PM_METAL_DONE;
  }

  if (pm_metal_time_mono_us () > c->deadline) {
    return PM_METAL_ERROR;
  }

  return pm_metal_await (self, pm_metal_sleep_us (2000));
}

STATIC
pm_metal_async_handle_t
VsndDrain (
  pm_metal_audio_stream_h  s,
  uint32_t                 nbytes
  )
{
  vsnd_drain_t  *c;

  if (s != 1 || !mStreams[1].used) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  c = (vsnd_drain_t *)pm_metal_coro (VsndDrainFn, sizeof (*c));
  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  c->s        = s;
  c->need     = mStreams[1].consumed + nbytes;
  c->deadline = pm_metal_time_mono_us () + 10000000ull;
  return pm_metal_async_adopt_host_coro (&c->coro);
}

STATIC
VOID
VsndMute (
  int  on
  )
{
  vsnd_pcm_hdr_t  ph;
  vsnd_hdr_t      resp;

  mMuted = on ? 1 : 0;
  if (!mReady) {
    return;
  }

  ZeroMem (&ph, sizeof (ph));
  ph.Hdr.Code = on ? VIRTIO_SND_R_PCM_STOP : VIRTIO_SND_R_PCM_START;
  ph.StreamId = 0;
  ZeroMem (&resp, sizeof (resp));
  (VOID)VsndCtrl (&ph, sizeof (ph), &resp, sizeof (resp));
  mStarted = on ? 0 : 1;
}

STATIC CONST pm_metal_audio_ops_t  mVirtioOps = {
  "virtio-snd",
  VsndInit,
  VsndPoll,
  VsndReady,
  VsndOpen,
  VsndClose,
  VsndQueue,
  VsndDrain,
  VsndMute
};

int
pm_metal_audio_virtio_probe (
  VOID
  )
{
  if (VsndInit () != 0) {
    return -1;
  }

  pm_metal_audio_set_ops (&mVirtioOps);
  {
    STATIC pm_metal_io_node_t  Node = {
      .class = PM_METAL_IO_AUDIO,
      .compat = "virtio-snd",
      .caps = 1,
      .bus = PM_METAL_IO_BUS_PCI
    };

    (VOID)pm_metal_io_dt_add (&Node);
  }
  return 0;
}
