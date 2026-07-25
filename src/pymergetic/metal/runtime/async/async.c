/** @file
  Async handle table + guest/host fiber trampolines.
**/
#include "async_internal.h"

#include <stdint.h>

#include "async_host.h"
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/dev/input/input.h>
#include <pymergetic/metal/boot/port.h>
#include <pymergetic/metal/guest/wasm/wasm.h>
#include <pymergetic/metal/guest/mod/mod.h>
#include <pymergetic/metal/runtime/mem/mem.h>
#include <runtime/time/time.h>
#include <runtime/slot/slot_table.h>

#include <assert.h>
#include <stddef.h>
#include <string.h>

/*
 * mSlots[] is a global handle table (host fibers, host/guest coros) that
 * any task on any CPU may allocate/clear/look up — unlike the wasm call-in
 * (mCallinBusy) or the present job (mPresentBusy), which each guard one
 * specific *resource*, this used to guard the *table itself* with one
 * SPIN_LOCK. That serializes every unrelated handle's Get/Clear behind a
 * single lock even though they don't touch the same memory — the same
 * "central bottleneck" the ready-ring in run.c avoids. So each slot is
 * instead one lock-free tagged word, same trick as the ready ring:
 *
 *   bits 0..47   ptr (0 = FREE — MetalAsyncAlloc never stores a NULL ptr,
 *                so a non-free slot's word is never 0)
 *   bits 48..50  kind (pm_metal_async_slot_kind_t — 3 bits, 5 values)
 *
 * MetalAsyncGet becomes a single atomic load (no lock at all — it's the
 * hottest call here, every host-fiber/guest-coro poll goes through it).
 * MetalAsyncAlloc CASes a FREE (0) slot to (kind|ptr); MetalAsyncClear is
 * a plain aligned 64-bit store back to 0 (atomic on x86-64 for free, so a
 * concurrent Get can't observe a torn kind/ptr pair — it sees the whole
 * old word or the whole new one). Assumes canonical low addresses (top
 * 16 bits zero), same assumption already established for the ready ring.
 */
#define PM_METAL_SLOT_PTR_MASK   0x0000FFFFFFFFFFFFull
#define PM_METAL_SLOT_KIND_SHIFT 48u
#define PM_METAL_SLOT_KIND_MASK  0x7ull

static volatile uint64_t mSlotWords[PM_METAL_ASYNC_MAX_HANDLES + 1];

static uint64_t MetalSlotPack(pm_metal_async_slot_kind_t kind, void *ptr)
{
  uint64_t p;

  p = (uint64_t)(uintptr_t)ptr;
  assert((p & ~PM_METAL_SLOT_PTR_MASK) == 0);
  return (p & PM_METAL_SLOT_PTR_MASK) |
         (((uint64_t)kind & PM_METAL_SLOT_KIND_MASK) << PM_METAL_SLOT_KIND_SHIFT);
}

static pm_metal_async_slot_kind_t MetalSlotKind(uint64_t word)
{
  return (pm_metal_async_slot_kind_t)((word >> PM_METAL_SLOT_KIND_SHIFT) & PM_METAL_SLOT_KIND_MASK);
}

static void *MetalSlotPtr(uint64_t word)
{
  return (void *)(uintptr_t)(word & PM_METAL_SLOT_PTR_MASK);
}

/* Set while MetalGuestCoroFn runs — child create(NULL) inherits this. */
static pm_metal_async_callin_t *mRunningCallin;

/*
 * The one genuinely shared, must-not-race resource in the guest call-in
 * path: wasm_runtime_call_wasm() itself, plus mRunningCallin/wasm_bind_inst
 * and MetalGuestCoroPinState/UnpinState's module-malloc calls — all of
 * which touch the single shared exec_env/module instance. Guest tasks no
 * longer share a CPU (no more session pinning), so two call-ins can now
 * genuinely race across runners; this narrow mutex is what actually keeps
 * them safe. Unlike the gfx present job, a call-in cannot be dropped on
 * contention — the coro must requeue itself (yield) and retry.
 */
static volatile uint32_t mCallinBusy;

static int32_t MetalCallinTryAcquire(void)
{
  return (pm_metal_slot_port_cas32(&mCallinBusy, 0, 1) == 0) ? 1 : 0;
}

