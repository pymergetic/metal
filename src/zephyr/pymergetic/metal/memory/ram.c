/*
 * Memory — zephyr ram ops (bind).
 * Probe: x86 multiboot E820 when present, else CONFIG_SRAM_SIZE.
 */
#include "pymergetic/metal/memory/ram.h"

#include <stddef.h>

#include <zephyr/autoconf.h>
#include <zephyr/kernel.h>

#if defined(CONFIG_X86) && defined(CONFIG_X86_MEMMAP)
#include <zephyr/arch/x86/memmap.h>
#endif

#if defined(CONFIG_X86) && defined(CONFIG_X86_MEMMAP)

static uint64_t pm_metal_memory_zephyr_x86_memmap_ram_total(void)
{
	uint64_t total = 0;
	int i;

	for (i = 0; i < CONFIG_X86_MEMMAP_ENTRIES; i++) {
		if (x86_memmap[i].type == X86_MEMMAP_ENTRY_RAM) {
			total += (uint64_t)x86_memmap[i].length;
		}
	}

	return total;
}

#endif

static uint64_t pm_metal_memory_zephyr_ram_probe(void)
{
#if defined(CONFIG_X86) && defined(CONFIG_X86_MEMMAP)
	if (x86_memmap_source >= X86_MEMMAP_SOURCE_MULTIBOOT_MEM) {
		const uint64_t total = pm_metal_memory_zephyr_x86_memmap_ram_total();

		if (total > 0U) {
			return total;
		}
	}
#endif

	return (uint64_t)CONFIG_SRAM_SIZE * 1024ULL;
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
