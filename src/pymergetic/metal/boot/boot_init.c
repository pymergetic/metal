/** @file
  Shared boot seed/init, floor tree, shell pump, shutdown.
  Proofs: boot_test.c — Python: boot_python.c
  Platform DT floor + handoff marker stay in bios/efi bind files.
**/
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/boot/boot.h>
#include <pymergetic/metal/boot/externals.h>
#include <pymergetic/metal/boot/port.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/bus/io/io.h>
#include <pymergetic/metal/net/ip/ip_ops.h>
#include <pymergetic/metal/net/ip/ip_cfg.h>
#include <pymergetic/metal/net/ip/ip_life.h>
#include <pymergetic/metal/net/ntp/ntp.h>
#include <pymergetic/metal/dev/blk/blk.h>
#include <pymergetic/metal/dev/gfx/gfx.h>
#include <pymergetic/metal/shell/ui/ui.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/guest/wasm/wasm.h>
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/fs/esp/esp.h>
#include <pymergetic/metal/trust/trust.h>
#include <pymergetic/metal/util/ip.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <runtime/task/task.h>
#include <runtime/time/time.h>

typedef enum {
  BOOT_GFX = 0,
  BOOT_UI,
  BOOT_TRUST,
  BOOT_NET,
  BOOT_WASM,
  BOOT_PY,
  BOOT_SHELL,
  BOOT_READY,
  BOOT_TESTS_AW,
  BOOT_PUMP_POLL,
  BOOT_PUMP_SLEEP,
  BOOT_SHUTDOWN
} metal_boot_step_t;

typedef struct {
  metal_boot_step_t       step;
  pm_metal_async_handle_t tests_h;
} metal_boot_init_t;

static const char *BootTreeClassName(pm_metal_io_class_t class)
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
  uint32_t index;
  uint32_t total;
} boot_tree_dt_ctx_t;

static int BootTreeDtIter(const pm_metal_io_node_t *n, void *ctx)
{
  boot_tree_dt_ctx_t *t;
  const char         *prefix;
  const char         *cls;
  const char         *compat;

  t      = (boot_tree_dt_ctx_t *)ctx;
  prefix = (t->index + 1u < t->total) ? "|   +-- " : "|   `-- ";
  cls    = BootTreeClassName(n->class);
  compat = (n->compat != NULL) ? n->compat : "?";

  if (n->class == PM_METAL_IO_BLK) {
    pm_metal_blk_h h;

    h = pm_metal_blk_at(n->unit);
    if (h != PM_METAL_BLK_INVALID && pm_metal_blk_ready(h)) {
      uint8_t     sec[512];
      const char *lba0;

      {
        int32_t lba_ok;

        lba_ok = (pm_metal_blk_read(h, 0, sec, 1) == 0) ? 1 : 0;
        lba0   = lba_ok ? "lba0 ok" : "lba0 fail";
        pm_metal_logf_styled(lba_ok ? PM_METAL_LOG_STYLE_OK : PM_METAL_LOG_STYLE_FAIL,
                             "%s%s/%s#%u  %llu sectors  %s",
                             prefix,
                             cls,
                             compat,
                             n->unit,
                             pm_metal_blk_capacity_sectors(h),
                             lba0);
      }
    } else {
      pm_metal_logf_styled(PM_METAL_LOG_STYLE_DIM, "%s%s/%s#%u", prefix, cls, compat, n->unit);
    }
  } else if (n->class == PM_METAL_IO_GFX && n->bus == PM_METAL_IO_BUS_PCI) {
    /* Floor: show real GPU id before scanout bind (855GM=3582, X300 GM45=2a42…). */
    pm_metal_logf_styled(PM_METAL_LOG_STYLE_DIM,
                         "%s%s/%s  pci %04x:%04x @%02x:%02x.%x",
                         prefix,
                         cls,
                         compat,
                         (uint32_t)((n->loc[3] >> 16) & 0xffffu),
                         (uint32_t)(n->loc[3] & 0xffffu),
                         (uint32_t)n->loc[0],
                         (uint32_t)n->loc[1],
                         (uint32_t)n->loc[2]);
  } else if (pm_metal_io_dt_count_class(n->class) > 1) {
    pm_metal_logf_styled(PM_METAL_LOG_STYLE_DIM, "%s%s/%s#%u", prefix, cls, compat, n->unit);
  } else {
    pm_metal_logf_styled(PM_METAL_LOG_STYLE_DIM, "%s%s/%s", prefix, cls, compat);
  }

  t->index++;
  return 0;
}

