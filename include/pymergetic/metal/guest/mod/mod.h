/*
 * Mod registry — load/unload hooks; functions + commands → process.
 *
 * Contract: docs/MODS.md
 *   Loader calls only pm_metal_mod_on_load / pm_metal_mod_on_unload.
 *   Mod registers funcs/cmds from on_load. No magic step export.
 *   process = registered command runs a function in a task.
 *   Guests use the same load/unload/cmd API as the host (mod→mod).
 *
 * impl: src/pymergetic/metal/guest/mod/mod.c
 */
#ifndef PYMERGETIC_METAL_GUEST_MOD_MOD_H_
#define PYMERGETIC_METAL_GUEST_MOD_MOD_H_

#include <stdint.h>

#include <pymergetic/metal/guest/process/process.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/shell/ui/ui.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_MOD_WASI_MODULE "pymergetic.metal.mod"

#define PM_METAL_MOD_ID_INVALID 0u
#define PM_METAL_MOD_MAX        32u
#define PM_METAL_MOD_FUNC_MAX   1024u
#define PM_METAL_MOD_CMD_MAX    64u
#define PM_METAL_MOD_FN_H_MAX   64u

typedef uint32_t pm_metal_mod_id_t;
typedef uint32_t pm_metal_mod_fn_h_t;

#define PM_METAL_MOD_FN_H_INVALID 0u

/**
 * Resolved mod function — fill once at callsite, then call without
 * string lookup. Pointers are live while the mod stays loaded.
 */
typedef struct pm_metal_mod_fn {
  void *inst;     /* wasm_module_inst_t */
  void *exec_env; /* wasm_exec_env_t */
  void *fn;       /* wasm_function_inst_t — async (i)i */
} pm_metal_mod_fn_t;

/**
 * Resolved command — fn first so &cmd.fn works with fn_coro.
 * name is the registry command (default process table name).
 */
typedef struct pm_metal_mod_cmd {
  pm_metal_mod_fn_t fn;
  char              name[64];
} pm_metal_mod_cmd_t;

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"

#define PM_METAL_MOD_IMPORT(name) PM_METAL_WASI_IMPORT(PM_METAL_MOD_WASI_MODULE, name)

/** Load package, call on_load, ready (no process). 0 ok, -1 fail. */
extern int32_t pm_metal_mod_load(const char *name) PM_METAL_MOD_IMPORT(pm_metal_mod_load);

/** Unload if idle; on_unload; drop image. 0 ok, -1 busy/missing. */
extern int32_t pm_metal_mod_unload(const char *name) PM_METAL_MOD_IMPORT(pm_metal_mod_unload);

/** 1 if mod is ready or running, else 0. */
extern int32_t pm_metal_mod_ready(const char *name) PM_METAL_MOD_IMPORT(pm_metal_mod_ready);

/**
 * Convenience: cmd_resolve + fn_process (shell/µPy entry).
 * ui_kind: pm_metal_process_ui_kind_t; tab: pm_metal_ui_handle_t (0 = invalid).
 */
extern int32_t pm_metal_mod_cmd_invoke(const char *cmd_name, uint32_t ui_kind, uint32_t tab)
  PM_METAL_MOD_IMPORT(pm_metal_mod_cmd_invoke);

/** 1 if command is registered. */
extern int32_t pm_metal_mod_cmd_exists(const char *cmd_name)
  PM_METAL_MOD_IMPORT(pm_metal_mod_cmd_exists);

/**
 * Resolve once → opaque handle (0 = fail). Hot path uses handle, not names.
 */
extern pm_metal_mod_fn_h_t pm_metal_mod_func_resolve(const char *mod_name, const char *func_name)
  PM_METAL_MOD_IMPORT(pm_metal_mod_func_resolve);

/**
 * Resolve command → handle carrying fn + cmd name.
 * Same handle type; fn_coro / fn_process both accept it.
 */
extern pm_metal_mod_fn_h_t pm_metal_mod_cmd_resolve(const char *cmd_name)
  PM_METAL_MOD_IMPORT(pm_metal_mod_cmd_resolve);

