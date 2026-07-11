/*
 * Memory — resolve() dispatch (impl: common). Just forwards to the
 * dedicated getter matching kind — see ops.h.
 */
#include "pymergetic/metal/memory/ops.h"

#include <stddef.h>

#include "pymergetic/metal/memory/bytecode.h"
#include "pymergetic/metal/memory/kheap.h"
#include "pymergetic/metal/memory/ram.h"

const pm_metal_memory_ops_t *pm_metal_memory_resolve(pm_metal_memory_kind_t kind)
{
	switch (kind) {
	case PM_METAL_MEMORY_RAM:
		return pm_metal_memory_ram_ops();
	case PM_METAL_MEMORY_KHEAP:
		return pm_metal_memory_kheap_ops();
	case PM_METAL_MEMORY_BYTECODE:
		return pm_metal_memory_bytecode_ops();
	default:
		return NULL;
	}
}