/** Emit init-tree net branch: ifs (mac + ip/dhcp), optional pkg, optional ntp. */
static void MetalBootLogNetBranch(int32_t dhcp_ok, const char *pkg_note, const char *ntp_note)
{
  uint32_t n;
  uint32_t i;
  uint32_t nif;
  uint32_t seen;
  uint32_t trail;

  n   = pm_metal_net_ip_if_count();
  nif = 0;
  for (i = 0; i < n; i++) {
    pm_metal_net_ip_ifcfg_t cfg;

    if (pm_metal_net_ip_if_get_index(i, &cfg) == 0) {
      nif++;
    }
  }

  trail = 0;
  if (pkg_note != NULL) {
    trail++;
  }

  if (ntp_note != NULL) {
    trail++;
  }

  pm_metal_log_styled(PM_METAL_LOG_STYLE_OK, "|   +-- net      ok");
  if (nif == 0u && trail == 0u) {
    pm_metal_log_styled(PM_METAL_LOG_STYLE_DIM, "|   |   `-- (no ifs)");
    return;
  }

  seen = 0;
  for (i = 0; i < n; i++) {
    pm_metal_net_ip_ifcfg_t cfg;
    uint32_t                ip;
    const char             *pref;
    const char             *addr;

    if (pm_metal_net_ip_if_get_index(i, &cfg) != 0) {
      continue;
    }

    seen++;
    if (seen < nif || trail != 0u) {
      pref = "|   |   +--";
    } else {
      pref = "|   |   `--";
    }

    if (pm_metal_util_ip4_parse(cfg.ip, &ip) == 0 && !pm_metal_util_ip4_is_unspecified(ip)) {
      addr = cfg.ip;
    } else if (dhcp_ok) {
      addr = "0.0.0.0";
    } else {
      addr = "dhcp -";
    }

    pm_metal_logf_styled(PM_METAL_LOG_STYLE_DIM,
                         "%s %s  %02x:%02x:%02x:%02x:%02x:%02x  %s",
                         pref,
                         cfg.name,
                         cfg.mac[0],
                         cfg.mac[1],
                         cfg.mac[2],
                         cfg.mac[3],
                         cfg.mac[4],
                         cfg.mac[5],
                         addr);
  }

  if (pkg_note != NULL) {
    if (ntp_note != NULL) {
      pm_metal_logf_styled(PM_METAL_LOG_STYLE_DIM, "|   |   +-- pkg   %s", pkg_note);
    } else {
      pm_metal_logf_styled(PM_METAL_LOG_STYLE_DIM, "|   |   `-- pkg   %s", pkg_note);
    }
  }

  if (ntp_note != NULL) {
    pm_metal_logf_styled(PM_METAL_LOG_STYLE_DIM, "|   |   `-- ntp   %s", ntp_note);
  }
}

static int32_t MetalBootHasLease(void)
{
  uint32_t                n;
  uint32_t                i;
  pm_metal_net_ip_ifcfg_t cfg;
  uint32_t                ip;

  n = pm_metal_net_ip_if_count();
  for (i = 0; i < n; i++) {
    if (pm_metal_net_ip_if_get_index(i, &cfg) != 0) {
      continue;
    }

    if (strcmp(cfg.name, "lo") == 0) {
      continue;
    }

    if (pm_metal_util_ip4_parse(cfg.ip, &ip) == 0 && !pm_metal_util_ip4_is_unspecified(ip)) {
      return 1;
    }
  }

  return 0;
}

