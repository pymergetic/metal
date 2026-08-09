/*
 * pymergetic.metal.fs.mtar — micro-tar VFS backend.
 */
#ifndef PYMERGETIC_METAL_FS_MTAR_H_
#define PYMERGETIC_METAL_FS_MTAR_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t pm_metal_fs_mtar_open_blob(const uint8_t *blob, size_t len);
uint32_t pm_metal_fs_mtar_open_owned(const uint8_t *blob, size_t len, size_t cap);
int32_t pm_metal_fs_mtar_mount(const uint8_t *target, const uint8_t *blob, size_t len);
int32_t pm_metal_fs_mtar_mount_rw(const uint8_t *target, uint8_t *blob_mut, size_t len, size_t cap);
int32_t pm_metal_fs_mtar_empty(uint8_t *out, size_t out_cap, size_t *out_len);
int32_t pm_metal_fs_mtar_pack_simple(const uint8_t *const *names, const uint8_t *const *datas,
                                     const uint32_t *lens, uint32_t count, uint8_t *out,
                                     size_t out_cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif
