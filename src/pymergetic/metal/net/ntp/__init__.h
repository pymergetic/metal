#ifndef PYMERGETIC_METAL_NET_NTP_H_
#define PYMERGETIC_METAL_NET_NTP_H_

#include <stdint.h>

uint32_t pm_metal_net_ntp_sync(const char *host);
uint32_t pm_metal_net_ntp_status(uint32_t h);
uint64_t pm_metal_net_ntp_last_unix_ms(void);

#endif /* PYMERGETIC_METAL_NET_NTP_H_ */
