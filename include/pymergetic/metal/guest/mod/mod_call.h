/*
 * Mod registry — resolve + invoke: turn a registered command/function
 * (see mod_lifecycle.h's register_func/register_cmd) into a call, either
 * the convenience one-shot (cmd_invoke) or resolve-once-call-many
 * (func_resolve/cmd_resolve + fn_coro/fn_process). Fresh, private
 * instances are mod_fresh.h instead.
 *
 * Convenience umbrella: mod.h (includes this + the other two above).
 * Contract: docs/MODS.md
 */
#ifndef PYMERGETIC_METAL_GUEST_MOD_MOD_CALL_H_
#define PYMERGETIC_METAL_GUEST_MOD_MOD_CALL_H_

#include <pymergetic/metal/guest/mod/mod_types.h>
#include <pymergetic/metal/guest/process/process.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/shell/ui/ui.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__wasm__)

/**
 * Convenience: cmd_resolve + fn_process (shell/µPy entry).
 * ui_kind: pm_metal_process_ui_kind_t; tab: pm_metal_ui_handle_t (0 = invalid).
 * instance_mode: pm_metal_mod_instance_t. flags: pm_metal_mod_flag_t bits.
 */
extern int32_t pm_metal_mod_cmd_invoke(const char *cmd_name,
                                       uint32_t    ui_kind,
                                       uint32_t    tab,
                                       uint32_t    instance_mode,
                                       uint32_t flags) PM_METAL_MOD_IMPORT(pm_metal_mod_cmd_invoke);

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
                                       uint32_t flags) PM_METAL_MOD_IMPORT(pm_metal_mod_fn_process);

/** 1 if mod has that function registered. */
extern int32_t pm_metal_mod_func_exists(const char *mod_name, const char *func_name)
  PM_METAL_MOD_IMPORT(pm_metal_mod_func_exists);

#else /* host */

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

#endif /* !__wasm__ */

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_GUEST_MOD_MOD_CALL_H_ */