static void MetalCallinRelease(void)
{
  (void)pm_metal_slot_port_cas32(&mCallinBusy, 1, 0);
}

void *pm_metal_async_guest_buf_durable(void *exec_env, uint32_t guest_off, uint32_t len)
{
  pm_metal_guest_coro_t *g;
  wasm_module_inst_t     inst;
  void                  *host;
  void                  *native;
  uint32_t               base;
  uint32_t               end;

  if (guest_off == 0 || len == 0) {
    return NULL;
  }

  host = NULL;
  if (mRunningCallin != NULL) {
    g = (pm_metal_guest_coro_t *)((uint8_t *)mRunningCallin -
                                  offsetof(pm_metal_guest_coro_t, callin));
    if (g->host_state != NULL && g->guest_state != 0 && g->state_bytes != 0) {
      base = g->guest_state;
      end  = base + g->state_bytes;
      if (guest_off >= base && len <= (end - guest_off)) {
        host = (uint8_t *)g->host_state + (guest_off - base);
      }
    }
  }

  inst = NULL;
  if (exec_env != NULL) {
    inst = wasm_runtime_get_module_inst((wasm_exec_env_t)exec_env);
  } else if (mRunningCallin != NULL) {
    inst = mRunningCallin->inst;
  }

  if (inst == NULL) {
    return host;
  }

  if (!wasm_runtime_validate_app_addr(inst, (uint64_t)guest_off, len)) {
    return NULL;
  }

  native = wasm_runtime_addr_app_to_native(inst, (uint64_t)guest_off);
  if (native == NULL) {
    return NULL;
  }

  if (host != NULL) {
    memcpy(host, native, len);
    return host;
  }

  return native;
}

pm_metal_async_handle_t MetalAsyncAlloc(pm_metal_async_slot_kind_t kind, void *ptr)
{
  uint32_t i;
  uint64_t word;

  if (ptr == NULL || kind == PM_METAL_ASYNC_SLOT_FREE) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  word = MetalSlotPack(kind, ptr);
  for (i = 1; i <= PM_METAL_ASYNC_MAX_HANDLES; i++) {
    if (pm_metal_slot_port_cas64(&mSlotWords[i], 0, word) == 0) {
      return (pm_metal_async_handle_t)i;
    }
  }

  return PM_METAL_ASYNC_HANDLE_INVALID;
}

void MetalAsyncClear(pm_metal_async_handle_t h)
{
  if (h == PM_METAL_ASYNC_HANDLE_INVALID || h > PM_METAL_ASYNC_MAX_HANDLES) {
    return;
  }

  mSlotWords[h] = 0; /* aligned 64-bit store — atomic on x86-64, no CAS needed */
}

void *MetalAsyncGet(pm_metal_async_handle_t h, pm_metal_async_slot_kind_t kind)
{
  uint64_t word;

  if (h == PM_METAL_ASYNC_HANDLE_INVALID || h > PM_METAL_ASYNC_MAX_HANDLES) {
    return NULL;
  }

  word = mSlotWords[h];
  return (MetalSlotKind(word) == kind) ? MetalSlotPtr(word) : NULL;
}

pm_metal_async_slot_t MetalAsyncSlotPeek(uint32_t i)
{
  pm_metal_async_slot_t s;
  uint64_t              word;

  if (i == 0 || i > PM_METAL_ASYNC_MAX_HANDLES) {
    s.kind = PM_METAL_ASYNC_SLOT_FREE;
    s.ptr  = NULL;
    return s;
  }

  word   = mSlotWords[i];
  s.kind = MetalSlotKind(word);
  s.ptr  = MetalSlotPtr(word);
  return s;
}

pm_metal_coro_t *MetalAsyncGetCoro(pm_metal_async_handle_t h)
{
  void *p;

  p = MetalAsyncGet(h, PM_METAL_ASYNC_SLOT_GUEST_CORO);
  if (p != NULL) {
    return &((pm_metal_guest_coro_t *)p)->coro;
  }

  p = MetalAsyncGet(h, PM_METAL_ASYNC_SLOT_HOST_FIBER);
  if (p != NULL) {
    return &((pm_metal_host_fiber_t *)p)->coro;
  }

  return (pm_metal_coro_t *)MetalAsyncGet(h, PM_METAL_ASYNC_SLOT_HOST_CORO);
}

