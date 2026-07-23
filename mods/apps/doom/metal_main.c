/*
 * Metal async entry for doomgeneric — stackless pm_metal_guest_step.
 *
 * Stem:
 *   mkdir saves → preload IWAD → Create →
 *   loop(Tick → save I/O → audio drain → present → phase-locked 60 Hz sleep)
 *
 * Pace uses absolute mono phase (same 60 Hz grid as host frame clock).
 * Avoid pm_metal_async_frame in this guest — wamrc AOT #GPs with that import.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../../external/doomgeneric/doomgeneric/doomgeneric.h"

#include "metal_doom.h"

#include "pymergetic/metal/runtime/async/async.h"
#include "pymergetic/metal/fs/fs.h"
#include "pymergetic/metal/dev/audio/audio.h"
#include "pymergetic/metal/shell/shell/shell.h"

extern int singletics;

#ifndef METAL_DOOM_MAX_TICKS
#define METAL_DOOM_MAX_TICKS 0
#endif

#ifndef METAL_DOOM_FRAME_HZ
#define METAL_DOOM_FRAME_HZ 60u
#endif

int
system(const char *cmd)
{
	(void)cmd;
	return -1;
}

enum {
	ST_MKDIR = 0,
	ST_MKDIR_WAIT,
	ST_SIZE,
	ST_SIZE_WAIT,
	ST_ALLOC,
	ST_READ_WAIT,
	ST_CREATE,
	ST_TICK,
	ST_SAVE_SIZE_WAIT,
	ST_SAVE_READ_WAIT,
	ST_SAVE_WRITE_WAIT,
	ST_AUDIO_DRAIN,
	ST_PRESENT_WAIT
};

#ifndef METAL_DOOM_WAD_WAIT_TRIES
#define METAL_DOOM_WAD_WAIT_TRIES 30u /* ~30s for background HTTP seed */
#endif

typedef struct {
	uint32_t step;
	uint32_t ticks;
	uint32_t aw;
	uint32_t wad_len;
	uint32_t wad_got;
	uint32_t wad_tries;
	uint8_t *wad_buf;
	uint32_t save_len;
	uint8_t *save_buf;
} doom_state_t;

static char *g_argv_storage[8];
static char g_arg0[] = "doom";
static char g_arg_iwad[] = "-iwad";
static char g_arg_path[] = METAL_DOOM_IWAD;

static int32_t
doom_await(doom_state_t *s, int32_t self_h, uint32_t aw, uint32_t next_step)
{
	if (aw == PM_METAL_ASYNC_HANDLE_INVALID) {
		return PM_METAL_ERROR;
	}

	s->aw   = aw;
	s->step = next_step;
	return pm_metal_async_await((pm_metal_async_handle_t)self_h, aw);
}

/* Separate func so AOT doesn't emit the ($ii) import inside giant guest_step. */
__attribute__((noinline))
static uint32_t
doom_fs_read_async(const char *path, uint32_t dest, uint32_t len)
{
	return pm_metal_fs_read_async(path, dest, len);
}

/** Next boundary on the shared 60 Hz mono grid (matches host frame clock). */
static uint64_t
doom_next_frame_us(void)
{
	uint64_t now;
	uint64_t period;

	period = 1000000u / (uint64_t)METAL_DOOM_FRAME_HZ;
	if (period == 0) {
		period = 16667u;
	}

	now = pm_metal_async_mono_us();
	return (now / period + 1u) * period;
}

static int32_t
doom_pace(doom_state_t *s, int32_t self_h)
{
	return doom_await(s, self_h,
			  pm_metal_async_sleep_until_us(doom_next_frame_us()),
			  ST_TICK);
}

static int32_t
doom_check_quit(void)
{
	if (!metal_doom_quit_requested()) {
		return 0;
	}

	if (metal_doom_quit_code() == 0) {
		pm_metal_shell_log("metal-doom: quit");
		return PM_METAL_DONE;
	}

	pm_metal_shell_log("metal-doom: error exit");
	return PM_METAL_ERROR;
}

