/* pymergetic.metal.net.zenoh — border prove. This passes when the card
 * initializes, opens a peer + a client on one shared _lo_ net.ip, resolves
 * 16-byte ZIDs, tears down cleanly across bounded poll() steps, interleaves
 * two cooperative sessions (listener + connector) on one thread over loopback,
 * and round-trips a PUT to a cross-peer SUBSCRIBER sample over that pair. */
#include "pymergetic/metal/async.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/net/zenoh.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ZENOH_ADDR_BE 0x7f000001u /* 127.0.0.1 — net.ip lo */
#define ZENOH_PORT 7447u

static int32_t fail_zenoh(const char *why) {
    fprintf(stderr, "metal.net.zenoh test: %s\n", why);
    return 1;
}

/* Queryable handler for the prove: echoes nothing, just records it fired and
 * appends a fixed marker byte into the reply. Proves the callback type wiring
 * regardless of whether a peer ever delivers a query. */
static void query_cb(const char *key, size_t klen, const uint8_t *params, size_t plen,
                     pm_metal_net_zenoh_reply_t *reply, void *arg) {
    uint8_t tag = 0x5a;
    (void)key;
    (void)klen;
    (void)params;
    (void)plen;
    (void)arg;
    if (reply != NULL) {
        (void)pm_metal_net_zenoh_reply_append(reply, &tag, 1);
    }
}

/* Cross-peer subscription capture. The card delivers a subscribed sample to the
 * slot's value callback synchronously inside a pump step; this records key +
 * payload into a bounded static target so the two-session prove can assert the
 * exact bytes arrived across the wire. */
static struct pm_zenoh_sample_capture {
    uint8_t key[PM_METAL_NET_ZENOH_MAX_SAMPLE];
    size_t keylen;
    uint8_t payload[PM_METAL_NET_ZENOH_MAX_SAMPLE];
    size_t payloadlen;
} g_sample;

static void on_sample(const char *key, size_t klen, const uint8_t *payload, size_t plen, void *arg) {
    (void)arg;
    fprintf(stderr, "[zenoh-sample] plen=%zu keylen=%zu\n", plen, klen);
    memset(&g_sample, 0, sizeof(g_sample));
    if (key != NULL && klen < sizeof(g_sample.key)) {
        memcpy(g_sample.key, key, klen);
        g_sample.keylen = klen;
    }
    if (payload != NULL && plen <= sizeof(g_sample.payload)) {
        memcpy(g_sample.payload, payload, plen);
        g_sample.payloadlen = plen;
    }
}

static int32_t case_zid_deterministic(void) {
    uint8_t zid1[PM_METAL_NET_ZENOH_ZID_LEN];
    uint8_t zid2[PM_METAL_NET_ZENOH_ZID_LEN];
    uint32_t i;
    uint8_t any;
    /* The boot harness init'ed the card; the ZID is resolvable pre-open via the
     * deterministic per-process identity. Two reads in one session are stable. */
    if (pm_metal_net_zenoh_zid(zid1) != 1) {
        return fail_zenoh("zid read1");
    }
    if (pm_metal_net_zenoh_zid(zid2) != 1) {
        return fail_zenoh("zid read2");
    }
    for (i = 0; i < PM_METAL_NET_ZENOH_ZID_LEN; i++) {
        if (zid1[i] != zid2[i]) {
            return fail_zenoh("zid unstable");
        }
    }
    any = 0;
    for (i = 0; i < PM_METAL_NET_ZENOH_ZID_LEN; i++) {
        any |= zid1[i];
    }
    if (any == 0) {
        return fail_zenoh("zid all-zero");
    }
    return 0;
}

