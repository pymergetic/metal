/*
 * doomgeneric platform — Metal wasm (gfx blit + async clock + input).
 *
 * Display follows launch (same paint policy either way):
 *   run doom  → console tab → fullscreen DEFAULT surface
 *   tab doom  → app tab     → that tab's content surface
 * Both: one Metal blit scales native 320×200 to the surface (no DG upscale).
 * Present: blit into shadow FB; guest_step awaits pm_metal_async_present.
 */
#include "../../../external/doomgeneric/doomgeneric/doomgeneric.h"

#include "metal_doom.h"

#include "pymergetic/metal/dev/gfx/gfx.h"
#include "pymergetic/metal/runtime/async/async.h"
#include "pymergetic/metal/dev/input/input.h"
#include "pymergetic/metal/guest/process/process.h"
#include "pymergetic/metal/shell/ui/ui.h"

extern int singletics;

static int s_windowed;
static uint32_t s_present_surf;

uint32_t
metal_doom_present_surface(void)
{
	return s_present_surf;
}

void
metal_doom_clear_present(void)
{
	s_present_surf = 0;
}

static pm_metal_gfx_surface_h
MetalDoomPickSurface(void)
{
	pm_metal_process_id_t  pid;
	uint32_t               kind;
	uint32_t               surf;
	pm_metal_ui_handle_t   tab;

	/*
	 * Host spawn kind is source of truth (`run` → FULLSCREEN/DEFAULT,
	 * `tab` → TAB/tab surface). Do not sniff console vs app tab — the
	 * console also has a content surface and that lied about windowed.
	 */
	pid = pm_metal_process_self();
	if (pid != PM_METAL_PROCESS_ID_INVALID) {
		kind = pm_metal_process_ui_kind(pid);
		surf = pm_metal_process_surface(pid);
		if (kind == (uint32_t)PM_METAL_PROC_UI_TAB
		    && surf != PM_METAL_GFX_SURFACE_INVALID
		    && surf != PM_METAL_GFX_SURFACE_DEFAULT)
		{
			return (pm_metal_gfx_surface_h)surf;
		}

		if (kind == (uint32_t)PM_METAL_PROC_UI_FULLSCREEN) {
			return PM_METAL_GFX_SURFACE_DEFAULT;
		}
	}

	/* Fallback if process metadata missing mid-startup. */
	tab = pm_metal_ui_tab_active();
	if (tab != PM_METAL_UI_HANDLE_INVALID
	    && tab != pm_metal_ui_console_handle())
	{
		surf = (uint32_t)pm_metal_ui_tab_surface(tab);
		if (surf != PM_METAL_GFX_SURFACE_INVALID
		    && surf != PM_METAL_GFX_SURFACE_DEFAULT)
		{
			return (pm_metal_gfx_surface_h)surf;
		}
	}

	return PM_METAL_GFX_SURFACE_DEFAULT;
}

void
DG_Init(void)
{
	pm_metal_gfx_surface_h  surf;

	singletics = 1;

	surf       = MetalDoomPickSurface();
	s_windowed = (surf != PM_METAL_GFX_SURFACE_DEFAULT
		      && surf != PM_METAL_GFX_SURFACE_INVALID);
	pm_metal_gfx_set_surface(surf);
	s_present_surf = (uint32_t)surf;

	/* Capture pointer for relative mouse look (Esc unlocks; click re-locks). */
	(void)pm_metal_input_pointer_lock(s_present_surf);
}

void
DG_DrawFrame(void)
{
	int                    gw;
	int                    gh;
	pm_metal_gfx_surface_h surf;

	if (DG_ScreenBuffer == NULL) {
		return;
	}

	surf = s_windowed ? MetalDoomPickSurface()
			  : PM_METAL_GFX_SURFACE_DEFAULT;
	pm_metal_gfx_set_surface(surf);

	gw = pm_metal_gfx_width();
	gh = pm_metal_gfx_height();
	if (gw <= 0 || gh <= 0) {
		return;
	}

	/*
	 * Single scale: DG buffer is native 320×200; Metal blit fills the
	 * surface (integer fast path when size divides evenly).
	 */
	(void)pm_metal_gfx_blit_bgra(
		0, 0, gw, gh, DG_ScreenBuffer, DOOMGENERIC_RESX, DOOMGENERIC_RESY,
		DOOMGENERIC_RESX * (int)sizeof(pixel_t));
	s_present_surf = (uint32_t)surf;
}

void
DG_SleepMs(uint32_t ms)
{
	/* Prefer --wrap=I_Sleep bump; keep no-op if called directly. */
	if (ms > 0) {
		metal_doom_sleep_bump_ms(ms);
	}
}

uint32_t
DG_GetTicksMs(void)
{
	return (uint32_t)pm_metal_async_mono_ms() + metal_doom_fake_ms();
}

static unsigned char
MetalKeyToDoom(pm_metal_keycode_t code)
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
		return 0xac; /* KEY_LEFTARROW — turn / menu; Alt+ = strafe */
	case PM_METAL_KEY_RIGHT:
		return 0xae; /* KEY_RIGHTARROW */
	case PM_METAL_KEY_UP:
		return 0xad; /* KEY_UPARROW — forward + menu */
	case PM_METAL_KEY_DOWN:
		return 0xaf; /* KEY_DOWNARROW — back */
	case PM_METAL_KEY_LCTRL:
	case PM_METAL_KEY_RCTRL:
		return 0xa3; /* KEY_FIRE */
	case PM_METAL_KEY_LSHIFT:
	case PM_METAL_KEY_RSHIFT:
		return (unsigned char)(0x80 + 0x36); /* run */
	case PM_METAL_KEY_LALT:
	case PM_METAL_KEY_RALT:
		return (unsigned char)(0x80 + 0x38); /* KEY_RALT — strafe mod */
	default:
		break;
	}

	/* WASD → classical actions (hybrid with arrows). */
	if (code == PM_METAL_KEY_A) {
		return 0xa0; /* KEY_STRAFE_L */
	}

	if (code == (pm_metal_keycode_t)(PM_METAL_KEY_A + 3u)) {
		return 0xa1; /* D → KEY_STRAFE_R */
	}

	if (code == (pm_metal_keycode_t)(PM_METAL_KEY_A + 22u)) {
		return 0xad; /* W → KEY_UPARROW */
	}

	if (code == (pm_metal_keycode_t)(PM_METAL_KEY_A + 18u)) {
		return 0xaf; /* S → KEY_DOWNARROW */
	}

	/* E (HID 0x08) → use (modern); Space also KEY_USE. */
	if (code == (pm_metal_keycode_t)(PM_METAL_KEY_A + 4u)) {
		return 0xa2; /* KEY_USE */
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
	pm_metal_input_key_event_t ev;
	unsigned char              dk;

	if (pm_metal_input_poll_key_event((uint32_t)(uintptr_t)&ev) == 0) {
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
	(void)title;
}
