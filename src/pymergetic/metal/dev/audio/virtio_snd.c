/** @file
  Virtio-snd PCM output (S16LE stereo 22050). (impl: efi|bios)
**/
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/dev/audio/audio_ops.h>
#include <pymergetic/metal/bus/virtio/virtio.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/bus/io/io.h>
#include <pymergetic/metal/runtime/mem/mem.h>
#include <runtime/time/time.h>
#include <runtime/time/cpu.h>
#include <runtime/run/run.h>

#define VSND_CTRL    0
#define VSND_EVENT   1
#define VSND_TX      2
#define VSND_QSZ     64
#define VSND_TX_BUFS 16
#define VSND_PERIOD  2048 /* bytes per TX period */

#define VIRTIO_SND_R_PCM_SET_PARAMS 0x0101u
#define VIRTIO_SND_R_PCM_PREPARE    0x0102u
#define VIRTIO_SND_R_PCM_START      0x0104u
#define VIRTIO_SND_R_PCM_STOP       0x0105u
#define VIRTIO_SND_S_OK             0x8000u

#define VIRTIO_SND_PCM_FMT_S16    5u
#define VIRTIO_SND_PCM_RATE_22050 10u

#pragma pack(1)
typedef struct {
  uint32_t Code;
} vsnd_hdr_t;

typedef struct {
  vsnd_hdr_t Hdr;
  uint32_t   StreamId;
  uint32_t   BufferBytes;
  uint32_t   PeriodBytes;
  uint32_t   Features;
  uint8_t    Channels;
  uint8_t    Format;
  uint8_t    Rate;
  uint8_t    Padding;
} vsnd_pcm_set_params_t;

typedef struct {
  vsnd_hdr_t Hdr;
  uint32_t   StreamId;
} vsnd_pcm_hdr_t;

typedef struct {
  uint32_t StreamId;
} vsnd_pcm_xfer_t;
#pragma pack()

typedef struct {
  int32_t  used;
  uint32_t format;
  uint32_t queued;
  uint32_t consumed;
} vsnd_stream_t;

static pm_metal_virtio_dev_t mDev;
static int32_t               mReady;
static int32_t               mMuted;
static int32_t               mStarted;
static vsnd_stream_t         mStreams[4];
static uint8_t              *mTxBufs[VSND_TX_BUFS];
static uint32_t              mTxLens[VSND_TX_BUFS];
static uint32_t              mTxFree; /* bitmap */
static uint8_t               mCtrlReq[64];
static uint8_t               mCtrlResp[64];

static int32_t VsndCtrl(const void *req, uint32_t req_len, void *resp, uint32_t resp_len)
{
  uint16_t head;
  uint32_t len;
  uint64_t deadline;

  if (req_len > sizeof(mCtrlReq) || resp_len > sizeof(mCtrlResp)) {
    return -1;
  }

  memcpy(mCtrlReq, req, req_len);
  memset(mCtrlResp, 0, resp_len);
  if (pm_metal_virtq_add2(
        &mDev.vqs[VSND_CTRL], mCtrlReq, req_len, 0, mCtrlResp, resp_len, 1, &head) != 0) {
    return -1;
  }

  pm_metal_virtq_kick(&mDev, &mDev.vqs[VSND_CTRL]);
  deadline = pm_metal_time_mono_us() + 2000000ull;
  while (pm_metal_time_mono_us() < deadline) {
    if (pm_metal_virtq_get_used(&mDev.vqs[VSND_CTRL], &head, &len)) {
      pm_metal_virtq_free_chain(&mDev.vqs[VSND_CTRL], head);
      memcpy(resp, mCtrlResp, resp_len);
      (void)len;
      return 0;
    }

    /*
     * No session pinning — safe to drain every runner. task.c's per-task
     * busy guard makes a re-entrant step (double-pump during open) a
     * harmless no-op rather than a race.
     */
    pm_metal_run_poll_all();
    pm_metal_cpu_pause();
  }

  return -1;
}

