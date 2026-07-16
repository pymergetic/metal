/*
 * Boot-time populate — global registry of embedded ustar [+ lz4] archives
 * extracted against guest "/" after Stage B. See docs/MOUNT.md Phase 4.
 *
 * Not name-keyed: each blob is a tree of guest paths; populate_all() resolves
 * every entry through the mount table so files land on whichever mount owns
 * that prefix. Each linked embed .c calls register(); app.c calls
 * populate_all() once after fstab/CLI mounts.
 */
#ifndef PYMERGETIC_METAL_MOUNT_POPULATE_H_
#define PYMERGETIC_METAL_MOUNT_POPULATE_H_

#include <stddef.h>
#include <stdint.h>

/* Blob is an lz4 block (as from util/lz4 compress); uncompressed_len is the
 * exact decompressed ustar size. Without this flag, blob is raw ustar and
 * uncompressed_len is ignored. */
#define PM_METAL_MOUNT_POPULATE_FLAG_LZ4 1u

/* impl: common — src/common/pymergetic/metal/mount/populate.c
 *
 * Append one archive to the process-wide list. blob must outlive populate_all()
 * (typical: static const in a generated embed .c). Returns 0/-1 (full/bad args).
 */
int pm_metal_mount_populate_register(const uint8_t *blob, size_t blob_len, size_t uncompressed_len,
				      unsigned flags);

/*
 * Decompress (if needed) + walk each registered ustar; for every entry, resolve
 * the guest path against the mount table and mkdir/write via the port.
 * Per-entry failure is logged and skipped (same spirit as Stage B per-line).
 * Empty registry is a no-op. Returns 0 always in practice (never fatal to boot).
 * impl: common — src/common/pymergetic/metal/mount/populate.c
 */
int pm_metal_mount_populate_all(void);

/* Drop every registration (test/shutdown hygiene). Does not free blob bytes —
 * those are caller-owned (usually static).
 * impl: common — src/common/pymergetic/metal/mount/populate.c */
void pm_metal_mount_populate_clear(void);

#endif /* PYMERGETIC_METAL_MOUNT_POPULATE_H_ */
