/*
 * ASCII art text — FIGlet "small" letterforms (embedded from upstream
 * small.flf; see ascii_fig_small.inc.c). Classic /|\_ art, ~5 rows —
 * much tighter than rasterizing the VGA console font as '#' blobs.
 *
 * Single implementation, host-side (src/pymergetic/metal/util/ascii.c) —
 * on wasm32 the declarations below are wasi-style imports resolved
 * against this file's own native registration, same shape as util/size.h.
 *
 * impl: common — src/pymergetic/metal/util/ascii.c
 * impl: wasi import — src/pymergetic/metal/util/ascii.c (wasm32 only)
 */
#ifndef PYMERGETIC_METAL_UTIL_ASCII_H_
#define PYMERGETIC_METAL_UTIL_ASCII_H_

#include <stddef.h>

#include "pymergetic/metal/wasi.h" /* IWYU pragma: keep */

/* This module's own import_module name — see ascii.c's native_register()
 * for the host side that must build from this exact same constant. */
#define PM_METAL_UTIL_ASCII_WASI_MODULE "pymergetic.metal.util.ascii"

#if defined(__wasm__)
#define PM_METAL_UTIL_ASCII_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_UTIL_ASCII_WASI_MODULE, name)
#endif

/*
 * Worst-case output bytes (including trailing NUL) for a text_len-byte
 * input rendered by ascii_render() below. Size `out` with this before
 * calling render().
 *
 * impl: common — src/pymergetic/metal/util/ascii.c
 * impl: wasi import — src/pymergetic/metal/util/ascii.c (wasm32 only)
 */
#if defined(__wasm__)
extern size_t pm_metal_util_ascii_bound(size_t text_len)
	PM_METAL_UTIL_ASCII_IMPORT(pm_metal_util_ascii_bound);
#else
size_t pm_metal_util_ascii_bound(size_t text_len);
#endif

/*
 * Render text as ASCII art into out (capacity out_cap). Rows are
 * newline-separated, trailing spaces trimmed, result NUL-terminated.
 * ink: character for set pixels (0 means '#').
 * Returns bytes written excluding NUL, or -1 on bad args / overflow.
 *
 * impl: common — src/pymergetic/metal/util/ascii.c
 * impl: wasi import — src/pymergetic/metal/util/ascii.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_util_ascii_render(const char *text, char ink, char *out,
				      size_t out_cap)
	PM_METAL_UTIL_ASCII_IMPORT(pm_metal_util_ascii_render);
#else
int pm_metal_util_ascii_render(const char *text, char ink, char *out,
			       size_t out_cap);
#endif

/*
 * Render text and append each art row to the host log ring (boot UI /
 * serial history). Guests reach the same host path via wasi import.
 *
 * impl: common — src/pymergetic/metal/util/ascii.c
 * impl: wasi import — src/pymergetic/metal/util/ascii.c (wasm32 only)
 */
#if defined(__wasm__)
extern void pm_metal_util_ascii_log(const char *text)
	PM_METAL_UTIL_ASCII_IMPORT(pm_metal_util_ascii_log);
#else
void pm_metal_util_ascii_log(const char *text);
#endif

#if !defined(__wasm__)
/*
 * Registers this module's own wasi-style imports (see
 * PM_METAL_UTIL_ASCII_WASI_MODULE above) — host-only, never included by
 * a mod. Call once after wasm_runtime_full_init(), before the first
 * load()/instantiate() of any module that might import these.
 *
 * impl: common — src/pymergetic/metal/util/ascii.c
 */
int pm_metal_util_ascii_native_register(void);
#endif

#endif /* PYMERGETIC_METAL_UTIL_ASCII_H_ */
