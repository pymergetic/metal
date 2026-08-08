#ifndef PM_METAL_NET_SSH_PACKET_H_
#define PM_METAL_NET_SSH_PACKET_H_

#include <stdint.h>

/* Cleartext SSH binary packet (no MAC) for pre-NEWKEYS DIY path. */
int32_t pm_metal_net_ssh_pkt_send(const uint8_t *payload, uint32_t payload_len);
/* 1 = full payload, 0 = need more, -1 = error. */
int32_t pm_metal_net_ssh_pkt_recv(uint8_t *payload, uint32_t cap, uint32_t *len_out);
void pm_metal_net_ssh_pkt_reset(void);
/* Bytes already read past the ident line (start of binary stream). */
int32_t pm_metal_net_ssh_pkt_push(const uint8_t *data, uint32_t len);

#endif

