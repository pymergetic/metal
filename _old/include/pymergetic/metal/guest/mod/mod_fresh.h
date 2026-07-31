/*
 * Mod registry — FRESH-mode private instances: open a scope handle,
 * resolve + call into it directly (no process-table spawn), then close
 * it. The standalone form of what mod_call.h's fn_process(FRESH) already
 * does internally for a spawned process.
 *
 * Convenience umbrella: mod.h (includes this + the other two above).
 * Contract: docs/MODS.md
 */
#ifndef PYMERGETIC_METAL_GUEST_MOD_MOD_FRESH_H_
#define PYMERGETIC_METAL_GUEST_MOD_MOD_FRESH_H_

#include <pymergetic/metal/guest/mod/mod_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__wasm__)

/**
 * Open a private, fresh instance of @a mod_name (refused — 0 — if the
 * mod declared PM_METAL_MOD_CAP_SINGLE, mirrors fn_process(FRESH)'s
 * existing guard). Loads the mod first if needed. Call
 * pm_metal_mod_fresh_resolve one or more times against the returned
 * handle, then pm_metal_mod_fresh_close it — the guest-side mirror of
 * pm_metal_mod_fn_process's FRESH path, but usable directly (no
 * process-table spawn) for "give me a handle, call into it a few
 * times, then close" — a wasm guest calling *another* mod previously
 * had no FRESH option at all (only fn_process did, and only host C
 * could reach it).
 */
extern pm_metal_mod_fresh_h_t pm_metal_mod_fresh_open(const char *mod_name)
  PM_METAL_MOD_IMPORT(pm_metal_mod_fresh_open);

/**
 * Resolve @a func_name against the fresh instance @a h (not the mod's
 * shared instance 0) → opaque fn handle (0 = fail), usable with
 * fn_coro / fn_process exactly like pm_metal_mod_func_resolve's handle.
 */
extern pm_metal_mod_fn_h_t pm_metal_mod_fresh_resolve(pm_metal_mod_fresh_h_t h,
                                                      const char            *func_name)
  PM_METAL_MOD_IMPORT(pm_metal_mod_fresh_resolve);

/** Deinstantiate the fresh instance and release the handle. */
extern int32_t pm_metal_mod_fresh_close(pm_metal_mod_fresh_h_t h)
  PM_METAL_MOD_IMPORT(pm_metal_mod_fresh_close);

#else /* host */

/**
 * Open a private, fresh instance of @a mod_name — the standalone
 * extraction of the instantiate step pm_metal_mod_fn_process's FRESH
 * path already does internally, usable directly without spawning a
 * process-table entry: "open a handle, resolve + call into it a few
 * times, then close it". Loads the mod first if needed. Refused (0)
 * if the mod declared PM_METAL_MOD_CAP_SINGLE (mirrors fn_process's
 * existing ModResolveUseFresh guard). 0 = fail.
 */
pm_metal_mod_fresh_h_t pm_metal_mod_fresh_open(const char *mod_name);

/**
 * Resolve @a func_name against the fresh instance @a h (not the mod's
 * shared instance 0) — same idiom as pm_metal_mod_func_resolve_on,
 * just keyed by the opaque scope handle instead of a raw image
 * pointer. 0 ok, -1 fail.
 */
int pm_metal_mod_fresh_resolve(pm_metal_mod_fresh_h_t h,
                               const char            *func_name,
                               pm_metal_mod_fn_t     *out);

/**
 * Deinstantiate the fresh instance opened by pm_metal_mod_fresh_open
 * and release the handle. Safe to call on an already-closed/invalid
 * handle (no-op). Decrements the owning mod's fresh_open count, same
 * accounting pm_metal_mod_on_fresh_instance_end already keeps for the
 * process-table FRESH path.
 */
void pm_metal_mod_fresh_close(pm_metal_mod_fresh_h_t h);

#endif /* !__wasm__ */

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_GUEST_MOD_MOD_FRESH_H_ */
