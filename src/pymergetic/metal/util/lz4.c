/*
 * pm_metal_util_lz4_* — impl: common (see util/lz4.h; wasm32 mods reach
 * this same code via this file's own wasi-style import registration at
 * the bottom, not via a second compiled copy of this file, and never
 * link a byte of upstream LZ4 themselves).
 *
 * All three functions below are a direct pass-through to the vendored
 * upstream library (external/lz4/lib/lz4.h, see docs/SOURCETREE.md
 * "Vendoring") — the only work done here is the size_t/int boundary
 * (LZ4's own public API is `int`-sized) and turning upstream's own
 * 0/negative failure returns into this module's single -1 sentinel.
 */
#include "pymergetic/metal/util/lz4.h"

#include <limits.h>

#include "external/lz4/lib/lz4.h" /* vendored — full path from this package's own root (see CMakeLists.txt's PM_METAL_ROOT include dir), not a bare "lz4.h" */

size_t pm_metal_util_lz4_compress_bound(size_t src_len)
{
	if (src_len > (size_t)INT_MAX) {
		return 0;
	}

	int bound = LZ4_compressBound((int)src_len);

	return bound > 0 ? (size_t)bound : 0;
}

int pm_metal_util_lz4_compress(const void *src, size_t src_len, void *dst, size_t dst_cap)
{
	if (!src || !dst || src_len > (size_t)INT_MAX || dst_cap > (size_t)INT_MAX) {
		return -1;
	}

	int n = LZ4_compress_default((const char *)src, (char *)dst, (int)src_len, (int)dst_cap);

	return n > 0 ? n : -1;
}

int pm_metal_util_lz4_decompress(const void *src, size_t src_len, void *dst, size_t dst_cap)
{
	if (!src || !dst || src_len > (size_t)INT_MAX || dst_cap > (size_t)INT_MAX) {
		return -1;
	}

	int n = LZ4_decompress_safe((const char *)src, (char *)dst, (int)src_len, (int)dst_cap);

	return n >= 0 ? n : -1;
}

/*
 * wasi-style import bridge — see size.c's own bridge comment for the
 * general signature-string rules this follows. src/dst are each a
 * '*'+'~' pair (address + its own byte length, for WAMR's own bounds
 * check at the import boundary) rather than a bare '*' — unlike arena.h's
 * opaque handles, src/dst here are real data buffers a guest also reads/
 * writes directly, so the length half of each pair is never optional.
 */
#include "wasm_export.h"

static int32_t pm_metal_util_lz4_compress_bound_native(wasm_exec_env_t exec_env, uint32_t src_len)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_lz4_compress_bound((size_t)src_len);
}

static int32_t pm_metal_util_lz4_compress_native(wasm_exec_env_t exec_env, const void *src,
						   uint32_t src_len, void *dst, uint32_t dst_cap)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_lz4_compress(src, (size_t)src_len, dst, (size_t)dst_cap);
}

static int32_t pm_metal_util_lz4_decompress_native(wasm_exec_env_t exec_env, const void *src,
						     uint32_t src_len, void *dst, uint32_t dst_cap)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_lz4_decompress(src, (size_t)src_len, dst, (size_t)dst_cap);
}

static NativeSymbol g_pm_metal_util_lz4_native_symbols[] = {
	{"pm_metal_util_lz4_compress_bound", (void *)pm_metal_util_lz4_compress_bound_native, "(i)i", NULL},
	{"pm_metal_util_lz4_compress", (void *)pm_metal_util_lz4_compress_native, "(*~*~)i", NULL},
	{"pm_metal_util_lz4_decompress", (void *)pm_metal_util_lz4_decompress_native, "(*~*~)i", NULL},
};

int pm_metal_util_lz4_native_register(void)
{
	if (!wasm_runtime_register_natives(PM_METAL_UTIL_LZ4_WASI_MODULE, g_pm_metal_util_lz4_native_symbols,
					    sizeof(g_pm_metal_util_lz4_native_symbols)
						    / sizeof(g_pm_metal_util_lz4_native_symbols[0]))) {
		return -1;
	}
	return 0;
}
