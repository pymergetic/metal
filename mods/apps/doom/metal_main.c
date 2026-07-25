/*
 * Metal async entry for doomgeneric — stackless doom_run (registered cmd).
 *
 * Stem:
 *   mkdir saves → preload IWAD → Create →
 *   loop(Tick → save I/O → present → phase-locked sleep)
 *   Audio is fire-and-forget queue; shell/session audio_poll recycles TX.
 *
 * Pace is absolute mono phase at METAL_DOOM_FRAME_HZ (default 35 = TICRATE).
 * A 60 Hz grid was half-locking to 30 fps whenever Tick+present wall time
 * exceeded one 16.7 ms slot. Avoid pm_metal_async_frame — wamrc AOT #GPs.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../external/doomgeneric/doomgeneric/doomgeneric.h"

#include "metal_doom.h"

#include "pymergetic/metal/runtime/async/async.h"
#include "pymergetic/metal/runtime/mem/mem.h"
#include "pymergetic/metal/guest/mod/mod.h"
#include "pymergetic/metal/fs/fs.h"
#include "pymergetic/metal/shell/shell/shell.h"

extern int singletics;

#ifndef METAL_DOOM_MAX_TICKS
#define METAL_DOOM_MAX_TICKS 0
#endif

/* Match vanilla TICRATE so one present ≈ one game tic (not host 60 Hz UI). */
#ifndef METAL_DOOM_FRAME_HZ
#define METAL_DOOM_FRAME_HZ 35u
#endif

int system(const char *cmd)
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
  ST_AFTER_IO, /* continue after_tick after save write */
  ST_PRESENT_WAIT
};

#ifndef METAL_DOOM_WAD_WAIT_TRIES
#define METAL_DOOM_WAD_WAIT_TRIES 30u /* ~30s for background HTTP seed */
#endif

typedef struct {
  uint32_t       step;
  uint32_t       ticks;
  uint32_t       aw;
  uint32_t       wad_len;
  uint32_t       wad_got;
  uint32_t       wad_tries;
  pm_metal_ptr_t wad_mem; /* host TLSF — durable IWAD */
  uint32_t       save_len;
  pm_metal_ptr_t save_mem;
  uint8_t       *save_buf;
  uint64_t       pace_next_us; /* absolute mono deadline for next Tick */
} doom_state_t;

static char *g_argv_storage[8];
static char  g_arg0[]     = "doom";
static char  g_arg_iwad[] = "-iwad";
static char  g_arg_path[] = METAL_DOOM_IWAD;

static int32_t doom_await(doom_state_t *s, int32_t self_h, uint32_t aw, uint32_t next_step)
{
  if (aw == PM_METAL_ASYNC_HANDLE_INVALID) {
    return PM_METAL_ERROR;
  }

  s->aw   = aw;
  s->step = next_step;
  return pm_metal_async_await(self_h, aw);
}

/* Separate func so AOT doesn't emit the import inside giant doom_run. */
__attribute__((noinline)) static uint32_t doom_fs_read_mem_async(const char    *path,
                                                                 pm_metal_ptr_t dest,
                                                                 uint32_t       len)
{
  return pm_metal_fs_read_mem_async(path, dest, len);
}

__attribute__((noinline)) static uint32_t doom_fs_write_mem_async(const char    *path,
                                                                  pm_metal_ptr_t src,
                                                                  uint32_t       len)
{
  return pm_metal_fs_write_mem_async(path, src, len);
}

/*
 * Keep doomgeneric_Create out of the giant guest_step switch — wamrc AOT
 * #GPs when Create (and its frame) is emitted inside that function.
 */