static pm_metal_status_t MetalBootInitStep(pm_metal_async_handle_t self_h)
{
  metal_boot_init_t      *s;
  pm_metal_async_handle_t h;

  h = self_h;
  s = (metal_boot_init_t *)(uintptr_t)pm_metal_async_coro_state(h);
  if (s == NULL) {
    return PM_METAL_ERROR;
  }

  for (;;) {
    switch (s->step) {
    case BOOT_GFX:
      if (pm_metal_gfx_init() != 0) {
        pm_metal_log_styled(PM_METAL_LOG_STYLE_FAIL, "|   +-- gfx      FAIL");
        return PM_METAL_ERROR;
      }

      pm_metal_logf_styled(PM_METAL_LOG_STYLE_OK,
                           "|   +-- gfx      ok  %s %dx%d",
                           pm_metal_gfx_scanout_name(),
                           pm_metal_gfx_width(),
                           pm_metal_gfx_height());
      s->step = BOOT_UI;
      continue;

    case BOOT_UI:
      if (pm_metal_ui_console_shell() != 0) {
        pm_metal_log_styled(PM_METAL_LOG_STYLE_FAIL, "|   +-- ui       FAIL");
        return PM_METAL_ERROR;
      }

      pm_metal_log_attach_ui();
      pm_metal_log_boot_complete();
      pm_metal_log_styled(PM_METAL_LOG_STYLE_OK, "|   +-- ui       ok");
      s->step = BOOT_TRUST;
      continue;

    case BOOT_TRUST: {
      int32_t               tr;
      pm_metal_trust_boot_t st;
      pm_metal_log_style_t  trust_style;
      const char           *detail;

      tr = pm_metal_trust_boot_check();
      st = pm_metal_trust_boot_status();
      if (st == PM_METAL_TRUST_BOOT_OK) {
        trust_style = PM_METAL_LOG_STYLE_OK;
        detail      = NULL;
      } else if (st == PM_METAL_TRUST_BOOT_WARN) {
        trust_style = PM_METAL_LOG_STYLE_WARN;
        detail      = "CA parse";
      } else if (st == PM_METAL_TRUST_BOOT_FAIL) {
        trust_style = PM_METAL_LOG_STYLE_FAIL;
        detail      = "kernel sig";
      } else {
        trust_style = PM_METAL_LOG_STYLE_DIM;
        detail      = NULL;
      }

      if (detail != NULL) {
        pm_metal_logf_styled(
          trust_style, "|   +-- trust    %s %s", pm_metal_trust_boot_status_str(), detail);
      } else {
        pm_metal_logf_styled(trust_style, "|   +-- trust    %s", pm_metal_trust_boot_status_str());
      }

      if (tr != 0) {
        pm_metal_log_styled(PM_METAL_LOG_STYLE_FAIL, "|   +-- trust    hard-fail (enforce)");
        return PM_METAL_ERROR;
      }

      /* Paint before NIC open / DHCP so the FB is not stuck on GOP residue. */
      s->step = BOOT_NET;
      continue;
    }

    case BOOT_NET: {
      const char *pkg_note;
      const char *ntp_note;
      int32_t     dhcp_ok;

      (void)pm_metal_net_ip_bge_start();
      (void)pm_metal_net_ip_virtio_start();
      (void)pm_metal_net_ip_loopback_start();
      (void)pm_metal_net_ip_life_start();

      /* Brief peek for the init tree; life coro finishes late work. */
      pm_metal_net_ip_poll();
      dhcp_ok = MetalBootHasLease();
      /* Guest packages are modular — no per-app notes in the boot tree. */
      pkg_note = NULL;
      ntp_note = (pm_metal_net_ntp_last_unix_ms() != 0ull) ? "ok" : "pending";
      MetalBootLogNetBranch(dhcp_ok, pkg_note, ntp_note);

      /* Blk smoke after devices are live (verify: metal-blk: lba0 ok). */
      if (pm_metal_blk_count() > 0) {
        pm_metal_blk_h bh;
        uint8_t        sec[512];

        bh = pm_metal_blk_at(0);
        if (bh != PM_METAL_BLK_INVALID && pm_metal_blk_read(bh, 0, sec, 1) == 0) {
          pm_metal_log("metal-blk: lba0 ok");
        } else {
          pm_metal_log("metal-blk: lba0 fail");
        }
      }

      s->step = BOOT_WASM;
      continue;
    }

    case BOOT_WASM:
      if (pm_metal_wasm_init() != 0) {
        pm_metal_log_styled(PM_METAL_LOG_STYLE_FAIL, "|   +-- wasm     FAIL");
        return PM_METAL_ERROR;
      }

      pm_metal_log_styled(PM_METAL_LOG_STYLE_OK, "|   +-- wasm     ok");
      s->step = BOOT_PY;
      continue;

    case BOOT_PY:
      if (pm_metal_boot_py_init() != 0) {
        return PM_METAL_ERROR;
      }

      s->step = BOOT_SHELL;
      continue;

    case BOOT_SHELL:
      if (pm_metal_shell_init() != 0) {
        pm_metal_log_styled(PM_METAL_LOG_STYLE_FAIL, "|   +-- shell    FAIL");
        return PM_METAL_ERROR;
      }

      pm_metal_log_styled(PM_METAL_LOG_STYLE_OK, "|   `-- shell    ok");
      s->step = BOOT_READY;
      continue;

    case BOOT_READY:
      pm_metal_log_styled(PM_METAL_LOG_STYLE_DIM, "|");
      /*
       * "ready" is no longer the last root-level tree leaf -- "python"
       * follows it below (own "|" spacer + leaf), so this one is now a
       * "+--" branch instead of the terminal "`--". The old standalone
       * "READY -- type help" / "metal-boot: ready" banner lines (pre-Python
       * console defaults) are gone too: the tree leaf + the Python banner
       * right below it *are* the boot-complete signal now.
       */
      pm_metal_log_styled(PM_METAL_LOG_STYLE_OK, "+-- ready        ok");
      pm_metal_log_styled(PM_METAL_LOG_STYLE_DIM, "|");
      /*
       * Python REPL becomes the primary interactive surface (see
       * docs/MICROPYTHON.md's "Python REPL as the main shell"): the C
       * command dispatcher stays fully wired and reachable via the
       * console() escape call typed at the >>> prompt -- never removed,
       * just no longer the default landing surface. Failure here is
       * non-fatal: fall back to a plain C shell prompt exactly like
       * before this feature existed.
       */
      if (pm_metal_py_repl_start() != 0) {
        int al;

        pm_metal_log_styled(PM_METAL_LOG_STYLE_OK, "`-- python       ok");
        al = pm_metal_py_autoload_run_once();
        if (al > 0) {
          pm_metal_log_styled(PM_METAL_LOG_STYLE_OK, "    `-- autoload ok");
        } else if (al == 0) {
          pm_metal_log_styled(PM_METAL_LOG_STYLE_DIM, "    `-- autoload -");
        } else {
          pm_metal_log_styled(PM_METAL_LOG_STYLE_WARN, "    `-- autoload FAIL");
        }
        pm_metal_log("");
        pm_metal_py_repl_print_banner();
      } else {
        pm_metal_log_styled(PM_METAL_LOG_STYLE_WARN, "`-- python       fallback");
        pm_metal_log("metal-boot: repl start failed, falling back to C console");
      }
      {
        uint32_t ty;
        uint32_t sz;

        /* EFI: optional ESP marker. BIOS esp_stat always fails → skip. */
        if (pm_metal_esp_stat("mods/tests/autotest", &sz, &ty) == 0) {
          s->tests_h = pm_metal_boot_tests_start();
          if (s->tests_h != PM_METAL_ASYNC_HANDLE_INVALID) {
            s->step = BOOT_TESTS_AW;
            return pm_metal_async_await(h, s->tests_h);
          }
        }
      }

      s->step = BOOT_PUMP_POLL;
      continue;

    case BOOT_TESTS_AW:
      (void)pm_metal_boot_tests_result(s->tests_h);
      s->tests_h = PM_METAL_ASYNC_HANDLE_INVALID;
      s->step    = BOOT_PUMP_POLL;
      continue;

    case BOOT_PUMP_POLL:
      if (pm_metal_shell_poll() != 0) {
        s->step = BOOT_SHUTDOWN;
        continue;
      }

      s->step = BOOT_PUMP_SLEEP;
      return pm_metal_async_await(h, pm_metal_async_sleep(pm_metal_shell_pump_sleep_ms()));

    case BOOT_PUMP_SLEEP:
      s->step = BOOT_PUMP_POLL;
      continue;

    case BOOT_SHUTDOWN:
      pm_metal_boot_shutdown(pm_metal_shell_exit_reboot(), pm_metal_shell_exit_fast());
      return PM_METAL_DONE;

    default:
      return PM_METAL_ERROR;
    }
  }
}

