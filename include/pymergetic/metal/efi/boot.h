/*
 * Boot harvest + seeded init into the async runner pool.
 */
#ifndef PYMERGETIC_METAL_BOOT_H_
#define PYMERGETIC_METAL_BOOT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

/**
 * Create and spawn the first init task (gfx/UI/wasm/shell/pump).
 * Call after runners are about to run (or already waiting on release).
 */
int pm_metal_boot_seed_init(void);

/** Probe virtio + register baseline DT nodes (pre-EBS harvest). */
int pm_metal_boot_harvest_devices(void);

/**
 * Print the floor boot tree (mem + cpu + devices + runners).
 * Call once after harvest + run_init + gfx harvest.
 */
void pm_metal_boot_print_floor_tree(
	uint64_t claim_mib,
	uint64_t map_bytes,
	uint64_t hole_mib,
	uint64_t heap_bytes,
	uint64_t stack_kib,
	unsigned n_cpus
	);

/** Manual bring-up suite (wasm proofs). Safe post-EBS. Returns 0 on success. */
int pm_metal_boot_run_tests(void);

/**
 * Reverse of init: stop shell work, fini wasm → ui → gfx, then stop runners.
 * Call from the init task when shell requests exit; run_enter returns after.
 */
void pm_metal_boot_shutdown(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_H_ */
