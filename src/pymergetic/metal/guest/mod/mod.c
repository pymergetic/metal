/** @file
  Mod registry — on_load/on_unload; register funcs/cmds; cmd → process.
  See docs/MODS.md.
**/
#include <pymergetic/metal/guest/mod/mod.h>
#include <pymergetic/metal/guest/wasm/wasm.h>
#include <pymergetic/metal/guest/process/process.h>
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/shell/ui/tab.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/log/log.h>

#include <pymergetic/metal/runtime/mem/mem.h>

#include <stdint.h>
#include <string.h>

#include "wasm_export.h"

typedef enum {
  MOD_EMPTY = 0,
  MOD_READY,
  MOD_RUNNING
} mod_state_t;

typedef struct {
  int32_t                   used;
  char                      name[64];
  pm_metal_wasm_mod_image_t img;
  mod_state_t               state;
  uint32_t                  open_tasks;
} mod_slot_t;

typedef struct {
  int32_t used;
  char    name[64];
  char    mod_name[64];
  char    export_name[64];
  void   *fn; /* wasm_function_inst_t */
} mod_func_t;

typedef struct {
  int32_t used;
  char    name[64];
  char    mod_name[64];
  char    func_name[64];
  char    help[96];
  int32_t shell_registered;
} mod_cmd_t;

static mod_slot_t           mMods[PM_METAL_MOD_MAX];
static mod_func_t           mFuncs[PM_METAL_MOD_FUNC_MAX];
static mod_cmd_t            mCmds[PM_METAL_MOD_CMD_MAX];
static pm_metal_shell_cmd_t mShellCmds[PM_METAL_MOD_CMD_MAX];
static mod_slot_t          *mConnecting;

static mod_slot_t *ModFind(const char *name)
{
  uint32_t i;

  if (name == NULL || name[0] == '\0') {
    return NULL;
  }

  for (i = 0; i < PM_METAL_MOD_MAX; i++) {
    if (mMods[i].used && strcmp(mMods[i].name, name) == 0) {
      return &mMods[i];
    }
  }

  return NULL;
}

static mod_slot_t *ModAlloc(const char *name)
{
  uint32_t    i;
  mod_slot_t *s;

  s = ModFind(name);
  if (s != NULL) {
    return s;
  }

  for (i = 0; i < PM_METAL_MOD_MAX; i++) {
    if (!mMods[i].used) {
      memset(&mMods[i], 0, sizeof(mMods[i]));
      mMods[i].used = 1;
      strncpy(mMods[i].name, name, sizeof(mMods[i].name) - 1);
      mMods[i].state = MOD_EMPTY;
      return &mMods[i];
    }
  }

  return NULL;
}

static mod_func_t *FuncFind(const char *mod_name, const char *name)
{
  uint32_t i;

  if (mod_name == NULL || name == NULL) {
    return NULL;
  }

  for (i = 0; i < PM_METAL_MOD_FUNC_MAX; i++) {
    if (mFuncs[i].used && strcmp(mFuncs[i].mod_name, mod_name) == 0 &&
        strcmp(mFuncs[i].name, name) == 0) {
      return &mFuncs[i];
    }
  }

  return NULL;
}

static mod_cmd_t *CmdFind(const char *name)
{
  uint32_t i;

  if (name == NULL || name[0] == '\0') {
    return NULL;
  }

  for (i = 0; i < PM_METAL_MOD_CMD_MAX; i++) {
    if (mCmds[i].used && strcmp(mCmds[i].name, name) == 0) {
      return &mCmds[i];
    }
  }

  return NULL;
}

static void ModClearFuncs(const char *mod_name)
{
  uint32_t i;

  for (i = 0; i < PM_METAL_MOD_FUNC_MAX; i++) {
    if (mFuncs[i].used && strcmp(mFuncs[i].mod_name, mod_name) == 0) {
      memset(&mFuncs[i], 0, sizeof(mFuncs[i]));
    }
  }
}

