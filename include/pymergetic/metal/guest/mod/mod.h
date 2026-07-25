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
 * Instancing capability, declared once from on_load via
 * pm_metal_mod_set_capability(). Governs what AUTO resolves to and which
 * forced instance_mode values are honored — see pm_metal_mod_instance_t.
 * Undeclared mods default to SINGLE (today's original, back-compat
 * behavior: one persistent instance for everything).
 */
typedef enum {
  /* One persistent instance ("instance 0") for everything, forever.
   * Forced FRESH is refused. Pick this for stateless mods or mods that
   * are fine with shared/reentrant statics (e.g. hello, the test mods). */
  PM_METAL_MOD_CAP_SINGLE = 0,
  /* Instance 0 still exists and runs on_load (func/cmd registration),
   * but real invocations are expected to use a fresh instance instead —
   * AUTO resolves to FRESH. Forced SHARED is still allowed (e.g. a
   * lightweight library call), it's just not the default. Pick this for
   * mods with real static state that must not leak/persist across runs
   * or that want to run multiple times concurrently (e.g. Doom). */
  PM_METAL_MOD_CAP_MULTI = 1
} pm_metal_mod_cap_t;

/**
 * Per-call instance selection for cmd_invoke / fn_process.
 * See pm_metal_mod_cap_t for how AUTO resolves and which forced values
 * a mod's declared capability will refuse.
 */
typedef enum {
  PM_METAL_MOD_INSTANCE_AUTO   = 0, /* ask the mod's declared capability */
  PM_METAL_MOD_INSTANCE_SHARED = 1, /* force the mod's persistent "instance 0" */
  PM_METAL_MOD_INSTANCE_FRESH  = 2  /* force a fresh, private instance (refused if cap == SINGLE) */
} pm_metal_mod_instance_t;

/** Bit flags for cmd_invoke / fn_process, alongside pm_metal_mod_instance_t. */
typedef enum {
  PM_METAL_MOD_FLAG_NONE = 0u,
  /*
   * Once this call's process ends and its instance is torn down,
   * best-effort unload the whole mod too (drop the compiled module +
   * registry rows), instead of leaving it READY/resident. Refused
   * silently (mod just stays loaded) if anything else is still using
   * it — see pm_metal_mod_unload(). Only meaningful together with
   * FRESH (or AUTO resolving to FRESH); ignored for a SHARED call.
   */
  PM_METAL_MOD_FLAG_AUTO_UNLOAD = 1u << 0
} pm_metal_mod_flag_t;

/**
 * Resolved mod function — fill once at callsite, then call without
 * string lookup. Pointers are live while the mod stays loaded (this is
 * the mod's shared "instance 0" — see fn_process's instance_mode param
 * for a private, fresh instance instead).
 */
typedef struct pm_metal_mod_fn {
  void *inst;     /* wasm_module_inst_t */
  void *exec_env; /* wasm_exec_env_t */
  void *fn;       /* wasm_function_inst_t — async (i)i */
} pm_metal_mod_fn_t;

/**
 * Resolved command — fn first so &cmd.fn works with fn_coro.
 * name is the registry command (default process table name).
 * mod_name/export_name identify the owning mod + wasm export so
 * fn_process(FRESH) can re-resolve fn against a fresh instance instead
 * of the shared one above.
 */
typedef struct pm_metal_mod_cmd {
  pm_metal_mod_fn_t fn;
  char              name[64];
  char              mod_name[64];
  char              export_name[64];
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
 * Soft reset: keep the compiled module, tear down + recreate instance
 * 0's inst/exec_env and rerun on_load against it (no refetch/recompile).
 * Refused (like unload) if busy: open tasks, live fresh instances, or
 * this mod is the current live process. 0 ok, -1 refused/fail.
 */
extern int32_t pm_metal_mod_reset(const char *name) PM_METAL_MOD_IMPORT(pm_metal_mod_reset);

/**
 * Declare this mod's instancing capability. Only valid while running
 * inside pm_metal_mod_on_load (i.e. call it first thing from on_load).
 * See pm_metal_mod_cap_t. 0 ok, -1 if called outside on_load.
 */
extern int32_t pm_metal_mod_set_capability(uint32_t cap)
  PM_METAL_MOD_IMPORT(pm_metal_mod_set_capability);

/**
 * Convenience: cmd_resolve + fn_process (shell/µPy entry).
 * ui_kind: pm_metal_process_ui_kind_t; tab: pm_metal_ui_handle_t (0 = invalid).
 * instance_mode: pm_metal_mod_instance_t. flags: pm_metal_mod_flag_t bits.
 */
extern int32_t pm_metal_mod_cmd_invoke(
  const char *cmd_name, uint32_t ui_kind, uint32_t tab, uint32_t instance_mode, uint32_t flags)
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
 * instance_mode/flags: see pm_metal_mod_cmd_invoke.
 */
extern int32_t pm_metal_mod_fn_process(pm_metal_mod_fn_h_t fn_h,
                                       const char         *proc_name,
                                       uint32_t            ui_kind,
                                       uint32_t            tab,
                                       uint32_t            instance_mode,
                                       uint32_t            flags)
  PM_METAL_MOD_IMPORT(pm_metal_mod_fn_process);

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
 * Soft reset: keep the compiled module, tear down + recreate instance
 * 0's inst/exec_env and rerun on_load against it (no refetch/recompile
 * of the module bytes). Refused (same idle checks as unload) if the mod
 * has open tasks, live fresh instances, or is the current live process.
 * 0 ok, -1 refused/fail.
 */
