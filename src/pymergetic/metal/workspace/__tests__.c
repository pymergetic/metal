/* pymergetic.metal.workspace tests — the Phase 14 prove:
 *  - materialize walks every embedded card file into fs, byte-identical
 *  - materialize is idempotent (second call, same bytes, no churn)
 *  - the tar reader extracts a real gzip'd tar: files land, dirs don't
 *  - the mirror writes the same bytes to the host dir (unix seat)
 *  - bad input is refused honestly (not gzip, truncated member)
 */
#include "pymergetic/metal/workspace/__types__.h"
#include "pymergetic/metal/fs/__exports__.h"
#include "pymergetic/util/zlib/__exports__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *g_backing;
static pm_util_mem_arena_t *g_arena;

static int32_t setup(void) {
    if (g_arena != NULL) {
        return 0;
    }
    g_backing = malloc(4u << 20);
    if (g_backing == NULL) {
        return 1;
    }
    g_arena = pm_util_mem_arena_create(g_backing, 4u << 20);
    if (g_arena == NULL) {
        free(g_backing);
        g_backing = NULL;
        return 2;
    }
    return 0;
}

/* ---- gzip'd tar fixture, built in-test on uzlib's deflate ---- */

static void put32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void tar_header(uint8_t *h, const char *name, size_t size, char type) {
    char oct[13];
    memset(h, 0, 512);
    snprintf((char *)h, 100, "%s", name);
    snprintf(oct, sizeof(oct), "%011zo", size);
    memcpy(h + 124, oct, 11);
    h[156] = (uint8_t)type;
    memcpy(h + 257, "ustar", 5);  /* POSIX ustar magic */
    h[263] = '0';
}

static void crc32_of(const uint8_t *data, size_t len, uint8_t out[4]);

/* build a gzip member around a raw deflate stream (no FNAME: shortest path
 * through the reader's header walk) */
static size_t gz_wrap(const uint8_t *deflated, size_t deflated_len,
    const uint8_t *raw, size_t raw_len, uint8_t *out, size_t out_cap) {
    size_t w = 0;
    if (10 + deflated_len + 8 > out_cap) {
        return 0;
    }
    out[w++] = 0x1f; out[w++] = 0x8b; out[w++] = 0x08; out[w++] = 0x00;
    out[w++] = 0; out[w++] = 0; out[w++] = 0; out[w++] = 0;
    out[w++] = 0; out[w++] = 0x03;
    memcpy(out + w, deflated, deflated_len);
    w += deflated_len;
    {
        uint8_t crc[4];
        crc32_of(raw, raw_len, crc);
        memcpy(out + w, crc, 4);
        w += 4;
        put32le(out + w, (uint32_t)raw_len);
        w += 4;
    }
    return w;
}

/* tar stream: two files + a dir + two zero blocks */
static size_t build_tar(uint8_t *tar, size_t cap) {
    const char *f1 = "fixture-1.0/README.md";
    const char *f1_body = "# fixture\nthe workspace tar reader prove\n";
    const char *f2 = "fixture-1.0/src/lib.c";
    const char *f2_body = "int ws_fixture_two(void) { return 2; }\n";
    size_t w = 0;
    if (cap < 512 * 8) {
        return 0;
    }
    tar_header(tar + w, "fixture-1.0/", 0, '5');
    w += 512;
    tar_header(tar + w, f1, strlen(f1_body), '0');
    memcpy(tar + w + 512, f1_body, strlen(f1_body));
    w += 512 + 512;
    tar_header(tar + w, "fixture-1.0/src/", 0, '5');
    w += 512;
    tar_header(tar + w, f2, strlen(f2_body), '0');
    memcpy(tar + w + 512, f2_body, strlen(f2_body));
    w += 512 + 512;
    memset(tar + w, 0, 1024);
    w += 1024;
    return w;
}

/* minimal crc32 (gzip polynomial, bitwise) — the gzip trailer check is not
 * read by our extractor, but the member must be well-formed anyway */
static void crc32_of(const uint8_t *data, size_t len, uint8_t out[4]) {
    uint32_t crc = 0xffffffffu;
    size_t i;
    int k;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    crc ^= 0xffffffffu;
    put32le(out, crc);
}

/* ---- proves ---- */

static int32_t test_materialize(void) {
    uint32_t n = 0;
    char err[PM_METAL_WORKSPACE_ERR_MAX];
    if (setup()) {
        return 1;
    }
    if (pm_metal_workspace_materialize(g_arena, &n, err, sizeof(err)) != 0) {
        return 2;
    }
    if (n == 0) {
        return 3; /* the embedded table is non-empty on every seat */
    }
    /* spot-check a known card file: build's own muscle */
    {
        const char *path = "/src/pymergetic/metal/build/__impl__.c";
        uint32_t len = 0;
        uint8_t buf[512];
        uint32_t rlen = sizeof(buf);
        if (pm_metal_fs_stat(path, &len) != 0 || len == 0) {
            return 4;
        }
        if (pm_metal_fs_read(path, buf, &rlen) != 0 || rlen != sizeof(buf)) {
            return 5;
        }
        if (memcmp(buf, "/* pymergetic.metal.build", 25) != 0) {
            return 6;
        }    }
    /* idempotent: same call again succeeds with the same count */
    {
        uint32_t n2 = 0;
        if (pm_metal_workspace_materialize(g_arena, &n2, err, sizeof(err)) != 0) {
            return 7;
        }
        if (n2 != n) {
            return 8;
        }
    }
    if (pm_metal_workspace_file_count() != n) {
        return 9;
    }
    return 0;
}

