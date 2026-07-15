/*
 * LZ4 block (de)compression — thin wrapper around the vendored upstream
 * library (external/lz4/lib/lz4.{c,h}, pinned + reproduced by
 * scripts/setup-lz4.sh, see docs/SOURCETREE.md "Vendoring"): plain LZ4
 * block format (no frame header, no checksum, no dictionary) via
 * upstream's own LZ4_compressBound() / LZ4_compress_default() /
 * LZ4_decompress_safe().
 *
 * Single implementation, host-side only (src/common/pymergetic/metal/
 * util/lz4.c; see util/size.h for the general pattern this follows) —
 * a mod never links a byte of upstream LZ4 itself, only ever calls
 * through this module's own wasi-style import bridge, same as
 * size.h/arena.h/log.h.
 */
#ifndef PYMERGETIC_METAL_UTIL_LZ4_H_
#define PYMERGETIC_METAL_UTIL_LZ4_H_

#include <stddef.h>

#include "pymergetic/metal/util/wasi.h" /* IWYU pragma: keep */

/* This module's own import_module name — see lz4.c's native_register()
 * for the host side that must build from this exact same constant. */
#define PM_METAL_UTIL_LZ4_WASI_MODULE "pymergetic.metal.util.lz4"

#if defined(__wasm__)
#define PM_METAL_UTIL_LZ4_IMPORT(name) \
	PM_METAL_UTIL_WASI_IMPORT(PM_METAL_UTIL_LZ4_WASI_MODULE, name)
#endif

/*
 * Worst-case compressed size for a src_len-byte input — size a dst
 * buffer with this before calling compress() below. 0 if src_len
 * exceeds LZ4's own (int-sized) length limit.
 *
 * impl: common — src/common/pymergetic/metal/util/lz4.c
 * impl: wasi import — src/common/pymergetic/metal/util/lz4.c (wasm32 only)
 */
#if defined(__wasm__)
extern size_t pm_metal_util_lz4_compress_bound(size_t src_len)
	PM_METAL_UTIL_LZ4_IMPORT(pm_metal_util_lz4_compress_bound);
#else
size_t pm_metal_util_lz4_compress_bound(size_t src_len);
#endif

/*
 * Compress src_len bytes at src into dst (capacity dst_cap), plain LZ4
 * block format. Returns the compressed size, or -1 if dst_cap is too
 * small (see compress_bound() above) or src_len/dst_cap exceed LZ4's own
 * length limit.
 *
 * impl: common — src/common/pymergetic/metal/util/lz4.c
 * impl: wasi import — src/common/pymergetic/metal/util/lz4.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_util_lz4_compress(const void *src, size_t src_len, void *dst, size_t dst_cap)
	PM_METAL_UTIL_LZ4_IMPORT(pm_metal_util_lz4_compress);
#else
int pm_metal_util_lz4_compress(const void *src, size_t src_len, void *dst, size_t dst_cap);
#endif

/*
 * Decompress an src_len-byte LZ4 block (as produced by compress() above)
 * at src into dst (capacity dst_cap). Returns the decompressed size, or
 * -1 on a malformed/truncated block or if dst_cap is too small for it.
 *
 * impl: common — src/common/pymergetic/metal/util/lz4.c
 * impl: wasi import — src/common/pymergetic/metal/util/lz4.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_util_lz4_decompress(const void *src, size_t src_len, void *dst, size_t dst_cap)
	PM_METAL_UTIL_LZ4_IMPORT(pm_metal_util_lz4_decompress);
#else
int pm_metal_util_lz4_decompress(const void *src, size_t src_len, void *dst, size_t dst_cap);
#endif

#if !defined(__wasm__)
/*
 * Registers this module's own wasi-style imports (see
 * PM_METAL_UTIL_LZ4_WASI_MODULE above) — host-only, never included by
 * a mod. Call once, after wasm_runtime_full_init() has succeeded and
 * before the first load()/instantiate() of any module that might import
 * these (runtime.c's init() is the only caller today). Returns 0 on
 * success, -1 if WAMR rejected the registration.
 *
 * impl: common — src/common/pymergetic/metal/util/lz4.c
 */
int pm_metal_util_lz4_native_register(void);
#endif

#endif /* PYMERGETIC_METAL_UTIL_LZ4_H_ */
