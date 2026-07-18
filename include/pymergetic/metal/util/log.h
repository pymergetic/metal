/*
 * Leveled log formatting — global capture floor + "[LEVEL] msg" framing.
 *
 * Single implementation, host-side only (src/common/pymergetic/metal/util/
 * log.c) — see util/size.h for the general pattern this follows. Two
 * differences from size.h/arena.h forced a small contract change here
 * instead of a straight port:
 *
 *   - a WASM import cannot be variadic, so the printf-style entry points
 *     are gone from the imported/common contract below — a caller
 *     formats its own line first (guest: its own libc vsnprintf; host:
 *     the *f() convenience wrappers at the bottom of this header, host-
 *     only, not exported) and hands the *finished* line to write()/
 *     write_raw() as a plain `const char *`.
 *   - a `FILE *` is a host-only concept with no meaning across the wasm
 *     boundary, so write()/write_raw() take a `pm_metal_log_stream_t`
 *     instead — always one of the *host* process's own stdout/stderr;
 *     there is no per-guest console/stream concept in this codebase
 *     today (see docs/RUNTIME.md — the previous console/shell layer was
 *     removed) so that is also the only thing "stream" could mean here.
 *
 * This header is deliberately not aware of consoles/sinks/panes of any
 * kind — it only decides "is this message even worth keeping" (the one
 * global floor below) and how to frame what survives that check.
 */
#ifndef PYMERGETIC_METAL_UTIL_LOG_H_
#define PYMERGETIC_METAL_UTIL_LOG_H_

#include <stddef.h>

#include "pymergetic/metal/wasi.h" /* IWYU pragma: keep */

typedef enum pm_metal_log_level {
	PM_METAL_LOG_TRACE = 0,
	PM_METAL_LOG_DEBUG,
	PM_METAL_LOG_INFO,
	PM_METAL_LOG_WARN,
	PM_METAL_LOG_ERROR,
	PM_METAL_LOG_FATAL,
	PM_METAL_LOG_LEVEL_COUNT,
} pm_metal_log_level_t;

/* Which of the *host* process's own streams write()/write_raw() land on —
 * see file header re: no per-guest stream concept exists here. */
typedef enum pm_metal_log_stream {
	PM_METAL_LOG_STREAM_STDOUT = 0,
	PM_METAL_LOG_STREAM_STDERR = 1,
} pm_metal_log_stream_t;

/* This module's own import_module name — see log.c's native_register()
 * for the host side that must build from this exact same constant. */
#define PM_METAL_UTIL_LOG_WASI_MODULE "pymergetic.metal.util.log"

#if defined(__wasm__)
#define PM_METAL_UTIL_LOG_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_UTIL_LOG_WASI_MODULE, name)
#endif

/*
 * The one global capture floor: write()/write_raw() drop (do not format,
 * do not touch either stream at all) any message below `level`. There is
 * exactly one floor for the whole process — kernel and every guest share
 * it, there is no per-caller override. Defaults to PM_METAL_LOG_INFO.
 * Intentionally callable from any guest (not gated like mount()): any mod
 * can raise or lower the shared floor for the whole host process — that
 * is a known coarse control, not a privilege boundary. Not thread-safe
 * against concurrent set_level() calls racing writes — safe to call from
 * init()/main() before other threads exist, which is the only place this
 * codebase calls it today.
 *
 * impl: common — src/common/pymergetic/metal/util/log.c
 * impl: wasi import — src/common/pymergetic/metal/util/log.c (wasm32 only)
 */
#if defined(__wasm__)
extern void pm_metal_util_log_set_level(pm_metal_log_level_t level)
	PM_METAL_UTIL_LOG_IMPORT(pm_metal_util_log_set_level);
extern pm_metal_log_level_t pm_metal_util_log_get_level(void)
	PM_METAL_UTIL_LOG_IMPORT(pm_metal_util_log_get_level);
#else
void pm_metal_util_log_set_level(pm_metal_log_level_t level);
pm_metal_log_level_t pm_metal_util_log_get_level(void);
#endif

/*
 * "TRACE".."FATAL" (or "?" for an out-of-range value) written into
 * out/cap — buffer-out rather than a returned `const char *`: a raw host
 * pointer is not a valid wasm app address, so this is the only shape that
 * works as a real import too (see util/size.h's format() for the same
 * idiom). Returns snprintf-style length, -1 on error.
 *
 * impl: common — src/common/pymergetic/metal/util/log.c
 * impl: wasi import — src/common/pymergetic/metal/util/log.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_util_log_level_name(pm_metal_log_level_t level, char *out, size_t cap)
	PM_METAL_UTIL_LOG_IMPORT(pm_metal_util_log_level_name);
#else
int pm_metal_util_log_level_name(pm_metal_log_level_t level, char *out, size_t cap);
#endif

/*
 * "[LEVEL] " + msg + "\n" onto `stream`, iff level >= the global floor
 * (checked first, before touching `stream` at all) — otherwise a complete
 * no-op. `msg` is one already-formatted line, no trailing newline.
 *
 * impl: common — src/common/pymergetic/metal/util/log.c
 * impl: wasi import — src/common/pymergetic/metal/util/log.c (wasm32 only)
 */
#if defined(__wasm__)
extern void pm_metal_util_log_write(pm_metal_log_stream_t stream, pm_metal_log_level_t level,
				     const char *msg)
	PM_METAL_UTIL_LOG_IMPORT(pm_metal_util_log_write);
#else
void pm_metal_util_log_write(pm_metal_log_stream_t stream, pm_metal_log_level_t level,
			      const char *msg);
#endif

/*
 * msg + "\n" onto `stream`, flushed — same framing as write() minus the
 * "[LEVEL] " prefix and the global floor check (this always writes). For
 * a command's own *data* (the literal answer to what was asked), which
 * should never be dropped by the level floor or tagged with a severity it
 * doesn't have.
 *
 * impl: common — src/common/pymergetic/metal/util/log.c
 * impl: wasi import — src/common/pymergetic/metal/util/log.c (wasm32 only)
 */
#if defined(__wasm__)
extern void pm_metal_util_log_write_raw(pm_metal_log_stream_t stream, const char *msg)
	PM_METAL_UTIL_LOG_IMPORT(pm_metal_util_log_write_raw);
#else
void pm_metal_util_log_write_raw(pm_metal_log_stream_t stream, const char *msg);
#endif

#if !defined(__wasm__)
/*
 * printf-style convenience over write()/write_raw() above — host-only:
 * wasm imports cannot be variadic, and guest code already has its own
 * libc vsnprintf to do the same formatting locally before calling the
 * fixed-signature primitives above directly.
 *
 * impl: common — src/common/pymergetic/metal/util/log.c
 */
void pm_metal_util_log_writef(pm_metal_log_stream_t stream, pm_metal_log_level_t level,
			       const char *fmt, ...);
void pm_metal_util_log_write_rawf(pm_metal_log_stream_t stream, const char *fmt, ...);

/*
 * Registers this module's own wasi-style imports (see
 * PM_METAL_UTIL_LOG_WASI_MODULE above) — never included by a mod. Call
 * once, after wasm_runtime_full_init()
 * has succeeded and before the first load()/instantiate() of any module that
 * might import these (runtime.c's init() is the only caller today). Returns
 * 0 on success, -1 if WAMR rejected the registration.
 *
 * impl: common — src/common/pymergetic/metal/util/log.c
 */
int pm_metal_util_log_native_register(void);
#endif

#endif /* PYMERGETIC_METAL_UTIL_LOG_H_ */
