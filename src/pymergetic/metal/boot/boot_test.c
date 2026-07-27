/** @file
  Bring-up proof suite — DHCP wait, wasm mods, then host py proofs.
**/
#include <pymergetic/metal/boot/boot.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/net/ip/ip_ops.h>
#include <pymergetic/metal/net/ip/ip_cfg.h>
#include <pymergetic/metal/shell/ui/ui.h>
#include <pymergetic/metal/guest/wasm/wasm.h>
#include <pymergetic/metal/guest/mod/mod.h>
#include <pymergetic/metal/guest/process/process.h>
#include <pymergetic/metal/fs/esp/esp.h>
#include <pymergetic/metal/util/ip.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <runtime/time/time.h>

#include <stddef.h>
#include <stdint.h>

typedef enum {
  TEST_SEED = 0,
  TEST_DHCP,
  TEST_HELLO,
  TEST_HELLO_FN_AW,
  TEST_HOST_SLEEP,
  TEST_HOST_SLEEP_AW,
  TEST_PROOF_RUN,
  TEST_PROOF_WAIT,
  TEST_PY_PROOFS,
  TEST_PY_PROOFS_AW,
  TEST_OK,
  TEST_FAIL
} metal_test_step_t;

typedef struct {
  metal_test_step_t       step;
  uint32_t                proof_i;
  uint64_t                deadline;
  int32_t                 rc;
  pm_metal_async_handle_t py_h;
  pm_metal_async_handle_t host_sleep_h;
  pm_metal_async_handle_t hello_fn_coro;
  pm_metal_mod_fn_t       hello_fn;
} metal_boot_test_t;

typedef struct {
  uint32_t step;
  uint32_t aw;
} metal_host_sleep_st_t;

static int32_t mTestsLastRc = -1;

/** Same call shape as mods/tests/t_async_sleep guest_step. */
static pm_metal_status_t MetalHostSleepStep(pm_metal_async_handle_t self_h)
{
  metal_host_sleep_st_t *s;

  s = (metal_host_sleep_st_t *)(uintptr_t)pm_metal_async_coro_state(self_h);
  if (s == NULL) {
    return PM_METAL_ERROR;
  }

  switch (s->step) {
  case 0:
    s->aw = pm_metal_async_sleep(50);
    if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
      return PM_METAL_ERROR;
    }

    s->step = 1;
    return pm_metal_async_await(self_h, s->aw);

  case 1:
    pm_metal_log("metal-async: host sleep ok");
    return PM_METAL_DONE;

  default:
    return PM_METAL_ERROR;
  }
}

typedef struct {
  const char *mod;
  const char *ok;
  const char *fail;
  uint32_t    max_ms;
} metal_boot_proof_t;

static const metal_boot_proof_t mProofs[] = {
  { "async_sleep", "metal-async: sleep ok", "metal-async: sleep fail", 5000 },
  { "async_fs", "metal-async: fs ok", "metal-async: fs fail", 5000 },
  { "async_fs_fd", "metal-async: fs-fd ok", "metal-async: fs-fd fail", 5000 },
  { "async_time", "metal-async: time ok", "metal-async: time fail", 5000 },
  { "async_blk", "metal-async: blk ok", "metal-async: blk fail", 5000 },
  { "async_net", "metal-async: net ok", "metal-async: net fail", 15000 },
  { "async_http", "metal-async: http ok", "metal-async: http fail", 30000 },
  { "async_tftp", "metal-async: tftp ok", "metal-async: tftp fail", 15000 },
  { "async_audio", "metal-async: audio ok", "metal-async: audio fail", 15000 },
  { "async_py", "metal-async: py ok", "metal-async: py fail", 10000 },
  { "fresh_guest", "metal-async: fresh ok", "metal-async: fresh fail", 10000 },
};

