/** @file
  Fake process table — host anchor for live wasm guests. (impl: efi|bios)
**/
#include <pymergetic/metal/guest/process/process.h>
#include <pymergetic/metal/guest/mod/mod.h>
#include <pymergetic/metal/guest/wasm/wasm.h>
#include <pymergetic/metal/shell/ui/tab.h>
#include <pymergetic/metal/shell/ui/input.h>
#include <pymergetic/metal/dev/gfx/gfx.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/log/log.h>
#include <runtime/run/run.h>
#include <runtime/slot/slot_table.h>

#include <stdint.h>
#include <string.h>

#include "wasm_export.h"

typedef struct {
  volatile uint32_t          used; /* slot ticket - see slot_table.h; must stay first */
  pm_metal_process_id_t      id;
  pm_metal_process_id_t      parent_id;
  pm_metal_async_handle_t    root_task_h;
  char                       name[64];
  uint32_t                   state;
  pm_metal_process_ui_kind_t ui_kind;
  pm_metal_ui_handle_t       tab;
  uint32_t                   surface;
  pm_metal_ui_handle_t       saved_stdout_tab;
} MetalProcessSlot;

static MetalProcessSlot      mSlots[PM_METAL_PROCESS_MAX];
static pm_metal_process_id_t mNextId    = 1u;
static pm_metal_process_id_t mCurrent   = PM_METAL_PROCESS_ID_INVALID;
static pm_metal_process_id_t mPending   = PM_METAL_PROCESS_ID_INVALID;
static pm_metal_process_id_t mStampProc = PM_METAL_PROCESS_ID_INVALID;
static wasm_module_inst_t    mProcInst;

static pm_metal_process_ui_kind_t mHintKind = PM_METAL_PROC_UI_NONE;
static pm_metal_ui_handle_t       mHintTab  = PM_METAL_UI_HANDLE_INVALID;
static int32_t                    mHintSet;

static void strlcpy_trunc(char *dst, size_t dst_sz, const char *src)
{
  if (dst == NULL || dst_sz == 0) {
    return;
  }

  if (src == NULL) {
    dst[0] = '\0';
    return;
  }

  strncpy(dst, src, dst_sz - 1);
  dst[dst_sz - 1] = '\0';
}

void pm_metal_process_bind_inst(void *module_inst)
{
  mProcInst = (wasm_module_inst_t)module_inst;
}

static MetalProcessSlot *MetalProcessFind(pm_metal_process_id_t id)
{
  uint32_t i;

  if (id == PM_METAL_PROCESS_ID_INVALID) {
    return NULL;
  }

  for (i = 0; i < PM_METAL_PROCESS_MAX; i++) {
    if (mSlots[i].used && mSlots[i].id == id) {
      return &mSlots[i];
    }
  }

  return NULL;
}

static MetalProcessSlot *MetalProcessAllocSlot(void)
{
  uint32_t i;

  /*
	 * Tasks/fibers can run on any CPU now (no session pinning), so two
	 * CPUs spawning a process at once must not be able to win the same
	 * free index - claim the slot ticket with a CAS before touching it.
	 */
  for (i = 0; i < PM_METAL_PROCESS_MAX; i++) {
    if (pm_metal_slot_try_claim(&mSlots[i].used, 1)) {
      pm_metal_slot_claimed_zero(&mSlots[i].used, sizeof(mSlots[i]));
      return &mSlots[i];
    }
  }

  return NULL;
}

void pm_metal_process_set_spawn_hint(pm_metal_process_ui_kind_t ui_kind, pm_metal_ui_handle_t tab)
{
  mHintKind = ui_kind;
  mHintTab  = tab;
  mHintSet  = 1;
}

void pm_metal_process_clear_spawn_hint(void)
{
  mHintKind = PM_METAL_PROC_UI_NONE;
  mHintTab  = PM_METAL_UI_HANDLE_INVALID;
  mHintSet  = 0;
}

int pm_metal_process_spawn_hint(pm_metal_process_ui_kind_t *ui_kind_out,
                                pm_metal_ui_handle_t       *tab_out)
{
  if (!mHintSet) {
    return 0;
  }

  if (ui_kind_out != NULL) {
    *ui_kind_out = mHintKind;
  }

  if (tab_out != NULL) {
    *tab_out = mHintTab;
  }

  return 1;
}

pm_metal_process_id_t pm_metal_process_pending(void)
{
  return mPending;
}