static void ModShellCmdFn(int argc, char **argv)
{
  const char *cmd;

  (void)argc;
  if (argv == NULL || argv[0] == NULL) {
    return;
  }

  cmd = argv[0];
  (void)pm_metal_mod_cmd_invoke(cmd, PM_METAL_PROC_UI_FULLSCREEN, pm_metal_ui_tab_active());
}

static int32_t ModRegisterFuncHost(const char *name, const char *export_name)
{
  mod_func_t *f;
  void       *fn;
  uint32_t    i;

  if (mConnecting == NULL || name == NULL || name[0] == '\0' || export_name == NULL ||
      export_name[0] == '\0') {
    return -1;
  }

  fn = pm_metal_wasm_mod_image_lookup(&mConnecting->img, export_name);
  if (fn == NULL) {
    pm_metal_logf("metal-mod: register_func missing export '%s'", export_name);
    return -1;
  }

  f = FuncFind(mConnecting->name, name);
  if (f == NULL) {
    for (i = 0; i < PM_METAL_MOD_FUNC_MAX; i++) {
      if (!mFuncs[i].used) {
        f = &mFuncs[i];
        memset(f, 0, sizeof(*f));
        f->used = 1;
        strncpy(f->name, name, sizeof(f->name) - 1);
        strncpy(f->mod_name, mConnecting->name, sizeof(f->mod_name) - 1);
        break;
      }
    }
  }

  if (f == NULL) {
    return -1;
  }

  strncpy(f->export_name, export_name, sizeof(f->export_name) - 1);
  f->fn = fn;
  pm_metal_logf("metal-mod: func '%s' → export '%s' (%s)", name, export_name, mConnecting->name);
  return 0;
}

static int32_t ModRegisterCmdHost(const char *cmd_name, const char *func_name, const char *help)
{
  mod_cmd_t            *c;
  mod_func_t           *f;
  uint32_t              i;
  pm_metal_shell_cmd_t *sc;

  if (mConnecting == NULL || cmd_name == NULL || cmd_name[0] == '\0' || func_name == NULL ||
      func_name[0] == '\0') {
    return -1;
  }

  f = FuncFind(mConnecting->name, func_name);
  if (f == NULL || f->fn == NULL) {
    pm_metal_logf("metal-mod: register_cmd unknown func '%s'", func_name);
    return -1;
  }

  c = CmdFind(cmd_name);
  if (c != NULL && strcmp(c->mod_name, mConnecting->name) != 0) {
    pm_metal_logf("metal-mod: cmd '%s' owned by other mod", cmd_name);
    return -1;
  }

  if (c == NULL) {
    for (i = 0; i < PM_METAL_MOD_CMD_MAX; i++) {
      if (!mCmds[i].used) {
        c  = &mCmds[i];
        sc = &mShellCmds[i];
        memset(c, 0, sizeof(*c));
        c->used = 1;
        strncpy(c->name, cmd_name, sizeof(c->name) - 1);
        strncpy(c->mod_name, mConnecting->name, sizeof(c->mod_name) - 1);
        if (help != NULL && help[0] != '\0') {
          strncpy(c->help, help, sizeof(c->help) - 1);
        } else {
          strncpy(c->help, "mod command", sizeof(c->help) - 1);
        }

        sc->name = c->name;
        sc->help = c->help;
        sc->fn   = ModShellCmdFn;
        pm_metal_shell_cmd_register(sc);
        c->shell_registered = 1;
        break;
      }
    }
  } else {
    strncpy(c->func_name, func_name, sizeof(c->func_name) - 1);
    if (help != NULL && help[0] != '\0') {
      strncpy(c->help, help, sizeof(c->help) - 1);
    }

    pm_metal_logf("metal-mod: cmd '%s' → func '%s' (%s)", cmd_name, func_name, mConnecting->name);
    return 0;
  }

  if (c == NULL) {
    return -1;
  }

  strncpy(c->func_name, func_name, sizeof(c->func_name) - 1);
  pm_metal_logf("metal-mod: cmd '%s' → func '%s' (%s)", cmd_name, func_name, mConnecting->name);
  return 0;
}

