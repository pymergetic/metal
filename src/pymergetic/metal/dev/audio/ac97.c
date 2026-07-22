/** @file
  ICH AC'97 PCM output (S16LE stereo 22050). Bare-metal / QEMU -device AC97.
  (impl: efi|bios)
**/
#include <pymergetic/metal/dev/audio/audio_ops.h>
#include <pymergetic/metal/bus/virtio/virtio.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/bus/io/io.h>
#include <pymergetic/metal/log/log.h>
#include <runtime/coro/coro.h>
#include <runtime/time/time.h>

#include "../../bus/pci/pci.h"

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define AC97_CLASS        0x04u
#define AC97_SUBCLASS     0x01u

#define AC97_NBUF         16u
#define AC97_PERIOD       2048u /* bytes; S16 stereo → 512 frames */

/* Mixer (NAMBAR) */
#define AC97_RESET        0x00u
#define AC97_MASTER_VOL   0x02u
#define AC97_PCM_VOL      0x18u
#define AC97_EXT_ID       0x2Au
#define AC97_EXT_CTRL     0x2Cu
#define AC97_PCM_FRONT    0x2Eu

/* NABM PCM Out */
#define AC97_PO_BDBAR     0x10u
#define AC97_PO_CIV       0x14u
#define AC97_PO_LVI       0x15u
#define AC97_PO_SR        0x16u
#define AC97_PO_CR        0x1Bu
#define AC97_GLB_CTRL     0x2Cu
#define AC97_GLB_STA      0x30u

#define AC97_CR_RPBM      0x01u
#define AC97_CR_RR        0x02u
#define AC97_CR_IOCE      0x10u

#define AC97_SR_DCH       0x01u
#define AC97_SR_BCIS      0x08u
#define AC97_SR_LVBCI     0x04u
#define AC97_SR_FIFOE     0x10u

#define AC97_GS_PCR       0x100u
#define AC97_GC_COLD      0x02u

#define AC97_BD_IOC       0x80000000u

typedef struct {
  INT32   used;
  UINT32  format;
  UINT32  queued;
  UINT32  consumed;
} ac97_stream_t;

typedef struct {
  pm_metal_coro_t  coro;
  pm_metal_audio_stream_h  s;
  UINT32           need;
  UINT64           deadline;
} ac97_drain_t;

STATIC UINT16         mNam;
STATIC UINT16         mNabm;
STATIC INT32          mReady;
STATIC INT32          mMuted;
STATIC INT32          mRunning;
STATIC ac97_stream_t  mStreams[4];
STATIC UINT8         *mPeriods[AC97_NBUF];
STATIC UINT32        *mBdl; /* AC97_NBUF * 2 dwords */
STATIC UINT32         mWriteIdx;
STATIC UINT8          mLastCiv;
STATIC UINT32         mPeriodBytesPlayed; /* partial period tracking via CIV */

STATIC
VOID
Ac97StallUs (
  UINT32  us
  )
{
  if (gBS != NULL) {
    gBS->Stall (us);
  } else {
    UINT64  t;

    t = pm_metal_time_mono_us () + (UINT64)us;
    while (pm_metal_time_mono_us () < t) {
    }
  }
}

STATIC
VOID
Ac97AckStatus (
  VOID
  )
{
  UINT16  sr;

  sr = IoRead16 (mNabm + AC97_PO_SR);
  if (sr != 0) {
    IoWrite16 (mNabm + AC97_PO_SR, (UINT16)(sr & (AC97_SR_BCIS | AC97_SR_LVBCI | AC97_SR_FIFOE)));
  }
}

STATIC
VOID
Ac97PollCiv (
  VOID
  )
{
  UINT8   civ;
  UINT8   advanced;
  UINT32  i;

  if (!mReady) {
    return;
  }

  Ac97AckStatus ();
  civ = IoRead8 (mNabm + AC97_PO_CIV);
  if (civ == mLastCiv) {
    return;
  }

  advanced = (UINT8)((civ - mLastCiv) & (AC97_NBUF - 1u));
  if (advanced == 0) {
    /* wrapped full ring — treat as NBUF-1 max progress this poll */
    advanced = (UINT8)(AC97_NBUF - 1u);
  }

  for (i = 0; i < advanced; i++) {
    if (mStreams[1].used) {
      mStreams[1].consumed += AC97_PERIOD;
    }

    /* silence the played slot so underrun stays quiet */
    {
      UINT8  idx;

      idx = (UINT8)((mLastCiv + i) & (AC97_NBUF - 1u));
      ZeroMem (mPeriods[idx], AC97_PERIOD);
    }
  }

  mLastCiv = civ;
  (VOID)mPeriodBytesPlayed;
}

