#ifndef PYMERGETIC_METAL_MOUNT_PROC_CMDLINE_H_
#define PYMERGETIC_METAL_MOUNT_PROC_CMDLINE_H_

#include <stddef.h>

/* impl: common — src/common/pymergetic/metal/mount/proc/cmdline.c */
int pm_metal_mount_proc_generate_cmdline(char *out, size_t cap, size_t *out_len);

#endif /* PYMERGETIC_METAL_MOUNT_PROC_CMDLINE_H_ */
