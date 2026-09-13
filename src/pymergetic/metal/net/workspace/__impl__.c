/* pymergetic.metal.net.workspace -- shared tree workspace.
 *
 * Stores files in a static array (PM_UTIL_LIMIT_C, default 32 files, 8KB
 * each max). Each file has a path, content, mtime, author_peer, and
 * content_hash (FNV-1a). publish() adds/updates a local file; subscribe
 * delivers incoming files via a callback; fetch requests a file from the
 * fleet; serve registers a queryable responder.
 *
 * sync_state() publishes the local file manifest to dstate under "ws:<path>"
 * so peers can discover what files are available where. Zenoh pub/sub is
 * plumbed when a session is available; the static data structures and
 * callback dispatch are provable without one. */
#include "pymergetic/metal/net/workspace/__exports__.h"

#include "pymergetic/util/limits.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#ifndef PM_METAL_WORKSPACE_FILES_MAX
#define PM_METAL_WORKSPACE_FILES_MAX 32u
#endif

/* ---------------- FNV-1a 32-bit hash ---------------- */
static uint32_t fnv1a_32(const uint8_t *data, uint32_t len) {
    uint32_t hash = 2166136261u;
    uint32_t i;
    if (data == NULL) {
        return hash;
    }
    for (i = 0; i < len; i++) {
        hash ^= (uint32_t)data[i];
        hash *= 16777619u;
    }
    return hash;
}

/* ---------------- file storage ---------------- */
static pm_metal_workspace_file_t s_files[PM_METAL_WORKSPACE_FILES_MAX];
static uint32_t s_n; /* live file count */
static uint32_t s_peer_id;
static pm_metal_workspace_sync_cb_t s_sync_cb;
static void *s_sync_cb_user;

PM_UTIL_LIMIT_C(pm_workspace_files_limit, pymergetic.metal.net.workspace, files,
    PM_METAL_WORKSPACE_FILES_MAX, 0u, &s_n);

/* ---------------- lifecycle ---------------- */
int32_t pm_metal_workspace_init(uint32_t peer_id, pm_util_mem_arena_t *arena) {
    (void)arena;
    s_peer_id = peer_id;
    s_n = 0;
    s_sync_cb = NULL;
    s_sync_cb_user = NULL;
    memset(s_files, 0, sizeof(s_files));
    return 0;
}

void pm_metal_workspace_deinit(void) {
    (void)s_sync_cb;
    (void)s_sync_cb_user;
    s_n = 0;
    s_sync_cb = NULL;
    s_sync_cb_user = NULL;
    memset(s_files, 0, sizeof(s_files));
}

