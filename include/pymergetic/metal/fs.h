/*
 * Metal ESP file load — guest/host dual ABI.
 *
 * Prefer size + read-into-guest-buffer so wasi-libc owns the heap
 * (WAMR module_malloc heap stays small for async coro state only).
 */
#ifndef PYMERGETIC_METAL_FS_H_
#define PYMERGETIC_METAL_FS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_FS_WASI_MODULE "pymergetic.metal.fs"

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_FS_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_FS_WASI_MODULE, name)

/** ESP-relative path size in bytes (0 if missing). */
extern uint32_t pm_metal_fs_size(const char *path)
	PM_METAL_FS_IMPORT(pm_metal_fs_size);
/**
 * Read file into guest linear buffer at dest (offset).
 * Returns bytes copied, or 0 on failure.
 */
extern uint32_t pm_metal_fs_read(const char *path, uint32_t dest,
				 uint32_t dest_len)
	PM_METAL_FS_IMPORT(pm_metal_fs_read);
#else
uint32_t pm_metal_fs_size(const char *path);
uint32_t pm_metal_fs_read(const char *path, uint32_t dest, uint32_t dest_len);
void pm_metal_fs_bind_inst(void *module_inst);
int pm_metal_fs_native_register(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_FS_H_ */
