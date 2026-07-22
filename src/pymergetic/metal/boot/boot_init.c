/** @file
  Shared boot seed/init, floor tree, proof runner, shutdown.
  Platform DT floor + handoff marker stay in bios/efi bind files.
**/
#include <pymergetic/metal/boot/boot.h>
#include <pymergetic/metal/boot/port.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/bus/io/io.h>
#include <pymergetic/metal/dev/net/net_ops.h>
#include <pymergetic/metal/dev/net/net_cfg.h>
#include <pymergetic/metal/dev/blk/blk.h>
#include <pymergetic/metal/dev/gfx/gfx.h>
#include <pymergetic/metal/shell/ui/ui.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/guest/wasm/wasm.h>
#include <pymergetic/metal/fs/esp/esp.h>
#include <pymergetic/metal/util/ip.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <runtime/coro/coro.h>
#include <runtime/task/task.h>
#include <runtime/time/time.h>
#include <runtime/run/run.h>

#include <Uefi.h>

typedef enum {
  BOOT_GFX = 0,
  BOOT_UI,
  BOOT_NET,
  BOOT_WASM,
  BOOT_SHELL,
  BOOT_READY,
  BOOT_TESTS_AW,
  BOOT_PUMP_POLL,
  BOOT_PUMP_SLEEP,
  BOOT_SHUTDOWN
} metal_boot_step_t;

typedef struct {
  pm_metal_coro_t           coro;
  metal_boot_step_t         step;
  pm_metal_async_handle_t   tests_h;
} metal_boot_init_t;

typedef enum {
  TEST_SEED = 0,
  TEST_DHCP,
  TEST_HELLO,
  TEST_PROOF_RUN,
  TEST_PROOF_WAIT,
  TEST_OK,
  TEST_FAIL
} metal_test_step_t;

typedef struct {
  pm_metal_coro_t     coro;
  metal_test_step_t   step;
  UINT32              proof_i;
  UINT64              deadline;
  INT32               rc;
} metal_boot_test_t;

STATIC INT32  mTestsLastRc = -1;

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
  { "async_tftp",  "metal-async: tftp ok",  "metal-async: tftp fail", 15000 },
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
        /* Paint before NIC open / DHCP so the FB is not stuck on GOP residue. */
        s->step = BOOT_NET;
        continue;

      case BOOT_NET:
        (VOID)pm_metal_net_bge_start ();
        (VOID)pm_metal_net_virtio_start ();
        (VOID)pm_metal_net_loopback_start ();
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

        pm_metal_log ("|   +-- net      ok");
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

          /* EFI: optional ESP marker. BIOS esp_stat always fails → skip. */
          if (pm_metal_esp_stat ("mods/tests/autotest", &sz, &ty) == 0) {
            s->tests_h = pm_metal_boot_tests_start ();
            if (s->tests_h != PM_METAL_ASYNC_HANDLE_INVALID) {
              s->step = BOOT_TESTS_AW;
              return (pm_metal_status_t)pm_metal_async_await_coro (
                       self,
                       s->tests_h
                       );
            }
          }
        }

        pm_metal_log ("");
        pm_metal_log ("type help - test runs proofs");
        s->step = BOOT_PUMP_POLL;
        continue;

      case BOOT_TESTS_AW:
        (VOID)pm_metal_boot_tests_result (s->tests_h);
        s->tests_h = PM_METAL_ASYNC_HANDLE_INVALID;
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
  pm_metal_boot_port_seed ();
  pm_metal_time_recalibrate ();
  if (pm_metal_blk_count () > 0) {
    (VOID)pm_metal_blk_virtio_resume ();
  }

  /* NIC/blk smoke run in the init coro after UI paints (BOOT_NET). */
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
    pm_metal_logf (
      reboot ? "metal-boot: reboot in %u s" : "metal-boot: power off in %u s",
      i
      );
    (VOID)pm_metal_ui_frame ();
    (VOID)pm_metal_gfx_present ();
    pm_metal_time_msleep (250u);
  }

  pm_metal_boot_dead (reboot);

  pm_metal_ui_shutdown ();
  pm_metal_gfx_fini ();
  pm_metal_port_reset (reboot);
}

