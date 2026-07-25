/** @file
  Boot-path MicroPython: always-on blob init + host overlap/yield proofs.
**/
#include <pymergetic/metal/boot/boot.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <runtime/run/run.h>
#include <runtime/time/time.h>

#include <stddef.h>
#include <stdint.h>

typedef enum {
  PY_PROOF_OVERLAP = 0,
  PY_PROOF_OVERLAP_WAIT,
  PY_PROOF_YIELD,
  PY_PROOF_YIELD_WAIT,
  PY_PROOF_OK,
  PY_PROOF_FAIL
} metal_boot_py_proof_step_t;

typedef struct {
  metal_boot_py_proof_step_t step;
  pm_metal_async_handle_t    a;
  pm_metal_async_handle_t    b;
  uint64_t                   t0;
  uint64_t                   deadline;
  int32_t                    rc;
} metal_boot_py_proof_t;

static int32_t mPyProofsLastRc = -1;

int pm_metal_boot_py_init(void)
{
  /* Always-on µPy MAP blob; scripts later = Metal tasks on runners. */
  if (pm_metal_py_init() != 0) {
    pm_metal_log_styled(PM_METAL_LOG_STYLE_FAIL, "|   +-- py       FAIL");
    return -1;
  }

  pm_metal_log_styled(PM_METAL_LOG_STYLE_OK, "|   +-- py       ok");
  return 0;
}

static pm_metal_status_t MetalBootPyProofStep(pm_metal_async_handle_t self_h)
{
  metal_boot_py_proof_t  *t;
  pm_metal_async_handle_t h;

  h = self_h;
  t = (metal_boot_py_proof_t *)(uintptr_t)pm_metal_async_coro_state(h);
  if (t == NULL) {
    return PM_METAL_ERROR;
  }

  switch (t->step) {
  case PY_PROOF_OVERLAP:
    if (!pm_metal_py_ready()) {
      pm_metal_log("metal-py: overlap fail");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->a = pm_metal_py_run_script("/mods/py/overlap_a.py");
    t->b = pm_metal_py_run_script("/mods/py/overlap_b.py");
    if (t->a == PM_METAL_ASYNC_HANDLE_INVALID || t->b == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: overlap fail");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->t0       = pm_metal_time_mono_us();
    t->deadline = t->t0 + 5000000ull;
    t->step     = PY_PROOF_OVERLAP_WAIT;
    return PM_METAL_PENDING;

  case PY_PROOF_OVERLAP_WAIT: {
    int32_t  sa;
    int32_t  sb;
    uint64_t dt;

    /*
         * Host py tasks are RR'd onto equal runners. During boot proofs the
         * shell pump (run_poll_all) is not running yet — drain every inbox so
         * work posted off the boot CPU still advances.
         */
    pm_metal_run_poll_all();
    sa = pm_metal_async_task_status(t->a);
    sb = pm_metal_async_task_status(t->b);
    if ((sa == PM_METAL_PENDING || sa == PM_METAL_WAITING) ||
        (sb == PM_METAL_PENDING || sb == PM_METAL_WAITING)) {
      if (pm_metal_time_mono_us() >= t->deadline) {
        pm_metal_async_task_cancel(t->a);
        pm_metal_async_task_cancel(t->b);
        pm_metal_log("metal-py: overlap fail");
        t->step = PY_PROOF_FAIL;
        return PM_METAL_PENDING;
      }

      return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
    }

    dt = pm_metal_time_mono_us() - t->t0;
    /* Two 150 ms sleeps overlapped ⇒ wall ≪ 300 ms. */
    if (sa == PM_METAL_DONE && sb == PM_METAL_DONE && dt < 250000ull) {
      pm_metal_log("metal-py: overlap ok");
      t->step = PY_PROOF_YIELD;
      return PM_METAL_PENDING;
    }

    pm_metal_logf("metal-py: overlap fail sa=%d sb=%d dt=%u", sa, sb, (uint32_t)dt);
    t->step = PY_PROOF_FAIL;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_YIELD:
    t->a = pm_metal_py_run_script("/mods/py/yield_peer.py");
    t->b = pm_metal_py_run_script("/mods/py/yield_sleeper.py");
    if (t->a == PM_METAL_ASYNC_HANDLE_INVALID || t->b == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: yield fail");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->t0       = pm_metal_time_mono_us();
    t->deadline = t->t0 + 5000000ull;
    t->step     = PY_PROOF_YIELD_WAIT;
    return PM_METAL_PENDING;

  case PY_PROOF_YIELD_WAIT: {
    int32_t  sa;
    int32_t  sb;
    uint64_t dt;

    pm_metal_run_poll_all();
    sa = pm_metal_async_task_status(t->a);
    sb = pm_metal_async_task_status(t->b);
    /* Wait until sleeper (b) finishes. */
    if (sb == PM_METAL_PENDING || sb == PM_METAL_WAITING) {
      if (pm_metal_time_mono_us() >= t->deadline) {
        pm_metal_async_task_cancel(t->a);
        pm_metal_async_task_cancel(t->b);
        pm_metal_log("metal-py: yield fail");
        t->step = PY_PROOF_FAIL;
        return PM_METAL_PENDING;
      }

      return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
    }

    dt = pm_metal_time_mono_us() - t->t0;
    if (sb == PM_METAL_DONE && dt < 200000ull) {
      if (sa == PM_METAL_PENDING || sa == PM_METAL_WAITING) {
        pm_metal_async_task_cancel(t->a);
      }

      pm_metal_log("metal-py: yield ok");
      t->step = PY_PROOF_OK;
      return PM_METAL_PENDING;
    }

    pm_metal_async_task_cancel(t->a);
    pm_metal_log("metal-py: yield fail");
    t->step = PY_PROOF_FAIL;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_OK:
    t->rc           = 0;
    mPyProofsLastRc = 0;
    pm_metal_async_set_result_u32(h, 1u);
    return PM_METAL_DONE;

  case PY_PROOF_FAIL:
    t->rc           = -1;
    mPyProofsLastRc = -1;
    pm_metal_async_set_result_u32(h, 0u);
    return PM_METAL_DONE;

  default:
    return PM_METAL_ERROR;
  }
}

pm_metal_async_handle_t pm_metal_boot_py_proofs_start(void)
{
  metal_boot_py_proof_t  *t;
  pm_metal_async_handle_t h;

  h = pm_metal_async_coro_create(MetalBootPyProofStep, sizeof(*t));
  if (h == PM_METAL_ASYNC_HANDLE_INVALID) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  t = (metal_boot_py_proof_t *)(uintptr_t)pm_metal_async_coro_state(h);
  if (t == NULL) {
    pm_metal_async_coro_close(h);
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  t->step     = PY_PROOF_OVERLAP;
  t->a        = PM_METAL_ASYNC_HANDLE_INVALID;
  t->b        = PM_METAL_ASYNC_HANDLE_INVALID;
  t->t0       = 0;
  t->deadline = 0;
  t->rc       = -1;
  return h;
}

int pm_metal_boot_py_proofs_result(pm_metal_async_handle_t h)
{
  metal_boot_py_proof_t *t;

  t = (metal_boot_py_proof_t *)(uintptr_t)pm_metal_async_coro_state(h);
  if (t != NULL) {
    return t->rc;
  }

  return mPyProofsLastRc;
}
