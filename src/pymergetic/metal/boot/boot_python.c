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
  PY_PROOF_DOTTED,
  PY_PROOF_EXC,
  PY_PROOF_EXC_WAIT,
  PY_PROOF_EXC_PROBE_WAIT,
  PY_PROOF_CANCEL,
  PY_PROOF_CANCEL_WAIT,
  PY_PROOF_CANCEL_PROBE_WAIT,
  PY_PROOF_OOM,
  PY_PROOF_OOM_WAIT,
  PY_PROOF_PMCMD,
  PY_PROOF_PMCMD_WAIT,
  PY_PROOF_MOD,
  PY_PROOF_MOD_WAIT,
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
      t->step = PY_PROOF_DOTTED;
      return PM_METAL_PENDING;
    }

    pm_metal_async_task_cancel(t->a);
    pm_metal_log("metal-py: yield fail");
    t->step = PY_PROOF_FAIL;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_DOTTED: {
    /*
     * Regression proof for the pm_metal_py_lookup dotted-path fix: a 4+
     * level bound name ("pymergetic.metal.aio.mono_us") used to always
     * fail because the old code tried to load "metal.aio.mono_us" as one
     * literal attribute after splitting only on the first dot.
     */
    pm_metal_py_ref_t ref;

    if (pm_metal_py_lookup("pymergetic.metal.aio.mono_us", &ref) != 0 || ref.obj == NULL) {
      pm_metal_log("metal-py: dotted-3 fail");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    pm_metal_log("metal-py: dotted-3 ok");
    t->step = PY_PROOF_EXC;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_EXC:
    t->a = pm_metal_py_run_str("raise ValueError('boot-proof induced exception')\n");
    if (t->a == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: exc-isolation fail (spawn)");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->t0       = pm_metal_time_mono_us();
    t->deadline = t->t0 + 3000000ull;
    t->step     = PY_PROOF_EXC_WAIT;
    return PM_METAL_PENDING;

  case PY_PROOF_EXC_WAIT: {
    int32_t sa;

    pm_metal_run_poll_all();
    sa = pm_metal_async_task_status(t->a);
    if (sa == PM_METAL_PENDING || sa == PM_METAL_WAITING) {
      if (pm_metal_time_mono_us() >= t->deadline) {
        pm_metal_async_task_cancel(t->a);
        pm_metal_log("metal-py: exc-isolation fail (timeout)");
        t->step = PY_PROOF_FAIL;
        return PM_METAL_PENDING;
      }

      return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
    }

    /* Uncaught exception must kill only this task (ERROR, not DONE). */
    if (sa != PM_METAL_ERROR || !pm_metal_py_ready()) {
      pm_metal_logf("metal-py: exc-isolation fail sa=%d ready=%d", sa, pm_metal_py_ready());
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    /* Blob must still answer right after — spawn+run a fresh probe script. */
    t->b = pm_metal_py_run_str("x = 1\n");
    if (t->b == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: exc-isolation fail (blob dead after exception)");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->t0       = pm_metal_time_mono_us();
    t->deadline = t->t0 + 3000000ull;
    t->step     = PY_PROOF_EXC_PROBE_WAIT;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_EXC_PROBE_WAIT: {
    int32_t sb;

    pm_metal_run_poll_all();
    sb = pm_metal_async_task_status(t->b);
    if (sb == PM_METAL_PENDING || sb == PM_METAL_WAITING) {
      if (pm_metal_time_mono_us() >= t->deadline) {
        pm_metal_async_task_cancel(t->b);
        pm_metal_log("metal-py: exc-isolation fail (probe timeout)");
        t->step = PY_PROOF_FAIL;
        return PM_METAL_PENDING;
      }

      return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
    }

    if (sb != PM_METAL_DONE) {
      pm_metal_logf("metal-py: exc-isolation fail probe sb=%d", sb);
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    pm_metal_log("metal-py: exc-isolation ok");
    t->step = PY_PROOF_CANCEL;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_CANCEL:
    /*
     * Short sleep on purpose (not e.g. 30s): if pm_metal_async_task_cancel
     * doesn't cleanly cascade-cancel the nested sleep-timer coro (a real
     * possibility worth flagging, but a run.c/async.c fix, not a py.c one —
     * out of this phase's scope), the timer's own natural expiry still
     * clears it out almost immediately instead of dragging the whole
     * runner/timer wheel out for tens of seconds.
     */
    t->a = pm_metal_py_run_str("import pymergetic.metal.aio as a\n"
                               "async def main():\n"
                               "    await a.sleep_us(500000)\n");
    if (t->a == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: cancel-cleanup fail (spawn)");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->t0       = pm_metal_time_mono_us();
    t->deadline = t->t0 + 3000000ull;
    t->step     = PY_PROOF_CANCEL_WAIT;
    return PM_METAL_PENDING;

  case PY_PROOF_CANCEL_WAIT: {
    int32_t sa;

    pm_metal_run_poll_all();
    sa = pm_metal_async_task_status(t->a);
    if (sa != PM_METAL_WAITING) {
      if (pm_metal_time_mono_us() >= t->deadline) {
        pm_metal_async_task_cancel(t->a);
        pm_metal_log("metal-py: cancel-cleanup fail (never parked)");
        t->step = PY_PROOF_FAIL;
        return PM_METAL_PENDING;
      }

      return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
    }

    /* Genuinely in-flight (parked on the sleep) — cancel it now. */
    pm_metal_async_task_cancel(t->a);

    /* Blob must still answer right after — spawn+run a fresh probe script. */
    t->b = pm_metal_py_run_str("x = 1\n");
    if (t->b == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: cancel-cleanup fail (blob dead after cancel)");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->t0       = pm_metal_time_mono_us();
    t->deadline = t->t0 + 3000000ull;
    t->step     = PY_PROOF_CANCEL_PROBE_WAIT;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_CANCEL_PROBE_WAIT: {
    int32_t sb;

    pm_metal_run_poll_all();
    sb = pm_metal_async_task_status(t->b);
    if (sb == PM_METAL_PENDING || sb == PM_METAL_WAITING) {
      if (pm_metal_time_mono_us() >= t->deadline) {
        pm_metal_async_task_cancel(t->b);
        pm_metal_log("metal-py: cancel-cleanup fail (probe timeout)");
        t->step = PY_PROOF_FAIL;
        return PM_METAL_PENDING;
      }

      return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
    }

    if (sb != PM_METAL_DONE) {
      pm_metal_logf("metal-py: cancel-cleanup fail probe sb=%d", sb);
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    pm_metal_log("metal-py: cancel-cleanup ok");
    t->step = PY_PROOF_OOM;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_OOM:
    /* Exponential string growth — deterministically exhausts the 256 KiB
     * blob heap in a handful of iterations (MemoryError, uncaught). */
    t->a = pm_metal_py_run_str("s = 'x' * 1024\n"
                               "while True:\n"
                               "    s = s + s\n");
    if (t->a == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: oom-isolation fail (spawn)");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->t0       = pm_metal_time_mono_us();
    t->deadline = t->t0 + 3000000ull;
    t->step     = PY_PROOF_OOM_WAIT;
    return PM_METAL_PENDING;

  case PY_PROOF_OOM_WAIT: {
    int32_t sa;

    pm_metal_run_poll_all();
    sa = pm_metal_async_task_status(t->a);
    if (sa == PM_METAL_PENDING || sa == PM_METAL_WAITING) {
      if (pm_metal_time_mono_us() >= t->deadline) {
        pm_metal_async_task_cancel(t->a);
        pm_metal_log("metal-py: oom-isolation fail (timeout)");
        t->step = PY_PROOF_FAIL;
        return PM_METAL_PENDING;
      }

      return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
    }

    /* MemoryError is an uncaught exception like any other — same isolation
     * path (nlr_push in py_exec_and_maybe_main/py_resume_coro) as PY_PROOF_EXC,
     * just a different raise site (GC alloc failure instead of `raise`). */
    if (sa != PM_METAL_ERROR || !pm_metal_py_ready()) {
      pm_metal_logf("metal-py: oom-isolation fail sa=%d ready=%d", sa, pm_metal_py_ready());
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    pm_metal_log("metal-py: oom-isolation ok");
    t->step = PY_PROOF_PMCMD;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_PMCMD:
    /*
     * pmcmd.<name>(*args) (Phase 2b): "echo" is a plain synchronous shell
     * command (no process spawn), so pymergetic.metal.process must read
     * back as idle right after — same call shape a real command would
     * see, just without a background task to observe.
     */
    t->a = pm_metal_py_run_str("import pmcmd\n"
                               "import pymergetic.metal.process as proc\n"
                               "pmcmd.echo('boot-proof-pmcmd')\n"
                               "assert proc.active() == 0\n"
                               "assert proc.current() == 0\n"
                               "assert proc.poll() == (0, 0)\n");
    if (t->a == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: pmcmd fail (spawn)");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->t0       = pm_metal_time_mono_us();
    t->deadline = t->t0 + 3000000ull;
    t->step     = PY_PROOF_PMCMD_WAIT;
    return PM_METAL_PENDING;

  case PY_PROOF_PMCMD_WAIT: {
    int32_t sa;

    pm_metal_run_poll_all();
    sa = pm_metal_async_task_status(t->a);
    if (sa == PM_METAL_PENDING || sa == PM_METAL_WAITING) {
      if (pm_metal_time_mono_us() >= t->deadline) {
        pm_metal_async_task_cancel(t->a);
        pm_metal_log("metal-py: pmcmd fail (timeout)");
        t->step = PY_PROOF_FAIL;
        return PM_METAL_PENDING;
      }

      return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
    }

    if (sa != PM_METAL_DONE) {
      pm_metal_logf("metal-py: pmcmd fail sa=%d", sa);
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    pm_metal_log("metal-py: pmcmd ok");
    t->step = PY_PROOF_MOD;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_MOD:
    /*
     * pymergetic.metal.mod.<name>.<func>() (Phase 2c): bad attribute name
     * raises AttributeError at attribute-access time (not call time), a
     * real registered mod func ("hello"/"run", same target boot_test.c's
     * host-side proof already uses) resolves + coro-spawns + awaits with
     * its result carried through the `await` expression.
     */
    t->a = pm_metal_py_run_str("import pymergetic.metal.mod as mod\n"
                               "got = False\n"
                               "try:\n"
                               "    mod.hello.nonexistent_func\n"
                               "except AttributeError:\n"
                               "    got = True\n"
                               "assert got\n"
                               "async def main():\n"
                               "    r = await mod.hello.run()\n"
                               "    assert r == 0\n");
    if (t->a == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: mod-func fail (spawn)");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->t0       = pm_metal_time_mono_us();
    t->deadline = t->t0 + 3000000ull;
    t->step     = PY_PROOF_MOD_WAIT;
    return PM_METAL_PENDING;

  case PY_PROOF_MOD_WAIT: {
    int32_t sa;

    pm_metal_run_poll_all();
    sa = pm_metal_async_task_status(t->a);
    if (sa == PM_METAL_PENDING || sa == PM_METAL_WAITING) {
      if (pm_metal_time_mono_us() >= t->deadline) {
        pm_metal_async_task_cancel(t->a);
        pm_metal_log("metal-py: mod-func fail (timeout)");
        t->step = PY_PROOF_FAIL;
        return PM_METAL_PENDING;
      }

      return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
    }

    if (sa != PM_METAL_DONE) {
      pm_metal_logf("metal-py: mod-func fail sa=%d", sa);
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    pm_metal_log("metal-py: mod-func ok");
    t->step = PY_PROOF_OK;
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