STATIC
pm_metal_status_t
MetalBootTestCoro (
  pm_metal_coro_t  *self
  )
{
  metal_boot_test_t  *t;
  INT32               st;

  t = (metal_boot_test_t *)self;

  switch (t->step) {
    case TEST_SEED:
      if (!pm_metal_wasm_ready ()) {
        pm_metal_log ("metal-test: wasm not ready");
        t->rc   = -1;
        t->step = TEST_FAIL;
        return PM_METAL_PENDING;
      }

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
      t->deadline = pm_metal_time_mono_us () + 10000000ull;
      t->step     = TEST_DHCP;
      return PM_METAL_PENDING;

    case TEST_DHCP:
      {
        pm_metal_net_ifcfg_t  cfg;
        UINT32                ip;

        pm_metal_net_poll ();
        if (pm_metal_net_if_get (&cfg) == 0
            && pm_metal_util_ip4_parse (cfg.ip, &ip) == 0
            && !pm_metal_util_ip4_is_unspecified (ip))
        {
          CHAR8  tftp[PM_METAL_NET_TFTP_HOST_MAX];
          CHAR8  boot[PM_METAL_NET_BOOT_FILE_MAX];

          pm_metal_logf ("metal-test: net %a/%a up", cfg.ip, cfg.backend);
          if (pm_metal_net_if_boot_get (
                NULL,
                tftp,
                sizeof (tftp),
                boot,
                sizeof (boot)
                ) == 0)
          {
            pm_metal_logf (
              "metal-test: dhcp-boot tftp=%a file=%a",
              tftp[0] != '\0' ? tftp : "-",
              boot[0] != '\0' ? boot : "-"
              );
          }

          t->step = TEST_HELLO;
          return PM_METAL_PENDING;
        }

        if (pm_metal_time_mono_us () >= t->deadline) {
          pm_metal_logf (
            "metal-test: net wait timeout (ifs=%u)",
            pm_metal_net_if_count ()
            );
          t->step = TEST_HELLO;
          return PM_METAL_PENDING;
        }

        return pm_metal_await (self, pm_metal_sleep_us (2000));
      }

    case TEST_HELLO:
      t->rc = pm_metal_wasm_run_mod ("hello");
      if (t->rc == 0) {
        t->rc = pm_metal_wasm_run_mod ("hello");
      }

      if (t->rc != 0) {
        pm_metal_log ("metal-wasm: t0_hello fail");
        t->step = TEST_FAIL;
        return PM_METAL_PENDING;
      }

      pm_metal_log ("metal-wasm: t0_hello ok");
      t->proof_i = 0;
      t->step    = TEST_PROOF_RUN;
      return PM_METAL_PENDING;

    case TEST_PROOF_RUN:
      if (t->proof_i >= (sizeof (mProofs) / sizeof (mProofs[0]))) {
        t->step = TEST_OK;
        return PM_METAL_PENDING;
      }

      t->rc = pm_metal_wasm_run_mod (mProofs[t->proof_i].mod);
      if (t->rc != 0) {
        pm_metal_log (mProofs[t->proof_i].fail);
        t->step = TEST_FAIL;
        return PM_METAL_PENDING;
      }

      if (!pm_metal_wasm_session_active ()) {
        pm_metal_log (mProofs[t->proof_i].ok);
        t->proof_i++;
        return PM_METAL_PENDING;
      }

      t->deadline = pm_metal_time_mono_us ()
                    + (UINT64)mProofs[t->proof_i].max_ms * 1000u;
      t->step = TEST_PROOF_WAIT;
      return PM_METAL_PENDING;

    case TEST_PROOF_WAIT:
      {
        INT32  pr;

        pr = pm_metal_wasm_session_poll (&st);
        if (pr != 0) {
          if (pr > 0 && st == (INT32)PM_METAL_DONE) {
            pm_metal_log (mProofs[t->proof_i].ok);
            t->proof_i++;
            t->step = TEST_PROOF_RUN;
            return PM_METAL_PENDING;
          }

          pm_metal_log (mProofs[t->proof_i].fail);
          t->step = TEST_FAIL;
          return PM_METAL_PENDING;
        }

        if (pm_metal_time_mono_us () >= t->deadline) {
          pm_metal_log (mProofs[t->proof_i].fail);
          t->step = TEST_FAIL;
          return PM_METAL_PENDING;
        }

        return pm_metal_await (self, pm_metal_sleep_us (2000));
      }

    case TEST_OK:
      pm_metal_wasm_set_stdout_tab (PM_METAL_UI_HANDLE_INVALID);
      pm_metal_log ("metal-test: ok");
      t->rc          = 0;
      mTestsLastRc   = 0;
      self->result   = (VOID *)(UINTN)1u;
      return PM_METAL_DONE;

    case TEST_FAIL:
      pm_metal_wasm_set_stdout_tab (PM_METAL_UI_HANDLE_INVALID);
      t->rc          = -1;
      mTestsLastRc   = -1;
      self->result   = (VOID *)(UINTN)0u;
      return PM_METAL_DONE;

    default:
      return PM_METAL_ERROR;
  }
}

pm_metal_async_handle_t
pm_metal_boot_tests_start (
  VOID
  )
{
  metal_boot_test_t  *t;

  t = (metal_boot_test_t *)pm_metal_coro (MetalBootTestCoro, sizeof (*t));
  if (t == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  t->step    = TEST_SEED;
  t->proof_i = 0;
  t->rc      = -1;
  return pm_metal_async_adopt_host_coro (&t->coro);
}

int
pm_metal_boot_tests_result (
  pm_metal_async_handle_t  h
  )
{
  metal_boot_test_t  *t;

  t = (metal_boot_test_t *)pm_metal_async_host_coro (h);
  if (t != NULL) {
    return t->rc;
  }

  return mTestsLastRc;
}
