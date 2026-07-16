/*
 * Memory — zephyr arena_budget + pool backing (plat-private).
 */
#include "pymergetic/metal/memory/budget.h"

#include <stdint.h>
#include <string.h>

#include <zephyr/autoconf.h>
#include <zephyr/kernel.h>

#if defined(CONFIG_MMU)
#include <zephyr/kernel/mm.h>

#include "pymergetic/metal/memory/ram.h"
#endif

extern char _end[];

#if !defined(CONFIG_MMU)
static uint8_t g_pm_metal_static_pool[CONFIG_PM_METAL_STATIC_POOL_BYTES]
	__aligned(CONFIG_PM_METAL_ARENA_ALIGN);
static size_t g_pm_metal_static_pool_off;
#endif

static uint32_t pm_metal_memory_zephyr_page_size(void)
{
#if defined(CONFIG_MMU_PAGE_SIZE)
	return CONFIG_MMU_PAGE_SIZE;
#else
	return CONFIG_PM_METAL_ARENA_ALIGN;
#endif
}

static uint64_t pm_metal_memory_zephyr_round_down(uint64_t value, uint64_t align)
{
	if (align == 0) {
		return value;
	}
	return value - (value % align);
}

static uintptr_t pm_metal_memory_zephyr_round_up_ptr(uintptr_t value, uintptr_t align)
{
	return (uintptr_t)pm_metal_memory_zephyr_round_down((uint64_t)value + align - 1U,
							     (uint64_t)align);
}

uint64_t pm_metal_memory_zephyr_link_used(void)
{
	const uintptr_t base = (uintptr_t)CONFIG_SRAM_BASE_ADDRESS;
	const uintptr_t end = pm_metal_memory_zephyr_round_up_ptr((uintptr_t)&_end, 8);

	if (end <= base) {
		return 0U;
	}

	return (uint64_t)(end - base);
}

uint64_t pm_metal_memory_zephyr_arena_budget(void)
{
#if !defined(CONFIG_MMU)
	/* On native_sim the static BSS pool is the real budget, not fake SRAM. */
	return (uint64_t)CONFIG_PM_METAL_STATIC_POOL_BYTES;
#else
	uint64_t machine = pm_metal_memory_ram_ops()->probe();
	uint64_t link = pm_metal_memory_zephyr_link_used();
	uint64_t kernel_heap = (uint64_t)CONFIG_HEAP_MEM_POOL_SIZE;

	if (machine <= link + kernel_heap) {
		return 0U;
	}

	return pm_metal_memory_zephyr_round_down(machine - link - kernel_heap,
						  pm_metal_memory_zephyr_page_size());
#endif
}

static uint64_t g_pm_metal_memory_budget_remaining;
static int g_pm_metal_memory_budget_inited;
#if defined(CONFIG_MMU)
static uintptr_t g_pm_metal_memory_phys_cursor;
#endif

static void pm_metal_memory_zephyr_budget_ensure(void)
{
	if (!g_pm_metal_memory_budget_inited) {
		g_pm_metal_memory_budget_remaining = pm_metal_memory_zephyr_arena_budget();
		g_pm_metal_memory_budget_inited = 1;
#if !defined(CONFIG_MMU)
		g_pm_metal_static_pool_off = 0;
#else
		g_pm_metal_memory_phys_cursor =
			pm_metal_memory_zephyr_round_up_ptr((uintptr_t)&_end, 8);
#endif
	}
}

uint64_t pm_metal_memory_zephyr_budget_take(uint64_t requested)
{
	uint64_t take;

	pm_metal_memory_zephyr_budget_ensure();
	if (requested == 0 || g_pm_metal_memory_budget_remaining == 0) {
		return 0;
	}
	take = requested;
	if (take > g_pm_metal_memory_budget_remaining) {
		take = g_pm_metal_memory_budget_remaining;
	}
	take = pm_metal_memory_zephyr_round_down(take, pm_metal_memory_zephyr_page_size());
	if (take == 0 && g_pm_metal_memory_budget_remaining > 0) {
		take = g_pm_metal_memory_budget_remaining;
	}
	g_pm_metal_memory_budget_remaining -= take;
	return take;
}

void pm_metal_memory_zephyr_budget_reset(void)
{
	g_pm_metal_memory_budget_remaining = 0;
	g_pm_metal_memory_budget_inited = 0;
#if !defined(CONFIG_MMU)
	g_pm_metal_static_pool_off = 0;
#else
	g_pm_metal_memory_phys_cursor = 0;
#endif
}

void *pm_metal_memory_zephyr_pool_alloc(uint64_t bytes)
{
	if (bytes == 0 || bytes > SIZE_MAX) {
		return NULL;
	}

	pm_metal_memory_zephyr_budget_ensure();

#if defined(CONFIG_MMU)
	{
		uintptr_t phys;
		void *virt = NULL;
		size_t need = (size_t)bytes;

		/*
		 * Cursor is a virtual address in the kernel linear map starting
		 * at &_end (unused RAM after the image). k_mem_phys_addr() is
		 * the linear-map formula (virt - vm_base + phys_base), not a
		 * page-table walk of an already-mapped region — so advancing
		 * past the originally mapped image still yields consecutive
		 * physical frames. k_mem_map_phys_bare() then installs a fresh
		 * RW alias for that frame range.
		 *
		 * Exercised by qemu_x86_64 (CONFIG_MMU=y); native_sim uses the
		 * static-pool path below (!CONFIG_MMU).
		 */
		phys = (uintptr_t)k_mem_phys_addr((void *)g_pm_metal_memory_phys_cursor);
		k_mem_map_phys_bare((uint8_t **)&virt, phys, need, K_MEM_PERM_RW);
		if (!virt) {
			return NULL;
		}
		g_pm_metal_memory_phys_cursor =
			pm_metal_memory_zephyr_round_up_ptr(g_pm_metal_memory_phys_cursor + need,
							     pm_metal_memory_zephyr_page_size());
		return virt;
	}
#else
	{
		size_t off = g_pm_metal_static_pool_off;
		size_t need = (size_t)bytes;

		if (off + need > sizeof(g_pm_metal_static_pool)) {
			return NULL;
		}
		g_pm_metal_static_pool_off = off + need;
		return &g_pm_metal_static_pool[off];
	}
#endif
}

void pm_metal_memory_zephyr_pool_free(void *ptr)
{
	(void)ptr;
	/* Static / phys-mapped pools live for the process lifetime. */
}
