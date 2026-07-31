#ifndef PYMERGETIC_METAL_NET_PING_H_
#define PYMERGETIC_METAL_NET_PING_H_

#include <stdint.h>

#define PM_METAL_NET_PING_ERR_NONE    0u
#define PM_METAL_NET_PING_ERR_RESOLVE 1u
#define PM_METAL_NET_PING_ERR_SEND    2u
#define PM_METAL_NET_PING_ERR_TIMEOUT 3u
#define PM_METAL_NET_PING_ERR_NOROUTE 4u
#define PM_METAL_NET_PING_ERR_NOMEM   5u

uint32_t pm_metal_net_ping(const char *host, uint32_t timeout_ms);
uint32_t pm_metal_net_ping_rtt_ms(uint32_t h);
uint32_t pm_metal_net_ping_rtt_us(uint32_t h);
uint32_t pm_metal_net_ping_last_err(void);

#endif /* PYMERGETIC_METAL_NET_PING_H_ */
