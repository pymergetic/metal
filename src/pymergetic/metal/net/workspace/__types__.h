/* pymergetic.metal.net.workspace -- shared tree workspace across peers.
 *
 * Every peer can publish files to, subscribe to, fetch, and serve files
 * in a shared workspace. File contents are stored in a static array
 * bounded by PM_UTIL_LIMIT_C (default 32 files, 8KB each max).
 * File metadata (path, mtime, author, hash) is tracked in dstate entries
 * keyed by "ws:<path>" so peers know who has what.
 *
 * Zenoh faces: publish = put on "workspace/<path>", subscribe on
 * "workspace/ *", fetch = query on "workspace/<path>" (first responder
 * wins), serve = queryable on "workspace/ *" (responds with file content).
 */
#ifndef PYMERGETIC_METAL_NET_WORKSPACE_TYPES_H
#define PYMERGETIC_METAL_NET_WORKSPACE_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_WORKSPACE_PATH_MAX 256
#define PM_METAL_WORKSPACE_CONTENT_MAX 8192

#ifndef PM_METAL_WORKSPACE_FILES_MAX
#define PM_METAL_WORKSPACE_FILES_MAX 32u
#endif

typedef struct pm_metal_workspace_file {
    char path[PM_METAL_WORKSPACE_PATH_MAX];
    uint8_t content[PM_METAL_WORKSPACE_CONTENT_MAX];
    uint32_t content_len;
    uint64_t mtime_us;        /* modification timestamp */
    uint32_t author_peer;     /* which peer last modified */
    uint32_t content_hash;    /* FNV-1a hash for dedup */
} pm_metal_workspace_file_t;

typedef int32_t (*pm_metal_workspace_sync_cb_t)(
    const char *path, const uint8_t *content, uint32_t content_len,
    uint64_t mtime, uint32_t author_peer, void *user);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_WORKSPACE_TYPES_H */