/** @file
  Fake process table — host anchor for live wasm guests. (impl: efi|bios)
**/
#include <pymergetic/metal/guest/process/process.h>
#include <pymergetic/metal/guest/wasm/wasm.h>
#include <pymergetic/metal/shell/ui/tab.h>
#include <pymergetic/metal/dev/gfx/gfx.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/log/log.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>

#include "wasm_export.h"

typedef struct {
  INT32                        used;
  pm_metal_process_id_t        id;
  CHAR8                        name[64];
  UINT32                       state;
  pm_metal_process_ui_kind_t   ui_kind;
  pm_metal_ui_handle_t         tab;
  UINT32                       surface;
} MetalProcessSlot;

STATIC MetalProcessSlot       mSlots[PM_METAL_PROCESS_MAX];
STATIC pm_metal_process_id_t  mNextId = 1u;
STATIC pm_metal_process_id_t  mCurrent = PM_METAL_PROCESS_ID_INVALID;
STATIC pm_metal_process_id_t  mPending = PM_METAL_PROCESS_ID_INVALID;
STATIC wasm_module_inst_t     mProcInst;

STATIC pm_metal_process_ui_kind_t  mHintKind = PM_METAL_PROC_UI_NONE;
STATIC pm_metal_ui_handle_t        mHintTab  = PM_METAL_UI_HANDLE_INVALID;
STATIC INT32                       mHintSet;

void
pm_metal_process_bind_inst (
  VOID  *module_inst
  )
{
  mProcInst = (wasm_module_inst_t)module_inst;
}

