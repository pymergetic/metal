/*
 * Fixed-size arena — first-fit, coalescing free-list allocator over a
 * single caller-supplied buffer.
 *
 * Single implementation, host-side only (src/common/pymergetic/metal/util/
 * arena.c) — backs the runtime's bytecode pool (memory/bytecode.h
 * pm_metal_memory_bytecode_ops). On wasm32 the declarations below are
 * wasi-style imports (see util/size.h for the pattern this follows).
 *
 * The arena *control block* lives in a host-only slot table — never in
 * guest linear memory. `buf` still holds block headers + payloads only.
 * On wasm, the opaque handle from init() is a host id (uint32_t), not an
 * app offset — pass it back into alloc/free/used; never dereference it.
 * Alloc results remain ordinary guest pointers into `buf`.
 */
#ifndef PYMERGETIC_METAL_UTIL_ARENA_H_
#define PYMERGETIC_METAL_UTIL_ARENA_H_

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/wasi.h" /* IWYU pragma: keep */

typedef struct pm_metal_util_arena pm_metal_util_arena_t;

/* This module's own import_module name — see arena.c's native_register()
 * for the host side that must build from this exact same constant. */
#define PM_METAL_UTIL_ARENA_WASI_MODULE "pymergetic.metal.util.arena"

#if defined(__wasm__)
#define PM_METAL_UTIL_ARENA_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_UTIL_ARENA_WASI_MODULE, name)
/* Opaque host id — not a pointer into guest linear memory. */
typedef uint32_t pm_metal_util_arena_handle_t;
#endif

/*
 * Carve an arena out of buf/buf_len — buf is owned by the caller for the
 * arena's lifetime; init() does not allocate guest memory. Returns NULL /
 * 0 if buf_len is too small or the host handle table is full.
 *
 * impl: common — src/common/pymergetic/metal/util/arena.c
 * impl: wasi import — src/common/pymergetic/metal/util/arena.c (wasm32 only)
 */
#if defined(__wasm__)
extern pm_metal_util_arena_handle_t pm_metal_util_arena_init(void *buf, size_t buf_len)
	PM_METAL_UTIL_ARENA_IMPORT(pm_metal_util_arena_init);
#else
pm_metal_util_arena_t *pm_metal_util_arena_init(void *buf, size_t buf_len);
#endif

/*
 * First-fit allocation within the arena. NULL if no free block is big
 * enough (exhausted or fragmented) — never grows past the original buf_len.
 *
 * impl: common — src/common/pymergetic/metal/util/arena.c
 * impl: wasi import — src/common/pymergetic/metal/util/arena.c (wasm32 only)
 */
#if defined(__wasm__)
extern void *pm_metal_util_arena_alloc(pm_metal_util_arena_handle_t arena, size_t size)
	PM_METAL_UTIL_ARENA_IMPORT(pm_metal_util_arena_alloc);
#else
void *pm_metal_util_arena_alloc(pm_metal_util_arena_t *arena, size_t size);
#endif

/*
 * Free a block returned by arena_alloc(); coalesces with physically
 * adjacent free neighbors to fight fragmentation.
 *
 * impl: common — src/common/pymergetic/metal/util/arena.c
 * impl: wasi import — src/common/pymergetic/metal/util/arena.c (wasm32 only)
 */
#if defined(__wasm__)
extern void pm_metal_util_arena_free(pm_metal_util_arena_handle_t arena, void *ptr)
	PM_METAL_UTIL_ARENA_IMPORT(pm_metal_util_arena_free);
#else
void pm_metal_util_arena_free(pm_metal_util_arena_t *arena, void *ptr);
#endif

/*
 * Bytes currently allocated (excludes block-header bookkeeping overhead).
 *
 * impl: common — src/common/pymergetic/metal/util/arena.c
 * impl: wasi import — src/common/pymergetic/metal/util/arena.c (wasm32 only)
 */
#if defined(__wasm__)
extern size_t pm_metal_util_arena_used(pm_metal_util_arena_handle_t arena)
	PM_METAL_UTIL_ARENA_IMPORT(pm_metal_util_arena_used);
#else
size_t pm_metal_util_arena_used(const pm_metal_util_arena_t *arena);
#endif

#if !defined(__wasm__)
/*
 * Registers this module's own wasi-style imports (see
 * PM_METAL_UTIL_ARENA_WASI_MODULE above) — host-only, never included by
 * a mod. Call once, after
 * wasm_runtime_full_init() has succeeded and before the first
 * load()/instantiate() of any module that might import these (runtime.c's
 * init() is the only caller today). Returns 0 on success, -1 if WAMR
 * rejected the registration.
 *
 * impl: common — src/common/pymergetic/metal/util/arena.c
 */
int pm_metal_util_arena_native_register(void);
#endif

#endif /* PYMERGETIC_METAL_UTIL_ARENA_H_ */
