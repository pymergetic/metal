/*
 * Named guest packages — lz4+ustar blobs with a dep graph.
 * Every platform embeds the same packages; boot applies them onto guest "/".
 * See docs/MOUNT.md (guest packages).
 */
#ifndef PYMERGETIC_METAL_MOUNT_PKG_H_
#define PYMERGETIC_METAL_MOUNT_PKG_H_

#include <stddef.h>
#include <stdint.h>

#define PM_METAL_MOUNT_PKG_MAX 8
#define PM_METAL_MOUNT_PKG_ID_MAX 32
#define PM_METAL_MOUNT_PKG_DEPS_MAX 4

/* Same flag bit as populate: blob is lz4 block, uncompressed_len is ustar size. */
#define PM_METAL_MOUNT_PKG_FLAG_LZ4 1u

/*
 * Register one package (typical: constructor in generated pkgs_embed.c).
 * deps may be NULL when ndeps==0; dep strings must outlive the process.
 * Returns 0/-1 (full / bad args / duplicate id).
 * impl: common — src/common/pymergetic/metal/mount/pkg.c
 */
int pm_metal_pkg_register(const char *id, const uint8_t *blob, size_t blob_len,
			   size_t uncompressed_len, unsigned flags, const char *const *deps,
			   size_t ndeps);

/*
 * Register every linked package embed (generated pkgs_init.c).
 * Weak no-op when no guest-pkgs are linked. Called from apply_all/ensure.
 */
void pm_metal_pkg_embed_init(void);

/* Apply id and its dependencies (topo order). Idempotent per boot. 0/-1. */
int pm_metal_pkg_ensure(const char *id);

/* Apply every registered package in dependency order. 0/-1. */
int pm_metal_pkg_apply_all(void);

/* Test/shutdown hygiene. */
void pm_metal_pkg_clear(void);

#endif /* PYMERGETIC_METAL_MOUNT_PKG_H_ */
