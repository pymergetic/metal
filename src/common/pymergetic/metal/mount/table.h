/*
 * Mount table — the thing runtime.c's own vfs_root used to be a single
 * hand-rolled instance of. See docs/MOUNT.md.
 *
 * Host-only (src/common). Privileged guest mount()/umount() wasi imports
 * live in include/pymergetic/metal/mount/mount.h — same shape as util/ headers.
 *
 * One process-wide table (same "controller-thread only for
 * mutation" contract as runtime.h's init()/shutdown() — nothing here
 * takes its own lock; see runtime.c's own file header for why the root
 * mount is safe to establish before g_pm_metal_runtime_lock even exists).
 * Guest WASI I/O still picks a frozen preopen via wasi-libc, then Metal's
 * linux wasi/file.c re-resolves the absolute guest path against this table
 * (live remount — see docs/MOUNT.md). The loader uses resolve() for the
 * same longest-prefix rule on host-side reads (mod bytecode, etc.).
 */
#ifndef PYMERGETIC_METAL_MOUNT_TABLE_H_
#define PYMERGETIC_METAL_MOUNT_TABLE_H_

#include <stddef.h>

#include "pymergetic/metal/mount/ops.h"

/* Table size — same style as runtime.h's own PM_METAL_RUNTIME_MAX_HANDLES:
 * public so anything that needs to size its own array to match (e.g.
 * runtime.c's run_ex() building map_dir_list) can do so without a second,
 * silently-drifting copy of the number. */
#define PM_METAL_MOUNT_MAX 8
#define PM_METAL_MOUNT_GUEST_PATH_MAX 128
#define PM_METAL_MOUNT_HOST_PATH_MAX 256
#define PM_METAL_MOUNT_OPTS_MAX 64
/* One build_map_dir_list() entry is "<guest>::<host>\0" — "::" (2) + NUL (1),
 * rounded up a little for slack. */
#define PM_METAL_MOUNT_MAP_DIR_ENTRY_MAX (PM_METAL_MOUNT_GUEST_PATH_MAX + PM_METAL_MOUNT_HOST_PATH_MAX + 8)

/* impl: common — src/common/pymergetic/metal/mount/table.c
 *
 * Register one mount. guest_path is normalized (leading "/" added if
 * missing, exactly one trailing slash stripped — "/" itself is left
 * alone) and must be unique in the table; mounting again at the same
 * (normalized) guest_path replaces whatever was there before — real
 * Linux's own "last mount at a given point wins" rule (see docs/MOUNT.md
 * "CLI --mount=") — release()ing the old kind's backing first.
 *
 * `opts` is a comma-separated options string (may be NULL): this call
 * itself only ever looks at the two generic tokens "ro"/"rw" (tracked in
 * the table for later inspection — read-only is not yet enforced against
 * guest WASI I/O, a real gap, see docs/MOUNT.md); the full string
 * (generic tokens included) is also handed to the kind's own establish()
 * unmodified, for whatever kind-specific tokens it looks for itself
 * (e.g. tmpfs's future "size=").
 *
 * Returns 0/-1 (bad args, table full, or the kind's own establish()
 * failed).
 */
int pm_metal_mount(const char *guest_path, pm_metal_mount_kind_t kind, const char *source, const char *opts);

/* Unmount whatever is registered at guest_path (exact match against the
 * normalized form — not a prefix match). release()s that kind's own
 * backing, then frees the table slot. Returns 0/-1 (nothing mounted
 * there, once normalized). */
int pm_metal_umount(const char *guest_path);

/*
 * Live longest-prefix resolve for WASI path ops (and the loader).
 * For HOSTDIR/TMPFS: host_path is host_base + remainder.
 * For PROC: host_path / host_base are the sentinel; remainder is under /proc
 * (never concatenated onto the sentinel).
 */
typedef struct pm_metal_mount_resolve {
	pm_metal_mount_kind_t kind;
	char guest_mount[PM_METAL_MOUNT_GUEST_PATH_MAX];
	char host_base[PM_METAL_MOUNT_HOST_PATH_MAX];
	char host_path[PM_METAL_MOUNT_HOST_PATH_MAX];
	char remainder[PM_METAL_MOUNT_GUEST_PATH_MAX];
} pm_metal_mount_resolve_t;

int pm_metal_mount_resolve_ex(const char *guest_path, pm_metal_mount_resolve_t *out);

/*
 * Loader convenience: resolve_ex then write host_path (full host file path
 * for hostdir/tmpfs; sentinel for proc). Returns 0/-1.
 */
int pm_metal_mount_resolve(const char *guest_path, char *out_host_path, size_t out_cap);

/*
 * Reverse lookup for os_open_preopendir tagging (WAMR only passes the host
 * half of guest::host). Exact match on establish()'s host_path. 0/-1.
 */
int pm_metal_mount_find_by_host(const char *host_path, char *out_guest, size_t guest_cap,
				 pm_metal_mount_kind_t *out_kind);

/*
 * Emits one "<guest>::<host>" string per registered mount into
 * out_bufs[0..count-1] (each PM_METAL_MOUNT_MAP_DIR_ENTRY_MAX bytes),
 * for wasm_runtime_set_wasi_args_ex()'s own map_dir_list — see
 * runtime.c's run_ex(). *out_count is the number written (<=
 * max_entries). Returns 0/-1 (bad args, or max_entries too small for the
 * table's current entry count — nothing partially written in that case).
 */
int pm_metal_mount_build_map_dir_list(char out_bufs[][PM_METAL_MOUNT_MAP_DIR_ENTRY_MAX], size_t max_entries,
				       size_t *out_count);

/* Tear down every registered mount (release()ing each kind's own
 * backing) and clear the table. Called once from runtime.c's
 * shutdown(). */
void pm_metal_mount_shutdown_all(void);

/* Exact-match: 1 if something is mounted at guest_path, else 0. */
int pm_metal_mount_exists(const char *guest_path);

/*
 * Walk every active table entry (arbitrary order). Used by procfs
 * /proc/mounts generation — see mount/proc.h.
 */
typedef void (*pm_metal_mount_foreach_fn)(const char *guest_path, const char *source,
					   const char *host_path, pm_metal_mount_kind_t kind,
					   const char *opts, int readonly, void *ctx);
void pm_metal_mount_foreach(pm_metal_mount_foreach_fn fn, void *ctx);

#endif /* PYMERGETIC_METAL_MOUNT_TABLE_H_ */
