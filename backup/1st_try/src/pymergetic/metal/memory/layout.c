/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pymergetic/metal/memory/layout.h>
#include "../../platform/plat.h"
#include "../util/size.h"

extern const pm_metal_memory_layout_heap_ops_t pm_metal_port_kernel_static_ops;
extern const pm_metal_memory_layout_heap_ops_t pm_metal_port_kernel_heap_ops;
extern const pm_metal_memory_layout_heap_ops_t pm_metal_port_userspace_blob_ops;

typedef struct pm_userspace_blob_stats {
	size_t total;
	size_t tlsf_used;
	size_t tlsf_free;
	size_t malloc_used;
	size_t mmap_used;
} pm_userspace_blob_stats_t;

int pm_userspace_blob_stats(pm_userspace_blob_stats_t *out);

#include <stdio.h>
#include <stdint.h>
#include <string.h>

extern char _end[];

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
		.slot = PM_METAL_MEMORY_LAYOUT_SLOT_USERSPACE_BLOB,
		.name = "Userspace blob (TLSF)",
		.ops = &pm_metal_port_userspace_blob_ops,
	},
};

size_t pm_metal_memory_layout_machine_ram(void)
{
	return pm_plat_machine_ram();
}

size_t pm_metal_memory_layout_kernel_link(void)
{
	return pm_plat_link_used();
}

size_t pm_metal_memory_layout_sram_tail(void)
{
	const size_t link = pm_metal_memory_layout_kernel_link();
	const size_t sram_budget = pm_plat_link_window();

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
	layout.userspace_blob = pm_metal_memory_layout_heap_bytes(
		pm_metal_memory_layout_heap_get(PM_METAL_MEMORY_LAYOUT_SLOT_USERSPACE_BLOB));

#if defined(CONFIG_PM_USERSPACE_BLOB)
	{
		pm_userspace_blob_stats_t blob_stats;

		if (pm_userspace_blob_stats(&blob_stats) == 0) {
			layout.userspace_malloc_used = blob_stats.malloc_used;
			layout.userspace_mmap_used = blob_stats.mmap_used;
			layout.userspace_pool_free = blob_stats.tlsf_free;
		}
	}
#endif

	return layout;
}

static void pm_metal_layout_report_userspace_usage(const char *prefix,
						   const pm_metal_memory_layout_t *layout)
{
	char human[40];

#define PM_LAYOUT_FMT_SIZE(value) \
	do { \
		if (pm_metal_util_size_format_bytes(human, sizeof(human), (value)) < 0) { \
			snprintf(human, sizeof(human), "%zu", (size_t)(value)); \
		} \
	} while (0)

#define PM_LAYOUT_TREE_LINE(prefix, connector, name, value, note) \
	do { \
		PM_LAYOUT_FMT_SIZE(value); \
		if ((note) != NULL && (note)[0] != '\0') { \
			printf("    %s%s%-16s  %s  %s\n", (prefix), (connector), (name), human, \
			       (note)); \
		} else { \
			printf("    %s%s%-16s  %s\n", (prefix), (connector), (name), human); \
		} \
	} while (0)

	PM_LAYOUT_TREE_LINE(prefix, "+-- ", "libc malloc", layout->userspace_malloc_used, "in use");
	PM_LAYOUT_TREE_LINE(prefix, "+-- ", "mmap", layout->userspace_mmap_used, "in use");
	PM_LAYOUT_TREE_LINE(prefix, "`-- ", "pool free", layout->userspace_pool_free, "");

#undef PM_LAYOUT_TREE_LINE
#undef PM_LAYOUT_FMT_SIZE
}

void pm_metal_memory_layout_report(const pm_metal_memory_layout_t *layout)
{
	char human[40];

	if (layout == NULL) {
		return;
	}

#define PM_LAYOUT_FMT_SIZE(value) \
	do { \
		if (pm_metal_util_size_format_bytes(human, sizeof(human), (value)) < 0) { \
			snprintf(human, sizeof(human), "%zu", (size_t)(value)); \
		} \
	} while (0)

#define PM_LAYOUT_TREE_LINE(prefix, connector, name, value, note) \
	do { \
		PM_LAYOUT_FMT_SIZE(value); \
		if ((note) != NULL && (note)[0] != '\0') { \
			printf("    %s%s%-16s  %s  %s\n", (prefix), (connector), (name), human, \
			       (note)); \
		} else { \
			printf("    %s%s%-16s  %s\n", (prefix), (connector), (name), human); \
		} \
	} while (0)

	PM_LAYOUT_FMT_SIZE(layout->machine_ram);
	printf("    %-20s  %s\n", "machine ram", human);
	printf("    |\n");

#if PM_METAL_PORT_IS_FAKE_METAL
	PM_LAYOUT_TREE_LINE("", "+-- ", "kernel link", layout->kernel_link, "(_end)");
	PM_LAYOUT_TREE_LINE("|   ", "+-- ", "kernel static", layout->kernel_static, "");
	PM_LAYOUT_TREE_LINE("|   ", "+-- ", "kernel heap", layout->kernel_heap, "(k_malloc)");
	PM_LAYOUT_TREE_LINE("|   ", "`-- ", "userspace blob", layout->userspace_blob, "(TLSF pool)");
	pm_metal_layout_report_userspace_usage("|       ", layout);
#else
	PM_LAYOUT_TREE_LINE("", "+-- ", "kernel link", layout->kernel_link, "(_end)");
	PM_LAYOUT_TREE_LINE("|   ", "+-- ", "kernel static", layout->kernel_static, "");
	PM_LAYOUT_TREE_LINE("|   ", "`-- ", "kernel heap", layout->kernel_heap, "(k_malloc)");
	printf("    |\n");
	PM_LAYOUT_TREE_LINE("", "`-- ", "userspace blob", layout->userspace_blob, "(TLSF pool)");
	pm_metal_layout_report_userspace_usage("    ", layout);

	{
		const size_t accounted = layout->kernel_link + layout->userspace_blob;

		if (layout->machine_ram > accounted) {
			const size_t gap = layout->machine_ram - accounted;

			PM_LAYOUT_TREE_LINE("", "    ", "unaccounted", gap, "(outside link+blob)");
		}
	}
#endif

#undef PM_LAYOUT_TREE_LINE
#undef PM_LAYOUT_FMT_SIZE
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