/**
 * Durable frame is Metal TLSF (host_state). Guest C needs a linear T* only
 * during the step — pin a short-lived alias, sync out on unpin.
 */
static int32_t MetalGuestCoroPinState(pm_metal_guest_coro_t *g)
{
  void    *native;
  uint64_t off;

  if (g == NULL || g->host_state == NULL || g->state_bytes == 0 || g->callin.inst == NULL) {
    return 0;
  }

  if (g->guest_state != 0) {
    return 0;
  }

  native = NULL;
  off    = wasm_runtime_module_malloc(g->callin.inst, g->state_bytes, &native);
  if (off == 0 || native == NULL) {
    return -1;
  }

  memcpy(native, g->host_state, g->state_bytes);
  g->guest_state = (uint32_t)off;
  return 0;
}

static void MetalGuestCoroUnpinState(pm_metal_guest_coro_t *g)
{
  void *native;

  if (g == NULL || g->guest_state == 0 || g->callin.inst == NULL) {
    return;
  }

  if (g->host_state != NULL && g->state_bytes > 0) {
    native = wasm_runtime_addr_app_to_native(g->callin.inst, (uint64_t)g->guest_state);
    if (native != NULL) {
      memcpy(g->host_state, native, g->state_bytes);
    }
  }

  wasm_runtime_module_free(g->callin.inst, (uint64_t)g->guest_state);
  g->guest_state = 0;
}

static pm_metal_status_t MetalGuestCoroFn(pm_metal_coro_t *self)
{
  pm_metal_guest_coro_t   *g;
  pm_metal_async_callin_t *c;
  pm_metal_async_callin_t *prev;
  uint32_t                 argv[1];
  uint64_t                 t0;
  uint64_t                 t1;
  pm_metal_status_t        st;

  g = (pm_metal_guest_coro_t *)self;
  c = &g->callin;
  if (c->exec_env == NULL || c->step_fn == NULL || c->inst == NULL) {
    return PM_METAL_ERROR;
  }

  /*
   * Requeue-and-retry rather than block: a second guest coro stepped on
   * another CPU right now must not spin here holding its runner hostage.
   */
  if (!MetalCallinTryAcquire()) {
    return pm_metal_await(self, pm_metal_yield());
  }

  if (MetalGuestCoroPinState(g) != 0) {
    MetalCallinRelease();
    return PM_METAL_ERROR;
  }

  prev           = mRunningCallin;
  mRunningCallin = c;
  pm_metal_wasm_bind_inst(c->inst);

  t0 = pm_metal_time_mono_us();
  if (mPerfLastStepEndUs != 0) {
    uint64_t gap;

    gap = t0 - mPerfLastStepEndUs;
    mPerfGapUsSum += gap;
    if (gap > mPerfGapUsMax) {
      mPerfGapUsMax = gap;
    }
  }

  argv[0] = g->self_h;
  if (!wasm_runtime_call_wasm(c->exec_env, c->step_fn, 1, argv)) {
    const char *exc;
    uint32_t    code;

    exc  = wasm_runtime_get_exception(c->inst);
    code = wasm_runtime_get_wasi_exit_code(c->inst);
    /*
     * Only wasi proc_exit is a clean finish. Traps also leave exit_code=0
     * — do not treat those as DONE (that hid guest Create failures).
     */
    if (exc != NULL && strstr(exc, "wasi proc exit") != NULL) {
      st = (code == 0) ? PM_METAL_DONE : PM_METAL_ERROR;
      MetalGuestCoroUnpinState(g);
      mRunningCallin = prev;
      MetalCallinRelease();
      return st;
    }

    pm_metal_logf(
      "metal-async: call-in step failed: %s (wasi_exit=%u)", exc != NULL ? exc : "?", code);
    MetalGuestCoroUnpinState(g);
    mRunningCallin = prev;
    MetalCallinRelease();
    return PM_METAL_ERROR;
  }

  t1 = pm_metal_time_mono_us();
  {
    uint64_t step_us;

    step_us = t1 - t0;
    mPerfStepUsSum += step_us;
    if (step_us > mPerfStepUsMax) {
      mPerfStepUsMax = step_us;
    }
  }
  mPerfLastStepEndUs = t1;
  mPerfSteps++;
  MetalAsyncPerfMaybeReport(t1);

  st = (pm_metal_status_t)argv[0];
  MetalGuestCoroUnpinState(g);
  mRunningCallin = prev;
  MetalCallinRelease();
  return st;
}