static void VsndPollTx(void)
{
  uint16_t head;
  uint32_t len;
  uint32_t i;

  if (!mReady) {
    return;
  }

  while (pm_metal_virtq_get_used(&mDev.vqs[VSND_TX], &head, &len)) {
    /* Match buffer by descriptor addr */
    {
      typedef struct {
        uint64_t Addr;
        uint32_t Len;
        uint16_t Flags;
        uint16_t Next;
      } desc_t;

      desc_t  *d;
      uint8_t *addr;

      d    = (desc_t *)mDev.vqs[VSND_TX].desc;
      addr = (uint8_t *)(uintptr_t)d[head].Addr;
      for (i = 0; i < VSND_TX_BUFS; i++) {
        if (mTxBufs[i] == addr) {
          if (mStreams[1].used && mTxLens[i] > sizeof(vsnd_pcm_xfer_t)) {
            mStreams[1].consumed += mTxLens[i] - sizeof(vsnd_pcm_xfer_t);
          }

          mTxFree |= (1u << i);
          mTxLens[i] = 0;
          break;
        }
      }
    }

    pm_metal_virtq_free_chain(&mDev.vqs[VSND_TX], head);
    (void)len;
  }
}

static int VsndInit(void)
{
  uint64_t              feats;
  vsnd_pcm_set_params_t sp;
  vsnd_pcm_hdr_t        ph;
  vsnd_hdr_t            resp;
  uint32_t              i;

  if (mReady) {
    return 0;
  }

  if (pm_metal_virtio_open(PM_METAL_VIRTIO_DEV_SOUND, &mDev) != 0) {
    return -1;
  }

  feats = pm_metal_virtio_get_features(&mDev);
  feats &= PM_METAL_VIRTIO_F_VERSION_1;
  if (pm_metal_virtio_set_features(&mDev, feats) != 0) {
    pm_metal_virtio_close(&mDev);
    return -1;
  }

  if (pm_metal_virtio_setup_queue(&mDev, VSND_CTRL, VSND_QSZ) != 0 ||
      pm_metal_virtio_setup_queue(&mDev, VSND_EVENT, VSND_QSZ) != 0 ||
      pm_metal_virtio_setup_queue(&mDev, VSND_TX, VSND_QSZ) != 0) {
    pm_metal_virtio_close(&mDev);
    return -1;
  }

  (void)pm_metal_virtio_driver_ok(&mDev);

  memset(&sp, 0, sizeof(sp));
  sp.Hdr.Code    = VIRTIO_SND_R_PCM_SET_PARAMS;
  sp.StreamId    = 0;
  sp.BufferBytes = VSND_PERIOD * 4u;
  sp.PeriodBytes = VSND_PERIOD;
  sp.Features    = 0;
  sp.Channels    = 2;
  sp.Format      = VIRTIO_SND_PCM_FMT_S16;
  sp.Rate        = VIRTIO_SND_PCM_RATE_22050;
  memset(&resp, 0, sizeof(resp));
  if (VsndCtrl(&sp, sizeof(sp), &resp, sizeof(resp)) != 0 || resp.Code != VIRTIO_SND_S_OK) {
    pm_metal_virtio_close(&mDev);
    return -1;
  }

  memset(&ph, 0, sizeof(ph));
  ph.Hdr.Code = VIRTIO_SND_R_PCM_PREPARE;
  ph.StreamId = 0;
  memset(&resp, 0, sizeof(resp));
  if (VsndCtrl(&ph, sizeof(ph), &resp, sizeof(resp)) != 0 || resp.Code != VIRTIO_SND_S_OK) {
    pm_metal_virtio_close(&mDev);
    return -1;
  }

  memset(&ph, 0, sizeof(ph));
  ph.Hdr.Code = VIRTIO_SND_R_PCM_START;
  ph.StreamId = 0;
  memset(&resp, 0, sizeof(resp));
  if (VsndCtrl(&ph, sizeof(ph), &resp, sizeof(resp)) != 0 || resp.Code != VIRTIO_SND_S_OK) {
    pm_metal_virtio_close(&mDev);
    return -1;
  }

  mTxFree = (1u << VSND_TX_BUFS) - 1u;
  for (i = 0; i < VSND_TX_BUFS; i++) {
    mTxBufs[i] = pm_metal_virtio_pages_alloc(
      PM_METAL_VIRTIO_SIZE_TO_PAGES(sizeof(vsnd_pcm_xfer_t) + VSND_PERIOD));
    if (mTxBufs[i] == NULL) {
      pm_metal_virtio_close(&mDev);
      return -1;
    }
  }

  mStarted = 1;
  mReady   = 1;

  return 0;
}

static void VsndPoll(void)
{
  VsndPollTx();
}

