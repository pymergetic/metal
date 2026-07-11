/*
 * Machine RAM probe contract. Real total installed host/machine RAM —
 * diagnostics/reporting only, never wired into pool sizing on linux (see
 * docs/RUNTIME.md). See docs/SOURCETREE.md "Ops-struct flavor of `bind`".
 */
#ifndef PYMERGETIC_METAL_MEMORY_RAM_H_
#define PYMERGETIC_METAL_MEMORY_RAM_H_

#include "pymergetic/metal/memory/ops.h"

/* impl: bind — src/linux/pymergetic/metal/memory/ram.c
 *              src/zephyr/pymergetic/metal/memory/ram.c
 *
 * Only ->probe() is set — ram is not a pool, so every other slot is NULL. */
const pm_metal_memory_ops_t *pm_metal_memory_ram_ops(void);

#endif /* PYMERGETIC_METAL_MEMORY_RAM_H_ */
