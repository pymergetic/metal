/* pymergetic.metal.net.ip — IPv4 + ICMP + UDP + TCP + lo.
 * This TU owns the card's state and its exported face; the stack itself is
 * split by concern: __link__.c (routes/interfaces/ARP), __wire__.c (datagram
 * in/out), __tcp__.c (TCP). Shared internals live in __priv__.h. */
#include "pymergetic/metal/net/ip/__exports__.h"

#include "pymergetic/metal/net/ip/__priv__.h"

#include "pymergetic/metal/async.h"
#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/util/mem.h"

#include <string.h>

pm_util_mem_arena_t *pm_ip_arena;
uint32_t pm_ip_lo_up;
uint32_t pm_ip_lo_addr_be = PM_METAL_IP_LO_BE;
struct pm_metal_sock pm_ip_sk[PM_METAL_IP_SOCK_MAX];
struct pm_metal_ip_l2 pm_ip_l2[PM_METAL_IP_L2_MAX];
uint32_t pm_ip_l2_n;
int32_t pm_ip_l2_cur = -1;
uint32_t pm_ip_if_pending_be;
uint32_t pm_ip_if_pending_mask;
struct pm_metal_ip_rt pm_ip_rt[PM_METAL_IP_RT_MAX];
int32_t pm_ip_rx_l2 = -1;
uint8_t pm_ip_eth_dmac[6];
uint32_t pm_ip_eth_dmac_ok;
uint8_t pm_ip_ping_out[PM_METAL_IP_RX_MAX];
uint32_t pm_ip_ping_len;

static uint16_t s_eph = 49152u;

void pm_ip_sock_wake(struct pm_metal_sock *s) {
    if (s->waiter != NULL) {
        (void)pm_metal_async_post_task(s->waiter);
        s->waiter = NULL;
    }
}

int32_t pm_ip_sock_alloc(uint8_t kind) {
    uint32_t i;
    for (i = 0; i < PM_METAL_IP_SOCK_MAX; i++) {
        if (!pm_ip_sk[i].used) {
            memset(&pm_ip_sk[i], 0, sizeof(pm_ip_sk[i]));
            pm_ip_sk[i].used = 1;
            pm_ip_sk[i].kind = kind;
            pm_ip_sk[i].listen_fd = -1;
            pm_ip_sk[i].l2_h = -1;
            return (int32_t)i;
        }
    }
    return -1;
}

static struct pm_metal_sock *sock_get(int32_t fd, uint8_t kind) {
    if (fd < 0 || (uint32_t)fd >= PM_METAL_IP_SOCK_MAX || !pm_ip_sk[fd].used) {
        return NULL;
    }
    if (kind != 0 && pm_ip_sk[fd].kind != kind) {
        return NULL;
    }
    return &pm_ip_sk[fd];
}

static uint16_t eph_port(void) {
    uint16_t p = s_eph++;
    if (s_eph < 49152u) {
        s_eph = 49152u;
    }
    return p;
}

int32_t pm_metal_net_ip_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    pm_ip_arena = arena;
    memset(pm_ip_sk, 0, sizeof(pm_ip_sk));
    pm_ip_l2_clear();
    pm_ip_lo_up = 0;
    pm_ip_rx_l2 = -1;
    pm_ip_ping_len = 0;
    s_eph = 49152u;
    (void)pm_ip_arena;
    return 0;
}

void pm_metal_net_ip_deinit(void) {
    memset(pm_ip_sk, 0, sizeof(pm_ip_sk));
    pm_ip_lo_up = 0;
    pm_ip_l2_clear();
    pm_ip_rx_l2 = -1;
    pm_ip_arena = NULL;
}

int32_t pm_metal_net_ip_lo_up(void) {
    if (pm_ip_arena == NULL) {
        return -1;
    }
    pm_ip_lo_up = 1;
    return 0;
}

static int32_t first_netdev(void) {
    int32_t i;
    for (i = 0; i < 32; i++) {
        if (pm_metal_drivers_net_dt_id(i) >= 0) {
            return i;
        }
    }
    return -1;
}