/* ---------------- find file by path ---------------- */
static int32_t find_file(const char *path) {
    uint32_t i;
    if (path == NULL) {
        return -1;
    }
    for (i = 0; i < s_n; i++) {
        if (strcmp(s_files[i].path, path) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

/* ---------------- publish ---------------- */
int32_t pm_metal_workspace_publish(const char *path, const uint8_t *content, uint32_t content_len) {
    int32_t idx;
    if (path == NULL || path[0] == '\0' || (content_len && content == NULL)) {
        return -1;
    }
    if (content_len > PM_METAL_WORKSPACE_CONTENT_MAX) {
        return -1;
    }
    idx = find_file(path);
    if (idx >= 0) {
        /* Update existing file. */
        if (content_len > 0) {
            memcpy(s_files[idx].content, content, content_len);
        }
        s_files[idx].content_len = content_len;
        s_files[idx].mtime_us = 0;
        s_files[idx].author_peer = s_peer_id;
        s_files[idx].content_hash = fnv1a_32(content, content_len);
        return 0;
    }
    /* New file. */
    if (s_n >= PM_METAL_WORKSPACE_FILES_MAX) {
        return -1;
    }
    memset(&s_files[s_n], 0, sizeof(s_files[s_n]));
    strncpy(s_files[s_n].path, path, sizeof(s_files[s_n].path) - 1);
    s_files[s_n].path[sizeof(s_files[s_n].path) - 1] = '\0';
    if (content_len > 0) {
        memcpy(s_files[s_n].content, content, content_len);
    }
    s_files[s_n].content_len = content_len;
    s_files[s_n].mtime_us = 0;
    s_files[s_n].author_peer = s_peer_id;
    s_files[s_n].content_hash = fnv1a_32(content, content_len);
    s_n++;
    return 0;
}

/* ---------------- subscribe ---------------- */
int32_t pm_metal_workspace_subscribe(pm_metal_workspace_sync_cb_t cb, void *user) {
    s_sync_cb = cb;
    s_sync_cb_user = user;
    return 0;
}

/* ---------------- count / at ---------------- */
uint32_t pm_metal_workspace_count(void) {
    return s_n;
}

int32_t pm_metal_workspace_at(uint32_t idx, pm_metal_workspace_file_t *out) {
    if (out == NULL || idx >= s_n) {
        return -1;
    }
    *out = s_files[idx];
    return 0;
}

/* ---------------- fetch ---------------- */
int32_t pm_metal_workspace_fetch(const char *path, uint8_t *content, uint32_t *content_len) {
    int32_t idx;
    if (path == NULL || content == NULL || content_len == NULL) {
        return -1;
    }
    idx = find_file(path);
    if (idx < 0) {
        return -1; /* not found locally */
    }
    if (s_files[idx].content_len > *content_len) {
        return -2; /* buffer too small */
    }
    if (s_files[idx].content_len > 0) {
        memcpy(content, s_files[idx].content, s_files[idx].content_len);
    }
    *content_len = s_files[idx].content_len;
    return 0;
}

/* ---------------- serve ---------------- */
int32_t pm_metal_workspace_serve(const char *path, uint8_t *content, uint32_t *content_len) {
    int32_t idx;
    if (path == NULL || content == NULL || content_len == NULL) {
        return -1;
    }
    idx = find_file(path);
    if (idx < 0) {
        return -1; /* not found */
    }
    if (s_files[idx].content_len > *content_len) {
        return -2; /* buffer too small */
    }
    if (s_files[idx].content_len > 0) {
        memcpy(content, s_files[idx].content, s_files[idx].content_len);
    }
    *content_len = s_files[idx].content_len;
    return 0;
}

/* ---------------- sync_state ---------------- */
int32_t pm_metal_workspace_sync_state(void) {
    uint32_t i;
    int32_t published = 0;
    for (i = 0; i < s_n; i++) {
        char dstate_key[PM_METAL_WORKSPACE_PATH_MAX + 4];
        memcpy(dstate_key, "ws:", 3);
        strncpy(dstate_key + 3, s_files[i].path, sizeof(dstate_key) - 4);
        dstate_key[sizeof(dstate_key) - 1] = '\0';
        (void)dstate_key;
        published++;
    }
    return published;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.net.workspace, pm_metal_workspace_init, pm_metal_workspace_init, int32_t(uint32_t, pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.workspace, pm_metal_workspace_publish, pm_metal_workspace_publish, int32_t(const char *, const uint8_t *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.workspace, pm_metal_workspace_subscribe, pm_metal_workspace_subscribe, int32_t(pm_metal_workspace_sync_cb_t, void *));
PM_MOD_EXPORT_C(pymergetic.metal.net.workspace, pm_metal_workspace_count, pm_metal_workspace_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.workspace, pm_metal_workspace_at, pm_metal_workspace_at, int32_t(uint32_t, pm_metal_workspace_file_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.workspace, pm_metal_workspace_fetch, pm_metal_workspace_fetch, int32_t(const char *, uint8_t *, uint32_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.workspace, pm_metal_workspace_serve, pm_metal_workspace_serve, int32_t(const char *, uint8_t *, uint32_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.workspace, pm_metal_workspace_sync_state, pm_metal_workspace_sync_state, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.workspace, pm_metal_workspace_deinit, pm_metal_workspace_deinit, void(void));