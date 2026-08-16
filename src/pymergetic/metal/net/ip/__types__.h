/* pymergetic.metal.net.ip — one stack + L2 attach. TCP rexmit on L2; lo is still instant. */
#ifndef PYMERGETIC_METAL_NET_IP_TYPES_H
#define PYMERGETIC_METAL_NET_IP_TYPES_H

#include <stdint.h>

#include "pymergetic/metal/drivers/net/__types__.h"
#include "pymergetic/util/mem/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PM_METAL_NET_IP_SOCK_STREAM = 1,
    PM_METAL_NET_IP_SOCK_DGRAM = 2,
};

/* Same face as metal.drivers.net. wg (tunnel) still attaches through this alias. */
typedef pm_metal_netdev_ops_t pm_metal_net_l2_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_IP_TYPES_H */
