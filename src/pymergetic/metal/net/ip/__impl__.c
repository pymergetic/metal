/* pymergetic.metal.net.ip — IPv4 + ICMP + UDP + TCP + lo.
 * This TU owns the card's state and its exported face; the stack itself is
 * split by concern: __link__.c (routes/interfaces/ARP), __wire__.c (datagram
 * in/out), __tcp__.c (TCP). Shared internals live in __priv__.h. */
#include "pymergetic/metal/net/ip/__exports__.h"

#include "pymergetic/metal/net/ip/__priv__.h"

#include "pymergetic/metal/async.h"
#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/util/lock.h"
#include "pymergetic/util/mem.h"

#include <string.h>

pm_util_mem_arena_t *pm_ip_arena;
uint32_t pm_ip_lo_up;
uint32_t pm_ip_lo_addr_be = PM_METAL_IP_LO_BE;
pm_util_lock_t pm_ip_lock;
struct pm_metal_sock pm_ip_sk[PM_METAL_IP_SOCK_MAX];
struct pm_metal_ip_l2 pm_ip_l2[PM_METAL_IP_L2_MAX];
uint32_t pm_ip_l2_n;
int32_t pm_ip_l2_cur = -1;
uint32_t pm_ip_if_pending_be;
uint32_t pm_ip_if_pending_mask;
struct pm_metal_ip_rt pm_ip_rt[PM_METAL_IP_RT_MAX];
struct pm_metal_ip_arp pm_ip_arp[PM_METAL_IP_ARP_MAX];
int32_t pm_ip_rx_l2 = -1;
uint8_t pm_ip_ping_out[PM_METAL_IP_RX_MAX];
uint32_t pm_ip_ping_len;
uint16_t pm_ip_ping_id;

static uint16_t s_eph = 49152u;
static uint16_t s_ping_seq;

/* Locked internals: defined later in this TU, but referenced by the if_up
 * path before their definitions. External entry points take pm_ip_lock and
 * call these; internal flow calls them with the lock already held. */
int32_t pm_ip_l2_attach_locked(int32_t h);
int32_t pm_ip_socket_locked(int32_t type);
int32_t pm_ip_close_locked(int32_t fd);
int32_t pm_ip_bind_locked(int32_t fd, uint32_t addr_be, uint16_t port_host);
int32_t pm_ip_bind_l2_locked(int32_t fd, int32_t h);
int32_t pm_ip_listen_locked(int32_t fd, int32_t backlog);
int32_t pm_ip_accept_locked(int32_t fd);
int32_t pm_ip_connect_locked(int32_t fd, uint32_t addr_be, uint16_t port_host);
int32_t pm_ip_send_locked(int32_t fd, const uint8_t *buf, uint32_t len);
int32_t pm_ip_recv_locked(int32_t fd, uint8_t *buf, uint32_t len);
int32_t pm_ip_sendto_locked(int32_t fd, const uint8_t *buf, uint32_t len, uint32_t addr_be,
    uint16_t port_host);
int32_t pm_ip_recvfrom_locked(int32_t fd, uint8_t *buf, uint32_t len, uint32_t *addr_be,
    uint16_t *port_host);
int32_t pm_ip_route_add_locked(uint32_t dst_be, uint32_t mask_be, uint32_t gw_be, int32_t h);
int32_t pm_ip_route_del_locked(uint32_t dst_be, uint32_t mask_be);
int32_t pm_ip_gw_set_locked(uint32_t gw_be);
int32_t pm_ip_arp_resolve_locked(uint32_t addr_be);
void pm_ip_pump_locked(void);

/* The IP stack is one shared, inherently single-threaded set of tables
 * (sockets, interfaces, routes, ARP, NIC rx). Background async runners poll
 * the NIC via pm_metal_net_ip_pump() concurrently with the main thread's
 * synchronous socket work (tests, run_until waiters, firmware main). Without a
 * gate that polling races the data path — a worker mid-rx can strand or corrupt
 * the bytes a synchronous recv/send/accept is about to consume, and a lost
 * wakeup makes a fetch stall. One RS lock serializes the whole card. Exported
 * entry points take pm_ip_lock and call the pm_ip_*_locked internals, so a lock
 * is never held across a nested exported call (RS lock is non-reentrant). */

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
    pm_util_lock_init(&pm_ip_lock);
    memset(pm_ip_sk, 0, sizeof(pm_ip_sk));
    pm_ip_l2_clear();
    pm_ip_lo_up = 0;
    pm_ip_rx_l2 = -1;
    pm_ip_ping_len = 0;
    pm_ip_ping_id = 0;
    s_eph = 49152u;
    s_ping_seq = 0;
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
    pm_util_lock_acquire(&pm_ip_lock);
    pm_ip_lo_up = 1;
    pm_util_lock_release(&pm_ip_lock);
    return 0;
}

