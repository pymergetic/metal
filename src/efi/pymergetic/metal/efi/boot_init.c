/** @file
  Boot harvest, floor tree, seeded init (no auto-tests), manual test suite.
  (impl: efi)
**/
#include <pymergetic/metal/efi/boot.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/io/io.h>
#include <pymergetic/metal/net/net_ops.h>
#include <pymergetic/metal/audio/audio_ops.h>
#include <pymergetic/metal/console/console.h>
#include <pymergetic/metal/blk/blk.h>
#include <pymergetic/metal/gfx/gfx.h>
#include <pymergetic/metal/ui/ui.h>
#include <pymergetic/metal/shell/shell.h>
#include <pymergetic/metal/wasm/wasm.h>
#include <pymergetic/metal/esp/esp.h>
#include <coro/coro.h>
#include <task/task.h>
#include <time/time.h>
#include <run/run.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>

typedef enum {
  BOOT_GFX = 0,
  BOOT_UI,
  BOOT_WASM,
  BOOT_SHELL,
  BOOT_READY,
  BOOT_PUMP_POLL,
  BOOT_PUMP_SLEEP,
  BOOT_SHUTDOWN
} metal_boot_step_t;

typedef struct {
  pm_metal_coro_t    coro;
  metal_boot_step_t  step;
} metal_boot_init_t;

typedef struct {
  CONST CHAR8  *mod;
  CONST CHAR8  *ok;
  CONST CHAR8  *fail;
  UINT32        max_ms;
} metal_boot_proof_t;

STATIC CONST metal_boot_proof_t  mProofs[] = {
  { "async_sleep", "metal-async: sleep ok", "metal-async: sleep fail", 5000 },
  { "async_fs",    "metal-async: fs ok",    "metal-async: fs fail",    5000 },
  { "async_time",  "metal-async: time ok",  "metal-async: time fail",  5000 },
  { "async_net",   "metal-async: net ok",   "metal-async: net fail",  15000 },
  { "async_audio", "metal-async: audio ok", "metal-async: audio fail", 15000 },
};

STATIC
VOID
BootTreeDtLine (
  CONST CHAR8         *prefix,
  CONST CHAR8         *class_name,
  pm_metal_io_class_t  class
  )
{
  CONST pm_metal_io_node_t  *n;
  CONST CHAR8               *backend;

  n       = pm_metal_io_dt_lookup (class);
  backend = (n != NULL && n->backend != NULL) ? n->backend : "?";
  pm_metal_logf ("%a%a/%a", prefix, class_name, backend);
}

STATIC
pm_metal_status_t
MetalBootInitCoro (
  pm_metal_coro_t  *self
  )
{
  metal_boot_init_t  *s;

  s = (metal_boot_init_t *)self;
  for (;;) {
    switch (s->step) {
      case BOOT_GFX:
        if (pm_metal_gfx_init () != 0) {
          pm_metal_log ("|   +-- gfx      FAIL");
          return PM_METAL_ERROR;
        }

        pm_metal_log ("|   +-- gfx      ok");
        s->step = BOOT_UI;
        continue;

      case BOOT_UI:
        if (pm_metal_ui_console_shell () != 0) {
          pm_metal_log ("|   +-- ui       FAIL");
          return PM_METAL_ERROR;
        }

        pm_metal_log_attach_ui ();
        pm_metal_log_boot_complete ();
        pm_metal_log ("|   +-- ui       ok");
        s->step = BOOT_WASM;
        continue;

      case BOOT_WASM:
        if (pm_metal_wasm_init () != 0) {
          pm_metal_log ("|   +-- wasm     FAIL");
          return PM_METAL_ERROR;
        }

        pm_metal_log ("|   +-- wasm     ok");
        s->step = BOOT_SHELL;
        continue;

      case BOOT_SHELL:
        if (pm_metal_shell_init () != 0) {
          pm_metal_log ("|   `-- shell    FAIL");
          return PM_METAL_ERROR;
        }

        pm_metal_log ("|   `-- shell    ready");
        s->step = BOOT_READY;
        continue;

      case BOOT_READY:
        pm_metal_log ("metal-boot: ready");
        pm_metal_log ("");
        pm_metal_log ("type help - test runs proofs");
        s->step = BOOT_PUMP_POLL;
        continue;

      case BOOT_PUMP_POLL:
        if (pm_metal_shell_poll () != 0) {
          s->step = BOOT_SHUTDOWN;
          continue;
        }

        s->step = BOOT_PUMP_SLEEP;
        return pm_metal_await (self, pm_metal_sleep (16));

      case BOOT_PUMP_SLEEP:
        s->step = BOOT_PUMP_POLL;
        continue;

      case BOOT_SHUTDOWN:
        pm_metal_boot_shutdown ();
        return PM_METAL_DONE;

      default:
        return PM_METAL_ERROR;
    }
  }
}

