/** @file
  ICH AC'97 PCM output (S16LE stereo 22050). Bare-metal / QEMU -device AC97.
  (impl: efi|bios)
**/
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/dev/audio/audio_ops.h>
#include <pymergetic/metal/bus/virtio/virtio.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/bus/io/io.h>
#include <pymergetic/metal/log/log.h>
#include <runtime/time/time.h>

#include <runtime/io/io.h>

#include "../../bus/pci/pci.h"

#define AC97_CLASS    0x04u
#define AC97_SUBCLASS 0x01u

#define AC97_NBUF   16u
#define AC97_PERIOD 2048u /* bytes; S16 stereo → 512 frames */

/* Mixer (NAMBAR) */
#define AC97_RESET      0x00u
#define AC97_MASTER_VOL 0x02u
#define AC97_PCM_VOL    0x18u
#define AC97_EXT_ID     0x2Au
#define AC97_EXT_CTRL   0x2Cu
#define AC97_PCM_FRONT  0x2Eu

/* NABM PCM Out */
#define AC97_PO_BDBAR 0x10u
#define AC97_PO_CIV   0x14u
#define AC97_PO_LVI   0x15u
#define AC97_PO_SR    0x16u
#define AC97_PO_CR    0x1Bu
#define AC97_GLB_CTRL 0x2Cu
#define AC97_GLB_STA  0x30u

#define AC97_CR_RPBM 0x01u
#define AC97_CR_RR   0x02u
#define AC97_CR_IOCE 0x10u

#define AC97_SR_DCH   0x01u
#define AC97_SR_BCIS  0x08u
#define AC97_SR_LVBCI 0x04u
#define AC97_SR_FIFOE 0x10u

#define AC97_GS_PCR  0x100u
#define AC97_GC_COLD 0x02u

#define AC97_BD_IOC 0x80000000u

typedef struct {
  int32_t  used;
  uint32_t format;
  uint32_t queued;
  uint32_t consumed;
} ac97_stream_t;

typedef struct {
  pm_metal_audio_stream_h s;
  uint32_t                need;
  uint64_t                deadline;
} ac97_drain_t;

static uint16_t      mNam;
static uint16_t      mNabm;
static int32_t       mReady;
static int32_t       mMuted;
static int32_t       mRunning;
static ac97_stream_t mStreams[4];
static uint8_t      *mPeriods[AC97_NBUF];
static uint32_t     *mBdl; /* AC97_NBUF * 2 dwords */
static uint32_t      mWriteIdx;
static uint8_t       mLastCiv;
static uint32_t      mPeriodBytesPlayed; /* partial period tracking via CIV */

static void Ac97StallUs(uint32_t us)
{
  pm_metal_time_usleep(us);
}

static void Ac97AckStatus(void)
{
  uint16_t sr;

  sr = pm_metal_io_in16(mNabm + AC97_PO_SR);
  if (sr != 0) {
    pm_metal_io_out16(mNabm + AC97_PO_SR,
                      (uint16_t)(sr & (AC97_SR_BCIS | AC97_SR_LVBCI | AC97_SR_FIFOE)));
  }
}

static void Ac97PollCiv(void)
{
  uint8_t  civ;
  uint8_t  advanced;
  uint32_t i;

  if (!mReady) {
    return;
  }

  Ac97AckStatus();
  civ = pm_metal_io_in8(mNabm + AC97_PO_CIV);
  if (civ == mLastCiv) {
    return;
  }

  advanced = (uint8_t)((civ - mLastCiv) & (AC97_NBUF - 1u));
  if (advanced == 0) {
    /* wrapped full ring — treat as NBUF-1 max progress this poll */
    advanced = (uint8_t)(AC97_NBUF - 1u);
  }

  for (i = 0; i < advanced; i++) {
    if (mStreams[1].used) {
      mStreams[1].consumed += AC97_PERIOD;
    }

    /* silence the played slot so underrun stays quiet */
    {
      uint8_t idx;

      idx = (uint8_t)((mLastCiv + i) & (AC97_NBUF - 1u));
      memset(mPeriods[idx], 0, AC97_PERIOD);
    }
  }

  mLastCiv = civ;
  (void)mPeriodBytesPlayed;
}

