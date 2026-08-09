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

/* Async: park until lease ready or timeout. result_u32 1 ok / 0 fail. */
uint32_t pm_metal_net_dhcp_start(void);
/* Advance wait (also hooked from net.pump). */
void pm_metal_net_dhcp_poll(void);
/* Last lease after successful start/run. */
const pm_metal_net_dhcp_lease_t *pm_metal_net_dhcp_lease(void);

/* Sync façade for bring-up/smoke (pumps until DONE). */
int32_t pm_metal_net_dhcp_run(pm_metal_net_dhcp_lease_t *lease_out);

#ifdef __cplusplus
}
#endif

#endif
