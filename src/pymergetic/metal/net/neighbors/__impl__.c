/* pymergetic.metal.net.neighbors -- neighbor discovery table.
 *
 * Tracks peers discovered via network. Uses a static array bounded by
 * PM_UTIL_LIMIT_C knob for max neighbors. Idempotent by zenoh_id.
 *
 * Lifecycle: init is a no-op (static array, always ready). deinit zeroes.
 * Registration, seen, drop, and reap are all O(n) -- the table is small.
 */
#include "pymergetic/metal/net/neighbors/__exports__.h"

#include "pymergetic/util/limits.h"

#include <string.h>
#include <stdint.h>

#ifndef PM_METAL_NEIGHBORS_MAX
#define PM_METAL_NEIGHBORS_MAX 32u
#endif

/* Static array -- always available, no arena dependency. */
static pm_metal_neighbor_t s_neighbors[PM_METAL_NEIGHBORS_MAX];
static uint32_t s_nn; /* live count */

/* Monotonic peer_id counter (0 = unused / none). */
static uint32_t s_next_peer_id = 1;

PM_UTIL_LIMIT_C(pm_neighbors_limit, pymergetic.metal.net.neighbors, count,
    PM_METAL_NEIGHBORS_MAX, 0u, &s_nn);

int32_t pm_metal_neighbors_init(pm_util_mem_arena_t *arena) {
    (void)arena;
    /* Static array -- always ready. */
    return 0;
}

void pm_metal_neighbors_deinit(void) {
    s_nn = 0;
    memset(s_neighbors, 0, sizeof(s_neighbors));
}

/* Idempotent: add a new neighbor or update an existing one (by zenoh_id).
 * Existing entries keep their peer_id; new entries get a fresh one.
 * Returns the peer_id on success, -1 on failure. */
int32_t pm_metal_neighbors_add(const char *zenoh_id, const char *host, uint32_t caps) {
    uint32_t idx;
    if (zenoh_id == NULL || host == NULL) {
        return -1;
    }
    /* Look for existing entry with same zenoh_id. */
    for (idx = 0; idx < s_nn; idx++) {
        if (s_neighbors[idx].alive &&
            strcmp(s_neighbors[idx].zenoh_id, zenoh_id) == 0) {
            /* Update. */
            strncpy(s_neighbors[idx].host, host,
                sizeof(s_neighbors[idx].host) - 1);
            s_neighbors[idx].host[sizeof(s_neighbors[idx].host) - 1] = '\0';
            s_neighbors[idx].caps = caps;
            s_neighbors[idx].alive = 1;
            return (int32_t)s_neighbors[idx].peer_id;
        }
    }
    /* New entry. */
    if (s_nn >= PM_METAL_NEIGHBORS_MAX) {
        return -1;
    }
    memset(&s_neighbors[s_nn], 0, sizeof(pm_metal_neighbor_t));
    s_neighbors[s_nn].peer_id = s_next_peer_id++;
    strncpy(s_neighbors[s_nn].zenoh_id, zenoh_id,
        sizeof(s_neighbors[s_nn].zenoh_id) - 1);
    s_neighbors[s_nn].zenoh_id[sizeof(s_neighbors[s_nn].zenoh_id) - 1] = '\0';
    strncpy(s_neighbors[s_nn].host, host,
        sizeof(s_neighbors[s_nn].host) - 1);
    s_neighbors[s_nn].host[sizeof(s_neighbors[s_nn].host) - 1] = '\0';
    s_neighbors[s_nn].caps = caps;
    s_neighbors[s_nn].alive = 1;
    s_nn++;
    return (int32_t)s_neighbors[s_nn - 1].peer_id;
}

int32_t pm_metal_neighbors_seen(const char *zenoh_id) {
    uint32_t i;
    if (zenoh_id == NULL) {
        return -1;
    }
    for (i = 0; i < s_nn; i++) {
        if (s_neighbors[i].alive &&
            strcmp(s_neighbors[i].zenoh_id, zenoh_id) == 0) {
            s_neighbors[i].alive = 1;
            return (int32_t)s_neighbors[i].peer_id;
        }
    }
    return -1;
}

int32_t pm_metal_neighbors_drop(const char *zenoh_id) {
    uint32_t i;
    if (zenoh_id == NULL) {
        return -1;
    }
    for (i = 0; i < s_nn; i++) {
        if (strcmp(s_neighbors[i].zenoh_id, zenoh_id) == 0) {
            /* Compact: swap with last */
            s_nn--;
            if (i < s_nn) {
                s_neighbors[i] = s_neighbors[s_nn];
            }
            memset(&s_neighbors[s_nn], 0, sizeof(pm_metal_neighbor_t));
            return 0;
        }
    }
    return -1;
}

uint32_t pm_metal_neighbors_count(void) {
    return s_nn;
}

int32_t pm_metal_neighbors_at(uint32_t idx, pm_metal_neighbor_t *out) {
    if (out == NULL || idx >= s_nn) {
        return -1;
    }
    *out = s_neighbors[idx];
    return 0;
}

int32_t pm_metal_neighbors_find(const char *zenoh_id) {
    uint32_t i;
    if (zenoh_id == NULL) {
        return -1;
    }
    for (i = 0; i < s_nn; i++) {
        if (strcmp(s_neighbors[i].zenoh_id, zenoh_id) == 0 &&
            s_neighbors[i].alive) {
            return (int32_t)i;
        }
    }
    return -1;
}

/* Reap neighbors no longer alive (alive=0 from drop or marked stale).
 * Compacts the array and returns number of entries removed. */
uint32_t pm_metal_neighbors_reap(int64_t timeout_us) {
    uint32_t i;
    uint32_t n;
    uint32_t reaped = 0;
    (void)timeout_us;
    n = s_nn;
    for (i = 0; i < n; i++) {
        if (!s_neighbors[i].alive) {
            n--;
            if (i < n) {
                s_neighbors[i] = s_neighbors[n];
            }
            memset(&s_neighbors[n], 0, sizeof(pm_metal_neighbor_t));
            reaped++;
            i--;
        }
    }
    s_nn = n;
    return reaped;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.net.neighbors, pm_metal_neighbors_add, pm_metal_neighbors_add, int32_t(const char *, const char *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.neighbors, pm_metal_neighbors_seen, pm_metal_neighbors_seen, int32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.metal.net.neighbors, pm_metal_neighbors_drop, pm_metal_neighbors_drop, int32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.metal.net.neighbors, pm_metal_neighbors_count, pm_metal_neighbors_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.neighbors, pm_metal_neighbors_at, pm_metal_neighbors_at, int32_t(uint32_t, pm_metal_neighbor_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.neighbors, pm_metal_neighbors_find, pm_metal_neighbors_find, int32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.metal.net.neighbors, pm_metal_neighbors_reap, pm_metal_neighbors_reap, uint32_t(int64_t));
PM_MOD_BOOT_C(pymergetic.metal.net.neighbors, pm_metal_neighbors_init, pm_metal_neighbors_deinit);
