/*
 * Memory — zephyr ram ops (bind). Stub — E820/devicetree/CONFIG_SRAM probe
 * pending.
 */
#include "pymergetic/metal/memory/ram.h"

#include <stddef.h>

static uint64_t pm_metal_memory_zephyr_ram_probe(void)
{
	return 0;
}

static const pm_metal_memory_ops_t g_pm_metal_memory_ram_ops = {
	.probe = pm_metal_memory_zephyr_ram_probe,
	.establish = NULL,
	.release = NULL,
	.bytes = NULL,
	.alloc = NULL,
	.free = NULL,
};

const pm_metal_memory_ops_t *pm_metal_memory_ram_ops(void)
{
	return &g_pm_metal_memory_ram_ops;
}
