/* pymergetic.metal.net.neighbors.sync_services -- remote service synchronization.
 *
 * When a new neighbor is discovered, query it for its services via the
 * services-to-zenoh bridge (pm_metal_services_discover_peer_services) and
 * register them locally as remote services. The actual zenoh query/reply
 * wire protocol lives in the services card; this file orchestrates the
 * per-neighbor and all-neighbors sync loops.
 */
#include "pymergetic/metal/net/neighbors/__exports__.h"
#include "pymergetic/metal/services/__exports__.h"

#include <stdint.h>
#include <string.h>

int32_t pm_metal_neighbors_sync_services(uint32_t peer_id) {
    pm_metal_neighbor_t nb;
    uint32_t n;
    uint32_t i;

    n = pm_metal_neighbors_count();
    for (i = 0; i < n; i++) {
        if (pm_metal_neighbors_at(i, &nb) != 0) {
            continue;
        }
        if (nb.peer_id != peer_id) {
            continue;
        }
        /* Found the neighbor. Query its services through the bridge.
         * The services card handles the zenoh queryable/reply wire
         * protocol and calls pm_metal_services_register_remote() for
         * each discovered service. */
        (void)pm_metal_services_discover_peer_services(nb.zenoh_id, peer_id);
        return 0;
    }
    return -1;
}

int32_t pm_metal_neighbors_sync_all(void) {
    pm_metal_neighbor_t nb;
    uint32_t n;
    uint32_t i;
    int32_t rc = 0;

    n = pm_metal_neighbors_count();
    for (i = 0; i < n; i++) {
        if (pm_metal_neighbors_at(i, &nb) != 0) {
            continue;
        }
        if (pm_metal_neighbors_sync_services(nb.peer_id) != 0) {
            rc = -1;
        }
    }
    return rc;
}