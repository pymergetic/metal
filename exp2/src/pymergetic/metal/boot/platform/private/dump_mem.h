/*
 * Dump DT MEM partition nodes (lowmem / highmem / heap) to log or console #0.
 */
#ifndef PYMERGETIC_METAL_BOOT_PLATFORM_PRIVATE_DUMP_MEM_H_
#define PYMERGETIC_METAL_BOOT_PLATFORM_PRIVATE_DUMP_MEM_H_

#ifdef __cplusplus
extern "C" {
#endif

/** Walk DT MEM class; print ASCII lines. Safe if log/console not ready. */
void pm_metal_boot_dump_mem(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_PLATFORM_PRIVATE_DUMP_MEM_H_ */
