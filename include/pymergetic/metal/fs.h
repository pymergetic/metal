#ifndef PM_METAL_FS_H_
#define PM_METAL_FS_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Async-shaped C ABI (Rust callee). Handles are async; use
 * pm_metal_async_result_u32 + pm_metal_async_coro_close. */

typedef uint32_t pm_metal_fs_h;
#define PM_METAL_FS_INVALID 0xffffffffu

#define PM_METAL_FS_O_RDONLY 1u
#define PM_METAL_FS_O_WRONLY 2u
#define PM_METAL_FS_O_RDWR 3u
#define PM_METAL_FS_O_CREAT 4u
#define PM_METAL_FS_O_TRUNC 8u
#define PM_METAL_FS_O_APPEND 16u
#define PM_METAL_FS_O_DIRECTORY 32u

uint32_t pm_metal_fs_open_async(const uint8_t *path, uint32_t flags);
uint32_t pm_metal_fs_close_async(pm_metal_fs_h h);
uint32_t pm_metal_fs_fread_async(pm_metal_fs_h h, uint8_t *dest, uint32_t len);
uint32_t pm_metal_fs_fwrite_async(pm_metal_fs_h h, const uint8_t *src, uint32_t len);
uint32_t pm_metal_fs_mkdir_async(const uint8_t *path);
uint32_t pm_metal_fs_read_async(const uint8_t *path, uint8_t *dest, uint32_t dest_len);
uint32_t pm_metal_fs_write_async(const uint8_t *path, const uint8_t *src, uint32_t src_len);

/* tmpfs backend */
int32_t pm_metal_fs_tmpfs_mount(const uint8_t *target);

/* wasmmod MPWP RO — target NULL → /mods/<pack.name> */
int32_t pm_metal_fs_wasmmod_mount_mpwp(const uint8_t *target, const uint8_t *mpwp,
                                       size_t mpwp_len);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_FS_H_ */
