/*
 * Guest mount API — WASI preview1 has no mount syscall; this is our own
 * extension. See docs/MOUNT.md and README.md "Visibility".
 *
 * Same shape as util/{size,arena,…}.h: one host impl in
 * src/common/.../mount/mount.c; on wasm32 the declarations below are
 * wasi-style imports (no local body in the mod). The host mount *table*
 * is a separate contract: src/common/.../mount/table.h.
 *
 * Public (any mod): fstype_count() / fstype_name() — readonly, same set as
 * /proc/filesystems.
 *
 * Privileged (-DPM_METAL_BUILD_KERNEL via empty mods/<name>/MOUNT marker):
 * mount()/umount() and the Linux-shaped mount()/umount() inlines.
 * On linux, effects are visible in the same process (live remount) and to
 * later spawns — see docs/MOUNT.md.
 */
#ifndef PYMERGETIC_METAL_MOUNT_MOUNT_H_
#define PYMERGETIC_METAL_MOUNT_MOUNT_H_

#include <stddef.h>
#include <stdio.h>

#include "pymergetic/metal/util/wasi.h" /* IWYU pragma: keep */

/* This module's own import_module name — see mount.c's native_register()
 * for the host side that must build from this exact same constant. */
#define PM_METAL_MOUNT_WASI_MODULE "pymergetic.metal.mount"

#if defined(__wasm__)
#define PM_METAL_MOUNT_IMPORT(name) \
	PM_METAL_UTIL_WASI_IMPORT(PM_METAL_MOUNT_WASI_MODULE, name)
#endif

/*
 * Index available fstype names (same set as /proc/filesystems). count() is
 * the number of names; name(i, …) copies the i-th into out (NUL-terminated).
 * Returns 0/-1 (bad index or out too small). Always visible — readonly.
 *
 * impl: common — src/common/pymergetic/metal/mount/mount.c
 * impl: wasi import — same file (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_mount_fstype_count(void) PM_METAL_MOUNT_IMPORT(pm_metal_mount_fstype_count);
extern int pm_metal_mount_fstype_name(unsigned index, char *out, size_t cap)
	PM_METAL_MOUNT_IMPORT(pm_metal_mount_fstype_name);
#else
int pm_metal_mount_fstype_count(void);
int pm_metal_mount_fstype_name(unsigned index, char *out, size_t cap);
#endif

#if defined(PM_METAL_BUILD_KERNEL) || !defined(__wasm__)

#ifndef MS_RDONLY
#define MS_RDONLY 1
#endif

/*
 * Mount `source` of `fstype` at guest `target`, with optional comma-separated
 * `options` (may be NULL/empty). Same field meaning as an /etc/fstab line /
 * --mount= CLI. Returns 0/-1.
 *
 * Named pm_metal_mount_mount / pm_metal_mount_umount so these never collide
 * with the host table API (table.h's pm_metal_mount() / pm_metal_umount()).
 * Guest-visible only with -DPM_METAL_BUILD_KERNEL.
 *
 * impl: common — src/common/pymergetic/metal/mount/mount.c
 * impl: wasi import — same file (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_mount_mount(const char *source, const char *target, const char *fstype,
				 const char *options) PM_METAL_MOUNT_IMPORT(pm_metal_mount_mount);
extern int pm_metal_mount_umount(const char *target) PM_METAL_MOUNT_IMPORT(pm_metal_mount_umount);
#else
int pm_metal_mount_mount(const char *source, const char *target, const char *fstype, const char *options);
int pm_metal_mount_umount(const char *target);
#endif

#if defined(PM_METAL_BUILD_KERNEL)
/* Linux-shaped aliases — same imports, for busybox / familiar call sites.
 * Only MS_RDONLY is translated today; any other flag bit is rejected
 * (-1) so callers don't silently get a non-bind / non-remount mount. */
static inline int mount(const char *source, const char *target, const char *filesystemtype,
			unsigned long mountflags, const void *data)
{
	const char *opts = data ? (const char *)data : NULL;
	char combined[128];

	if (mountflags & ~(unsigned long)MS_RDONLY) {
		return -1;
	}
	if (mountflags & MS_RDONLY) {
		if (opts && opts[0]) {
			/* Already has ro? keep caller string; else prepend. */
			const char *p = opts;
			int has_ro = 0;

			while (*p) {
				const char *start = p;

				while (*p && *p != ',') {
					p++;
				}
				if ((size_t)(p - start) == 2 && start[0] == 'r' && start[1] == 'o') {
					has_ro = 1;
					break;
				}
				if (*p == ',') {
					p++;
				}
			}
			if (!has_ro) {
				int n = snprintf(combined, sizeof(combined), "ro,%s", opts);

				if (n <= 0 || (size_t)n >= sizeof(combined)) {
					return -1;
				}
				opts = combined;
			}
		} else {
			opts = "ro";
		}
	}
	return pm_metal_mount_mount(source, target, filesystemtype, opts);
}

static inline int umount(const char *target)
{
	return pm_metal_mount_umount(target);
}
#endif /* PM_METAL_BUILD_KERNEL */

#endif /* PM_METAL_BUILD_KERNEL || !__wasm__ */

#if !defined(__wasm__)
/*
 * Registers this module's wasi-style imports — host-only. Call once after
 * wasm_runtime_full_init() (runtime.c init() is the only caller today).
 *
 * impl: common — src/common/pymergetic/metal/mount/mount.c
 */
int pm_metal_mount_native_register(void);
#endif

#endif /* PYMERGETIC_METAL_MOUNT_MOUNT_H_ */
