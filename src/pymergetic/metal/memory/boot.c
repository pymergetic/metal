/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pymergetic/metal/memory/boot.h>
#include <pymergetic/metal/memory/layout.h>
#include <pymergetic/metal/port/platform.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>

#if defined(CONFIG_X86) && !PM_METAL_PORT_IS_FAKE_METAL && defined(CONFIG_X86_MEMMAP)
#include <zephyr/arch/x86/memmap.h>
#endif

#define PM_METAL_MEMORY_BOOT_PROBE_BYTES ((size_t)256)

static void print_metrics(const pm_metal_memory_layout_stats_t *stats)
{
	printf("    metrics: in use: %zu  free: %zu\n", stats->allocated, stats->free_bytes);
}

static int step_machine_ram(void)
{
	const size_t machine = pm_metal_memory_layout_machine_ram();
	const char *source = "devicetree";

#if defined(CONFIG_X86) && !PM_METAL_PORT_IS_FAKE_METAL && defined(CONFIG_X86_MEMMAP)
	switch (x86_memmap_source) {
	case X86_MEMMAP_SOURCE_MULTIBOOT_MMAP:
		source = "multiboot E820";
		break;
	case X86_MEMMAP_SOURCE_MULTIBOOT_MEM:
		source = "multiboot basic";
		break;
	case X86_MEMMAP_SOURCE_MANUAL:
		source = "manual memmap";
		break;
	default:
		break;
	}
#endif

	printf("-> Machine RAM\n");
	printf("    total: %zu\n", machine);
	printf("    source: %s\n", source);

	if (machine == 0U) {
		printf("    check: failed (no SRAM)\n");
		return 1;
	}

	printf("    check: ok\n");
	return 0;
}

static int step_heap(const pm_metal_memory_layout_heap_t *heap)
{
	pm_metal_memory_layout_stats_t stats;
	void *block;
	const size_t reserved = pm_metal_memory_layout_heap_bytes(heap);

	if (heap == NULL || heap->ops == NULL) {
		return 1;
	}

	printf("\n-> %s\n", heap->name);
	printf("    reserved: %zu\n", reserved);

	if (heap->ops->alloc == NULL) {
		const size_t link = pm_metal_memory_layout_kernel_link();

		if (pm_metal_memory_layout_heap_stats(heap, &stats) == 0) {
			printf("    static size: %zu\n", stats.reserved);
		}

		if (link == 0U || stats.reserved == 0U) {
			printf("    check: failed\n");
			return 1;
		}

		printf("    kernel link: %zu  (_end)\n", link);
		printf("    check: ok\n");
		return 0;
	}

	if (reserved == 0U) {
		printf("    test alloc: skipped (disabled)\n");
		return 0;
	}

	block = heap->ops->alloc(PM_METAL_MEMORY_BOOT_PROBE_BYTES);
	if (block == NULL) {
		printf("    test alloc: failed\n");
		return 1;
	}

	memset(block, 0xA5, PM_METAL_MEMORY_BOOT_PROBE_BYTES);
	printf("    test alloc: ok (%zu bytes)\n", PM_METAL_MEMORY_BOOT_PROBE_BYTES);

	if (pm_metal_memory_layout_heap_stats(heap, &stats) == 0) {
		print_metrics(&stats);
	}

	heap->ops->free(block);
	printf("    test free:  ok\n");

	if (pm_metal_memory_layout_heap_stats(heap, &stats) == 0) {
		print_metrics(&stats);
	}

	if (heap->slot == PM_METAL_MEMORY_LAYOUT_SLOT_MALLOC_HEAP) {
		block = malloc(PM_METAL_MEMORY_BOOT_PROBE_BYTES);
		if (block == NULL) {
			printf("    libc malloc: failed\n");
			return 1;
		}

		free(block);
		printf("    libc malloc: ok\n");
	}

	return 0;
}

int pm_metal_memory_boot(void)
{
	pm_metal_memory_layout_t layout;

	printf("\n");
	printf("pymergetic-metal\n");
	printf("--------------------------------\n\n");
	printf("  welcome — starting on %s\n\n", CONFIG_BOARD_TARGET);

	if (step_machine_ram() != 0) {
		goto fail;
	}
	if (step_heap(pm_metal_memory_layout_heap_get(PM_METAL_MEMORY_LAYOUT_SLOT_KERNEL_STATIC)) != 0) {
		goto fail;
	}
	if (step_heap(pm_metal_memory_layout_heap_get(PM_METAL_MEMORY_LAYOUT_SLOT_KERNEL_HEAP)) != 0) {
		goto fail;
	}
	if (step_heap(pm_metal_memory_layout_heap_get(PM_METAL_MEMORY_LAYOUT_SLOT_MALLOC_HEAP)) != 0) {
		goto fail;
	}

	layout = pm_metal_memory_layout_get();
	printf("\n-> Memory layout\n");
	pm_metal_memory_layout_report(&layout);

	printf("\n* pymergetic-metal ready\n\n");
	return 0;

fail:
	printf("\n! pymergetic-metal boot failed\n\n");
	return 1;
}
