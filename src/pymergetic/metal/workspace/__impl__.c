/* pymergetic.metal.workspace — source workspace materialization (Phase 14).
 *
 * Cards walk out of the embedded src table (the tree already ships in the
 * image on every seat); externals extract through the minimal tar reader on
 * uzlib's inflate (pymergetic.util.zlib, linked from the wasmmod staticlib).
 * The unix seat mirrors every materialized file to a host directory so
 * vim/VS Code see the same bytes the kernel sees.
 */
#include "pymergetic/metal/workspace/__exports__.h"

#include "pymergetic/metal/workspace/__types__.h"
#include "pymergetic/metal/fs/__exports__.h"
#include "pymergetic/util/zlib/__exports__.h"
#include "pymergetic/util/mem.h"

#include "pymergetic/metal/inspect/src_embed.inc.h"

#include <stdio.h>
#include <string.h>

static uint32_t s_count;

static void err_set(char *errbuf, size_t errbuf_len, const char *msg) {
    if (errbuf == NULL || errbuf_len == 0) {
        return;
    }
    snprintf(errbuf, errbuf_len, "%s", msg);
}

/*------------------ unix host mirror ------------------
 * A projection of the fs tree, not a second source of truth: every
 * materialized file is also written to <root>/<path> on the host. The seat
 * decision (PM_METAL_WORKSPACE_MIRROR) is made once in __types__.h so the
 * impl and the tests agree; firmware and emscripten compile it off and
 * mirror_set refuses. */

#ifdef PM_METAL_WORKSPACE_MIRROR
#include <sys/stat.h>
#include <sys/types.h>

static char s_mirror_root[128];
#endif

int32_t pm_metal_workspace_mirror_set(const char *root) {
#ifdef PM_METAL_WORKSPACE_MIRROR
    if (root == NULL || root[0] == 0) {
        s_mirror_root[0] = 0;
        return 0;
    }
    if (strlen(root) >= sizeof(s_mirror_root)) {
        return -1;
    }
    snprintf(s_mirror_root, sizeof(s_mirror_root), "%s", root);
    return 0;
#else
    (void)root;
    return -1; /* no host FS on this seat */
#endif
}

/* Write one file through to the mirror (best effort: a mirror failure never
 * blocks the fs add — fs is the source of truth, the host copy a
 * projection). mkdir -p walks the path's '/'s before the fopen. */
static void mirror_write(const char *fs_path, const uint8_t *data, uint32_t len) {
#ifdef PM_METAL_WORKSPACE_MIRROR
    char host[PM_METAL_WORKSPACE_PATH_MAX + 160];
    FILE *f;
    char *p;
    if (s_mirror_root[0] == 0 || fs_path == NULL || data == NULL) {
        return;
    }
    if (snprintf(host, sizeof(host), "%s%s", s_mirror_root, fs_path)
            >= (int)sizeof(host)) {
        return;
    }
    for (p = host + 1; *p != 0; p++) {
        if (*p == '/') {
            *p = 0;
            (void)mkdir(host, 0755);
            *p = '/';
        }
    }
    f = fopen(host, "wb");
    if (f == NULL) {
        return;
    }
    (void)fwrite(data, 1, len, f);
    (void)fclose(f);
#else
    (void)fs_path; (void)data; (void)len;
#endif
}

/* Add to fs through the replace face (the rebuild contract) and mirror.
 * pm_metal_fs_write is in-place when the length matches, so a re-materialize
 * with identical bytes neither grows the arena nor churns the tree; a changed
 * file is a new version (drop + fresh reserve). */
static int32_t ws_add(const char *path, const uint8_t *data, uint32_t len) {
    if (pm_metal_fs_write(path, data, len) < 0) {
        return -1;
    }
    mirror_write(path, data, len);
    return 0;
}

/*------------------ card tree materialization ------------------
 * The embedded table is sorted by fqn; the fs path mirrors the module tail
 * (pymergetic.metal.jit.c -> /src/pymergetic/metal/jit/c). The pymergetic.
 * prefix is shared by every card in the table, so the walk strips it once. */

