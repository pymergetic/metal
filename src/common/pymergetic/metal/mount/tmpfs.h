/*
 * tmpfs — named, in-RAM scratch fstype. See docs/MOUNT.md "fstype vs.
 * source" / "Named ramdisks" for the full design. Unlike hostdir, this one
 * genuinely differs per target (linux: mkdtemp() under a real /dev/shm,
 * registered afterward as an ordinary hostdir; zephyr: composes
 * mount/device.h's ramdisk + fs_mount(), not yet implemented — see
 * docs/MOUNT.md "Zephyr prerequisite") — `impl: bind`, one .c per target,
 * unlike hostdir.c's single shared impl.
 */
#ifndef PYMERGETIC_METAL_MOUNT_TMPFS_H_
#define PYMERGETIC_METAL_MOUNT_TMPFS_H_

#include "pymergetic/metal/mount/ops.h"

/* impl: bind — src/linux/pymergetic/metal/mount/tmpfs.c
 *              src/zephyr/pymergetic/metal/mount/tmpfs.c (stub — see above) */
const pm_metal_mount_ops_t *pm_metal_mount_tmpfs_ops(void);

#endif /* PYMERGETIC_METAL_MOUNT_TMPFS_H_ */