void pm_metal_boot_print_floor_tree(uint64_t claim_mib,
                                    uint64_t map_bytes,
                                    uint64_t hole_mib,
                                    uint64_t heap_bytes,
                                    uint64_t stack_kib,
                                    unsigned n_cpus)
{
  uintptr_t i;

  pm_metal_boot_banner();
  pm_metal_log_styled(PM_METAL_LOG_STYLE_ACCENT, "+-- pymergetic metal");
  pm_metal_log_styled(PM_METAL_LOG_STYLE_DIM, "|");
  pm_metal_logf_styled(PM_METAL_LOG_STYLE_DIM, "+-- mem          %llu MiB claimed", claim_mib);
  if (map_bytes < 1024 * 1024) {
    pm_metal_logf_styled(PM_METAL_LOG_STYLE_DIM, "|   +-- MAP      %llu KiB", map_bytes / 1024);
  } else {
    pm_metal_logf_styled(
      PM_METAL_LOG_STYLE_DIM, "|   +-- MAP      %llu MiB", map_bytes / (1024 * 1024));
  }

  for (i = 0; i < n_cpus; i++) {
    pm_metal_logf_styled(PM_METAL_LOG_STYLE_DIM,
                         "|   |   %s cpu%u   %llu KiB stack",
                         (i + 1 < n_cpus) ? "+--" : "`--",
                         (uint32_t)i,
                         stack_kib);
  }

  pm_metal_logf_styled(PM_METAL_LOG_STYLE_DIM, "|   +-- HOLE     %llu MiB", hole_mib);
  if (heap_bytes < 1024 * 1024) {
    pm_metal_logf_styled(PM_METAL_LOG_STYLE_DIM, "|   `-- HEAP     %llu KiB", heap_bytes / 1024);
  } else {
    pm_metal_logf_styled(
      PM_METAL_LOG_STYLE_DIM, "|   `-- HEAP     %llu MiB", heap_bytes / (1024 * 1024));
  }

  pm_metal_logf_styled(PM_METAL_LOG_STYLE_DIM, "+-- cpu          %u runners", n_cpus);
  pm_metal_log_styled(PM_METAL_LOG_STYLE_DIM, "+-- devices");
  {
    boot_tree_dt_ctx_t ctx;

    ctx.index = 0;
    ctx.total = pm_metal_io_dt_count();
    if (ctx.total == 0) {
      pm_metal_log_styled(PM_METAL_LOG_STYLE_DIM, "|   `-- (none)");
    } else {
      pm_metal_io_dt_foreach(BootTreeDtIter, &ctx);
    }
  }

  /* Detailed check runs under `-- init` (needs ESP); show policy mode early. */
  pm_metal_logf_styled((pm_metal_trust_mode() == PM_METAL_TRUST_MODE_OFF) ? PM_METAL_LOG_STYLE_DIM
                                                                          : PM_METAL_LOG_STYLE_OK,
                       "+-- trust        %s",
                       pm_metal_trust_mode_str());

  pm_metal_log_styled(PM_METAL_LOG_STYLE_DIM, "|");
}

