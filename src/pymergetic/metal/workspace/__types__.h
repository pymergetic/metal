/* pymergetic.metal.workspace — source workspace materialization (Phase 14).
 *
 * The closing move of the self-hosting story: the kernel's own source tree,
 * materialized where the kernel can build from it. Two sources of bytes:
 *
 *  - cards: the embedded src table (tools/embed_src.py -> src_embed.inc.h)
 *    IS the card tree — a materialize walk copies each card's files into the
 *    fs card at /src/pymergetic/metal/<card-path>/<file>. No archive, no
 *    filesystem: the tree ships in the image on every seat.
 *
 *  - externals: the manifest's archive field names a .tar.gz; the minimal
 *    tar reader below walks it (uzlib inflate for the gzip layer, a
 *    512-byte-header walk for the tar layer) and adds every member under
 *    /src/externals/<name>/<member>. The archive bytes come from the caller
 *    (host-loaded): a 575 MB rustc tarball is a build input, not image bytes.
 *
 * The unix seat mirrors every materialized file to a host directory
 * (default /tmp/metal-src) so vim/VS Code see the same bytes the kernel
 * sees. The mirror is a fill, not a second tree: fs is the single source
 * of truth, the host copy is a projection of it.
 */
#ifndef PYMERGETIC_METAL_WORKSPACE_TYPES_H
#define PYMERGETIC_METAL_WORKSPACE_TYPES_H

#include "pymergetic/util/mem/__types__.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Seat decision shared by every TU (the impl gates its host calls on it,
 * the tests gate their expectations on it): the mirror exists only on seats
 * with a host filesystem — the unix/host binary. Firmware and emscripten
 * have no host FS; they compile it off and mirror_set refuses. */
#if !defined(PM_METAL_WORKSPACE_NO_MIRROR) \
    && !defined(__EMSCRIPTEN__) \
    && !defined(PM_METAL_FIRMWARE)
#define PM_METAL_WORKSPACE_MIRROR 1
#endif

#define PM_METAL_WORKSPACE_ERR_MAX 256u
#define PM_METAL_WORKSPACE_TAR_BLOCK 512u
#define PM_METAL_WORKSPACE_PATH_MAX 384u

/* Materialize the whole card tree into the fs card.
 *
 * Walks the embedded src table and adds every card file at
 * /src/pymergetic/metal/<card-path>/<file>, where <card-path> is the fqn's
 * module tail (pymergetic.metal.jit.c -> jit/c). Idempotent: an existing
 * identical file is skipped, an existing different file is replaced
 * (drop + add — the rebuild contract).
 *
 * :param arena: scratch for path building (not retained)
 * :param n_files: out, number of files materialized
 * :param errbuf: error text when the return is not 0
 * :return: 0 on success
 * :example:
 * uint32_t n = 0; char err[256];
 * if (pm_metal_workspace_materialize(arena, &n, err, sizeof(err)) != 0)
 *     return -1;
 */
int32_t pm_metal_workspace_materialize(pm_util_mem_arena_t *arena,
    uint32_t *n_files, char *errbuf, size_t errbuf_len);

/* Extract an external's .tar.gz into the fs card at /src/externals/<name>/.
 *
 * The minimal tar reader: gzip header skip -> uzlib inflate (the whole
 * member stream into the arena) -> 512-byte header walk. Members are added
 * with the archive's leading directory component preserved so
 * rustc-1.90.0-src.tar.gz lands as /src/externals/rustc-1.90.0-src/....
 * Directories (typeflag '5') and links are skipped — files only.
 *
 * :param arena: scratch for the inflated tar stream (freed by caller)
 * :param name: external name (the fs prefix under /src/externals/)
 * :param archive: the .tar.gz bytes (caller-owned, copied during the call)
 * :param archive_len: byte length of archive
 * :param n_files: out, number of files extracted
 * :param errbuf: error text when the return is not 0
 * :return: 0 on success
 * :example:
 * uint32_t n = 0; char err[256];
 * if (pm_metal_workspace_extract_external(arena, "mrustc", gz, gz_len,
 *         &n, err, sizeof(err)) != 0)
 *     return -1;
 */
int32_t pm_metal_workspace_extract_external(pm_util_mem_arena_t *arena,
    const char *name, const uint8_t *archive, size_t archive_len,
    uint32_t *n_files, char *errbuf, size_t errbuf_len);

/* Set the unix mirror root (empty = mirror off; off by default).
 *
 * On unix seats every materialized file is also written to
 * <root>/src/... after the fs add. The mirror is write-only from the
 * kernel's perspective: fs stays the source of truth.
 *
 * :param root: host directory for the mirror ("" disables)
 * :return: 0 on success, -1 on a non-unix seat (no host FS)
 * :example:
 * pm_metal_workspace_mirror_set("/tmp/metal-src");
 */
int32_t pm_metal_workspace_mirror_set(const char *root);

/* Number of files currently in the materialized workspace (/src/...). */
uint32_t pm_metal_workspace_file_count(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_WORKSPACE_TYPES_H */