void pm_metal_process_ui_from_tab(pm_metal_ui_handle_t        tab,
                                  pm_metal_process_ui_kind_t *kind_out,
                                  uint32_t                   *surface_out)
{
  pm_metal_gfx_surface_h surf;

  if (kind_out != NULL) {
    *kind_out = PM_METAL_PROC_UI_NONE;
  }

  if (surface_out != NULL) {
    *surface_out = PM_METAL_GFX_SURFACE_INVALID;
  }

  if (tab == PM_METAL_UI_HANDLE_INVALID) {
    return;
  }

  surf = pm_metal_ui_tab_surface(tab);
  if (surf != PM_METAL_GFX_SURFACE_INVALID && surf != PM_METAL_GFX_SURFACE_DEFAULT) {
    if (kind_out != NULL) {
      *kind_out = PM_METAL_PROC_UI_TAB;
    }

    if (surface_out != NULL) {
      *surface_out = (uint32_t)surf;
    }

    return;
  }

  if (kind_out != NULL) {
    *kind_out = PM_METAL_PROC_UI_FULLSCREEN;
  }

  if (surface_out != NULL) {
    *surface_out = PM_METAL_GFX_SURFACE_DEFAULT;
  }
}

pm_metal_process_id_t pm_metal_process_reserve(const char                *name,
                                               pm_metal_process_ui_kind_t ui_kind,
                                               pm_metal_ui_handle_t       tab)
{
  MetalProcessSlot          *s;
  pm_metal_process_ui_kind_t kind;
  uint32_t                   surface;

  /* One reserve in flight; nesting allowed when mCurrent is set. */
  if (mPending != PM_METAL_PROCESS_ID_INVALID) {
    pm_metal_log("metal-process: reserve refused (pending)");
    return PM_METAL_PROCESS_ID_INVALID;
  }

  s = MetalProcessAllocSlot();
  if (s == NULL) {
    pm_metal_log("metal-process: table full");
    return PM_METAL_PROCESS_ID_INVALID;
  }

  if (mNextId == PM_METAL_PROCESS_ID_INVALID) {
    mNextId = 1u;
  }

  s->id               = mNextId++;
  s->parent_id        = mCurrent;
  s->root_task_h      = PM_METAL_ASYNC_HANDLE_INVALID;
  s->saved_stdout_tab = PM_METAL_UI_HANDLE_INVALID;
  strlcpy_trunc(s->name, sizeof(s->name), name != NULL ? name : "mod");
  s->state = PM_METAL_PROC_STATE_RUNNING;
  s->tab   = tab;

  if (ui_kind == PM_METAL_PROC_UI_NONE && tab != PM_METAL_UI_HANDLE_INVALID) {
    pm_metal_process_ui_from_tab(tab, &kind, &surface);
    s->ui_kind = kind;
    s->surface = surface;
  } else if (ui_kind == PM_METAL_PROC_UI_TAB) {
    s->ui_kind = PM_METAL_PROC_UI_TAB;
    s->surface = (uint32_t)pm_metal_ui_tab_surface(tab);
    if (s->surface == PM_METAL_GFX_SURFACE_INVALID) {
      s->surface = PM_METAL_GFX_SURFACE_DEFAULT;
    }
  } else if (ui_kind == PM_METAL_PROC_UI_FULLSCREEN) {
    s->ui_kind = PM_METAL_PROC_UI_FULLSCREEN;
    s->surface = PM_METAL_GFX_SURFACE_DEFAULT;
  } else {
    s->ui_kind = PM_METAL_PROC_UI_NONE;
    s->surface = PM_METAL_GFX_SURFACE_INVALID;
  }

  mPending = s->id;
  pm_metal_logf("metal-process: reserve pid=%u name=%s parent=%u", s->id, s->name, s->parent_id);
  return s->id;
}

void pm_metal_process_stamp_begin(pm_metal_process_id_t id)
{
  mStampProc = id;
}

void pm_metal_process_stamp_end(void)
{
  mStampProc = PM_METAL_PROCESS_ID_INVALID;
}

uint32_t pm_metal_process_inherit_id(void)
{
  if (mStampProc != PM_METAL_PROCESS_ID_INVALID) {
    return (uint32_t)mStampProc;
  }

  if (mCurrent != PM_METAL_PROCESS_ID_INVALID) {
    return (uint32_t)mCurrent;
  }

  return 0u;
}

