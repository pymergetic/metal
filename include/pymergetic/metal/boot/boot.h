/*
 * Boot harvest + seeded init (port-neutral API).
 *
 * impl: bios — src/bios/pymergetic/metal/boot/bios/boot_init.c
 * impl: efi  — src/efi/pymergetic/metal/boot/efi/boot_init.c
 * impl: common — src/pymergetic/metal/boot/banner.c (boot/dead art)
 */
#ifndef PYMERGETIC_METAL_BOOT_BOOT_H_
#define PYMERGETIC_METAL_BOOT_BOOT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

/**
 * Create and spawn the first init task (gfx/UI/wasm/shell/pump).
 * Call after runners are about to run (or already waiting on release).
 * impl: efi — src/efi/pymergetic/metal/boot/efi/boot_init.c
 */
int pm_metal_boot_seed_init(void);

/**
 * Probe virtio + register baseline DT nodes (pre-EBS harvest).
 * impl: efi — src/efi/pymergetic/metal/boot/efi/boot_init.c
 */
int pm_metal_boot_harvest_devices(void);

/**
 * Print the floor boot tree (mem + cpu + devices + runners).
 * Call once after harvest + run_init + gfx harvest.
 * impl: efi — src/efi/pymergetic/metal/boot/efi/boot_init.c
 */
void pm_metal_boot_print_floor_tree(uint64_t claim_mib, uint64_t map_bytes,
				    uint64_t hole_mib, uint64_t heap_bytes,
				    uint64_t stack_kib, unsigned n_cpus);

/**
 * Manual bring-up suite (wasm proofs). Safe post-EBS. Returns 0 on success.
 * impl: efi — src/efi/pymergetic/metal/boot/efi/boot_init.c
 */
int pm_metal_boot_run_tests(void);

/**
 * Reverse of init: stop shell work, then wasm → ui → gfx, countdown, reset.
 * reboot: 0 power-off, non-zero restart. Does not return on success.
 * impl: efi — src/efi/pymergetic/metal/boot/efi/boot_init.c
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
