/*
 * hostdir — passthrough of a real, already-existing host directory.
 * `source` must already be an absolute, resolved host path; establish()
 * itself does no realpath()/existence probing of its own (that stays a
 * caller concern — e.g. main.c's own realpath() on --rootfs=/--vfs-root=
 * today, fstab.c's later — since neither is available/meaningful on
 * every target the same way, while this file's own copy-the-string logic
 * is). Trivial enough that one impl covers every target — no per-target
 * hostdir.c needed, unlike tmpfs (see docs/MOUNT.md "fstype vs. source").
 */
#ifndef PYMERGETIC_METAL_MOUNT_HOSTDIR_H_
#define PYMERGETIC_METAL_MOUNT_HOSTDIR_H_

#include "pymergetic/metal/mount/ops.h"

/* impl: common — src/common/pymergetic/metal/mount/hostdir.c */
const pm_metal_mount_ops_t *pm_metal_mount_hostdir_ops(void);

#endif /* PYMERGETIC_METAL_MOUNT_HOSTDIR_H_ */