void pm_metal_process_bind_root_task(pm_metal_process_id_t id, pm_metal_async_handle_t root_task_h)
{
  MetalProcessSlot *s;

  s = MetalProcessFind(id);
  if (s == NULL) {
    return;
  }

  s->root_task_h = root_task_h;
  pm_metal_async_task_set_proc_id(root_task_h, (uint32_t)id);
}

pm_metal_async_handle_t pm_metal_process_root_task(pm_metal_process_id_t id)
{
  MetalProcessSlot *s;

  s = MetalProcessFind(id);
  return (s != NULL) ? s->root_task_h : PM_METAL_ASYNC_HANDLE_INVALID;
}

pm_metal_process_id_t pm_metal_process_parent(pm_metal_process_id_t id)
{
  MetalProcessSlot *s;

  s = MetalProcessFind(id);
  return (s != NULL) ? s->parent_id : PM_METAL_PROCESS_ID_INVALID;
}

void pm_metal_process_commit_live(pm_metal_process_id_t id)
{
  MetalProcessSlot *s;

  s = MetalProcessFind(id);
  if (s == NULL) {
    return;
  }

  s->state = PM_METAL_PROC_STATE_RUNNING;
  mCurrent = id;
  mPending = PM_METAL_PROCESS_ID_INVALID;
  pm_metal_wasm_set_stdout_tab(s->tab);
  if (s->root_task_h == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_process_bind_root_task(id, pm_metal_async_session_root_task());
  }

  pm_metal_logf("metal-process: live pid=%u name=%s", id, s->name);
}

void pm_metal_process_commit_child(pm_metal_process_id_t id, pm_metal_async_handle_t root_task_h)
{
  MetalProcessSlot *s;

  s = MetalProcessFind(id);
  if (s == NULL) {
    return;
  }

  s->saved_stdout_tab = pm_metal_wasm_stdout_tab();
  pm_metal_wasm_set_stdout_tab(s->tab);
  s->state = PM_METAL_PROC_STATE_RUNNING;
  mCurrent = id;
  mPending = PM_METAL_PROCESS_ID_INVALID;
  pm_metal_process_bind_root_task(id, root_task_h);
  pm_metal_logf("metal-process: child live pid=%u parent=%u name=%s", id, s->parent_id, s->name);
}

void pm_metal_process_release(pm_metal_process_id_t id)
{
  MetalProcessSlot *s;

  s = MetalProcessFind(id);
  if (s == NULL) {
    return;
  }

  if (mPending == id) {
    mPending = PM_METAL_PROCESS_ID_INVALID;
  }

  if (mCurrent == id) {
    mCurrent = PM_METAL_PROCESS_ID_INVALID;
  }

  memset(s, 0, sizeof(*s));
}

void pm_metal_process_reap(pm_metal_process_id_t id)
{
  MetalProcessSlot *s;

  s = MetalProcessFind(id);
  if (s == NULL) {
    return;
  }

  pm_metal_logf("metal-process: reap pid=%u name=%s", id, s->name);
  if (mCurrent == id) {
    mCurrent = PM_METAL_PROCESS_ID_INVALID;
  }

  if (mPending == id) {
    mPending = PM_METAL_PROCESS_ID_INVALID;
  }

  memset(s, 0, sizeof(*s));
}

int pm_metal_process_spawn_mod(const char                *name,
                               pm_metal_process_ui_kind_t ui_kind,
                               pm_metal_ui_handle_t       tab)
{
  /*
	 * Product path: command invoke (load mod → run func in a task = process).
	 * See docs/MODS.md. Name is the command (today == mod name).
	 */
  return pm_metal_mod_cmd_invoke(name, ui_kind, tab);
}

void pm_metal_process_pump_runners(void)
{
  /*
	 * Shell-facing runner drain across every CPU — no session pinning,
	 * so there is no "leave the live process's runner alone" case
	 * anymore. Re-entrant task_step (task.c) is a safe no-op if the
	 * live process's task happens to already be mid-step elsewhere.
	 */
  pm_metal_run_poll_all();
}

