/* pymergetic.metal.net.swarm.discovery — card-private types + the public face
 * border. The discovery card turns net.zenoh's SCOUT/HELLO primitives into a
 * fleet-discovery face: a node scouts the member group and answers SCOUTs so
 * peers discover it. It never owns a zenoh session or a socket of its own — it
 * drives the net.zenoh public scout faces, so it stays one defining language
 * (C) and one card (no second discovery transport). */
#ifndef PYMERGETIC_METAL_NET_SWARM_DISCOVERY_TYPES_H
#define PYMERGETIC_METAL_NET_SWARM_DISCOVERY_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A discovered peer is identified by its 16-byte zenoh ZID + whatami role. */
#define PM_METAL_NET_SWARM_PEER_ID_LEN 16u

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_SWARM_DISCOVERY_TYPES_H */
