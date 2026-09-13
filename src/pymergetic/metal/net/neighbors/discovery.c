/* pymergetic.metal.net.neighbors.discovery -- zenoh-based neighbor discovery loop.
 *
 * Runs a cooperative background task that periodically scouts for zenoh peers
 * and updates the neighbor table. Each scout round-trip (UDP multicast
 * SCOUT/HELLO) resolves one peer; the scan loops until no more peers
 * answer within the bounded timeout.
 */
#include "pymergetic/metal/net/neighbors/__exports__.h"
#include "pymergetic/metal/net/zenoh/__exports__.h"
#include "pymergetic/metal/coop/__exports__.h"
#include "pymergetic/util/mem.h"

#include <stdint.h>
#include <string.h>

/* Max peers to resolve in one scan before yielding (scout is O(1) each, but
 * looping too many without yielding starves other tasks). A P2P mesh is
 * small; 8 scouts per scan keeps it cooperative. */
#define PM_METAL_NEIGHBORS_DISCOVER_MAX_PER_SCAN 8u

static pm_metal_coop_task_t *s_discover_task;
static uint32_t s_discover_interval_us;

/* Hex-encode 16 bytes into a 33-byte string (32 hex + NUL). */
static void hex_zid(const uint8_t *bin, char *out) {
    static const char hex[] = "0123456789abcdef";
    uint32_t i;
    for (i = 0; i < 16; i++) {
        out[i * 2] = hex[bin[i] >> 4];
        out[i * 2 + 1] = hex[bin[i] & 0x0f];
    }
    out[32] = '\0';
}

/* Run one discovery scan: scout all peers, update neighbor table.
 * Returns number of new peers found. */
int32_t pm_metal_neighbors_discover_scan(void) {
    uint8_t zid_bin[16];
    uint8_t whatami;
    int32_t rc;
    uint32_t scanned;
    int32_t new_peers = 0;
    char zid_str[33];
    /* Zenoh WhatAmI filter: 2 = Peer. Scout for peers only. */
    const uint8_t what = 2;

    for (scanned = 0; scanned < PM_METAL_NEIGHBORS_DISCOVER_MAX_PER_SCAN; scanned++) {
        (void)memset(zid_bin, 0, sizeof(zid_bin));
        rc = pm_metal_net_zenoh_scout(what, zid_bin, &whatami);
        if (rc <= 0) {
            /* 0 = timeout (no more peers), -1 = error. Stop scanning. */
            break;
        }
        /* Got a peer. Hex-encode the 16-byte ZID. */
        hex_zid(zid_bin, zid_str);
        /* Idempotent add: updates existing, adds new. Host unknown from scout. */
        rc = pm_metal_neighbors_add(zid_str, "", PM_METAL_NEIGHBOR_CAP_HAS_ZENOH);
        if (rc >= 0) {
            new_peers++;
        }
        /* Call seen() to bump the liveness timestamp. */
        (void)pm_metal_neighbors_seen(zid_str);
    }
    return new_peers;
}

/* Coop step: periodic discovery loop. Yields for interval_us between scans. */
static pm_metal_coop_status_t discover_step(pm_metal_coop_coro_t *self) {
    (void)pm_metal_neighbors_discover_scan();
    return pm_metal_coop_sleep_us(self, s_discover_interval_us);
}

/* Coro frame: step fn pointer is first member (auto_free contract). */
typedef struct {
    pm_metal_coop_coro_t coro;
} discover_frame_t;

int32_t pm_metal_neighbors_discover_start(uint32_t interval_us) {
    pm_metal_coop_coro_t *coro;
    pm_metal_coop_task_t *task;

    if (s_discover_task != NULL) {
        return -1; /* already running */
    }
    if (interval_us == 0) {
        return -1;
    }
    s_discover_interval_us = interval_us;

    coro = pm_metal_coop_coro_create(
        (pm_metal_coop_step_fn)discover_step, sizeof(discover_frame_t));
    if (coro == NULL) {
        return -1;
    }
    pm_metal_coop_coro_set_auto_free(coro);
    task = pm_metal_coop_create_task(coro);
    if (task == NULL) {
        /* Frame still allocated; free it manually since auto_free
         * only fires when the task reaches terminal state. */
        pm_util_mem_arena_t *arena = pm_metal_coop_arena();
        if (arena != NULL) {
            pm_util_mem_free(arena, coro);
        }
        return -1;
    }
    s_discover_task = task;
    return 0;
}

void pm_metal_neighbors_discover_stop(void) {
    if (s_discover_task != NULL) {
        (void)pm_metal_coop_task_reclaim(s_discover_task);
        s_discover_task = NULL;
    }
    s_discover_interval_us = 0;
}