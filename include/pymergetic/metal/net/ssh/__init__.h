#ifndef PYMERGETIC_METAL_NET_SSH_H_
#define PYMERGETIC_METAL_NET_SSH_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * pymergetic.metal.net.ssh — DIY hybrid face (C impl).
 * WolfSSH/libssh* banned (license). Crypto/KEX still TODO;
 * version-exchange (RFC4253 §4.2) is real.
 */
int32_t pm_metal_net_ssh_available(void); /* 1 once DIY path is linked */
int32_t pm_metal_net_ssh_init(void);
int32_t pm_metal_net_ssh_autoload(void);

uint32_t pm_metal_net_ssh_listen(uint32_t port); /* handle, or 0 */
void pm_metal_net_ssh_close(uint32_t s);
int32_t pm_metal_net_ssh_poll(void); /* drive server state; 1 = ident done */
int32_t pm_metal_net_ssh_served(void);

int32_t pm_metal_net_ssh_status(uint8_t *buf, uint32_t buf_len);
uint32_t pm_metal_net_ssh_listen_port(void);
int32_t pm_metal_net_ssh_hostkey_label(uint8_t *buf, uint32_t buf_len);

int32_t pm_metal_net_ssh_client_exec(const char *host, uint16_t port,
    const char *user, const char *cmd, uint8_t *buf, uint32_t cap, uint32_t *len_out);

/* Ident helpers (also used by live-ssh / smoke). */
int32_t pm_metal_net_ssh_banner_send(void);
int32_t pm_metal_net_ssh_banner_sent(void);
void pm_metal_net_ssh_banner_reset(void);

int32_t pm_metal_net_ssh_bind_reg(void);

#ifdef __cplusplus
}
#endif

#endif
