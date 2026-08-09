/*
 * pymergetic.metal.fs.littlefs — LittleFS mount (RS + vendor C).
 */
#ifndef PYMERGETIC_METAL_FS_LITTLEFS_H_
#define PYMERGETIC_METAL_FS_LITTLEFS_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t pm_metal_fs_littlefs_mount(const uint8_t *target, uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif
