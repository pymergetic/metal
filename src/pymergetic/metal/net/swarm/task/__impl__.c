/* pymergetic.metal.net.swarm.task — distributed work dispatch on a net.zenoh
 * session. The card picks the currently-selected zenoh slot (the caller uses
 * pm_metal_net_zenoh_sel to choose which session — peer listener or connector —
 * this node participates with), then:
 *
 *   - offer(verb, cb, arg): arms a queryable at "@task/offer/<verb>" so a peer
 *     orchestrator can probe whether this node is willing to run <verb> right
 *     now. The host cb decides ACCEPT / BUSY / ERR and that single byte is the
 *     reply body.
 *   - declare(verb, cb, arg): subscribes to "@task/run/<verb>" — the job
 *     command stream. Each dispatched job record (payload) is handed to cb.
 *   - dispatch(verb, payload, len): puts a job record on "@task/run/<verb>".
 *   - done(): tears down a pending offer and a pending declare.
 *
 * Like membership, the card never owns a zenoh session — it drives the public
 * net.zenoh faces, so it stays one defining language (C) and one card. A full
 * two-peer dispatch round-trip needs a second session and stays the tracked
 * `two-session-prove` data-plane remainder; what this card proves on every
 * seat, single-session, is the armed offer + armed declare + dispatch put + the
 * off/on state machine on the reliably-opening peer listener. */
#include "pymergetic/metal/net/swarm/task/__exports__.h"

#include "pymergetic/wasmmod/guest.h" /* PM_MOD_EXPORT_C / PM_MOD_TEST_C */
#include "pymergetic/metal/net/zenoh.h"
#include "pymergetic/metal/net/swarm/task/__types__.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct offer_cb {
    pm_metal_net_swarm_offer_cb_t cb;
    void *arg;
};
static struct offer_cb s_offer;
static uint8_t s_offering;  /* 1 when the offer queryable is armed on a slot */
static uint8_t s_declaring; /* 1 when the exec subscription is armed on a slot */

/* The offer probe handler: ask the host cb how it feels about the job right
 * now and answer with a single ACCEPT / BUSY / ERR byte (the probe's verdict
 * that a real orchestrator reads before dispatching). */
static void offer_query_cb(const char *key, size_t klen, const uint8_t *params,
                           size_t plen, pm_metal_net_zenoh_reply_t *reply, void *arg) {
    struct offer_cb *o = (struct offer_cb *)arg;
    const uint8_t verdict = (o != NULL && o->cb != NULL)
        ? (*o->cb)(params, plen, o->arg)
        : PM_METAL_NET_SWARM_OFFER_ERR;
    (void)key;
    (void)klen;
    (void)pm_metal_net_zenoh_reply_append(reply, &verdict, 1);
}

/* The exec stream handler: forward an delivered job record (payload) to the
 * host cb. The sample is already copied into the slot's bounded buffer by
 * net.zenoh, so `payload` is valid for this call only. */
static void exec_value_cb(const char *key, size_t klen, const uint8_t *payload,
                          size_t plen, void *arg) {
    pm_metal_net_swarm_exec_cb_t cb = (pm_metal_net_swarm_exec_cb_t)arg;
    (void)key;
    (void)klen;
    if (cb != NULL) {
        (*cb)(payload, plen, NULL);
    }
}

int32_t pm_metal_net_swarm_task_offer(const char *verb, pm_metal_net_swarm_offer_cb_t cb,
                                      void *arg) {
    char key[PM_METAL_NET_SWARM_VERB_MAX + 16];
    size_t vlen;
    if (verb == NULL || cb == NULL) {
        return -1;
    }
    vlen = strlen(verb);
    if (vlen == 0 || vlen > (size_t)PM_METAL_NET_SWARM_VERB_MAX) {
        return -1;
    }
    if (s_offering) {
        return -1; /* already offering; done() first */
    }
    /* The selected zenoh slot must be an open session to arm the queryable. */
    if ((uint32_t)pm_metal_net_zenoh_up() != 1u) {
        return 0; /* no open session yet; caller retries after poll() */
    }
    (void)snprintf(key, sizeof(key), "@task/offer/%.*s", (int)vlen, verb);
    s_offer.cb = cb;
    s_offer.arg = arg;
    if (pm_metal_net_zenoh_queryable(key, offer_query_cb, &s_offer) != 1) {
        s_offer.cb = NULL;
        s_offer.arg = NULL;
        return -1;
    }
    s_offering = 1;
    return 1;
}

