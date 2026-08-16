/* pymergetic.metal.net.dhcp — DHCPv4 DISCOVER → OFFER, then if_up. */
#include "pymergetic/metal/net/dhcp/__exports__.h"

#include "pymergetic/metal/net/ip.h"

#include <string.h>

#define BOOT_MIN 240u
#define COOKIE 0x63825363u

static pm_util_mem_arena_t *s_arena;
static int32_t s_fd = -1;
static uint32_t s_offer_be = 0x0a000002u;
static uint32_t s_xid = 1;

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint8_t opt_type(const uint8_t *p, uint32_t n) {
    uint32_t i = BOOT_MIN;
    while (i + 2u <= n && p[i] != 255) {
        if (p[i] == 0) {
            i++;
            continue;
        }
        uint8_t t = p[i];
        uint8_t l = p[i + 1];
        if (i + 2u + l > n) {
            break;
        }
        if (t == 53 && l >= 1) {
            return p[i + 2];
        }
        i += 2u + l;
    }
    return 0;
}

int32_t pm_metal_net_dhcp_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    s_fd = -1;
    s_offer_be = 0x0a000002u;
    return 0;
}

void pm_metal_net_dhcp_deinit(void) {
    if (s_fd >= 0) {
        (void)pm_metal_net_ip_close(s_fd);
        s_fd = -1;
    }
    s_arena = NULL;
}

int32_t pm_metal_net_dhcp_set_offer(uint32_t addr_be) {
    if (addr_be == 0) {
        return -1;
    }
    s_offer_be = addr_be;
    return 0;
}

int32_t pm_metal_net_dhcp_listen(uint32_t addr_be, uint16_t port) {
    if (s_arena == NULL) {
        return -1;
    }
    if (s_fd >= 0) {
        return 0;
    }
    s_fd = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    if (s_fd < 0 || pm_metal_net_ip_bind(s_fd, addr_be, port) != 0) {
        if (s_fd >= 0) {
            (void)pm_metal_net_ip_close(s_fd);
            s_fd = -1;
        }
        return -1;
    }
    return 0;
}

int32_t pm_metal_net_dhcp_poll(void) {
    uint8_t buf[548];
    uint32_t src = 0;
    uint16_t sport = 0;
    int32_t n;
    if (s_fd < 0) {
        return 0;
    }
    n = pm_metal_net_ip_recvfrom(s_fd, buf, sizeof(buf), &src, &sport);
    if (n < (int32_t)BOOT_MIN || buf[0] != 1) {
        return 0;
    }
    if (opt_type(buf, (uint32_t)n) != 1) {
        return 0;
    }
    buf[0] = 2;
    put_be32(buf + 16, s_offer_be);
    buf[BOOT_MIN] = 53;
    buf[BOOT_MIN + 1] = 1;
    buf[BOOT_MIN + 2] = 2;
    buf[BOOT_MIN + 3] = 255;
    (void)pm_metal_net_ip_sendto(s_fd, buf, BOOT_MIN + 4u, src, sport);
    return 0;
}

int32_t pm_metal_net_dhcp_discover(uint32_t server_be, uint16_t server_port, uint32_t *yiaddr_be) {
    uint8_t q[BOOT_MIN + 4u];
    uint8_t buf[548];
    int32_t fd;
    int32_t n;
    uint32_t xid;
    if (yiaddr_be == NULL || s_arena == NULL) {
        return -1;
    }
    fd = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_DGRAM);
    if (fd < 0 || pm_metal_net_ip_bind(fd, 0x7f000001u, 68) != 0) {
        if (fd >= 0) {
            (void)pm_metal_net_ip_close(fd);
        }
        return -1;
    }
    memset(q, 0, sizeof(q));
    q[0] = 1;
    q[1] = 1;
    q[2] = 6;
    xid = s_xid++;
    put_be32(q + 4, xid);
    put_be32(q + 236, COOKIE);
    q[BOOT_MIN] = 53;
    q[BOOT_MIN + 1] = 1;
    q[BOOT_MIN + 2] = 1;
    q[BOOT_MIN + 3] = 255;
    if (pm_metal_net_ip_sendto(fd, q, sizeof(q), server_be, server_port) < 0) {
        (void)pm_metal_net_ip_close(fd);
        return -1;
    }
    (void)pm_metal_net_dhcp_poll();
    n = pm_metal_net_ip_recvfrom(fd, buf, sizeof(buf), NULL, NULL);
    (void)pm_metal_net_ip_close(fd);
    if (n < (int32_t)BOOT_MIN || buf[0] != 2 || get_be32(buf + 4) != xid) {
        return -1;
    }
    if (opt_type(buf, (uint32_t)n) != 2) {
        return -1;
    }
    *yiaddr_be = get_be32(buf + 16);
    return 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.net.dhcp, pm_metal_net_dhcp_init, pm_metal_net_dhcp_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.dhcp, pm_metal_net_dhcp_deinit, pm_metal_net_dhcp_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.dhcp, pm_metal_net_dhcp_set_offer, pm_metal_net_dhcp_set_offer, int32_t(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.dhcp, pm_metal_net_dhcp_listen, pm_metal_net_dhcp_listen, int32_t(uint32_t, uint16_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.dhcp, pm_metal_net_dhcp_poll, pm_metal_net_dhcp_poll, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.dhcp, pm_metal_net_dhcp_discover, pm_metal_net_dhcp_discover, int32_t(uint32_t, uint16_t, uint32_t *));

PM_MOD_BOOT_C(pymergetic.metal.net.dhcp, pm_metal_net_dhcp_init, pm_metal_net_dhcp_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.net.dhcp, pymergetic.metal.net.ip);
