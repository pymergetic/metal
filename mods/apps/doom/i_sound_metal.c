/*
 * Phase D — Metal DG_sound_module / stub DG_music_module.
 * Queue S16LE stereo 22050 into pm_metal_audio_*; drain await in guest_step.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../../../external/doomgeneric/doomgeneric/doomtype.h"
#include "../../../external/doomgeneric/doomgeneric/i_sound.h"
#include "../../../external/doomgeneric/doomgeneric/m_misc.h"
#include "../../../external/doomgeneric/doomgeneric/w_wad.h"
#include "../../../external/doomgeneric/doomgeneric/z_zone.h"
#include "../../../external/doomgeneric/doomgeneric/deh_str.h"

#include "metal_doom.h"

#include "pymergetic/metal/dev/audio/audio.h"

#define NUM_CHANNELS 8
#define MIX_HZ 22050
#define MIX_FRAMES 512 /* ~23 ms stereo slice */
#define MIX_BYTES (MIX_FRAMES * 2 * (int)sizeof(int16_t))

int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;

typedef struct {
	int16_t *pcm; /* interleaved stereo S16 */
	int frames;
} metal_sfx_t;

typedef struct {
	metal_sfx_t *sfx;
	int pos;
	int vol_l;
	int vol_r;
	int active;
} metal_chan_t;

static boolean s_use_prefix;
static boolean s_inited;
static pm_metal_audio_stream_h s_stream = PM_METAL_AUDIO_STREAM_INVALID;
static metal_chan_t s_chan[NUM_CHANNELS];
static int16_t s_mix[MIX_FRAMES * 2];

static int s_drain_pending;
static uint32_t s_drain_stream;
static uint32_t s_drain_nbytes;

int
metal_doom_audio_drain_pending(void)
{
	return s_drain_pending;
}

uint32_t
metal_doom_audio_drain_stream(void)
{
	return s_drain_stream;
}

uint32_t
metal_doom_audio_drain_nbytes(void)
{
	return s_drain_nbytes;
}

void
metal_doom_audio_drain_clear(void)
{
	s_drain_pending = 0;
	s_drain_nbytes  = 0;
}

void
metal_doom_audio_request_drain(uint32_t stream, uint32_t nbytes)
{
	s_drain_pending = 1;
	s_drain_stream  = stream;
	s_drain_nbytes  = nbytes;
}

static void
GetSfxLumpName(sfxinfo_t *sfx, char *buf, size_t buf_len)
{
	if (sfx->link != NULL) {
		sfx = sfx->link;
	}

	if (s_use_prefix) {
		M_snprintf(buf, buf_len, "ds%s", DEH_String(sfx->name));
	} else {
		M_StringCopy(buf, DEH_String(sfx->name), buf_len);
	}
}

