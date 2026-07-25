/** @file Always-on µPy blob + Python task = Metal task (port-neutral). */
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/fs/fs.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/runtime/mem/mem.h>
#include <runtime/slot/slot_table.h>
#include <runtime/time/cpu.h>

#include "port/micropython_embed.h"
#include "py/compile.h"
#include "py/lexer.h"
#include "py/runtime.h"
#include "py/obj.h"

#include "py_internal.h"

enum {
  PY_STEP_LOAD = 0,
  PY_STEP_EXEC,
  PY_STEP_CALL,
  PY_STEP_RESUME,
  PY_STEP_DONE
};

static void              *g_blob;
static size_t             g_blob_bytes;
static int                g_ready;
static pm_metal_py_job_t *g_current_job;
/*
 * Shared-blob exclusivity until task-local GC (§5). Held only while in
 * bytecode; released before park on Metal await so sleeps can overlap.
 */
static volatile uint32_t mPyRunLock;

/*
 * Non-blocking: single CAS attempt, 0 acquired / -1 busy. Never spins —
 * the async engine is now a per-CPU work-stealing ring (run.c), so a
 * blocking spin here would peg a whole runner behind a peer Python task's
 * bytecode. Callers that run inside an async step (py_job_step) must
 * park-and-retry on busy (pm_metal_async_await(self_h, pm_metal_async_yield()))
 * instead of looping; pm_metal_py_call() (outside any step, can't await)
 * bounds its own retry loop below instead.
 */
static int py_run_try_lock(void)
{
  return (pm_metal_slot_port_cas32(&mPyRunLock, 0, 1) == 0) ? 0 : -1;
}

static void py_run_unlock(void)
{
  (void)pm_metal_slot_port_cas32(&mPyRunLock, 1, 0);
}

pm_metal_py_job_t *pm_metal_py_job_current(void)
{
  return g_current_job;
}

void pm_metal_py_job_set_pending(pm_metal_py_job_t *job, pm_metal_async_handle_t h, mp_obj_t aw)
{
  if (job == NULL) {
    return;
  }
  job->pending    = h;
  job->pending_aw = aw;
}

int pm_metal_py_ready(void)
{
  return g_ready;
}

size_t pm_metal_py_blob_bytes(void)
{
  return g_blob_bytes;
}

void pm_metal_py_c_py_demo_seed(void)
{
  static const char src[] = "import pymergetic.metal.aio as _a\n"
                            "\n"
                            "def add(a, b):\n"
                            "    return a + b\n"
                            "\n"
                            "async def blink(us):\n"
                            "    await _a.sleep_us(us)\n";
  nlr_buf_t         nlr;
  mp_obj_t          mod;
  mp_obj_dict_t    *globals;
  mp_obj_dict_t    *old_g;
  mp_obj_dict_t    *old_l;

  mod     = mp_obj_new_module(qstr_from_str("c_py_demo"));
  globals = (mp_obj_dict_t *)MP_OBJ_TO_PTR(((mp_obj_module_t *)MP_OBJ_TO_PTR(mod))->globals);
  mp_obj_dict_store(MP_OBJ_FROM_PTR(globals),
                    MP_OBJ_NEW_QSTR(qstr_from_str("__name__")),
                    MP_OBJ_NEW_QSTR(qstr_from_str("c_py_demo")));

  old_g = mp_globals_get();
  old_l = mp_locals_get();
  mp_globals_set(globals);
  mp_locals_set(globals);

  if (nlr_push(&nlr) == 0) {
    mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, sizeof(src) - 1u, 0);
    qstr        source_name    = lex->source_name;
    mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
    mp_obj_t        module_fun = mp_compile(&parse_tree, source_name, false);
    mp_call_function_0(module_fun);
    nlr_pop();
  } else {
    mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
  }

  mp_globals_set(old_g);
  mp_locals_set(old_l);
  mp_obj_dict_store(MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_loaded_modules_dict)),
                    MP_OBJ_NEW_QSTR(qstr_from_str("c_py_demo")),
                    mod);
}