static void MetalGuestCoroRelease(pm_metal_coro_t *self)
{
  pm_metal_guest_coro_t *g;

  g = (pm_metal_guest_coro_t *)self;
  MetalGuestCoroUnpinState(g);
  if (g->host_state != NULL) {
    pm_metal_mem_free(g->host_state);
    g->host_state  = NULL;
    g->state_bytes = 0;
  }

  if (g->callin.inst != NULL) {
    pm_metal_mod_on_guest_coro_end(g->callin.inst);
  }

  memset(&g->callin, 0, sizeof(g->callin));
  if (g->self_h != PM_METAL_ASYNC_HANDLE_INVALID) {
    MetalAsyncClear(g->self_h);
    g->self_h = PM_METAL_ASYNC_HANDLE_INVALID;
  }
}

static pm_metal_status_t MetalHostFiberFn(pm_metal_coro_t *self)
{
  pm_metal_host_fiber_t *f;

  f = (pm_metal_host_fiber_t *)self;
  if (f->step == NULL) {
    return PM_METAL_ERROR;
  }

  return f->step(f->self_h);
}

static void MetalHostFiberRelease(pm_metal_coro_t *self)
{
  pm_metal_host_fiber_t *f;

  f = (pm_metal_host_fiber_t *)self;
  if (f->state != NULL) {
    if (f->state_release != NULL) {
      f->state_release(f->state);
      f->state_release = NULL;
    }

    pm_metal_mem_free(f->state);
    f->state = NULL;
  }

  if (f->self_h != PM_METAL_ASYNC_HANDLE_INVALID) {
    MetalAsyncClear(f->self_h);
    f->self_h = PM_METAL_ASYNC_HANDLE_INVALID;
  }
}

