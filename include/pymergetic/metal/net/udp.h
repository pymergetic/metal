#ifndef PM_METAL_NET_UDP_H_
#define PM_METAL_NET_UDP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t pm_metal_udp_bind(uint16_t local_port);
int32_t pm_metal_udp_sendto(uint32_t dst_ip, uint16_t dst_port,
                            const void *data, uint32_t len);

/* Dequeue one datagram; 1=got packet, 0=empty, -1=error. */
int32_t pm_metal_udp_recv(uint32_t *src_ip, uint16_t *src_port,
                          void *buf, uint32_t cap, uint32_t *len_out);

#ifdef __cplusplus
}
#endif

#endif
