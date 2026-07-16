#ifndef PYMERGETIC_METAL_MOUNT_PROC_MOUNTS_H_
#define PYMERGETIC_METAL_MOUNT_PROC_MOUNTS_H_

#include <stddef.h>

/* impl: common — src/common/pymergetic/metal/mount/proc/mounts.c */
int pm_metal_mount_proc_generate_mounts(char *out, size_t cap, size_t *out_len);

#endif /* PYMERGETIC_METAL_MOUNT_PROC_MOUNTS_H_ */
