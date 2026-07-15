/*
 * Stage B — /etc/fstab parsing + apply. See docs/MOUNT.md "Boot
 * sequence". impl: common — pure text parsing plus calls into mount.h,
 * no per-target code (a `tmpfs` fstab line goes through mount.h's own
 * kind dispatch exactly like a `hostdir` line does — this file never
 * touches any per-kind backend directly).
 */
#ifndef PYMERGETIC_METAL_MOUNT_FSTAB_H_
#define PYMERGETIC_METAL_MOUNT_FSTAB_H_

/* impl: common — src/common/pymergetic/metal/mount/fstab.c
 *
 * Applies one already-split fstab line's fields as a single mount()
 * call — shared by fstab_apply() below (one call per non-comment file
 * line) and main.c's own --mount= CLI flag (its own
 * "<fstype>:<source>:<target>[:opts]" syntax, just reordered into these
 * same fields before calling this) — "same parser, same apply function,
 * one code path," per docs/MOUNT.md "CLI --mount=". `options` may be
 * NULL/empty. Logs and returns -1 on an unknown fstype or a failed
 * mount() — never fatal to the caller, matching mount -a's own per-line
 * semantics (see fstab_apply()).
 */
int pm_metal_mount_fstab_apply_fields(const char *source, const char *target, const char *fstype,
				       const char *options);

/*
 * Reads guest_fstab_path (resolved against the already-mounted root —
 * call only after Stage A) and applies each non-blank, non-'#'-comment
 * line via apply_fields() above, in file order (mount -a's own line
 * order, not sorted by path depth). A bad/short line is logged and
 * skipped, never aborts the rest of the file. Missing file is a silent
 * no-op (0) — existing single-root setups with no fstab keep working
 * completely unchanged. Returns 0 always in practice: Stage B is defined
 * to never be fatal (only Stage A is, per docs/MOUNT.md) — a per-line
 * failure, or even a hard failure reading an *existing* fstab, just logs
 * and moves on rather than propagating up as a whole-file failure.
 */
int pm_metal_mount_fstab_apply(const char *guest_fstab_path);

#endif /* PYMERGETIC_METAL_MOUNT_FSTAB_H_ */