int32_t pm_metal_net_ip_if_up_mask(int32_t h, uint32_t addr_be, uint32_t mask_be) {
    if (pm_metal_net_ip_l2_attach(h) != 0) {
        return -1;
    }
    return pm_ip_if_up_mask(h, addr_be, mask_be);
}

int32_t pm_metal_net_ip_if_up_h(int32_t h, uint32_t addr_be) {
    return pm_metal_net_ip_if_up_mask(h, addr_be, PM_METAL_IP_MASK24);
}

int32_t pm_metal_net_ip_if_up(uint32_t addr_be) {
    int32_t h;
    if (pm_ip_arena == NULL || addr_be == 0) {
        return -1;
    }
    if (pm_ip_l2_cur >= 0) {
        return pm_ip_if_up_mask(pm_ip_l2_cur, addr_be, PM_METAL_IP_MASK24);
    }
    if (pm_ip_l2_n != 0) {
        return pm_ip_if_up_mask(pm_ip_l2[0].h, addr_be, PM_METAL_IP_MASK24);
    }
    h = first_netdev();
    if (h >= 0) {
        return pm_metal_net_ip_if_up_mask(h, addr_be, PM_METAL_IP_MASK24);
    }
    pm_ip_if_pending_be = addr_be;
    pm_ip_if_pending_mask = PM_METAL_IP_MASK24;
    return 0;
}

uint32_t pm_metal_net_ip_if_addr(int32_t h) {
    return pm_ip_l2_addr_of(h);
}

void pm_metal_net_ip_pump(void) {
    uint32_t i;
    pm_ip_tcp_check_timeouts();
    for (i = 0; i < pm_ip_l2_n; i++) {
        (void)pm_metal_drivers_net_poll(pm_ip_l2[i].h);
    }
}

int32_t pm_metal_net_ip_l2_attach(int32_t h) {
    uint32_t i;
    if (pm_metal_drivers_net_dt_id(h) < 0) {
        return -1;
    }
    for (i = 0; i < pm_ip_l2_n; i++) {
        if (pm_ip_l2[i].h == h) {
            pm_ip_l2_cur = h;
            pm_ip_l2_apply_pending();
            return 0;
        }
    }
    if (pm_ip_l2_n >= PM_METAL_IP_L2_MAX) {
        return -1;
    }
    pm_ip_l2[pm_ip_l2_n].h = h;
    pm_ip_l2[pm_ip_l2_n].addr_be = 0;
    pm_ip_l2[pm_ip_l2_n].mask_be = 0;
    pm_ip_l2_n++;
    pm_ip_l2_cur = h;
    pm_ip_l2_apply_pending();
    return 0;
}

int32_t pm_metal_net_ip_l2_detach(int32_t h) {
    uint32_t i;
    uint32_t j;
    int32_t found = 0;
    for (i = 0; i < pm_ip_l2_n; i++) {
        if (pm_ip_l2[i].h == h) {
            for (j = i; j + 1u < pm_ip_l2_n; j++) {
                pm_ip_l2[j] = pm_ip_l2[j + 1u];
            }
            pm_ip_l2_n--;
            found = 1;
            break;
        }
    }
    if (!found) {
        return -1;
    }
    pm_ip_rt_del_h(h);
    if (pm_ip_l2_cur == h) {
        pm_ip_l2_cur = pm_ip_l2_n != 0 ? pm_ip_l2[pm_ip_l2_n - 1u].h : -1;
        if (pm_ip_l2_cur >= 0) {
            pm_ip_rt_upsert(0, 0, pm_ip_l2_cur);
        }
    }
    return 0;
}

int32_t pm_metal_net_l2_attach(const char *name, const pm_metal_net_l2_ops_t *ops) {
    int32_t dt;
    int32_t h;
    if (name == NULL || name[0] == 0 || ops == NULL) {
        return -1;
    }
    dt = pm_metal_dt_add(PM_METAL_DT_CLASS_NET, name, PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, 0);
    if (dt < 0) {
        return -1;
    }
    h = pm_metal_drivers_net_bind(dt, ops);
    if (h < 0) {
        return h;
    }
    return pm_metal_net_ip_l2_attach(h);
}