int32_t pm_metal_workspace_materialize(pm_util_mem_arena_t *arena,
    uint32_t *n_files, char *errbuf, size_t errbuf_len) {
    uint32_t n = pm_metal_src_card_count();
    uint32_t i;
    uint32_t count = 0;
    (void)arena;

    if (n_files == NULL) {
        err_set(errbuf, errbuf_len, "materialize: bad args");
        return -1;
    }
    *n_files = 0;
    for (i = 0; i < n; i++) {
        const pm_metal_src_card_t *c = &PM_METAL_SRC_CARDS[i];
        char dir[PM_METAL_WORKSPACE_PATH_MAX];
        const char *fqn;
        const char *tail;
        const char *q;
        size_t w = 0;
        uint32_t f;
        if (c->fqn == NULL || c->files == NULL) {
            continue;
        }
        fqn = c->fqn;
        tail = strncmp(fqn, "pymergetic.", 11) == 0 ? fqn + 11 : fqn;
        /* fqn dots are fs path slashes: pymergetic.metal.jit.c -> jit/c */
        w = (size_t)snprintf(dir, sizeof(dir), "/src/pymergetic/");
        if (w >= sizeof(dir)) {
            continue;
        }
        for (q = tail; *q != 0 && w + 1 < sizeof(dir); q++) {
            dir[w++] = *q == '.' ? '/' : *q;
        }
        dir[w] = 0;
        for (f = 0; f < c->nfiles; f++) {
            char path[PM_METAL_WORKSPACE_PATH_MAX + 64];
            const char *rel = c->files[f].rel;
            if (rel == NULL || c->files[f].data == NULL) {
                continue;
            }
            if (snprintf(path, sizeof(path), "%s/%s", dir, rel)
                    >= (int)sizeof(path)) {
                continue;
            }
            if (ws_add(path, c->files[f].data, c->files[f].len) != 0) {
                char msg[PM_METAL_WORKSPACE_ERR_MAX];
                snprintf(msg, sizeof(msg),
                    "materialize: fs refused %.180s", path);
                err_set(errbuf, errbuf_len, msg);
                return -1;
            }
            count++;
        }
    }
    s_count = count;
    *n_files = count;
    return 0;
}

/*------------------ minimal tar reader on uzlib ------------------
 * gzip container -> raw deflate (RFC 1952 header walk) -> uzlib inflate
 * (pymergetic.util.zlib) -> 512-byte tar header walk. Files only: dirs,
 * links, pax/GNU meta blocks are skipped with their data. */

static const uint8_t *gz_deflate_payload(const uint8_t *gz, size_t gz_len,
    size_t *deflate_len) {
    const uint8_t *p;
    const uint8_t *end;
    uint8_t flg;
    if (gz == NULL || gz_len < 18) {
        return NULL;
    }
    if (gz[0] != 0x1f || gz[1] != 0x8b || gz[2] != 0x08) {
        return NULL;
    }
    flg = gz[3];
    p = gz + 10;
    end = gz + gz_len;
    if (flg & 0x04) { /* FEXTRA: 2-byte len + len bytes */
        uint32_t xlen;
        if (end - p < 2) {
            return NULL;
        }
        xlen = (uint32_t)p[0] | ((uint32_t)p[1] << 8);
        p += 2 + xlen;
    }
    if (flg & 0x08) { /* FNAME: NUL-terminated */
        while (p < end && *p != 0) {
            p++;
        }
        p++;
    }
    if (flg & 0x10) { /* FCOMMENT: NUL-terminated */
        while (p < end && *p != 0) {
            p++;
        }
        p++;
    }
    if (flg & 0x02) { /* FHCRC: 2 bytes */
        p += 2;
    }
    if (p >= end) {
        return NULL;
    }
    /* uzlib stops at the end-of-stream marker, so the exact tail bound does
     * not matter — hand it everything past the header. */
    *deflate_len = (size_t)(end - p);
    return p;
}

/* tar numeric fields: octal ASCII, NUL/space padded */
static size_t tar_num(const uint8_t *field, size_t width) {
    size_t v = 0;
    size_t i;
    for (i = 0; i < width; i++) {
        uint8_t c = field[i];
        if (c == 0 || c == ' ') {
            continue;
        }
        if (c < '0' || c > '7') {
            break;
        }
        v = v * 8u + (size_t)(c - '0');
    }
    return v;
}

