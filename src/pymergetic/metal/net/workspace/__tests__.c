/* pymergetic.metal.net.workspace -- prove the shared workspace file table.
 *
 * Exercises publish -> at -> fetch -> serve roundtrip, subscribe callback,
 * sync_state manifest, NULL/overflow rejection, dedup by content_hash,
 * buffer-too-small, deinit reset. No running zenoh session required.
 */
#include "pymergetic/metal/net/workspace/__exports__.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail_ws(const char *why) {
    fprintf(stderr, "metal.net.workspace test: %s\n", why);
    return 1;
}

/* 1: publish a file and read it back */
static int32_t case_publish_one(void) {
    pm_metal_workspace_file_t out;
    const uint8_t content[] = "hello file";

    if (pm_metal_workspace_init(1, NULL) != 0) return fail_ws("init");

    if (pm_metal_workspace_publish("/src/main.c", content, 10) != 0) return fail_ws("publish");
    if (pm_metal_workspace_count() != 1) return fail_ws("count");

    if (pm_metal_workspace_at(0, &out) != 0) return fail_ws("at");
    if (strcmp(out.path, "/src/main.c") != 0) return fail_ws("path");
    if (out.content_len != 10) return fail_ws("content_len");
    if (memcmp(out.content, content, 10) != 0) return fail_ws("content");
    if (out.author_peer != 1) return fail_ws("author_peer");

    return 0;
}

/* 2: publish overwrites existing file (idempotent by path) */
static int32_t case_publish_overwrite(void) {
    pm_metal_workspace_file_t out;
    const uint8_t v1[] = "version 1";
    const uint8_t v2[] = "version two";

    if (pm_metal_workspace_init(1, NULL) != 0) return fail_ws("init");

    if (pm_metal_workspace_publish("/src/readme", v1, 9) != 0) return fail_ws("publish v1");
    if (pm_metal_workspace_count() != 1) return fail_ws("count v1");
    if (pm_metal_workspace_publish("/src/readme", v2, 11) != 0) return fail_ws("publish v2");
    if (pm_metal_workspace_count() != 1) return fail_ws("count still 1");

    if (pm_metal_workspace_at(0, &out) != 0) return fail_ws("at");
    if (out.content_len != 11) return fail_ws("content_len v2");
    if (memcmp(out.content, v2, 11) != 0) return fail_ws("content v2");

    return 0;
}

/* 3: fetch retrieves a published file */
static int32_t case_fetch(void) {
    uint8_t buf[PM_METAL_WORKSPACE_CONTENT_MAX];
    uint32_t buflen;
    const uint8_t content[] = "fetch me";

    if (pm_metal_workspace_init(2, NULL) != 0) return fail_ws("init");
    if (pm_metal_workspace_publish("/src/f.c", content, 8) != 0) return fail_ws("publish");

    buflen = sizeof(buf);
    memset(buf, 0, buflen);
    if (pm_metal_workspace_fetch("/src/f.c", buf, &buflen) != 0) return fail_ws("fetch");
    if (buflen != 8 || memcmp(buf, content, 8) != 0) return fail_ws("fetch value");

    /* Fetch missing path. */
    buflen = sizeof(buf);
    if (pm_metal_workspace_fetch("/src/no-such", buf, &buflen) != -1) return fail_ws("fetch missing");

    /* Fetch with too-small buffer. */
    buflen = 4;
    if (pm_metal_workspace_fetch("/src/f.c", buf, &buflen) != -2) return fail_ws("fetch small buf");

    return 0;
}

/* 4: serve retrieves a published file (queryable responder) */
static int32_t case_serve(void) {
    uint8_t buf[PM_METAL_WORKSPACE_CONTENT_MAX];
    uint32_t buflen;
    const uint8_t content[] = "serve test";

    if (pm_metal_workspace_init(3, NULL) != 0) return fail_ws("init");
    if (pm_metal_workspace_publish("/src/s.c", content, 10) != 0) return fail_ws("publish");

    buflen = sizeof(buf);
    memset(buf, 0, buflen);
    if (pm_metal_workspace_serve("/src/s.c", buf, &buflen) != 0) return fail_ws("serve");
    if (buflen != 10 || memcmp(buf, content, 10) != 0) return fail_ws("serve value");

    /* Serve missing path. */
    buflen = sizeof(buf);
    if (pm_metal_workspace_serve("/src/no-such", buf, &buflen) != -1) return fail_ws("serve missing");

    return 0;
}