static int ModConnect(mod_slot_t *s)
{
  mod_slot_t *prev;
  int32_t     ret;
  int32_t     rc;

  if (s == NULL) {
    return -1;
  }

  /* Nested load from another mod's on_load (deps) — restore after. */
  prev        = mConnecting;
  mConnecting = s;
  ret         = 0;
  rc          = pm_metal_wasm_mod_image_call0(&s->img, "pm_metal_mod_on_load", &ret);
  mConnecting = prev;
  if (rc != 0) {
    pm_metal_logf("metal-mod: missing on_load in %s", s->name);
    return -1;
  }

  if (ret != 0) {
    pm_metal_logf("metal-mod: on_load %s failed (%d)", s->name, (int)ret);
    return -1;
  }

  return 0;
}

static int ModDisconnect(mod_slot_t *s)
{
  int32_t ret;
  int32_t rc;

  if (s == NULL || s->img.inst == NULL) {
    return 0;
  }

  ret = 0;
  rc  = pm_metal_wasm_mod_image_call0(&s->img, "pm_metal_mod_on_unload", &ret);
  if (rc != 0) {
    /* unload hook optional if image was torn down mid-flight */
    return 0;
  }

  if (ret != 0) {
    pm_metal_logf("metal-mod: on_unload %s rc=%d", s->name, (int)ret);
  }

  return 0;
}

static int ModEnsureReady(mod_slot_t *s)
{
  const uint8_t *bytes;
  uint32_t       len;
  uint8_t       *esp_owned;
  int32_t        rc;

  if (s == NULL) {
    return -1;
  }

  if (s->state == MOD_READY && s->img.module != NULL) {
    return 0;
  }

  if (s->state == MOD_RUNNING) {
    return -1;
  }

  if (s->img.module != NULL) {
    (void)ModDisconnect(s);
    pm_metal_wasm_mod_image_close(&s->img);
  }

  esp_owned = NULL;
  rc        = pm_metal_wasm_mod_fetch(s->name, &bytes, &len, &esp_owned);
  if (rc != 0) {
    return -1;
  }

  rc = pm_metal_wasm_mod_image_open(s->name, bytes, len, &s->img);
  if (esp_owned != NULL) {
    pm_metal_mem_free(esp_owned);
  }

  if (rc != 0) {
    s->state = MOD_EMPTY;
    return -1;
  }

  if (ModConnect(s) != 0) {
    pm_metal_wasm_mod_image_close(&s->img);
    s->state = MOD_EMPTY;
    return -1;
  }

  s->state      = MOD_READY;
  s->open_tasks = 0;
  return 0;
}

int pm_metal_mod_load(const char *name)
{
  mod_slot_t *s;

  if (name == NULL || name[0] == '\0') {
    return -1;
  }

  if (!pm_metal_wasm_ready()) {
    return -1;
  }

  s = ModFind(name);
  if (s != NULL && s->state == MOD_RUNNING) {
    return 0;
  }

  if (s != NULL && s->state == MOD_READY && s->img.module != NULL) {
    return 0;
  }

  s = ModAlloc(name);
  if (s == NULL) {
    pm_metal_log("metal-mod: registry full");
    return -1;
  }

  if (ModEnsureReady(s) != 0) {
    if (s->state == MOD_EMPTY && s->img.module == NULL && CmdFind(name) == NULL) {
      s->used = 0;
    }

    return -1;
  }

  pm_metal_logf("metal-mod: ready %s", name);
  return 0;
}

int pm_metal_mod_unload(const char *name)
{
  mod_slot_t *s;
  uint32_t    i;

  s = ModFind(name);
  if (s == NULL) {
    return -1;
  }

  if (s->state == MOD_RUNNING || s->open_tasks != 0) {
    pm_metal_logf("metal-mod: unload refused %s (open tasks)", name);
    return -1;
  }

  if (pm_metal_process_active() && pm_metal_process_name(pm_metal_process_current()) != NULL &&
      strcmp(pm_metal_process_name(pm_metal_process_current()), name) == 0) {
    pm_metal_logf("metal-mod: unload refused %s (process live)", name);
    return -1;
  }

  (void)ModDisconnect(s);
  pm_metal_wasm_mod_image_close(&s->img);
  ModClearFuncs(name);
  for (i = 0; i < PM_METAL_MOD_CMD_MAX; i++) {
    if (mCmds[i].used && strcmp(mCmds[i].mod_name, name) == 0) {
      /* keep shell name; mark func stale until next load */
      mCmds[i].func_name[0] = '\0';
    }
  }

  s->state      = MOD_EMPTY;
  s->open_tasks = 0;
  pm_metal_logf("metal-mod: unloaded %s", name);
  return 0;
}