static metal_sfx_t *
CacheSFX(sfxinfo_t *sfxinfo)
{
	byte *data;
	unsigned int lumplen;
	unsigned int length;
	int samplerate;
	int lumpnum;
	metal_sfx_t *out;
	int16_t *dst;
	int out_frames;
	int i;
	byte *src;

	if (sfxinfo->driver_data != NULL) {
		return (metal_sfx_t *)sfxinfo->driver_data;
	}

	lumpnum = sfxinfo->lumpnum;
	data = W_CacheLumpNum(lumpnum, PU_STATIC);
	lumplen = W_LumpLength(lumpnum);

	if (lumplen < 8 || data[0] != 0x03 || data[1] != 0x00) {
		W_ReleaseLumpNum(lumpnum);
		return NULL;
	}

	samplerate = (data[3] << 8) | data[2];
	length = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
	if (length > lumplen - 8 || length <= 48 || samplerate < 1) {
		W_ReleaseLumpNum(lumpnum);
		return NULL;
	}

	/* DMX skips first/last 16 samples of payload. */
	src = data + 8 + 16;
	length -= 32;

	out_frames = (int)(((int64_t)length * MIX_HZ) / samplerate);
	if (out_frames < 1) {
		W_ReleaseLumpNum(lumpnum);
		return NULL;
	}

	out = (metal_sfx_t *)malloc(sizeof(*out));
	if (out == NULL) {
		W_ReleaseLumpNum(lumpnum);
		return NULL;
	}

	dst = (int16_t *)malloc((size_t)out_frames * 2u * sizeof(int16_t));
	if (dst == NULL) {
		free(out);
		W_ReleaseLumpNum(lumpnum);
		return NULL;
	}

	for (i = 0; i < out_frames; i++) {
		int src_i;
		int16_t sample;

		src_i = (int)(((int64_t)i * samplerate) / MIX_HZ);
		if (src_i < 0) {
			src_i = 0;
		}
		if ((unsigned int)src_i >= length) {
			src_i = (int)length - 1;
		}

		sample = (int16_t)((src[src_i] - 128) << 8);
		dst[i * 2]     = sample;
		dst[i * 2 + 1] = sample;
	}

	out->pcm = dst;
	out->frames = out_frames;
	sfxinfo->driver_data = out;
	W_ReleaseLumpNum(lumpnum);
	return out;
}

static boolean
Metal_Init(boolean use_sfx_prefix)
{
	int i;

	s_use_prefix = use_sfx_prefix;
	for (i = 0; i < NUM_CHANNELS; i++) {
		s_chan[i].active = 0;
		s_chan[i].sfx = NULL;
	}

	/* Force mixer rate used by higher layers. */
	snd_samplerate = MIX_HZ;

	s_stream = pm_metal_audio_open(PM_METAL_AUDIO_FMT_S16LE_STEREO_22050,
				       (uint32_t)(MIX_FRAMES * 4));
	if (s_stream == PM_METAL_AUDIO_STREAM_INVALID) {
		fprintf(stderr, "metal-doom: audio open failed\n");
		return false;
	}

	s_inited = true;
	return true;
}

static void
Metal_Shutdown(void)
{
	if (!s_inited) {
		return;
	}

	if (s_stream != PM_METAL_AUDIO_STREAM_INVALID) {
		pm_metal_audio_close(s_stream);
		s_stream = PM_METAL_AUDIO_STREAM_INVALID;
	}

	s_inited = false;
}

static int
Metal_GetSfxLumpNum(sfxinfo_t *sfxinfo)
{
	char namebuf[9];

	GetSfxLumpName(sfxinfo, namebuf, sizeof(namebuf));
	return W_GetNumForName(namebuf);
}

static void
Metal_UpdateSoundParams(int channel, int vol, int sep)
{
	int left;
	int right;

	if (channel < 0 || channel >= NUM_CHANNELS) {
		return;
	}

	/* Chocolate-doom style sep/vol → L/R (0-255). */
	left  = ((254 - sep) * vol) / 127;
	right = (sep * vol) / 127;
	if (left > 255) {
		left = 255;
	}
	if (right > 255) {
		right = 255;
	}

	s_chan[channel].vol_l = left;
	s_chan[channel].vol_r = right;
}

static int
Metal_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep)
{
	metal_sfx_t *sfx;

	if (!s_inited || channel < 0 || channel >= NUM_CHANNELS) {
		return -1;
	}

	sfx = CacheSFX(sfxinfo);
	if (sfx == NULL) {
		return -1;
	}

	s_chan[channel].sfx = sfx;
	s_chan[channel].pos = 0;
	s_chan[channel].active = 1;
	Metal_UpdateSoundParams(channel, vol, sep);
	return channel;
}

static void
Metal_StopSound(int channel)
{
	if (channel < 0 || channel >= NUM_CHANNELS) {
		return;
	}

	s_chan[channel].active = 0;
	s_chan[channel].sfx = NULL;
}

static boolean
Metal_SoundIsPlaying(int channel)
{
	if (channel < 0 || channel >= NUM_CHANNELS) {
		return false;
	}

	return s_chan[channel].active ? true : false;
}