static int32_t VsndReady(void)
{
  return mReady ? 1 : 0;
}

static pm_metal_audio_stream_h VsndOpen(uint32_t format, uint32_t frames)
{
  (void)frames;
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

static void VsndClose(pm_metal_audio_stream_h s)
{
  if (s != 1) {
    return;
  }

  memset(&mStreams[1], 0, sizeof(mStreams[1]));
}

static uint32_t VsndQueue(pm_metal_audio_stream_h s, const void *pcm, uint32_t nbytes)
{
  uint32_t i;
  uint32_t n;
  uint32_t placed;

  if (s != 1 || !mStreams[1].used || pcm == NULL || nbytes == 0 || mMuted) {
    return 0;
  }

  placed = 0;
  while (placed < nbytes) {
    VsndPollTx();
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
      vsnd_pcm_xfer_t *x;

      x           = (vsnd_pcm_xfer_t *)mTxBufs[i];
      x->StreamId = 0;
      memcpy(mTxBufs[i] + sizeof(*x), (const uint8_t *)pcm + placed, n);
      mTxLens[i] = sizeof(*x) + n;
      if (pm_metal_virtq_add(&mDev.vqs[VSND_TX], mTxBufs[i], mTxLens[i], 0, NULL) != 0) {
        break;
      }

      mTxFree &= ~(1u << i);
      placed += n;
      mStreams[1].queued += n;
    }
  }

  if (placed > 0) {
    pm_metal_virtq_kick(&mDev, &mDev.vqs[VSND_TX]);
  }

  return placed;
}

typedef struct {
  pm_metal_audio_stream_h s;
  uint32_t                need;
  uint64_t                deadline;
} vsnd_drain_t;

static pm_metal_status_t VsndDrainStep(pm_metal_async_handle_t self_h)
{
  vsnd_drain_t *c;

  c = (vsnd_drain_t *)(uintptr_t)pm_metal_async_coro_state(self_h);
  if (c == NULL) {
    return PM_METAL_ERROR;
  }

  VsndPollTx();
  if (c->s == 1 && mStreams[1].used && mStreams[1].consumed >= c->need) {
    return PM_METAL_DONE;
  }

  if (pm_metal_time_mono_us() > c->deadline) {
    return PM_METAL_ERROR;
  }

  return pm_metal_async_await(self_h, pm_metal_async_sleep_us(2000));
}

static pm_metal_async_handle_t VsndDrain(pm_metal_audio_stream_h s, uint32_t nbytes)
{
  vsnd_drain_t           *c;
  pm_metal_async_handle_t h;

  if (s != 1 || !mStreams[1].used) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  h = pm_metal_async_coro_create(VsndDrainStep, sizeof(*c));
  if (h == PM_METAL_ASYNC_HANDLE_INVALID) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  c = (vsnd_drain_t *)(uintptr_t)pm_metal_async_coro_state(h);
  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  c->s        = s;
  c->need     = mStreams[1].consumed + nbytes;
  c->deadline = pm_metal_time_mono_us() + 10000000ull;
  return h;
}

static void VsndMute(int on)
{
  vsnd_pcm_hdr_t ph;
  vsnd_hdr_t     resp;

  mMuted = on ? 1 : 0;
  if (!mReady) {
    return;
  }

  memset(&ph, 0, sizeof(ph));
  ph.Hdr.Code = on ? VIRTIO_SND_R_PCM_STOP : VIRTIO_SND_R_PCM_START;
  ph.StreamId = 0;
  memset(&resp, 0, sizeof(resp));
  (void)VsndCtrl(&ph, sizeof(ph), &resp, sizeof(resp));
  mStarted = on ? 0 : 1;
}

static const pm_metal_audio_ops_t mVirtioOps = { "virtio-snd", VsndInit,  VsndPoll,
                                                 VsndReady,    VsndOpen,  VsndClose,
                                                 VsndQueue,    VsndDrain, VsndMute };

int pm_metal_audio_virtio_probe(void)
{
  if (VsndInit() != 0) {
    return -1;
  }

  pm_metal_audio_set_ops(&mVirtioOps);
  {
    static pm_metal_io_node_t Node = {
      .class = PM_METAL_IO_AUDIO, .compat = "virtio-snd", .caps = 1, .bus = PM_METAL_IO_BUS_PCI
    };

    (void)pm_metal_io_dt_add(&Node);
  }
  return 0;
}