void
pm_metal_boot_print_floor_tree (
  uint64_t  claim_mib,
  uint64_t  map_bytes,
  uint64_t  hole_mib,
  uint64_t  heap_bytes,
  uint64_t  stack_kib,
  unsigned  n_cpus
  )
{
  UINTN  i;

  pm_metal_log ("");
  pm_metal_log ("pymergetic metal");
  pm_metal_log ("|");
  pm_metal_logf ("+-- mem          %Lu MiB claimed", claim_mib);
  if (map_bytes < 1024 * 1024) {
    pm_metal_logf ("|   +-- MAP      %Lu KiB", map_bytes / 1024);
  } else {
    pm_metal_logf ("|   +-- MAP      %Lu MiB", map_bytes / (1024 * 1024));
  }

  for (i = 0; i < n_cpus; i++) {
    pm_metal_logf (
      "|   |   %a cpu%u   %Lu KiB stack",
      (i + 1 < n_cpus) ? "+--" : "`--",
      (UINT32)i,
      stack_kib
      );
  }

  pm_metal_logf ("|   +-- HOLE     %Lu MiB", hole_mib);
  if (heap_bytes < 1024 * 1024) {
    pm_metal_logf ("|   `-- HEAP     %Lu KiB", heap_bytes / 1024);
  } else {
    pm_metal_logf (
      "|   `-- HEAP     %Lu MiB",
      heap_bytes / (1024 * 1024)
      );
  }

  pm_metal_logf ("+-- cpu          %u runners", n_cpus);
  pm_metal_log ("+-- devices");
  BootTreeDtLine ("|   +-- ", "time", PM_METAL_IO_TIME);
  BootTreeDtLine ("|   +-- ", "gfx", PM_METAL_IO_GFX);
  BootTreeDtLine ("|   +-- ", "fs", PM_METAL_IO_FS);
  BootTreeDtLine ("|   +-- ", "input", PM_METAL_IO_INPUT);
  BootTreeDtLine ("|   +-- ", "stream", PM_METAL_IO_STREAM);
  BootTreeDtLine ("|   +-- ", "net", PM_METAL_IO_NET);
  BootTreeDtLine ("|   +-- ", "audio", PM_METAL_IO_AUDIO);
  BootTreeDtLine ("|   +-- ", "random", PM_METAL_IO_RANDOM);
  if (pm_metal_console_ready ()) {
    pm_metal_log ("|   +-- console/virtio-console");
  } else {
    pm_metal_log ("|   +-- console/com1");
  }

  if (pm_metal_blk_ready ()) {
    pm_metal_logf (
      "|   `-- blk/virtio-blk  %Lu sectors",
      pm_metal_blk_capacity_sectors ()
      );
  } else {
    pm_metal_log ("|   `-- blk/none");
  }

  pm_metal_log ("");
}

int
pm_metal_boot_harvest_devices (
  VOID
  )
{
  STATIC CONST pm_metal_io_node_t  TimeNode = {
    PM_METAL_IO_TIME, "tsc", 0
  };
  STATIC CONST pm_metal_io_node_t  GfxNode = {
    PM_METAL_IO_GFX, "framebuffer", 0
  };
  STATIC CONST pm_metal_io_node_t  FsNode = {
    PM_METAL_IO_FS, "esp", 0
  };
  STATIC CONST pm_metal_io_node_t  InputNode = {
    PM_METAL_IO_INPUT, "ps2+com1", 0
  };
  STATIC CONST pm_metal_io_node_t  StreamNode = {
    PM_METAL_IO_STREAM, "uart+ui", 0
  };
  STATIC CONST pm_metal_io_node_t  RandomNode = {
    PM_METAL_IO_RANDOM, "efi-or-weak", 0
  };

  (VOID)pm_metal_io_dt_register (&TimeNode);
  (VOID)pm_metal_io_dt_register (&GfxNode);
  (VOID)pm_metal_io_dt_register (&FsNode);
  (VOID)pm_metal_io_dt_register (&InputNode);
  (VOID)pm_metal_io_dt_register (&StreamNode);
  (VOID)pm_metal_io_dt_register (&RandomNode);

  if (pm_metal_net_virtio_probe () != 0) {
    pm_metal_net_null_install ();
    {
      STATIC CONST pm_metal_io_node_t  NetNode = {
        PM_METAL_IO_NET, "null", 0
      };

      (VOID)pm_metal_io_dt_register (&NetNode);
    }
  }

  if (pm_metal_audio_virtio_probe () != 0) {
    pm_metal_audio_null_install ();
    {
      STATIC CONST pm_metal_io_node_t  AudioNode = {
        PM_METAL_IO_AUDIO, "null", 0
      };

      (VOID)pm_metal_io_dt_register (&AudioNode);
    }
  }

  (VOID)pm_metal_console_virtio_probe ();
  (VOID)pm_metal_blk_virtio_probe ();
  return 0;
}