static pm_metal_status_t MetalBootTestStep(pm_metal_async_handle_t self_h)
{
  metal_boot_test_t      *t;
  pm_metal_async_handle_t h;
  int32_t                 st;

  h = self_h;
  t = (metal_boot_test_t *)(uintptr_t)pm_metal_async_coro_state(h);
  if (t == NULL) {
    return PM_METAL_ERROR;
  }

  switch (t->step) {
  case TEST_SEED:
    if (!pm_metal_wasm_ready()) {
      pm_metal_log("metal-test: wasm not ready");
      t->rc   = -1;
      t->step = TEST_FAIL;
      return PM_METAL_PENDING;
    }

    {
      static const uint8_t FsMarker[] = "metal-async-fs\n";

      (void)pm_metal_esp_cache_put("mods/tests/async_fs.txt", FsMarker, sizeof(FsMarker) - 1);
    }

    pm_metal_log("metal-test: begin");
    pm_metal_wasm_set_stdout_tab(pm_metal_ui_console_handle());
    t->deadline = pm_metal_time_mono_us() + 10000000ull;
    t->step     = TEST_DHCP;
    return PM_METAL_PENDING;

  case TEST_DHCP: {
    pm_metal_net_ip_ifcfg_t cfg;
    uint32_t             ip;

    pm_metal_net_ip_poll();
    if (pm_metal_net_ip_if_get(&cfg) == 0 && pm_metal_util_ip4_parse(cfg.ip, &ip) == 0 &&
        !pm_metal_util_ip4_is_unspecified(ip)) {
      char tftp[PM_METAL_NET_TFTP_HOST_MAX];
      char boot[PM_METAL_NET_IP_BOOT_FILE_MAX];

      pm_metal_logf("metal-test: net %s/%s up", cfg.ip, cfg.backend);
      if (pm_metal_net_ip_if_boot_get(NULL, tftp, sizeof(tftp), boot, sizeof(boot)) == 0) {
        pm_metal_logf("metal-test: dhcp-boot tftp=%s file=%s",
                      tftp[0] != '\0' ? tftp : "-",
                      boot[0] != '\0' ? boot : "-");
      }

      t->step = TEST_HELLO;
      return PM_METAL_PENDING;
    }

    if (pm_metal_time_mono_us() >= t->deadline) {
      pm_metal_logf("metal-test: net wait timeout (ifs=%u)", pm_metal_net_ip_if_count());
      t->step = TEST_HELLO;
      return PM_METAL_PENDING;
    }

    return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
  }

  case TEST_HELLO:
    /* Twice on purpose: proves the shared "instance 0" is reentrant. */
    t->rc = pm_metal_mod_cmd_invoke("hello",
                                    PM_METAL_PROC_UI_NONE,
                                    pm_metal_ui_console_handle(),
                                    PM_METAL_MOD_INSTANCE_SHARED,
                                    PM_METAL_MOD_FLAG_NONE,
                                    NULL);
    if (t->rc == 0) {
      t->rc = pm_metal_mod_cmd_invoke("hello",
                                      PM_METAL_PROC_UI_NONE,
                                      pm_metal_ui_console_handle(),
                                      PM_METAL_MOD_INSTANCE_SHARED,
                                      PM_METAL_MOD_FLAG_NONE,
                                      NULL);
    }

    /* Resolve once at callsite; await coro like host sleep (no process). */
    if (t->rc == 0) {
      t->rc = pm_metal_mod_func_resolve("hello", "run", &t->hello_fn);
      if (t->rc == 0) {
        t->hello_fn_coro = pm_metal_mod_fn_coro(&t->hello_fn);
        if (t->hello_fn_coro == PM_METAL_ASYNC_HANDLE_INVALID) {
          t->rc = -1;
        }
      }
    }

    if (t->rc != 0) {
      pm_metal_log("metal-wasm: t0_hello fail");
      t->step = TEST_FAIL;
      return PM_METAL_PENDING;
    }

    t->step = TEST_HELLO_FN_AW;
    return pm_metal_async_await(h, t->hello_fn_coro);

  case TEST_HELLO_FN_AW:
    t->hello_fn_coro = PM_METAL_ASYNC_HANDLE_INVALID;
    pm_metal_log("metal-wasm: t0_hello ok");
    t->step = TEST_HOST_SLEEP;
    return PM_METAL_PENDING;

  case TEST_HOST_SLEEP:
    t->host_sleep_h = pm_metal_async_coro_create(MetalHostSleepStep, sizeof(metal_host_sleep_st_t));
    if (t->host_sleep_h == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-async: host sleep fail");
      t->step = TEST_FAIL;
      return PM_METAL_PENDING;
    }

    t->step = TEST_HOST_SLEEP_AW;
    return pm_metal_async_await(h, t->host_sleep_h);

  case TEST_HOST_SLEEP_AW:
    t->host_sleep_h = PM_METAL_ASYNC_HANDLE_INVALID;
    t->proof_i      = 0;
    t->step         = TEST_PROOF_RUN;
    return PM_METAL_PENDING;

  case TEST_PROOF_RUN:
    if (t->proof_i >= (sizeof(mProofs) / sizeof(mProofs[0]))) {
      t->step = TEST_PY_PROOFS;
      return PM_METAL_PENDING;
    }

    t->rc = pm_metal_mod_cmd_invoke(mProofs[t->proof_i].mod,
                                    PM_METAL_PROC_UI_NONE,
                                    pm_metal_ui_console_handle(),
                                    PM_METAL_MOD_INSTANCE_SHARED,
                                    PM_METAL_MOD_FLAG_NONE,
                                    NULL);
    if (t->rc != 0) {
      pm_metal_log(mProofs[t->proof_i].fail);
      t->step = TEST_FAIL;
      return PM_METAL_PENDING;
    }

    if (!pm_metal_process_active()) {
      pm_metal_log(mProofs[t->proof_i].ok);
      t->proof_i++;
      return PM_METAL_PENDING;
    }

    t->deadline = pm_metal_time_mono_us() + (uint64_t)mProofs[t->proof_i].max_ms * 1000u;
    t->step     = TEST_PROOF_WAIT;
    return PM_METAL_PENDING;

  case TEST_PROOF_WAIT: {
    int32_t pr;

    pr = pm_metal_process_poll(&st);
    if (pr != 0) {
      if (pr > 0 && st == PM_METAL_DONE) {
        pm_metal_log(mProofs[t->proof_i].ok);
        t->proof_i++;
        t->step = TEST_PROOF_RUN;
        return PM_METAL_PENDING;
      }

      pm_metal_log(mProofs[t->proof_i].fail);
      t->step = TEST_FAIL;
      return PM_METAL_PENDING;
    }

    if (pm_metal_time_mono_us() >= t->deadline) {
      pm_metal_log(mProofs[t->proof_i].fail);
      t->step = TEST_FAIL;
      return PM_METAL_PENDING;
    }

    return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
  }

  case TEST_PY_PROOFS:
    t->py_h = pm_metal_boot_py_proofs_start();
    if (t->py_h == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: overlap fail");
      t->step = TEST_FAIL;
      return PM_METAL_PENDING;
    }

    t->step = TEST_PY_PROOFS_AW;
    return pm_metal_async_await(h, t->py_h);

  case TEST_PY_PROOFS_AW:
    if (pm_metal_boot_py_proofs_result(t->py_h) != 0) {
      t->step = TEST_FAIL;
      return PM_METAL_PENDING;
    }

    t->step = TEST_OK;
    return PM_METAL_PENDING;

  case TEST_OK:
    pm_metal_wasm_set_stdout_tab(PM_METAL_UI_HANDLE_INVALID);
    pm_metal_log("metal-test: ok");
    t->rc        = 0;
    mTestsLastRc = 0;
    pm_metal_async_set_result_u32(h, 1u);
    return PM_METAL_DONE;

  case TEST_FAIL:
    pm_metal_wasm_set_stdout_tab(PM_METAL_UI_HANDLE_INVALID);
    t->rc        = -1;
    mTestsLastRc = -1;
    pm_metal_async_set_result_u32(h, 0u);
    return PM_METAL_DONE;

  default:
    return PM_METAL_ERROR;
  }
}

pm_metal_async_handle_t pm_metal_boot_tests_start(void)
{
  metal_boot_test_t      *t;
  pm_metal_async_handle_t h;

  h = pm_metal_async_coro_create(MetalBootTestStep, sizeof(*t));
  if (h == PM_METAL_ASYNC_HANDLE_INVALID) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  t = (metal_boot_test_t *)(uintptr_t)pm_metal_async_coro_state(h);
  if (t == NULL) {
    pm_metal_async_coro_close(h);
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  t->step         = TEST_SEED;
  t->proof_i      = 0;
  t->rc           = -1;
  t->py_h         = PM_METAL_ASYNC_HANDLE_INVALID;
  t->host_sleep_h = PM_METAL_ASYNC_HANDLE_INVALID;
  return h;
}

int pm_metal_boot_tests_result(pm_metal_async_handle_t h)
{
  metal_boot_test_t *t;

  t = (metal_boot_test_t *)(uintptr_t)pm_metal_async_coro_state(h);
  if (t != NULL) {
    return t->rc;
  }

  return mTestsLastRc;
}
