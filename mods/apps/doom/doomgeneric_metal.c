/*
 * doomgeneric platform — Metal wasm (gfx blit + async clock + input).
 *
 * App-local: Metal HID keycodes → doomgeneric key bytes. Host input ABI
 * is Metal-only; this translator stays in the parked doom package.
 *
 * Display: default fullscreen (DEFAULT surface). `-w` → active tab surface
 * only (windowed; shell chrome stays).
 */
#include "../../../external/doomgeneric/doomgeneric/doomgeneric.h"

#include "pymergetic/metal/dev/gfx/gfx.h"
#include "pymergetic/metal/runtime/async/async.h"
#include "pymergetic/metal/dev/input/input.h"
#include "pymergetic/metal/shell/ui/ui.h"

/* From d_loop.c — avoid d_loop.h (pulls doomtype.h → <strings.h>). */
extern int singletics;

/* From m_argv.c — set before DG_Init by doomgeneric_Create. */
extern int M_CheckParm(char *check);

static int s_windowed;

void
DG_Init(void)
{
	pm_metal_ui_handle_t    tab;
	pm_metal_gfx_surface_h  surf;

	/* Force one tic per Tick — pacing is await(sleep) in guest_step. */
	singletics = 1;

	s_windowed = M_CheckParm("-w") != 0;
	if (!s_windowed) {
		pm_metal_gfx_set_surface(PM_METAL_GFX_SURFACE_DEFAULT);
		return;
	}

	tab  = pm_metal_ui_tab_active();
	surf = pm_metal_ui_tab_surface(tab);
	if (surf == PM_METAL_GFX_SURFACE_INVALID) {
		/* No tab surface — fall back to fullscreen. */
		s_windowed = 0;
		pm_metal_gfx_set_surface(PM_METAL_GFX_SURFACE_DEFAULT);
		return;
	}

	pm_metal_gfx_set_surface(surf);
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

	/* Re-bind each frame: shell chrome paint may reset DEFAULT. */
	if (s_windowed) {
		pm_metal_ui_handle_t    tab;
		pm_metal_gfx_surface_h  surf;

		tab  = pm_metal_ui_tab_active();
		surf = pm_metal_ui_tab_surface(tab);
		if (surf != PM_METAL_GFX_SURFACE_INVALID) {
			pm_metal_gfx_set_surface(surf);
		}
	} else {
		pm_metal_gfx_set_surface(PM_METAL_GFX_SURFACE_DEFAULT);
	}

	gw = pm_metal_gfx_width();
	gh = pm_metal_gfx_height();
	if (gw <= 0 || gh <= 0) {
		return;
	}

	/* Max integer scale that fits (fullscreen or tab content). */
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
