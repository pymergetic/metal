/* pymergetic.metal.net.swarm.membership — fleet membership on a net.zenoh
 * session. The card picks the currently-selected zenoh slot (the caller uses
 * pm_metal_net_zenoh_sel to choose which session — peer listener or connector —
 * this node participates with), then arms a roster queryable at
 * "@roster/<group>" that answers each query with this node's own base-16 ZID
 * (one registered member). The zenoh session's liveliness token (declared by
 * net.zenoh on open) is the heartbeat that makes the roster entry discoverable
 * liveness-wise; this card owns the roster answer + the node identity view.
 *
 * A two-peer roster round-trip — node A scouting/querying node B's queryable —
 * needs a second session and stays the tracked `two-session-prove` data-plane
 * remainder (same gate that bounds net.zenoh's put/subscribe and queryable
 * deliver). What this card proves on every seat, single-session, is the armed
 * roster + node identity + teardown on the reliably-opening peer listener.
 */
#include "pymergetic/metal/net/swarm/membership/__exports__.h"

#include "pymergetic/wasmmod/guest.h" /* PM_MOD_EXPORT_C / PM_MOD_TEST_C */
#include "pymergetic/metal/net/zenoh.h"
#include "pymergetic/metal/net/swarm/membership/__types__.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Roster keyexpr namespace this card arms. "%s" is the group (lowercased caller
 * filter); the queryable answers "@roster/<group>". */
static char s_group[PM_METAL_NET_SWARM_GROUP_MAX];
static uint8_t s_group_len;
static uint8_t s_armed; /* 1 when the roster queryable is declared on a slot */

/* Convert the zenoh ZID bytes to a lowercase base-16 identity. */
static void zid_to_hex(const uint8_t zid[16], char out[PM_METAL_NET_SWARM_MEMBER_ID_LEN]) {
    static const char hextab[] = "0123456789abcdef";
    uint8_t i;
    char *p = out;
    for (i = 0; i < 16; i++) {
        *p++ = hextab[(zid[i] >> 4) & 0xf];
        *p++ = hextab[zid[i] & 0xf];
    }
    *p = '\0';
}

/* Roster query handler: answer with this node's own ZID (the one member this
 * node registers itself under the group). The reply sink appends the identity
 * string; the net.zenoh card sends it to the querier. */
static void roster_query_cb(const char *key, size_t klen, const uint8_t *params, size_t plen,
                            pm_metal_net_zenoh_reply_t *reply, void *arg) {
    uint8_t zid[16];
    char id[PM_METAL_NET_SWARM_MEMBER_ID_LEN];
    (void)key;
    (void)klen;
    (void)params;
    (void)plen;
    (void)arg;
    if (pm_metal_net_zenoh_zid(zid) != 1) {
        return;
    }
    zid_to_hex(zid, id);
    (void)pm_metal_net_zenoh_reply_append(reply, (const uint8_t *)id, strlen(id));
}

int32_t pm_metal_net_swarm_membership_start(const char *group) {
    char key[PM_METAL_NET_SWARM_GROUP_MAX + 16];
    size_t glen;
    if (group == NULL) {
        return -1;
    }
    glen = strlen(group);
    if (glen == 0 || glen > (size_t)PM_METAL_NET_SWARM_GROUP_MAX) {
        return -1;
    }
    if (s_armed) {
        return -1; /* already armed; stop() first */
    }
    /* The selected zenoh slot must be an open session (liveness: its transport
     * carries the liveliness token that is this node's heartbeat). */
    if ((uint32_t)pm_metal_net_zenoh_up() != 1u) {
        return 0; /* no open session yet; caller retries after poll() */
    }
    (void)snprintf(key, sizeof(key), "@roster/%.*s", (int)glen, group);
    if (pm_metal_net_zenoh_queryable(key, roster_query_cb, NULL) != 1) {
        return -1;
    }
    memcpy(s_group, group, glen);
    s_group_len = (uint8_t)glen;
    s_armed = 1;
    return 1;
}

int32_t pm_metal_net_swarm_membership_stop(void) {
    if (!s_armed) {
        return 0;
    }
    (void)pm_metal_net_zenoh_undeclare_queryable();
    s_armed = 0;
    s_group_len = 0;
    return 1;
}

/* 1 while armed on an open session (the node's own membership is live). */
int32_t pm_metal_net_swarm_membership_alive(void) {
    if (!s_armed) {
        return 0;
    }
    if ((uint32_t)pm_metal_net_zenoh_up() != 1u) {
        return 0;
    }
    return 1;
}

/* Copy this node's own identity (base-16 ZID) into out. Returns the length, or
 * -1 when the session ZID is not resolvable. */
int32_t pm_metal_net_swarm_membership_node_id(char out[PM_METAL_NET_SWARM_MEMBER_ID_LEN]) {
    uint8_t zid[16];
    if (out == NULL) {
        return -1;
    }
    if (pm_metal_net_zenoh_zid(zid) != 1) {
        return -1;
    }
    zid_to_hex(zid, out);
    return (int32_t)strlen(out);
}

PM_MOD_EXPORT_C(pymergetic.metal.net.swarm.membership, pm_metal_net_swarm_membership_start,
                pm_metal_net_swarm_membership_start, int32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.metal.net.swarm.membership, pm_metal_net_swarm_membership_stop,
                pm_metal_net_swarm_membership_stop, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.swarm.membership, pm_metal_net_swarm_membership_alive,
                pm_metal_net_swarm_membership_alive, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.swarm.membership, pm_metal_net_swarm_membership_node_id,
                pm_metal_net_swarm_membership_node_id,
                int32_t(char[PM_METAL_NET_SWARM_MEMBER_ID_LEN]));
