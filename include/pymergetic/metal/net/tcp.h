#ifndef PM_METAL_NET_TCP_H_
#define PM_METAL_NET_TCP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t pm_metal_tcp_listen(uint16_t local_port);

/* Active open; returns 0 SYN sent, -2 ARP pending, -1 error. Poll until established. */
int32_t pm_metal_tcp_connect(uint32_t dst_ip, uint16_t dst_port);

/*
 * Abort the focused PCB (listen or connect). Client abort leaves the
 * passive/server PCB intact (dual-slot stack).
 */
void pm_metal_tcp_abort(void);

int32_t pm_metal_tcp_established(void);

/* 1 if the passive/server PCB (listen slot) is ESTABLISHED. */
int32_t pm_metal_tcp_passive_established(void);

/* 1 if the passive/server PCB is still in LISTEN. */
int32_t pm_metal_tcp_passive_listening(void);

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
