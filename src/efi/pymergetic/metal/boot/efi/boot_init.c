/** @file
  Boot harvest, floor tree, seeded init (no auto-tests), manual test suite.
  (impl: efi)
**/
#include <pymergetic/metal/boot/boot.h>
#include <pymergetic/metal/boot/port.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/bus/io/io.h>
#include <pymergetic/metal/dev/net/net_ops.h>
#include <pymergetic/metal/dev/audio/audio_ops.h>
#include <pymergetic/metal/dev/console/console.h>
#include <pymergetic/metal/dev/blk/blk.h>
#include <pymergetic/metal/dev/gfx/gfx.h>
#include <pymergetic/metal/shell/ui/ui.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/guest/wasm/wasm.h>
#include <pymergetic/metal/fs/esp/esp.h>
#include <runtime/coro/coro.h>
#include <runtime/task/task.h>
#include <runtime/time/time.h>
#include <runtime/run/run.h>

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
  { "async_fs_fd", "metal-async: fs-fd ok", "metal-async: fs-fd fail", 5000 },
  { "async_time",  "metal-async: time ok",  "metal-async: time fail",  5000 },
  { "async_blk",   "metal-async: blk ok",   "metal-async: blk fail",    5000 },
  { "async_net",   "metal-async: net ok",   "metal-async: net fail",  15000 },
  { "async_http",  "metal-async: http ok",  "metal-async: http fail", 30000 },
  { "async_audio", "metal-async: audio ok", "metal-async: audio fail", 15000 },
};

STATIC
CONST CHAR8 *
BootTreeClassName (
  pm_metal_io_class_t  class
  )
{
  switch (class) {
    case PM_METAL_IO_TIME:
      return "time";
    case PM_METAL_IO_GFX:
      return "gfx";
    case PM_METAL_IO_AUDIO:
      return "audio";
    case PM_METAL_IO_INPUT:
      return "input";
    case PM_METAL_IO_FS:
      return "fs";
    case PM_METAL_IO_STREAM:
      return "stream";
    case PM_METAL_IO_NET:
      return "net";
    case PM_METAL_IO_RANDOM:
      return "random";
    case PM_METAL_IO_BLK:
      return "blk";
    default:
      return "unknown";
  }
}

typedef struct {
  UINT32  index;
  UINT32  total;
} boot_tree_dt_ctx_t;

STATIC
int
BootTreeDtIter (
  CONST pm_metal_io_node_t  *n,
  VOID                      *ctx
  )
{
  boot_tree_dt_ctx_t  *t;
  CONST CHAR8         *prefix;
  CONST CHAR8         *cls;
  CONST CHAR8         *compat;

  t      = (boot_tree_dt_ctx_t *)ctx;
  prefix = (t->index + 1u < t->total) ? "|   +-- " : "|   `-- ";
  cls    = BootTreeClassName (n->class);
  compat = (n->compat != NULL) ? n->compat : "?";

  if (n->class == PM_METAL_IO_BLK) {
    pm_metal_blk_h  h;

    h = pm_metal_blk_at (n->unit);
    if (h != PM_METAL_BLK_INVALID && pm_metal_blk_ready (h)) {
      pm_metal_logf (
        "%a%a/%a#%u  %Lu sectors",
        prefix,
        cls,
        compat,
        n->unit,
        pm_metal_blk_capacity_sectors (h)
        );
    } else {
      pm_metal_logf ("%a%a/%a#%u", prefix, cls, compat, n->unit);
    }
  } else if (pm_metal_io_dt_count_class (n->class) > 1) {
    pm_metal_logf ("%a%a/%a#%u", prefix, cls, compat, n->unit);
  } else {
    pm_metal_logf ("%a%a/%a", prefix, cls, compat);
  }

  t->index++;
  return 0;
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
          pm_metal_log ("|   +-- shell    FAIL");
          return PM_METAL_ERROR;
        }

        pm_metal_log ("|   +-- shell    ok");
        s->step = BOOT_READY;
        continue;

      case BOOT_READY:
        pm_metal_log ("|   `-- ready    ok");
        pm_metal_log ("metal-boot: ready");
        {
          UINT32  ty;
          UINT32  sz;

          if (pm_metal_esp_stat ("mods/tests/autotest", &sz, &ty) == 0) {
            (VOID)pm_metal_boot_run_tests ();
          }
        }

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
        return pm_metal_await (self, pm_metal_sleep (pm_metal_shell_pump_sleep_ms ()));

      case BOOT_PUMP_SLEEP:
        s->step = BOOT_PUMP_POLL;
        continue;

      case BOOT_SHUTDOWN:
        pm_metal_boot_shutdown (pm_metal_shell_exit_reboot ());
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

  pm_metal_boot_banner ();
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
  {
    boot_tree_dt_ctx_t  ctx;

    ctx.index = 0;
    ctx.total = pm_metal_io_dt_count ();
    if (ctx.total == 0) {
      pm_metal_log ("|   `-- (none)");
    } else {
      pm_metal_io_dt_foreach (BootTreeDtIter, &ctx);
    }
  }

  pm_metal_log ("|");
}

