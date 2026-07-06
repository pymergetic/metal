/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pymergetic/metal/memory/layout.h>
#include <pymergetic/metal/port/kernel_heap_port.h>
#include <pymergetic/metal/port/kernel_static_port.h>
#include <pymergetic/metal/port/malloc_heap_port.h>
#include <pymergetic/metal/port/platform.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_X86) && !PM_METAL_PORT_IS_FAKE_METAL && defined(CONFIG_X86_MEMMAP)
#include <zephyr/arch/x86/memmap.h>
#endif

extern char _end[];

#if defined(CONFIG_X86) && !PM_METAL_PORT_IS_FAKE_METAL && defined(CONFIG_X86_MEMMAP)

static bool pm_metal_layout_x86_memmap_discovered(void)
{
	return x86_memmap_source >= X86_MEMMAP_SOURCE_MULTIBOOT_MEM;
}

static size_t pm_metal_layout_x86_memmap_total_ram(void)
{
	size_t total = 0U;

	for (int i = 0; i < CONFIG_X86_MEMMAP_ENTRIES; i++) {
		if (x86_memmap[i].type == X86_MEMMAP_ENTRY_RAM) {
			total += (size_t)x86_memmap[i].length;
		}
	}

	return total;
}

#endif /* x86 real metal memmap */

static const pm_metal_memory_layout_heap_t pm_metal_memory_layout_heaps[PM_METAL_MEMORY_LAYOUT_SLOT_COUNT] = {
	{
		.slot = PM_METAL_MEMORY_LAYOUT_SLOT_KERNEL_STATIC,
		.name = "Kernel static",
		.ops = &pm_metal_port_kernel_static_ops,
	},
	{
		.slot = PM_METAL_MEMORY_LAYOUT_SLOT_KERNEL_HEAP,
		.name = "Kernel heap (k_malloc)",
		.ops = &pm_metal_port_kernel_heap_ops,
	},
	{
		.slot = PM_METAL_MEMORY_LAYOUT_SLOT_MALLOC_HEAP,
		.name = "Malloc heap (picolibc)",
		.ops = &pm_metal_port_malloc_heap_ops,
	},
};

size_t pm_metal_memory_layout_machine_ram(void)
{
#if defined(CONFIG_X86) && !PM_METAL_PORT_IS_FAKE_METAL && defined(CONFIG_X86_MEMMAP)
	if (pm_metal_layout_x86_memmap_discovered()) {
		const size_t total = pm_metal_layout_x86_memmap_total_ram();

		if (total > 0U) {
			return total;
		}
	}
#endif

	return DT_REG_SIZE(DT_CHOSEN(zephyr_sram));
}

size_t pm_metal_memory_layout_kernel_link(void)
{
	const uintptr_t base = (uintptr_t)CONFIG_SRAM_BASE_ADDRESS;
	const uintptr_t end = ROUND_UP((uintptr_t)&_end, 8);

	if (end <= base) {
		return 0U;
	}

	return (size_t)(end - base);
}

size_t pm_metal_memory_layout_sram_tail(void)
{
	const size_t link = pm_metal_memory_layout_kernel_link();
	const size_t sram_budget = DT_REG_SIZE(DT_CHOSEN(zephyr_sram));

	if (link >= sram_budget) {
		return 0U;
	}

	return sram_budget - link;
}

const pm_metal_memory_layout_heap_t *pm_metal_memory_layout_heap_get(pm_metal_memory_layout_slot_t slot)
{
	if (slot >= PM_METAL_MEMORY_LAYOUT_SLOT_COUNT) {
		return NULL;
	}

	return &pm_metal_memory_layout_heaps[slot];
}

size_t pm_metal_memory_layout_heap_bytes(const pm_metal_memory_layout_heap_t *heap)
{
	if (heap == NULL || heap->ops == NULL || heap->ops->bytes == NULL) {
		return 0U;
	}

	return heap->ops->bytes();
}

pm_metal_memory_layout_t pm_metal_memory_layout_get(void)
{
	pm_metal_memory_layout_t layout = {0};

	layout.machine_ram = pm_metal_memory_layout_machine_ram();
	layout.kernel_link = pm_metal_memory_layout_kernel_link();
	layout.kernel_static = pm_metal_memory_layout_heap_bytes(
		pm_metal_memory_layout_heap_get(PM_METAL_MEMORY_LAYOUT_SLOT_KERNEL_STATIC));
	layout.kernel_heap = pm_metal_memory_layout_heap_bytes(
		pm_metal_memory_layout_heap_get(PM_METAL_MEMORY_LAYOUT_SLOT_KERNEL_HEAP));
	layout.malloc_heap = pm_metal_memory_layout_heap_bytes(
		pm_metal_memory_layout_heap_get(PM_METAL_MEMORY_LAYOUT_SLOT_MALLOC_HEAP));

	return layout;
}

void pm_metal_memory_layout_report(const pm_metal_memory_layout_t *layout)
{
	if (layout == NULL) {
		return;
	}

	printf("    machine ram:   %zu\n", layout->machine_ram);
	printf("    kernel static: %zu\n", layout->kernel_static);
	printf("    kernel heap:   %zu  (k_malloc)\n", layout->kernel_heap);
	printf("    malloc heap:   %zu  (picolibc)\n", layout->malloc_heap);
	printf("    kernel link:   %zu  (_end)\n", layout->kernel_link);
}

const pm_metal_memory_layout_ops_t pm_metal_memory_layout_ops = {
	.machine_ram = pm_metal_memory_layout_machine_ram,
	.kernel_link = pm_metal_memory_layout_kernel_link,
	.report = pm_metal_memory_layout_report,
};

int pm_metal_memory_layout_heap_stats(const pm_metal_memory_layout_heap_t *heap,
				      pm_metal_memory_layout_stats_t *out)
{
	if (heap == NULL || heap->ops == NULL || out == NULL) {
		return -1;
	}

	memset(out, 0, sizeof(*out));
	out->reserved = pm_metal_memory_layout_heap_bytes(heap);

	if (heap->ops->stats != NULL) {
		return heap->ops->stats(out);
	}

	out->allocated = out->reserved;
	return 0;
}