static int32_t Ac97ProbeHw(void)
{
  uint8_t  bus;
  uint8_t  dev;
  uint8_t  func;
  uint16_t gs;
  uint32_t i;
  uint32_t spins;

  if (pm_bios_pci_find_class(AC97_CLASS, AC97_SUBCLASS, &bus, &dev, &func) != 0) {
    /* QEMU / ICH common ID as fallback */
    if (pm_bios_pci_find(0x8086, 0x2415, &bus, &dev, &func) != 0) {
      return -1;
    }
  }

  pm_bios_pci_enable_io_bm(bus, dev, func);
  mNam  = pm_bios_pci_bar_io(bus, dev, func, 0);
  mNabm = pm_bios_pci_bar_io(bus, dev, func, 1);
  if (mNam == 0 || mNabm == 0) {
    return -1;
  }

  /* Cold reset */
  pm_metal_io_out32(mNabm + AC97_GLB_CTRL, AC97_GC_COLD);
  Ac97StallUs(1000);
  pm_metal_io_out32(mNabm + AC97_GLB_CTRL, 0);
  Ac97StallUs(1000);

  spins = 0;
  do {
    gs = pm_metal_io_in16(mNabm + AC97_GLB_STA);
    if ((gs & AC97_GS_PCR) != 0) {
      break;
    }

    Ac97StallUs(100);
  } while (++spins < 10000u);

  if ((gs & AC97_GS_PCR) == 0) {
    return -1;
  }

  pm_metal_io_out16(mNam + AC97_RESET, 0);
  Ac97StallUs(1000);
  pm_metal_io_out16(mNam + AC97_MASTER_VOL, 0x0000); /* 0 dB, unmuted */
  pm_metal_io_out16(mNam + AC97_PCM_VOL, 0x0000);

  /* Variable rate → 22050 when supported */
  if ((pm_metal_io_in16(mNam + AC97_EXT_ID) & 0x1u) != 0) {
    pm_metal_io_out16(mNam + AC97_EXT_CTRL,
                      (uint16_t)(pm_metal_io_in16(mNam + AC97_EXT_CTRL) | 0x1u));
    pm_metal_io_out16(mNam + AC97_PCM_FRONT, 22050);
    Ac97StallUs(100);
    if (pm_metal_io_in16(mNam + AC97_PCM_FRONT) != 22050) {
      pm_metal_log("metal-audio: ac97 VRA 22050 rejected");
      return -1;
    }
  } else {
    pm_metal_log("metal-audio: ac97 no VRA");
    return -1;
  }

  mBdl = (uint32_t *)pm_metal_virtio_pages_alloc(PM_METAL_VIRTIO_SIZE_TO_PAGES(AC97_NBUF * 8u));
  if (mBdl == NULL) {
    return -1;
  }

  memset(mBdl, 0, AC97_NBUF * 8u);
  for (i = 0; i < AC97_NBUF; i++) {
    mPeriods[i] =
      (uint8_t *)pm_metal_virtio_pages_alloc(PM_METAL_VIRTIO_SIZE_TO_PAGES(AC97_PERIOD));
    if (mPeriods[i] == NULL) {
      return -1;
    }

    memset(mPeriods[i], 0, AC97_PERIOD);
    mBdl[i * 2u] = (uint32_t)(uintptr_t)mPeriods[i];
    /* length in 16-bit samples, minus one; IOC each period */
    mBdl[i * 2u + 1u] = AC97_BD_IOC | ((AC97_PERIOD / 2u) - 1u);
  }

  /* Reset PCM Out engine */
  pm_metal_io_out8(mNabm + AC97_PO_CR, AC97_CR_RR);
  Ac97StallUs(100);
  pm_metal_io_out8(mNabm + AC97_PO_CR, 0);
  Ac97StallUs(100);

  pm_metal_io_out32(mNabm + AC97_PO_BDBAR, (uint32_t)(uintptr_t)mBdl);
  pm_metal_io_out8(mNabm + AC97_PO_LVI, (uint8_t)(AC97_NBUF - 1u));
  Ac97AckStatus();
  mLastCiv  = pm_metal_io_in8(mNabm + AC97_PO_CIV);
  mWriteIdx = (uint32_t)((mLastCiv + 1u) & (AC97_NBUF - 1u));
  mRunning  = 0;

  {
    char msg[72];

    snprintf(msg, sizeof(msg), "metal-audio: ac97 nam=0x%x nabm=0x%x", mNam, mNabm);
    pm_metal_log(msg);
  }

  return 0;
}

static void Ac97EnsureRun(void)
{
  if (mRunning || mMuted) {
    return;
  }

  pm_metal_io_out8(mNabm + AC97_PO_LVI, (uint8_t)(AC97_NBUF - 1u));
  pm_metal_io_out8(mNabm + AC97_PO_CR, (uint8_t)(AC97_CR_RPBM | AC97_CR_IOCE));
  mRunning = 1;
}

static int Ac97Init(void)
{
  if (mReady) {
    return 0;
  }

  if (Ac97ProbeHw() != 0) {
    return -1;
  }

  mReady = 1;
  return 0;
}