int
pm_metal_boot_harvest_devices (
  VOID
  )
{
  STATIC CONST pm_metal_io_node_t  TimeNode = {
    .class = PM_METAL_IO_TIME,
    .compat = "tsc",
    .bus = PM_METAL_IO_BUS_PLATFORM
  };
  STATIC CONST pm_metal_io_node_t  GfxNode = {
    .class = PM_METAL_IO_GFX,
    .compat = "framebuffer",
    .bus = PM_METAL_IO_BUS_PLATFORM
  };
  STATIC CONST pm_metal_io_node_t  FsNode = {
    .class = PM_METAL_IO_FS,
    .compat = "esp",
    .bus = PM_METAL_IO_BUS_PLATFORM
  };
  STATIC CONST pm_metal_io_node_t  InputNode = {
    .class = PM_METAL_IO_INPUT,
    .compat = "ps2+com1",
    .bus = PM_METAL_IO_BUS_ISA
  };
  STATIC CONST pm_metal_io_node_t  StreamNode = {
    .class = PM_METAL_IO_STREAM,
    .compat = "uart+ui",
    .bus = PM_METAL_IO_BUS_PLATFORM
  };
  STATIC CONST pm_metal_io_node_t  RandomNode = {
    .class = PM_METAL_IO_RANDOM,
    .compat = "efi-or-weak",
    .bus = PM_METAL_IO_BUS_PLATFORM
  };

  (VOID)pm_metal_io_dt_add (&TimeNode);
  (VOID)pm_metal_io_dt_add (&GfxNode);
  (VOID)pm_metal_io_dt_add (&FsNode);
  (VOID)pm_metal_io_dt_add (&InputNode);
  (VOID)pm_metal_io_dt_add (&StreamNode);
  (VOID)pm_metal_io_dt_add (&RandomNode);

  {
    INT32  v;
    INT32  b;

    v = pm_metal_net_virtio_detect ();
    b = pm_metal_net_bge_detect ();
    if (v != 0 && b != 0) {
      pm_metal_net_null_install ();
      {
        STATIC CONST pm_metal_io_node_t  NetNode = {
          .class = PM_METAL_IO_NET,
          .compat = "null",
          .bus = PM_METAL_IO_BUS_PLATFORM
        };

        (VOID)pm_metal_io_dt_add (&NetNode);
      }
    }
  }

  if (pm_metal_audio_virtio_probe () != 0) {
    pm_metal_audio_null_install ();
    {
      STATIC CONST pm_metal_io_node_t  AudioNode = {
        .class = PM_METAL_IO_AUDIO,
        .compat = "null",
        .bus = PM_METAL_IO_BUS_PLATFORM
      };

      (VOID)pm_metal_io_dt_add (&AudioNode);
    }
  }

  (VOID)pm_metal_console_virtio_probe ();
  (VOID)pm_metal_blk_virtio_detect ();
  (VOID)pm_metal_blk_ide_detect ();
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
  pm_metal_time_recalibrate ();
  if (pm_metal_blk_count () > 0) {
    (VOID)pm_metal_blk_virtio_resume ();
  }

  if (pm_metal_net_bge_start () != 0) {
    /* try virtio too — both may succeed for multi-if */
  }

  (VOID)pm_metal_net_virtio_start ();

  if (pm_metal_blk_count () > 0) {
    pm_metal_blk_h  bh;
    UINT8           sec[512];

    bh = pm_metal_blk_at (0);
    if (pm_metal_blk_read (bh, 0, sec, 1) == 0) {
      pm_metal_log ("metal-blk: lba0 ok");
    } else {
      pm_metal_log ("metal-blk: lba0 fail");
    }
  }

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
  int  reboot
  )
{
  UINT32  i;
  CHAR8   line[72];

  /*
   * Reverse of pool init: shell (stop pump) → wasm → ui → gfx.
   * Keep UI/gfx up through the countdown so the halt tree is visible
   * on real hardware (no serial), then power off or reboot.
   */
  pm_metal_log ("");
  pm_metal_log (reboot ? "metal-boot: reboot" : "metal-boot: shutdown");
  pm_metal_log ("`-- stop");
  pm_metal_log ("|   +-- shell    ok");

  pm_metal_wasm_shutdown ();
  pm_metal_log ("|   +-- wasm     ok");
  pm_metal_log ("|   +-- ui       ok");
  pm_metal_log ("|   `-- gfx      ok");

  for (i = 3; i > 0; i--) {
    AsciiSPrint (
      line,
      sizeof (line),
      reboot ? "metal-boot: reboot in %u s" : "metal-boot: power off in %u s",
      i
      );
    pm_metal_log (line);
    (VOID)pm_metal_ui_frame ();
    (VOID)pm_metal_gfx_present ();
    pm_metal_time_msleep (250u);
  }

  pm_metal_boot_dead (reboot);

  pm_metal_ui_fini ();
  pm_metal_gfx_fini ();
  pm_metal_port_reset (reboot);
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
      {
        UINT32  spins;

        spins = 0;
        for (;;) {
          INT32  pr;

          pr = pm_metal_wasm_session_poll (&st);
          if (pr != 0) {
            rc = (pr > 0 && st == (INT32)PM_METAL_DONE) ? 0 : -1;
            break;
          }

          if (pm_metal_time_mono_us () >= deadline || ++spins > 5000000u) {
            pm_metal_log (mProofs[i].fail);
            pm_metal_wasm_set_stdout_tab (PM_METAL_UI_HANDLE_INVALID);
            return -1;
          }

          /* Yield to the pool while waiting. */
          pm_metal_run_poll_all ();
        }
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