int pm_metal_process_poll(int32_t *status_out)
{
  MetalProcessSlot     *s;
  pm_metal_process_id_t parent;
  pm_metal_status_t     st;

  if (mCurrent == PM_METAL_PROCESS_ID_INVALID) {
    return 0;
  }

  if (pm_metal_async_session_active()) {
    pm_metal_async_session_pump();
  }

  s = MetalProcessFind(mCurrent);
  if (s == NULL || s->root_task_h == PM_METAL_ASYNC_HANDLE_INVALID) {
    return 0;
  }

  st = pm_metal_async_task_status(s->root_task_h);
  if (st != PM_METAL_DONE && st != PM_METAL_ERROR && st != PM_METAL_CANCELLED) {
    return 0;
  }

  if (status_out != NULL) {
    *status_out = (int32_t)st;
  }

  parent = s->parent_id;
  if (parent != PM_METAL_PROCESS_ID_INVALID) {
    /* Subprocess done — pop redirect; parent keeps running. */
    pm_metal_wasm_set_stdout_tab(s->saved_stdout_tab);
    pm_metal_process_reap(s->id);
    mCurrent = parent;
    pm_metal_ui_sync_input_focus();
    return 0;
  }

  pm_metal_wasm_live_finish();
  return (st == PM_METAL_DONE) ? 1 : -1;
}

int pm_metal_process_active(void)
{
  return (mCurrent != PM_METAL_PROCESS_ID_INVALID) ? 1 : 0;
}

pm_metal_process_id_t pm_metal_process_current(void)
{
  return mCurrent;
}

pm_metal_process_id_t pm_metal_process_self(void)
{
  if (mCurrent != PM_METAL_PROCESS_ID_INVALID) {
    return mCurrent;
  }

  return mPending;
}

const char *pm_metal_process_name(pm_metal_process_id_t id)
{
  MetalProcessSlot *s;

  s = MetalProcessFind(id);
  if (s == NULL || s->name[0] == '\0') {
    return NULL;
  }

  return s->name;
}

int pm_metal_process_info(pm_metal_process_id_t id, pm_metal_process_info_t *out)
{
  MetalProcessSlot *s;

  if (out == NULL) {
    return -1;
  }

  s = MetalProcessFind(id);
  if (s == NULL) {
    return -1;
  }

  memset(out, 0, sizeof(*out));
  out->id      = s->id;
  out->state   = s->state;
  out->ui_kind = (uint32_t)s->ui_kind;
  out->tab     = s->tab;
  out->surface = s->surface;
  strlcpy_trunc(out->name, sizeof(out->name), s->name);
  return 0;
}

uint32_t pm_metal_process_list(pm_metal_process_info_t *out, uint32_t max)
{
  uint32_t i;
  uint32_t n;

  if (out == NULL || max == 0) {
    return 0;
  }

  n = 0;
  for (i = 0; i < PM_METAL_PROCESS_MAX && n < max; i++) {
    if (!mSlots[i].used) {
      continue;
    }

    (void)pm_metal_process_info(mSlots[i].id, &out[n]);
    n++;
  }

  return n;
}

int pm_metal_process_attach_ui(pm_metal_process_id_t      id,
                               pm_metal_process_ui_kind_t ui_kind,
                               pm_metal_ui_handle_t       tab)
{
  MetalProcessSlot *s;

  s = MetalProcessFind(id);
  if (s == NULL) {
    return -1;
  }

  s->tab     = tab;
  s->ui_kind = ui_kind;
  if (ui_kind == PM_METAL_PROC_UI_TAB) {
    s->surface = (uint32_t)pm_metal_ui_tab_surface(tab);
  } else if (ui_kind == PM_METAL_PROC_UI_FULLSCREEN) {
    s->surface = PM_METAL_GFX_SURFACE_DEFAULT;
  } else {
    s->surface = PM_METAL_GFX_SURFACE_INVALID;
  }

  return 0;
}

pm_metal_ui_handle_t pm_metal_process_tab(pm_metal_process_id_t id)
{
  MetalProcessSlot *s;

  s = MetalProcessFind(id);
  return (s != NULL) ? s->tab : PM_METAL_UI_HANDLE_INVALID;
}

uint32_t pm_metal_process_surface(pm_metal_process_id_t id)
{
  MetalProcessSlot *s;

  s = MetalProcessFind(id);
  return (s != NULL) ? s->surface : PM_METAL_GFX_SURFACE_INVALID;
}

pm_metal_process_ui_kind_t pm_metal_process_ui_kind(pm_metal_process_id_t id)
{
  MetalProcessSlot *s;

  s = MetalProcessFind(id);
  return (s != NULL) ? s->ui_kind : PM_METAL_PROC_UI_NONE;
}