/** Spawn guest coro for resolved handle (no pre-sized frame — use coro_alloc). */
extern pm_metal_async_handle_t pm_metal_mod_fn_coro(pm_metal_mod_fn_h_t fn_h)
  PM_METAL_MOD_IMPORT(pm_metal_mod_fn_coro);

/**
 * Command Extrawurst: UI/stdio redirect + process root.
 * proc_name empty/NULL → use resolved cmd name from handle.
 */
extern int32_t pm_metal_mod_fn_process(pm_metal_mod_fn_h_t fn_h,
                                       const char         *proc_name,
                                       uint32_t            ui_kind,
                                       uint32_t tab) PM_METAL_MOD_IMPORT(pm_metal_mod_fn_process);

/** 1 if mod has that function registered. */
extern int32_t pm_metal_mod_func_exists(const char *mod_name, const char *func_name)
  PM_METAL_MOD_IMPORT(pm_metal_mod_func_exists);

/**
 * Register a wasm export as a named function (async: (i)i status(self_h)).
 * Only valid during pm_metal_mod_on_load.
 */
extern int32_t pm_metal_mod_register_func(const char *name, const char *export_name)
  PM_METAL_MOD_IMPORT(pm_metal_mod_register_func);

/**
 * Register a shell/µPy command → existing function (invoke = process).
 * Only valid during pm_metal_mod_on_load. help may be NULL / "".
 */
extern int32_t pm_metal_mod_register_cmd(const char *cmd_name,
                                         const char *func_name,
                                         const char *help)
  PM_METAL_MOD_IMPORT(pm_metal_mod_register_cmd);

#else /* host */

/** Load package, call on_load, ready (no process). 0 ok, -1 fail. */
int pm_metal_mod_load(const char *name);

/** Call on_unload if idle; drop regs; deinstantiate. */
int pm_metal_mod_unload(const char *name);

int pm_metal_mod_ready(const char *name);

/**
 * Convenience: cmd_resolve + fn_process.
 */
int pm_metal_mod_cmd_invoke(const char                *cmd_name,
                            pm_metal_process_ui_kind_t ui_kind,
                            pm_metal_ui_handle_t       tab);

int pm_metal_mod_cmd_exists(const char *cmd_name);

/**
 * Load if needed; fill *out. Callsite keeps this — no more string finds.
 * 0 ok, -1 fail.
 */
int pm_metal_mod_func_resolve(const char *mod_name, const char *func_name, pm_metal_mod_fn_t *out);

/**
 * Resolve command → fn + name. Loads owning mod. 0 ok, -1 fail.
 */
int pm_metal_mod_cmd_resolve(const char *cmd_name, pm_metal_mod_cmd_t *out);

/** Spawn guest coro for resolved fn (frame via coro_alloc inside the step). */
pm_metal_async_handle_t pm_metal_mod_fn_coro(const pm_metal_mod_fn_t *fn);

/**
 * Command Extrawurst: process-root task + UI/stdio redirect.
 * Nestable under a live process (subprocess via task tree).
 * proc_name NULL or "" → use cmd->name.
 */
int pm_metal_mod_fn_process(const pm_metal_mod_cmd_t  *cmd,
                            const char                *proc_name,
                            pm_metal_process_ui_kind_t ui_kind,
                            pm_metal_ui_handle_t       tab);

/** 1 if mod has that function registered. */
int pm_metal_mod_func_exists(const char *mod_name, const char *func_name);

/**
 * Register a wasm export as a named function (async: (i)i status(self_h)).
 * Only valid during pm_metal_mod_on_load (connecting mod).
 */
int32_t pm_metal_mod_register_func(const char *name, const char *export_name);

/**
 * Register a shell/µPy command → existing function (invoke = process).
 * Only valid during pm_metal_mod_on_load. help may be NULL / "".
 */
int32_t pm_metal_mod_register_cmd(const char *cmd_name, const char *func_name, const char *help);

/** Process session ended — RUNNING → READY; image + regs kept. */
void pm_metal_mod_on_session_end(const char *name);

/** Guest coro create/release — open_tasks accounting for that mod instance. */
void pm_metal_mod_on_guest_coro_begin(void *module_inst);
void pm_metal_mod_on_guest_coro_end(void *module_inst);

int pm_metal_mod_native_register(void);

#endif /* !__wasm__ */

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_GUEST_MOD_MOD_H_ */
