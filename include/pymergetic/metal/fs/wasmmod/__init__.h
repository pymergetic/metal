/*
 * pymergetic.metal.fs.wasmmod — MPWP RO pack mount.
 */
#ifndef PYMERGETIC_METAL_FS_WASMMOD_H_
#define PYMERGETIC_METAL_FS_WASMMOD_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t pm_metal_fs_wasmmod_mount_mpwp(const uint8_t *target, const uint8_t *mpwp, size_t mpwp_len);

#ifdef __cplusplus
}
#endif

#endif
