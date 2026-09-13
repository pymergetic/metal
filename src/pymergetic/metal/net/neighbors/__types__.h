/* pymergetic.metal.net.neighbors — neighbor discovery table.
 *
 * Tracks peers discovered via network (zenoh scouting, gossip, or manual
 * announce). Each neighbor has a zenoh UUID, hostname, capability flags,
 * and a last-seen timestamp for timeout-based reaping.
 *
 * Registration is idempotent by zenoh_id: adding a known ID updates the
 * host/caps and bumps last_seen_us. A background loop calls seen() on
 * each contact and reap() to prune stale entries.
 */
#ifndef PYMERGETIC_METAL_NET_NEIGHBORS_TYPES_H
#define PYMERGETIC_METAL_NET_NEIGHBORS_TYPES_H

#include <stdint.h>

#include "pymergetic/util/mem/__types__.h"

/* Capability flags — bits a peer advertises. */
#define PM_METAL_NEIGHBOR_CAP_HAS_BUILD  1u
#define PM_METAL_NEIGHBOR_CAP_HAS_JIT    2u
#define PM_METAL_NEIGHBOR_CAP_HAS_ZENOH  4u
#define PM_METAL_NEIGHBOR_CAP_HAS_SERVICES 8u

typedef struct pm_metal_neighbor {
    uint32_t peer_id;       /* opaque peer identifier (0 = none/invalid) */
    char host[64];          /* hostname or IP */
    char zenoh_id[128];     /* zenoh peer UUID string */
    uint32_t caps;          /* capability flags bitmask */
    int64_t last_seen_us;   /* monotonic timestamp of last contact */
    int32_t alive;          /* 1 = still connected, 0 = timed out */
} pm_metal_neighbor_t;

#endif /* PYMERGETIC_METAL_NET_NEIGHBORS_TYPES_H */