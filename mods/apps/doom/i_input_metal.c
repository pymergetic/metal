/*
 * Metal I_GetEvent / I_InitInput — replaces doomgeneric i_input.c.
 *
 * Hybrid controls (mouse optional):
 *   Classical — arrows move/turn, Alt+←/→ strafe, Shift run, Ctrl fire
 *   Modern    — WASD → same actions (W/S walk, A/D strafe), E/Space use
 *   Mouse     — pointer-lock relative look (X turn); Y ignored (novert)
 *
 * Host dx/dy are FB pixels. G_Responder *assigns* mousex per ev_mouse
 * (does not add) — coalesce all ring samples into one event per tick.
 *
 * Gain is width-relative: ~half the draw surface width ≈ 180° yaw at
 * mouseSensitivity 9 (neutral across QEMU tablet / PS/2 / iron).
 */
#include <ctype.h>
#include <stdint.h>

#include "doomgeneric.h"
#include "doomkeys.h"
#include "d_event.h"

#include "pymergetic/metal/dev/gfx/gfx.h"
#include "pymergetic/metal/dev/input/input.h"

#include "metal_doom.h"

#ifndef METAL_DOOM_MOUSE_MAX
#define METAL_DOOM_MOUSE_MAX 8192
#endif

extern int usemouse;
extern int mouseSensitivity;
extern int key_up;
extern int key_down;
extern int key_left;
extern int key_right;
extern int key_strafeleft;
extern int key_straferight;
extern int key_strafe; /* Alt — hold + ←/→ to strafe (classic) */
extern int key_fire;
extern int key_use;
extern int key_speed;
extern int mousebfire;
extern int mousebstrafe;
extern int mousebforward;
extern int mousebuse;

static int shiftdown;
static int s_mouse_buttons;

/**
 * Pixel → doom mouse units. At sens 9, half FB width ≈ 180°
 * (mousex≈4096 → angleturn*0x8<<16 ≈ half circle).
 */
static int
MetalDoomScaleMouse(int v)
{
	int a;
	int s;
	int gw;
	int gain;

	if (v == 0) {
		return 0;
	}

	a  = (v < 0) ? -v : v;
	s  = (v < 0) ? -1 : 1;
	gw = pm_metal_gfx_width();
	if (gw < 320) {
		gw = 320;
	}

	/* 5852/gw ≈ units per pixel for half-width → 180° at sens 9. */
	gain = (5852 + gw / 2) / gw;
	if (gain < 4) {
		gain = 4;
	}

	a *= gain;
	/* Mild accel on larger flicks. */
	if (a > 8) {
		a = 8 + ((a - 8) * 3) / 2;
	}

	if (a > METAL_DOOM_MOUSE_MAX) {
		a = METAL_DOOM_MOUSE_MAX;
	}

	return s * a;
}

static void
MetalDoomApplyBindings(void)
{
	/* Classical defaults — arrows work without mouse. */
	key_up = KEY_UPARROW;
	key_down = KEY_DOWNARROW;
	key_left = KEY_LEFTARROW;
	key_right = KEY_RIGHTARROW;
	key_strafe = KEY_RALT; /* Alt+←/→ strafe */
	key_strafeleft = KEY_STRAFE_L; /* WASD A */
	key_straferight = KEY_STRAFE_R; /* WASD D */
	key_fire = KEY_FIRE;
	key_use = KEY_USE; /* Space; E also → KEY_USE */
	key_speed = KEY_RSHIFT;

	usemouse = 1; /* optional look; keyboard is enough */
	mouseSensitivity = 9;
	mousebfire = 0; /* left */
	mousebstrafe = -1;
	mousebforward = -1;
	mousebuse = -1;
}

static void
MetalDoomEnsurePointerLock(void)
{
	uint32_t surf;

	if (pm_metal_input_pointer_locked() != 0) {
		return;
	}

	surf = metal_doom_present_surface();
	if (surf == 0) {
		surf = (uint32_t)PM_METAL_GFX_SURFACE_DEFAULT;
	}

	(void)pm_metal_input_pointer_lock(surf);
}

static void
UpdateShiftStatus(int pressed, unsigned char key)
{
	int change;

	change = pressed ? 1 : -1;
	if (key == KEY_RSHIFT) {
		shiftdown += change;
	}
}

static void
PostKey(int pressed, unsigned char key)
{
	event_t event;

	UpdateShiftStatus(pressed, key);
	if (pressed) {
		event.type = ev_keydown;
		event.data1 = key;
		event.data2 = (shiftdown > 0 && key >= 'a' && key <= 'z')
				? (int)toupper((unsigned char)key)
				: (int)key;
		if (event.data1 != 0) {
			D_PostEvent(&event);
		}
	} else {
		event.type = ev_keyup;
		event.data1 = key;
		event.data2 = 0;
		if (event.data1 != 0) {
			D_PostEvent(&event);
		}
	}
}

static void
DrainKeys(void)
{
	int pressed;
	unsigned char key;

	while (DG_GetKey(&pressed, &key)) {
		PostKey(pressed, key);
		if (!pressed) {
			break;
		}
	}
}

static void
DrainMouse(void)
{
	pm_metal_input_pointer_t ev;
	event_t                  event;
	int                      sum_dx;
	int                      n;
	int                      buttons;
	int                      prev;
	int                      got;

	if (!usemouse) {
		return;
	}

	sum_dx = 0;
	n      = 0;
	got    = 0;
	buttons = s_mouse_buttons;

	while (pm_metal_input_poll_pointer(
		       (uint32_t)(uintptr_t)&ev) != 0) {
		got = 1;
		prev = s_mouse_buttons;
		buttons = (int)(ev.buttons & 7u);
		s_mouse_buttons = buttons;

		/* Click-to-capture after Esc unlock. */
		if (pm_metal_input_pointer_locked() == 0
		    && (buttons & 1) != 0 && (prev & 1) == 0) {
			MetalDoomEnsurePointerLock();
		}

		if ((ev.flags & PM_METAL_INPUT_PTR_WHEEL) != 0) {
			continue;
		}

		sum_dx += MetalDoomScaleMouse(ev.dx);
		n++;
		(void)ev.dy; /* novert */
	}

	if (!got) {
		return;
	}

	if (sum_dx > METAL_DOOM_MOUSE_MAX) {
		sum_dx = METAL_DOOM_MOUSE_MAX;
	}
	if (sum_dx < -METAL_DOOM_MOUSE_MAX) {
		sum_dx = -METAL_DOOM_MOUSE_MAX;
	}

	/*
	 * One ev_mouse per tick — G_Responder overwrites mousex, so posting
	 * per-SYN would keep only the last 1–2 px delta (felt frozen).
	 */
	event.type = ev_mouse;
	event.data1 = buttons;
	event.data2 = sum_dx;
	event.data3 = 0;
	event.data4 = 0;
	(void)n;
	D_PostEvent(&event);
}

void
I_InitInput(void)
{
	MetalDoomApplyBindings();
	s_mouse_buttons = 0;
	shiftdown = 0;
}

void
I_GetEvent(void)
{
	DrainKeys();
	DrainMouse();
}
