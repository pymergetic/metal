/*
 * Host-only pluggable audio backend ops.
 *
 * impl: common — src/pymergetic/metal/dev/audio/audio.c
 * impl: backends — src/pymergetic/metal/dev/audio/{audio_null,virtio_snd,ac97}.c
 */
#ifndef PYMERGETIC_METAL_DEV_AUDIO_AUDIO_OPS_H_
#define PYMERGETIC_METAL_DEV_AUDIO_AUDIO_OPS_H_

#include <stdint.h>

#include "pymergetic/metal/runtime/async/async.h"
#include "pymergetic/metal/dev/audio/audio.h"

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

typedef struct pm_metal_audio_ops {
	const char *name;
	int (*init)(void);
	void (*poll)(void);
	int32_t (*ready)(void);
	pm_metal_audio_stream_h (*open)(uint32_t format, uint32_t frames);
	void (*close)(pm_metal_audio_stream_h s);
	uint32_t (*queue)(pm_metal_audio_stream_h s, const void *pcm, uint32_t n);
	pm_metal_async_handle_t (*drain)(pm_metal_audio_stream_h s, uint32_t n);
	void (*mute)(int on);
} pm_metal_audio_ops_t;

void pm_metal_audio_set_ops(const pm_metal_audio_ops_t *ops);
const pm_metal_audio_ops_t *pm_metal_audio_get_ops(void);
void pm_metal_audio_poll(void);
void pm_metal_audio_mute(int on);

int pm_metal_audio_virtio_probe(void);
int pm_metal_audio_ac97_probe(void);
void pm_metal_audio_null_install(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_AUDIO_AUDIO_OPS_H_ */