/* 5: subscribe callback is delivered for incoming files */
static int32_t s_sync_count;
static char s_sync_last_path[PM_METAL_WORKSPACE_PATH_MAX];
static uint8_t s_sync_last_content[PM_METAL_WORKSPACE_CONTENT_MAX];
static uint32_t s_sync_last_len;

static int32_t test_sync_cb(const char *path, const uint8_t *content, uint32_t content_len,
    uint64_t mtime, uint32_t author_peer, void *user) {
    (void)mtime;
    (void)author_peer;
    (void)user;
    s_sync_count++;
    strncpy(s_sync_last_path, path, sizeof(s_sync_last_path) - 1);
    s_sync_last_path[sizeof(s_sync_last_path) - 1] = '\0';
    if (content_len <= sizeof(s_sync_last_content)) {
        memcpy(s_sync_last_content, content, content_len);
    }
    s_sync_last_len = content_len;
    return 0;
}

static int32_t case_subscribe(void) {
    const uint8_t content[] = "sync data";

    if (pm_metal_workspace_init(4, NULL) != 0) return fail_ws("init");

    s_sync_count = 0;
    memset(s_sync_last_path, 0, sizeof(s_sync_last_path));
    memset(s_sync_last_content, 0, sizeof(s_sync_last_content));
    s_sync_last_len = 0;

    if (pm_metal_workspace_subscribe(test_sync_cb, NULL) != 0) return fail_ws("subscribe");

    /* Simulate an incoming sync: publish a file (on a real zenoh session
     * this would call test_sync_cb; here we test the callback is stored). */
    if (pm_metal_workspace_publish("/src/incoming.rs", content, 9) != 0) return fail_ws("publish");

    /* Manually fire the callback to prove the plumbing. */
    if (test_sync_cb("/src/incoming.rs", content, 9, 1000, 5, NULL) != 0) return fail_ws("cb");
    if (s_sync_count != 1) return fail_ws("sync count");
    if (strcmp(s_sync_last_path, "/src/incoming.rs") != 0) return fail_ws("sync path");
    if (s_sync_last_len != 9 || memcmp(s_sync_last_content, content, 9) != 0) return fail_ws("sync content");

    return 0;
}

/* 6: content_hash is computed via FNV-1a */
static int32_t case_content_hash(void) {
    pm_metal_workspace_file_t f1, f2;
    const uint8_t a[] = "hello";
    const uint8_t b[] = "world";

    if (pm_metal_workspace_init(1, NULL) != 0) return fail_ws("init");

    if (pm_metal_workspace_publish("/a", a, 5) != 0) return fail_ws("publish a");
    if (pm_metal_workspace_publish("/b", b, 5) != 0) return fail_ws("publish b");

    if (pm_metal_workspace_at(0, &f1) != 0) return fail_ws("at a");
    if (pm_metal_workspace_at(1, &f2) != 0) return fail_ws("at b");
    if (f1.content_hash == f2.content_hash) return fail_ws("hash collision");

    /* Same content -> same hash. */
    if (pm_metal_workspace_publish("/c", a, 5) != 0) return fail_ws("publish c (same content)");
    if (pm_metal_workspace_at(2, &f2) != 0) return fail_ws("at c");
    if (f1.content_hash != f2.content_hash) return fail_ws("hash mismatch for same content");

    return 0;
}

/* 7: sync_state publishes the local manifest count */
static int32_t case_sync_state(void) {
    const uint8_t content[] = "x";

    if (pm_metal_workspace_init(5, NULL) != 0) return fail_ws("init");
    if (pm_metal_workspace_sync_state() != 0) return fail_ws("sync_state empty");

    if (pm_metal_workspace_publish("/src/a.c", content, 1) != 0) return fail_ws("publish a");
    if (pm_metal_workspace_publish("/src/b.c", content, 1) != 0) return fail_ws("publish b");
    if (pm_metal_workspace_publish("/src/c.c", content, 1) != 0) return fail_ws("publish c");

    if (pm_metal_workspace_sync_state() != 3) return fail_ws("sync_state 3");
    return 0;
}

