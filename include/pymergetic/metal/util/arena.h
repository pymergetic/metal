/*
 * Fixed-size arena — first-fit, coalescing free-list allocator over a
 * single caller-supplied buffer. Pure C, zero OS dependency (impl: shared,
 * see docs/SOURCETREE.md) — backs the runtime's bytecode pool
 * (memory/bytecode.h pm_metal_memory_bytecode_ops) and is available to
 * guests too: init() never allocates, it only carves up memory the caller
 * already owns, so it works identically over a malloc'd block (linux), a
 * static array (zephyr), or wasm linear memory (a guest).
 */
#ifndef PYMERGETIC_METAL_UTIL_ARENA_H_
#define PYMERGETIC_METAL_UTIL_ARENA_H_

#include <stddef.h>

typedef struct pm_metal_util_arena pm_metal_util_arena_t;

/* impl: shared — include/pymergetic/metal/util/arena_impl.h
 *
 * Carve an arena out of buf/buf_len — buf is owned by the caller for the
 * arena's lifetime; init() does not allocate or take ownership. Returns
 * NULL if buf_len is too small to hold even the arena's own bookkeeping. */
pm_metal_util_arena_t *pm_metal_util_arena_init(void *buf, size_t buf_len);

/* impl: shared — include/pymergetic/metal/util/arena_impl.h
 *
 * First-fit allocation within the arena. NULL if no free block is big
 * enough (exhausted or fragmented) — never grows past the original buf_len. */
void *pm_metal_util_arena_alloc(pm_metal_util_arena_t *arena, size_t size);

/* impl: shared — include/pymergetic/metal/util/arena_impl.h
 *
 * Free a block returned by arena_alloc(); coalesces with physically
 * adjacent free neighbors to fight fragmentation. */
void pm_metal_util_arena_free(pm_metal_util_arena_t *arena, void *ptr);

/* impl: shared — include/pymergetic/metal/util/arena_impl.h
 *
 * Bytes currently allocated (excludes block-header bookkeeping overhead). */
size_t pm_metal_util_arena_used(const pm_metal_util_arena_t *arena);

#endif /* PYMERGETIC_METAL_UTIL_ARENA_H_ */
