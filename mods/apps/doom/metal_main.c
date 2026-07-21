/*
 * Metal async entry for doomgeneric — stackless pm_metal_guest_step.
 *
 * Headless verify: METAL_DOOM_MAX_TICKS (compile-time) auto-quits after N ticks.
 * Interactive: 60 fps deadline pacing (sleep only the remainder of the frame).
 */
#include <stddef.h>
#include <stdint.h>

#include "../../../external/doomgeneric/doomgeneric/doomgeneric.h"

#include "pymergetic/metal/async/async.h"
#include "pymergetic/metal/shell/shell.h"

/* From d_loop.c — avoid d_loop.h (pulls doomtype.h → <strings.h>). */
extern int singletics;

#ifndef METAL_DOOM_MAX_TICKS
#define METAL_DOOM_MAX_TICKS 0
#endif

/* Target frame period — 60 Hz. */
#ifndef METAL_DOOM_FRAME_MS
#define METAL_DOOM_FRAME_MS 16u
#endif

/* Satisfy --allow-undefined pull-in of libc system(3) as env.system. */
int
system(const char *cmd)
{
	(void)cmd;
	return -1;
}

#ifndef METAL_DOOM_IWAD
#define METAL_DOOM_IWAD "/mods/apps/doom/doom1.wad"
#endif

typedef struct {
	uint32_t step;
	uint32_t ticks;
	uint32_t aw;
	uint32_t next_ms;
} doom_state_t;

static char *g_argv_storage[8];
static char g_arg0[] = "doom";
static char g_arg_iwad[] = "-iwad";
static char g_arg_path[] = METAL_DOOM_IWAD;

int32_t
pm_metal_guest_step(int32_t self_h)
{
	doom_state_t *s;

	s = (doom_state_t *)(uintptr_t)pm_metal_async_coro_state(
		(pm_metal_async_handle_t)self_h);
	if (s == NULL) {
		return PM_METAL_ERROR;
	}

	switch (s->step) {
	case 0: {
		int argc;

		singletics = 1;
		g_argv_storage[0] = g_arg0;
		g_argv_storage[1] = g_arg_iwad;
		g_argv_storage[2] = g_arg_path;
		argc = 3;
		pm_metal_shell_log("metal-doom: create");
		doomgeneric_Create(argc, g_argv_storage);
		pm_metal_shell_log("metal-doom: create done");
		s->ticks = 0;
		s->next_ms = 0;
		s->step = 1;
		s->aw = pm_metal_async_sleep(0);
		if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
			return PM_METAL_ERROR;
		}
		return pm_metal_async_await((pm_metal_async_handle_t)self_h, s->aw);
	}

	case 1: {
		uint32_t now;
		uint32_t delay;

		doomgeneric_Tick();
		s->ticks++;
#if METAL_DOOM_MAX_TICKS > 0
		if (s->ticks >= (uint32_t)METAL_DOOM_MAX_TICKS) {
			pm_metal_shell_log("metal-doom: ok");
			return PM_METAL_DONE;
		}
#endif
		/*
		 * Frame-deadline pacing toward 60 fps:
		 *   work, then sleep only until next_ms (0 if we slipped).
		 */
		now = (uint32_t)pm_metal_async_mono_ms();
		if (s->next_ms == 0) {
			s->next_ms = now + METAL_DOOM_FRAME_MS;
		} else {
			s->next_ms += METAL_DOOM_FRAME_MS;
			/* More than 2 frames late — resync (don't spiral). */
			if ((int32_t)(now - s->next_ms)
			    > (int32_t)(2u * METAL_DOOM_FRAME_MS)) {
				s->next_ms = now + METAL_DOOM_FRAME_MS;
			}
		}
		if ((int32_t)(s->next_ms - now) > 0) {
			delay = s->next_ms - now;
		} else {
			delay = 0;
		}
		s->aw = pm_metal_async_sleep(delay);
		if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
			return PM_METAL_ERROR;
		}
		return pm_metal_async_await((pm_metal_async_handle_t)self_h, s->aw);
	}

	default:
		return PM_METAL_ERROR;
	}
}

int
main(void)
{
	/* Sync entry unused when host detects pm_metal_guest_step. */
	return 0;
}
