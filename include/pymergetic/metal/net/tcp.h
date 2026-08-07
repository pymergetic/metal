#ifndef PM_METAL_NET_TCP_H_
#define PM_METAL_NET_TCP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t pm_metal_tcp_listen(uint16_t local_port);

/* Internal smoke: inject SYN, drive SYN-ACK + ACK; 0 on success. */
int32_t pm_metal_tcp_smoke_syn_ack(void);

#ifdef __cplusplus
}
#endif

#endif