int32_t pm_metal_net_ip_socket(int32_t type) {
    if (type == PM_METAL_NET_IP_SOCK_STREAM) {
        return pm_ip_sock_alloc(SK_TCP);
    }
    if (type == PM_METAL_NET_IP_SOCK_DGRAM) {
        return pm_ip_sock_alloc(SK_UDP);
    }
    return -1;
}

int32_t pm_metal_net_ip_close(int32_t fd) {
    struct pm_metal_sock *s = sock_get(fd, 0);
    if (s == NULL) {
        return -1;
    }
    if (s->kind == SK_TCP && s->tcp_st == TCP_ESTAB) {
        pm_ip_tcp_xmit(s, (uint8_t)(TCP_FIN | TCP_ACK), NULL, 0);
        s->tcp_st = TCP_FIN_WAIT;
    }
    memset(s, 0, sizeof(*s));
    return 0;
}

int32_t pm_metal_net_ip_bind(int32_t fd, uint32_t addr_be, uint16_t port_host) {
    struct pm_metal_sock *s = sock_get(fd, 0);
    int32_t h;
    if (s == NULL) {
        return -1;
    }
    s->laddr_be = addr_be;
    s->lport = port_host;
    s->bound = 1;
    h = pm_ip_l2_h_for_addr(addr_be);
    if (h >= 0) {
        s->l2_h = h;
    }
    return 0;
}

int32_t pm_metal_net_ip_bind_l2(int32_t fd, int32_t h) {
    struct pm_metal_sock *s = sock_get(fd, 0);
    if (s == NULL || !pm_ip_l2_has(h)) {
        return -1;
    }
    s->l2_h = h;
    return 0;
}

int32_t pm_metal_net_ip_listen(int32_t fd, int32_t backlog) {
    struct pm_metal_sock *s = sock_get(fd, SK_TCP);
    (void)backlog;
    if (s == NULL || !s->bound) {
        return -1;
    }
    s->tcp_st = TCP_LISTEN;
    return 0;
}

int32_t pm_metal_net_ip_accept(int32_t fd) {
    struct pm_metal_sock *s = sock_get(fd, SK_TCP);
    if (s == NULL || s->tcp_st != TCP_LISTEN) {
        return -1;
    }
    if (s->accept_n == 0) {
        pm_metal_async_task_t *cur = pm_metal_async_current_task();
        if (cur != NULL) {
            s->waiter = cur;
            return -2;
        }
        return -1;
    }
    int32_t child = s->accept_q[0];
    uint32_t i;
    for (i = 1; i < s->accept_n; i++) {
        s->accept_q[i - 1u] = s->accept_q[i];
    }
    s->accept_n--;
    return child;
}

int32_t pm_metal_net_ip_connect(int32_t fd, uint32_t addr_be, uint16_t port_host) {
    struct pm_metal_sock *s = sock_get(fd, SK_TCP);
    if (s == NULL) {
        return -1;
    }
    if (!s->bound) {
        s->laddr_be = pm_ip_src_for(s, addr_be);
        s->lport = eph_port();
        s->bound = 1;
    } else if (s->laddr_be == 0 || s->laddr_be == 0xffffffffu) {
        s->laddr_be = pm_ip_src_for(s, addr_be);
    }
    s->raddr_be = addr_be;
    s->rport = port_host;
    s->iss = 2000u;
    s->snd_nxt = s->iss;
    s->snd_una = s->iss;
    s->tcp_st = TCP_SYN_SENT;
    pm_ip_tcp_xmit(s, TCP_SYN, NULL, 0);
    if (s->tcp_st == TCP_ESTAB) {
        return 1;
    }
    pm_metal_async_task_t *cur = pm_metal_async_current_task();
    if (cur != NULL) {
        s->waiter = cur;
        return 0;
    }
    return 0;
}