int32_t pm_metal_net_ip_lo_ready(void) {
    pm_util_lock_acquire(&pm_ip_lock);
    int32_t r = (pm_ip_arena != NULL && pm_ip_lo_up != 0) ? 1 : 0;
    pm_util_lock_release(&pm_ip_lock);
    return r;
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
    int32_t rc = -1;
    pm_util_lock_acquire(&pm_ip_lock);
    if (pm_ip_l2_attach_locked(h) == 0) {
        rc = pm_ip_if_up_mask(h, addr_be, mask_be);
    }
    pm_util_lock_release(&pm_ip_lock);
    return rc;
}

int32_t pm_metal_net_ip_if_up_h(int32_t h, uint32_t addr_be) {
    return pm_metal_net_ip_if_up_mask(h, addr_be, PM_METAL_IP_MASK24);
}

int32_t pm_metal_net_ip_if_up(uint32_t addr_be) {
    int32_t rc = -1;
    int32_t h;
    if (pm_ip_arena == NULL || addr_be == 0) {
        return -1;
    }
    pm_util_lock_acquire(&pm_ip_lock);
    if (pm_ip_l2_cur >= 0) {
        rc = pm_ip_if_up_mask(pm_ip_l2_cur, addr_be, PM_METAL_IP_MASK24);
    } else if (pm_ip_l2_n != 0) {
        rc = pm_ip_if_up_mask(pm_ip_l2[0].h, addr_be, PM_METAL_IP_MASK24);
    } else {
        h = first_netdev();
        if (h >= 0) {
            rc = pm_ip_l2_attach_locked(h);
            if (rc == 0) {
                rc = pm_ip_if_up_mask(h, addr_be, PM_METAL_IP_MASK24);
            }
        } else {
            pm_ip_if_pending_be = addr_be;
            pm_ip_if_pending_mask = PM_METAL_IP_MASK24;
            rc = 0;
        }
    }
    pm_util_lock_release(&pm_ip_lock);
    return rc;
}

uint32_t pm_metal_net_ip_if_addr(int32_t h) {
    pm_util_lock_acquire(&pm_ip_lock);
    uint32_t v = pm_ip_l2_addr_of(h);
    pm_util_lock_release(&pm_ip_lock);
    return v;
}

void pm_ip_pump_locked(void) {
    uint32_t i;
    pm_ip_tcp_check_timeouts();
    pm_ip_arp_tick();
    for (i = 0; i < pm_ip_l2_n; i++) {
        (void)pm_metal_drivers_net_poll(pm_ip_l2[i].h);
    }
}

void pm_metal_net_ip_pump(void) {
    pm_util_lock_acquire(&pm_ip_lock);
    pm_ip_pump_locked();
    pm_util_lock_release(&pm_ip_lock);
}

int32_t pm_ip_route_add_locked(uint32_t dst_be, uint32_t mask_be, uint32_t gw_be, int32_t h) {
    if (pm_ip_arena == NULL || !pm_ip_l2_has(h)) {
        return -1;
    }
    pm_ip_rt_upsert(dst_be & mask_be, mask_be, gw_be, h);
    return 0;
}

int32_t pm_metal_net_ip_route_add(uint32_t dst_be, uint32_t mask_be, uint32_t gw_be, int32_t h) {
    pm_util_lock_acquire(&pm_ip_lock);
    int32_t rc = pm_ip_route_add_locked(dst_be, mask_be, gw_be, h);
    pm_util_lock_release(&pm_ip_lock);
    return rc;
}

int32_t pm_ip_route_del_locked(uint32_t dst_be, uint32_t mask_be) {
    if (pm_ip_arena == NULL) {
        return -1;
    }
    pm_ip_rt_del(dst_be & mask_be, mask_be);
    return 0;
}

int32_t pm_metal_net_ip_route_del(uint32_t dst_be, uint32_t mask_be) {
    pm_util_lock_acquire(&pm_ip_lock);
    int32_t rc = pm_ip_route_del_locked(dst_be, mask_be);
    pm_util_lock_release(&pm_ip_lock);
    return rc;
}

int32_t pm_ip_gw_set_locked(uint32_t gw_be) {
    int32_t h = pm_ip_l2_cur;
    if (pm_ip_arena == NULL) {
        return -1;
    }
    if (h < 0) {
        h = pm_ip_l2_n != 0 ? pm_ip_l2[0].h : -1;
    }
    if (h < 0) {
        return -1;
    }
    pm_ip_rt_upsert(0, 0, gw_be, h);
    return 0;
}