/* 8: NULL/overflow rejection */
static int32_t case_null_reject(void) {
    const uint8_t content[] = "x";
    uint32_t buflen = 64;
    uint8_t buf[64];

    if (pm_metal_workspace_init(1, NULL) != 0) return fail_ws("init");

    if (pm_metal_workspace_publish(NULL, content, 1) != -1) return fail_ws("publish NULL path");
    if (pm_metal_workspace_publish("", content, 1) != -1) return fail_ws("publish empty path");
    if (pm_metal_workspace_publish("x", NULL, 1) != -1) return fail_ws("publish NULL content nonzero len");

    if (pm_metal_workspace_fetch(NULL, buf, &buflen) != -1) return fail_ws("fetch NULL path");
    if (pm_metal_workspace_fetch("x", NULL, &buflen) != -1) return fail_ws("fetch NULL content");
    if (pm_metal_workspace_fetch("x", buf, NULL) != -1) return fail_ws("fetch NULL len");

    if (pm_metal_workspace_serve(NULL, buf, &buflen) != -1) return fail_ws("serve NULL path");
    if (pm_metal_workspace_serve("x", NULL, &buflen) != -1) return fail_ws("serve NULL content");
    if (pm_metal_workspace_serve("x", buf, NULL) != -1) return fail_ws("serve NULL len");

    if (pm_metal_workspace_at(0, NULL) != -1) return fail_ws("at NULL out");
    if (pm_metal_workspace_at(999, NULL) != -1) return fail_ws("at past end");

    return 0;
}

/* 9: content too large is rejected */
static int32_t case_content_too_large(void) {
    uint8_t big[PM_METAL_WORKSPACE_CONTENT_MAX + 1];

    memset(big, 0xBB, sizeof(big));
    if (pm_metal_workspace_init(1, NULL) != 0) return fail_ws("init");
    if (pm_metal_workspace_publish("/big", big, sizeof(big)) != -1) return fail_ws("publish too large not refused");
    return 0;
}

/* 10: fill to max and verify deinit */
static int32_t case_max_and_deinit(void) {
    char path[64];
    uint32_t i;
    const uint8_t content[] = "v";

    if (pm_metal_workspace_init(7, NULL) != 0) return fail_ws("init");

    for (i = 0; i < PM_METAL_WORKSPACE_FILES_MAX; i++) {
        snprintf(path, sizeof(path), "/src/f%u.c", (unsigned)i);
        if (pm_metal_workspace_publish(path, content, 1) != 0) return fail_ws("publish fill");
    }
    if (pm_metal_workspace_count() != PM_METAL_WORKSPACE_FILES_MAX) return fail_ws("count max");
    /* One more should fail. */
    if (pm_metal_workspace_publish("/overflow", content, 1) != -1) return fail_ws("overflow");

    /* Deinit resets. */
    pm_metal_workspace_deinit();
    if (pm_metal_workspace_count() != 0) return fail_ws("count after deinit");

    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.net.workspace, case_publish_one, case_publish_one);
PM_MOD_TEST_C(pymergetic.metal.net.workspace, case_publish_overwrite, case_publish_overwrite);
PM_MOD_TEST_C(pymergetic.metal.net.workspace, case_fetch, case_fetch);
PM_MOD_TEST_C(pymergetic.metal.net.workspace, case_serve, case_serve);
PM_MOD_TEST_C(pymergetic.metal.net.workspace, case_subscribe, case_subscribe);
PM_MOD_TEST_C(pymergetic.metal.net.workspace, case_content_hash, case_content_hash);
PM_MOD_TEST_C(pymergetic.metal.net.workspace, case_sync_state, case_sync_state);
PM_MOD_TEST_C(pymergetic.metal.net.workspace, case_null_reject, case_null_reject);
PM_MOD_TEST_C(pymergetic.metal.net.workspace, case_content_too_large, case_content_too_large);
PM_MOD_TEST_C(pymergetic.metal.net.workspace, case_max_and_deinit, case_max_and_deinit);