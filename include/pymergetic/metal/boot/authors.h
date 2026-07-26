/*
 * Metal's own "about" record — kernel authorship + one-line description,
 * hand-bumped like version.h, not build-generated. Deliberately outside
 * the mod registry (guest/mod/mod.h): the kernel is not a loaded mod, so
 * it gets its own tiny static table instead of a fake registry row.
 * Reuses pm_metal_mod_about_t / pm_metal_mod_author_t purely as a value
 * type — same struct shape, unrelated storage.
 *
 * impl: common — src/pymergetic/metal/boot/authors.c
 */
#ifndef PYMERGETIC_METAL_BOOT_AUTHORS_H_
#define PYMERGETIC_METAL_BOOT_AUTHORS_H_

#include <pymergetic/metal/guest/mod/mod_types.h>

#define PM_METAL_AUTHORS_WASI_MODULE "pymergetic.metal.authors"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_AUTHORS_IMPORT(name) PM_METAL_WASI_IMPORT(PM_METAL_AUTHORS_WASI_MODULE, name)

/**
 * Fill *out (address of a pm_metal_mod_about_t in this mod's own linear
 * memory) with an about record — name "" -> Metal's own (always 0); a
 * mod name -> that mod's declared record via the registry (0 ok, all-zero
 * if it never called pm_metal_mod_set_about(); -1 if name is unknown).
 * A wasm string can't carry NULL, unlike the host form below — pass ""
 * for "give me Metal's own", same as `about` with no argument.
 */
extern int32_t pm_metal_about_get(const char *name, uint32_t out)
  PM_METAL_AUTHORS_IMPORT(pm_metal_about_get);

/**
 * Convenience for mods that don't want their own identity (e.g. test
 * mods): replace this mod's about record with Metal's own kernel about
 * record, verbatim — same effect as this mod calling pm_metal_mod_set_about()
 * with a copy of the kernel's version/desc/authors. on_load-only, same
 * mConnecting rule as pm_metal_mod_set_about(). 0 ok, -1 outside on_load.
 */
extern int32_t pm_metal_mod_set_about_kernel(void)
  PM_METAL_AUTHORS_IMPORT(pm_metal_mod_set_about_kernel);
#else

/** Metal's own version/desc/authors. Host-only accessor, direct pointer —
 * see pm_metal_about_get() for the copying, dual-ABI (host + guest) form. */
const pm_metal_mod_about_t *pm_metal_kernel_about(void);

/**
 * *out = Metal's own about record if @a name is NULL or "" (mirrors the
 * `about` shell command's own no-arg case), else pm_metal_mod_about_get()
 * against the mod registry for @a name. One entry point instead of two —
 * deliberately *not* folded into pm_metal_mod_about_get() itself (that
 * stays registry-only; see mod.h) so guest/mod/mod.c never has to depend
 * on this (boot-only) kernel identity module.
 */
int32_t pm_metal_about_get(const char *name, pm_metal_mod_about_t *out);

/**
 * Host-side counterpart of the wasm import above — same effect
 * (pm_metal_mod_set_about(pm_metal_kernel_about())), for a compiled-in
 * host mod rather than a wasm guest. See the wasm-side declaration for
 * the on_load/mConnecting rule this still has to follow.
 */
int32_t pm_metal_mod_set_about_kernel(void);

int pm_metal_authors_native_register(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_AUTHORS_H_ */
