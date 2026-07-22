/*
 * Boot harvest + seeded init (port-neutral API).
 *
 * impl: common — src/pymergetic/metal/boot/boot_init.c (seed/tree/tests/shutdown)
 * impl: common — src/pymergetic/metal/boot/boot_harvest.c (floor + bus probes)
 * impl: common — src/pymergetic/metal/boot/banner.c (boot/dead art)
 * impl: bind   — src/{bios,efi}/…/boot_init.c (port_floor + port_seed only)
 */
#ifndef PYMERGETIC_METAL_BOOT_BOOT_H_
#define PYMERGETIC_METAL_BOOT_BOOT_H_

#include <stdint.h>

#include "pymergetic/metal/runtime/async/async.h"

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

/**
 * Create and spawn the first init task (gfx/UI/wasm/shell/pump).
 * Call after runners are about to run (or already waiting on release).
 * impl: common — src/pymergetic/metal/boot/boot_init.c
 */
int pm_metal_boot_seed_init(void);

/**
 * Platform handoff marker logged just before driver start / init spawn.
 * impl: bind — bios/efi boot_init.c
 */
void pm_metal_boot_port_seed(void);

/**
 * Platform floor deltas for shared harvest (fs/random compat + early input).
 * impl: bind — bios/efi boot_init.c
 */
void pm_metal_boot_port_floor(const char **fs_compat,
			      const char **random_compat);

/**
 * Register baseline DT nodes + bus probes (pre-EBS harvest).
 * impl: common — src/pymergetic/metal/boot/boot_harvest.c
 */
int pm_metal_boot_harvest_devices(void);

/**
 * Shared PCI/ISA bus probes (net/audio/console/blk) after platform DT floor.
 * impl: common — src/pymergetic/metal/boot/boot_harvest.c
 */
void pm_metal_boot_harvest_bus_devices(void);

/**
 * Print the floor boot tree (mem + cpu + devices + runners).
 * Call once after harvest + run_init + gfx harvest.
 * impl: common — src/pymergetic/metal/boot/boot_init.c
 */
void pm_metal_boot_print_floor_tree(uint64_t claim_mib, uint64_t map_bytes,
				    uint64_t hole_mib, uint64_t heap_bytes,
				    uint64_t stack_kib, unsigned n_cpus);

/**
 * Start bring-up suite as an async coro (DHCP wait + wasm proofs).
 * Await the handle; then pm_metal_boot_tests_result().
 * impl: common — src/pymergetic/metal/boot/boot_init.c
 */
pm_metal_async_handle_t pm_metal_boot_tests_start(void);
/** After await on tests_start: 0 ok, -1 failed. */
int pm_metal_boot_tests_result(pm_metal_async_handle_t h);

/**
 * Reverse of init: stop shell work, then wasm → ui → gfx, countdown, reset.
 * reboot: 0 power-off, non-zero restart. Does not return on success.
 * impl: common — src/pymergetic/metal/boot/boot_init.c
 */
void pm_metal_boot_shutdown(int reboot);

/**
 * Startup METAL ASCII banner (log only). Printed at the head of the floor tree.
 * impl: common — src/pymergetic/metal/boot/banner.c
 */
void pm_metal_boot_banner(void);

/**
 * Big "DEAD" banner after the shutdown countdown; holds briefly so it is
 * visible before fini/reset (longer when reboot != 0).
 * impl: common — src/pymergetic/metal/boot/banner.c
 */
void pm_metal_boot_dead(int reboot);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_BOOT_H_ */