static pm_metal_async_handle_t MetalAsyncCoroCreateGuest(const pm_metal_async_callin_t *callin,
                                                         uint32_t                       state_bytes)
{
  pm_metal_guest_coro_t  *g;
  pm_metal_async_handle_t h;

  if (callin == NULL || callin->inst == NULL || callin->exec_env == NULL ||
      callin->step_fn == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  g = (pm_metal_guest_coro_t *)pm_metal_coro(MetalGuestCoroFn, sizeof(*g));
  if (g == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  g->coro.release = MetalGuestCoroRelease;
  g->self_h       = PM_METAL_ASYNC_HANDLE_INVALID;
  g->host_state   = NULL;
  g->guest_state  = 0;
  g->state_bytes  = 0;
  g->callin       = *callin; /* stamp by value */
  pm_metal_mod_on_guest_coro_begin(callin->inst);

  if (state_bytes > 0) {
    g->host_state = pm_metal_mem_alloc(state_bytes, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
    if (g->host_state == NULL) {
      pm_metal_coro_close(&g->coro);
      return PM_METAL_ASYNC_HANDLE_INVALID;
    }

    memset(g->host_state, 0, state_bytes);
    g->state_bytes = state_bytes;
  }

  h = MetalAsyncAlloc(PM_METAL_ASYNC_SLOT_GUEST_CORO, g);
  if (h == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_coro_close(&g->coro);
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  g->self_h = h;
  return h;
}

static pm_metal_async_handle_t MetalAsyncCoroCreateHost(pm_metal_async_step_fn_t step,
                                                        uint32_t                 state_bytes)
{
  pm_metal_host_fiber_t  *f;
  pm_metal_async_handle_t h;

  if (step == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  f = (pm_metal_host_fiber_t *)pm_metal_coro(MetalHostFiberFn, sizeof(*f));
  if (f == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  f->coro.release  = MetalHostFiberRelease;
  f->step          = step;
  f->state_release = NULL;
  f->state         = NULL;
  f->self_h        = PM_METAL_ASYNC_HANDLE_INVALID;
  f->state_bytes   = state_bytes;

  if (state_bytes > 0) {
    f->state = pm_metal_mem_alloc(state_bytes, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
    if (f->state == NULL) {
      pm_metal_coro_close(&f->coro);
      return PM_METAL_ASYNC_HANDLE_INVALID;
    }

    memset(f->state, 0, state_bytes);
  }

  h = MetalAsyncAlloc(PM_METAL_ASYNC_SLOT_HOST_FIBER, f);
  if (h == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_coro_close(&f->coro);
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  f->self_h = h;
  return h;
}

pm_metal_async_handle_t pm_metal_async_coro_create(pm_metal_async_step_fn_t step,
                                                   uint32_t                 state_bytes)
{
  if (step != NULL) {
    return MetalAsyncCoroCreateHost(step, state_bytes);
  }

  /* NULL step: inherit running guest, else process-session call-in. */
  if (mRunningCallin != NULL) {
    return MetalAsyncCoroCreateGuest(mRunningCallin, state_bytes);
  }

  if (mActive && mCallin.inst != NULL) {
    return MetalAsyncCoroCreateGuest(&mCallin, state_bytes);
  }

  return PM_METAL_ASYNC_HANDLE_INVALID;
}

pm_metal_async_handle_t pm_metal_async_coro_create_guest(void    *module_inst,
                                                         void    *exec_env,
                                                         void    *step_fn,
                                                         uint32_t state_bytes)
{
  pm_metal_async_callin_t c;

  c.inst     = (wasm_module_inst_t)module_inst;
  c.exec_env = (wasm_exec_env_t)exec_env;
  c.step_fn  = (wasm_function_inst_t)step_fn;
  return MetalAsyncCoroCreateGuest(&c, state_bytes);
}

pm_metal_ptr_t pm_metal_async_coro_state(pm_metal_async_handle_t h)
{
  pm_metal_guest_coro_t *g;
  pm_metal_host_fiber_t *f;

  g = (pm_metal_guest_coro_t *)MetalAsyncGet(h, PM_METAL_ASYNC_SLOT_GUEST_CORO);
  if (g != NULL) {
    /* Guest: linear alias (valid only while step is pinned). */
    return (void *)(uintptr_t)g->guest_state;
  }

  f = (pm_metal_host_fiber_t *)MetalAsyncGet(h, PM_METAL_ASYNC_SLOT_HOST_FIBER);
  if (f != NULL) {
    return f->state;
  }

  return NULL;
}

pm_metal_ptr_t pm_metal_async_coro_alloc(pm_metal_async_handle_t h, uint32_t n)
{
  pm_metal_guest_coro_t *g;
  pm_metal_host_fiber_t *f;

  if (n == 0) {
    return NULL;
  }

  g = (pm_metal_guest_coro_t *)MetalAsyncGet(h, PM_METAL_ASYNC_SLOT_GUEST_CORO);
  if (g != NULL) {
    if (g->host_state != NULL) {
      if (g->guest_state == 0 && MetalGuestCoroPinState(g) != 0) {
        return NULL;
      }

      return (void *)(uintptr_t)g->guest_state;
    }

    g->host_state = pm_metal_mem_alloc(n, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
    if (g->host_state == NULL) {
      return NULL;
    }

    memset(g->host_state, 0, n);
    g->state_bytes = n;
    if (MetalGuestCoroPinState(g) != 0) {
      pm_metal_mem_free(g->host_state);
      g->host_state  = NULL;
      g->state_bytes = 0;
      return NULL;
    }

    return (void *)(uintptr_t)g->guest_state;
  }

  f = (pm_metal_host_fiber_t *)MetalAsyncGet(h, PM_METAL_ASYNC_SLOT_HOST_FIBER);
  if (f != NULL) {
    if (f->state != NULL) {
      return f->state;
    }

    f->state = pm_metal_mem_alloc(n, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
    if (f->state == NULL) {
      return NULL;
    }

    memset(f->state, 0, n);
    f->state_bytes = n;
    return f->state;
  }

  return NULL;
}

void pm_metal_async_coro_close(pm_metal_async_handle_t h)
{
  pm_metal_guest_coro_t *g;
  pm_metal_host_fiber_t *f;
  pm_metal_coro_t       *c;

  g = (pm_metal_guest_coro_t *)MetalAsyncGet(h, PM_METAL_ASYNC_SLOT_GUEST_CORO);
  if (g != NULL) {
    /* Owned by a task — destroy via task path. */
    if (g->coro.owner != NULL) {
      return;
    }

    pm_metal_coro_close(&g->coro);
    return;
  }

  f = (pm_metal_host_fiber_t *)MetalAsyncGet(h, PM_METAL_ASYNC_SLOT_HOST_FIBER);
  if (f != NULL) {
    if (f->coro.owner != NULL) {
      return;
    }

    pm_metal_coro_close(&f->coro);
    return;
  }

  c = (pm_metal_coro_t *)MetalAsyncGet(h, PM_METAL_ASYNC_SLOT_HOST_CORO);
  if (c != NULL) {
    MetalAsyncClear(h);
    pm_metal_coro_close(c);
  }
}

static pm_metal_coro_release_fn mHostCoroOrigRelease[PM_METAL_ASYNC_MAX_HANDLES + 1];

static void MetalHostCoroRelease(pm_metal_coro_t *self)
{
  uint32_t                 i;
  pm_metal_coro_release_fn orig;

  orig = NULL;
  for (i = 1; i <= PM_METAL_ASYNC_MAX_HANDLES; i++) {
    uint64_t word;

    /*
     * Find-and-clear as one CAS (not a peek followed by a separate
     * MetalAsyncClear) so a concurrent MetalAsyncAlloc can't reuse this
     * exact slot between the match and the clear. Retry on this same
     * index if the CAS loses the word we just observed; move on once it
     * no longer matches (already cleared by someone else).
     */
    for (;;) {
      word = mSlotWords[i];
      if (MetalSlotKind(word) != PM_METAL_ASYNC_SLOT_HOST_CORO || MetalSlotPtr(word) != self) {
        break;
      }

      if (pm_metal_slot_port_cas64(&mSlotWords[i], word, 0) == word) {
        orig                    = mHostCoroOrigRelease[i];
        mHostCoroOrigRelease[i] = NULL;
        goto found;
      }
    }
  }

found:
  if (orig != NULL) {
    orig(self);
  }
}

pm_metal_async_handle_t pm_metal_async_adopt_host_coro(pm_metal_coro_t *c)
{
  pm_metal_async_handle_t h;

  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  h = MetalAsyncAlloc(PM_METAL_ASYNC_SLOT_HOST_CORO, c);
  if (h == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_coro_close(c);
    return h;
  }

  mHostCoroOrigRelease[h] = c->release;
  c->release              = MetalHostCoroRelease;
  return h;
}

pm_metal_coro_t *pm_metal_async_host_coro(pm_metal_async_handle_t h)
{
  return MetalAsyncGetCoro(h);
}

pm_metal_async_handle_t pm_metal_async_handle_of(pm_metal_coro_t *c)
{
  uint32_t i;
  void    *p;

  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  for (i = 1; i <= PM_METAL_ASYNC_MAX_HANDLES; i++) {
    uint64_t                   word;
    pm_metal_async_slot_kind_t kind;

    word = mSlotWords[i];
    kind = MetalSlotKind(word);

    if (kind == PM_METAL_ASYNC_SLOT_HOST_CORO && MetalSlotPtr(word) == c) {
      return (pm_metal_async_handle_t)i;
    }

    if (kind == PM_METAL_ASYNC_SLOT_HOST_FIBER) {
      p = MetalSlotPtr(word);
      if (p != NULL && &((pm_metal_host_fiber_t *)p)->coro == c) {
        return (pm_metal_async_handle_t)i;
      }
    }

    if (kind == PM_METAL_ASYNC_SLOT_GUEST_CORO) {
      p = MetalSlotPtr(word);
      if (p != NULL && &((pm_metal_guest_coro_t *)p)->coro == c) {
        return (pm_metal_async_handle_t)i;
      }
    }
  }

  return PM_METAL_ASYNC_HANDLE_INVALID;
}
