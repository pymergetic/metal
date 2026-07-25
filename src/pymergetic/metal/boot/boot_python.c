/** @file
  Boot-path MicroPython: always-on blob init + host overlap/yield proofs.
**/
#include <pymergetic/metal/boot/boot.h>
#include <pymergetic/metal/fs/fs.h>
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
  PY_PROOF_FRESH,
  PY_PROOF_FRESH_WAIT,
  PY_PROOF_SHADOW,
  PY_PROOF_SHADOW_WAIT,
  PY_PROOF_PARALLEL,
  PY_PROOF_PARALLEL_WAIT,
  PY_PROOF_STDLIB,
  PY_PROOF_STDLIB_WAIT,
  PY_PROOF_ISOLATED_STDLIB,
  PY_PROOF_ISOLATED_STDLIB_WAIT,
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
    t->step = PY_PROOF_FRESH;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_FRESH:
    /*
     * Phase 2d: pymergetic.metal.mod.<name>.fresh() — two concurrent
     * "async with" scopes against the same MULTI-cap test mod must not
     * share state: fresh_counter's g_counter lives in each fresh
     * instance's own wasm linear memory, so bumping each once must
     * read back 1 in both, never 1 then 2.
     */
    t->a = pm_metal_py_run_str("import pymergetic.metal.mod as mod\n"
                               "async def main():\n"
                               "    async with mod.fresh_counter.fresh() as a:\n"
                               "        async with mod.fresh_counter.fresh() as b:\n"
                               "            va = await a.bump()\n"
                               "            vb = await b.bump()\n"
                               "            assert va == 1 and vb == 1\n");
    if (t->a == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: fresh fail (spawn)");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->t0       = pm_metal_time_mono_us();
    t->deadline = t->t0 + 3000000ull;
    t->step     = PY_PROOF_FRESH_WAIT;
    return PM_METAL_PENDING;

  case PY_PROOF_FRESH_WAIT: {
    int32_t sa;

    pm_metal_run_poll_all();
    sa = pm_metal_async_task_status(t->a);
    if (sa == PM_METAL_PENDING || sa == PM_METAL_WAITING) {
      if (pm_metal_time_mono_us() >= t->deadline) {
        pm_metal_async_task_cancel(t->a);
        pm_metal_log("metal-py: fresh fail (timeout)");
        t->step = PY_PROOF_FAIL;
        return PM_METAL_PENDING;
      }

      return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
    }

    if (sa != PM_METAL_DONE) {
      pm_metal_logf("metal-py: fresh fail sa=%d", sa);
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    pm_metal_log("metal-py: fresh ok");
    t->step = PY_PROOF_SHADOW;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_SHADOW: {
    /*
     * Phase 3 import-shadow regression: a same-named .py sitting on
     * sys.path must never shadow a C-registered dotted module.
     * pm_metal_py_init() populates mp_loaded_modules_dict (binds/pmcmd/
     * mod install) before any script or zip content is ever imported,
     * and builtinimport.c's process_import_at_level checks that dict
     * FIRST — a hit there returns immediately, never touching sys.path
     * (see external/micropython/py/builtinimport.c ~line 380). Prove it
     * by planting a decoy right on /mods/py (already on sys.path) under
     * the exact name of a C module ("pmcmd", the short-name exception —
     * same dict, same code path a "pymergetic/metal.py" decoy would hit,
     * chosen here to avoid needing a fresh subdirectory on either ESP
     * backend) and confirming its sentinel never surfaces.
     */
    static const char decoy[] = "SHADOWED = True\n";

    if (pm_metal_fs_write("/mods/py/pmcmd.py", decoy, (uint32_t)(sizeof(decoy) - 1u)) !=
        (uint32_t)(sizeof(decoy) - 1u)) {
      pm_metal_log("metal-py: shadow fail (decoy write)");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->a = pm_metal_py_run_str("import pmcmd\n"
                               "assert not hasattr(pmcmd, 'SHADOWED')\n"
                               "assert hasattr(pmcmd, 'echo')\n");
    if (t->a == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: shadow fail (spawn)");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->t0       = pm_metal_time_mono_us();
    t->deadline = t->t0 + 3000000ull;
    t->step     = PY_PROOF_SHADOW_WAIT;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_SHADOW_WAIT: {
    int32_t sa;

    pm_metal_run_poll_all();
    sa = pm_metal_async_task_status(t->a);
    if (sa == PM_METAL_PENDING || sa == PM_METAL_WAITING) {
      if (pm_metal_time_mono_us() >= t->deadline) {
        pm_metal_async_task_cancel(t->a);
        pm_metal_log("metal-py: shadow fail (timeout)");
        t->step = PY_PROOF_FAIL;
        return PM_METAL_PENDING;
      }

      return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
    }

    if (sa != PM_METAL_DONE) {
      pm_metal_logf("metal-py: shadow fail sa=%d", sa);
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    pm_metal_log("metal-py: shadow ok");
    t->step = PY_PROOF_PARALLEL;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_PARALLEL:
    /*
     * Task-local GC contexts (pm_metal_py_run_str_isolated / py_ctx.c):
     * two tasks, each its own exclusively-owned VM context (own heap,
     * own globals, no mPyRunLock). Each does real CPU-bound bytecode
     * work (a busy loop — this is the part a shared/serialized context
     * could never truly overlap, unlike PY_PROOF_OVERLAP's I/O-bound
     * sleeps above, which any async design can overlap) with a
     * different value baked into a same-named global, then a real
     * await/park/resume round-trip before checking it survived. If the
     * two contexts secretly shared one heap/globals dict, whichever
     * task's write ran last would stomp the other's — at least one
     * assert below would fail (ERROR, not DONE) with high probability
     * regardless of scheduling order. Passing proves the heaps are
     * genuinely disjoint; the two busy loops finishing without one
     * blocking the other (checked as a soft signal below, not a hard
     * gate — wall-clock ratios are inherently emulation/CI-timing
     * dependent) is the concurrent-execution half of the claim.
     */
    t->a = pm_metal_py_run_str_isolated("import pymergetic.metal.aio as a\n"
                                        "X = 111\n"
                                        "i = 0\n"
                                        "s = 0\n"
                                        "while i < 150000:\n"
                                        "    s = s + i\n"
                                        "    i = i + 1\n"
                                        "async def main():\n"
                                        "    await a.sleep_us(30000)\n"
                                        "    assert X == 111\n",
                                        0);
    t->b = pm_metal_py_run_str_isolated("import pymergetic.metal.aio as a\n"
                                        "X = 222\n"
                                        "i = 0\n"
                                        "s = 0\n"
                                        "while i < 150000:\n"
                                        "    s = s + i\n"
                                        "    i = i + 1\n"
                                        "async def main():\n"
                                        "    await a.sleep_us(30000)\n"
                                        "    assert X == 222\n",
                                        0);
    if (t->a == PM_METAL_ASYNC_HANDLE_INVALID || t->b == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: parallel-ctx fail (spawn)");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->t0       = pm_metal_time_mono_us();
    t->deadline = t->t0 + 5000000ull;
    t->step     = PY_PROOF_PARALLEL_WAIT;
    return PM_METAL_PENDING;

  case PY_PROOF_PARALLEL_WAIT: {
    int32_t  sa;
    int32_t  sb;
    uint64_t dt;

    pm_metal_run_poll_all();
    sa = pm_metal_async_task_status(t->a);
    sb = pm_metal_async_task_status(t->b);
    if ((sa == PM_METAL_PENDING || sa == PM_METAL_WAITING) ||
        (sb == PM_METAL_PENDING || sb == PM_METAL_WAITING)) {
      if (pm_metal_time_mono_us() >= t->deadline) {
        pm_metal_async_task_cancel(t->a);
        pm_metal_async_task_cancel(t->b);
        pm_metal_log("metal-py: parallel-ctx fail (timeout)");
        t->step = PY_PROOF_FAIL;
        return PM_METAL_PENDING;
      }

      return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
    }

    dt = pm_metal_time_mono_us() - t->t0;
    if (sa != PM_METAL_DONE || sb != PM_METAL_DONE) {
      pm_metal_logf("metal-py: parallel-ctx fail (heaps not disjoint?) sa=%d sb=%d", sa, sb);
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    /* Isolation is proven above (both DONE); this is just an informational
     * data point on whether the two busy-loop+sleep contexts genuinely
     * overlapped, not a pass/fail gate. */
    pm_metal_logf("metal-py: parallel-ctx ok (dt=%uus)", (uint32_t)dt);
    t->step = PY_PROOF_STDLIB;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_STDLIB: {
    /*
     * mods/py/stdlib.zip's "Easy" pure-Python pack (docs/MICROPYTHON.md),
     * read straight out of the STORED-zip via py_zip_read.c's in-place
     * archive reader (no extraction to loose files) — imports every packed
     * module and exercises one real call per module, so a module that
     * imports but is subtly broken (wrong builtin alias, missing extmod
     * flag) still fails loudly instead of just "not raising ImportError".
     */
    static const char src[] = "import collections\n"
                              "from collections import defaultdict\n"
                              "import heapq, bisect, functools, itertools, copy, contextlib\n"
                              "import string, pprint, operator, types, warnings, errno, keyword\n"
                              "import abc, quopri, html, argparse, struct, binascii\n"
                              "d = defaultdict(int)\n"
                              "d['a'] += 1\n"
                              "assert d['a'] == 1\n"
                              "hq = []\n"
                              "heapq.heappush(hq, 3)\n"
                              "heapq.heappush(hq, 1)\n"
                              "assert heapq.heappop(hq) == 1\n"
                              "assert bisect.bisect([1, 3, 5], 4) == 2\n"
                              "assert functools.reduce(lambda a, b: a + b, [1, 2, 3]) == 6\n"
                              "assert list(itertools.chain([1, 2], [3])) == [1, 2, 3]\n"
                              "assert copy.copy([1, 2, 3]) == [1, 2, 3]\n"
                              "@contextlib.contextmanager\n"
                              "def cm():\n"
                              "    yield 42\n"
                              "with cm() as v:\n"
                              "    assert v == 42\n"
                              "assert errno.ENOENT > 0\n"
                              "assert keyword.iskeyword('if')\n"
                              "assert abc.abstractmethod(lambda: 1)() == 1\n"
                              "ap = argparse.ArgumentParser()\n"
                              "ap.add_argument('--x')\n"
                              "assert ap.parse_args(['--x', '1']).x == '1'\n"
                              "packed = struct.pack('<I', 258)\n"
                              "assert struct.unpack('<I', packed)[0] == 258\n"
                              "assert binascii.hexlify(b'ab') == b'6162'\n"
                              "assert html.escape('<a>') == '&lt;a&gt;'\n";

    t->a = pm_metal_py_run_str(src);
    if (t->a == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: stdlib fail (spawn)");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->t0       = pm_metal_time_mono_us();
    t->deadline = t->t0 + 3000000ull;
    t->step     = PY_PROOF_STDLIB_WAIT;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_STDLIB_WAIT: {
    int32_t sa;

    pm_metal_run_poll_all();
    sa = pm_metal_async_task_status(t->a);
    if (sa == PM_METAL_PENDING || sa == PM_METAL_WAITING) {
      if (pm_metal_time_mono_us() >= t->deadline) {
        pm_metal_async_task_cancel(t->a);
        pm_metal_log("metal-py: stdlib fail (timeout)");
        t->step = PY_PROOF_FAIL;
        return PM_METAL_PENDING;
      }

      return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
    }

    if (sa != PM_METAL_DONE) {
      pm_metal_logf("metal-py: stdlib fail sa=%d", sa);
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    pm_metal_log("metal-py: stdlib ok");
    t->step = PY_PROOF_ISOLATED_STDLIB;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_ISOLATED_STDLIB:
    /*
     * docs/TODO.md "Isolated-context ergonomics": an isolated context
     * (its own disjoint heap, see PY_PROOF_PARALLEL above) must be able
     * to import from stdlib.zip too, not just pymergetic.metal / pmcmd /
     * mod — pm_metal_py_ctx_create() now seeds this context's own
     * sys.path the same way pm_metal_py_init() seeds the shared one's.
     * heapq is an arbitrary pick from the "Easy" pack; the point is
     * proving the import machinery (sys.path + py_zip_read.c's in-place
     * archive reader) works against this context's own state, not the
     * shared one's.
     */
    t->a = pm_metal_py_run_str_isolated("import heapq\n"
                                        "hq = []\n"
                                        "heapq.heappush(hq, 5)\n"
                                        "heapq.heappush(hq, 2)\n"
                                        "assert heapq.heappop(hq) == 2\n",
                                        0);
    if (t->a == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: isolated-stdlib fail (spawn)");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->t0       = pm_metal_time_mono_us();
    t->deadline = t->t0 + 3000000ull;
    t->step     = PY_PROOF_ISOLATED_STDLIB_WAIT;
    return PM_METAL_PENDING;

  case PY_PROOF_ISOLATED_STDLIB_WAIT: {
    int32_t sa;

    pm_metal_run_poll_all();
    sa = pm_metal_async_task_status(t->a);
    if (sa == PM_METAL_PENDING || sa == PM_METAL_WAITING) {
      if (pm_metal_time_mono_us() >= t->deadline) {
        pm_metal_async_task_cancel(t->a);
        pm_metal_log("metal-py: isolated-stdlib fail (timeout)");
        t->step = PY_PROOF_FAIL;
        return PM_METAL_PENDING;
      }

      return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
    }

    if (sa != PM_METAL_DONE) {
      pm_metal_logf("metal-py: isolated-stdlib fail sa=%d", sa);
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    pm_metal_log("metal-py: isolated-stdlib ok");
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
