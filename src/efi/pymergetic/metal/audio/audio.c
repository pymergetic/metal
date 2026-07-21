/** @file
  Metal audio facade — pluggable ops (virtio-snd / null). (impl: efi)
**/
#include <pymergetic/metal/audio/audio.h>
#include <pymergetic/metal/audio/audio_ops.h>

#include <Uefi.h>

#include "wasm_export.h"

STATIC CONST pm_metal_audio_ops_t  *mOps;
STATIC wasm_module_inst_t           mAudioInst;

void
pm_metal_audio_set_ops (
  CONST pm_metal_audio_ops_t  *ops
  )
{
  mOps = ops;
}

CONST pm_metal_audio_ops_t *
pm_metal_audio_get_ops (
  VOID
  )
{
  return mOps;
}

void
pm_metal_audio_poll (
  VOID
  )
{
  if (mOps != NULL && mOps->poll != NULL) {
    mOps->poll ();
  }
}

void
pm_metal_audio_mute (
  int  on
  )
{
  if (mOps != NULL && mOps->mute != NULL) {
    mOps->mute (on);
  }
}

void
pm_metal_audio_bind_inst (
  VOID  *module_inst
  )
{
  mAudioInst = (wasm_module_inst_t)module_inst;
}

int32_t
pm_metal_audio_ready (
  VOID
  )
{
  if (mOps == NULL || mOps->ready == NULL) {
    return 0;
  }

  return mOps->ready ();
}

pm_metal_audio_stream_h
pm_metal_audio_open (
  uint32_t  format,
  uint32_t  frames_buffered
  )
{
  if (mOps == NULL || mOps->open == NULL) {
    return PM_METAL_AUDIO_STREAM_INVALID;
  }

  return mOps->open (format, frames_buffered);
}

void
pm_metal_audio_close (
  pm_metal_audio_stream_h  s
  )
{
  if (mOps != NULL && mOps->close != NULL) {
    mOps->close (s);
  }
}

uint32_t
pm_metal_audio_queue (
  pm_metal_audio_stream_h  s,
  CONST VOID              *pcm,
  uint32_t                 nbytes
  )
{
  if (mOps == NULL || mOps->queue == NULL) {
    return 0;
  }

  return mOps->queue (s, pcm, nbytes);
}

pm_metal_async_handle_t
pm_metal_audio_drain (
  pm_metal_audio_stream_h  s,
  uint32_t                 nbytes
  )
{
  if (mOps == NULL || mOps->drain == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return mOps->drain (s, nbytes);
}

STATIC INT32
pm_metal_audio_ready_native (
  wasm_exec_env_t  exec_env
  )
{
  (VOID)exec_env;
  return pm_metal_audio_ready ();
}

STATIC UINT32
pm_metal_audio_open_native (
  wasm_exec_env_t  exec_env,
  UINT32           format,
  UINT32           frames
  )
{
  (VOID)exec_env;
  return pm_metal_audio_open (format, frames);
}

STATIC VOID
pm_metal_audio_close_native (
  wasm_exec_env_t  exec_env,
  UINT32           s
  )
{
  (VOID)exec_env;
  pm_metal_audio_close (s);
}

STATIC UINT32
pm_metal_audio_queue_native (
  wasm_exec_env_t  exec_env,
  UINT32           s,
  UINT32           pcm,
  UINT32           nbytes
  )
{
  VOID  *native;

  (VOID)exec_env;
  if (mAudioInst == NULL || nbytes == 0) {
    return 0;
  }

  if (!wasm_runtime_validate_app_addr (mAudioInst, pcm, nbytes)) {
    return 0;
  }

  native = wasm_runtime_addr_app_to_native (mAudioInst, pcm);
  if (native == NULL) {
    return 0;
  }

  return pm_metal_audio_queue (s, native, nbytes);
}

STATIC UINT32
pm_metal_audio_drain_native (
  wasm_exec_env_t  exec_env,
  UINT32           s,
  UINT32           nbytes
  )
{
  (VOID)exec_env;
  return pm_metal_audio_drain (s, nbytes);
}

STATIC NativeSymbol g_pm_metal_audio_native_symbols[] = {
  { "pm_metal_audio_ready", (VOID *)pm_metal_audio_ready_native, "()i", NULL },
  { "pm_metal_audio_open", (VOID *)pm_metal_audio_open_native, "(ii)i", NULL },
  { "pm_metal_audio_close", (VOID *)pm_metal_audio_close_native, "(i)", NULL },
  { "pm_metal_audio_queue", (VOID *)pm_metal_audio_queue_native, "(iii)i", NULL },
  { "pm_metal_audio_drain", (VOID *)pm_metal_audio_drain_native, "(ii)i", NULL },
};

int
pm_metal_audio_native_register (
  VOID
  )
{
  if (!wasm_runtime_register_natives (
         PM_METAL_AUDIO_WASI_MODULE,
         g_pm_metal_audio_native_symbols,
         sizeof (g_pm_metal_audio_native_symbols)
           / sizeof (g_pm_metal_audio_native_symbols[0])
         ))
  {
    return -1;
  }

  return 0;
}
