#include "pymergetic/metal/net/wg/__init__.h"

#include <stdio.h>
#include <string.h>

#include "lwip/err.h"
#include "lwip/ip.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "wireguardif.h"

#include "pymergetic/metal/async/board_time.h"
#include "pymergetic/metal/net/ip/__init__.h"
#include "pymergetic/metal/net/ip/cfg.h"
#include "pymergetic/metal/net/ip/lwip_start.h"

typedef struct {
    struct wireguardif_init_data init;
    char priv[PM_METAL_NET_WG_KEY_B64_MAX];
    char peer_pub[PM_METAL_NET_WG_KEY_B64_MAX];
    char name[PM_METAL_NET_IP_IFNAME_MAX];
    struct netif *netif;
    uint8_t peer_idx;
    uint16_t port;
    int up;
} wg_slot_t;

static wg_slot_t g_wg[PM_METAL_NET_WG_MAX_IFS];

/* `wg` + decimal index → slot; NULL/"" → wg0. Rejects non-wg names and out-of-budget indices. */
static int slot_for_name(const char *ifname)
{
    unsigned idx = 0;
    const char *p;

    if (ifname == NULL || ifname[0] == '\0') {
        return 0;
    }
    if (ifname[0] != 'w' || ifname[1] != 'g') {
        return -1;
    }
    p = ifname + 2;
    if (*p < '0' || *p > '9') {
        return -1;
    }
    while (*p >= '0' && *p <= '9') {
        idx = idx * 10u + (unsigned)(*p - '0');
        if (idx >= (unsigned)PM_METAL_NET_WG_MAX_IFS) {
            return -1;
        }
        p++;
    }
    if (*p != '\0') {
        return -1;
    }
    return (int)idx;
}

int pm_metal_net_wg_up_named(const char *ifname, const char *private_key_b64, uint16_t listen_port,
                             const char *tunnel_ip, const char *tunnel_mask)
{
    ip4_addr_t ip, nm, gw;
    int slot = slot_for_name(ifname);
    wg_slot_t *s;
    if (slot < 0 || private_key_b64 == NULL || tunnel_ip == NULL || tunnel_mask == NULL) {
        return -1;
    }
    s = &g_wg[slot];
    if (s->up) {
        return 0;
    }
    if (pm_metal_ip_parse_ipv4(tunnel_ip, &ip) != 0 || pm_metal_ip_parse_ipv4(tunnel_mask, &nm) != 0) {
        return -1;
    }
    IP4_ADDR(&gw, 0, 0, 0, 0);
    snprintf(s->name, sizeof(s->name), "wg%u", (unsigned)slot);
    snprintf(s->priv, sizeof(s->priv), "%s", private_key_b64);
    s->port = listen_port ? listen_port : WIREGUARDIF_DEFAULT_PORT;
    memset(&s->init, 0, sizeof(s->init));
    s->init.private_key = s->priv;
    s->init.listen_port = s->port;
    s->init.bind_netif = NULL;
    s->peer_idx = WIREGUARDIF_INVALID_INDEX;
    s->peer_pub[0] = '\0';

    s->netif = (struct netif *)pm_metal_net_ip_register_netif(s->name, "wireguard", NULL);
    if (s->netif == NULL) {
        return -1;
    }
    if (netif_add(s->netif, &ip, &nm, &gw, &s->init, wireguardif_init, ip_input) == NULL) {
        pm_metal_net_ip_unregister_named(s->name);
        s->netif = NULL;
        return -1;
    }
    netif_set_up(s->netif);
    s->up = 1;
    pm_metal_net_ip_bump_if_gen();
    return 0;
}

int pm_metal_net_wg_down_named(const char *ifname)
{
    int slot = slot_for_name(ifname);
    wg_slot_t *s;
    if (slot < 0) {
        return -1;
    }
    s = &g_wg[slot];
    if (!s->up) {
        return 0;
    }
    if (s->peer_idx != WIREGUARDIF_INVALID_INDEX && s->netif != NULL) {
        (void)wireguardif_remove_peer(s->netif, s->peer_idx);
        s->peer_idx = WIREGUARDIF_INVALID_INDEX;
    }
    if (s->netif != NULL) {
        netif_set_down(s->netif);
        netif_remove(s->netif);
    }
    pm_metal_net_ip_unregister_named(s->name);
    s->netif = NULL;
    s->up = 0;
    s->peer_pub[0] = '\0';
    pm_metal_net_ip_bump_if_gen();
    return 0;
}

int pm_metal_net_wg_peer_add_named(const char *ifname, const char *public_key_b64,
                                   const char *endpoint_ip, uint16_t endpoint_port,
                                   const char *allowed_ip, const char *allowed_mask)
{
    struct wireguardif_peer peer;
    ip4_addr_t a, m, ep;
    int slot = slot_for_name(ifname);
    wg_slot_t *s;
    if (slot < 0) {
        return -1;
    }
    s = &g_wg[slot];
    if (!s->up || s->netif == NULL || public_key_b64 == NULL || allowed_ip == NULL ||
        allowed_mask == NULL) {
        return -1;
    }
    wireguardif_peer_init(&peer);
    peer.public_key = public_key_b64;
    peer.preshared_key = NULL;
    if (pm_metal_ip_parse_ipv4(allowed_ip, &a) != 0 || pm_metal_ip_parse_ipv4(allowed_mask, &m) != 0) {
        return -1;
    }
    ip_addr_copy_from_ip4(peer.allowed_ip, a);
    ip_addr_copy_from_ip4(peer.allowed_mask, m);
    if (endpoint_ip != NULL && endpoint_ip[0] != '\0' &&
        pm_metal_ip_parse_ipv4(endpoint_ip, &ep) == 0) {
        ip_addr_copy_from_ip4(peer.endpoint_ip, ep);
        peer.endport_port = endpoint_port ? endpoint_port : WIREGUARDIF_DEFAULT_PORT;
    }
    peer.keep_alive = 5;
    if (wireguardif_add_peer(s->netif, &peer, &s->peer_idx) != ERR_OK) {
        return -1;
    }
    snprintf(s->peer_pub, sizeof(s->peer_pub), "%s", public_key_b64);
    if (endpoint_ip != NULL && endpoint_ip[0] != '\0') {
        (void)wireguardif_connect(s->netif, s->peer_idx);
    }
    pm_metal_net_ip_bump_if_gen();
    return 0;
}