int
pm_metal_boot_seed_init (
  VOID
  )
{
  metal_boot_init_t  *init;
  pm_metal_task_t    *task;

  init = (metal_boot_init_t *)pm_metal_coro (
                                MetalBootInitCoro,
                                sizeof (*init)
                                );
  if (init == NULL) {
    return -1;
  }

  init->step = BOOT_GFX;
  pm_metal_log ("+-- ebs          ok");
  pm_metal_log ("`-- init");
  task = pm_metal_task_new (&init->coro);
  if (task == NULL) {
    return -1;
  }

  /* Exit / init failure posts STOP so run_enter returns → ResetSystem. */
  task->stop_on_done = 1;

  if (pm_metal_task_spawn (task, 0) != 0) {
    return -1;
  }

  return 0;
}

void
pm_metal_boot_shutdown (
  VOID
  )
{
  /*
   * Reverse of pool init: shell (stop pump) → wasm → ui → gfx.
   * Caller returns DONE; stop_on_done drains every looper.
   */
  pm_metal_log ("");
  pm_metal_log ("metal-boot: shutdown");
  pm_metal_log ("`-- fini");
  pm_metal_log ("|   +-- shell    ok");

  pm_metal_wasm_fini ();
  pm_metal_log ("|   +-- wasm     ok");

  pm_metal_ui_fini ();
  pm_metal_log ("|   +-- ui       ok");

  pm_metal_gfx_fini ();
  pm_metal_log ("|   `-- gfx      ok");
  pm_metal_log ("metal-boot: halt");
}

int
pm_metal_boot_run_tests (
  VOID
  )
{
  UINT32  i;
  INT32   rc;
  INT32   st;
  UINT64  deadline;

  if (!pm_metal_wasm_ready ()) {
    pm_metal_log ("metal-test: wasm not ready");
    return -1;
  }

  /*
   * Floor preloads ESP→RAM before EBS. If that was skipped, seed the
   * known marker so async_fs still works post-EBS.
   */
  {
    STATIC CONST UINT8  FsMarker[] = "metal-async-fs\n";

    (VOID)pm_metal_esp_cache_put (
            "mods/tests/async_fs.txt",
            FsMarker,
            sizeof (FsMarker) - 1
            );
  }

  pm_metal_log ("metal-test: begin");
  pm_metal_wasm_set_stdout_tab (pm_metal_ui_console_handle ());

  rc = pm_metal_wasm_run_mod ("hello");
  if (rc == 0) {
    rc = pm_metal_wasm_run_mod ("hello");
  }

  if (rc != 0) {
    pm_metal_log ("metal-wasm: t0_hello fail");
    pm_metal_wasm_set_stdout_tab (PM_METAL_UI_HANDLE_INVALID);
    return -1;
  }

  pm_metal_log ("metal-wasm: t0_hello ok");

  for (i = 0; i < (sizeof (mProofs) / sizeof (mProofs[0])); i++) {
    rc = pm_metal_wasm_run_mod (mProofs[i].mod);
    if (rc != 0) {
      pm_metal_log (mProofs[i].fail);
      pm_metal_wasm_set_stdout_tab (PM_METAL_UI_HANDLE_INVALID);
      return -1;
    }

    if (pm_metal_wasm_session_active ()) {
      deadline = pm_metal_time_mono_us ()
                   + (UINT64)mProofs[i].max_ms * 1000u;
      for (;;) {
        INT32  pr;

        pr = pm_metal_wasm_session_poll (&st);
        if (pr != 0) {
          rc = (pr > 0 && st == (INT32)PM_METAL_DONE) ? 0 : -1;
          break;
        }

        if (pm_metal_time_mono_us () >= deadline) {
          pm_metal_log (mProofs[i].fail);
          pm_metal_wasm_set_stdout_tab (PM_METAL_UI_HANDLE_INVALID);
          return -1;
        }

        /* Yield to the pool while waiting. */
        pm_metal_run_poll_all ();
      }

      if (rc != 0) {
        pm_metal_log (mProofs[i].fail);
        pm_metal_wasm_set_stdout_tab (PM_METAL_UI_HANDLE_INVALID);
        return -1;
      }
    }

    pm_metal_log (mProofs[i].ok);
  }

  pm_metal_wasm_set_stdout_tab (PM_METAL_UI_HANDLE_INVALID);
  pm_metal_log ("metal-test: ok");
  return 0;
}