int32_t pm_metal_net_swarm_task_declare(const char *verb, pm_metal_net_swarm_exec_cb_t cb) {
    char key[PM_METAL_NET_SWARM_VERB_MAX + 16];
    size_t vlen;
    if (verb == NULL || cb == NULL) {
        return -1;
    }
    vlen = strlen(verb);
    if (vlen == 0 || vlen > (size_t)PM_METAL_NET_SWARM_VERB_MAX) {
        return -1;
    }
    if (s_declaring) {
        return -1; /* already declared; done() first */
    }
    if ((uint32_t)pm_metal_net_zenoh_up() != 1u) {
        return 0; /* no open session yet; caller retries after poll() */
    }
    (void)snprintf(key, sizeof(key), "@task/run/%.*s", (int)vlen, verb);
    if (pm_metal_net_zenoh_subscribe(key, exec_value_cb, (void *)cb) != 1) {
        return -1;
    }
    s_declaring = 1;
    return 1;
}

int32_t pm_metal_net_swarm_task_dispatch(const char *verb, const uint8_t *payload, uint32_t len) {
    char key[PM_METAL_NET_SWARM_VERB_MAX + 16];
    size_t vlen;
    int32_t rc;
    if (verb == NULL || (len && payload == NULL)) {
        return -1;
    }
    vlen = strlen(verb);
    if (vlen == 0 || vlen > (size_t)PM_METAL_NET_SWARM_VERB_MAX) {
        return -1;
    }
    if (len > PM_METAL_NET_SWARM_JOB_MAX) {
        return -1;
    }
    if ((uint32_t)pm_metal_net_zenoh_up() != 1u) {
        return 0; /* no open session yet; caller retries after poll() */
    }
    (void)snprintf(key, sizeof(key), "@task/run/%.*s", (int)vlen, verb);
    rc = pm_metal_net_zenoh_put(key, payload, len);
    if (rc != 1) {
        return -1; /* put rejected on the open session */
    }
    return 1;
}

/* 1 while offering on an open session. */
int32_t pm_metal_net_swarm_task_offering(void) {
    if (!s_offering) {
        return 0;
    }
    if ((uint32_t)pm_metal_net_zenoh_up() != 1u) {
        return 0;
    }
    return 1;
}

/* 1 while declaring on an open session. */
int32_t pm_metal_net_swarm_task_declaring(void) {
    if (!s_declaring) {
        return 0;
    }
    if ((uint32_t)pm_metal_net_zenoh_up() != 1u) {
        return 0;
    }
    return 1;
}

int32_t pm_metal_net_swarm_task_done(void) {
    int32_t did = 0;
    if (s_offering) {
        did |= pm_metal_net_zenoh_undeclare_queryable();
        s_offering = 0;
        s_offer.cb = NULL;
        s_offer.arg = NULL;
    }
    if (s_declaring) {
        did |= pm_metal_net_zenoh_undeclare_subscribe();
        s_declaring = 0;
    }
    return did ? 1 : 0;
}

PM_MOD_EXPORT_C(pymergetic.metal.net.swarm.task, pm_metal_net_swarm_task_offer,
                pm_metal_net_swarm_task_offer,
                int32_t(const char *, pm_metal_net_swarm_offer_cb_t, void *));
PM_MOD_EXPORT_C(pymergetic.metal.net.swarm.task, pm_metal_net_swarm_task_declare,
                pm_metal_net_swarm_task_declare,
                int32_t(const char *, pm_metal_net_swarm_exec_cb_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.swarm.task, pm_metal_net_swarm_task_dispatch,
                pm_metal_net_swarm_task_dispatch,
                int32_t(const char *, const uint8_t *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.swarm.task, pm_metal_net_swarm_task_offering,
                pm_metal_net_swarm_task_offering, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.swarm.task, pm_metal_net_swarm_task_declaring,
                pm_metal_net_swarm_task_declaring, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.swarm.task, pm_metal_net_swarm_task_done,
                pm_metal_net_swarm_task_done, int32_t(void));