static int32_t
doom_after_tick(doom_state_t *s, int32_t self_h)
{
	metal_doom_io_kind_t io;
	uint32_t             surf;

	io = metal_doom_io_pending();
	if (io == METAL_DOOM_IO_SAVE_READ) {
		pm_metal_shell_log("metal-doom: save size");
		return doom_await(s, self_h,
				  pm_metal_fs_size_async(metal_doom_io_path()),
				  ST_SAVE_SIZE_WAIT);
	}

	if (io == METAL_DOOM_IO_SAVE_WRITE) {
		pm_metal_shell_log("metal-doom: save write");
		return doom_await(
			s, self_h,
			pm_metal_fs_write_async(metal_doom_io_path(),
						PM_METAL_FS_IO_PTR(
							metal_doom_io_buf()),
						metal_doom_io_len()),
			ST_SAVE_WRITE_WAIT);
	}

	if (metal_doom_audio_drain_pending()) {
		uint32_t aw;

		aw = pm_metal_audio_drain(metal_doom_audio_drain_stream(),
					  metal_doom_audio_drain_nbytes());
		metal_doom_audio_drain_clear();
		if (aw != PM_METAL_ASYNC_HANDLE_INVALID) {
			return doom_await(s, self_h, aw, ST_AUDIO_DRAIN);
		}
	}

	surf = metal_doom_present_surface();
	if (surf != 0) {
		metal_doom_clear_present();
		return doom_await(s, self_h, pm_metal_async_present(surf),
				  ST_PRESENT_WAIT);
	}

	return doom_pace(s, self_h);
}

