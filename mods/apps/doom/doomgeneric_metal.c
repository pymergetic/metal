/*
 * doomgeneric platform — Metal wasm (gfx blit + async clock + input).
 *
 * App-local: Metal HID keycodes → doomgeneric key bytes. Host input ABI
 * is Metal-only; this translator stays in the parked doom package.
 */
#include "../../../external/doomgeneric/doomgeneric/doomgeneric.h"

#include "pymergetic/metal/dev/gfx/gfx.h"
#include "pymergetic/metal/runtime/async/async.h"
#include "pymergetic/metal/dev/input/input.h"

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
	/* Rate/idle: host metal-perf on serial (printf→ConOut paints over GOP). */
	(void)scale;
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

static unsigned char
MetalKeyToDoom (
	pm_metal_keycode_t  code
	)
{
	switch (code) {
	case PM_METAL_KEY_ESCAPE:
		return 0x1b;
	case PM_METAL_KEY_ENTER:
		return 0x0d;
	case PM_METAL_KEY_TAB:
		return 0x09;
	case PM_METAL_KEY_SPACE:
		return 0xa2; /* KEY_USE */
	case PM_METAL_KEY_LEFT:
		return 0xac;
	case PM_METAL_KEY_RIGHT:
		return 0xae;
	case PM_METAL_KEY_UP:
		return 0xad;
	case PM_METAL_KEY_DOWN:
		return 0xaf;
	case PM_METAL_KEY_LCTRL:
	case PM_METAL_KEY_RCTRL:
		return 0xa3; /* KEY_FIRE */
	case PM_METAL_KEY_LSHIFT:
	case PM_METAL_KEY_RSHIFT:
		return (unsigned char)(0x80 + 0x36);
	case PM_METAL_KEY_LALT:
	case PM_METAL_KEY_RALT:
		return (unsigned char)(0x80 + 0x38);
	default:
		break;
	}

	if (code >= PM_METAL_KEY_A && code <= PM_METAL_KEY_Z) {
		return (unsigned char)('a' + (code - PM_METAL_KEY_A));
	}

	if (code >= PM_METAL_KEY_1 && code <= PM_METAL_KEY_1 + 8) {
		return (unsigned char)('1' + (code - PM_METAL_KEY_1));
	}

	if (code == PM_METAL_KEY_0) {
		return '0';
	}

	return 0;
}

int
DG_GetKey(int *pressed, unsigned char *doomKey)
{
	pm_metal_input_key_event_t  ev;
	unsigned char               dk;

	if (pm_metal_input_poll_key_event(&ev) == 0) {
		return 0;
	}

	dk = MetalKeyToDoom(ev.code);
	if (dk == 0) {
		return 0;
	}

	if (pressed != NULL) {
		*pressed = ev.pressed ? 1 : 0;
	}
	if (doomKey != NULL) {
		*doomKey = dk;
	}
	return 1;
}

void
DG_SetWindowTitle(const char *title)
{
	/* Status bar updates dirty the shell chrome; skip under game FB. */
	(void)title;
}
