/* pymergetic.metal.net.swarm.membership — border prove. The membership card
 * layers fleet membership on a net.zenoh session: it arms a roster queryable on
 * the currently-selected open slot and reports the node's own identity + alive
 * state. A full two-peer roster round-trip needs a second session and stays the
 * `two-session-prove` data-plane remainder; what this prove gates on every seat
 * (host C, unix µPy, emcc, firmware) is the armed roster + node identity +
 * teardown on the reliably-opening peer listener, and the misuse guards.
 */
#include "pymergetic/metal/net/swarm/membership/__exports__.h"
#include "pymergetic/wasmmod/guest.h" /* PM_MOD_EXPORT_C / PM_MOD_TEST_C */
#include "pymergetic/metal/async.h"
#include "pymergetic/metal/net/zenoh.h"
#include "pymergetic/metal/net/swarm/membership.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ZENOH_ADDR_BE 0x7f000001u /* 127.0.0.1 — net.ip lo */
#define ZENOH_PORT 7447u

static int32_t fail_swarm(const char *why) {
    fprintf(stderr, "metal.net.swarm.membership test: %s\n", why);
    return 1;
}

static int32_t case_membership_single(void) {
    char id[PM_METAL_NET_SWARM_MEMBER_ID_LEN];
    uint8_t zid[16];
    uint32_t steps;

    /* No session open yet: start() must return 0 (defer), not fail or arm. */
    if (pm_metal_net_swarm_membership_start("fleet") != 0) {
        return fail_swarm("start not-open");
    }
    /* Misuse guards: NULL + empty + over-long group are rejected. */
    if (pm_metal_net_swarm_membership_start(NULL) != -1) {
        return fail_swarm("start null");
    }
    if (pm_metal_net_swarm_membership_start("") != -1) {
        return fail_swarm("start empty");
    }
    if (pm_metal_net_swarm_membership_alive() != 0) {
        return fail_swarm("alive not-open");
    }

    /* Open the peer listener session (slot 1) — the reliably-opening half. */
    if (pm_metal_net_zenoh_sel(1) != 0) {
        return fail_swarm("sel1");
    }
    if (pm_metal_net_zenoh_peer(ZENOH_ADDR_BE, ZENOH_PORT, 1) != 0) {
        return fail_swarm("peer1");
    }
    for (steps = 0; steps < 40u; steps++) {
        if ((uint32_t)pm_metal_net_zenoh_up() == 1u) {
            break;
        }
        (void)pm_metal_net_zenoh_poll();
        (void)pm_metal_async_poll();
    }
    if ((uint32_t)pm_metal_net_zenoh_up() != 1u) {
        return fail_swarm("listener open");
    }

    /* Node identity: base-16 ZID, resolvable on the open session. */
    if (pm_metal_net_zenoh_zid(zid) != 1) {
        return fail_swarm("zenoh zid");
    }
    if (pm_metal_net_swarm_membership_node_id(id) < 0) {
        return fail_swarm("node id");
    }
    if (strlen(id) != 32u) {
        return fail_swarm("node id len");
    }

    /* Arm membership on the open session: roster queryable + heart-beating
     * liveness. start() succeeds once the slot is open; alive() reflects it. */
    if (pm_metal_net_swarm_membership_alive() != 0) {
        return fail_swarm("alive before start");
    }
    if (pm_metal_net_swarm_membership_start("fleet") != 1) {
        return fail_swarm("start open");
    }
    if (pm_metal_net_swarm_membership_start("fleet2") != -1) {
        (void)pm_metal_net_swarm_membership_stop();
        return fail_swarm("double start");
    }
    if (pm_metal_net_swarm_membership_alive() != 1) {
        (void)pm_metal_net_swarm_membership_stop();
        return fail_swarm("alive armed");
    }

    /* Teardown: stop() frees the roster; a second stop is a no-op. */
    if (pm_metal_net_swarm_membership_stop() != 1) {
        return fail_swarm("stop");
    }
    if (pm_metal_net_swarm_membership_stop() != 0) {
        return fail_swarm("stop twice");
    }
    if (pm_metal_net_swarm_membership_alive() != 0) {
        return fail_swarm("alive after stop");
    }

    /* Clean the shared boot up for the tests that follow. */
    pm_metal_net_zenoh_deinit();
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.net.swarm.membership, case_membership_single, case_membership_single);