int pm_metal_mod_ready(const char *name)
{
  mod_slot_t *s;

  s = ModFind(name);
  return (s != NULL && (s->state == MOD_READY || s->state == MOD_RUNNING)) ? 1 : 0;
}

int pm_metal_mod_cmd_exists(const char *cmd_name)
{
  return CmdFind(cmd_name) != NULL ? 1 : 0;
}

static mod_slot_t *ModFindByInst(void *inst)
{
  uint32_t i;

  if (inst == NULL) {
    return NULL;
  }

  for (i = 0; i < PM_METAL_MOD_MAX; i++) {
    if (mMods[i].used && mMods[i].img.inst == inst) {
      return &mMods[i];
    }
  }

  return NULL;
}

void pm_metal_mod_on_guest_coro_begin(void *module_inst)
{
  mod_slot_t *s;

  s = ModFindByInst(module_inst);
  if (s == NULL) {
    return;
  }

  s->open_tasks++;
}

void pm_metal_mod_on_guest_coro_end(void *module_inst)
{
  mod_slot_t *s;

  s = ModFindByInst(module_inst);
  if (s == NULL || s->open_tasks == 0) {
    return;
  }

  s->open_tasks--;
  if (s->open_tasks == 0 && s->state == MOD_RUNNING &&
      pm_metal_process_current() == PM_METAL_PROCESS_ID_INVALID &&
      pm_metal_process_pending() == PM_METAL_PROCESS_ID_INVALID) {
    s->state = MOD_READY;
  }
}

void pm_metal_mod_on_session_end(const char *name)
{
  mod_slot_t *s;

  if (name == NULL || name[0] == '\0') {
    return;
  }

  s = ModFind(name);
  if (s == NULL) {
    return;
  }

  /* Process redirect ended — mod stays READY with image + regs. */
  if (s->state == MOD_RUNNING && s->open_tasks == 0) {
    s->state = MOD_READY;
    pm_metal_logf("metal-mod: process end %s (still ready)", name);
  } else if (s->state == MOD_RUNNING) {
    s->state = MOD_READY;
    pm_metal_logf("metal-mod: process end %s (still ready)", name);
  }
}

int pm_metal_mod_func_exists(const char *mod_name, const char *func_name)
{
  if (mod_name == NULL || mod_name[0] == '\0' || func_name == NULL || func_name[0] == '\0') {
    return 0;
  }

  return FuncFind(mod_name, func_name) != NULL ? 1 : 0;
}

int pm_metal_mod_func_resolve(const char *mod_name, const char *func_name, pm_metal_mod_fn_t *out)
{
  mod_slot_t *s;
  mod_func_t *f;

  if (out == NULL || mod_name == NULL || mod_name[0] == '\0' || func_name == NULL ||
      func_name[0] == '\0') {
    return -1;
  }

  memset(out, 0, sizeof(*out));
  if (pm_metal_mod_load(mod_name) != 0) {
    return -1;
  }

  s = ModFind(mod_name);
  f = FuncFind(mod_name, func_name);
  if (s == NULL || f == NULL || f->fn == NULL || s->img.inst == NULL || s->img.exec_env == NULL) {
    pm_metal_logf("metal-mod: resolve '%s.%s' failed", mod_name, func_name);
    return -1;
  }

  out->inst     = s->img.inst;
  out->exec_env = s->img.exec_env;
  out->fn       = f->fn;
  return 0;
}

