/*
 * pymergetic.metal.fs.fat — in-memory FAT16/32 (RS callee).
 */
#ifndef PYMERGETIC_METAL_FS_FAT_H_
#define PYMERGETIC_METAL_FS_FAT_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t pm_metal_fs_fat_format_buf(uint8_t *buf, size_t len);
uint32_t pm_metal_fs_fat_open_buf(uint8_t *buf, size_t len);
int32_t pm_metal_fs_fat_close(uint32_t vol);
int32_t pm_metal_fs_fat_mount(const uint8_t *target, uint8_t *buf, size_t len);
int32_t pm_metal_fs_fat_seed_simple(uint8_t *buf, size_t len, const uint8_t *const *names,
                                    const uint8_t *const *datas, const uint32_t *lens,
                                    uint32_t count);
int32_t pm_metal_fs_fat_mount_ram(const uint8_t *target, uint32_t ram_h);

#ifdef __cplusplus
}
#endif

#endif
