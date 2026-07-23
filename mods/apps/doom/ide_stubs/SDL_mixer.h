/* Minimal stub — i_sound.c includes SDL_mixer.h under FEATURE_SOUND. */
#ifndef METAL_DOOM_SDL_MIXER_STUB_H_
#define METAL_DOOM_SDL_MIXER_STUB_H_

typedef struct Mix_Chunk {
	int allocated;
	unsigned char *abuf;
	unsigned int alen;
	unsigned char volume;
} Mix_Chunk;

#endif /* METAL_DOOM_SDL_MIXER_STUB_H_ */