pm_metal_async_handle_t pm_metal_mod_fn_coro(const pm_metal_mod_fn_t *fn)
{
  if (fn == NULL || fn->inst == NULL || fn->exec_env == NULL || fn->fn == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_async_coro_create_guest(fn->inst, fn->exec_env, fn->fn, 0u);
}

int pm_metal_mod_cmd_resolve(const char *cmd_name, pm_metal_mod_cmd_t *out)
{
  mod_cmd_t  *c;
  const char *mod_name;

  if (out == NULL || cmd_name == NULL || cmd_name[0] == '\0') {
    return -1;
  }

  memset(out, 0, sizeof(*out));
  c        = CmdFind(cmd_name);
  mod_name = (c != NULL) ? c->mod_name : cmd_name;
  if (pm_metal_mod_load(mod_name) != 0) {
    return -1;
  }

  c = CmdFind(cmd_name);
  if (c == NULL) {
    pm_metal_logf("metal-mod: no cmd '%s' after load", cmd_name);
    return -1;
  }

  if (pm_metal_mod_func_resolve(c->mod_name, c->func_name, &out->fn) != 0) {
    return -1;
  }

  strncpy(out->name, c->name, sizeof(out->name) - 1);
  out->name[sizeof(out->name) - 1] = '\0';
  return 0;
}

int pm_metal_mod_fn_process(const pm_metal_mod_cmd_t  *cmd,
                            const char                *proc_name,
                            pm_metal_process_ui_kind_t ui_kind,
                            pm_metal_ui_handle_t       tab)
{
  mod_slot_t              *s;
  pm_metal_async_handle_t  coro;
  pm_metal_async_handle_t  task_h;
  pm_metal_process_id_t    pid;
  pm_metal_process_id_t    parent;
  const char              *name;
  const pm_metal_mod_fn_t *fn;
  int32_t                  rc;

  if (cmd == NULL) {
    return -1;
  }

  fn   = &cmd->fn;
  name = (proc_name != NULL && proc_name[0] != '\0') ? proc_name : cmd->name;
  if (fn->inst == NULL || fn->exec_env == NULL || fn->fn == NULL || name == NULL ||
      name[0] == '\0') {
    return -1;
  }

  if (pm_metal_process_pending() != PM_METAL_PROCESS_ID_INVALID) {
    pm_metal_log("metal-mod: fn_process refused (reserve pending)");
    return -1;
  }

  s = ModFindByInst(fn->inst);
  if (s == NULL) {
    pm_metal_log("metal-mod: fn_process: unknown mod instance");
    return -1;
  }

  parent = pm_metal_process_current();
  pm_metal_process_set_spawn_hint(ui_kind, tab);
  pid = pm_metal_process_reserve(name, ui_kind, tab);
  pm_metal_process_clear_spawn_hint();
  if (pid == PM_METAL_PROCESS_ID_INVALID) {
    return -1;
  }

  coro = pm_metal_mod_fn_coro(fn);
  if (coro == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_process_release(pid);
    return -1;
  }

  if (pm_metal_async_session_active()) {
    /* Subprocess: child process-root task under live session. */
    pm_metal_process_stamp_begin(pid);
    task_h = pm_metal_async_create_task(coro);
    pm_metal_process_stamp_end();
    if (task_h == PM_METAL_ASYNC_HANDLE_INVALID) {
      pm_metal_async_coro_close(coro);
      pm_metal_process_release(pid);
      return -1;
    }

    pm_metal_process_commit_child(pid, task_h);
    s->state = MOD_RUNNING;
    pm_metal_logf(
      "metal-mod: subprocess '%s' under %u (mod '%s')", name, (unsigned)parent, s->name);
    return 0;
  }

  /* Top-level: stdout for startup pump + live. */
  pm_metal_wasm_set_stdout_tab(tab);
  rc = pm_metal_wasm_fn_start_async(
    s->img.module, fn->inst, fn->exec_env, fn->fn, coro, s->name, s->img.copy, (uint32_t)pid);
  if (rc != 0) {
    pm_metal_process_release(pid);
    return rc;
  }

  if (!pm_metal_async_session_active()) {
    pm_metal_process_release(pid);
    s->state = MOD_READY;
    return 0;
  }

  pm_metal_process_bind_root_task(pid, pm_metal_async_session_root_task());
  s->state = MOD_RUNNING;
  pm_metal_logf("metal-mod: process '%s' (mod '%s')", name, s->name);
  return 0;
}

#ifndef PM_METAL_MOD_FN_H_SLOTS
#define PM_METAL_MOD_FN_H_SLOTS PM_METAL_MOD_FN_H_MAX
#endif

/* Guest handles store cmd shape (fn + optional name). */
static pm_metal_mod_cmd_t mFnHandles[PM_METAL_MOD_FN_H_SLOTS + 1];

static pm_metal_mod_fn_h_t ModFnHandleAlloc(const pm_metal_mod_cmd_t *cmd)
{
  uint32_t i;

  for (i = 1; i <= PM_METAL_MOD_FN_H_SLOTS; i++) {
    if (mFnHandles[i].fn.fn == NULL) {
      mFnHandles[i] = *cmd;
      return (pm_metal_mod_fn_h_t)i;
    }
  }

  return PM_METAL_MOD_FN_H_INVALID;
}

static pm_metal_mod_cmd_t *ModFnHandleGet(pm_metal_mod_fn_h_t h)
{
  if (h == PM_METAL_MOD_FN_H_INVALID || h > PM_METAL_MOD_FN_H_SLOTS) {
    return NULL;
  }

  if (mFnHandles[h].fn.fn == NULL) {
    return NULL;
  }

  return &mFnHandles[h];
}

int pm_metal_mod_cmd_invoke(const char                *cmd_name,
                            pm_metal_process_ui_kind_t ui_kind,
                            pm_metal_ui_handle_t       tab)
{
  pm_metal_mod_cmd_t cmd;

  if (pm_metal_mod_cmd_resolve(cmd_name, &cmd) != 0) {
    return -1;
  }

  return pm_metal_mod_fn_process(&cmd, NULL, ui_kind, tab);
}

int32_t pm_metal_mod_register_func(const char *name, const char *export_name)
{
  return ModRegisterFuncHost(name, export_name);
}

int32_t pm_metal_mod_register_cmd(const char *cmd_name, const char *func_name, const char *help)
{
  return ModRegisterCmdHost(cmd_name, func_name, help);
}

static int32_t pm_metal_mod_load_native(wasm_exec_env_t exec_env, const char *name)
{
  (void)exec_env;
  return (int32_t)pm_metal_mod_load(name);
}

static int32_t pm_metal_mod_unload_native(wasm_exec_env_t exec_env, const char *name)
{
  (void)exec_env;
  return (int32_t)pm_metal_mod_unload(name);
}

static int32_t pm_metal_mod_ready_native(wasm_exec_env_t exec_env, const char *name)
{
  (void)exec_env;
  return (int32_t)pm_metal_mod_ready(name);
}

static int32_t pm_metal_mod_cmd_exists_native(wasm_exec_env_t exec_env, const char *cmd_name)
{
  (void)exec_env;
  return (int32_t)pm_metal_mod_cmd_exists(cmd_name);
}

static int32_t pm_metal_mod_cmd_invoke_native(wasm_exec_env_t exec_env,
                                              const char     *cmd_name,
                                              uint32_t        ui_kind,
                                              uint32_t        tab)
{
  (void)exec_env;
  return (int32_t)pm_metal_mod_cmd_invoke(
    cmd_name, (pm_metal_process_ui_kind_t)ui_kind, (pm_metal_ui_handle_t)tab);
}

static uint32_t pm_metal_mod_func_resolve_native(wasm_exec_env_t exec_env,
                                                 const char     *mod_name,
                                                 const char     *func_name)
{
  pm_metal_mod_cmd_t cmd;

  (void)exec_env;
  memset(&cmd, 0, sizeof(cmd));
  if (pm_metal_mod_func_resolve(mod_name, func_name, &cmd.fn) != 0) {
    return PM_METAL_MOD_FN_H_INVALID;
  }

  return (uint32_t)ModFnHandleAlloc(&cmd);
}

static uint32_t pm_metal_mod_cmd_resolve_native(wasm_exec_env_t exec_env, const char *cmd_name)
{
  pm_metal_mod_cmd_t cmd;

  (void)exec_env;
  if (pm_metal_mod_cmd_resolve(cmd_name, &cmd) != 0) {
    return PM_METAL_MOD_FN_H_INVALID;
  }

  return (uint32_t)ModFnHandleAlloc(&cmd);
}

static uint32_t pm_metal_mod_fn_coro_native(wasm_exec_env_t exec_env, uint32_t fn_h)
{
  pm_metal_mod_cmd_t *cmd;

  (void)exec_env;
  cmd = ModFnHandleGet((pm_metal_mod_fn_h_t)fn_h);
  if (cmd == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return (uint32_t)pm_metal_mod_fn_coro(&cmd->fn);
}

static int32_t pm_metal_mod_fn_process_native(
  wasm_exec_env_t exec_env, uint32_t fn_h, const char *proc_name, uint32_t ui_kind, uint32_t tab)
{
  pm_metal_mod_cmd_t *cmd;

  (void)exec_env;
  cmd = ModFnHandleGet((pm_metal_mod_fn_h_t)fn_h);
  if (cmd == NULL) {
    return -1;
  }

  return (int32_t)pm_metal_mod_fn_process(
    cmd, proc_name, (pm_metal_process_ui_kind_t)ui_kind, (pm_metal_ui_handle_t)tab);
}

static int32_t pm_metal_mod_func_exists_native(wasm_exec_env_t exec_env,
                                               const char     *mod_name,
                                               const char     *func_name)
{
  (void)exec_env;
  return (int32_t)pm_metal_mod_func_exists(mod_name, func_name);
}

static int32_t pm_metal_mod_register_func_native(wasm_exec_env_t exec_env,
                                                 const char     *name,
                                                 const char     *export_name)
{
  (void)exec_env;
  return pm_metal_mod_register_func(name, export_name);
}

static int32_t pm_metal_mod_register_cmd_native(wasm_exec_env_t exec_env,
                                                const char     *cmd_name,
                                                const char     *func_name,
                                                const char     *help)
{
  (void)exec_env;
  return pm_metal_mod_register_cmd(cmd_name, func_name, help);
}

static NativeSymbol g_pm_metal_mod_native_symbols[] = {
  { "pm_metal_mod_load", (void *)pm_metal_mod_load_native, "($)i", NULL },
  { "pm_metal_mod_unload", (void *)pm_metal_mod_unload_native, "($)i", NULL },
  { "pm_metal_mod_ready", (void *)pm_metal_mod_ready_native, "($)i", NULL },
  { "pm_metal_mod_cmd_exists", (void *)pm_metal_mod_cmd_exists_native, "($)i", NULL },
  { "pm_metal_mod_cmd_invoke", (void *)pm_metal_mod_cmd_invoke_native, "($ii)i", NULL },
  { "pm_metal_mod_func_resolve", (void *)pm_metal_mod_func_resolve_native, "($$)i", NULL },
  { "pm_metal_mod_cmd_resolve", (void *)pm_metal_mod_cmd_resolve_native, "($)i", NULL },
  { "pm_metal_mod_fn_coro", (void *)pm_metal_mod_fn_coro_native, "(i)i", NULL },
  { "pm_metal_mod_fn_process", (void *)pm_metal_mod_fn_process_native, "(i$ii)i", NULL },
  { "pm_metal_mod_func_exists", (void *)pm_metal_mod_func_exists_native, "($$)i", NULL },
  { "pm_metal_mod_register_func", (void *)pm_metal_mod_register_func_native, "($$)i", NULL },
  { "pm_metal_mod_register_cmd", (void *)pm_metal_mod_register_cmd_native, "($$$)i", NULL },
};

int pm_metal_mod_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_MOD_WASI_MODULE,
                                     g_pm_metal_mod_native_symbols,
                                     sizeof(g_pm_metal_mod_native_symbols) /
                                       sizeof(g_pm_metal_mod_native_symbols[0]))) {
    return -1;
  }

  return 0;
}