static int tar_block_zero(const uint8_t *block) {
    size_t i;
    for (i = 0; i < PM_METAL_WORKSPACE_TAR_BLOCK; i++) {
        if (block[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static size_t tar_member_len(const uint8_t *hdr) {
    /* name field is 100 bytes, NUL-terminated (or full, GNU-style) */
    size_t n = 0;
    while (n < 100 && hdr[n] != 0) {
        n++;
    }
    return n;
}

/* skip a member's data blocks (padded up to 512) */
static size_t tar_data_span(size_t size) {
    return (size + PM_METAL_WORKSPACE_TAR_BLOCK - 1u)
        / PM_METAL_WORKSPACE_TAR_BLOCK * PM_METAL_WORKSPACE_TAR_BLOCK;
}

int32_t pm_metal_workspace_extract_external(pm_util_mem_arena_t *arena,
    const char *name, const uint8_t *archive, size_t archive_len,
    uint32_t *n_files, char *errbuf, size_t errbuf_len) {
    const uint8_t *deflate;
    size_t deflate_len;
    uint8_t *tar;
    size_t tar_cap;
    int32_t inflated;
    size_t off;
    uint32_t count = 0;

    if (arena == NULL || name == NULL || archive == NULL || n_files == NULL
        || archive_len == 0) {
        err_set(errbuf, errbuf_len, "extract: bad args");
        return -1;
    }
    *n_files = 0;

    deflate = gz_deflate_payload(archive, archive_len, &deflate_len);
    if (deflate == NULL || deflate_len == 0) {
        err_set(errbuf, errbuf_len, "extract: not a gzip stream");
        return -1;
    }

    /* gzip trailer isize (last 4 bytes, LE) is the uncompressed length mod
     * 2^32 — the honest arena cap for the tar stream. */
    {
        const uint8_t *trailer = archive + archive_len - 4;
        uint32_t isize = (uint32_t)trailer[0] | ((uint32_t)trailer[1] << 8)
            | ((uint32_t)trailer[2] << 16) | ((uint32_t)trailer[3] << 24);
        if (isize == 0) {
            err_set(errbuf, errbuf_len, "extract: empty archive");
            return -1;
        }
        tar_cap = isize;
    }
    tar = (uint8_t *)pm_util_mem_alloc(arena, tar_cap);
    if (tar == NULL) {
        err_set(errbuf, errbuf_len, "extract: arena exhausted");
        return -1;
    }
    inflated = pm_util_zlib_inflate(deflate, deflate_len, tar, tar_cap);
    if (inflated < 0
        || (size_t)inflated < 2 * PM_METAL_WORKSPACE_TAR_BLOCK) {
        err_set(errbuf, errbuf_len, "extract: inflate failed");
        return -1;
    }

    off = 0;
    while (off + PM_METAL_WORKSPACE_TAR_BLOCK <= (size_t)inflated) {
        const uint8_t *hdr = tar + off;
        char path[PM_METAL_WORKSPACE_PATH_MAX + 64];
        size_t size;
        uint8_t typeflag;
        size_t member_len;
        off += PM_METAL_WORKSPACE_TAR_BLOCK;

        if (tar_block_zero(hdr)) {
            break; /* two zero blocks = end of archive */
        }
        member_len = tar_member_len(hdr);
        size = tar_num(hdr + 124, 12);
        typeflag = hdr[156];
        if (member_len == 0 || typeflag == '5' || typeflag == 'L'
            || typeflag == 'K' || typeflag == 'x' || typeflag == 'g'
            || (typeflag != '0' && typeflag != 0 && typeflag != '7')) {
            /* dirs, pax/GNU meta, links, devices: skip data, add nothing */
            off += tar_data_span(size);
            continue;
        }
        if (off + size > (size_t)inflated) {
            err_set(errbuf, errbuf_len, "extract: member overruns archive");
            return -1;
        }
        if (snprintf(path, sizeof(path), "/src/externals/%s/%.*s",
                name, (int)member_len, (const char *)hdr)
                >= (int)sizeof(path)) {
            off += tar_data_span(size);
            continue;
        }
        if (size > 0) {
            if (ws_add(path, tar + off, (uint32_t)size) != 0) {
                char msg[PM_METAL_WORKSPACE_ERR_MAX];
                snprintf(msg, sizeof(msg),
                    "extract: fs refused %.180s", path);
                err_set(errbuf, errbuf_len, msg);
                return -1;
            }
            count++;
        }
        off += tar_data_span(size);
    }
    s_count += count;
    *n_files = count;
    return 0;
}

/*------------------ count ------------------*/

uint32_t pm_metal_workspace_file_count(void) {
    /* answered from the last materialize/extract calls — the workspace card
     * is the /src/ writer, the inspector serves the tree itself. */
    return s_count;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.workspace, pm_metal_workspace_materialize, pm_metal_workspace_materialize,
    int32_t(pm_util_mem_arena_t *, uint32_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.workspace, pm_metal_workspace_extract_external, pm_metal_workspace_extract_external,
    int32_t(pm_util_mem_arena_t *, const char *, const uint8_t *, size_t,
        uint32_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.workspace, pm_metal_workspace_mirror_set, pm_metal_workspace_mirror_set,
    int32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.metal.workspace, pm_metal_workspace_file_count, pm_metal_workspace_file_count,
    uint32_t(void));

PM_MOD_BOOTDEP_C(pymergetic.metal.workspace, pymergetic.metal.fs);
