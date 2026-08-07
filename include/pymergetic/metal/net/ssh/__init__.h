#ifndef PYMERGETIC_METAL_NET_SSH_H_
#define PYMERGETIC_METAL_NET_SSH_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * pymergetic.metal.net.ssh — hybrid face (C impl, RS/Py exports).
 * Stub until wolfSSH/DIY; live-ssh may send an ident banner only.
 */
int32_t pm_metal_net_ssh_available(void); /* 0 = not implemented */
int32_t pm_metal_net_ssh_init(void);
int32_t pm_metal_net_ssh_autoload(void);

uint32_t pm_metal_net_ssh_listen(uint32_t port); /* handle, or 0 */
void pm_metal_net_ssh_close(uint32_t s);
int32_t pm_metal_net_ssh_poll(void);
int32_t pm_metal_net_ssh_served(void);

int32_t pm_metal_net_ssh_status(uint8_t *buf, uint32_t buf_len);
uint32_t pm_metal_net_ssh_listen_port(void);
int32_t pm_metal_net_ssh_hostkey_label(uint8_t *buf, uint32_t buf_len);

int32_t pm_metal_net_ssh_client_exec(const char *host, uint16_t port,
    const char *user, const char *cmd, uint8_t *buf, uint32_t cap, uint32_t *len_out);

/* Transitional ident-only (not crypto). */
int32_t pm_metal_net_ssh_banner_send(void);
int32_t pm_metal_net_ssh_banner_sent(void);
void pm_metal_net_ssh_banner_reset(void);

int32_t pm_metal_net_ssh_bind_reg(void);

#ifdef __cplusplus
}
#endif

#endif
