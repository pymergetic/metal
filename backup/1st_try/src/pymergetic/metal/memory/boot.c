/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pymergetic/metal/memory/boot.h>
#include <pymergetic/metal/memory/layout.h>
#include "../../platform/plat.h"
#include "../../port/headers/userspace_blob.h"
#include "../util/size.h"

#if defined(CONFIG_PM_MEMORY_BENCH)
#include <pymergetic/metal/memory/bench.h>
#include "../../port/headers/bench.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(CONFIG_PM_USERSPACE_BLOB)
#include <sys/mman.h>
#endif

#include <zephyr/kernel.h>

#define PM_METAL_MEMORY_BOOT_PROBE_BYTES ((size_t)256)
#define PM_METAL_SIZE_LINE_MAX 40U

static void format_size(char *out, size_t cap, size_t bytes)
{
	if (pm_metal_util_size_format_bytes(out, cap, bytes) < 0) {
		snprintf(out, cap, "%zu", bytes);
	}
}

static void print_metrics(const pm_metal_memory_layout_stats_t *stats)
{
	char used[PM_METAL_SIZE_LINE_MAX];
	char free_bytes[PM_METAL_SIZE_LINE_MAX];

	format_size(used, sizeof(used), stats->allocated);
	format_size(free_bytes, sizeof(free_bytes), stats->free_bytes);
	printf("    metrics: in use: %s  free: %s\n", used, free_bytes);
}

static int step_machine_ram(void)
{
	const char *source;
	char total[PM_METAL_SIZE_LINE_MAX];
	size_t machine;

	machine = pm_metal_memory_layout_machine_ram();
	source = pm_plat_machine_ram_source_name(pm_plat_machine_ram_source());
	format_size(total, sizeof(total), machine);
	printf("    total: %s\n", total);
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
	char size_line[PM_METAL_SIZE_LINE_MAX];

	if (heap == NULL || heap->ops == NULL) {
		return 1;
	}

	printf("\n-> %s\n", heap->name);
	format_size(size_line, sizeof(size_line), reserved);
	printf("    reserved: %s\n", size_line);

	if (heap->ops->alloc == NULL) {
		const size_t link = pm_metal_memory_layout_kernel_link();

		if (pm_metal_memory_layout_heap_stats(heap, &stats) == 0) {
			format_size(size_line, sizeof(size_line), stats.reserved);
			printf("    static size: %s\n", size_line);
		}

		if (link == 0U || stats.reserved == 0U) {
			printf("    check: failed\n");
			return 1;
		}

		format_size(size_line, sizeof(size_line), link);
		printf("    kernel link: %s  (_end)\n", size_line);
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
	format_size(size_line, sizeof(size_line), PM_METAL_MEMORY_BOOT_PROBE_BYTES);
	printf("    test alloc: ok (%s)\n", size_line);

	if (pm_metal_memory_layout_heap_stats(heap, &stats) == 0) {
		print_metrics(&stats);
	}

	heap->ops->free(block);
	printf("    test free:  ok\n");

	if (pm_metal_memory_layout_heap_stats(heap, &stats) == 0) {
		print_metrics(&stats);
	}

	if (heap->slot == PM_METAL_MEMORY_LAYOUT_SLOT_USERSPACE_BLOB) {
#if defined(CONFIG_PM_USERSPACE_BLOB_MEMTEST)
		printf("    memtest: %s\n", pm_userspace_blob_memtest_passed() ? "ok" : "failed");
		if (!pm_userspace_blob_memtest_passed()) {
			return 1;
		}
#endif
		block = malloc(PM_METAL_MEMORY_BOOT_PROBE_BYTES);
		if (block == NULL) {
			printf("    libc malloc: failed\n");
			return 1;
		}

		free(block);
		printf("    libc malloc: ok\n");

#if defined(CONFIG_PM_USERSPACE_BLOB)
		void *map = mmap(NULL, PM_METAL_MEMORY_BOOT_PROBE_BYTES, PROT_READ | PROT_WRITE,
				 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		if (map == MAP_FAILED) {
			printf("    blob mmap: failed\n");
			return 1;
		}

		*(volatile char *)map = 1;
		if (munmap(map, PM_METAL_MEMORY_BOOT_PROBE_BYTES) != 0) {
			printf("    blob mmap: munmap failed\n");
			return 1;
		}

		printf("    blob mmap: ok\n");
#endif
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
	if (step_heap(
		    pm_metal_memory_layout_heap_get(PM_METAL_MEMORY_LAYOUT_SLOT_USERSPACE_BLOB)) != 0) {
		goto fail;
	}

	layout = pm_metal_memory_layout_get();
	printf("\n-> Memory layout\n");
	pm_metal_memory_layout_report(&layout);

#if defined(CONFIG_PM_MEMORY_BENCH) && defined(CONFIG_PM_USERSPACE_BLOB)
	pm_metal_memory_bench_run(pm_port_bench_now_ns, CONFIG_BOARD_TARGET);
#endif

	printf("\n* pymergetic-metal ready\n\n");
	return 0;

fail:
	printf("\n! pymergetic-metal boot failed\n\n");
	return 1;
}
