/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Portable floor for memory — implemented per OS in port/{zephyr,linux}/plat.c.
 * Metal includes this header only; no Zephyr or POSIX headers here.
 */

#ifndef PM_PLATFORM_PLAT_H_
#define PM_PLATFORM_PLAT_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum pm_plat_ram_source {
	PM_PLAT_RAM_SOURCE_UNKNOWN = 0,
	PM_PLAT_RAM_SOURCE_DEVICETREE,
	PM_PLAT_RAM_SOURCE_MULTIBOOT_E820,
	PM_PLAT_RAM_SOURCE_EFI_MEMMAP,
} pm_plat_ram_source_t;

/* Installed machine RAM (probe). Never the link-window DT cap on dynamic x86. */
size_t pm_plat_machine_ram(void);
pm_plat_ram_source_t pm_plat_machine_ram_source(void);
const char *pm_plat_machine_ram_source_name(pm_plat_ram_source_t source);

/* Bytes from RAM base to _end (kernel link image). */
size_t pm_plat_link_used(void);

/* Userspace arena budget: machine_ram - link_used (page-rounded by port). */
size_t pm_plat_arena_budget(void);

/* Kernel link map window (DT / CONFIG_SRAM_SIZE) — not machine RAM. */
size_t pm_plat_link_window(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_PLATFORM_PLAT_H_ */
