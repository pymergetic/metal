/*
 * /etc/sshd.json — default seed (no JSON/fs load until sync fs helper exists).
 */
#ifndef PYMERGETIC_METAL_NET_SSH_CONFIG_H_
#define PYMERGETIC_METAL_NET_SSH_CONFIG_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t port;
  uint32_t budget_pct;
  int32_t auth_passwd;
  int32_t auth_pubkey;
  int32_t auth_sslcert;
  char host_key[96];
  char client_ca[96];
} pm_metal_sshd_cfg_t;

pm_metal_sshd_cfg_t *pm_metal_net_ssh_cfg(void);
/** Seed defaults + lab password user. Returns 0. */
int32_t pm_metal_net_ssh_cfg_load(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_SSH_CONFIG_H_ */
