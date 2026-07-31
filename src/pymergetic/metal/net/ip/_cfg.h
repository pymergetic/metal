/*
 * Host network interface config (exp2 minimal slice).
 */
#ifndef PYMERGETIC_METAL_NET_IP_CFG_PRIV_H_
#define PYMERGETIC_METAL_NET_IP_CFG_PRIV_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_NET_IP_IFNAME_MAX 8
#define PM_METAL_NET_IP_MAX_IFS    4

typedef struct pm_metal_net_ip_ifcfg {
  char          name[PM_METAL_NET_IP_IFNAME_MAX];
  char          ip[16];
  char          mask[16];
  char          gw[16];
  unsigned char mac[6];
  int           link_up;
  const char   *backend;
} pm_metal_net_ip_ifcfg_t;

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_IP_CFG_PRIV_H_ */
