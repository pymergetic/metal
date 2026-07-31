/** @file
  Metal audio facade — pluggable ops (virtio-snd / ac97 / null). (impl: efi|bios)
**/
#include <pymergetic/metal/dev/audio/audio.h>
#include <pymergetic/metal/dev/audio/audio_ops.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "wasm_export.h"

static const pm_metal_audio_ops_t *mOps;
static wasm_module_inst_t          mAudioInst;
static int32_t                     mMuted;
static uint32_t                    mVolume = 100u;
static uint8_t                     mVolScratch[2048];

void pm_metal_audio_set_ops(const pm_metal_audio_ops_t *ops)
{
  mOps = ops;
}

const pm_metal_audio_ops_t *pm_metal_audio_get_ops(void)
{
  return mOps;
}

void pm_metal_audio_poll(void)
{
  if (mOps != NULL && mOps->poll != NULL) {
    mOps->poll();
  }
}

void pm_metal_audio_mute(int32_t on)
{
  mMuted = on ? 1 : 0;
  if (mOps != NULL && mOps->mute != NULL) {
    mOps->mute(mMuted);
  }
}

int32_t pm_metal_audio_muted(void)
{
  return mMuted;
}

void pm_metal_audio_volume_set(uint32_t pct)
{
  if (pct > 100u) {
    pct = 100u;
  }

  mVolume = pct;
  if (mOps != NULL && mOps->volume_set != NULL) {
    mOps->volume_set(pct);
  }
}

uint32_t pm_metal_audio_volume_get(void)
{
  if (mOps != NULL && mOps->volume_get != NULL) {
    return mOps->volume_get();
  }

  return mVolume;
}

int32_t pm_metal_audio_backend(char *out, uint32_t out_cap)
{
  const char *name;

  if (out == NULL || out_cap == 0u) {
    return -1;
  }

  name = (mOps != NULL && mOps->name != NULL) ? mOps->name : "";
  if (strlen(name) + 1u > (size_t)out_cap) {
    return -1;
  }

  memcpy(out, name, strlen(name) + 1u);
  return 0;
}

void pm_metal_audio_bind_inst(void *module_inst)
{
  mAudioInst = (wasm_module_inst_t)module_inst;
}

int32_t pm_metal_audio_ready(void)
{
  if (mOps == NULL || mOps->ready == NULL) {
    return 0;
  }

  return mOps->ready();
}

pm_metal_audio_stream_h pm_metal_audio_open(uint32_t format, uint32_t frames_buffered)
{
  if (mOps == NULL || mOps->open == NULL) {
    return PM_METAL_AUDIO_STREAM_INVALID;
  }

  return mOps->open(format, frames_buffered);
}

void pm_metal_audio_close(pm_metal_audio_stream_h s)
{
  if (mOps != NULL && mOps->close != NULL) {
    mOps->close(s);
  }
}

static void AudioScaleS16(const int16_t *src, int16_t *dst, uint32_t nsamp, uint32_t pct)
{
  uint32_t i;

  for (i = 0; i < nsamp; i++) {
    dst[i] = (int16_t)(((int32_t)src[i] * (int32_t)pct) / 100);
  }
}

uint32_t pm_metal_audio_queue(pm_metal_audio_stream_h s, const void *pcm, uint32_t nbytes)
{
  const uint8_t *src;
  uint32_t       placed;
  uint32_t       vol;

  if (mOps == NULL || mOps->queue == NULL || pcm == NULL || nbytes == 0u) {
    return 0;
  }

  /*
   * Mute/volume=0 still go through the backend so DMA rings keep pacing
   * (Doom tracks queued frames from the return value - returning 0 here
   * caused catch-up storms and QEMU rate-control resets).
   * Backends with volume_set (AC97) own attenuation in hardware.
   */
  if (mOps->volume_set != NULL) {
    return mOps->queue(s, pcm, nbytes);
  }

  vol = mMuted ? 0u : mVolume;
  if (vol >= 100u || (nbytes & 1u) != 0u) {
    return mOps->queue(s, pcm, nbytes);
  }

  src    = (const uint8_t *)pcm;
  placed = 0;
  while (placed < nbytes) {
    uint32_t chunk;
    uint32_t got;

    chunk = nbytes - placed;
    if (chunk > sizeof(mVolScratch)) {
      chunk = (uint32_t)sizeof(mVolScratch);
    }

    chunk &= ~1u;
    if (chunk == 0u) {
      break;
    }

    if (vol == 0u) {
      memset(mVolScratch, 0, chunk);
    } else {
      AudioScaleS16((const int16_t *)(src + placed), (int16_t *)mVolScratch, chunk / 2u, vol);
    }

    got = mOps->queue(s, mVolScratch, chunk);
    if (got == 0u) {
      break;
    }

    placed += got;
    if (got < chunk) {
      break;
    }
  }

  return placed;
}

