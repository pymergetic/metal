/*
 * Bytecode arena contract — a dedicated pool for raw .wasm module buffers,
 * separate from the kheap pool so a big mod can never starve WAMR's own
 * runtime structs or vice versa. See docs/RUNTIME.md, docs/SOURCETREE.md
 * "Ops-struct flavor of `bind`".
 */
#ifndef PYMERGETIC_METAL_MEMORY_BYTECODE_H_
#define PYMERGETIC_METAL_MEMORY_BYTECODE_H_

#include "pymergetic/metal/memory/ops.h"

/* impl: bind — src/linux/pymergetic/metal/memory/bytecode.c
 *              src/zephyr/pymergetic/metal/memory/bytecode.c
 *
 * All of ->establish()/->release()/->bytes()/->alloc()/->free() are set;
 * ->probe() is NULL — this kind has no probe of its own. */
const pm_metal_memory_ops_t *pm_metal_memory_bytecode_ops(void);

#endif /* PYMERGETIC_METAL_MEMORY_BYTECODE_H_ */
