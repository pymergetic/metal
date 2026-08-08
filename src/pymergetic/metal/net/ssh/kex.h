#ifndef PM_METAL_NET_SSH_KEX_H_
#define PM_METAL_NET_SSH_KEX_H_

#include <stdint.h>

#define PM_METAL_SSH_KEXINIT_MAX 2048u
#define PM_METAL_SSH_IDENT_MAX 128u

typedef struct {
    uint8_t v_c[PM_METAL_SSH_IDENT_MAX];
    uint32_t v_c_len;
    uint8_t v_s[PM_METAL_SSH_IDENT_MAX];
    uint32_t v_s_len;
    uint8_t i_c[PM_METAL_SSH_KEXINIT_MAX];
    uint32_t i_c_len;
    uint8_t i_s[PM_METAL_SSH_KEXINIT_MAX];
    uint32_t i_s_len;
    uint8_t eph_sk[32];
    uint8_t eph_pk[32];
    uint8_t session_id[32];
    uint8_t have_session;
} pm_metal_net_ssh_kex_t;

void pm_metal_net_ssh_kex_reset(pm_metal_net_ssh_kex_t *k);

/* Build our KEXINIT payload (msg type included). Returns length or 0. */
uint32_t pm_metal_net_ssh_kex_build_init(uint8_t *dst, uint32_t cap);

/* Handle peer ECDH_INIT; fill reply payload (msg type included). */
int32_t pm_metal_net_ssh_kex_server_reply(pm_metal_net_ssh_kex_t *k,
    const uint8_t *peer_init, uint32_t peer_init_len, uint8_t *reply,
    uint32_t reply_cap, uint32_t *reply_len);

#endif