static int32_t case_up_poll_bounded(void) {
    uint8_t zid[PM_METAL_NET_ZENOH_ZID_LEN];
    uint32_t steps;
    fprintf(stderr, "===SCOPE up_poll_bounded BEGIN===\n");
    /* Client open against a listener that is not up: the open step must return
     * immediately (cooperative, no blocking z_open), and poll() must retry a
     * bounded number of steps without blocking or erroring the card. */
    if (pm_metal_net_zenoh_peer(ZENOH_ADDR_BE, ZENOH_PORT, 0) != 0) {
        return fail_zenoh("peer");
    }
    (void)pm_metal_net_zenoh_up();
    for (steps = 0; steps < 16u; steps++) {
        (void)pm_metal_net_zenoh_poll();
        (void)pm_metal_async_poll();
        (void)pm_metal_net_ip_pump();
    }
    /* The ZID must still be resolvable (local identity persists pre-open). */
    if (pm_metal_net_zenoh_zid(zid) != 1) {
        return fail_zenoh("zid after open attempt");
    }
    /* Re-point peer + deinit-less teardown boundary must be safe. */
    (void)pm_metal_net_zenoh_peer(ZENOH_ADDR_BE, ZENOH_PORT, 1);
    return 0;
}

/* Pump both open slots + net.ip once. This is what lets a listener and a
 * connector on one shared _lo_ net.ip interleave across bounded poll() steps:
 * each slot's poll() drives its zenoh executor, and pm_metal_net_ip_pump()
 * moves the loopback segment around the stack. */
static void pump_both(void) {
    (void)pm_metal_net_zenoh_sel(0);
    (void)pm_metal_net_zenoh_poll();
    (void)pm_metal_net_zenoh_sel(1);
    (void)pm_metal_net_zenoh_poll();
    (void)pm_metal_async_poll();
    (void)pm_metal_net_ip_pump();
}

/* Two-slot host: a listener (slot 1, peer mode) plus a connector (slot 0, client
 * mode) on one shared _lo_ net.ip.
 *
 * Scope of this prove and the state of the end-to-end plan:
 *   - The async-open machinery is proven: a peer-mode listener opens reliably
 *     and non-blocking (open_state==2 within a bounded number of cooperative
 *     steps), and z_open runs under the platform's cooperative yield. This came
 *     from _z_open_tcp treating a synchronous loopback ESTAB (connect()==1) as
 *     success instead of a hard error (no more reconnect churn exhausting the
 *     shared net.ip socket table), plus net.ip closing a TCP listener's children
 *     (listen_fd back-pointer) on close so a torn-down session does not leak
 *     accept sockets and corrupt later tests (http.asgi).
 *   - Two sessions interleave on one cooperative thread by driving both open
 *     slots' executors (listener accept + client read) in the same pump loop.
 *     Traced on the wire: the connector's TCP connect reaches ESTAB, the
 *     listener accepts (fd->fd) and reads the client INIT, and the listener's
 *     OPEN_REPLY is delivered into the connector's socket (seq==rcv_nxt, rx
 *     populated) inside the same cooperative runs.
 *   - Async-open reentrancy fix (the blocking bug that hid all of this): the
 *     connector's in-open read that drives the listener's accept+reply, and the
 *     listener's own accept read, both yield to pm_metal_net_ip_pump(). The
 *     pump body (pm_ip_pump_locked) holds the non-reentrant pm_ip_lock spinlock,
 *     and a cooperative waiter driven inside that body re-enters net.ip pump and
 *     spins forever on the lock it already holds on the same OS thread. Fixed in
 *     net.ip's pm_metal_net_ip_pump with a per-OS-thread nest guard so a
 *     same-thread re-entrant pump runs the body inline (the workers that spin on
 *     the lock are unchanged). The full client handshake now drives instead of
 *     deadlocking. 
 *   - NOT gated here (remaining task on `two-session-prove`): the client-mode
 *     connector still does not deterministically reach open_state==2 within a
 *     bounded cooperative step budget. The reentrancy deadlock is gone (net.ip
 *     now nests saftely), but zenoh-pico's z_open runs the whole client
 *     handshake in one synchronous call with a self-contained real-time connect
 *     window, and the connector's own in-open read deadline (TX_FAILED/-120)
 *     races the single-threaded driver, so the client retries with a fresh
 *     ephemeral socket instead of settling. The put/subscriber faces only
 *     accept once a slot is open_state==2, so the publish round-trip is not
 *     gateable yet. This test drives all of it, asserts the asynchronous peer
 *     half and clean dual-session teardown, and stays green on the shared
 *     single boot. */
