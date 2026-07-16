/*
 * tmpfs name registry — "has named tmpfs source X already been
 * establish()'d this boot" bookkeeping, shared by every target's own
 * tmpfs.c. See docs/MOUNT.md "Named ramdisks": keyed by *name* (the tmpfs
 * fstype's own <source> column), deliberately separate from mount.c's own
 * table (keyed by guest target path) — the same named tmpfs can legitimately
 * back more than one mount point at once (e.g. mounted rw at one path for
 * setup, ro at another for guests — read-only not yet enforced, but the
 * *addressing* already supports it). impl: common — pure bookkeeping, no OS
 * calls of its own; each target's own tmpfs.c does the real establish()/
 * teardown and only asks this registry "does this name exist yet" / "am I
 * the last reference".
 */
#ifndef PYMERGETIC_METAL_MOUNT_TMPFS_REGISTRY_H_
#define PYMERGETIC_METAL_MOUNT_TMPFS_REGISTRY_H_

#include <stddef.h>

/* impl: common — src/common/pymergetic/metal/mount/tmpfs_registry.c
 *
 * Look up `name`; on a hit, copies its already-established host path into
 * out_host_path (out_cap bytes) and increments its refcount — this call
 * itself counts as a new reference (the caller's own upcoming mount-table
 * entry). Returns 0/-1 (not registered yet, or out_host_path too small).
 */
int pm_metal_mount_tmpfs_registry_acquire(const char *name, char *out_host_path, size_t out_cap);

/*
 * Registers a brand new name -> host_path mapping with an initial refcount
 * of 1 (the caller's own reference, from the establish() that just created
 * host_path). Caller must have already confirmed via acquire() above that
 * `name` isn't registered yet — this does not check. Returns 0/-1 (registry
 * full, or name/host_path too long).
 * impl: common — src/common/pymergetic/metal/mount/tmpfs_registry.c
 */
int pm_metal_mount_tmpfs_registry_insert(const char *name, const char *host_path);

/*
 * Drops one reference from whichever name currently maps to host_path
 * (exact string match — the same one establish() wrote/acquire() returned).
 * Returns 1 if that was the last reference (entry removed from the
 * registry — caller must now actually tear host_path down itself), 0 if
 * other references remain (caller must leave host_path alone), -1 if
 * host_path isn't registered at all (caller bug — logged by the caller,
 * not here).
 * impl: common — src/common/pymergetic/metal/mount/tmpfs_registry.c
 */
int pm_metal_mount_tmpfs_registry_release(const char *host_path);

#endif /* PYMERGETIC_METAL_MOUNT_TMPFS_REGISTRY_H_ */
