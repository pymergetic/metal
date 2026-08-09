/*
 * pymergetic.metal.fs.overlay — lower/upper overlay mount.
 */
#ifndef PYMERGETIC_METAL_FS_OVERLAY_H_
#define PYMERGETIC_METAL_FS_OVERLAY_H_

#include <stdint.h>
#include <pymergetic/metal/fs/__init__.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t pm_metal_fs_overlay_mount(const uint8_t *target, const pm_metal_fs_ops_t *lower_ops,
                                  void *lower_ctx, const pm_metal_fs_ops_t *upper_ops,
                                  void *upper_ctx);

#ifdef __cplusplus
}
#endif

#endif