static int32_t case_two_session_roundtrip(void) {
    uint32_t steps;
    uint8_t zid[PM_METAL_NET_ZENOH_ZID_LEN];
    fprintf(stderr, "===SCOPE two_session_roundtrip BEGIN===\n");
    /* The whole card shares one boot with other cards (net.swarm.* opens a
     * peer/listen session in slot 1, and this suite's own case_up_poll_bounded
     * leaves slot 0 open against that listener). Prior tests can leave slots
     * mid-open, so tear the session slate down before re-proving the two-session
     * data plane from a clean, known state. */
    pm_metal_net_zenoh_deinit();
    /* Slot 1 = listener (peer mode, bind 127.0.0.1:7447). Peer-mode open must be
     * immediate and non-blocking (nothing to wait for yet). */
    if (pm_metal_net_zenoh_sel(1) != 0) {
        return fail_zenoh("sel1");
    }
    if (pm_metal_net_zenoh_peer(ZENOH_ADDR_BE, ZENOH_PORT, 1) != 0) {
        return fail_zenoh("peer1");
    }
    for (steps = 0; steps < 40u; steps++) {
        if ((uint32_t)pm_metal_net_zenoh_up() == 1u) {
            break;
        }
        pump_both();
    }
    if ((uint32_t)pm_metal_net_zenoh_up() != 1u) {
        return fail_zenoh("listener open");
    }
    if (pm_metal_net_zenoh_zid(zid) != 1) {
        return fail_zenoh("listener zid");
    }

    /* Queryable (swarm roster) faces on the open peer session. Declare succeeds
     * on the open listener; a second declare on the same slot is rejected (one
     * queryable per slot for this border pass); replying while unarmed is a
     * guarded no-op; undeclare frees the slot so a fresh declare succeeds. A
     * full query/roster round-trip needs a second peer and stays the
     * `two-session-prove` data-plane remainder (same gate as subscribe/put). */
    {
        if (pm_metal_net_zenoh_queryable("@/swarm/roster", query_cb, NULL) != 1) {
            return fail_zenoh("queryable declare");
        }
        if (pm_metal_net_zenoh_queryable("@/swarm/roster2", query_cb, NULL) != -1) {
            (void)pm_metal_net_zenoh_undeclare_queryable();
            return fail_zenoh("second queryable accepted");
        }
        if (pm_metal_net_zenoh_undeclare_queryable() != 1) {
            return fail_zenoh("queryable undeclare");
        }
        if (pm_metal_net_zenoh_undeclare_queryable() != 0) {
            return fail_zenoh("queryable undeclare twice");
        }
        if (pm_metal_net_zenoh_reply_append(NULL, (const uint8_t *)"x", 1) != -1) {
            return fail_zenoh("reply append unarmed");
        }
        if (pm_metal_net_zenoh_queryable("@/swarm/roster", query_cb, NULL) != 1) {
            return fail_zenoh("queryable redeclare");
        }
        if (pm_metal_net_zenoh_undeclare_queryable() != 1) {
            return fail_zenoh("queryable undeclare 2");
        }
    }

    /* Slot 0 = connector (client mode) against the same endpoint. Drive its
     * open cooperatively together with the open listener: this is the async-open
     * call path under the platform's cooperative yield. The client's z_open runs
     * the whole INIT/OPEN handshake synchronously; its platform reads yield to
     * pump net.ip and spin the listener's executor (the accept task answers the
     * connector) and its platform sends now loop partial net.ip writes to a full
     * frame (a short write on a streamed link is TX_FAILED, which used to churn a
     * fresh socket instead of settling). So the connector must deterministically
     * reach open_state==2 within a bounded cooperative step budget. */
    if (pm_metal_net_zenoh_sel(0) != 0) {
        return fail_zenoh("sel0");
    }
    if (pm_metal_net_zenoh_peer(ZENOH_ADDR_BE, ZENOH_PORT, 0) != 0) {
        return fail_zenoh("peer0");
    }
    (void)pm_metal_net_zenoh_up(); /* first attempt */
    for (steps = 0; steps < 400u; steps++) {
        if ((uint32_t)pm_metal_net_zenoh_up() == 1u) {
            break;
        }
        pump_both();
    }
    if ((uint32_t)pm_metal_net_zenoh_up() != 1u) {
        return fail_zenoh("connector open");
    }
    if (pm_metal_net_zenoh_zid(zid) != 1) {
        return fail_zenoh("connector zid");
    }

    /* Cross-peer data plane: the connector PUTs a payload on a keyexpr the
     * listener has subscribed to, and the listener's value callback must fire
     * (synchronously inside a pump step) with the exact bytes. This is the
     * two-session put/subscriber round-trip the card drives as its raison
     * d'etre: two cooperatively-interleaved zenoh sessions on one thread,
     * sample delivered end-to-end over the shared _lo_ net.ip. */
    {
        uint32_t x;
        static const uint8_t payload[] = "tcp-two-session-data-plane";
        static const char keyexpr[] = "demo/twosession";
        memset(&g_sample, 0, sizeof(g_sample));
        /* Listener declares the subscription first, then the connector puts on
         * that keyexpr; a subscriber that is not yet active would drop the
         * sample, so declare before putting and pump once to arm the interest. */
        if (pm_metal_net_zenoh_sel(1) != 0) {
            return fail_zenoh("sel1-sub");
        }
        if (pm_metal_net_zenoh_subscribe(keyexpr, on_sample, NULL) != 1) {
            return fail_zenoh("subscriber declare");
        }
        /* Interest is async: the listener's executor must flush the DECLARE to
         * the connector peer and the connector's executor must parse it back
         * into a remote interest for the keyexpr before a put will route to the
         * listener. Pump the interleaved pair until that round-trip settles. */
        {
            uint32_t p;
            for (p = 0; p < 20u; p++) {
                pump_both();
            }
        }
        /* Connector put on the subscribed keyexpr. */
        if (pm_metal_net_zenoh_sel(0) != 0) {
            return fail_zenoh("sel0-put");
        }
        if (pm_metal_net_zenoh_put(keyexpr, payload, (uint32_t)sizeof(payload) - 1u) != 1) {
            return fail_zenoh("connector put");
        }
        /* Pump the interleaved pair until the listener's callback receives the
         * sample or a bounded budget lapses. */
        for (x = 0; x < 200u; x++) {
            pump_both();
            if (g_sample.payloadlen != 0u) {
                break;
            }
        }
        if (g_sample.payloadlen == 0u) {
            return fail_zenoh("no sample delivered");
        }
        if (g_sample.payloadlen != (uint32_t)sizeof(payload) - 1u ||
            memcmp(g_sample.payload, payload, (size_t)g_sample.payloadlen) != 0) {
            return fail_zenoh("sample mismatch");
        }
        if (g_sample.keylen != sizeof(keyexpr) - 1u ||
            memcmp(g_sample.key, keyexpr, (size_t)g_sample.keylen) != 0) {
            return fail_zenoh("sample key mismatch");
        }
        (void)pm_metal_net_zenoh_sel(1);
        (void)pm_metal_net_zenoh_undeclare_subscribe();
    }

    /* The listener must still be live and its ZID resolvable after driving both
     * executors cooperatively and round-tripping a sample: the peer half of the
     * async open holds up while a client churns against it. */
    (void)pm_metal_net_zenoh_sel(1);
    if ((uint32_t)pm_metal_net_zenoh_up() != 1u) {
        return fail_zenoh("listener lost");
    }
    if (pm_metal_net_zenoh_zid(zid) != 1) {
        return fail_zenoh("listener zid after");
    }

    /* Tear the card down: the whole host suite shares one boot, so an idle
     * listener must not keep the _lo_ net.ip socket bound for the tests that
     * follow (e.g. http.asgi). deinit is idempotent w.r.t. the final teardown. */
    pm_metal_net_zenoh_deinit();
    return 0;
}