pm_metal_async_handle_t pm_metal_audio_drain(pm_metal_audio_stream_h s, uint32_t nbytes)
{
  if (mOps == NULL || mOps->drain == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return mOps->drain(s, nbytes);
}

static int32_t pm_metal_audio_ready_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_audio_ready();
}

static uint32_t pm_metal_audio_open_native(wasm_exec_env_t exec_env,
                                           uint32_t        format,
                                           uint32_t        frames)
{
  (void)exec_env;
  return pm_metal_audio_open(format, frames);
}

static void pm_metal_audio_close_native(wasm_exec_env_t exec_env, uint32_t s)
{
  (void)exec_env;
  pm_metal_audio_close(s);
}

static uint32_t pm_metal_audio_queue_native(wasm_exec_env_t exec_env,
                                            uint32_t        s,
                                            uint32_t        pcm,
                                            uint32_t        nbytes)
{
  void *native;

  (void)exec_env;
  if (mAudioInst == NULL || nbytes == 0) {
    return 0;
  }

  if (!wasm_runtime_validate_app_addr(mAudioInst, pcm, nbytes)) {
    return 0;
  }

  native = wasm_runtime_addr_app_to_native(mAudioInst, pcm);
  if (native == NULL) {
    return 0;
  }

  return pm_metal_audio_queue(s, native, nbytes);
}

static uint32_t pm_metal_audio_drain_native(wasm_exec_env_t exec_env, uint32_t s, uint32_t nbytes)
{
  (void)exec_env;
  return pm_metal_audio_drain(s, nbytes);
}

static void pm_metal_audio_mute_native(wasm_exec_env_t exec_env, int32_t on)
{
  (void)exec_env;
  pm_metal_audio_mute(on);
}

static int32_t pm_metal_audio_muted_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_audio_muted();
}

static void pm_metal_audio_volume_set_native(wasm_exec_env_t exec_env, uint32_t pct)
{
  (void)exec_env;
  pm_metal_audio_volume_set(pct);
}

static uint32_t pm_metal_audio_volume_get_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_audio_volume_get();
}

static int32_t pm_metal_audio_backend_native(wasm_exec_env_t exec_env,
                                             uint32_t        dest,
                                             uint32_t        dest_cap)
{
  void *native;

  (void)exec_env;
  if (mAudioInst == NULL || dest_cap == 0u) {
    return -1;
  }

  if (!wasm_runtime_validate_app_addr(mAudioInst, dest, dest_cap)) {
    return -1;
  }

  native = wasm_runtime_addr_app_to_native(mAudioInst, dest);
  if (native == NULL) {
    return -1;
  }

  return pm_metal_audio_backend((char *)native, dest_cap);
}

static NativeSymbol g_pm_metal_audio_native_symbols[] = {
  { "pm_metal_audio_ready", (void *)pm_metal_audio_ready_native, "()i", NULL },
  { "pm_metal_audio_open", (void *)pm_metal_audio_open_native, "(ii)i", NULL },
  { "pm_metal_audio_close", (void *)pm_metal_audio_close_native, "(i)", NULL },
  { "pm_metal_audio_queue", (void *)pm_metal_audio_queue_native, "(iii)i", NULL },
  { "pm_metal_audio_drain", (void *)pm_metal_audio_drain_native, "(ii)i", NULL },
  { "pm_metal_audio_mute", (void *)pm_metal_audio_mute_native, "(i)", NULL },
  { "pm_metal_audio_muted", (void *)pm_metal_audio_muted_native, "()i", NULL },
  { "pm_metal_audio_volume_set", (void *)pm_metal_audio_volume_set_native, "(i)", NULL },
  { "pm_metal_audio_volume_get", (void *)pm_metal_audio_volume_get_native, "()i", NULL },
  { "pm_metal_audio_backend", (void *)pm_metal_audio_backend_native, "(ii)i", NULL },
};

int pm_metal_audio_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_AUDIO_WASI_MODULE,
                                     g_pm_metal_audio_native_symbols,
                                     sizeof(g_pm_metal_audio_native_symbols) /
                                       sizeof(g_pm_metal_audio_native_symbols[0]))) {
    return -1;
  }

  return 0;
}