STATIC
INT32
Ac97ProbeHw (
  VOID
  )
{
  UINT8   bus;
  UINT8   dev;
  UINT8   func;
  UINT16  gs;
  UINT32  i;
  UINT32  spins;

  if (pm_bios_pci_find_class (AC97_CLASS, AC97_SUBCLASS, &bus, &dev, &func) != 0) {
    /* QEMU / ICH common ID as fallback */
    if (pm_bios_pci_find (0x8086, 0x2415, &bus, &dev, &func) != 0) {
      return -1;
    }
  }

  pm_bios_pci_enable_io_bm (bus, dev, func);
  mNam  = pm_bios_pci_bar_io (bus, dev, func, 0);
  mNabm = pm_bios_pci_bar_io (bus, dev, func, 1);
  if (mNam == 0 || mNabm == 0) {
    return -1;
  }

  /* Cold reset */
  IoWrite32 (mNabm + AC97_GLB_CTRL, AC97_GC_COLD);
  Ac97StallUs (1000);
  IoWrite32 (mNabm + AC97_GLB_CTRL, 0);
  Ac97StallUs (1000);

  spins = 0;
  do {
    gs = IoRead16 (mNabm + AC97_GLB_STA);
    if ((gs & AC97_GS_PCR) != 0) {
      break;
    }

    Ac97StallUs (100);
  } while (++spins < 10000u);

  if ((gs & AC97_GS_PCR) == 0) {
    return -1;
  }

  IoWrite16 (mNam + AC97_RESET, 0);
  Ac97StallUs (1000);
  IoWrite16 (mNam + AC97_MASTER_VOL, 0x0000); /* 0 dB, unmuted */
  IoWrite16 (mNam + AC97_PCM_VOL, 0x0000);

  /* Variable rate → 22050 when supported */
  if ((IoRead16 (mNam + AC97_EXT_ID) & 0x1u) != 0) {
    IoWrite16 (mNam + AC97_EXT_CTRL, (UINT16)(IoRead16 (mNam + AC97_EXT_CTRL) | 0x1u));
    IoWrite16 (mNam + AC97_PCM_FRONT, 22050);
    Ac97StallUs (100);
    if (IoRead16 (mNam + AC97_PCM_FRONT) != 22050) {
      pm_metal_log ("metal-audio: ac97 VRA 22050 rejected");
      return -1;
    }
  } else {
    pm_metal_log ("metal-audio: ac97 no VRA");
    return -1;
  }

  mBdl = (UINT32 *)pm_metal_virtio_pages_alloc (
                     EFI_SIZE_TO_PAGES (AC97_NBUF * 8u)
                     );
  if (mBdl == NULL) {
    return -1;
  }

  ZeroMem (mBdl, AC97_NBUF * 8u);
  for (i = 0; i < AC97_NBUF; i++) {
    mPeriods[i] = (UINT8 *)pm_metal_virtio_pages_alloc (
                              EFI_SIZE_TO_PAGES (AC97_PERIOD)
                              );
    if (mPeriods[i] == NULL) {
      return -1;
    }

    ZeroMem (mPeriods[i], AC97_PERIOD);
    mBdl[i * 2u]     = (UINT32)(UINTN)mPeriods[i];
    /* length in 16-bit samples, minus one; IOC each period */
    mBdl[i * 2u + 1u] = AC97_BD_IOC | ((AC97_PERIOD / 2u) - 1u);
  }

  /* Reset PCM Out engine */
  IoWrite8 (mNabm + AC97_PO_CR, AC97_CR_RR);
  Ac97StallUs (100);
  IoWrite8 (mNabm + AC97_PO_CR, 0);
  Ac97StallUs (100);

  IoWrite32 (mNabm + AC97_PO_BDBAR, (UINT32)(UINTN)mBdl);
  IoWrite8 (mNabm + AC97_PO_LVI, (UINT8)(AC97_NBUF - 1u));
  Ac97AckStatus ();
  mLastCiv  = IoRead8 (mNabm + AC97_PO_CIV);
  mWriteIdx = (UINT32)((mLastCiv + 1u) & (AC97_NBUF - 1u));
  mRunning  = 0;

  {
    CHAR8  msg[72];

    AsciiSPrint (
      msg,
      sizeof (msg),
      "metal-audio: ac97 nam=0x%x nabm=0x%x",
      mNam,
      mNabm
      );
    pm_metal_log (msg);
  }

  return 0;
}