int pm_metal_process_kill(pm_metal_process_id_t id)
{
  MetalProcessSlot     *s;
  pm_metal_process_id_t parent;

  if (id == PM_METAL_PROCESS_ID_INVALID || id != mCurrent) {
    return -1;
  }

  s = MetalProcessFind(id);
  if (s == NULL) {
    return -1;
  }

  parent = s->parent_id;
  if (parent != PM_METAL_PROCESS_ID_INVALID) {
    if (s->root_task_h != PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_async_task_cancel(s->root_task_h);
    }

    pm_metal_wasm_set_stdout_tab(s->saved_stdout_tab);
    pm_metal_process_reap(id);
    mCurrent = parent;
    pm_metal_ui_sync_input_focus();
    return 0;
  }

  if (!pm_metal_async_session_active()) {
    pm_metal_process_reap(id);
    return 0;
  }

  pm_metal_wasm_live_finish();
  return 0;
}

/* ---- guest natives ---- */

static uint32_t pm_metal_process_self_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return (uint32_t)pm_metal_process_self();
}

static int32_t pm_metal_process_info_native(wasm_exec_env_t exec_env, uint32_t id, uint32_t dest)
{
  pm_metal_process_info_t info;
  void                   *native;

  (void)exec_env;
  if (mProcInst == NULL || !wasm_runtime_validate_app_addr(mProcInst, dest, sizeof(info))) {
    return 0;
  }

  if (pm_metal_process_info((pm_metal_process_id_t)id, &info) != 0) {
    return 0;
  }

  native = wasm_runtime_addr_app_to_native(mProcInst, dest);
  if (native == NULL) {
    return 0;
  }

  memcpy(native, &info, sizeof(info));
  return 1;
}

static uint32_t pm_metal_process_list_native(wasm_exec_env_t exec_env, uint32_t dest, uint32_t max)
{
  pm_metal_process_info_t tmp[PM_METAL_PROCESS_MAX];
  uint32_t                n;
  uint32_t                bytes;
  void                   *native;

  (void)exec_env;
  if (max == 0) {
    return 0;
  }

  if (max > PM_METAL_PROCESS_MAX) {
    max = PM_METAL_PROCESS_MAX;
  }

  bytes = max * (uint32_t)sizeof(pm_metal_process_info_t);
  if (mProcInst == NULL || !wasm_runtime_validate_app_addr(mProcInst, dest, bytes)) {
    return 0;
  }

  n = pm_metal_process_list(tmp, max);
  if (n == 0) {
    return 0;
  }

  native = wasm_runtime_addr_app_to_native(mProcInst, dest);
  if (native == NULL) {
    return 0;
  }

  memcpy(native, tmp, n * sizeof(pm_metal_process_info_t));
  return n;
}

static uint32_t pm_metal_process_ui_kind_native(wasm_exec_env_t exec_env, uint32_t id)
{
  (void)exec_env;
  return (uint32_t)pm_metal_process_ui_kind((pm_metal_process_id_t)id);
}

static uint32_t pm_metal_process_surface_native(wasm_exec_env_t exec_env, uint32_t id)
{
  (void)exec_env;
  return pm_metal_process_surface((pm_metal_process_id_t)id);
}

static uint32_t pm_metal_process_tab_native(wasm_exec_env_t exec_env, uint32_t id)
{
  (void)exec_env;
  return (uint32_t)pm_metal_process_tab((pm_metal_process_id_t)id);
}

static NativeSymbol g_pm_metal_process_native_symbols[] = {
  { "pm_metal_process_self", (void *)pm_metal_process_self_native, "()i", NULL },
  { "pm_metal_process_info", (void *)pm_metal_process_info_native, "(ii)i", NULL },
  { "pm_metal_process_list", (void *)pm_metal_process_list_native, "(ii)i", NULL },
  { "pm_metal_process_ui_kind", (void *)pm_metal_process_ui_kind_native, "(i)i", NULL },
  { "pm_metal_process_surface", (void *)pm_metal_process_surface_native, "(i)i", NULL },
  { "pm_metal_process_tab", (void *)pm_metal_process_tab_native, "(i)i", NULL },
};

int pm_metal_process_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_PROCESS_WASI_MODULE,
                                     g_pm_metal_process_native_symbols,
                                     sizeof(g_pm_metal_process_native_symbols) /
                                       sizeof(g_pm_metal_process_native_symbols[0]))) {
    return -1;
  }

  return 0;
}
