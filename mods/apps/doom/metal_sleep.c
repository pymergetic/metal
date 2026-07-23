/*
 * --wrap=I_Sleep — advance a fake clock so D_Display wipe melt cannot spin
 * forever when real mono_ms is frozen inside one guest_step.
 *
 * Bump at least one Doom tic (~29 ms) so each I_Sleep advances wipe by one
 * outer-loop iteration. Still runs the full wipe inside one Tick (CPU hog);
 * cooperative one-frame wipes need a D_Display stem later.
 */
#include "metal_doom.h"

/* TICRATE=35 → ceil(1000/35) ms for I_GetTime to advance one tic. */
#define METAL_DOOM_TIC_MS 29u

static uint32_t s_fake_ms;

void
metal_doom_sleep_bump_ms(uint32_t ms)
{
	s_fake_ms += ms;
}

uint32_t
metal_doom_fake_ms(void)
{
	return s_fake_ms;
}

void
__wrap_I_Sleep(int ms)
{
	uint32_t bump;

	if (ms < 1) {
		ms = 1;
	}

	bump = (uint32_t)ms;
	if (bump < METAL_DOOM_TIC_MS) {
		bump = METAL_DOOM_TIC_MS;
	}

	metal_doom_sleep_bump_ms(bump);
}