__attribute__((noinline)) static int32_t doom_do_create(doom_state_t *s, int32_t self_h)
{
  int argc;

  singletics        = 1;
  g_argv_storage[0] = g_arg0;
  g_argv_storage[1] = g_arg_iwad;
  g_argv_storage[2] = g_arg_path;
  argc              = 3;
  pm_metal_shell_log("metal-doom: create");
  doomgeneric_Create(argc, g_argv_storage);
  /*
	 * libc block-buffers printf when stdout is not a TTY — Create's
	 * D_CheckNetGame / I_InitGraphics lines otherwise dump on exit and
	 * look like a New Game crash. Flush so serial matches reality.
	 */
  fflush(stdout);
  fflush(stderr);
  pm_metal_shell_log("metal-doom: create done; pace 35 Hz");
  s->ticks        = 0;
  s->pace_next_us = 0;
  s->step         = ST_TICK;
  return doom_await(s, self_h, pm_metal_async_yield(), ST_TICK);
}

/**
 * Pace to METAL_DOOM_FRAME_HZ from a running deadline.
 * If late, resync phase — but MUST park (yield), not sleep(0).
 * sleep(0) is eager DONE inside MetalTaskStepLocked → same-pump cascades
 * another Tick/present/pace (burst frames → audio/present thrash → hop).
 */
static int32_t doom_pace(doom_state_t *s, int32_t self_h)
{
  uint64_t now;
  uint64_t period;
  uint64_t target;

  period = 1000000u / (uint64_t)METAL_DOOM_FRAME_HZ;
  if (period == 0) {
    period = 28571u;
  }

  now = pm_metal_async_mono_us();
  if (s->pace_next_us == 0 || now >= s->pace_next_us) {
    /* Late / first — one fairness park, then Tick on next pump. */
    s->pace_next_us = now + period;
    return doom_await(s, self_h, pm_metal_async_yield(), ST_TICK);
  }

  target          = s->pace_next_us;
  s->pace_next_us = target + period;
  return doom_await(s, self_h, pm_metal_async_sleep_until_us(target), ST_TICK);
}

static int32_t doom_check_quit(void)
{
  if (!metal_doom_quit_requested()) {
    return 0;
  }

  metal_doom_wad_release();

  if (metal_doom_quit_code() == 0) {
    pm_metal_shell_log("metal-doom: quit");
    return PM_METAL_DONE;
  }

  pm_metal_shell_log("metal-doom: error exit");
  return PM_METAL_ERROR;
}

