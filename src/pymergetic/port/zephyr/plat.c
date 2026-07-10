/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr memory probe — multiboot E820, EFI handoff, or DT (fake metal).
 */

#include "../../platform/plat.h"
#include "../headers/efi_ram.h"
#include "traits.h"

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_X86) && !PM_METAL_PORT_IS_FAKE_METAL && defined(CONFIG_X86_MEMMAP)
#include <zephyr/arch/x86/memmap.h>
#endif

extern char _end[];

#if defined(CONFIG_MMU_PAGE_SIZE)
#define PM_PLAT_PAGE_SIZE CONFIG_MMU_PAGE_SIZE
#else
#define PM_PLAT_PAGE_SIZE 4096
#endif

static pm_plat_ram_source_t pm_plat_ram_source;

const char *pm_plat_machine_ram_source_name(pm_plat_ram_source_t source)
{
	switch (source) {
	case PM_PLAT_RAM_SOURCE_DEVICETREE:
		return "devicetree";
	case PM_PLAT_RAM_SOURCE_MULTIBOOT_E820:
		return "multiboot E820";
	case PM_PLAT_RAM_SOURCE_EFI_MEMMAP:
#if defined(CONFIG_X86_EFI)
		switch (pm_port_efi_ram_source()) {
		case PM_PORT_EFI_RAM_SOURCE_MEMMAP:
			return "EFI memory map";
		case PM_PORT_EFI_RAM_SOURCE_SMBIOS:
			return "EFI SMBIOS";
		default:
			return "EFI firmware";
		}
#else
		return "EFI firmware";
#endif
	default:
		return "unknown";
	}
}

pm_plat_ram_source_t pm_plat_machine_ram_source(void)
{
	return pm_plat_ram_source;
}

#if defined(CONFIG_X86) && !PM_METAL_PORT_IS_FAKE_METAL && defined(CONFIG_X86_MEMMAP)

static bool pm_plat_x86_memmap_usable(void)
{
	return x86_memmap_source >= X86_MEMMAP_SOURCE_MULTIBOOT_MEM;
}

static size_t pm_plat_x86_memmap_ram_total(void)
{
	size_t total = 0U;

	for (int i = 0; i < CONFIG_X86_MEMMAP_ENTRIES; i++) {
		if (x86_memmap[i].type == X86_MEMMAP_ENTRY_RAM) {
			total += (size_t)x86_memmap[i].length;
		}
	}

	return total;
}

#endif

size_t pm_plat_machine_ram(void)
{
#if PM_METAL_PORT_IS_FAKE_METAL
	pm_plat_ram_source = PM_PLAT_RAM_SOURCE_DEVICETREE;
	return DT_REG_SIZE(DT_CHOSEN(zephyr_sram));

#elif defined(CONFIG_X86_EFI)
	{
		const size_t efi_ram = pm_port_efi_machine_ram();

		if (efi_ram > 0U) {
			pm_plat_ram_source = PM_PLAT_RAM_SOURCE_EFI_MEMMAP;
			return efi_ram;
		}
	}
#endif

#if defined(CONFIG_X86) && !PM_METAL_PORT_IS_FAKE_METAL && defined(CONFIG_X86_MEMMAP)
	if (pm_plat_x86_memmap_usable()) {
		const size_t total = pm_plat_x86_memmap_ram_total();

		if (total > 0U) {
			if (x86_memmap_source >= X86_MEMMAP_SOURCE_MULTIBOOT_MMAP) {
				pm_plat_ram_source = PM_PLAT_RAM_SOURCE_MULTIBOOT_E820;
			}
			return total;
		}
	}
#endif

	pm_plat_ram_source = PM_PLAT_RAM_SOURCE_DEVICETREE;
	return DT_REG_SIZE(DT_CHOSEN(zephyr_sram));
}

size_t pm_plat_link_used(void)
{
	const uintptr_t base = (uintptr_t)CONFIG_SRAM_BASE_ADDRESS;
	const uintptr_t end = ROUND_UP((uintptr_t)&_end, 8);

	if (end <= base) {
		return 0U;
	}

	return (size_t)(end - base);
}

size_t pm_plat_arena_budget(void)
{
	const size_t machine = pm_plat_machine_ram();
	const size_t link = pm_plat_link_used();

	if (machine <= link) {
		return 0U;
	}

	return ROUND_DOWN(machine - link, PM_PLAT_PAGE_SIZE);
}

size_t pm_plat_link_window(void)
{
	return DT_REG_SIZE(DT_CHOSEN(zephyr_sram));
}