int32_t pm_metal_net_ip_send(int32_t fd, const uint8_t *buf, uint32_t len) {
    struct pm_metal_sock *s = sock_get(fd, SK_TCP);
    if (s == NULL || buf == NULL || s->tcp_st != TCP_ESTAB) {
        return -1;
    }
    if (len > PM_METAL_IP_TCP_MSS) {
        len = PM_METAL_IP_TCP_MSS;
    }
    pm_ip_tcp_xmit(s, (uint8_t)(TCP_PSH | TCP_ACK), buf, len);
    return (int32_t)len;
}

int32_t pm_metal_net_ip_recv(int32_t fd, uint8_t *buf, uint32_t len) {
    struct pm_metal_sock *s = sock_get(fd, SK_TCP);
    if (s == NULL || buf == NULL) {
        return -1;
    }
    if (s->rx_len == 0) {
        if (s->peer_fin) {
            return -2;
        }
        pm_metal_async_task_t *cur = pm_metal_async_current_task();
        if (cur != NULL) {
            s->waiter = cur;
            return 0;
        }
        return -1;
    }
    uint32_t n = s->rx_len < len ? s->rx_len : len;
    memcpy(buf, s->rx, n);
    if (n < s->rx_len) {
        memmove(s->rx, s->rx + n, s->rx_len - n);
    }
    s->rx_len -= n;
    return (int32_t)n;
}

int32_t pm_metal_net_ip_sendto(int32_t fd, const uint8_t *buf, uint32_t len, uint32_t addr_be,
    uint16_t port_host) {
    struct pm_metal_sock *pcb = sock_get(fd, SK_UDP);
    if (pcb == NULL || buf == NULL) {
        return -1;
    }
    if (!pcb->bound || len > PM_METAL_IP_RX_MAX) {
        return -1;
    }
    uint32_t total = 20u + 8u + len;
    uint8_t pkt[PM_METAL_IP_PKT_MAX];
    if (total > PM_METAL_IP_PKT_MAX) {
        return -1;
    }
    memset(pkt, 0, total);
    pkt[0] = 0x45;
    pm_ip_write_be16(pkt + 2, (uint16_t)total);
    pkt[8] = 64;
    pkt[9] = 17;
    pm_ip_write_be32(pkt + 12, pm_ip_src_for(pcb, addr_be));
    pm_ip_write_be32(pkt + 16, addr_be);
    uint16_t cs = pm_ip_csum(pkt, 20);
    pm_ip_write_be16(pkt + 10, cs);
    pm_ip_write_be16(pkt + 20, pcb->lport);
    pm_ip_write_be16(pkt + 22, port_host);
    pm_ip_write_be16(pkt + 24, (uint16_t)(8u + len));
    memcpy(pkt + 28, buf, len);
    pm_ip_output(pkt, total);
    return (int32_t)len;
}

int32_t pm_metal_net_ip_recvfrom(int32_t fd, uint8_t *buf, uint32_t len, uint32_t *addr_be,
    uint16_t *port_host) {
    struct pm_metal_sock *pcb = sock_get(fd, SK_UDP);
    if (pcb == NULL || buf == NULL) {
        return -1;
    }
    if (pcb->rx_len == 0) {
        pm_metal_async_task_t *cur = pm_metal_async_current_task();
        if (cur != NULL) {
            pcb->waiter = cur;
            return 0;
        }
        return -1;
    }
    uint32_t n = pcb->rx_len < len ? pcb->rx_len : len;
    memcpy(buf, pcb->rx, n);
    if (addr_be != NULL) {
        *addr_be = pcb->rx_addr_be;
    }
    if (port_host != NULL) {
        *port_host = pcb->rx_port;
    }
    pcb->rx_len = 0;
    return (int32_t)n;
}