STATIC
MetalProcessSlot *
MetalProcessFind (
  pm_metal_process_id_t  id
  )
{
  UINT32  i;

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

STATIC
MetalProcessSlot *
MetalProcessAllocSlot (
  VOID
  )
{
  UINT32  i;

  for (i = 0; i < PM_METAL_PROCESS_MAX; i++) {
    if (!mSlots[i].used) {
      ZeroMem (&mSlots[i], sizeof (mSlots[i]));
      mSlots[i].used = 1;
      return &mSlots[i];
    }
  }

  return NULL;
}

void
pm_metal_process_set_spawn_hint (
  pm_metal_process_ui_kind_t  ui_kind,
  pm_metal_ui_handle_t        tab
  )
{
  mHintKind = ui_kind;
  mHintTab  = tab;
  mHintSet  = 1;
}

void
pm_metal_process_clear_spawn_hint (
  VOID
  )
{
  mHintKind = PM_METAL_PROC_UI_NONE;
  mHintTab  = PM_METAL_UI_HANDLE_INVALID;
  mHintSet  = 0;
}

int
pm_metal_process_spawn_hint (
  pm_metal_process_ui_kind_t  *ui_kind_out,
  pm_metal_ui_handle_t        *tab_out
  )
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

pm_metal_process_id_t
pm_metal_process_pending (
  VOID
  )
{
  return mPending;
}

void
pm_metal_process_ui_from_tab (
  pm_metal_ui_handle_t          tab,
  pm_metal_process_ui_kind_t   *kind_out,
  UINT32                       *surface_out
  )
{
  pm_metal_gfx_surface_h  surf;

  if (kind_out != NULL) {
    *kind_out = PM_METAL_PROC_UI_NONE;
  }

  if (surface_out != NULL) {
    *surface_out = PM_METAL_GFX_SURFACE_INVALID;
  }

  if (tab == PM_METAL_UI_HANDLE_INVALID) {
    return;
  }

  surf = pm_metal_ui_tab_surface (tab);
  if (surf != PM_METAL_GFX_SURFACE_INVALID
      && surf != PM_METAL_GFX_SURFACE_DEFAULT)
  {
    if (kind_out != NULL) {
      *kind_out = PM_METAL_PROC_UI_TAB;
    }

    if (surface_out != NULL) {
      *surface_out = (UINT32)surf;
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

pm_metal_process_id_t
pm_metal_process_reserve (
  CONST CHAR8                  *name,
  pm_metal_process_ui_kind_t    ui_kind,
  pm_metal_ui_handle_t          tab
  )
{
  MetalProcessSlot            *s;
  pm_metal_process_ui_kind_t   kind;
  UINT32                       surface;

  if (mCurrent != PM_METAL_PROCESS_ID_INVALID
      || mPending != PM_METAL_PROCESS_ID_INVALID
      || pm_metal_async_session_active ())
  {
    pm_metal_log ("metal-process: reserve refused (live guest)");
    return PM_METAL_PROCESS_ID_INVALID;
  }

  s = MetalProcessAllocSlot ();
  if (s == NULL) {
    pm_metal_log ("metal-process: table full");
    return PM_METAL_PROCESS_ID_INVALID;
  }

  if (mNextId == PM_METAL_PROCESS_ID_INVALID) {
    mNextId = 1u;
  }

  s->id = mNextId++;
  AsciiStrnCpyS (
    s->name,
    sizeof (s->name),
    name != NULL ? name : "mod",
    sizeof (s->name) - 1
    );
  s->state = PM_METAL_PROC_STATE_RUNNING;
  s->tab   = tab;

  if (ui_kind == PM_METAL_PROC_UI_NONE && tab != PM_METAL_UI_HANDLE_INVALID) {
    pm_metal_process_ui_from_tab (tab, &kind, &surface);
    s->ui_kind = kind;
    s->surface = surface;
  } else if (ui_kind == PM_METAL_PROC_UI_TAB) {
    s->ui_kind = PM_METAL_PROC_UI_TAB;
    s->surface = (UINT32)pm_metal_ui_tab_surface (tab);
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
  pm_metal_logf ("metal-process: reserve pid=%u name=%a", s->id, s->name);
  return s->id;
}

void
pm_metal_process_commit_live (
  pm_metal_process_id_t  id
  )
{
  MetalProcessSlot  *s;

  s = MetalProcessFind (id);
  if (s == NULL) {
    return;
  }

  s->state = PM_METAL_PROC_STATE_RUNNING;
  mCurrent = id;
  mPending = PM_METAL_PROCESS_ID_INVALID;
  pm_metal_logf ("metal-process: live pid=%u name=%a", id, s->name);
}

void
pm_metal_process_release (
  pm_metal_process_id_t  id
  )
{
  MetalProcessSlot  *s;

  s = MetalProcessFind (id);
  if (s == NULL) {
    return;
  }

  if (mPending == id) {
    mPending = PM_METAL_PROCESS_ID_INVALID;
  }

  if (mCurrent == id) {
    mCurrent = PM_METAL_PROCESS_ID_INVALID;
  }

  ZeroMem (s, sizeof (*s));
}

void
pm_metal_process_reap (
  pm_metal_process_id_t  id
  )
{
  MetalProcessSlot  *s;

  s = MetalProcessFind (id);
  if (s == NULL) {
    return;
  }

  pm_metal_logf ("metal-process: reap pid=%u name=%a", id, s->name);
  if (mCurrent == id) {
    mCurrent = PM_METAL_PROCESS_ID_INVALID;
  }

  if (mPending == id) {
    mPending = PM_METAL_PROCESS_ID_INVALID;
  }

  ZeroMem (s, sizeof (*s));
}

int
pm_metal_process_spawn_mod (
  CONST CHAR8                 *name,
  pm_metal_process_ui_kind_t   ui_kind,
  pm_metal_ui_handle_t         tab
  )
{
  INT32  rc;

  if (name == NULL || name[0] == '\0') {
    return -1;
  }

  if (!pm_metal_wasm_ready ()) {
    return -1;
  }

  if (pm_metal_process_active () || pm_metal_async_session_active ()) {
    pm_metal_log ("metal-process: spawn refused (already active)");
    return -1;
  }

  pm_metal_process_set_spawn_hint (ui_kind, tab);
  pm_metal_wasm_set_stdout_tab (tab);
  rc = pm_metal_wasm_run_mod (name);
  pm_metal_process_clear_spawn_hint ();

  if (!pm_metal_process_active ()) {
    pm_metal_wasm_set_stdout_tab (PM_METAL_UI_HANDLE_INVALID);
  }

  return rc;
}

int
pm_metal_process_poll (
  INT32  *status_out
  )
{
  INT32                 st;
  pm_metal_process_id_t pid;

  if (!pm_metal_async_session_active ()) {
    return 0;
  }

  pm_metal_async_session_pump ();
  if (!pm_metal_async_session_root_done ()) {
    return 0;
  }

  st = pm_metal_async_session_root_status ();
  if (status_out != NULL) {
    *status_out = st;
  }

  pid = mCurrent;
  if (pid == PM_METAL_PROCESS_ID_INVALID) {
    pid = mPending;
  }

  pm_metal_wasm_live_finish ();
  (VOID)pid;
  return (st == (INT32)PM_METAL_DONE) ? 1 : -1;
}

int
pm_metal_process_active (
  VOID
  )
{
  return (mCurrent != PM_METAL_PROCESS_ID_INVALID
          && pm_metal_async_session_active ()) ? 1 : 0;
}

pm_metal_process_id_t
pm_metal_process_current (
  VOID
  )
{
  return mCurrent;
}

pm_metal_process_id_t
pm_metal_process_self (
  VOID
  )
{
  if (mCurrent != PM_METAL_PROCESS_ID_INVALID) {
    return mCurrent;
  }

  return mPending;
}

CONST CHAR8 *
pm_metal_process_name (
  pm_metal_process_id_t  id
  )
{
  MetalProcessSlot  *s;

  s = MetalProcessFind (id);
  if (s == NULL || s->name[0] == '\0') {
    return NULL;
  }

  return s->name;
}

int
pm_metal_process_info (
  pm_metal_process_id_t     id,
  pm_metal_process_info_t  *out
  )
{
  MetalProcessSlot  *s;

  if (out == NULL) {
    return -1;
  }

  s = MetalProcessFind (id);
  if (s == NULL) {
    return -1;
  }

  ZeroMem (out, sizeof (*out));
  out->id      = s->id;
  out->state   = s->state;
  out->ui_kind = (UINT32)s->ui_kind;
  out->tab     = s->tab;
  out->surface = s->surface;
  AsciiStrnCpyS (out->name, sizeof (out->name), s->name, sizeof (out->name) - 1);
  return 0;
}

UINT32
pm_metal_process_list (
  pm_metal_process_info_t  *out,
  UINT32                    max
  )
{
  UINT32  i;
  UINT32  n;

  if (out == NULL || max == 0) {
    return 0;
  }

  n = 0;
  for (i = 0; i < PM_METAL_PROCESS_MAX && n < max; i++) {
    if (!mSlots[i].used) {
      continue;
    }

    (VOID)pm_metal_process_info (mSlots[i].id, &out[n]);
    n++;
  }

  return n;
}

int
pm_metal_process_attach_ui (
  pm_metal_process_id_t       id,
  pm_metal_process_ui_kind_t  ui_kind,
  pm_metal_ui_handle_t        tab
  )
{
  MetalProcessSlot  *s;

  s = MetalProcessFind (id);
  if (s == NULL) {
    return -1;
  }

  s->tab     = tab;
  s->ui_kind = ui_kind;
  if (ui_kind == PM_METAL_PROC_UI_TAB) {
    s->surface = (UINT32)pm_metal_ui_tab_surface (tab);
  } else if (ui_kind == PM_METAL_PROC_UI_FULLSCREEN) {
    s->surface = PM_METAL_GFX_SURFACE_DEFAULT;
  } else {
    s->surface = PM_METAL_GFX_SURFACE_INVALID;
  }

  return 0;
}

pm_metal_ui_handle_t
pm_metal_process_tab (
  pm_metal_process_id_t  id
  )
{
  MetalProcessSlot  *s;

  s = MetalProcessFind (id);
  return (s != NULL) ? s->tab : PM_METAL_UI_HANDLE_INVALID;
}

UINT32
pm_metal_process_surface (
  pm_metal_process_id_t  id
  )
{
  MetalProcessSlot  *s;

  s = MetalProcessFind (id);
  return (s != NULL) ? s->surface : PM_METAL_GFX_SURFACE_INVALID;
}

pm_metal_process_ui_kind_t
pm_metal_process_ui_kind (
  pm_metal_process_id_t  id
  )
{
  MetalProcessSlot  *s;

  s = MetalProcessFind (id);
  return (s != NULL) ? s->ui_kind : PM_METAL_PROC_UI_NONE;
}

int
pm_metal_process_kill (
  pm_metal_process_id_t  id
  )
{
  if (id == PM_METAL_PROCESS_ID_INVALID || id != mCurrent) {
    return -1;
  }

  if (!pm_metal_async_session_active ()) {
    pm_metal_process_reap (id);
    return 0;
  }

  pm_metal_wasm_live_finish ();
  return 0;
}

/* ---- guest natives ---- */

STATIC UINT32
pm_metal_process_self_native (
  wasm_exec_env_t  exec_env
  )
{
  (VOID)exec_env;
  return (UINT32)pm_metal_process_self ();
}

STATIC INT32
pm_metal_process_info_native (
  wasm_exec_env_t  exec_env,
  UINT32           id,
  UINT32           dest
  )
{
  pm_metal_process_info_t  info;
  VOID                    *native;

  (VOID)exec_env;
  if (mProcInst == NULL
      || !wasm_runtime_validate_app_addr (
            mProcInst,
            dest,
            sizeof (info)
            ))
  {
    return 0;
  }

  if (pm_metal_process_info ((pm_metal_process_id_t)id, &info) != 0) {
    return 0;
  }

  native = wasm_runtime_addr_app_to_native (mProcInst, dest);
  if (native == NULL) {
    return 0;
  }

  CopyMem (native, &info, sizeof (info));
  return 1;
}

STATIC UINT32
pm_metal_process_list_native (
  wasm_exec_env_t  exec_env,
  UINT32           dest,
  UINT32           max
  )
{
  pm_metal_process_info_t  tmp[PM_METAL_PROCESS_MAX];
  UINT32                   n;
  UINT32                   bytes;
  VOID                    *native;

  (VOID)exec_env;
  if (max == 0) {
    return 0;
  }

  if (max > PM_METAL_PROCESS_MAX) {
    max = PM_METAL_PROCESS_MAX;
  }

  bytes = max * (UINT32)sizeof (pm_metal_process_info_t);
  if (mProcInst == NULL
      || !wasm_runtime_validate_app_addr (mProcInst, dest, bytes))
  {
    return 0;
  }

  n = pm_metal_process_list (tmp, max);
  if (n == 0) {
    return 0;
  }

  native = wasm_runtime_addr_app_to_native (mProcInst, dest);
  if (native == NULL) {
    return 0;
  }

  CopyMem (native, tmp, n * sizeof (pm_metal_process_info_t));
  return n;
}

STATIC UINT32
pm_metal_process_ui_kind_native (
  wasm_exec_env_t  exec_env,
  UINT32           id
  )
{
  (VOID)exec_env;
  return (UINT32)pm_metal_process_ui_kind ((pm_metal_process_id_t)id);
}

STATIC UINT32
pm_metal_process_surface_native (
  wasm_exec_env_t  exec_env,
  UINT32           id
  )
{
  (VOID)exec_env;
  return pm_metal_process_surface ((pm_metal_process_id_t)id);
}

STATIC UINT32
pm_metal_process_tab_native (
  wasm_exec_env_t  exec_env,
  UINT32           id
  )
{
  (VOID)exec_env;
  return (UINT32)pm_metal_process_tab ((pm_metal_process_id_t)id);
}

STATIC NativeSymbol g_pm_metal_process_native_symbols[] = {
  { "pm_metal_process_self", (VOID *)pm_metal_process_self_native, "()i", NULL },
  { "pm_metal_process_info", (VOID *)pm_metal_process_info_native, "(ii)i", NULL },
  { "pm_metal_process_list", (VOID *)pm_metal_process_list_native, "(ii)i", NULL },
  { "pm_metal_process_ui_kind", (VOID *)pm_metal_process_ui_kind_native, "(i)i", NULL },
  { "pm_metal_process_surface", (VOID *)pm_metal_process_surface_native, "(i)i", NULL },
  { "pm_metal_process_tab", (VOID *)pm_metal_process_tab_native, "(i)i", NULL },
};

int
pm_metal_process_native_register (
  VOID
  )
{
  if (!wasm_runtime_register_natives (
         PM_METAL_PROCESS_WASI_MODULE,
         g_pm_metal_process_native_symbols,
         sizeof (g_pm_metal_process_native_symbols)
           / sizeof (g_pm_metal_process_native_symbols[0])
         ))
  {
    return -1;
  }

  return 0;
}
