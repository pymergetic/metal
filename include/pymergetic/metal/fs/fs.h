/*
 * Metal ESP file load — guest/host dual ABI.
 *
 * Product path is awaitable: size_async / read_async → await → fs_result.
 * Sync size/read remain for host helpers and parked doom until step 6.
 */
#ifndef PYMERGETIC_METAL_FS_H_
#define PYMERGETIC_METAL_FS_H_

#include <stdint.h>

#include "pymergetic/metal/async/async.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_FS_WASI_MODULE "pymergetic.metal.fs"

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_FS_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_FS_WASI_MODULE, name)

/**
 * Awaitable size probe. Await the handle, then pm_metal_fs_result(self).
 * Result is byte length, or 0 if missing/error.
 */
extern pm_metal_async_handle_t pm_metal_fs_size_async(const char *path)
	PM_METAL_FS_IMPORT(pm_metal_fs_size_async);
/**
 * Awaitable read into guest linear buffer at dest.
 * Await, then pm_metal_fs_result(self) → bytes copied (0 on failure).
 */
extern pm_metal_async_handle_t pm_metal_fs_read_async(const char *path,
						      uint32_t dest,
						      uint32_t dest_len)
	PM_METAL_FS_IMPORT(pm_metal_fs_read_async);
/**
 * Awaitable create/truncate write from guest buffer at src.
 * Await, then pm_metal_fs_result(self) → bytes written (0 on failure).
 */
extern pm_metal_async_handle_t pm_metal_fs_write_async(const char *path,
						       uint32_t src,
						       uint32_t src_len)
	PM_METAL_FS_IMPORT(pm_metal_fs_write_async);
/** After await resumes on self: u32 payload from the completed FS op. */
extern uint32_t pm_metal_fs_result(pm_metal_async_handle_t self_h)
	PM_METAL_FS_IMPORT(pm_metal_fs_result);

/** Transitional sync (parked doom). Prefer *_async. */
extern uint32_t pm_metal_fs_size(const char *path)
	PM_METAL_FS_IMPORT(pm_metal_fs_size);
extern uint32_t pm_metal_fs_read(const char *path, uint32_t dest,
				 uint32_t dest_len)
	PM_METAL_FS_IMPORT(pm_metal_fs_read);
#else
pm_metal_async_handle_t pm_metal_fs_size_async(const char *path);
pm_metal_async_handle_t pm_metal_fs_read_async(const char *path, uint32_t dest,
					       uint32_t dest_len);
pm_metal_async_handle_t pm_metal_fs_write_async(const char *path, uint32_t src,
						uint32_t src_len);
uint32_t pm_metal_fs_result(pm_metal_async_handle_t self_h);

uint32_t pm_metal_fs_size(const char *path);
uint32_t pm_metal_fs_read(const char *path, uint32_t dest, uint32_t dest_len);
uint32_t pm_metal_fs_write(const char *path, uint32_t src, uint32_t src_len);
void pm_metal_fs_bind_inst(void *module_inst);
int pm_metal_fs_native_register(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_FS_H_ */