int32_t pm_metal_net_ip_ping4(uint32_t addr_be, const uint8_t *payload, uint32_t plen, uint8_t *out,
    uint32_t *out_len) {
    if (!pm_ip_lo_up || payload == NULL || out == NULL || out_len == NULL) {
        return -1;
    }
    uint32_t icmp_len = 8u + plen;
    uint32_t total = 20u + icmp_len;
    uint8_t pkt[PM_METAL_IP_PKT_MAX];
    if (total > PM_METAL_IP_PKT_MAX) {
        return -1;
    }
    memset(pkt, 0, total);
    pkt[0] = 0x45;
    pm_ip_write_be16(pkt + 2, (uint16_t)total);
    pkt[8] = 64;
    pkt[9] = 1;
    pm_ip_write_be32(pkt + 12, pm_ip_lo_addr_be);
    pm_ip_write_be32(pkt + 16, addr_be);
    uint16_t cs = pm_ip_csum(pkt, 20);
    pm_ip_write_be16(pkt + 10, cs);
    pkt[20] = 8;
    memcpy(pkt + 28, payload, plen);
    cs = pm_ip_csum(pkt + 20, icmp_len);
    pm_ip_write_be16(pkt + 22, cs);
    pm_ip_ping_len = 0;
    pm_ip_output(pkt, total);
    if (pm_ip_ping_len == 0) {
        return -1;
    }
    uint32_t n = pm_ip_ping_len < *out_len ? pm_ip_ping_len : *out_len;
    memcpy(out, pm_ip_ping_out, n);
    *out_len = n;
    return 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_init, pm_metal_net_ip_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_deinit, pm_metal_net_ip_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_lo_up, pm_metal_net_ip_lo_up, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_if_up, pm_metal_net_ip_if_up, int32_t(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_if_up_h, pm_metal_net_ip_if_up_h, int32_t(int32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_if_up_mask, pm_metal_net_ip_if_up_mask, int32_t(int32_t, uint32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_if_addr, pm_metal_net_ip_if_addr, uint32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_pump, pm_metal_net_ip_pump, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_l2_attach, pm_metal_net_ip_l2_attach, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_l2_detach, pm_metal_net_ip_l2_detach, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_l2_attach, pm_metal_net_l2_attach, int32_t(const char *, const pm_metal_net_l2_ops_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_rx, pm_metal_net_ip_rx, int32_t(const uint8_t *, uint16_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_rx_from, pm_metal_net_ip_rx_from, int32_t(int32_t, const uint8_t *, uint16_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_socket, pm_metal_net_ip_socket, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_close, pm_metal_net_ip_close, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_bind, pm_metal_net_ip_bind, int32_t(int32_t, uint32_t, uint16_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_bind_l2, pm_metal_net_ip_bind_l2, int32_t(int32_t, int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_listen, pm_metal_net_ip_listen, int32_t(int32_t, int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_connect, pm_metal_net_ip_connect, int32_t(int32_t, uint32_t, uint16_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_send, pm_metal_net_ip_send, int32_t(int32_t, const uint8_t *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_recv, pm_metal_net_ip_recv, int32_t(int32_t, uint8_t *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_accept, pm_metal_net_ip_accept, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_sendto, pm_metal_net_ip_sendto, int32_t(int32_t, const uint8_t *, uint32_t, uint32_t, uint16_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_recvfrom, pm_metal_net_ip_recvfrom, int32_t(int32_t, uint8_t *, uint32_t, uint32_t *, uint16_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_ping4, pm_metal_net_ip_ping4, int32_t(uint32_t, const uint8_t *, uint32_t, uint8_t *, uint32_t *));

PM_MOD_BOOT_READY_C(pymergetic.metal.net.ip, pm_metal_net_ip_init, pm_metal_net_ip_deinit, pm_metal_net_ip_lo_up);
PM_MOD_BOOTDEP_C(pymergetic.metal.net.ip, pymergetic.metal.async);
PM_MOD_BOOTDEP_C(pymergetic.metal.net.ip, pymergetic.metal.drivers.net);
PM_MOD_BOOTDEP_C(pymergetic.metal.net.ip, pymergetic.metal.dt);
