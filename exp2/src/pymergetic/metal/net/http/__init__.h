#ifndef PYMERGETIC_METAL_NET_HTTP_H_
#define PYMERGETIC_METAL_NET_HTTP_H_

#include <stdint.h>

uint32_t pm_metal_net_http_get(const char *url, void *dest, uint32_t dest_cap);
uint32_t pm_metal_net_http_status(uint32_t h);
uint32_t pm_metal_net_http_body_len(uint32_t h);

#endif /* PYMERGETIC_METAL_NET_HTTP_H_ */