int32_t pm_metal_net_ip_gw_set(uint32_t gw_be) {
    pm_util_lock_acquire(&pm_ip_lock);
    int32_t rc = pm_ip_gw_set_locked(gw_be);
    pm_util_lock_release(&pm_ip_lock);
    return rc;
}

uint32_t pm_ip_gw_locked(void) {
    uint32_t i;
    for (i = 0; i < PM_METAL_IP_RT_MAX; i++) {
        if (pm_ip_rt[i].used && pm_ip_rt[i].mask_be == 0) {
            return pm_ip_rt[i].gw_be;
        }
    }
    return 0;
}

uint32_t pm_metal_net_ip_gw(void) {
    pm_util_lock_acquire(&pm_ip_lock);
    uint32_t v = pm_ip_gw_locked();
    pm_util_lock_release(&pm_ip_lock);
    return v;
}

int32_t pm_ip_arp_resolve_locked(uint32_t addr_be) {
    uint8_t mac[6];
    uint32_t hop = 0;
    int32_t h;
    if (pm_ip_arena == NULL || addr_be == 0) {
        return -1;
    }
    h = pm_ip_route_out(-1, 0, addr_be, &hop);
    if (h < 0) {
        return -1;
    }
    if (pm_ip_arp_lookup(h, hop, mac) == 1) {
        return 1;
    }
    pm_ip_arp_ask(h, hop);
    return 0;
}

int32_t pm_metal_net_ip_arp_resolve(uint32_t addr_be) {
    pm_util_lock_acquire(&pm_ip_lock);
    int32_t rc = pm_ip_arp_resolve_locked(addr_be);
    pm_util_lock_release(&pm_ip_lock);
    return rc;
}

int32_t pm_ip_l2_attach_locked(int32_t h) {
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

int32_t pm_metal_net_ip_l2_attach(int32_t h) {
    pm_util_lock_acquire(&pm_ip_lock);
    int32_t rc = pm_ip_l2_attach_locked(h);
    pm_util_lock_release(&pm_ip_lock);
    return rc;
}

int32_t pm_metal_net_ip_l2_detach(int32_t h) {
    uint32_t i;
    uint32_t j;
    uint32_t gw;
    int32_t rc = -1;
    pm_util_lock_acquire(&pm_ip_lock);
    gw = pm_ip_gw_locked();
    {
        uint32_t found = 0;
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
            rc = -1;
            goto out;
        }
        pm_ip_rt_del_h(h);
        if (pm_ip_l2_cur == h) {
            pm_ip_l2_cur = pm_ip_l2_n != 0 ? pm_ip_l2[pm_ip_l2_n - 1u].h : -1;
            if (pm_ip_l2_cur >= 0) {
                /* The gateway outlives the interface it was reached through: the
                 * remaining interface is on the same wire in every case we serve. */
                pm_ip_rt_upsert(0, 0, gw, pm_ip_l2_cur);
            }
        }
        rc = 0;
    }
out:
    pm_util_lock_release(&pm_ip_lock);
    return rc;
}

int32_t pm_metal_net_l2_attach(const char *name, const pm_metal_net_l2_ops_t *ops) {
    int32_t dt;
    int32_t h;
    int32_t rc = -1;
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
    pm_util_lock_acquire(&pm_ip_lock);
    rc = pm_ip_l2_attach_locked(h);
    pm_util_lock_release(&pm_ip_lock);
    return rc;
}

int32_t pm_ip_socket_locked(int32_t type) {
    if (type == PM_METAL_NET_IP_SOCK_STREAM) {
        return pm_ip_sock_alloc(SK_TCP);
    }
    if (type == PM_METAL_NET_IP_SOCK_DGRAM) {
        return pm_ip_sock_alloc(SK_UDP);
    }
    return -1;
}

int32_t pm_metal_net_ip_socket(int32_t type) {
    pm_util_lock_acquire(&pm_ip_lock);
    int32_t rc = pm_ip_socket_locked(type);
    pm_util_lock_release(&pm_ip_lock);
    return rc;
}

