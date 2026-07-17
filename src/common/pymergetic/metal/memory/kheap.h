/*
 * WAMR kheap pool contract — the pool handed to WAMR's own Alloc_With_Pool
 * allocator (wasm linear memory + WAMR's own runtime structs). See
 * docs/RUNTIME.md, docs/SOURCETREE.md "Ops-struct flavor of `bind`".
 */
#ifndef PYMERGETIC_METAL_MEMORY_KHEAP_H_
#define PYMERGETIC_METAL_MEMORY_KHEAP_H_

#include "pymergetic/metal/memory/ops.h"

/* impl: bind — src/linux/pymergetic/metal/memory/kheap.c
 *              src/zephyr/pymergetic/metal/memory/kheap.c
 *              src/nuttx/pymergetic/metal/memory/kheap.c
 *
 * ->establish()/->release()/->bytes() are set; ->probe()/->alloc()/->free()
 * are NULL — this kind has no probe and is never sub-allocated (handed to
 * WAMR wholesale). */
const pm_metal_memory_ops_t *pm_metal_memory_kheap_ops(void);

#endif /* PYMERGETIC_METAL_MEMORY_KHEAP_H_ */