static int32_t doom_after_tick(doom_state_t *s, int32_t self_h)
{
  metal_doom_io_kind_t io;
  uint32_t             surf;

  io = metal_doom_io_pending();
  if (io == METAL_DOOM_IO_SAVE_READ) {
    pm_metal_shell_log("metal-doom: save size");
    return doom_await(s, self_h, pm_metal_fs_size_async(metal_doom_io_path()), ST_SAVE_SIZE_WAIT);
  }

  if (io == METAL_DOOM_IO_SAVE_WRITE) {
    s->save_len = metal_doom_io_len();
    s->save_mem = pm_metal_mem_alloc((size_t)s->save_len, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
    if (s->save_mem == NULL) {
      pm_metal_shell_log("metal-doom: save mem alloc fail");
      metal_doom_io_clear();
      return doom_pace(s, self_h);
    }

    if (pm_metal_mem_copy_in(s->save_mem, PM_METAL_FS_IO_PTR(metal_doom_io_buf()), s->save_len) !=
        0) {
      pm_metal_shell_log("metal-doom: save copy_in fail");
      pm_metal_mem_free(s->save_mem);
      s->save_mem = NULL;
      metal_doom_io_clear();
      return doom_pace(s, self_h);
    }

    pm_metal_shell_log("metal-doom: save write");
    return doom_await(s,
                      self_h,
                      doom_fs_write_mem_async(metal_doom_io_path(), s->save_mem, s->save_len),
                      ST_SAVE_WRITE_WAIT);
  }

  surf = metal_doom_present_surface();
  if (surf != 0) {
    metal_doom_clear_present();
    return doom_await(s, self_h, pm_metal_async_present(surf), ST_PRESENT_WAIT);
  }

  return doom_pace(s, self_h);
}

pm_metal_status_t doom_run(pm_metal_async_handle_t self_h)
{
  doom_state_t *s;
  int32_t       q;

  s = pm_metal_async_coro_frame(self_h, (uint32_t)sizeof(*s));
  if (s == NULL) {
    return PM_METAL_ERROR;
  }

  /*
	 * Mod instance stays loaded across processes ("still ready"). Quit is
	 * a wasm static — clear on a fresh stem so tab doom works again.
	 */
  if (s->step == ST_MKDIR) {
    metal_doom_clear_quit();
    metal_doom_wad_release();
  }

  q = doom_check_quit();
  if (q != 0) {
    return q;
  }

  switch (s->step) {
  case ST_MKDIR:
    pm_metal_shell_log("metal-doom: save mkdir");
    return doom_await(s, self_h, pm_metal_fs_mkdir_async(METAL_DOOM_SAVE_DIR), ST_MKDIR_WAIT);

  case ST_MKDIR_WAIT:
    /* mkdir may fail if exists — ignore result. */
    (void)pm_metal_fs_result(self_h);
    /* No switch fallthrough — wamrc AOT #GPs on some edges. */
    return doom_await(s, self_h, pm_metal_async_sleep(0), ST_SIZE);

  case ST_SIZE:
    if (s->wad_tries == 0u) {
      pm_metal_shell_log("metal-doom: wad size");
    }

    return doom_await(s, self_h, pm_metal_fs_size_async(METAL_DOOM_IWAD), ST_SIZE_WAIT);

  case ST_SIZE_WAIT:
    s->wad_len = pm_metal_fs_result(self_h);
    if (s->wad_len < 1000u) {
      /*
			 * Common on iron: doom.wasm preloaded from ESP, IWAD
			 * still HTTP-seeding in net-life. Wait before failing.
			 */
      if (s->wad_tries < METAL_DOOM_WAD_WAIT_TRIES) {
        if (s->wad_tries == 0u) {
          pm_metal_shell_log("metal-doom: wad missing — waiting ESP/HTTP seed");
        }

        s->wad_tries++;
        return doom_await(s, self_h, pm_metal_async_sleep(1000), ST_SIZE);
      }

      pm_metal_shell_log("metal-doom: wad missing (mods/apps/doom/doom1.wad via ESP or "
                         "http://<next-server|:gw>:8080/)");
      return PM_METAL_ERROR;
    }

    return doom_await(s, self_h, pm_metal_async_sleep(0), ST_ALLOC);

  case ST_ALLOC:
    /* Durable host buffer for await-spanning FS read. */
    s->wad_mem = pm_metal_mem_alloc((size_t)s->wad_len, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
    if (s->wad_mem == NULL) {
      pm_metal_shell_log("metal-doom: wad mem alloc fail");
      return PM_METAL_ERROR;
    }

    pm_metal_shell_log("metal-doom: wad read");
    return doom_await(
      s, self_h, doom_fs_read_mem_async(METAL_DOOM_IWAD, s->wad_mem, s->wad_len), ST_READ_WAIT);

  case ST_READ_WAIT:
    s->wad_got = pm_metal_fs_result(self_h);
    if (s->wad_got != s->wad_len) {
      pm_metal_shell_log("metal-doom: wad read fail");
      pm_metal_mem_free(s->wad_mem);
      s->wad_mem = NULL;
      return PM_METAL_ERROR;
    }

    /* Keep IWAD on host TLSF — W_Read copies lumps into zone. */
    if (metal_doom_wad_install(s->wad_mem, s->wad_len) != 0) {
      pm_metal_mem_free(s->wad_mem);
      s->wad_mem = NULL;
      return PM_METAL_ERROR;
    }
    s->wad_mem = NULL; /* owned by w_file_metal */

    /*
		 * Fresh step into Create — fallthrough + doomgeneric_Create in
		 * the same AOT frame after a multi-MiB host read has #GP'd.
		 */
    return doom_await(s, self_h, pm_metal_async_sleep(0), ST_CREATE);

  case ST_CREATE:
    return doom_do_create(s, self_h);

  case ST_TICK:
    doomgeneric_Tick();
    s->ticks++;

    q = doom_check_quit();
    if (q != 0) {
      return q;
    }

#if METAL_DOOM_MAX_TICKS > 0
    if (s->ticks >= (uint32_t)METAL_DOOM_MAX_TICKS) {
      metal_doom_wad_release();
      pm_metal_shell_log("metal-doom: ok");
      return PM_METAL_DONE;
    }
#endif

    return doom_after_tick(s, self_h);

  case ST_SAVE_SIZE_WAIT:
    s->save_len = pm_metal_fs_result(self_h);
    if (s->save_len == 0 || s->save_len > METAL_DOOM_SAVEGAME_SIZE) {
      pm_metal_shell_log("metal-doom: save missing");
      metal_doom_io_abort_load();
      return doom_pace(s, self_h);
    }

    s->save_mem = pm_metal_mem_alloc((size_t)s->save_len, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
    if (s->save_mem == NULL) {
      metal_doom_io_clear();
      return PM_METAL_ERROR;
    }

    return doom_await(s,
                      self_h,
                      doom_fs_read_mem_async(metal_doom_io_path(), s->save_mem, s->save_len),
                      ST_SAVE_READ_WAIT);

  case ST_SAVE_READ_WAIT:
    if (pm_metal_fs_result(self_h) != s->save_len) {
      pm_metal_shell_log("metal-doom: save read fail");
      pm_metal_mem_free(s->save_mem);
      s->save_mem = NULL;
      metal_doom_io_abort_load();
      return doom_pace(s, self_h);
    }

    s->save_buf = (uint8_t *)malloc((size_t)s->save_len);
    if (s->save_buf == NULL) {
      pm_metal_mem_free(s->save_mem);
      s->save_mem = NULL;
      metal_doom_io_clear();
      return PM_METAL_ERROR;
    }

    if (pm_metal_mem_copy_out(s->save_mem, PM_METAL_FS_IO_PTR(s->save_buf), s->save_len) != 0) {
      pm_metal_shell_log("metal-doom: save copy_out fail");
      free(s->save_buf);
      s->save_buf = NULL;
      pm_metal_mem_free(s->save_mem);
      s->save_mem = NULL;
      metal_doom_io_abort_load();
      return doom_pace(s, self_h);
    }

    pm_metal_mem_free(s->save_mem);
    s->save_mem = NULL;
    metal_doom_io_install_read(s->save_buf, s->save_len);
    s->save_buf = NULL; /* owned by save glue */
    s->step     = ST_TICK;
    return doom_await(s, self_h, pm_metal_async_yield(), ST_TICK);

  case ST_SAVE_WRITE_WAIT:
    if (pm_metal_fs_result(self_h) != s->save_len) {
      pm_metal_shell_log("metal-doom: save write fail");
    } else {
      pm_metal_shell_log("metal-doom: save ok");
    }

    if (s->save_mem != NULL) {
      pm_metal_mem_free(s->save_mem);
      s->save_mem = NULL;
    }

    metal_doom_io_clear();
    return doom_await(s, self_h, pm_metal_async_yield(), ST_AFTER_IO);

  case ST_AFTER_IO:
    return doom_after_tick(s, self_h);

  case ST_PRESENT_WAIT:
    return doom_pace(s, self_h);

  default:
    return PM_METAL_ERROR;
  }
}

int32_t pm_metal_mod_on_load(void)
{
  /* Real static state (zone heap, screen buffer, saves) — every run
   * needs its own fresh instance, not the shared "instance 0". */
  if (pm_metal_mod_set_capability(PM_METAL_MOD_CAP_MULTI) != 0) {
    return -1;
  }

  if (pm_metal_mod_register_func("run", "doom_run") != 0) {
    return -1;
  }

  if (pm_metal_mod_register_cmd("doom", "run", "Doom") != 0) {
    return -1;
  }

  return 0;
}

int32_t pm_metal_mod_on_unload(void)
{
  metal_doom_wad_release();
  return 0;
}

int main(void)
{
  return 0;
}
