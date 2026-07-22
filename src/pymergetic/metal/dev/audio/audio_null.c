/** @file
  Null audio backend — discard + eager drain. (impl: efi|bios)
**/
#include <pymergetic/metal/dev/audio/audio_ops.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <runtime/coro/coro.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiLib.h>

#ifndef PM_METAL_AUDIO_MAX_STREAMS
#define PM_METAL_AUDIO_MAX_STREAMS  8u
#endif

typedef struct {
  INT32   used;
  UINT32  format;
  UINT32  frames;
} null_stream_t;

STATIC null_stream_t  mStreams[PM_METAL_AUDIO_MAX_STREAMS + 1];
STATIC INT32          mLogged;
STATIC INT32          mMuted;

STATIC
pm_metal_status_t
NullDrainFn (
  pm_metal_coro_t  *self
  )
{
  (VOID)self;
  return PM_METAL_DONE;
}

STATIC
int
NullInit (
  VOID
  )
{
  if (!mLogged) {
    Print (L"metal-audio: null\r\n");
    mLogged = 1;
  }

  return 0;
}

STATIC
VOID
NullPoll (
  VOID
  )
{
}

STATIC
int32_t
NullReady (
  VOID
  )
{
  return 0;
}

STATIC
pm_metal_audio_stream_h
NullOpen (
  uint32_t  format,
  uint32_t  frames
  )
{
  UINT32  i;

  if (format == 0) {
    return PM_METAL_AUDIO_STREAM_INVALID;
  }

  for (i = 1; i <= PM_METAL_AUDIO_MAX_STREAMS; i++) {
    if (!mStreams[i].used) {
      mStreams[i].used   = 1;
      mStreams[i].format = format;
      mStreams[i].frames = frames;
      return (pm_metal_audio_stream_h)i;
    }
  }

  return PM_METAL_AUDIO_STREAM_INVALID;
}

STATIC
VOID
NullClose (
  pm_metal_audio_stream_h  s
  )
{
  if (s == 0 || s > PM_METAL_AUDIO_MAX_STREAMS) {
    return;
  }

  ZeroMem (&mStreams[s], sizeof (mStreams[s]));
}

STATIC
uint32_t
NullQueue (
  pm_metal_audio_stream_h  s,
  CONST VOID              *pcm,
  uint32_t                 nbytes
  )
{
  (VOID)pcm;
  if (s == 0 || !mStreams[s].used || mMuted) {
    return 0;
  }

  return nbytes;
}

STATIC
pm_metal_async_handle_t
NullDrain (
  pm_metal_audio_stream_h  s,
  uint32_t                 nbytes
  )
{
  pm_metal_coro_t  *c;

  (VOID)nbytes;
  if (s == 0 || !mStreams[s].used) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  c = pm_metal_coro (NullDrainFn, sizeof (*c));
  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_async_adopt_host_coro (c);
}

STATIC
VOID
NullMute (
  int  on
  )
{
  mMuted = on ? 1 : 0;
}

STATIC CONST pm_metal_audio_ops_t  mNullOps = {
  "null",
  NullInit,
  NullPoll,
  NullReady,
  NullOpen,
  NullClose,
  NullQueue,
  NullDrain,
  NullMute
};

void
pm_metal_audio_null_install (
  VOID
  )
{
  pm_metal_audio_set_ops (&mNullOps);
  (VOID)NullInit ();
}