static void Ac97Poll(void)
{
  Ac97PollCiv();
}

static int32_t Ac97Ready(void)
{
  return mReady ? 1 : 0;
}

static pm_metal_audio_stream_h Ac97Open(uint32_t format, uint32_t frames)
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

static void Ac97Close(pm_metal_audio_stream_h s)
{
  if (s != 1) {
    return;
  }

  memset(&mStreams[1], 0, sizeof(mStreams[1]));
}

static uint32_t Ac97Queue(pm_metal_audio_stream_h s, const void *pcm, uint32_t nbytes)
{
  const uint8_t *src;
  uint32_t       left;
  uint32_t       accepted;

  if (s != 1 || !mStreams[1].used || pcm == NULL || nbytes == 0 || mMuted) {
    return 0;
  }

  Ac97PollCiv();
  src      = (const uint8_t *)pcm;
  left     = nbytes;
  accepted = 0;

  while (left > 0) {
    uint8_t  civ;
    uint32_t ahead;
    uint32_t chunk;

    civ   = pm_metal_io_in8(mNabm + AC97_PO_CIV);
    ahead = (mWriteIdx - (uint32_t)civ) & (AC97_NBUF - 1u);
    /* keep ≥2 periods of headroom so DMA never hits the write cursor */
    if (ahead >= AC97_NBUF - 2u) {
      break;
    }

    chunk = left;
    if (chunk > AC97_PERIOD) {
      chunk = AC97_PERIOD;
    }

    memcpy(mPeriods[mWriteIdx], src, chunk);
    if (chunk < AC97_PERIOD) {
      memset(mPeriods[mWriteIdx] + chunk, 0, AC97_PERIOD - chunk);
    }

    mWriteIdx = (mWriteIdx + 1u) & (AC97_NBUF - 1u);
    src += chunk;
    left -= chunk;
    accepted += chunk;
  }

  if (accepted > 0) {
    mStreams[1].queued += accepted;
    pm_metal_io_out8(mNabm + AC97_PO_LVI, (uint8_t)(AC97_NBUF - 1u));
    Ac97EnsureRun();
  }

  return accepted;
}

static pm_metal_status_t Ac97DrainStep(pm_metal_async_handle_t self_h)
{
  ac97_drain_t *c;

  c = (ac97_drain_t *)(uintptr_t)pm_metal_async_coro_state(self_h);
  if (c == NULL) {
    return PM_METAL_ERROR;
  }

  Ac97PollCiv();
  if (c->s == 1 && mStreams[1].used && mStreams[1].consumed >= c->need) {
    return PM_METAL_DONE;
  }

  if (pm_metal_time_mono_us() > c->deadline) {
    return PM_METAL_ERROR;
  }

  return pm_metal_async_await(self_h, pm_metal_async_sleep_us(2000));
}

static pm_metal_async_handle_t Ac97Drain(pm_metal_audio_stream_h s, uint32_t nbytes)
{
  ac97_drain_t           *c;
  pm_metal_async_handle_t h;

  if (s != 1 || !mStreams[1].used) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  h = pm_metal_async_coro_create(Ac97DrainStep, sizeof(*c));
  if (h == PM_METAL_ASYNC_HANDLE_INVALID) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  c = (ac97_drain_t *)(uintptr_t)pm_metal_async_coro_state(h);
  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  c->s        = s;
  c->need     = mStreams[1].consumed + nbytes;
  c->deadline = pm_metal_time_mono_us() + 10000000ull;
  return h;
}

static void Ac97Mute(int on)
{
  mMuted = on ? 1 : 0;
  if (!mReady) {
    return;
  }

  if (on) {
    pm_metal_io_out8(mNabm + AC97_PO_CR, 0);
    mRunning = 0;
    pm_metal_io_out16(mNam + AC97_MASTER_VOL, 0x8000);
  } else {
    pm_metal_io_out16(mNam + AC97_MASTER_VOL, 0x0000);
    Ac97EnsureRun();
  }
}

static const pm_metal_audio_ops_t mAc97Ops = { "ac97",    Ac97Init,  Ac97Poll,  Ac97Ready, Ac97Open,
                                               Ac97Close, Ac97Queue, Ac97Drain, Ac97Mute };

int pm_metal_audio_ac97_probe(void)
{
  if (Ac97Init() != 0) {
    return -1;
  }

  pm_metal_audio_set_ops(&mAc97Ops);
  {
    static pm_metal_io_node_t Node = {
      .class = PM_METAL_IO_AUDIO, .compat = "ac97", .caps = 1, .bus = PM_METAL_IO_BUS_PCI
    };

    (void)pm_metal_io_dt_add(&Node);
  }
  return 0;
}