int pm_metal_py_init(void)
{
  int stack_top;

  if (g_ready) {
    return 0;
  }
  g_blob = pm_metal_mem_map(PM_METAL_PY_BLOB_BYTES);
  if (g_blob == NULL) {
    pm_metal_log("py: MAP blob alloc failed");
    return -1;
  }
  g_blob_bytes = PM_METAL_PY_BLOB_BYTES;
  mp_embed_init(g_blob, g_blob_bytes, &stack_top);
  pm_metal_py_binds_install();
  pm_metal_py_pmcmd_install();
  pm_metal_py_mod_install();
  pm_metal_py_c_py_demo_seed();
  pm_metal_py_zip_init_sys_path();
  g_ready = 1;
  /* Boot tree prints "|   +-- py       ok"; no extra chatty line. */
  return 0;
}

static int py_read_path(const char *path, char **out, size_t *out_len)
{
  uint32_t sz;
  uint32_t n;
  char    *buf;

  sz = pm_metal_fs_size(path);
  if (sz == 0 || sz > 256u * 1024u) {
    return -1;
  }
  buf = (char *)pm_metal_mem_alloc(sz + 1u, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (buf == NULL) {
    return -1;
  }
  n = pm_metal_fs_read(path, buf, sz);
  if (n == 0) {
    pm_metal_mem_free(buf);
    return -1;
  }
  buf[n]   = '\0';
  *out     = buf;
  *out_len = n;
  return 0;
}

/* Caller (py_job_step) already holds mPyRunLock via py_run_try_lock(). */
static int py_exec_and_maybe_main(pm_metal_py_job_t *job)
{
  nlr_buf_t nlr;
  int       rc;

  g_current_job = job;
  if (nlr_push(&nlr) == 0) {
    mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, job->src, job->src_len, 0);
    qstr        source_name    = lex->source_name;
    mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
    mp_obj_t        module_fun = mp_compile(&parse_tree, source_name, true);
    mp_call_function_0(module_fun);

    {
      qstr           mq = qstr_from_str("main");
      mp_map_elem_t *el = mp_map_lookup(&mp_globals_get()->map, MP_OBJ_NEW_QSTR(mq), MP_MAP_LOOKUP);
      if (el != NULL && mp_obj_is_callable(el->value)) {
        job->py_coro = mp_call_function_0(el->value);
      }
    }
    nlr_pop();
    rc = 0;
  } else {
    mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
    job->exe_exception = 1;
    rc                 = -1;
  }
  g_current_job = NULL;
  py_run_unlock();
  return rc;
}

/* Caller (py_job_step) already holds mPyRunLock via py_run_try_lock(). */
static int py_call_bound(pm_metal_py_job_t *job)
{
  nlr_buf_t nlr;
  mp_obj_t  arg;
  mp_obj_t  ret;
  int       rc;

  if (job->call_fn == MP_OBJ_NULL) {
    return -1;
  }
  g_current_job = job;
  arg           = mp_obj_new_int_from_uint(job->call_arg0);
  if (nlr_push(&nlr) == 0) {
    ret = mp_call_function_1(job->call_fn, arg);
    /*
     * Mirror of pm_metal_py_call's sync-into-async guard: this path
     * (pm_metal_py_fn_call_async) always drives job->py_coro through
     * py_resume_coro()'s mp_resume() next, which expects a real
     * generator/coroutine. Binding a plain sync function and calling it
     * here would otherwise "work" only via mp_resume's incidental
     * AttributeError on .send() — make the mismatch explicit instead.
     */
    if (!mp_obj_is_type(ret, &mp_type_gen_instance)) {
      pm_metal_log("py: async call target did not return a coroutine");
      job->exe_exception = 1;
      job->py_coro        = MP_OBJ_NULL;
      rc                  = -1;
    } else {
      job->py_coro = ret;
      rc           = 0;
    }
    nlr_pop();
  } else {
    mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
    job->exe_exception = 1;
    rc                 = -1;
  }
  g_current_job = NULL;
  py_run_unlock();
  return rc;
}