int pm_metal_boot_seed_init(void)
{
  metal_boot_init_t      *st;
  pm_metal_async_handle_t h;
  pm_metal_async_handle_t th;

  h = pm_metal_async_coro_create(MetalBootInitStep, sizeof(*st));
  if (h == PM_METAL_ASYNC_HANDLE_INVALID) {
    return -1;
  }

  st = (metal_boot_init_t *)(uintptr_t)pm_metal_async_coro_state(h);
  if (st == NULL) {
    pm_metal_async_coro_close(h);
    return -1;
  }

  st->step    = BOOT_GFX;
  st->tests_h = PM_METAL_ASYNC_HANDLE_INVALID;

  pm_metal_boot_port_seed();
  pm_metal_time_recalibrate();
  if (pm_metal_blk_count() > 0) {
    (void)pm_metal_blk_virtio_resume();
  }

  /* NIC/blk smoke run in the init coro after UI paints (BOOT_NET). */
  pm_metal_log_styled(PM_METAL_LOG_STYLE_DIM, "`-- init");
  /* No CPU affinity — round-robin like everything else (no wasm call-in
     involved here, so which runner picks it up genuinely doesn't matter). */
  th = pm_metal_async_create_task(h);
  if (th == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_async_coro_close(h);
    return -1;
  }

  /* Exit / init failure posts STOP so run_enter returns → ResetSystem. */
  pm_metal_async_task_set_stop_on_done(th, 1);

  return 0;
}

