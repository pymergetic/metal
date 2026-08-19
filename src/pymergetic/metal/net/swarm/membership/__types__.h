/* pymergetic.metal.net.swarm.membership — card-private types + the public face
 * border shared by the card's own TUs. The membership card layers fleet
 * membership on a net.zenoh session: it arms a roster queryable on the current
 * open slot (chosen via pm_metal_net_zenoh_sel) and reports the node's own
 * identity + alive state. It never owns a zenoh session of its own — it drives
 * the net.zenoh public faces, so the swarm card stays one defining language (C)
 * and one card (no second zenoh transport). */
#ifndef PYMERGETIC_METAL_NET_SWARM_MEMBERSHIP_TYPES_H
#define PYMERGETIC_METAL_NET_SWARM_MEMBERSHIP_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bound on a member identity string (the node's base-16 zenoh ZID) + the group
 * the roster answers under. The card copies identities into caller buffers on
 * the border, never holds a pointer across a poll step. */
#define PM_METAL_NET_SWARM_MEMBER_ID_LEN 40u
#define PM_METAL_NET_SWARM_GROUP_MAX 32u

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_SWARM_MEMBERSHIP_TYPES_H */
