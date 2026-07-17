/*
 * Memory — linux ram ops (bind).
 */
#include "pymergetic/metal/memory/ram.h"

#include <stddef.h>
#include <unistd.h>

static uint64_t pm_metal_memory_nuttx_ram_probe(void)
{
	long pages = sysconf(_SC_PHYS_PAGES);
	long page_size = sysconf(_SC_PAGE_SIZE);

	if (pages < 0 || page_size < 0) {
		return 0;
	}

	return (uint64_t)pages * (uint64_t)page_size;
}

static const pm_metal_memory_ops_t g_pm_metal_memory_ram_ops = {
	.probe = pm_metal_memory_nuttx_ram_probe,
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
