/* pymergetic.metal.net.swarm.discovery — fleet peer discovery on a net.zenoh
 * transport. A node scouts the member discovery group for peers (SCOUT →
 * HELLO) and answers SCOUTs addressed to it so peers discover it back. This
 * card is a thin orchestrator over net.zenoh's proven scout/hello primitives:
 * each face drives the matching net.zenoh face and forwards the discovered
 * peer identity. Single language (C), single card — no second send/recv.
 */
#include "pymergetic/metal/net/swarm/discovery/__exports__.h"

#include "pymergetic/wasmmod/guest.h" /* PM_MOD_EXPORT_C */
#include "pymergetic/metal/net/zenoh.h"
#include "pymergetic/metal/net/swarm/discovery/__types__.h"

#include <stdint.h>
#include <string.h>

/* Zenoh wire roles reused by the discovery face (see net.zenoh scout). */
#define PM_NET_SWARM_WHAT_PEER 2u
#define PM_NET_SWARM_WHATAMI_PEER 2u

/* Scout the member group once. Returns 1 with a discovered peer's ZID +
 * whatami filled when a live peer answers the HELLO; 0 when no peer is up yet
 * (caller retries after discovery_pump); -1 on misuse. Cooperative and
 * bounded: net.zenoh's scout spins net.ip + the answerer for a fixed budget, so
 * this never blocks the seat. */
int32_t pm_metal_net_swarm_discovery_scout(uint8_t peer_id[PM_METAL_NET_SWARM_PEER_ID_LEN],
                                           uint8_t *peer_whatami) {
    uint8_t zid[PM_METAL_NET_ZENOH_ZID_LEN];
    uint8_t whatami = 0;
    int32_t rc;
    if (peer_id == NULL) {
        return -1;
    }
    rc = pm_metal_net_zenoh_scout(PM_NET_SWARM_WHAT_PEER, zid, &whatami);
    if (rc != 1) {
        if (peer_whatami != NULL) {
            *peer_whatami = 0;
        }
        return rc; /* 0 = no peer yet (cooperative), -1 = misuse */
    }
    memcpy(peer_id, zid, PM_METAL_NET_SWARM_PEER_ID_LEN);
    if (peer_whatami != NULL) {
        *peer_whatami = whatami;
    }
    return 1;
}

/* Arm this node as a discoverable member: it answers SCOUTs addressed to the
 * group with a HELLO carrying its local ZID + WhatAmI=Peer. One answerer at a
 * time. Returns 1 once armed, 0 when an answerer is already armed, -1 on
 * failure (e.g. no joinable multicast group on the seat). */
int32_t pm_metal_net_swarm_discovery_answer_on(void) {
    int32_t rc = pm_metal_net_zenoh_scout_answer_on();
    if (rc == 0) {
        return 1; /* armed */
    }
    if (rc == -1) {
        return 0; /* already armed (or bind/join failure — see zenoh) */
    }
    return -1;
}

void pm_metal_net_swarm_discovery_answer_off(void) {
    pm_metal_net_zenoh_scout_answer_off();
}

/* Drive the discovery step: pump net.ip + the local SCOUT→HELLO answerer so a
 * scout that landed gets its HELLO, and a pending scout() sees the reply. Call
 * in the card's poll() step. Returns 1 when a HELLO was sent this step, else 0.
 */
int32_t pm_metal_net_swarm_discovery_pump(void) {
    return pm_metal_net_zenoh_scout_answer_pump();
}

PM_MOD_EXPORT_C(pymergetic.metal.net.swarm.discovery, pm_metal_net_swarm_discovery_scout,
                pm_metal_net_swarm_discovery_scout,
                int32_t(uint8_t[PM_METAL_NET_SWARM_PEER_ID_LEN], uint8_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.swarm.discovery, pm_metal_net_swarm_discovery_answer_on,
                pm_metal_net_swarm_discovery_answer_on, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.swarm.discovery, pm_metal_net_swarm_discovery_answer_off,
                pm_metal_net_swarm_discovery_answer_off, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.swarm.discovery, pm_metal_net_swarm_discovery_pump,
                pm_metal_net_swarm_discovery_pump, int32_t(void));
