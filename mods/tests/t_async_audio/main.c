/*
 * Guest proof — audio open/queue/drain (virtio-snd or null-safe).
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/runtime/async/async.h"
#include "pymergetic/metal/dev/audio/audio.h"
#include "pymergetic/metal/shell/shell/shell.h"

typedef struct {
	uint32_t step;
	uint32_t aw;
	uint32_t stream;
	uint8_t  pcm[512];
} guest_state_t;

int32_t
pm_metal_guest_step(int32_t self_h)
{
	guest_state_t *s;
	uint32_t       n;

	s = (guest_state_t *)(uintptr_t)pm_metal_async_coro_state(
		(pm_metal_async_handle_t)self_h);
	if (s == NULL) {
		return PM_METAL_ERROR;
	}

	switch (s->step) {
	case 0:
		if (!pm_metal_audio_ready()) {
			/* Null backend: still exercise open/queue/eager drain. */
			s->stream = pm_metal_audio_open(
				PM_METAL_AUDIO_FMT_S16LE_STEREO_22050, 256);
			if (s->stream == PM_METAL_AUDIO_STREAM_INVALID) {
				return PM_METAL_ERROR;
			}
			memset(s->pcm, 0, sizeof(s->pcm));
			n = pm_metal_audio_queue(s->stream, (uint32_t)(uintptr_t)s->pcm,
						 (uint32_t)sizeof(s->pcm));
			if (n == 0) {
				return PM_METAL_ERROR;
			}
			s->aw = pm_metal_audio_drain(s->stream, n);
			if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
				return PM_METAL_ERROR;
			}
			s->step = 1;
			return pm_metal_async_await((pm_metal_async_handle_t)self_h,
						    s->aw);
		}
		s->stream = pm_metal_audio_open(
			PM_METAL_AUDIO_FMT_S16LE_STEREO_22050, 1024);
		if (s->stream == PM_METAL_AUDIO_STREAM_INVALID) {
			return PM_METAL_ERROR;
		}
		memset(s->pcm, 0, sizeof(s->pcm));
		n = pm_metal_audio_queue(s->stream, (uint32_t)(uintptr_t)s->pcm,
					 (uint32_t)sizeof(s->pcm));
		if (n == 0) {
			return PM_METAL_ERROR;
		}
		s->aw = pm_metal_audio_drain(s->stream, n);
		if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
			return PM_METAL_ERROR;
		}
		s->step = 1;
		return pm_metal_async_await((pm_metal_async_handle_t)self_h, s->aw);

	case 1:
		pm_metal_audio_close(s->stream);
		pm_metal_shell_log("metal-async: audio ok");
		return PM_METAL_DONE;

	default:
		return PM_METAL_ERROR;
	}
}

int
main(void)
{
	return 0;
}
