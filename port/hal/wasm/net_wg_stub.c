/*
 * Browser net.wg — same C ABI; no UDP WireGuard tunnel in the browser seat.
 */
#include "pymergetic/metal/net/wg/__init__.h"

#include <string.h>

static void zero_status(pm_metal_net_wg_status_t *out)
{
    if (out) {
        memset(out, 0, sizeof(*out));
    }
}

int pm_metal_net_wg_up_named(const char *ifname, const char *private_key_b64, uint16_t listen_port,
                             const char *tunnel_ip, const char *tunnel_mask)
{
    (void)ifname;
    (void)private_key_b64;
    (void)listen_port;
    (void)tunnel_ip;
    (void)tunnel_mask;
    return -1;
}

int pm_metal_net_wg_down_named(const char *ifname)
{
    (void)ifname;
    return -1;
}

int pm_metal_net_wg_peer_add_named(const char *ifname, const char *public_key_b64,
                                   const char *endpoint_ip, uint16_t endpoint_port,
                                   const char *allowed_ip, const char *allowed_mask)
{
    (void)ifname;
    (void)public_key_b64;
    (void)endpoint_ip;
    (void)endpoint_port;
    (void)allowed_ip;
    (void)allowed_mask;
    return -1;
}

int pm_metal_net_wg_peer_del_named(const char *ifname, const char *public_key_b64)
{
    (void)ifname;
    (void)public_key_b64;
    return -1;
}

int pm_metal_net_wg_status_named(const char *ifname, pm_metal_net_wg_status_t *out)
{
    (void)ifname;
    zero_status(out);
    return -1;
}

int pm_metal_net_wg_up(const char *private_key_b64, uint16_t listen_port, const char *tunnel_ip,
                       const char *tunnel_mask)
{
    return pm_metal_net_wg_up_named(PM_METAL_NET_WG_IFNAME, private_key_b64, listen_port, tunnel_ip,
                                    tunnel_mask);
}

int pm_metal_net_wg_down(void)
{
    return pm_metal_net_wg_down_named(PM_METAL_NET_WG_IFNAME);
}

int pm_metal_net_wg_peer_add(const char *public_key_b64, const char *endpoint_ip,
                             uint16_t endpoint_port, const char *allowed_ip,
                             const char *allowed_mask)
{
    return pm_metal_net_wg_peer_add_named(PM_METAL_NET_WG_IFNAME, public_key_b64, endpoint_ip,
                                          endpoint_port, allowed_ip, allowed_mask);
}

int pm_metal_net_wg_peer_del(const char *public_key_b64)
{
    return pm_metal_net_wg_peer_del_named(PM_METAL_NET_WG_IFNAME, public_key_b64);
}

int pm_metal_net_wg_status(pm_metal_net_wg_status_t *out)
{
    return pm_metal_net_wg_status_named(PM_METAL_NET_WG_IFNAME, out);
}

int pm_metal_net_wg_ready(void)
{
    return 0;
}

int pm_metal_net_wg_handshake_smoke(void)
{
    return -1;
}
