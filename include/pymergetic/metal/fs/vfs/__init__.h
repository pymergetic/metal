/*
 * pymergetic.metal.fs.vfs — mount table / path resolve.
 */
#ifndef PYMERGETIC_METAL_FS_VFS_H_
#define PYMERGETIC_METAL_FS_VFS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t pm_metal_fs_vfs_mount_h;

typedef struct pm_metal_fs_vfs_resolve {
    const void *ops;
    void *ctx;
    const uint8_t *rel;
    pm_metal_fs_vfs_mount_h mount;
} pm_metal_fs_vfs_resolve_t;

pm_metal_fs_vfs_mount_h pm_metal_fs_vfs_mount(const uint8_t *target, const void *ops, void *ctx);
int32_t pm_metal_fs_vfs_umount(const uint8_t *target);
uint32_t pm_metal_fs_vfs_mount_count(void);
int32_t pm_metal_fs_vfs_mount_info(uint32_t index, uint8_t *target_out, uint32_t target_cap,
                                   uint8_t *fstype_out, uint32_t fstype_cap);
int32_t pm_metal_fs_vfs_mount_get(uint32_t index, const void **ops_out, void **ctx_out);
int32_t pm_metal_fs_vfs_resolve(const uint8_t *path, pm_metal_fs_vfs_resolve_t *out);

#ifdef __cplusplus
}
#endif

#endif
