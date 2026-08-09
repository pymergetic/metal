/*
 * pymergetic.metal.fs.tmpfs — memory-backed mount.
 */
#ifndef PYMERGETIC_METAL_FS_TMPFS_H_
#define PYMERGETIC_METAL_FS_TMPFS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t pm_metal_fs_tmpfs_mount(const uint8_t *target);

#ifdef __cplusplus
}
#endif

#endif
