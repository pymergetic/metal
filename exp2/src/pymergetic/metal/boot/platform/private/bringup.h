/*
 * Shared post-floor bring-up (BIOS + EFI). Not a public module face.
 * Target main: ingest firmware map, then call this.
 */
#ifndef PYMERGETIC_METAL_BOOT_PLATFORM_PRIVATE_BRINGUP_H_
#define PYMERGETIC_METAL_BOOT_PLATFORM_PRIVATE_BRINGUP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Claim heap, console #0, DT mem+uart seed, serial viewport, handoff, mem smoke.
 * Requires mem_map already ingested. Returns 0 on success, negative on failure
 * (failure text may already be on console #0 if it was up).
 */
int32_t pm_metal_boot_bringup(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_PLATFORM_PRIVATE_BRINGUP_H_ */