static void
Metal_Update(void)
{
	int i;
	int f;
	uint32_t queued;

	if (!s_inited) {
		return;
	}

	memset(s_mix, 0, sizeof(s_mix));

	for (i = 0; i < NUM_CHANNELS; i++) {
		metal_chan_t *ch = &s_chan[i];
		metal_sfx_t *sfx;

		if (!ch->active || ch->sfx == NULL) {
			continue;
		}

		sfx = ch->sfx;
		for (f = 0; f < MIX_FRAMES; f++) {
			int32_t l;
			int32_t r;
			int idx;

			if (ch->pos >= sfx->frames) {
				ch->active = 0;
				ch->sfx = NULL;
				break;
			}

			idx = ch->pos * 2;
			l = (int32_t)s_mix[f * 2]
			    + (((int32_t)sfx->pcm[idx] * ch->vol_l) / 255);
			r = (int32_t)s_mix[f * 2 + 1]
			    + (((int32_t)sfx->pcm[idx + 1] * ch->vol_r) / 255);
			if (l > 32767) {
				l = 32767;
			}
			if (l < -32768) {
				l = -32768;
			}
			if (r > 32767) {
				r = 32767;
			}
			if (r < -32768) {
				r = -32768;
			}
			s_mix[f * 2] = (int16_t)l;
			s_mix[f * 2 + 1] = (int16_t)r;
			ch->pos++;
		}
	}

	queued = pm_metal_audio_queue(s_stream, (uint32_t)(uintptr_t)s_mix,
				      (uint32_t)MIX_BYTES);
	if (queued > 0) {
		metal_doom_audio_request_drain(s_stream, queued);
	}
}

static void
Metal_CacheSounds(sfxinfo_t *sounds, int num_sounds)
{
	(void)sounds;
	(void)num_sounds;
}

static snddevice_t s_sfx_devices[] = {
	SNDDEVICE_SB,
	SNDDEVICE_PAS,
	SNDDEVICE_GUS,
	SNDDEVICE_WAVEBLASTER,
	SNDDEVICE_SOUNDCANVAS,
	SNDDEVICE_AWE32,
};

sound_module_t DG_sound_module = {
	s_sfx_devices,
	arrlen(s_sfx_devices),
	Metal_Init,
	Metal_Shutdown,
	Metal_GetSfxLumpNum,
	Metal_Update,
	Metal_UpdateSoundParams,
	Metal_StartSound,
	Metal_StopSound,
	Metal_SoundIsPlaying,
	Metal_CacheSounds,
};

/* --- music stub --- */

static boolean
Music_Init(void)
{
	return true;
}

static void
Music_Shutdown(void)
{
}

static void
Music_SetVolume(int volume)
{
	(void)volume;
}

static void
Music_Pause(void)
{
}

static void
Music_Resume(void)
{
}

static void *
Music_RegisterSong(void *data, int len)
{
	(void)data;
	(void)len;
	return (void *)1;
}

static void
Music_UnRegisterSong(void *handle)
{
	(void)handle;
}

static void
Music_PlaySong(void *handle, boolean looping)
{
	(void)handle;
	(void)looping;
}

static void
Music_StopSong(void)
{
}

static boolean
Music_IsPlaying(void)
{
	return false;
}

static void
Music_Poll(void)
{
}

static snddevice_t s_mus_devices[] = {
	SNDDEVICE_SB,
	SNDDEVICE_GENMIDI,
	SNDDEVICE_GUS,
};

music_module_t DG_music_module = {
	s_mus_devices,
	arrlen(s_mus_devices),
	Music_Init,
	Music_Shutdown,
	Music_SetVolume,
	Music_Pause,
	Music_Resume,
	Music_RegisterSong,
	Music_UnRegisterSong,
	Music_PlaySong,
	Music_StopSong,
	Music_IsPlaying,
	Music_Poll,
};
