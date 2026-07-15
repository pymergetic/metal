/*
 * Shared helpers for Metal /proc node generators.
 */
#ifndef PYMERGETIC_METAL_MOUNT_PROC_UTIL_H_
#define PYMERGETIC_METAL_MOUNT_PROC_UTIL_H_

#include <stddef.h>

int pm_metal_mount_proc_put_str(char *out, size_t cap, size_t *out_len, const char *s);

int pm_metal_mount_proc_put_nul_list(char *out, size_t cap, size_t *out_len, int count,
				     const char *const *items);

#endif /* PYMERGETIC_METAL_MOUNT_PROC_UTIL_H_ */
