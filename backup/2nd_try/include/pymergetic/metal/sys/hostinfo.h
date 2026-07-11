/*
 * Bootstrap handoff via /sys/pm — lower publish (kernel), wasm mod load (public).
 *
 * Lower implementations (symmetric filenames per host tree):
 *   host/linux/pymergetic/metal/sys/hostinfo.c  — Linux VFS publish (firmware)
 *   host/zephyr/pymergetic/metal/sys/hostinfo.c — Zephyr VFS publish (firmware)
 *   src/pymergetic/metal/sys/hostinfo.c         — wasm mod load (pm_metal_sys_hostinfo_load)
 */
#ifndef PYMERGETIC_METAL_SYS_HOSTINFO_H_
#define PYMERGETIC_METAL_SYS_HOSTINFO_H_

#include <pymergetic/metal/sys/sys.h>
#include <pymergetic/metal/export.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Wasm mod — read and validate PM_METAL_SYS_BOOTSTRAP_PATH into out.
 * Defined: src/pymergetic/metal/sys/hostinfo.c
 */
PM_METAL_API(int, pm_metal_sys_hostinfo_load, (pm_metal_sys_bootstrap_t *out));

/* Lower — write blob to vfs_root/bootstrap (vfs_root is PM_METAL_SYS_HANDOFF_VFS_ROOT).
 * Defined: host/linux/pymergetic/metal/sys/hostinfo.c
 *          host/zephyr/pymergetic/metal/sys/hostinfo.c
 */
PM_METAL_KERNEL_API(int, pm_metal_sys_hostinfo_publish,
		    (const char *vfs_root, const void *blob, size_t blob_len));

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_SYS_HOSTINFO_H_ */
