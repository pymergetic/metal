#ifndef PYMERGETIC_METAL_MOUNT_PROC_MEMINFO_H_
#define PYMERGETIC_METAL_MOUNT_PROC_MEMINFO_H_

#include <stddef.h>

int pm_metal_mount_proc_generate_meminfo(char *out, size_t cap, size_t *out_len);

#endif /* PYMERGETIC_METAL_MOUNT_PROC_MEMINFO_H_ */
