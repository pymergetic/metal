/* pymergetic.metal.net.swarm.discovery — border prove. The discovery card
 * scouts the member group (SCOUT → HELLO) and answers SCOUTs so peers discover
 * it. This prove gates the full discovery round-trip on one seat:
 *   1. With no joined answerer the scout resolves 0 — nothing is discoverable.
 *   2. Armed (answer_on) the node answers; a scout round-trips a HELLO carrying
 *      the local ZID + WhatAmI=Peer.
 *   3. A second arm is rejected; teardown leaves no group member behind.
 * The prove drives the cooperative answer pump between scout steps, exactly as
 * net.zenoh's role does, so it passes on every seat that links net.zenoh.
 */
#include "pymergetic/metal/net/swarm/discovery/__exports__.h"
#include "pymergetic/wasmmod/guest.h" /* PM_MOD_EXPORT_C / PM_MOD_TEST_C */
#include "pymergetic/metal/async.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/net/zenoh.h"
#include "pymergetic/metal/net/swarm/discovery.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail_disc(const char *why) {
    fprintf(stderr, "metal.net.swarm.discovery test: %s\n", why);
    return 1;
}

static int32_t case_discovery_roundtrip(void) {
    uint8_t peer_id[PM_METAL_NET_SWARM_PEER_ID_LEN];
    uint8_t local_zid[PM_METAL_NET_ZENOH_ZID_LEN];
    uint8_t whatami = 0;
    uint32_t steps;
    int32_t rc;

    /* Gate 1: no joined answerer -> a scout must not resolve (0). */
    rc = pm_metal_net_swarm_discovery_scout(peer_id, &whatami);
    if (rc != 0) {
        return fail_disc("scout resolved with no answerer");
    }
    if (pm_metal_net_swarm_discovery_answer_on() != 1) {
        return fail_disc("answer on");
    }
    if (pm_metal_net_swarm_discovery_answer_on() != 0) {
        (void)pm_metal_net_swarm_discovery_answer_off();
        return fail_disc("second answer accepted");
    }
    if (pm_metal_net_zenoh_zid(local_zid) != 1) {
        (void)pm_metal_net_swarm_discovery_answer_off();
        return fail_disc("local zid");
    }

    /* Gate 2: with the answerer live, the scout round-trips. Drive the
     * cooperative step (scout + answer pump + net.ip) until a HELLO lands. */
    rc = 0;
    for (steps = 0; steps < 24u; steps++) {
        rc = pm_metal_net_swarm_discovery_scout(peer_id, &whatami);
        (void)pm_metal_net_swarm_discovery_pump();
        (void)pm_metal_net_ip_pump();
        (void)pm_metal_async_poll();
        if (rc == 1) {
            break;
        }
    }
    (void)pm_metal_net_swarm_discovery_answer_off();
    if (rc != 1) {
        return fail_disc("scout no hello");
    }
    if (whatami != 2u /* Z_WHATAMI_PEER */) {
        return fail_disc("scout whatami");
    }
    if (memcmp(peer_id, local_zid, PM_METAL_NET_SWARM_PEER_ID_LEN) != 0) {
        return fail_disc("scout zid mismatch");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.net.swarm.discovery, case_discovery_roundtrip, case_discovery_roundtrip);
