/*
 * SPDX-License-Identifier: Apache-2.0
 * Zephyr firmware RAM probe — multiboot E820 on qemu_x86_64, CONFIG_SRAM on native_sim.
 */
#include <pymergetic/metal/port/plat.h>

#include <zephyr/autoconf.h>
#include <zephyr/kernel.h>

#include <stdlib.h>

#if defined(CONFIG_MMU)
#include <zephyr/kernel/mm.h>
#endif

#if defined(CONFIG_X86) && defined(CONFIG_X86_MEMMAP)
#include <zephyr/arch/x86/memmap.h>
#endif

extern char _end[];

static inline uint32_t pm_plat_page_size(void)
{
#if defined(CONFIG_MMU_PAGE_SIZE)
	return CONFIG_MMU_PAGE_SIZE;
#else
	return CONFIG_PM_METAL_ARENA_ALIGN;
#endif
}

static inline uint64_t pm_plat_round_down_u64(uint64_t value, uint64_t align)
{
	return value - (value % align);
}

static inline uintptr_t pm_plat_round_up(uintptr_t value, uintptr_t align)
{
	return (uintptr_t)pm_plat_round_down_u64((uint64_t)value + align - 1U, (uint64_t)align);
}

pm_metal_port_target_id_t pm_metal_port_target_id(void)
{
	return PM_METAL_PORT_TARGET_ZEPHYR;
}

#if defined(CONFIG_X86) && defined(CONFIG_X86_MEMMAP)

static uint64_t pm_metal_port_x86_memmap_ram_total(void)
{
	uint64_t total = 0;

	for (int i = 0; i < CONFIG_X86_MEMMAP_ENTRIES; i++) {
		if (x86_memmap[i].type == X86_MEMMAP_ENTRY_RAM) {
			total += (uint64_t)x86_memmap[i].length;
		}
	}

	return total;
}

#endif

uint64_t pm_metal_port_machine_ram(void)
{
#if defined(CONFIG_X86) && defined(CONFIG_X86_MEMMAP)
	if (x86_memmap_source >= X86_MEMMAP_SOURCE_MULTIBOOT_MEM) {
		const uint64_t total = pm_metal_port_x86_memmap_ram_total();

		if (total > 0U) {
			return total;
		}
	}
#endif

	return (uint64_t)CONFIG_SRAM_SIZE * 1024ULL;
}

uint64_t pm_metal_port_link_used(void)
{
	const uintptr_t base = (uintptr_t)CONFIG_SRAM_BASE_ADDRESS;
	const uintptr_t end = pm_plat_round_up((uintptr_t)&_end, 8);

	if (end <= base) {
		return 0U;
	}

	return (uint64_t)(end - base);
}

uint64_t pm_metal_port_arena_budget(void)
{
	uint64_t machine = pm_metal_port_machine_ram();
	uint64_t link = pm_metal_port_link_used();

	if (machine <= link) {
		return 0U;
	}

	return pm_plat_round_down_u64(machine - link, pm_plat_page_size());
}

#define PM_METAL_WAMR_POOL_MIN_BYTES 65536U

#if IS_ENABLED(CONFIG_PM_METAL_RUN_WASM_MODS)
static uint8_t g_wamr_pool_buf[CONFIG_PM_METAL_WAMR_POOL_BYTES];
#endif

static void *g_wamr_pool_mapped;
static size_t g_wamr_pool_size;

int pm_metal_port_wamr_pool_establish(uint8_t **out_buf, size_t *out_size)
{
	size_t pool_size;

	if (out_buf == NULL || out_size == NULL) {
		return -EINVAL;
	}

	if (g_wamr_pool_mapped != NULL) {
		*out_buf = (uint8_t *)g_wamr_pool_mapped;
		*out_size = g_wamr_pool_size;
		return 0;
	}

	pool_size = (size_t)pm_metal_port_arena_budget();
#if IS_ENABLED(CONFIG_PM_METAL_RUN_WASM_MODS)
	if (pool_size > (size_t)CONFIG_PM_METAL_WAMR_POOL_BYTES) {
		pool_size = (size_t)CONFIG_PM_METAL_WAMR_POOL_BYTES;
	}
#endif
	if (pool_size < PM_METAL_WAMR_POOL_MIN_BYTES) {
		return -ENOMEM;
	}

#if defined(CONFIG_MMU)
	{
		const uintptr_t link_end = pm_plat_round_up((uintptr_t)&_end, 8);
		uintptr_t phys;
		void *virt = NULL;

		phys = (uintptr_t)k_mem_phys_addr((void *)link_end);
		k_mem_map_phys_bare((uint8_t **)&virt, phys, pool_size, K_MEM_PERM_RW);
		if (virt == NULL) {
			return -ENOMEM;
		}

		g_wamr_pool_mapped = virt;
		g_wamr_pool_size = pool_size;
	}
#else
	{
#if IS_ENABLED(CONFIG_PM_METAL_RUN_WASM_MODS)
		if (pool_size > sizeof(g_wamr_pool_buf)) {
			pool_size = sizeof(g_wamr_pool_buf);
		}
		g_wamr_pool_mapped = g_wamr_pool_buf;
		g_wamr_pool_size = pool_size;
#else
		void *pool = malloc(pool_size);

		if (pool == NULL) {
			return -ENOMEM;
		}

		g_wamr_pool_mapped = pool;
		g_wamr_pool_size = pool_size;
#endif
	}
#endif

	*out_buf = (uint8_t *)g_wamr_pool_mapped;
	*out_size = g_wamr_pool_size;
	return 0;
}
