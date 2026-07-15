/*
 * Mount ops-struct layout — one shared shape for every mount fstype
 * (hostdir passthrough now; tmpfs later — see mount/hostdir.h and
 * docs/MOUNT.md). Mirrors memory/ops.h's own pattern: a struct of
 * function pointers per "kind", plus a resolve()-by-enum dispatcher for
 * callers that pick a kind at runtime (mount.c's mount(), fstab.c's
 * applier) rather than at compile time.
 */
#ifndef PYMERGETIC_METAL_MOUNT_OPS_H_
#define PYMERGETIC_METAL_MOUNT_OPS_H_

#include <stddef.h>

typedef enum pm_metal_mount_kind {
	PM_METAL_MOUNT_HOSTDIR = 0,
	PM_METAL_MOUNT_TMPFS,
	PM_METAL_MOUNT_PROC,
	PM_METAL_MOUNT_KIND_COUNT,
} pm_metal_mount_kind_t;

typedef struct pm_metal_mount_ops {
	/*
	 * Establish (or attach to an already-established) backing for this
	 * kind, and write the real host directory path it resolves to into
	 * out_host_path (out_cap bytes) — the directory this mount's WASI
	 * preopen will point at. `source`'s meaning is kind-specific
	 * (HOSTDIR: the host path itself, already resolved by the caller —
	 * see mount/hostdir.h). `opts` is this mount/fstab line's raw
	 * options string, kind-specific tokens only (mount.c itself strips
	 * the generic "ro"/"rw" tokens before anything reaches here — see
	 * mount.h); may be NULL. Returns 0/-1.
	 */
	int (*establish)(const char *source, const char *opts, char *out_host_path, size_t out_cap);

	/*
	 * Tear down whatever establish() above produced for host_path (the
	 * exact string establish() wrote there). No-op for a kind with
	 * nothing of its own to release (e.g. HOSTDIR — the directory isn't
	 * ours to remove).
	 */
	void (*release)(const char *host_path);
} pm_metal_mount_ops_t;

/* impl: common — src/common/pymergetic/metal/mount/ops.c
 *
 * Look up a kind's ops table by enum value, for callers that pick a kind
 * dynamically (mount.c, fstab.c) instead of calling a dedicated getter
 * (pm_metal_mount_hostdir_ops() etc.) directly. NULL if kind is out of
 * range. */
const pm_metal_mount_ops_t *pm_metal_mount_resolve_kind(pm_metal_mount_kind_t kind);

/*
 * fstab/CLI <fstype> column name (e.g. "hostdir") -> kind, for fstab.c's
 * parser and main.c's --mount= flag. Returns 0/-1 (unknown name);
 * *out_kind is only written on success.
 */
int pm_metal_mount_kind_by_name(const char *name, pm_metal_mount_kind_t *out_kind);

/* Inverse of kind_by_name — stable fstype column for /proc/mounts. NULL if
 * kind is out of range. */
const char *pm_metal_mount_kind_name(pm_metal_mount_kind_t kind);

#endif /* PYMERGETIC_METAL_MOUNT_OPS_H_ */
