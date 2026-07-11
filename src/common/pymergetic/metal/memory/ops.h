/*
 * Memory ops-struct layout — one shared shape, reused by every memory kind
 * (ram probe, kheap pool, bytecode arena — see ram.h/kheap.h/bytecode.h,
 * each of which has its own getter and its own bind impl). A kind that has
 * no use for a given slot leaves it NULL — callers only ever call a slot
 * the kind they're using is documented to support, so NULL there means
 * "not applicable to this kind", never "not implemented yet" (that case
 * gets a real stub function that returns 0/NULL instead; see the zephyr
 * ram.c/kheap.c/bytecode.c implementations).
 *
 * pm_metal_memory_kind_t + pm_metal_memory_resolve() below are an
 * alternative lookup path for callers that pick a kind at runtime (e.g.
 * iterating all kinds for diagnostics) — most call sites know their kind
 * at compile time and should call the dedicated getter directly instead.
 * See docs/RUNTIME.md, docs/SOURCETREE.md.
 */
#ifndef PYMERGETIC_METAL_MEMORY_OPS_H_
#define PYMERGETIC_METAL_MEMORY_OPS_H_

#include <stdint.h>

typedef enum pm_metal_memory_kind {
	PM_METAL_MEMORY_RAM = 0,
	PM_METAL_MEMORY_KHEAP,
	PM_METAL_MEMORY_BYTECODE,
	PM_METAL_MEMORY_KIND_COUNT,
} pm_metal_memory_kind_t;

typedef struct pm_metal_memory_ops {
	/* RAM only. */
	uint64_t (*probe)(void);

	/* KHEAP, BYTECODE. Establish the pool/arena for this kind, from a
	 * requested size. One per process per kind (runtime.c enforces one
	 * init()/shutdown() pair). Returns the pool/handle pointer (also
	 * written to *out_bytes), NULL on failure. */
	void *(*establish)(uint64_t requested_bytes, uint64_t *out_bytes);

	/* KHEAP, BYTECODE. Release the pool/arena obtained from
	 * establish(). For BYTECODE, every buffer handed out by alloc()
	 * must already be freed. */
	void (*release)(void);

	/* KHEAP, BYTECODE. Bytes currently established, 0 if none. */
	uint64_t (*bytes)(void);

	/* BYTECODE only. Allocate size bytes from the arena. NULL if
	 * exhausted or not established. */
	void *(*alloc)(uint32_t size);

	/* BYTECODE only. Free a buffer returned by alloc(). */
	void (*free)(void *ptr);
} pm_metal_memory_ops_t;

/* impl: common — src/common/pymergetic/metal/memory/ops.c
 *
 * Look up a kind's ops table by enum value instead of calling its
 * dedicated getter (pm_metal_memory_ram_ops() etc.) directly — for callers
 * that pick a kind dynamically. Just dispatches to the one dedicated
 * getter matching kind, so it carries no target-specific logic of its own
 * and needs only one impl, shared by every target. NULL if kind is out of
 * range. */
const pm_metal_memory_ops_t *pm_metal_memory_resolve(pm_metal_memory_kind_t kind);

#endif /* PYMERGETIC_METAL_MEMORY_OPS_H_ */