STATIC
VOID
Ac97EnsureRun (
  VOID
  )
{
  if (mRunning || mMuted) {
    return;
  }

  IoWrite8 (mNabm + AC97_PO_LVI, (UINT8)(AC97_NBUF - 1u));
  IoWrite8 (mNabm + AC97_PO_CR, (UINT8)(AC97_CR_RPBM | AC97_CR_IOCE));
  mRunning = 1;
}

STATIC
int
Ac97Init (
  VOID
  )
{
  if (mReady) {
    return 0;
  }

  if (Ac97ProbeHw () != 0) {
    return -1;
  }

  mReady = 1;
  return 0;
}

STATIC
VOID
Ac97Poll (
  VOID
  )
{
  Ac97PollCiv ();
}

STATIC
int32_t
Ac97Ready (
  VOID
  )
{
  return mReady ? 1 : 0;
}

STATIC
pm_metal_audio_stream_h
Ac97Open (
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
Ac97Close (
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
Ac97Queue (
  pm_metal_audio_stream_h  s,
  CONST VOID              *pcm,
  uint32_t                 nbytes
  )
{
  CONST UINT8  *src;
  UINT32        left;
  UINT32        accepted;

  if (s != 1 || !mStreams[1].used || pcm == NULL || nbytes == 0 || mMuted) {
    return 0;
  }

  Ac97PollCiv ();
  src      = (CONST UINT8 *)pcm;
  left     = nbytes;
  accepted = 0;

  while (left > 0) {
    UINT8   civ;
    UINT32  ahead;
    UINT32  chunk;

    civ = IoRead8 (mNabm + AC97_PO_CIV);
    ahead = (mWriteIdx - (UINT32)civ) & (AC97_NBUF - 1u);
    /* keep ≥2 periods of headroom so DMA never hits the write cursor */
    if (ahead >= AC97_NBUF - 2u) {
      break;
    }

    chunk = left;
    if (chunk > AC97_PERIOD) {
      chunk = AC97_PERIOD;
    }

    CopyMem (mPeriods[mWriteIdx], src, chunk);
    if (chunk < AC97_PERIOD) {
      ZeroMem (mPeriods[mWriteIdx] + chunk, AC97_PERIOD - chunk);
    }

    mWriteIdx = (mWriteIdx + 1u) & (AC97_NBUF - 1u);
    src      += chunk;
    left     -= chunk;
    accepted += chunk;
  }

  if (accepted > 0) {
    mStreams[1].queued += accepted;
    IoWrite8 (mNabm + AC97_PO_LVI, (UINT8)(AC97_NBUF - 1u));
    Ac97EnsureRun ();
  }

  return accepted;
}

STATIC
pm_metal_status_t
Ac97DrainFn (
  pm_metal_coro_t  *self
  )
{
  ac97_drain_t  *c;

  c = (ac97_drain_t *)self;
  Ac97PollCiv ();
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
Ac97Drain (
  pm_metal_audio_stream_h  s,
  uint32_t                 nbytes
  )
{
  ac97_drain_t  *c;

  if (s != 1 || !mStreams[1].used) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  c = (ac97_drain_t *)pm_metal_coro (Ac97DrainFn, sizeof (*c));
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
Ac97Mute (
  int  on
  )
{
  mMuted = on ? 1 : 0;
  if (!mReady) {
    return;
  }

  if (on) {
    IoWrite8 (mNabm + AC97_PO_CR, 0);
    mRunning = 0;
    IoWrite16 (mNam + AC97_MASTER_VOL, 0x8000);
  } else {
    IoWrite16 (mNam + AC97_MASTER_VOL, 0x0000);
    Ac97EnsureRun ();
  }
}

STATIC CONST pm_metal_audio_ops_t  mAc97Ops = {
  "ac97",
  Ac97Init,
  Ac97Poll,
  Ac97Ready,
  Ac97Open,
  Ac97Close,
  Ac97Queue,
  Ac97Drain,
  Ac97Mute
};

int
pm_metal_audio_ac97_probe (
  VOID
  )
{
  if (Ac97Init () != 0) {
    return -1;
  }

  pm_metal_audio_set_ops (&mAc97Ops);
  {
    STATIC pm_metal_io_node_t  Node = {
      .class = PM_METAL_IO_AUDIO,
      .compat = "ac97",
      .caps = 1,
      .bus = PM_METAL_IO_BUS_PCI
    };

    (VOID)pm_metal_io_dt_add (&Node);
  }
  return 0;
}