void pm_metal_boot_shutdown(int reboot, int fast)
{
  uint32_t i;

  /*
   * Reverse of pool init: shell (stop pump) → wasm → ui → gfx.
   * Keep UI/gfx up through the countdown so the halt tree is visible
   * on real hardware (no serial), then power off or reboot.
   * fast (`exit -f`): skip countdown sleeps + DEAD hold/fade.
   */
  pm_metal_log("");
  pm_metal_log_styled(PM_METAL_LOG_STYLE_WARN,
                      reboot ? "metal-boot: reboot" : "metal-boot: shutdown");
  pm_metal_log_styled(PM_METAL_LOG_STYLE_DIM, "`-- stop");
  pm_metal_log_styled(PM_METAL_LOG_STYLE_OK, "|   +-- shell    ok");

  pm_metal_wasm_shutdown();
  pm_metal_log_styled(PM_METAL_LOG_STYLE_OK, "|   +-- wasm     ok");
  pm_metal_log_styled(PM_METAL_LOG_STYLE_OK, "|   +-- ui       ok");
  pm_metal_log_styled(PM_METAL_LOG_STYLE_OK, "|   `-- gfx      ok");

  if (fast) {
    pm_metal_ui_set_status(reboot ? "reboot..." : "power off...");
    pm_metal_log_styled(PM_METAL_LOG_STYLE_WARN,
                        reboot ? "metal-boot: reboot (fast)" : "metal-boot: power off (fast)");
    (void)pm_metal_ui_frame();
    (void)pm_metal_gfx_present_surface(PM_METAL_GFX_SURFACE_DEFAULT);
  } else {
    /*
     * Real 1 s ticks — iron has no serial, so the UI banner must stay up
     * long enough to read (was 250 ms/tick and vanished).
     */
    for (i = 3; i > 0; i--) {
      char status[48];

      snprintf(status, sizeof(status), reboot ? "reboot in %u..." : "power off in %u...", i);
      pm_metal_ui_set_status(status);
      pm_metal_logf_styled(PM_METAL_LOG_STYLE_WARN,
                           reboot ? "metal-boot: reboot in %u s" : "metal-boot: power off in %u s",
                           i);
      (void)pm_metal_ui_frame();
      (void)pm_metal_gfx_present_surface(PM_METAL_GFX_SURFACE_DEFAULT);
      pm_metal_time_msleep(1000u);
    }
  }

  pm_metal_boot_dead(reboot, fast);

  pm_metal_ui_shutdown();
  pm_metal_gfx_fini();
  pm_metal_port_reset(reboot);
}

/* Hand-bumped with scripts/setup.d/deps/edk2.sh EDK2_REF. */
PM_METAL_EXTERNAL(g_pm_metal_ext_edk2,
                  edk2,
                  "edk2-stable202502",
                  "https://github.com/tianocore/edk2",
                  "UEFI build SDK (Tianocore)");
