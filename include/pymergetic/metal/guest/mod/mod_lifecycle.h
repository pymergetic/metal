/*
 * Mod registry — lifecycle: load/unload/ready/reset, plus everything a
 * mod declares about itself from on_load (capability, about record) and
 * registers from on_load (funcs, cmds). See mod_call.h for resolving +
 * invoking what got registered here, mod_fresh.h for private instances.
 *
 * Convenience umbrella: mod.h (includes this + the other two above).
 * Contract: docs/MODS.md
 */
#ifndef PYMERGETIC_METAL_GUEST_MOD_MOD_LIFECYCLE_H_
#define PYMERGETIC_METAL_GUEST_MOD_MOD_LIFECYCLE_H_

#include <pymergetic/metal/guest/mod/mod_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__wasm__)

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
 * Declare this mod's version/description/authors — one call, from
 * on_load, replacing this mod's previous about record (if any) wholesale.
 * about is the address of a pm_metal_mod_about_t in this mod's own
 * linear memory. 0 ok, -1 outside on_load or bad address.
 */
extern int32_t pm_metal_mod_set_about(uint32_t about) PM_METAL_MOD_IMPORT(pm_metal_mod_set_about);

/**
 * Fill *out (address of a pm_metal_mod_about_t in this mod's own linear
 * memory) with mod_name's declared about record. 0 ok (out all-zero if
 * that mod never called set_about), -1 if mod_name is not a known mod.
 */
extern int32_t pm_metal_mod_about_get(const char *mod_name, uint32_t out)
  PM_METAL_MOD_IMPORT(pm_metal_mod_about_get);

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
 * Declare this mod's version/description/authors — one call, from
 * on_load, replacing this mod's previous about record (if any) wholesale.
 * Only valid while mConnecting is set (i.e. called from on_load).
 * 0 ok, -1 if called outside on_load.
 */
int32_t pm_metal_mod_set_about(const pm_metal_mod_about_t *about);

/**
 * Fill *out with mod_name's declared about record. 0 ok (*out all-zero
 * if that mod never called set_about), -1 if mod_name is not a known
 * mod. No kernel special-case here — see boot/authors.h for the
 * kernel's own (separate, non-registry) about record.
 */
int32_t pm_metal_mod_about_get(const char *mod_name, pm_metal_mod_about_t *out);

/** "author" / "maintainer" / "contributor" — shared by the `about` shell
 * command and mod_py_bind.c so both print/return the same spelling. */
const char *pm_metal_mod_author_role_name(pm_metal_mod_author_role_t role);

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

#endif /* !__wasm__ */

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_GUEST_MOD_MOD_LIFECYCLE_H_ */
