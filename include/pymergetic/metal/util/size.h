/*
 * Byte-count formatting — binary-prefix (KiB/MiB/GiB/TiB) human strings.
 *
 * Single implementation, host-side only (src/common/pymergetic/metal/util/
 * size.c) — mods never link a copy of it. On wasm32 the declarations below
 * are wasi-style imports instead of ordinary prototypes: no local body
 * exists in a mod's own .wasm at all, the calls are resolved against
 * size.c's own native registration (pm_metal_util_size_native_register(),
 * same file) at wasm_runtime_instantiate() time, exactly like a real WASI
 * import. The host build of this same header (native target, no __wasm__)
 * declares plain C prototypes instead, backed directly by size.c.
 */
#ifndef PYMERGETIC_METAL_UTIL_SIZE_H_
#define PYMERGETIC_METAL_UTIL_SIZE_H_

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/wasi.h" /* IWYU pragma: keep */

/* "1023 TiB" + NUL */
#define PM_METAL_UTIL_SIZE_FORMAT_MAX 16U

/* This module's own import_module name — see size.c's native_register()
 * for the host side that must build from this exact same constant. */
#define PM_METAL_UTIL_SIZE_WASI_MODULE "pymergetic.metal.util.size"

#if defined(__wasm__)
#define PM_METAL_UTIL_SIZE_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_UTIL_SIZE_WASI_MODULE, name)
#endif

/*
 * Format bytes using binary prefixes (KiB/MiB/GiB/TiB) — largest unit with
 * value >= 1, integer division. Returns snprintf-style length, -1 on error.
 *
 * impl: common — src/common/pymergetic/metal/util/size.c
 * impl: wasi import — src/common/pymergetic/metal/util/size.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_util_size_format(char *out, size_t cap, uint64_t bytes)
	PM_METAL_UTIL_SIZE_IMPORT(pm_metal_util_size_format);
#else
int pm_metal_util_size_format(char *out, size_t cap, uint64_t bytes);
#endif

/*
 * Format as "<bytes> (<human>)", e.g. "92946432 (88 MiB)". Returns
 * snprintf-style length, -1 on error.
 *
 * impl: common — src/common/pymergetic/metal/util/size.c
 * impl: wasi import — src/common/pymergetic/metal/util/size.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_util_size_format_bytes(char *out, size_t cap, uint64_t bytes)
	PM_METAL_UTIL_SIZE_IMPORT(pm_metal_util_size_format_bytes);
#else
int pm_metal_util_size_format_bytes(char *out, size_t cap, uint64_t bytes);
#endif

#if !defined(__wasm__)
/*
 * Registers this module's own wasi-style imports (see
 * PM_METAL_UTIL_SIZE_WASI_MODULE above) — host-only, never included by
 * a mod. Call once, after
 * wasm_runtime_full_init() has succeeded and before the first
 * load()/instantiate() of any module that might import these (runtime.c's
 * init() is the only caller today). Returns 0 on success, -1 if WAMR
 * rejected the registration.
 *
 * impl: common — src/common/pymergetic/metal/util/size.c
 */
int pm_metal_util_size_native_register(void);
#endif

#endif /* PYMERGETIC_METAL_UTIL_SIZE_H_ */