int32_t pm_ip_close_locked(int32_t fd) {
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

int32_t pm_metal_net_ip_close(int32_t fd) {
    pm_util_lock_acquire(&pm_ip_lock);
    int32_t rc = pm_ip_close_locked(fd);
    pm_util_lock_release(&pm_ip_lock);
    return rc;
}

int32_t pm_ip_bind_locked(int32_t fd, uint32_t addr_be, uint16_t port_host) {
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

int32_t pm_metal_net_ip_bind(int32_t fd, uint32_t addr_be, uint16_t port_host) {
    pm_util_lock_acquire(&pm_ip_lock);
    int32_t rc = pm_ip_bind_locked(fd, addr_be, port_host);
    pm_util_lock_release(&pm_ip_lock);
    return rc;
}

int32_t pm_ip_bind_l2_locked(int32_t fd, int32_t h) {
    struct pm_metal_sock *s = sock_get(fd, 0);
    if (s == NULL || !pm_ip_l2_has(h)) {
        return -1;
    }
    s->l2_h = h;
    return 0;
}

int32_t pm_metal_net_ip_bind_l2(int32_t fd, int32_t h) {
    pm_util_lock_acquire(&pm_ip_lock);
    int32_t rc = pm_ip_bind_l2_locked(fd, h);
    pm_util_lock_release(&pm_ip_lock);
    return rc;
}

int32_t pm_ip_listen_locked(int32_t fd, int32_t backlog) {
    struct pm_metal_sock *s = sock_get(fd, SK_TCP);
    (void)backlog;
    if (s == NULL || !s->bound) {
        return -1;
    }
    s->tcp_st = TCP_LISTEN;
    return 0;
}

int32_t pm_metal_net_ip_listen(int32_t fd, int32_t backlog) {
    pm_util_lock_acquire(&pm_ip_lock);
    int32_t rc = pm_ip_listen_locked(fd, backlog);
    pm_util_lock_release(&pm_ip_lock);
    return rc;
}

int32_t pm_ip_accept_locked(int32_t fd) {
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

int32_t pm_metal_net_ip_accept(int32_t fd) {
    pm_util_lock_acquire(&pm_ip_lock);
    int32_t rc = pm_ip_accept_locked(fd);
    pm_util_lock_release(&pm_ip_lock);
    return rc;
}

int32_t pm_ip_connect_locked(int32_t fd, uint32_t addr_be, uint16_t port_host) {
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

int32_t pm_metal_net_ip_connect(int32_t fd, uint32_t addr_be, uint16_t port_host) {
    pm_util_lock_acquire(&pm_ip_lock);
    int32_t rc = pm_ip_connect_locked(fd, addr_be, port_host);
    pm_util_lock_release(&pm_ip_lock);
    return rc;
}

int32_t pm_ip_send_locked(int32_t fd, const uint8_t *buf, uint32_t len) {
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

int32_t pm_metal_net_ip_send(int32_t fd, const uint8_t *buf, uint32_t len) {
    pm_util_lock_acquire(&pm_ip_lock);
    int32_t rc = pm_ip_send_locked(fd, buf, len);
    pm_util_lock_release(&pm_ip_lock);
    return rc;
}

int32_t pm_ip_recv_locked(int32_t fd, uint8_t *buf, uint32_t len) {
    struct pm_metal_sock *s = sock_get(fd, SK_TCP);
    if (s == NULL || buf == NULL) {
        return -1;
    }
    if (s->rx_len == 0) {
        if (s->peer_fin) {
            return -2;
        }
        /* Empty is wait, not an error — same as connect. A step that runs
         * without current_task (SMP runner vs run_until) used to return -1
         * after the first segment and abort a live GET. */
        pm_metal_async_task_t *cur = pm_metal_async_current_task();
        if (cur != NULL) {
            s->waiter = cur;
        }
        return 0;
    }
    uint32_t n = s->rx_len < len ? s->rx_len : len;
    memcpy(buf, s->rx, n);
    if (n < s->rx_len) {
        memmove(s->rx, s->rx + n, s->rx_len - n);
    }
    s->rx_len -= n;
    return (int32_t)n;
}

int32_t pm_metal_net_ip_recv(int32_t fd, uint8_t *buf, uint32_t len) {
    pm_util_lock_acquire(&pm_ip_lock);
    int32_t rc = pm_ip_recv_locked(fd, buf, len);
    pm_util_lock_release(&pm_ip_lock);
    return rc;
}

int32_t pm_ip_sendto_locked(int32_t fd, const uint8_t *buf, uint32_t len, uint32_t addr_be,
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
    pm_ip_l4_stamp(pkt, total);
    pm_ip_output_via(pcb->l2_h, pkt, total);
    return (int32_t)len;
}

int32_t pm_metal_net_ip_sendto(int32_t fd, const uint8_t *buf, uint32_t len, uint32_t addr_be,
    uint16_t port_host) {
    pm_util_lock_acquire(&pm_ip_lock);
    int32_t rc = pm_ip_sendto_locked(fd, buf, len, addr_be, port_host);
    pm_util_lock_release(&pm_ip_lock);
    return rc;
}

int32_t pm_ip_recvfrom_locked(int32_t fd, uint8_t *buf, uint32_t len, uint32_t *addr_be,
    uint16_t *port_host) {
    struct pm_metal_sock *pcb = sock_get(fd, SK_UDP);
    if (pcb == NULL || buf == NULL) {
        return -1;
    }
    if (pcb->rx_len == 0) {
        /* Nothing queued is 0 bytes, never an error: a caller that polls has to
         * be able to tell an empty socket from a bad one. A task also parks. */
        pm_metal_async_task_t *cur = pm_metal_async_current_task();
        if (cur != NULL) {
            pcb->waiter = cur;
        }
        return 0;
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

int32_t pm_metal_net_ip_recvfrom(int32_t fd, uint8_t *buf, uint32_t len, uint32_t *addr_be,
    uint16_t *port_host) {
    pm_util_lock_acquire(&pm_ip_lock);
    int32_t rc = pm_ip_recvfrom_locked(fd, buf, len, addr_be, port_host);
    pm_util_lock_release(&pm_ip_lock);
    return rc;
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
    pm_ip_write_be32(pkt + 12, pm_ip_src_for(NULL, addr_be));
    pm_ip_write_be32(pkt + 16, addr_be);
    uint16_t cs = pm_ip_csum(pkt, 20);
    pm_ip_write_be16(pkt + 10, cs);
    pkt[20] = 8;
    pm_ip_ping_id = (uint16_t)(0x4d45u + s_ping_seq);
    pm_ip_write_be16(pkt + 24, pm_ip_ping_id);
    pm_ip_write_be16(pkt + 26, ++s_ping_seq);
    memcpy(pkt + 28, payload, plen);
    cs = pm_ip_csum(pkt + 20, icmp_len);
    pm_ip_write_be16(pkt + 22, cs);
    pm_util_lock_acquire(&pm_ip_lock);
    pm_ip_ping_len = 0;
    pm_ip_output(pkt, total);
    pm_util_lock_release(&pm_ip_lock);
    if (pm_ip_ping_len == 0) {
        /* Off-box the reply arrives on a later poll, and the first send may still
         * be waiting on ARP. Loopback answered inside pm_ip_output already.
         * The spin cap is the real bound on a seat whose monotonic clock is a
         * call counter (no cycle counter under it), where the deadline alone
         * would take billions of polls to pass. Short locked rounds keep the
         * stack (and background runners) moving instead of holding pm_ip_lock. */
        uint64_t deadline = pm_metal_async_mono_us() + PM_METAL_IP_PING_WAIT_US;
        uint32_t spins;
        for (spins = 0; spins < PM_METAL_IP_PING_SPINS; spins++) {
            pm_util_lock_acquire(&pm_ip_lock);
            pm_ip_pump_locked();
            uint32_t got = pm_ip_ping_len;
            pm_util_lock_release(&pm_ip_lock);
            if (got != 0 || pm_metal_async_mono_us() >= deadline) {
                break;
            }
        }
    }
    pm_util_lock_acquire(&pm_ip_lock);
    uint32_t have = pm_ip_ping_len;
    uint8_t tmp[PM_METAL_IP_RX_MAX];
    if (have != 0) {
        uint32_t n = have < *out_len ? have : *out_len;
        memcpy(tmp, pm_ip_ping_out, n);
        pm_util_lock_release(&pm_ip_lock);
        memcpy(out, tmp, n);
        *out_len = n;
        return 0;
    }
    pm_util_lock_release(&pm_ip_lock);
    return -2;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_init, pm_metal_net_ip_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_deinit, pm_metal_net_ip_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_lo_up, pm_metal_net_ip_lo_up, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_lo_ready, pm_metal_net_ip_lo_ready, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_if_up, pm_metal_net_ip_if_up, int32_t(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_if_up_h, pm_metal_net_ip_if_up_h, int32_t(int32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_if_up_mask, pm_metal_net_ip_if_up_mask, int32_t(int32_t, uint32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_if_addr, pm_metal_net_ip_if_addr, uint32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_pump, pm_metal_net_ip_pump, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_route_add, pm_metal_net_ip_route_add, int32_t(uint32_t, uint32_t, uint32_t, int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_route_del, pm_metal_net_ip_route_del, int32_t(uint32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_gw_set, pm_metal_net_ip_gw_set, int32_t(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_gw, pm_metal_net_ip_gw, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.ip, pm_metal_net_ip_arp_resolve, pm_metal_net_ip_arp_resolve, int32_t(uint32_t));
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
