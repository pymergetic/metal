/* pymergetic.metal.net.swarm.task — border prove. The task card layers
 * distributed work dispatch on a net.zenoh session: offer (a probe queryable),
 * declare (an exec subscription), dispatch (a put). A full two-peer dispatch
 * round-trip needs a second session and stays the `two-session-prove`
 * data-plane remainder; what this prove gates on every seat (host C, unix µPy,
 * emcc, firmware) is the armed offer + armed declare + dispatch put + the
 * off/on state machine + teardown on the reliably-opening peer listener, and
 * the misuse guards.
 */
#include "pymergetic/metal/net/swarm/task/__exports__.h"
#include "pymergetic/wasmmod/guest.h" /* PM_MOD_TEST_C */
#include "pymergetic/metal/async.h"
#include "pymergetic/metal/net/zenoh.h"
#include "pymergetic/metal/net/swarm/task.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ZENOH_ADDR_BE 0x7f000001u /* 127.0.0.1 — net.ip lo */
#define ZENOH_PORT 7447u

static int32_t fail_task(const char *why) {
    fprintf(stderr, "metal.net.swarm.task test: %s\n", why);
    return 1;
}

/* Host offer callback: accept any job tagged with an odd first byte (dummy
 * policy so the prove exercises the probe path's verdict byte). */
static uint8_t offer_policy(const uint8_t *params, size_t plen, void *arg) {
    (void)arg;
    if (params == NULL || plen == 0 || (params[0] & 1)) {
        return PM_METAL_NET_SWARM_OFFER_ACCEPT;
    }
    return PM_METAL_NET_SWARM_OFFER_BUSY;
}

static void exec_recv(const uint8_t *payload, size_t plen, void *arg) {
    (void)payload;
    (void)plen;
    (void)arg;
}

static int32_t case_task_single(void) {
    static const uint8_t job[5] = {0x11, 0x22, 0x33, 0x44, 0x55};
    uint32_t steps;

    /* No session open yet: offer/declare/dispatch must defer (0), not arm. */
    if (pm_metal_net_swarm_task_offer("render", offer_policy, NULL) != 0) {
        return fail_task("offer not-open");
    }
    if (pm_metal_net_swarm_task_declare("render", exec_recv) != 0) {
        return fail_task("declare not-open");
    }
    if (pm_metal_net_swarm_task_dispatch("render", job, sizeof(job)) != 0) {
        return fail_task("dispatch not-open");
    }
    if (pm_metal_net_swarm_task_offering() != 0) {
        return fail_task("offering not-open");
    }
    if (pm_metal_net_swarm_task_declaring() != 0) {
        return fail_task("declaring not-open");
    }

    /* Misuse guards: NULL/empty/over-long verb, NULL cb, oversize payload. */
    if (pm_metal_net_swarm_task_offer(NULL, offer_policy, NULL) != -1) {
        return fail_task("offer null verb");
    }
    if (pm_metal_net_swarm_task_offer("", offer_policy, NULL) != -1) {
        return fail_task("offer empty verb");
    }
    if (pm_metal_net_swarm_task_offer("a", NULL, NULL) != -1) {
        return fail_task("offer null cb");
    }
    if (pm_metal_net_swarm_task_declare(NULL, exec_recv) != -1) {
        return fail_task("declare null verb");
    }
    if (pm_metal_net_swarm_task_declare("a", NULL) != -1) {
        return fail_task("declare null cb");
    }
    if (pm_metal_net_swarm_task_dispatch(NULL, job, 1) != -1) {
        return fail_task("dispatch null verb");
    }
    if (pm_metal_net_swarm_task_dispatch("a", NULL, 1) != -1) {
        return fail_task("dispatch null payload");
    }
    if (pm_metal_net_swarm_task_dispatch("a", job, PM_METAL_NET_SWARM_JOB_MAX + 1) != -1) {
        return fail_task("dispatch oversize");
    }

    /* Open the peer listener session (slot 1) — the reliably-opening half. */
    if (pm_metal_net_zenoh_sel(1) != 0) {
        return fail_task("sel1");
    }
    if (pm_metal_net_zenoh_peer(ZENOH_ADDR_BE, ZENOH_PORT, 1) != 0) {
        return fail_task("peer1");
    }
    for (steps = 0; steps < 40u; steps++) {
        if ((uint32_t)pm_metal_net_zenoh_up() == 1u) {
            break;
        }
        (void)pm_metal_net_zenoh_poll();
        (void)pm_metal_async_poll();
    }
    if ((uint32_t)pm_metal_net_zenoh_up() != 1u) {
        return fail_task("listener open");
    }

    /* Arm offer + declare on the open session, then dispatch a job. */
    if (pm_metal_net_swarm_task_offer("render", offer_policy, NULL) != 1) {
        return fail_task("offer open");
    }
    if (pm_metal_net_swarm_task_offer("shade", offer_policy, NULL) != -1) {
        (void)pm_metal_net_swarm_task_done();
        return fail_task("double offer");
    }
    if (pm_metal_net_swarm_task_declare("render", exec_recv) != 1) {
        (void)pm_metal_net_swarm_task_done();
        return fail_task("declare open");
    }
    if (pm_metal_net_swarm_task_declare("render", exec_recv) != -1) {
        (void)pm_metal_net_swarm_task_done();
        return fail_task("double declare");
    }
    if (pm_metal_net_swarm_task_offering() != 1) {
        (void)pm_metal_net_swarm_task_done();
        return fail_task("offering armed");
    }
    if (pm_metal_net_swarm_task_declaring() != 1) {
        (void)pm_metal_net_swarm_task_done();
        return fail_task("declaring armed");
    }
    if (pm_metal_net_swarm_task_dispatch("render", job, sizeof(job)) != 1) {
        (void)pm_metal_net_swarm_task_done();
        return fail_task("dispatch open");
    }

    /* Teardown: done() frees both arms; a second done is a no-op. */
    if (pm_metal_net_swarm_task_done() != 1) {
        return fail_task("done");
    }
    if (pm_metal_net_swarm_task_done() != 0) {
        return fail_task("done twice");
    }
    if (pm_metal_net_swarm_task_offering() != 0) {
        return fail_task("offering after done");
    }
    if (pm_metal_net_swarm_task_declaring() != 0) {
        return fail_task("declaring after done");
    }

    /* Clean the shared boot up for the tests that follow. */
    pm_metal_net_zenoh_deinit();
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.net.swarm.task, case_task_single, case_task_single);
