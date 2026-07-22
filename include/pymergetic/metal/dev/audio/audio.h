/*
 * Metal audio — guest/host dual ABI (PCM streams + awaitable drain).
 * See docs/IO.md. Default host backend is silent/null.
 *
 * impl: common — src/pymergetic/metal/dev/audio/audio.c
 */
#ifndef PYMERGETIC_METAL_DEV_AUDIO_AUDIO_H_
#define PYMERGETIC_METAL_DEV_AUDIO_AUDIO_H_

#include <stdint.h>

#include "pymergetic/metal/runtime/async/async.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t pm_metal_audio_stream_h;

#define PM_METAL_AUDIO_STREAM_INVALID 0u

/** S16LE stereo 22050 Hz — v1 PCM format constant. */
#define PM_METAL_AUDIO_FMT_S16LE_STEREO_22050 1u

#define PM_METAL_AUDIO_WASI_MODULE "pymergetic.metal.audio"

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_AUDIO_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_AUDIO_WASI_MODULE, name)

extern int32_t pm_metal_audio_ready(void)
	PM_METAL_AUDIO_IMPORT(pm_metal_audio_ready);
extern pm_metal_audio_stream_h pm_metal_audio_open(uint32_t format,
						   uint32_t frames_buffered)
	PM_METAL_AUDIO_IMPORT(pm_metal_audio_open);
extern void pm_metal_audio_close(pm_metal_audio_stream_h s)
	PM_METAL_AUDIO_IMPORT(pm_metal_audio_close);
extern uint32_t pm_metal_audio_queue(pm_metal_audio_stream_h s, uint32_t pcm,
				     uint32_t nbytes)
	PM_METAL_AUDIO_IMPORT(pm_metal_audio_queue);
extern pm_metal_async_handle_t pm_metal_audio_drain(pm_metal_audio_stream_h s,
						    uint32_t nbytes)
	PM_METAL_AUDIO_IMPORT(pm_metal_audio_drain);
#else
int32_t pm_metal_audio_ready(void);
pm_metal_audio_stream_h pm_metal_audio_open(uint32_t format,
					    uint32_t frames_buffered);
void pm_metal_audio_close(pm_metal_audio_stream_h s);
uint32_t pm_metal_audio_queue(pm_metal_audio_stream_h s, const void *pcm,
			      uint32_t nbytes);
pm_metal_async_handle_t pm_metal_audio_drain(pm_metal_audio_stream_h s,
					     uint32_t nbytes);
int pm_metal_audio_native_register(void);
void pm_metal_audio_bind_inst(void *module_inst);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_AUDIO_AUDIO_H_ */
