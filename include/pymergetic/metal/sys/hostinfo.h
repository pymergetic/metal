/*
 * Engine-only: publish bootstrap blob for guest /sys/pm (one-time handoff).
 * Pair of metal/sys/guestinfo.h (orchestrator load).
 */
#ifndef PYMERGETIC_METAL_SYS_HOSTINFO_H_
#define PYMERGETIC_METAL_SYS_HOSTINFO_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Write blob under vfs_root/bootstrap (guest sees /sys/pm/bootstrap when preopened). */
int pm_metal_sys_hostinfo_publish(const char *vfs_root, const void *blob, size_t blob_len);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_SYS_HOSTINFO_H_ */