/* Scout/HELLO round-trip gated behind the net.ip multicast join_group task.
 *
 * zenoh scouting sends a SCOUT datagram to 224.0.0.224:7446; a live peer
 * answers a HELLO unicast to the scout's source. The vendored zenoh-pico core
 * only encodes/decodes scouting messages (it never answers a SCOUT), so the
 * card synthesizes the hello side (scout_answer_*). This proves:
 *   1. The net.ip multicast group-join is the gate — with NO joined answerer,
 *      scout() gets no HELLO (0); the group datagram is not delivered anywhere.
 *   2. Once the joined answerer is armed, a scout sent to the group is
 *      looped back, decoded, and answered with a HELLO carrying the local ZID
 *      and WhatAmI=Peer, received by the ephemeral scout socket on loopback.
 *   3. The answer listener is single-slot (a second armed answer is rejected),
 *      so the prove never leaves a stray group member behind.
 * All steps are bounded and cooperative; the same host C test proves every seat.
 * Runs after case_two_session_roundtrip (which deinit's the session slots); the
 * card keeps the boot arena across deinit so this pre-open z_malloc still works.
 */
static int32_t case_scout_roundtrip(void) {
    uint8_t zid[PM_METAL_NET_ZENOH_ZID_LEN];
    uint8_t whatami = 0;
    uint8_t local_zid[PM_METAL_NET_ZENOH_ZID_LEN];
    uint32_t steps;
    int32_t rc;
    /* Self-contained: close any session slots left over from earlier proves so
     * the HELLO we compare against is the deterministic pre-open ZID
     * (zenoh_local_zid), not a stale session's random one. Deinit also clears
     * peer config, so the poll() calls below will not re-open those slots. */
    pm_metal_net_zenoh_deinit();
    (void)pm_metal_net_zenoh_sel(0);
    /* Gate 1: no joined answerer -> no HELLO. A scout must NOT resolve (0)
     * because nothing is on the multicast group yet. */
    (void)pm_metal_net_ip_pump();
    rc = 0;
    for (steps = 0; steps < 8u; steps++) {
        rc = pm_metal_net_zenoh_scout(2u /* Z_WHAT_PEER */, zid, &whatami);
        (void)pm_metal_net_zenoh_poll();
        (void)pm_metal_async_poll();
        if (rc != 0) {
            return fail_zenoh("scout resolved with no answerer");
        }
    }
    /* Arm the hello side (joins 224.0.0.224:7446 on net.ip). A second arm while
     * one is live must be rejected: only one group listener at a time. */
    if (pm_metal_net_zenoh_scout_answer_on() != 0) {
        return fail_zenoh("scout answer on");
    }
    if (pm_metal_net_zenoh_scout_answer_on() == 0) {
        (void)pm_metal_net_zenoh_scout_answer_off();
        return fail_zenoh("second scout answer accepted");
    }
    /* Local ZID the HELLO must carry back (pre-open identity). */
    if (pm_metal_net_zenoh_zid(local_zid) != 1) {
        (void)pm_metal_net_zenoh_scout_answer_off();
        return fail_zenoh("scout local zid");
    }
    /* Gate 2: with the joined answerer live, the scout round-trips. Drive the
     * cooperative step (scout + answer pump + net.ip) until a HELLO lands or a
     * bounded budget is exhausted. */
    rc = 0;
    for (steps = 0; steps < 24u; steps++) {
        rc = pm_metal_net_zenoh_scout(2u /* Z_WHAT_PEER */, zid, &whatami);
        (void)pm_metal_net_zenoh_scout_answer_pump();
        (void)pm_metal_net_ip_pump();
        (void)pm_metal_async_poll();
        if (rc == 1) {
            break;
        }
    }
    (void)pm_metal_net_zenoh_scout_answer_off();
    if (rc != 1) {
        return fail_zenoh("scout no hello");
    }
    if (whatami != 2u /* Z_WHATAMI_PEER */) {
        return fail_zenoh("scout whatami");
    }
    if (memcmp(zid, local_zid, PM_METAL_NET_ZENOH_ZID_LEN) != 0) {
        uint_fast8_t z;
        fprintf(stderr, "scout zid mismatch: scout=");
        for (z = 0; z < PM_METAL_NET_ZENOH_ZID_LEN; z++) fprintf(stderr, "%02x", (unsigned)zid[z]);
        fprintf(stderr, " local=");
        for (z = 0; z < PM_METAL_NET_ZENOH_ZID_LEN; z++) fprintf(stderr, "%02x", (unsigned)local_zid[z]);
        fprintf(stderr, "\n");
        return fail_zenoh("scout zid mismatch");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.net.zenoh, "zid_deterministic", case_zid_deterministic);
PM_MOD_TEST_C(pymergetic.metal.net.zenoh, "up_poll_bounded", case_up_poll_bounded);
PM_MOD_TEST_C(pymergetic.metal.net.zenoh, "two_session_roundtrip", case_two_session_roundtrip);
PM_MOD_TEST_C(pymergetic.metal.net.zenoh, "scout_roundtrip", case_scout_roundtrip);
