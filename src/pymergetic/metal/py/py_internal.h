#ifndef PM_METAL_PY_INTERNAL_H_
#define PM_METAL_PY_INTERNAL_H_

#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/runtime/async/async.h>

#include "py/obj.h"

#include "py_ctx.h"

typedef struct pm_metal_py_job {
  uint32_t                step;
  uint32_t                after_boot_step; /* target once PY_STEP_BOOT finishes */
  char                   *src;
  size_t                  src_len;
  mp_obj_t                py_coro; /* async main / call_async coro, or MP_OBJ_NULL */
  mp_obj_t                call_fn; /* bound callable for PY_STEP_CALL */
  uint32_t                call_arg0;
  pm_metal_async_handle_t pending;
  mp_obj_t                pending_aw; /* awaitable obj waiting for pending */
  int                     exe_exception;
  /*
   * NULL (default): shared/default context, mPyRunLock-serialized, exactly
   * today's behavior. Non-NULL: this job exclusively owns an isolated
   * pm_metal_py_ctx_t (own heap, own VM state) for its whole lifetime — no
   * lock needed, and it can run bytecode in true parallel with every other
   * context on a different CPU. See py_ctx.h + docs/MICROPYTHON.md.
   */
  pm_metal_py_ctx_t *ctx;
} pm_metal_py_job_t;

pm_metal_py_job_t *pm_metal_py_job_current(void);
void pm_metal_py_job_set_pending(pm_metal_py_job_t *job, pm_metal_async_handle_t h, mp_obj_t aw);

void pm_metal_py_c_py_demo_seed(void);
void pm_metal_py_pmcmd_install(void);
void pm_metal_py_mod_install(void);

#ifndef PM_METAL_PY_STDLIB_DIR
#define PM_METAL_PY_STDLIB_DIR "/mods/py/stdlib"
#endif

/** Append /mods + /mods/py/stdlib to the current context's sys.path. */
void pm_metal_py_init_sys_path(void);

/**
 * Ensure loose stdlib under PM_METAL_PY_STDLIB_DIR. No-op when ESP/PXE
 * already staged them; else materialize from iface pack py@metal.stdlib.
 */
void pm_metal_py_stdlib_ensure(void);

/** Materialize py@metal.guest -> /mods (httpd/api/…) if ESP/PXE tree missing. */
void pm_metal_py_guest_ensure(void);

/** Shared-context bytecode lock — see py.c (also used by py_autoload.c). */
int  pm_metal_py_run_try_lock(void);
void pm_metal_py_run_unlock(void);

/** Run each mods/<name>/autoload.py once (idempotent). See py.h. */
int pm_metal_py_autoload_run_once(void);
int pm_metal_py_autoload_done(void);
int pm_metal_py_autoload_for_mod(const char *name);

/**
 * Flush mphalport_metal.c's cross-call stdout accumulator (see that file)
 * -- hands any buffered text with no trailing '\n' yet to
 * pm_metal_shell_out() as its own line. Call after a REPL chunk/statement
 * finishes so output like `print('x', end='')` still shows up before the
 * next ">>> " prompt instead of waiting indefinitely for a newline that
 * may never come.
 */
void pm_metal_py_stdout_flush(void);

#endif
