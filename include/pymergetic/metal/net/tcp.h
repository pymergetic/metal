#ifndef PM_METAL_NET_TCP_H_
#define PM_METAL_NET_TCP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t pm_metal_tcp_listen(uint16_t local_port);
int32_t pm_metal_tcp_established(void);

/* Send on the established connection. Returns 0 on success, -2 ARP pending. */
int32_t pm_metal_tcp_send(const void *data, uint32_t len);

/* Copy queued RX payload; returns 1 if data, 0 if empty, -1 on error. */
int32_t pm_metal_tcp_recv(uint8_t *buf, uint32_t cap, uint32_t *len_out);

/* Internal smoke: inject SYN, drive SYN-ACK + ACK; 0 on success. */
int32_t pm_metal_tcp_smoke_syn_ack(void);

/* Inject a data segment from the smoke peer (after established). */
int32_t pm_metal_tcp_smoke_inject_payload(const void *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif
