#ifndef PYMERGETIC_METAL_NET_WG_H_
#define PYMERGETIC_METAL_NET_WG_H_

#include <stdint.h>

#include "pymergetic/metal/net/ip/cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_NET_WG_KEY_B64_MAX 64
/** Default product face (sugar for named "wg0"). */
#define PM_METAL_NET_WG_IFNAME "wg0"
/** Slot budget — same freestanding table size as host if-mgmt (`ethN` / `wgN`). */
#define PM_METAL_NET_WG_MAX_IFS PM_METAL_NET_IP_MAX_IFS

typedef struct pm_metal_net_wg_status {
    int up;
    char name[PM_METAL_NET_IP_IFNAME_MAX];
    char ip[16];
    char peer_public[PM_METAL_NET_WG_KEY_B64_MAX];
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint32_t last_handshake_sec;
    uint16_t listen_port;
} pm_metal_net_wg_status_t;

/** Create/up named WireGuard iface (`wg` + decimal index, e.g. wg0…wg7). listen_port 0 → 51820. */
int pm_metal_net_wg_up_named(const char *ifname, const char *private_key_b64, uint16_t listen_port,
                             const char *tunnel_ip, const char *tunnel_mask);
int pm_metal_net_wg_down_named(const char *ifname);
int pm_metal_net_wg_peer_add_named(const char *ifname, const char *public_key_b64,
                                   const char *endpoint_ip, uint16_t endpoint_port,
                                   const char *allowed_ip, const char *allowed_mask);
int pm_metal_net_wg_peer_del_named(const char *ifname, const char *public_key_b64);
int pm_metal_net_wg_status_named(const char *ifname, pm_metal_net_wg_status_t *out);

/** wg0 convenience wrappers (server/client product face). */
int pm_metal_net_wg_up(const char *private_key_b64, uint16_t listen_port, const char *tunnel_ip,
                       const char *tunnel_mask);
int pm_metal_net_wg_down(void);
int pm_metal_net_wg_peer_add(const char *public_key_b64, const char *endpoint_ip, uint16_t endpoint_port,
                             const char *allowed_ip, const char *allowed_mask);
int pm_metal_net_wg_peer_del(const char *public_key_b64);
int pm_metal_net_wg_status(pm_metal_net_wg_status_t *out);
int pm_metal_net_wg_ready(void);

/**
 * Sync façade for bring-up/smoke: wg0 (listen) ↔ wg1 (client) over 127.0.0.1 UDP.
 * Pumps ip_poll until both peers report last_handshake_sec != 0 (or timeout).
 * Tears down wg1; leaves wg0 up. Product waits use up/peer/status + net pump.
 */
int pm_metal_net_wg_handshake_smoke(void);

#ifdef __cplusplus
}
#endif

#endif
