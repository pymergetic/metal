/* pymergetic.metal.net.swarm.task — card-private types + the public face border
 * shared by the card's own TUs. The task card layers distributed work dispatch
 * on a net.zenoh session: it offers named jobs via a queryable (a peer probes
 * "@task/offer/verb" and gets ACCEPT/BUSY/ERR), declares a job command stream
 * by subscribing to "@task/run/verb", and dispatches a job by putting a record
 * on "@task/run/verb". It never owns a zenoh session of its own — it drives the
 * net.zenoh public faces, so the card stays one defining language (C) and one
 * card (no second zenoh transport).
 *
 * The cross-peer dispatch round-trip (one node's dispatch reaching another
 * node's declare over the wire) is the tracked `two-session-prove` data-plane
 * remainder, the same gate that bounds net.zenoh's put/subscribe delivery. What
 * is proven on every seat, single-session, is the armed offer+declare, the put
 * on the open session, the misuse guards, and teardown. */
#ifndef PYMERGETIC_METAL_NET_SWARM_TASK_TYPES_H
#define PYMERGETIC_METAL_NET_SWARM_TASK_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bound on a job verb (task name) this node offers / dispatches, + the bound
 * on a dispatched job record payload. The card copies job payloads into caller
 * buffers on the border, never holding a pointer across a poll step. */
#define PM_METAL_NET_SWARM_VERB_MAX 32u
#define PM_METAL_NET_SWARM_JOB_MAX 512u

/* A worker's answer to a job offer probe. Appended as the queryable reply
 * body by the host's offer callback. */
#define PM_METAL_NET_SWARM_OFFER_ACCEPT 0x07u /* "run it now" */
#define PM_METAL_NET_SWARM_OFFER_BUSY 0x08u   /* "can't now, retry later" */
#define PM_METAL_NET_SWARM_OFFER_ERR 0x09u    /* "won't ever run it" */

/* Callbacks the host installs on offer()/declare(). Both fire inside a
 * net.zenoh poll() step; their args are valid only for the duration of the
 * call (metal-no-pointer-across-await). */
typedef uint8_t (*pm_metal_net_swarm_offer_cb_t)(const uint8_t *params, size_t plen,
    void *arg);
typedef void (*pm_metal_net_swarm_exec_cb_t)(const uint8_t *payload, size_t plen,
    void *arg);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_SWARM_TASK_TYPES_H */