int32_t
pm_metal_guest_step(int32_t self_h)
{
	doom_state_t *s;
	int32_t       q;

	s = (doom_state_t *)(uintptr_t)pm_metal_async_coro_state(
		(pm_metal_async_handle_t)self_h);
	if (s == NULL) {
		return PM_METAL_ERROR;
	}

	q = doom_check_quit();
	if (q != 0) {
		return q;
	}

	switch (s->step) {
	case ST_MKDIR:
		pm_metal_shell_log("metal-doom: save mkdir");
		return doom_await(s, self_h,
				  pm_metal_fs_mkdir_async(METAL_DOOM_SAVE_DIR),
				  ST_MKDIR_WAIT);

	case ST_MKDIR_WAIT:
		/* mkdir may fail if exists — ignore result. */
		(void)pm_metal_fs_result((pm_metal_async_handle_t)self_h);
		s->step = ST_SIZE;
		/* fallthrough */

	case ST_SIZE:
		if (s->wad_tries == 0u) {
			pm_metal_shell_log("metal-doom: wad size");
		}

		return doom_await(s, self_h,
				  pm_metal_fs_size_async(METAL_DOOM_IWAD),
				  ST_SIZE_WAIT);

	case ST_SIZE_WAIT:
		s->wad_len = pm_metal_fs_result((pm_metal_async_handle_t)self_h);
		if (s->wad_len < 1000u) {
			/*
			 * Common on iron: doom.wasm preloaded from ESP, IWAD
			 * still HTTP-seeding in net-life. Wait before failing.
			 */
			if (s->wad_tries < METAL_DOOM_WAD_WAIT_TRIES) {
				if (s->wad_tries == 0u) {
					pm_metal_shell_log(
						"metal-doom: wad missing — waiting ESP/HTTP seed");
				}

				s->wad_tries++;
				return doom_await(s, self_h,
						  pm_metal_async_sleep(1000),
						  ST_SIZE);
			}

			pm_metal_shell_log(
				"metal-doom: wad missing (mods/apps/doom/doom1.wad via ESP or http://<next-server|:gw>:8080/)");
			return PM_METAL_ERROR;
		}

		s->step = ST_ALLOC;
		/* fallthrough */

	case ST_ALLOC:
		s->wad_buf = (uint8_t *)malloc((size_t)s->wad_len);
		if (s->wad_buf == NULL) {
			pm_metal_shell_log("metal-doom: wad malloc fail");
			return PM_METAL_ERROR;
		}

		pm_metal_shell_log("metal-doom: wad read");
		return doom_await(
			s, self_h,
			doom_fs_read_async(METAL_DOOM_IWAD,
					   PM_METAL_FS_IO_PTR(s->wad_buf),
					   s->wad_len),
			ST_READ_WAIT);

	case ST_READ_WAIT:
		s->wad_got = pm_metal_fs_result((pm_metal_async_handle_t)self_h);
		if (s->wad_got != s->wad_len) {
			pm_metal_shell_log("metal-doom: wad read fail");
			free(s->wad_buf);
			s->wad_buf = NULL;
			return PM_METAL_ERROR;
		}

		if (metal_doom_wad_install(s->wad_buf, s->wad_len) != 0) {
			free(s->wad_buf);
			s->wad_buf = NULL;
			return PM_METAL_ERROR;
		}

		s->step = ST_CREATE;
		/* fallthrough */

	case ST_CREATE: {
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
		s->step  = ST_TICK;
		return doom_await(s, self_h, pm_metal_async_sleep(0), ST_TICK);
	}

	case ST_TICK:
		doomgeneric_Tick();
		s->ticks++;

		q = doom_check_quit();
		if (q != 0) {
			return q;
		}

#if METAL_DOOM_MAX_TICKS > 0
		if (s->ticks >= (uint32_t)METAL_DOOM_MAX_TICKS) {
			pm_metal_shell_log("metal-doom: ok");
			return PM_METAL_DONE;
		}
#endif

		return doom_after_tick(s, self_h);

	case ST_SAVE_SIZE_WAIT:
		s->save_len = pm_metal_fs_result((pm_metal_async_handle_t)self_h);
		if (s->save_len == 0 || s->save_len > METAL_DOOM_SAVEGAME_SIZE) {
			pm_metal_shell_log("metal-doom: save missing");
			metal_doom_io_abort_load();
			return doom_pace(s, self_h);
		}

		s->save_buf = (uint8_t *)malloc((size_t)s->save_len);
		if (s->save_buf == NULL) {
			metal_doom_io_clear();
			return PM_METAL_ERROR;
		}

		return doom_await(
			s, self_h,
			pm_metal_fs_read_async(metal_doom_io_path(),
					       PM_METAL_FS_IO_PTR(s->save_buf),
					       s->save_len),
			ST_SAVE_READ_WAIT);

	case ST_SAVE_READ_WAIT:
		if (pm_metal_fs_result((pm_metal_async_handle_t)self_h)
		    != s->save_len) {
			pm_metal_shell_log("metal-doom: save read fail");
			free(s->save_buf);
			s->save_buf = NULL;
			metal_doom_io_abort_load();
			return doom_pace(s, self_h);
		}

		metal_doom_io_install_read(s->save_buf, s->save_len);
		s->save_buf = NULL; /* owned by save glue */
		s->step = ST_TICK;
		return doom_await(s, self_h, pm_metal_async_sleep(0), ST_TICK);

	case ST_SAVE_WRITE_WAIT:
		if (pm_metal_fs_result((pm_metal_async_handle_t)self_h)
		    != metal_doom_io_len()) {
			pm_metal_shell_log("metal-doom: save write fail");
		} else {
			pm_metal_shell_log("metal-doom: save ok");
		}

		metal_doom_io_clear();
		s->step = ST_AUDIO_DRAIN;
		/* fallthrough */

	case ST_AUDIO_DRAIN:
		return doom_after_tick(s, self_h);

	case ST_PRESENT_WAIT:
		return doom_pace(s, self_h);

	default:
		return PM_METAL_ERROR;
	}
}

int
main(void)
{
	return 0;
}
