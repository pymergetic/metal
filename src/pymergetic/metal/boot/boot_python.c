/** @file
  Boot-path MicroPython: always-on blob init + host overlap/yield proofs.
**/
#include <pymergetic/metal/boot/boot.h>
#include <pymergetic/metal/net/ip/ip_ops.h>
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
  PY_PROOF_RANDOM,
  PY_PROOF_RANDOM_WAIT,
  PY_PROOF_HASHLIB,
  PY_PROOF_HASHLIB_WAIT,
  PY_PROOF_OSIO,
  PY_PROOF_OSIO_WAIT,
  PY_PROOF_RE,
  PY_PROOF_RE_WAIT,
  PY_PROOF_TIME,
  PY_PROOF_TIME_WAIT,
  PY_PROOF_ARCHIVE,
  PY_PROOF_ARCHIVE_WAIT,
  PY_PROOF_FSMOD,
  PY_PROOF_FSMOD_WAIT,
  PY_PROOF_NET,
  PY_PROOF_NET_WAIT,
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

    t->a = pm_metal_py_run_script("/mods/py/tests/overlap_a.py");
    t->b = pm_metal_py_run_script("/mods/py/tests/overlap_b.py");
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
    t->a = pm_metal_py_run_script("/mods/py/tests/yield_peer.py");
    t->b = pm_metal_py_run_script("/mods/py/tests/yield_sleeper.py");
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
    static const char src[] =
      "import collections\n"
      "from collections import defaultdict\n"
      "import heapq, bisect, functools, itertools, copy, contextlib\n"
      "import string, pprint, operator, types, warnings, errno, keyword\n"
      "import abc, quopri, html, argparse, struct, binascii\n"
      "import stat, pickle, inspect, traceback, logging, sys\n"
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
      "assert html.escape('<a>') == '&lt;a&gt;'\n"
      "assert stat.S_ISDIR(stat.S_IFDIR) is True\n"
      "assert pickle.loads(pickle.dumps([1, 2, 3])) == [1, 2, 3]\n"
      "assert isinstance(pickle.dumps([1, 2]), str)\n"
      "assert inspect.isclass(int)\n"
      "assert 'ValueError' in ''.join(traceback.format_exception_only(ValueError, "
      "ValueError('x')))\n"
      "logging.basicConfig(level=logging.INFO)\n"
      "logging.getLogger('boot-proof').info('logging ok')\n"
      "try:\n"
      "    raise ValueError('traceback-proof')\n"
      "except ValueError:\n"
      "    tb_type, tb_val, _ = sys.exc_info()\n"
      "    assert tb_val.args[0] == 'traceback-proof'\n"
      "    traceback.print_exc()\n";

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
    t->step = PY_PROOF_RANDOM;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_RANDOM:
    /*
     * extmod/modrandom.c — a real C extmod (not a stdlib.zip .py, see
     * docs/MICROPYTHON.md), so `import random` works with zero Python
     * glue. Seeded from one real pm_metal_random draw
     * (pymergetic.metal.random.seed_u32, dev/random/random_py_bind.c)
     * instead of the extmod's fixed compile-time default, so successive
     * boots don't replay the exact same pseudo-random sequence.
     * random()/uniform() stay untested (float-gated, off in this build).
     */
    t->a = pm_metal_py_run_str("import random\n"
                               "import pymergetic.metal.random as mr\n"
                               "random.seed(mr.seed_u32())\n"
                               "assert 0 <= random.getrandbits(8) <= 255\n"
                               "assert 1 <= random.randint(1, 10) <= 10\n"
                               "assert 5 <= random.randrange(5, 9) < 9\n"
                               "assert random.choice([7, 8, 9]) in (7, 8, 9)\n");
    if (t->a == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: random fail (spawn)");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->t0       = pm_metal_time_mono_us();
    t->deadline = t->t0 + 3000000ull;
    t->step     = PY_PROOF_RANDOM_WAIT;
    return PM_METAL_PENDING;

  case PY_PROOF_RANDOM_WAIT: {
    int32_t sa;

    pm_metal_run_poll_all();
    sa = pm_metal_async_task_status(t->a);
    if (sa == PM_METAL_PENDING || sa == PM_METAL_WAITING) {
      if (pm_metal_time_mono_us() >= t->deadline) {
        pm_metal_async_task_cancel(t->a);
        pm_metal_log("metal-py: random fail (timeout)");
        t->step = PY_PROOF_FAIL;
        return PM_METAL_PENDING;
      }

      return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
    }

    if (sa != PM_METAL_DONE) {
      pm_metal_logf("metal-py: random fail sa=%d", sa);
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    pm_metal_log("metal-py: random ok");
    t->step = PY_PROOF_HASHLIB;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_HASHLIB:
    /*
     * extmod/modhashlib.c, SHA-256 only (extmod/lib/crypto-algorithms/
     * sha256.c fallback — no mbedtls/axtls in this build, so
     * MICROPY_PY_HASHLIB_SHA1/_MD5 stay off, see docs/MICROPYTHON.md).
     * Known vector: sha256("abc").
     */
    t->a =
      pm_metal_py_run_str("import hashlib, binascii\n"
                          "d = hashlib.sha256(b'abc').digest()\n"
                          "assert binascii.hexlify(d) == "
                          "b'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad', "
                          "binascii.hexlify(d)\n");
    if (t->a == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: hashlib fail (spawn)");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->t0       = pm_metal_time_mono_us();
    t->deadline = t->t0 + 3000000ull;
    t->step     = PY_PROOF_HASHLIB_WAIT;
    return PM_METAL_PENDING;

  case PY_PROOF_HASHLIB_WAIT: {
    int32_t sa;

    pm_metal_run_poll_all();
    sa = pm_metal_async_task_status(t->a);
    if (sa == PM_METAL_PENDING || sa == PM_METAL_WAITING) {
      if (pm_metal_time_mono_us() >= t->deadline) {
        pm_metal_async_task_cancel(t->a);
        pm_metal_log("metal-py: hashlib fail (timeout)");
        t->step = PY_PROOF_FAIL;
        return PM_METAL_PENDING;
      }

      return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
    }

    if (sa != PM_METAL_DONE) {
      pm_metal_logf("metal-py: hashlib fail sa=%d", sa);
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    pm_metal_log("metal-py: hashlib ok");
    t->step = PY_PROOF_OSIO;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_OSIO:
    /*
     * Metal's own os/io (mods/py/stdlib_src/os/, io.py — pymergetic.metal.fs.*
     * sync bindings, fs/fs_py_bind.c; not micropython-lib's uos-based
     * versions, this build has no MICROPY_VFS/uos). Full roundtrip: write,
     * read back, stat, listdir, mkdir, rename, unlink.
     */
    t->a = pm_metal_py_run_str("import os, io\n"
                               "p = '/mods/py/py_osio_proof.bin'\n"
                               "f = io.open(p, 'wb')\n"
                               "f.write(b'hello-metal')\n"
                               "f.close()\n"
                               "assert os.path.exists(p)\n"
                               "assert os.path.isfile(p)\n"
                               "assert not os.path.isdir(p)\n"
                               "assert os.stat(p)[6] == len(b'hello-metal')\n"
                               "f = io.open(p, 'rb')\n"
                               "assert f.read() == b'hello-metal'\n"
                               "f.close()\n"
                               "assert p.rsplit('/', 1)[1] in os.listdir('/mods/py')\n"
                               "os.mkdir('/mods/py/py_osio_proof_dir')\n"
                               "assert os.path.isdir('/mods/py/py_osio_proof_dir')\n"
                               "p2 = '/mods/py/py_osio_proof2.bin'\n"
                               "os.rename(p, p2)\n"
                               "assert not os.path.exists(p)\n"
                               "assert os.path.exists(p2)\n"
                               "os.unlink(p2)\n"
                               "assert not os.path.exists(p2)\n");
    if (t->a == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: os+io fail (spawn)");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->t0       = pm_metal_time_mono_us();
    t->deadline = t->t0 + 3000000ull;
    t->step     = PY_PROOF_OSIO_WAIT;
    return PM_METAL_PENDING;

  case PY_PROOF_OSIO_WAIT: {
    int32_t sa;

    pm_metal_run_poll_all();
    sa = pm_metal_async_task_status(t->a);
    if (sa == PM_METAL_PENDING || sa == PM_METAL_WAITING) {
      if (pm_metal_time_mono_us() >= t->deadline) {
        pm_metal_async_task_cancel(t->a);
        pm_metal_log("metal-py: os+io fail (timeout)");
        t->step = PY_PROOF_FAIL;
        return PM_METAL_PENDING;
      }

      return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
    }

    if (sa != PM_METAL_DONE) {
      pm_metal_logf("metal-py: os+io fail sa=%d", sa);
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    pm_metal_log("metal-py: os+io ok");
    t->step = PY_PROOF_RE;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_RE:
    /*
     * extmod/modre.c + lib/re1.5's sources — a real C extmod (not a
     * stdlib.zip .py), see docs/MICROPYTHON.md. match/group/groups/sub.
     * base64/fnmatch ride along here too: both were Needs-glue only
     * because they `import re`, and now that `re` (+ MICROPY_PY_BUILTINS_
     * BYTEARRAY for base64's own decode path) exists, both packed cleanly
     * with zero source changes — promoted to Easy (docs/MICROPYTHON.md).
     */
    t->a = pm_metal_py_run_str("import re\n"
                               "m = re.match(r'(\\d+)-(\\d+)', '123-456')\n"
                               "assert m is not None\n"
                               "assert m.group(1) == '123'\n"
                               "assert m.group(2) == '456'\n"
                               "assert m.groups() == ('123', '456')\n"
                               "assert re.sub(r'\\d+', 'N', '1 and 22') == 'N and N'\n"
                               "assert re.match(r'a.c', 'abc') is not None\n"
                               "assert re.match(r'a.c', 'axc') is not None\n"
                               "assert re.match(r'^abc$', 'abc') is not None\n"
                               "import base64, fnmatch\n"
                               "assert base64.b64decode(base64.b64encode(b'metal')) == b'metal'\n"
                               "assert base64.b16encode(b'ab') == b'6162'\n"
                               "assert fnmatch.fnmatch('boot.py', '*.py')\n"
                               "assert not fnmatch.fnmatch('boot.py', '*.txt')\n");
    if (t->a == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: re fail (spawn)");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->t0       = pm_metal_time_mono_us();
    t->deadline = t->t0 + 3000000ull;
    t->step     = PY_PROOF_RE_WAIT;
    return PM_METAL_PENDING;

  case PY_PROOF_RE_WAIT: {
    int32_t sa;

    pm_metal_run_poll_all();
    sa = pm_metal_async_task_status(t->a);
    if (sa == PM_METAL_PENDING || sa == PM_METAL_WAITING) {
      if (pm_metal_time_mono_us() >= t->deadline) {
        pm_metal_async_task_cancel(t->a);
        pm_metal_log("metal-py: re fail (timeout)");
        t->step = PY_PROOF_FAIL;
        return PM_METAL_PENDING;
      }

      return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
    }

    if (sa != PM_METAL_DONE) {
      pm_metal_logf("metal-py: re fail sa=%d", sa);
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    pm_metal_log("metal-py: re ok");
    t->step = PY_PROOF_TIME;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_TIME:
    /*
     * pymergetic.metal.time (dev/random/time_py_bind.c) + mods/py/
     * stdlib_src/time.py on top — real wall clock (EFI's gRT->GetTime()/
     * BIOS's CMOS RTC, refined by SNTP — net/ntp/ntp.c), no floats
     * anywhere (MICROPY_PY_BUILTINS_FLOAT is off). datetime.py
     * (MICROPY_PY_BUILTINS_PROPERTY, flipped for this) and hmac.py
     * (same flag, HMAC.name/digest_size) ride on top of time+hashlib.
     */
    t->a = pm_metal_py_run_str(
      "import time\n"
      "now = time.time()\n"
      "assert now > 1700000000, now\n" /* sometime after 2023 */
      "lt = time.localtime(now)\n"
      "assert time.mktime(lt) == now, (time.mktime(lt), now)\n" /* mktime inverts localtime, not gmtime */
      "assert time.gmtime(0) == (1970, 1, 1, 0, 0, 0, 3, 1, 0)\n"
      "assert time.gmtime(86400) == (1970, 1, 2, 0, 0, 0, 4, 2, 0)\n"
      "m0 = time.monotonic_ns()\n"
      "time.sleep_ms(1)\n"
      "assert time.monotonic_ns() > m0\n"
      "import datetime\n"
      "d = datetime.date(2024, 3, 1)\n"
      "assert (d - datetime.date(2024, 2, 1)).days == 29, (d - datetime.date(2024, 2, 1)).days\n"
      "dt = datetime.datetime(2024, 1, 1, 12, 30, 0)\n"
      "assert dt.hour == 12 and dt.minute == 30\n"
      "import hmac, hashlib\n"
      "mac = hmac.new(b'key', b'The quick brown fox jumps over the lazy dog', "
      "hashlib.sha256).hexdigest()\n"
      "assert mac == 'f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8', mac\n");
    if (t->a == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: time fail (spawn)");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->t0       = pm_metal_time_mono_us();
    t->deadline = t->t0 + 3000000ull;
    t->step     = PY_PROOF_TIME_WAIT;
    return PM_METAL_PENDING;

  case PY_PROOF_TIME_WAIT: {
    int32_t sa;

    pm_metal_run_poll_all();
    sa = pm_metal_async_task_status(t->a);
    if (sa == PM_METAL_PENDING || sa == PM_METAL_WAITING) {
      if (pm_metal_time_mono_us() >= t->deadline) {
        pm_metal_async_task_cancel(t->a);
        pm_metal_log("metal-py: time fail (timeout)");
        t->step = PY_PROOF_FAIL;
        return PM_METAL_PENDING;
      }

      return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
    }

    if (sa != PM_METAL_DONE) {
      pm_metal_logf("metal-py: time fail sa=%d", sa);
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    pm_metal_log("metal-py: time ok");
    t->step = PY_PROOF_ARCHIVE;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_ARCHIVE:
    /*
     * tarfile.py (pymergetic.metal.tar.* -> util/tar.c's own ustar
     * reader/writer, util/tar_py_bind.c) write-then-read roundtrip, plus
     * zlib/gzip.py (extmod/moddeflate.c's real DEFLATE engine, backed by
     * io.py's native uio.BytesIO — see mpconfigport.h's MICROPY_PY_DEFLATE
     * note) compress/decompress roundtrip.
     */
    t->a =
      pm_metal_py_run_str("import tarfile\n"
                          "w = tarfile.TarWriter()\n"
                          "w.add_bytes('hello.txt', b'hello-metal-tar')\n"
                          "w.add_dir('sub')\n"
                          "data = w.finish()\n"
                          "tf = tarfile.open_bytes(data)\n"
                          "names = tf.getnames()\n"
                          "assert 'hello.txt' in names and 'sub' in names, names\n"
                          "info = tarfile.TarInfo('hello.txt', len(b'hello-metal-tar'), False)\n"
                          "assert tf.extractfile(info) == b'hello-metal-tar'\n"
                          "tf.close()\n"
                          "import zlib\n"
                          "c = zlib.compress(b'metal-metal-metal-metal-metal')\n"
                          "assert zlib.decompress(c) == b'metal-metal-metal-metal-metal'\n"
                          "import gzip\n"
                          "gz = gzip.compress(b'gzip-roundtrip-metal')\n"
                          "assert gzip.decompress(gz) == b'gzip-roundtrip-metal'\n"
                          /* Checkpoint: gzip.open() wraps a *real* on-disk
                           * io.FileIO now (not just BytesIO) -- needs
                           * FileIO subclassing native io.IOBase, see
                           * io.py's own note on why. */
                          "gp = '/mods/py/py_gzip_file_proof.gz'\n"
                          "with gzip.open(gp, 'wb') as f:\n"
                          "    f.write(b'gzip-on-disk-metal-proof')\n"
                          "with gzip.open(gp, 'rb') as f:\n"
                          "    assert f.read() == b'gzip-on-disk-metal-proof'\n"
                          "import os\n"
                          "os.unlink(gp)\n");
    if (t->a == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: archive fail (spawn)");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->t0       = pm_metal_time_mono_us();
    t->deadline = t->t0 + 3000000ull;
    t->step     = PY_PROOF_ARCHIVE_WAIT;
    return PM_METAL_PENDING;

  case PY_PROOF_ARCHIVE_WAIT: {
    int32_t sa;

    pm_metal_run_poll_all();
    sa = pm_metal_async_task_status(t->a);
    if (sa == PM_METAL_PENDING || sa == PM_METAL_WAITING) {
      if (pm_metal_time_mono_us() >= t->deadline) {
        pm_metal_async_task_cancel(t->a);
        pm_metal_log("metal-py: archive fail (timeout)");
        t->step = PY_PROOF_FAIL;
        return PM_METAL_PENDING;
      }

      return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
    }

    if (sa != PM_METAL_DONE) {
      pm_metal_logf("metal-py: archive fail sa=%d", sa);
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    pm_metal_log("metal-py: archive ok");
    t->step = PY_PROOF_FSMOD;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_FSMOD:
    /*
     * pathlib/shutil/tempfile (packaging-only, os+io backed) + unittest
     * (simple TestCase run) + textwrap (re-free _split(), see stdlib_src/
     * textwrap.py's Metal patch note) + uu (pure-Python codec, no
     * binascii b2a_uu/a2b_uu upstream — stdlib_src/uu.py's own note).
     */
    t->a = pm_metal_py_run_str(
      "import pathlib\n"
      "p = pathlib.Path('/mods/py/py_pathlib_proof.bin')\n"
      "p.write_bytes(b'pathlib-metal')\n"
      "assert p.exists() and p.read_bytes() == b'pathlib-metal'\n"
      "p.unlink()\n"
      "assert not p.exists()\n"
      "import shutil, io, os\n"
      "src_p = '/mods/py/py_shutil_src.bin'\n"
      "dst_p = '/mods/py/py_shutil_dst.bin'\n"
      "f = io.open(src_p, 'wb')\n"
      "f.write(b'shutil-metal')\n"
      "f.close()\n"
      "shutil.copyfile(src_p, dst_p)\n"
      "assert os.path.exists(dst_p)\n"
      "f = io.open(dst_p, 'rb')\n"
      "assert f.read() == b'shutil-metal'\n"
      "f.close()\n"
      "os.unlink(src_p)\n"
      "os.unlink(dst_p)\n"
      "import tempfile\n"
      "tf = tempfile.TemporaryFile()\n"
      "tf.write(b'tmp-metal')\n"
      "tf.seek(0)\n"
      "assert tf.read() == b'tmp-metal'\n"
      "tf.close()\n"
      "import unittest\n"
      "class T(unittest.TestCase):\n"
      "    def test_ok(self):\n"
      "        self.assertEqual(1 + 1, 2)\n"
      "r = unittest.TestResult()\n"
      "suite = unittest.TestSuite()\n"
      "suite.addTest(T)\n"
      "suite.run(r)\n"
      "assert r.errorsNum == 0 and r.failuresNum == 0 and r.testsRun == 1, "
      "(r.errorsNum, r.failuresNum, r.testsRun)\n"
      "import textwrap\n"
      "assert textwrap.wrap('metal metal metal metal metal', 12) == ["
      "'metal metal', 'metal metal', 'metal']\n"
      "assert textwrap.dedent('  a\\n  b\\n') == 'a\\nb\\n'\n"
      "import uu, io\n"
      "src = io.BytesIO(b'uu-roundtrip-metal-proof')\n"
      "enc = io.BytesIO()\n"
      "uu.encode(src, enc, name='proof.bin')\n"
      "dec_in = io.BytesIO(enc.getvalue())\n"
      "dec_out = io.BytesIO()\n"
      "uu.decode(dec_in, dec_out)\n"
      "assert dec_out.getvalue() == b'uu-roundtrip-metal-proof', dec_out.getvalue()\n"
      "import json\n"
      "s = json.dumps({'a': 1, 'b': [1, 2, 3], 'c': 'metal'})\n"
      "assert json.loads(s) == {'a': 1, 'b': [1, 2, 3], 'c': 'metal'}, s\n");
    if (t->a == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: fsmod fail (spawn)");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->t0       = pm_metal_time_mono_us();
    t->deadline = t->t0 + 3000000ull;
    t->step     = PY_PROOF_FSMOD_WAIT;
    return PM_METAL_PENDING;

  case PY_PROOF_FSMOD_WAIT: {
    int32_t sa;

    pm_metal_run_poll_all();
    sa = pm_metal_async_task_status(t->a);
    if (sa == PM_METAL_PENDING || sa == PM_METAL_WAITING) {
      if (pm_metal_time_mono_us() >= t->deadline) {
        pm_metal_async_task_cancel(t->a);
        pm_metal_log("metal-py: fsmod fail (timeout)");
        t->step = PY_PROOF_FAIL;
        return PM_METAL_PENDING;
      }

      return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
    }

    if (sa != PM_METAL_DONE) {
      pm_metal_logf("metal-py: fsmod fail sa=%d", sa);
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    pm_metal_log("metal-py: fsmod ok");
    t->step = PY_PROOF_NET;
    return PM_METAL_PENDING;
  }

  case PY_PROOF_NET:
    /*
     * pymergetic.metal.net (net_py_bind.c) — offline-safe: loopback only,
     * no real network required (docs/MICROPYTHON.md's net plan explicitly
     * excludes online proofs). listen()+accept() are exercised here for
     * the first time in this codebase (every earlier net_lwip.c proof —
     * ping/ntp/tftp/http fetch — was client-only); by the time accept()
     * runs, connect() has already completed, so PromoteAcceptPcb's
     * fast-path in LwipAccept should fire immediately rather than parking
     * on its own 30s internal deadline — this step's own deadline below
     * is just a backstop.
     */
    t->a =
      pm_metal_py_run_str("import pymergetic.metal.net as net\n"
                          "async def main():\n"
                          "    lsock = net.socket()\n"
                          "    await net.listen(lsock, 34567)\n"
                          "    csock = net.socket()\n"
                          "    ok = await net.connect(csock, '127.0.0.1', 34567)\n"
                          "    assert ok == 1, ok\n"
                          "    ssock = await net.accept(lsock)\n"
                          "    assert ssock != 0, ssock\n"
                          "    assert net.send(csock, b'ping-metal') == len(b'ping-metal')\n"
                          "    got = await net.recv(ssock, 64)\n"
                          "    assert got == b'ping-metal', got\n"
                          "    assert net.send(ssock, b'pong-metal') == len(b'pong-metal')\n"
                          "    got2 = await net.recv(csock, 64)\n"
                          "    assert got2 == b'pong-metal', got2\n"
                          "    dns_ok = await net.dns('localhost')\n"
                          "    assert dns_ok == 1, dns_ok\n"
                          "    assert net.dns_last_ntoa() == '127.0.0.1', net.dns_last_ntoa()\n"
                          "    net.close(csock)\n"
                          "    net.close(ssock)\n"
                          "    net.close(lsock)\n");
    if (t->a == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_log("metal-py: net fail (spawn)");
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    t->t0       = pm_metal_time_mono_us();
    t->deadline = t->t0 + 5000000ull;
    t->step     = PY_PROOF_NET_WAIT;
    return PM_METAL_PENDING;

  case PY_PROOF_NET_WAIT: {
    int32_t sa;

    /*
     * Unlike every other proof step's plain pm_metal_run_poll_all(), this
     * one actually touches the network stack (listen/connect/accept/recv
     * on lwIP) — lwIP is NO_SYS and only makes progress (TCP callbacks
     * firing, loopback packets delivered) when something calls
     * pm_metal_net_ip_poll(), same as pm_metal_async_session_pump() does for
     * the normal post-boot shell loop. Boot proofs run before that pump
     * ever starts, so this step has to drive it itself.
     */
    pm_metal_net_ip_poll();
    pm_metal_run_poll_all();
    sa = pm_metal_async_task_status(t->a);
    if (sa == PM_METAL_PENDING || sa == PM_METAL_WAITING) {
      if (pm_metal_time_mono_us() >= t->deadline) {
        pm_metal_async_task_cancel(t->a);
        pm_metal_log("metal-py: net fail (timeout)");
        t->step = PY_PROOF_FAIL;
        return PM_METAL_PENDING;
      }

      return pm_metal_async_await(h, pm_metal_async_sleep_us(2000));
    }

    if (sa != PM_METAL_DONE) {
      pm_metal_logf("metal-py: net fail sa=%d", sa);
      t->step = PY_PROOF_FAIL;
      return PM_METAL_PENDING;
    }

    pm_metal_log("metal-py: net ok");
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