/* Caller (py_job_step) already holds mPyRunLock via py_run_try_lock(). */
static pm_metal_status_t py_resume_coro(pm_metal_py_job_t *job)
{
  mp_obj_t            ret_val = MP_OBJ_NULL;
  mp_vm_return_kind_t kind;
  nlr_buf_t           nlr;

  g_current_job   = job;
  job->pending    = 0;
  job->pending_aw = MP_OBJ_NULL;

  if (nlr_push(&nlr) == 0) {
    kind = mp_resume(job->py_coro, mp_const_none, MP_OBJ_NULL, &ret_val);
    nlr_pop();
  } else {
    mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
    job->exe_exception = 1;
    g_current_job      = NULL;
    py_run_unlock();
    return PM_METAL_ERROR;
  }
  g_current_job = NULL;
  /* Unlock before park so another py task can run while we sleep. */
  py_run_unlock();

  if (kind == MP_VM_RETURN_NORMAL) {
    return PM_METAL_DONE;
  }
  if (kind == MP_VM_RETURN_YIELD) {
    if (job->pending != 0) {
      return PM_METAL_WAITING;
    }
    return PM_METAL_DONE;
  }
  if (kind == MP_VM_RETURN_EXCEPTION) {
    mp_obj_print_exception(&mp_plat_print, ret_val);
    job->exe_exception = 1;
    return PM_METAL_ERROR;
  }
  return PM_METAL_DONE;
}

static void py_job_release(void *state)
{
  pm_metal_py_job_t *job = (pm_metal_py_job_t *)state;

  if (job == NULL) {
    return;
  }
  if (job->src != NULL) {
    pm_metal_mem_free(job->src);
    job->src = NULL;
  }
}

static pm_metal_status_t py_job_step(pm_metal_async_handle_t self_h)
{
  pm_metal_py_job_t *job;

  job = (pm_metal_py_job_t *)(uintptr_t)pm_metal_async_coro_state(self_h);
  if (job == NULL) {
    return PM_METAL_ERROR;
  }

  /*
   * PY_STEP_LOAD does its own fs I/O, no bytecode yet — only EXEC/CALL/
   * RESUME touch the shared blob, so only those try the lock, right
   * before the call that needs it. Busy: park-and-retry at the same
   * step, never spin the runner.
   */
  switch (job->step) {
  case PY_STEP_LOAD: {
    char   path[256];
    char  *body     = NULL;
    size_t body_len = 0;

    if (job->src == NULL || job->src_len >= sizeof(path)) {
      return PM_METAL_ERROR;
    }
    memcpy(path, job->src, job->src_len + 1u);
    pm_metal_mem_free(job->src);
    job->src     = NULL;
    job->src_len = 0;
    if (py_read_path(path, &body, &body_len) != 0) {
      pm_metal_shell_out("py: read failed");
      return PM_METAL_ERROR;
    }
    job->src     = body;
    job->src_len = body_len;
    job->step    = PY_STEP_EXEC;
  }
    /* fallthrough */
  case PY_STEP_EXEC:
    /*
     * py_exec_and_maybe_main() unlocks internally on every path (success,
     * script exception, or "no main()") before returning — this call site
     * only needs to gate *entry*, not release.
     */
    if (py_run_try_lock() != 0) {
      return pm_metal_async_await(self_h, pm_metal_async_yield());
    }
    if (py_exec_and_maybe_main(job) != 0) {
      job->step = PY_STEP_DONE;
      return PM_METAL_ERROR;
    }
    if (job->py_coro == MP_OBJ_NULL) {
      job->step = PY_STEP_DONE;
      return PM_METAL_DONE;
    }
    job->step = PY_STEP_RESUME;
    goto py_resume;
  case PY_STEP_CALL:
    /* py_call_bound() unlocks internally on every path — same as above. */
    if (py_run_try_lock() != 0) {
      return pm_metal_async_await(self_h, pm_metal_async_yield());
    }
    if (py_call_bound(job) != 0) {
      job->step = PY_STEP_DONE;
      return PM_METAL_ERROR;
    }
    if (job->py_coro == MP_OBJ_NULL) {
      job->step = PY_STEP_DONE;
      return PM_METAL_DONE;
    }
    job->step = PY_STEP_RESUME;
    /* fallthrough */
  case PY_STEP_RESUME:
  py_resume:
    /*
     * py_resume_coro() itself unlocks right after mp_resume (before
     * reporting WAITING) so a peer Python task can run while we sleep —
     * only the entry needs gating here too.
     */
    if (py_run_try_lock() != 0) {
      return pm_metal_async_await(self_h, pm_metal_async_yield());
    }
    {
      pm_metal_status_t st = py_resume_coro(job);
      if (st == PM_METAL_WAITING && job->pending != 0) {
        return pm_metal_async_await(self_h, job->pending);
      }
      job->step = PY_STEP_DONE;
      return st;
    }
  default:
    return PM_METAL_DONE;
  }
}

