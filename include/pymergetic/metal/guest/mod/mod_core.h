/*
 * Mod registry — host-internal plumbing: not a guest-callable API at
 * all (no wasm-side declarations here), just the hooks other host
 * modules (process.c, wasm.c, async.c) call into the registry with.
 *
 * Convenience umbrella: mod.h (includes this + the other three above).
 * Contract: docs/MODS.md
 */
#ifndef PYMERGETIC_METAL_GUEST_MOD_MOD_CORE_H_
#define PYMERGETIC_METAL_GUEST_MOD_MOD_CORE_H_

#include <pymergetic/metal/guest/mod/mod_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

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

#endif /* PYMERGETIC_METAL_GUEST_MOD_MOD_CORE_H_ */
