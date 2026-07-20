/*
 * doomgeneric platform — Metal wasm (gfx blit + async clock + input).
 */
#include <stdio.h>

#include "../../../external/doomgeneric/doomgeneric/doomgeneric.h"

#include "pymergetic/metal/gfx.h"
#include "pymergetic/metal/async.h"
#include "pymergetic/metal/input.h"

/* From d_loop.c — avoid d_loop.h (pulls doomtype.h → <strings.h>). */
extern int singletics;

void
DG_Init(void)
{
	/* Force one tic per Tick — pacing is await(sleep) in guest_step. */
	singletics = 1;
}

void
DG_DrawFrame(void)
{
	int gw;
	int gh;
	int scale;
	int sx;
	int sy;
	int dw;
	int dh;
	int dx;
	int dy;
	static int s_letterbox;

	if (DG_ScreenBuffer == NULL) {
		return;
	}

	gw = pm_metal_gfx_width();
	gh = pm_metal_gfx_height();
	if (gw <= 0 || gh <= 0) {
		return;
	}

	/* Max integer scale that fits (fullscreen letterbox). */
	sx = gw / DOOMGENERIC_RESX;
	sy = gh / DOOMGENERIC_RESY;
	scale = sx < sy ? sx : sy;
	if (scale < 1) {
		scale = 1;
	}

	dw = DOOMGENERIC_RESX * scale;
	dh = DOOMGENERIC_RESY * scale;
	dx = (gw - dw) / 2;
	dy = (gh - dh) / 2;

	if (!s_letterbox) {
		pm_metal_gfx_clear(PM_METAL_GFX_RGB(0, 0, 0));
		(void)pm_metal_gfx_present();
		s_letterbox = 1;
	}

	(void)pm_metal_gfx_blit_bgra(
		dx, dy, dw, dh, DG_ScreenBuffer, DOOMGENERIC_RESX, DOOMGENERIC_RESY,
		DOOMGENERIC_RESX * (int)sizeof(pixel_t));
}

void
DG_SleepMs(uint32_t ms)
{
	/* No-op: frame pacing is await(sleep) in pm_metal_guest_step. */
	(void)ms;
}

uint32_t
DG_GetTicksMs(void)
{
	return (uint32_t)pm_metal_async_mono_ms();
}

int
DG_GetKey(int *pressed, unsigned char *doomKey)
{
	int32_t packed;

	packed = pm_metal_input_poll_key_packed();
	if (packed == 0) {
		return 0;
	}

	if (pressed != NULL) {
		*pressed = (packed >> 8) & 1;
	}
	if (doomKey != NULL) {
		*doomKey = (unsigned char)(packed & 0xff);
	}
	return 1;
}

void
DG_SetWindowTitle(const char *title)
{
	/* Status bar updates dirty the shell chrome; skip under game FB. */
	(void)title;
}