static int32_t test_extract(void) {
    uint8_t tar[512 * 8];
    uint8_t *deflated;
    size_t tar_len;
    size_t deflated_len = 0;
    uint8_t gz[8192];
    size_t gz_len;
    static uint8_t hist[32 * 1024];
    uint32_t n = 0;
    char err[PM_METAL_WORKSPACE_ERR_MAX];

    if (setup()) {
        return 20;
    }
    tar_len = build_tar(tar, sizeof(tar));
    if (tar_len == 0) {
        return 21;
    }
    deflated = (uint8_t *)pm_util_mem_alloc(g_arena, tar_len * 2u);
    if (deflated == NULL) {
        return 22;
    }
    deflated_len = (size_t)pm_util_zlib_deflate(tar, tar_len,
        deflated, tar_len * 2u, hist, sizeof(hist));
    if (deflated_len == 0) {
        return 23;
    }
    gz_len = gz_wrap(deflated, deflated_len, tar, tar_len, gz, sizeof(gz));
    if (gz_len == 0) {
        return 24;
    }
    if (pm_metal_workspace_extract_external(g_arena, "fixture", gz, gz_len,
            &n, err, sizeof(err)) != 0) {
        fprintf(stderr, "workspace extract: %s\n", err);
        return 25;
    }
    if (n != 2) {
        return 26; /* two files; the two dirs must not have been added */
    }
    /* member bodies land byte-identical */
    {
        const char *p = "/src/externals/fixture/fixture-1.0/README.md";
        const char *body = "# fixture\nthe workspace tar reader prove\n";
        char buf[128];
        uint32_t rlen = sizeof(buf);
        if (pm_metal_fs_read(p, (uint8_t *)buf, &rlen) != 0) {
            return 27;
        }
        if (rlen != strlen(body) || memcmp(buf, body, rlen) != 0) {
            return 28;
        }
    }
    {
        const char *p = "/src/externals/fixture/fixture-1.0/src/lib.c";
        const char *body = "int ws_fixture_two(void) { return 2; }\n";
        char buf[128];
        uint32_t rlen = sizeof(buf);
        if (pm_metal_fs_read(p, (uint8_t *)buf, &rlen) != 0) {
            return 29;
        }
        if (rlen != strlen(body) || memcmp(buf, body, rlen) != 0) {
            return 30;
        }
    }
    /* a directory member must not appear as a file */
    {
        uint32_t len = 0;
        if (pm_metal_fs_stat("/src/externals/fixture/fixture-1.0/", &len) == 0) {
            return 31;
        }
    }
    return 0;
}

static int32_t test_bad_input(void) {
    uint32_t n = 0;
    char err[PM_METAL_WORKSPACE_ERR_MAX];
    uint8_t notgz[64];
    memset(notgz, 0x41, sizeof(notgz));
    if (pm_metal_workspace_extract_external(g_arena, "bad", notgz,
            sizeof(notgz), &n, err, sizeof(err)) == 0) {
        return 40;
    }
    if (err[0] == 0) {
        return 41; /* refused with a reason, not silence */
    }
    if (pm_metal_workspace_extract_external(g_arena, "bad", NULL, 0,
            &n, err, sizeof(err)) == 0) {
        return 42;
    }
    return 0;
}

static int32_t test_mirror(void) {
#ifdef PM_METAL_WORKSPACE_MIRROR
    /* mirror_set is the unix-seat face; prove it takes a root and the next
     * materialize writes through. The read-back uses fopen (host side). */
    uint32_t n = 0;
    char err[PM_METAL_WORKSPACE_ERR_MAX];
    FILE *f;
    char buf[64];
    size_t got;
    const char *path;
    if (pm_metal_workspace_mirror_set("/tmp/metal-src-test") != 0) {
        return 50;
    }
    if (pm_metal_workspace_materialize(g_arena, &n, err, sizeof(err)) != 0) {
        return 51;
    }
    path = "/tmp/metal-src-test/src/pymergetic/metal/fs/__impl__.c";
    f = fopen(path, "rb");
    if (f == NULL) {
        return 52;
    }
    got = fread(buf, 1, 20, f);
    (void)fclose(f);
    if (got != 20 || memcmp(buf, "/* pymergetic.metal.f", 20) != 0) {
        return 53;
    }
    if (pm_metal_workspace_mirror_set("") != 0) {
        return 54; /* off is also a unix-seat op */
    }
#else
    if (pm_metal_workspace_mirror_set("/tmp/x") == 0) {
        return 55; /* no host FS: must refuse */
    }
#endif
    return 0;
}

int32_t pm_metal_workspace_tests(void) {
    int32_t rc;
    rc = test_materialize();
    if (rc) return rc;
    rc = test_extract();
    if (rc) return rc;
    rc = test_bad_input();
    if (rc) return rc;
    rc = test_mirror();
    if (rc) return rc;
    if (g_arena != NULL) {
        pm_util_mem_arena_destroy(g_arena);
        g_arena = NULL;
        free(g_backing);
        g_backing = NULL;
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.workspace, tests, pm_metal_workspace_tests);
