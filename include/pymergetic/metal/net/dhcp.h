#ifndef PM_METAL_NET_DHCP_H_
#define PM_METAL_NET_DHCP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t yiaddr;
    uint32_t mask;
    uint32_t gw;
    uint32_t dns;
    uint32_t server;
} pm_metal_net_dhcp_lease_t;

/* Run DORA (DISCOVER/OFFER/REQUEST/ACK). Requires IP stack init (addr may be 0). */
int32_t pm_metal_net_dhcp_run(pm_metal_net_dhcp_lease_t *lease_out);

#ifdef __cplusplus
}
#endif

#endif
