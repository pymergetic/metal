#ifndef PM_METAL_NET_SSH_PACKET_H_
#define PM_METAL_NET_SSH_PACKET_H_

#include <stdint.h>

#define PM_METAL_SSH_PKT_ACC 4096u

void pm_metal_net_ssh_pkt_reset(void);

/* Arm chacha20-poly1305@openssh.com (main||header = 32||32 each direction). */
void pm_metal_net_ssh_pkt_set_keys(const uint8_t key_c2s[64],
                                   const uint8_t key_s2c[64]);

int32_t pm_metal_net_ssh_pkt_send(const uint8_t *payload, uint32_t payload_len);
int32_t pm_metal_net_ssh_pkt_recv(uint8_t *payload, uint32_t cap, uint32_t *len_out);
int32_t pm_metal_net_ssh_pkt_push(const uint8_t *data, uint32_t len);

#endif
