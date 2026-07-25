/** @file
  Null audio backend — discard + eager drain. (impl: efi|bios)
**/
#include <pymergetic/metal/dev/audio/audio_ops.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/log/log.h>

#include <stdint.h>
#include <string.h>

#ifndef PM_METAL_AUDIO_MAX_STREAMS
#define PM_METAL_AUDIO_MAX_STREAMS 8u
#endif

typedef struct {
  int32_t  used;
  uint32_t format;
  uint32_t frames;
} null_stream_t;

static null_stream_t mStreams[PM_METAL_AUDIO_MAX_STREAMS + 1];
static int32_t       mLogged;
static int32_t       mMuted;

static pm_metal_status_t NullDrainStep(pm_metal_async_handle_t self_h)
{
  (void)self_h;
  return PM_METAL_DONE;
}

static int NullInit(void)
{
  if (!mLogged) {
    pm_metal_log("metal-audio: null");
    mLogged = 1;
  }

  return 0;
}

static void NullPoll(void) {}

static int32_t NullReady(void)
{
  return 0;
}

static pm_metal_audio_stream_h NullOpen(uint32_t format, uint32_t frames)
{
  uint32_t i;

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

static void NullClose(pm_metal_audio_stream_h s)
{
  if (s == 0 || s > PM_METAL_AUDIO_MAX_STREAMS) {
    return;
  }

  memset(&mStreams[s], 0, sizeof(mStreams[s]));
}

static uint32_t NullQueue(pm_metal_audio_stream_h s, const void *pcm, uint32_t nbytes)
{
  (void)pcm;
  if (s == 0 || !mStreams[s].used || mMuted) {
    return 0;
  }

  return nbytes;
}

static pm_metal_async_handle_t NullDrain(pm_metal_audio_stream_h s, uint32_t nbytes)
{
  (void)nbytes;
  if (s == 0 || !mStreams[s].used) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_async_coro_create(NullDrainStep, 0);
}

static void NullMute(int on)
{
  mMuted = on ? 1 : 0;
}

static const pm_metal_audio_ops_t mNullOps = { "null",    NullInit,  NullPoll,  NullReady, NullOpen,
                                               NullClose, NullQueue, NullDrain, NullMute };

void pm_metal_audio_null_install(void)
{
  pm_metal_audio_set_ops(&mNullOps);
  (void)NullInit();
}