int pm_metal_net_wg_peer_del_named(const char *ifname, const char *public_key_b64)
{
    int slot = slot_for_name(ifname);
    wg_slot_t *s;
    (void)public_key_b64;
    if (slot < 0) {
        return -1;
    }
    s = &g_wg[slot];
    if (!s->up || s->netif == NULL || s->peer_idx == WIREGUARDIF_INVALID_INDEX) {
        return -1;
    }
    (void)wireguardif_remove_peer(s->netif, s->peer_idx);
    s->peer_idx = WIREGUARDIF_INVALID_INDEX;
    s->peer_pub[0] = '\0';
    pm_metal_net_ip_bump_if_gen();
    return 0;
}

int pm_metal_net_wg_status_named(const char *ifname, pm_metal_net_wg_status_t *out)
{
    pm_metal_net_ip_ifcfg_t cfg;
    ip_addr_t cur_ip;
    u16_t cur_port;
    int slot = slot_for_name(ifname);
    wg_slot_t *s;
    if (out == NULL || slot < 0) {
        return -1;
    }
    s = &g_wg[slot];
    memset(out, 0, sizeof(*out));
    if (s->name[0] != '\0') {
        snprintf(out->name, sizeof(out->name), "%s", s->name);
    } else {
        snprintf(out->name, sizeof(out->name), "wg%u", (unsigned)slot);
    }
    out->up = s->up;
    out->listen_port = s->port;
    snprintf(out->peer_public, sizeof(out->peer_public), "%s", s->peer_pub);
    if (s->up && pm_metal_net_ip_if_get_named(s->name, &cfg) == 0) {
        snprintf(out->ip, sizeof(out->ip), "%s", cfg.ip);
    }
    if (s->netif != NULL && s->peer_idx != WIREGUARDIF_INVALID_INDEX &&
        wireguardif_peer_is_up(s->netif, s->peer_idx, &cur_ip, &cur_port) == ERR_OK) {
        out->last_handshake_sec = 1;
    }
    return 0;
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

int pm_metal_net_wg_peer_add(const char *public_key_b64, const char *endpoint_ip, uint16_t endpoint_port,
                             const char *allowed_ip, const char *allowed_mask)
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
    return g_wg[0].up;
}

int pm_metal_net_wg_handshake_smoke(void)
{
    /* Fresh keypairs for in-guest lo handshake (A=wg0 listen, B=wg1 client). */
    static const char a_priv[] = "UBd4LECdLxBcZdz9niTBJYhCDnJNp7STArVyY8XQWko=";
    static const char a_pub[] = "eWQ6FO8d4GEhnwCwDb1bYzE7RhPmGfY3dfzeB4JipDI=";
    static const char b_priv[] = "EEK4zhAevEjiKB6GTshZmRaQqroaO8dC/6TeJwiTjl4=";
    static const char b_pub[] = "i5w3svMY/vdrDZkxGcrg1gNhwMQjmMg/E7zNXaCDlBA=";
    pm_metal_net_wg_status_t st0, st1;
    int i;

    (void)pm_metal_net_wg_down_named("wg1");
    (void)pm_metal_net_wg_down_named("wg0");

    if (pm_metal_net_wg_up_named("wg0", a_priv, 51820, "192.168.40.1", "255.255.255.0") != 0) {
        return -1;
    }
    if (pm_metal_net_wg_up_named("wg1", b_priv, 51821, "192.168.40.2", "255.255.255.0") != 0) {
        (void)pm_metal_net_wg_down_named("wg0");
        return -1;
    }
    /* Server peer (no endpoint) + client peer pointing at lo:51820. */
    if (pm_metal_net_wg_peer_add_named("wg0", b_pub, NULL, 0, "192.168.40.2", "255.255.255.255") !=
        0) {
        goto fail;
    }
    if (pm_metal_net_wg_peer_add_named("wg1", a_pub, "127.0.0.1", 51820, "192.168.40.1",
                                       "255.255.255.255") != 0) {
        goto fail;
    }

    for (i = 0; i < 800; i++) {
        pm_metal_net_ip_poll();
        pm_metal_board_time_advance_us(5000);
        if (pm_metal_net_wg_status_named("wg0", &st0) == 0 &&
            pm_metal_net_wg_status_named("wg1", &st1) == 0 && st0.last_handshake_sec != 0 &&
            st1.last_handshake_sec != 0) {
            (void)pm_metal_net_wg_down_named("wg1");
            /* Leave wg0 up so F7 / if_status show a live WireGuard iface. */
            return 0;
        }
    }

fail:
    (void)pm_metal_net_wg_down_named("wg1");
    (void)pm_metal_net_wg_down_named("wg0");
    return -1;
}