static pm_metal_async_handle_t py_spawn(char *src, size_t len, uint32_t step)
{
  pm_metal_py_job_t      *job;
  pm_metal_async_handle_t ch;
  pm_metal_async_handle_t th;

  if (pm_metal_py_init() != 0) {
    if (src != NULL) {
      pm_metal_mem_free(src);
    }
    return 0;
  }
  (void)pm_metal_py_zip_ensure();

  ch = pm_metal_async_coro_create(py_job_step, sizeof(*job));
  if (ch == PM_METAL_ASYNC_HANDLE_INVALID) {
    if (src != NULL) {
      pm_metal_mem_free(src);
    }
    return 0;
  }
  job = (pm_metal_py_job_t *)(uintptr_t)pm_metal_async_coro_state(ch);
  if (job == NULL) {
    pm_metal_async_coro_close(ch);
    if (src != NULL) {
      pm_metal_mem_free(src);
    }
    return 0;
  }
  job->step          = step;
  job->src           = src;
  job->src_len       = len;
  job->py_coro       = MP_OBJ_NULL;
  job->call_fn       = MP_OBJ_NULL;
  job->call_arg0     = 0;
  job->pending       = 0;
  job->pending_aw    = MP_OBJ_NULL;
  job->exe_exception = 0;
  pm_metal_async_coro_set_release(ch, py_job_release);

  th = pm_metal_async_create_task(ch);
  if (th == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_async_coro_close(ch);
    return 0;
  }
  return th;
}