int pm_metal_mod_reset(const char *name);

/**
 * Declare this mod's instancing capability — see pm_metal_mod_cap_t.
 * Only valid while mConnecting is set, i.e. called from on_load.
 * 0 ok, -1 if called outside on_load.
 */
int32_t pm_metal_mod_set_capability(pm_metal_mod_cap_t cap);

/**
 * Convenience: cmd_resolve + fn_process.
 * instance_mode: AUTO asks the mod's declared capability (default
 * SINGLE → shared "instance 0", today's original behavior); forced
 * SHARED/FRESH override it (FRESH refused if the mod declared SINGLE).
 * flags: PM_METAL_MOD_FLAG_* bits, e.g. AUTO_UNLOAD.
 */
int pm_metal_mod_cmd_invoke(const char                *cmd_name,
                            pm_metal_process_ui_kind_t ui_kind,
                            pm_metal_ui_handle_t       tab,
                            pm_metal_mod_instance_t    instance_mode,
                            uint32_t                   flags);

int pm_metal_mod_cmd_exists(const char *cmd_name);

/**
 * Load if needed; fill *out. Callsite keeps this — no more string finds.
 * Resolves against the mod's shared instance 0. 0 ok, -1 fail.
 */
int pm_metal_mod_func_resolve(const char *mod_name, const char *func_name, pm_metal_mod_fn_t *out);

/**
 * Like func_resolve, but against a specific already-instantiated image
 * instead of the mod's shared instance 0 — e.g. a fresh instance a
 * process owns (pm_metal_process_owned_image()). img_owner NULL behaves
 * exactly like func_resolve (instance 0). img_owner is a
 * const pm_metal_wasm_mod_image_t*; caller keeps it alive for the call.
 * Host-only (no guest-visible image pointers). 0 ok, -1 fail.
 */
int pm_metal_mod_func_resolve_on(const char        *mod_name,
                                 const char        *func_name,
                                 const void        *img_owner,
                                 pm_metal_mod_fn_t *out);

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
 * instance_mode/flags: see pm_metal_mod_cmd_invoke. FRESH (forced or
 * via AUTO) requires cmd->mod_name/cmd->export_name to be set
 * (pm_metal_mod_cmd_resolve fills these).
 */
int pm_metal_mod_fn_process(const pm_metal_mod_cmd_t  *cmd,
                            const char                *proc_name,
                            pm_metal_process_ui_kind_t ui_kind,
                            pm_metal_ui_handle_t       tab,
                            pm_metal_mod_instance_t    instance_mode,
                            uint32_t                   flags);

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

/**
 * Host-internal: teardown hook for a FRESH-mode instance owned by a
 * process slot (pm_metal_process_set_owned_image's counterpart).
 * img is a pm_metal_wasm_mod_image_t*. Deinstantiates it, updates the
 * mod's live fresh-instance count, and — if auto_unload — best-effort
 * unloads the whole mod once idle (see PM_METAL_MOD_FLAG_AUTO_UNLOAD).
 */
void pm_metal_mod_on_fresh_instance_end(void *img, int32_t auto_unload);

/** Guest coro create/release — open_tasks accounting for that mod instance. */
void pm_metal_mod_on_guest_coro_begin(void *module_inst);
void pm_metal_mod_on_guest_coro_end(void *module_inst);

int pm_metal_mod_native_register(void);

#endif /* !__wasm__ */

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_GUEST_MOD_MOD_H_ */