pm_metal_async_handle_t pm_metal_py_run_str(const char *src)
{
  size_t n;
  char  *copy;

  if (src == NULL) {
    return 0;
  }
  n    = strlen(src);
  copy = (char *)pm_metal_mem_alloc(n + 1u, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (copy == NULL) {
    return 0;
  }
  memcpy(copy, src, n + 1u);
  return py_spawn(copy, n, PY_STEP_EXEC);
}

pm_metal_async_handle_t pm_metal_py_run_script(const char *path)
{
  size_t n;
  char  *copy;

  if (path == NULL) {
    return 0;
  }
  n    = strlen(path);
  copy = (char *)pm_metal_mem_alloc(n + 1u, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (copy == NULL) {
    return 0;
  }
  memcpy(copy, path, n + 1u);
  return py_spawn(copy, n, PY_STEP_LOAD);
}

/* --- natural C → Py (sync add / async blink-style) --- */

int pm_metal_py_lookup(const char *dotted, pm_metal_py_ref_t *out)
{
  nlr_buf_t nlr;
  mp_obj_t  obj;

  if (out == NULL || dotted == NULL) {
    return -1;
  }
  out->obj = NULL;
  if (!g_ready && pm_metal_py_init() != 0) {
    return -1;
  }
  if (nlr_push(&nlr) == 0) {
    const char *dot = strchr(dotted, '.');
    if (dot == NULL) {
      obj = mp_load_name(qstr_from_str(dotted));
    } else {
      char           mod[64];
      size_t         ml = (size_t)(dot - dotted);
      mp_map_elem_t *el;
      const char    *seg;

      if (ml >= sizeof(mod)) {
        nlr_pop();
        return -1;
      }
      memcpy(mod, dotted, ml);
      mod[ml] = '\0';
      el      = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map,
                         MP_OBJ_NEW_QSTR(qstr_from_str(mod)),
                         MP_MAP_LOOKUP);
      if (el == NULL) {
        nlr_pop();
        return -1;
      }
      obj = el->value;

      /*
       * Walk each remaining dot-separated segment with its own
       * mp_load_attr hop instead of treating everything after the first
       * dot as one literal attribute name. The old single-hop version was
       * correct by accident for exactly 2 segments ("metal.aio") but
       * always failed for 3+ ("pymergetic.metal.aio.sleep_us" tried to
       * load the literal attribute "metal.aio.sleep_us", which never
       * exists).
       */
      seg = dot + 1;
      for (;;) {
        const char *next_dot = strchr(seg, '.');
        char        attr[64];
        size_t      al = (next_dot != NULL) ? (size_t)(next_dot - seg) : strlen(seg);

        if (al == 0 || al >= sizeof(attr)) {
          nlr_pop();
          return -1;
        }
        memcpy(attr, seg, al);
        attr[al] = '\0';
        obj      = mp_load_attr(obj, qstr_from_str(attr));

        if (next_dot == NULL) {
          break;
        }
        seg = next_dot + 1;
      }
    }
    out->obj = (void *)obj;
    nlr_pop();
    return 0;
  }
  return -1;
}

int pm_metal_py_fn_bind(pm_metal_py_fn_t *fn, const char *dotted_name)
{
  if (fn == NULL || dotted_name == NULL) {
    return -1;
  }
  memset(fn, 0, sizeof(*fn));
  strncpy(fn->name, dotted_name, sizeof(fn->name) - 1u);
  fn->class_ = (uint8_t)PM_METAL_PY_ASYNC;
  return pm_metal_py_lookup(dotted_name, &fn->ref);
}

/*
 * Max non-blocking try_lock attempts before pm_metal_py_call() fails
 * closed instead of spinning forever — this runs outside any async step
 * (direct host C / shell call), so it cannot pm_metal_async_await() like
 * py_job_step() does on contention; a bounded spin + explicit "busy"
 * failure is the "no fake sync" answer instead.
 */
#define PY_CALL_LOCK_MAX_TRIES 10000000u

int pm_metal_py_call(pm_metal_py_fn_t *fn, int32_t *out_i32, int32_t a, int32_t b)
{
  nlr_buf_t nlr;
  mp_obj_t  ret;
  mp_obj_t  args[2];
  int       rc;
  uint32_t  tries;

  if (fn == NULL || fn->ref.obj == NULL) {
    return -1;
  }

  tries = 0;
  while (py_run_try_lock() != 0) {
    pm_metal_cpu_pause();
    if (++tries > PY_CALL_LOCK_MAX_TRIES) {
      pm_metal_log("py: call busy");
      return -1;
    }
  }

  args[0] = mp_obj_new_int(a);
  args[1] = mp_obj_new_int(b);
  if (nlr_push(&nlr) == 0) {
    ret = mp_call_function_n_kw((mp_obj_t)fn->ref.obj, 2, 0, args);
    /*
     * Calling an async-def target just returns an inert generator without
     * running its body — real Python semantics, not a park. A sync
     * trampoline can't drive that (would need py_resume_coro's machinery),
     * so fail explicitly instead of relying on mp_obj_get_int()'s
     * incidental TypeError on a generator object.
     */
    if (mp_obj_is_type(ret, &mp_type_gen_instance)) {
      pm_metal_log("py: sync call cannot park");
      rc = -1;
    } else {
      if (out_i32 != NULL) {
        *out_i32 = (int32_t)mp_obj_get_int(ret);
      }
      rc = 0;
    }
    nlr_pop();
  } else {
    rc = -1;
  }
  py_run_unlock();
  return rc;
}

pm_metal_async_handle_t pm_metal_py_fn_call_async_bound(pm_metal_py_fn_t *fn, uint32_t arg0)
{
  pm_metal_py_job_t      *job;
  pm_metal_async_handle_t ch;
  pm_metal_async_handle_t th;

  if (fn == NULL || fn->ref.obj == NULL) {
    return 0;
  }
  if (pm_metal_py_init() != 0) {
    return 0;
  }

  ch = pm_metal_async_coro_create(py_job_step, sizeof(*job));
  if (ch == PM_METAL_ASYNC_HANDLE_INVALID) {
    return 0;
  }
  job = (pm_metal_py_job_t *)(uintptr_t)pm_metal_async_coro_state(ch);
  if (job == NULL) {
    pm_metal_async_coro_close(ch);
    return 0;
  }
  job->step          = PY_STEP_CALL;
  job->src           = NULL;
  job->src_len       = 0;
  job->py_coro       = MP_OBJ_NULL;
  job->call_fn       = (mp_obj_t)fn->ref.obj;
  job->call_arg0     = arg0;
  job->pending       = 0;
  job->pending_aw    = MP_OBJ_NULL;
  job->exe_exception = 0;
  pm_metal_async_coro_set_release(ch, py_job_release);

  th = pm_metal_async_create_task(ch);
  if (th == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_async_coro_close(ch);
    return 0;
  }
  return th;
}

/*
 * Handle table for guest-visible pm_metal_py_fn_resolve/_call/_call_async
 * (py.h) — mirrors mod.c's ModFnHandleAlloc/Get exactly: fixed slots,
 * index 1..N, a slot is free when its ref.obj field is NULL.
 */
static pm_metal_py_fn_t mPyFnHandles[PM_METAL_PY_FN_H_MAX + 1];

static pm_metal_py_fn_h_t PyFnHandleAlloc(const pm_metal_py_fn_t *fn)
{
  uint32_t i;

  for (i = 1; i <= PM_METAL_PY_FN_H_MAX; i++) {
    if (mPyFnHandles[i].ref.obj == NULL) {
      mPyFnHandles[i] = *fn;
      return (pm_metal_py_fn_h_t)i;
    }
  }

  return PM_METAL_PY_FN_H_INVALID;
}

static pm_metal_py_fn_t *PyFnHandleGet(pm_metal_py_fn_h_t h)
{
  if (h == PM_METAL_PY_FN_H_INVALID || h > PM_METAL_PY_FN_H_MAX) {
    return NULL;
  }
  if (mPyFnHandles[h].ref.obj == NULL) {
    return NULL;
  }
  return &mPyFnHandles[h];
}

pm_metal_py_fn_h_t pm_metal_py_fn_resolve(const char *dotted_name)
{
  pm_metal_py_fn_t fn;

  if (pm_metal_py_fn_bind(&fn, dotted_name) != 0) {
    return PM_METAL_PY_FN_H_INVALID;
  }
  return PyFnHandleAlloc(&fn);
}

int pm_metal_py_fn_call(pm_metal_py_fn_h_t fn_h, int32_t *out_i32, int32_t a, int32_t b)
{
  pm_metal_py_fn_t *fn = PyFnHandleGet(fn_h);

  if (fn == NULL) {
    return -1;
  }
  return pm_metal_py_call(fn, out_i32, a, b);
}

pm_metal_async_handle_t pm_metal_py_fn_call_async(pm_metal_py_fn_h_t fn_h, uint32_t arg0)
{
  pm_metal_py_fn_t *fn = PyFnHandleGet(fn_h);

  if (fn == NULL) {
    return 0;
  }
  return pm_metal_py_fn_call_async_bound(fn, arg0);
}
