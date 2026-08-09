#include "ip_lwip_internal.h"

#include "lwip/dns.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"

#include "pymergetic/metal/async/await.h"
#include "pymergetic/metal/async/handle.h"

static void sock_clear(msock_t *s)
{
    memset(s, 0, sizeof(*s));
}

static void tcp_err_cb(void *arg, err_t err)
{
    msock_t *s = (msock_t *)arg;
    (void)err;
    if (s == NULL) {
        return;
    }
    s->tcp = NULL;
    s->conn_done = 1;
    s->conn_ok = 0;
    s->recv_err = 1;
    s->recv_done = 1;
    if (s->wait_connect) {
        pm_metal_async_set_result_u32(s->wait_connect, 0u);
        pm_metal_async_wake(s->wait_connect);
        s->wait_connect = 0;
    }
    if (s->wait_recv) {
        pm_metal_async_set_result_u32(s->wait_recv, (uint32_t)-1);
        pm_metal_async_wake(s->wait_recv);
        s->wait_recv = 0;
    }
}

static err_t tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    msock_t *s = (msock_t *)arg;
    (void)tpcb;
    if (s == NULL) {
        if (p != NULL) {
            pbuf_free(p);
        }
        return ERR_OK;
    }
    if (p == NULL || err != ERR_OK) {
        s->recv_err = 1;
        s->recv_done = 1;
        if (s->wait_recv) {
            pm_metal_async_set_result_u32(s->wait_recv, s->recv_got ? s->recv_got : (uint32_t)-1);
            pm_metal_async_wake(s->wait_recv);
            s->wait_recv = 0;
        }
        return ERR_OK;
    }
    if (s->rx_q == NULL) {
        s->rx_q = p;
    } else {
        pbuf_cat(s->rx_q, p);
    }
    if (s->wait_recv && s->recv_buf != NULL) {
        uint32_t n = pbuf_copy_partial(s->rx_q, s->recv_buf, s->recv_cap, 0);
        if (n > 0) {
            s->rx_q = pbuf_free_header(s->rx_q, (u16_t)n);
            tcp_recved(tpcb, (u16_t)n);
            s->recv_got = n;
            s->recv_done = 1;
            pm_metal_async_set_result_u32(s->wait_recv, n);
            pm_metal_async_wake(s->wait_recv);
            s->wait_recv = 0;
        }
    }
    return ERR_OK;
}

static err_t tcp_connected_cb(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    msock_t *s = (msock_t *)arg;
    (void)tpcb;
    if (s == NULL) {
        return ERR_ARG;
    }
    s->conn_done = 1;
    s->conn_ok = (err == ERR_OK) ? 1 : 0;
    if (s->wait_connect) {
        pm_metal_async_set_result_u32(s->wait_connect, s->conn_ok ? 1u : 0u);
        pm_metal_async_wake(s->wait_connect);
        s->wait_connect = 0;
    }
    return ERR_OK;
}

static err_t tcp_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    msock_t *s = (msock_t *)arg;
    pm_metal_net_ip_sock_h nh;
    uint32_t i;
    if (s == NULL || newpcb == NULL || err != ERR_OK) {
        if (newpcb != NULL) {
            tcp_abort(newpcb);
        }
        return ERR_VAL;
    }
    for (i = 1; i <= METAL_NET_MAX_SOCKS; i++) {
        if (!g_metal_socks[i].used) {
            break;
        }
    }
    if (i > METAL_NET_MAX_SOCKS) {
        tcp_abort(newpcb);
        return ERR_MEM;
    }
    sock_clear(&g_metal_socks[i]);
    g_metal_socks[i].used = 1;
    g_metal_socks[i].domain = s->domain;
    g_metal_socks[i].type = PM_METAL_NET_IP_SOCK_STREAM;
    g_metal_socks[i].tcp = newpcb;
    g_metal_socks[i].conn_done = 1;
    g_metal_socks[i].conn_ok = 1;
    tcp_arg(newpcb, &g_metal_socks[i]);
    tcp_recv(newpcb, tcp_recv_cb);
    tcp_err(newpcb, tcp_err_cb);
    nh = (pm_metal_net_ip_sock_h)i;
    if (s->accept_q_n < METAL_NET_ACCEPT_Q) {
        uint8_t slot = (uint8_t)((s->accept_q_r + s->accept_q_n) % METAL_NET_ACCEPT_Q);
        s->accept_q[slot] = nh;
        s->accept_q_n++;
    } else {
        s->accept_pcb = newpcb;
    }
    if (s->wait_accept) {
        pm_metal_net_ip_sock_h out = 0;
        if (s->accept_q_n > 0) {
            out = s->accept_q[s->accept_q_r];
            s->accept_q_r = (uint8_t)((s->accept_q_r + 1u) % METAL_NET_ACCEPT_Q);
            s->accept_q_n--;
        }
        pm_metal_async_set_result_u32(s->wait_accept, (uint32_t)out);
        pm_metal_async_wake(s->wait_accept);
        s->wait_accept = 0;
    }
    return ERR_OK;
}

static void udp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr,
                        u16_t port)
{
    msock_t *s = (msock_t *)arg;
    (void)pcb;
    if (s == NULL || p == NULL) {
        if (p != NULL) {
            pbuf_free(p);
        }
        return;
    }
    if (s->udp_rx_n >= METAL_UDP_RX_MAX) {
        pbuf_free(p);
        return;
    }
    {
        uint16_t slot = (uint16_t)((s->udp_rx_head + s->udp_rx_n) % METAL_UDP_RX_MAX);
        s->udp_rx[slot].p = p;
        s->udp_rx[slot].addr = *addr;
        s->udp_rx[slot].port = port;
        s->udp_rx_n++;
    }
    if (s->wait_recv && s->recv_buf != NULL) {
        uint32_t n = pm_metal_net_ip_try_recv((pm_metal_net_ip_sock_h)(s - g_metal_socks),
                                              s->recv_buf, s->recv_cap);
        if (n != 0u && n != (uint32_t)-1) {
            s->recv_got = n;
            s->recv_done = 1;
            pm_metal_async_set_result_u32(s->wait_recv, n);
            pm_metal_async_wake(s->wait_recv);
            s->wait_recv = 0;
        }
    }
}

static void dns_found(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    uint32_t h = (uint32_t)(uintptr_t)arg;
    (void)name;
    if (ipaddr != NULL) {
        ipaddr_ntoa_r(ipaddr, g_metal_dns_last, sizeof(g_metal_dns_last));
        pm_metal_async_set_result_u32(h, 1u);
    } else {
        g_metal_dns_last[0] = '\0';
        pm_metal_async_set_result_u32(h, 0u);
    }
    pm_metal_async_wake(h);
}

void pm_metal_ip_sock_wake_poll(void)
{
    /* Connect/recv/accept wakes are callback-driven; nothing periodic here. */
}

pm_metal_net_ip_sock_h pm_metal_net_ip_socket(uint32_t domain, uint32_t type)
{
    uint32_t i;
    if (!g_metal_lwip_inited) {
        return PM_METAL_NET_IP_SOCK_INVALID;
    }
    if (domain != PM_METAL_NET_IP_AF_INET) {
        return PM_METAL_NET_IP_SOCK_INVALID;
    }
    for (i = 1; i <= METAL_NET_MAX_SOCKS; i++) {
        if (g_metal_socks[i].used) {
            continue;
        }
        sock_clear(&g_metal_socks[i]);
        g_metal_socks[i].used = 1;
        g_metal_socks[i].domain = domain;
        g_metal_socks[i].type = type;
        if (type == PM_METAL_NET_IP_SOCK_STREAM) {
            g_metal_socks[i].tcp = tcp_new();
            if (g_metal_socks[i].tcp == NULL) {
                sock_clear(&g_metal_socks[i]);
                return PM_METAL_NET_IP_SOCK_INVALID;
            }
            tcp_arg(g_metal_socks[i].tcp, &g_metal_socks[i]);
            tcp_err(g_metal_socks[i].tcp, tcp_err_cb);
        } else if (type == PM_METAL_NET_IP_SOCK_DGRAM) {
            g_metal_socks[i].udp = udp_new();
            if (g_metal_socks[i].udp == NULL) {
                sock_clear(&g_metal_socks[i]);
                return PM_METAL_NET_IP_SOCK_INVALID;
            }
            udp_recv(g_metal_socks[i].udp, udp_recv_cb, &g_metal_socks[i]);
        } else {
            sock_clear(&g_metal_socks[i]);
            return PM_METAL_NET_IP_SOCK_INVALID;
        }
        return (pm_metal_net_ip_sock_h)i;
    }
    return PM_METAL_NET_IP_SOCK_INVALID;
}

void pm_metal_net_ip_close(pm_metal_net_ip_sock_h h)
{
    msock_t *s;
    if (h == 0 || h > METAL_NET_MAX_SOCKS) {
        return;
    }
    s = &g_metal_socks[h];
    if (!s->used) {
        return;
    }
    if (s->tcp != NULL) {
        tcp_arg(s->tcp, NULL);
        tcp_recv(s->tcp, NULL);
        tcp_err(s->tcp, NULL);
        tcp_close(s->tcp);
        s->tcp = NULL;
    }
    if (s->udp != NULL) {
        udp_remove(s->udp);
        s->udp = NULL;
    }
    while (s->rx_q != NULL) {
        struct pbuf *n = s->rx_q->next;
        s->rx_q->next = NULL;
        pbuf_free(s->rx_q);
        s->rx_q = n;
    }
    while (s->udp_rx_n > 0) {
        pbuf_free(s->udp_rx[s->udp_rx_head].p);
        s->udp_rx_head = (uint16_t)((s->udp_rx_head + 1u) % METAL_UDP_RX_MAX);
        s->udp_rx_n--;
    }
    sock_clear(s);
}

int32_t pm_metal_net_ip_bind(pm_metal_net_ip_sock_h h, uint32_t port)
{
    msock_t *s;
    if (h == 0 || h > METAL_NET_MAX_SOCKS) {
        return -1;
    }
    s = &g_metal_socks[h];
    if (!s->used) {
        return -1;
    }
    if (s->type == PM_METAL_NET_IP_SOCK_STREAM) {
        return (s->tcp != NULL && tcp_bind(s->tcp, IP_ANY_TYPE, (u16_t)port) == ERR_OK) ? 0 : -1;
    }
    if (s->type == PM_METAL_NET_IP_SOCK_DGRAM) {
        return (s->udp != NULL && udp_bind(s->udp, IP_ANY_TYPE, (u16_t)port) == ERR_OK) ? 0 : -1;
    }
    return -1;
}

int32_t pm_metal_net_ip_bind_if(pm_metal_net_ip_sock_h h, const char *ifname)
{
    msock_t *s;
    metal_net_iface_t *mif;
    if (h == 0 || h > METAL_NET_MAX_SOCKS) {
        return -1;
    }
    s = &g_metal_socks[h];
    if (!s->used) {
        return -1;
    }
    if (ifname == NULL) {
        s->bound_if = -1;
        return 0;
    }
    mif = pm_metal_ip_iface_by_name(ifname);
    if (mif == NULL) {
        return -1;
    }
    s->bound_if = (int32_t)(mif - g_metal_ifaces);
    if (s->tcp != NULL) {
        tcp_bind_netif(s->tcp, &mif->netif);
    }
    if (s->udp != NULL) {
        udp_bind_netif(s->udp, &mif->netif);
    }
    return 0;
}

static int start_tcp_connect(msock_t *s, const ip_addr_t *addr, uint16_t port)
{
    err_t e;
    if (s->tcp == NULL) {
        return -1;
    }
    tcp_recv(s->tcp, tcp_recv_cb);
    s->conn_done = 0;
    s->conn_ok = 0;
    e = tcp_connect(s->tcp, addr, port, tcp_connected_cb);
    return (e == ERR_OK) ? 0 : -1;
}

typedef struct {
    msock_t *s;
    uint16_t port;
    uint32_t ah;
} dns_connect_t;

static dns_connect_t g_dns_conn;

static void dns_connect_found(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    dns_connect_t *c = (dns_connect_t *)arg;
    (void)name;
    if (c == NULL || c->s == NULL) {
        return;
    }
    if (ipaddr == NULL) {
        pm_metal_async_set_result_u32(c->ah, 0u);
        pm_metal_async_wake(c->ah);
        c->s->wait_connect = 0;
        return;
    }
    c->s->remote = *ipaddr;
    c->s->have_remote = 1;
    ipaddr_ntoa_r(ipaddr, g_metal_dns_last, sizeof(g_metal_dns_last));
    if (start_tcp_connect(c->s, ipaddr, c->port) != 0) {
        pm_metal_async_set_result_u32(c->ah, 0u);
        pm_metal_async_wake(c->ah);
        c->s->wait_connect = 0;
        return;
    }
    c->s->wait_connect = c->ah;
    if (c->s->conn_done) {
        pm_metal_async_set_result_u32(c->ah, c->s->conn_ok ? 1u : 0u);
        pm_metal_async_wake(c->ah);
        c->s->wait_connect = 0;
    }
}

uint32_t pm_metal_net_ip_connect(pm_metal_net_ip_sock_h h, const char *host, uint32_t port)
{
    msock_t *s;
    ip_addr_t addr;
    uint32_t ah;
    err_t e;
    if (h == 0 || h > METAL_NET_MAX_SOCKS || host == NULL) {
        return 0;
    }
    s = &g_metal_socks[h];
    if (!s->used) {
        return 0;
    }
    s->remote_port = (uint16_t)port;
    if (s->type == PM_METAL_NET_IP_SOCK_DGRAM) {
        if (pm_metal_ip_parse_host(host, &addr) != 0) {
            return 0;
        }
        s->remote = addr;
        s->have_remote = 1;
        return pm_metal_async_completed_u32(1u);
    }
    if (s->tcp == NULL) {
        return 0;
    }
    ah = pm_metal_async_park();
    if (ah == 0u) {
        return 0;
    }
    if (pm_metal_ip_parse_host(host, &addr) == 0) {
        s->remote = addr;
        s->have_remote = 1;
        if (start_tcp_connect(s, &addr, (uint16_t)port) != 0) {
            pm_metal_async_set_result_u32(ah, 0u);
            pm_metal_async_wake(ah);
            return ah;
        }
        s->wait_connect = ah;
        if (s->conn_done) {
            pm_metal_async_set_result_u32(ah, s->conn_ok ? 1u : 0u);
            pm_metal_async_wake(ah);
            s->wait_connect = 0;
        }
        return ah;
    }
    g_dns_conn.s = s;
    g_dns_conn.port = (uint16_t)port;
    g_dns_conn.ah = ah;
    e = dns_gethostbyname(host, &addr, dns_connect_found, &g_dns_conn);
    if (e == ERR_OK) {
        dns_connect_found(host, &addr, &g_dns_conn);
        return ah;
    }
    if (e == ERR_INPROGRESS) {
        return ah;
    }
    pm_metal_async_set_result_u32(ah, 0u);
    pm_metal_async_wake(ah);
    return ah;
}

uint32_t pm_metal_net_ip_listen(pm_metal_net_ip_sock_h h, uint32_t port)
{
    msock_t *s;
    struct tcp_pcb *l;
    if (h == 0 || h > METAL_NET_MAX_SOCKS) {
        return 0;
    }
    s = &g_metal_socks[h];
    if (!s->used || s->tcp == NULL) {
        return 0;
    }
    if (tcp_bind(s->tcp, IP_ANY_TYPE, (u16_t)port) != ERR_OK) {
        return 0;
    }
    l = tcp_listen(s->tcp);
    if (l == NULL) {
        return 0;
    }
    s->tcp = l;
    s->listening = 1;
    tcp_arg(l, s);
    tcp_accept(l, tcp_accept_cb);
    return pm_metal_async_completed_u32(1u);
}

uint32_t pm_metal_net_ip_accept(pm_metal_net_ip_sock_h h)
{
    msock_t *s;
    uint32_t ah;
    if (h == 0 || h > METAL_NET_MAX_SOCKS) {
        return 0;
    }
    s = &g_metal_socks[h];
    if (!s->used || !s->listening) {
        return 0;
    }
    if (s->accept_q_n > 0) {
        pm_metal_net_ip_sock_h out = s->accept_q[s->accept_q_r];
        s->accept_q_r = (uint8_t)((s->accept_q_r + 1u) % METAL_NET_ACCEPT_Q);
        s->accept_q_n--;
        return pm_metal_async_completed_u32((uint32_t)out);
    }
    ah = pm_metal_async_park();
    if (ah == 0u) {
        return 0;
    }
    s->wait_accept = ah;
    return ah;
}

uint32_t pm_metal_net_ip_send(pm_metal_net_ip_sock_h h, const void *ptr, uint32_t len)
{
    msock_t *s;
    err_t e;
    if (h == 0 || h > METAL_NET_MAX_SOCKS || ptr == NULL || len == 0) {
        return 0;
    }
    s = &g_metal_socks[h];
    if (!s->used) {
        return 0;
    }
    if (s->type == PM_METAL_NET_IP_SOCK_STREAM) {
        if (s->tcp == NULL) {
            return 0;
        }
        e = tcp_write(s->tcp, ptr, (u16_t)len, TCP_WRITE_FLAG_COPY);
        if (e != ERR_OK) {
            return 0;
        }
        tcp_output(s->tcp);
        return len;
    }
    if (s->type == PM_METAL_NET_IP_SOCK_DGRAM && s->have_remote && s->udp != NULL) {
        return pm_metal_net_ip_sendto(h, ptr, len, NULL, s->remote_port);
    }
    return 0;
}

uint32_t pm_metal_net_ip_sendto(pm_metal_net_ip_sock_h h, const void *ptr, uint32_t len,
                                const char *host, uint32_t port)
{
    msock_t *s;
    struct pbuf *p;
    ip_addr_t addr;
    if (h == 0 || h > METAL_NET_MAX_SOCKS || ptr == NULL || len == 0) {
        return 0;
    }
    s = &g_metal_socks[h];
    if (!s->used || s->udp == NULL) {
        return 0;
    }
    if (host != NULL) {
        if (pm_metal_ip_parse_host(host, &addr) != 0) {
            return 0;
        }
    } else if (s->have_remote) {
        addr = s->remote;
    } else {
        return 0;
    }
    p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
    if (p == NULL) {
        return 0;
    }
    memcpy(p->payload, ptr, len);
    if (udp_sendto(s->udp, p, &addr, (u16_t)port) != ERR_OK) {
        pbuf_free(p);
        return 0;
    }
    pbuf_free(p);
    return len;
}

uint32_t pm_metal_net_ip_try_recv(pm_metal_net_ip_sock_h h, void *ptr, uint32_t len)
{
    msock_t *s;
    if (h == 0 || h > METAL_NET_MAX_SOCKS || ptr == NULL || len == 0) {
        return 0;
    }
    s = &g_metal_socks[h];
    if (!s->used) {
        return (uint32_t)-1;
    }
    if (s->type == PM_METAL_NET_IP_SOCK_STREAM) {
        uint32_t n;
        if (s->rx_q == NULL) {
            return s->recv_err ? (uint32_t)-1 : 0;
        }
        n = pbuf_copy_partial(s->rx_q, ptr, len, 0);
        if (n > 0) {
            s->rx_q = pbuf_free_header(s->rx_q, (u16_t)n);
            if (s->tcp != NULL) {
                tcp_recved(s->tcp, (u16_t)n);
            }
        }
        return n;
    }
    if (s->udp_rx_n == 0) {
        return 0;
    }
    {
        metal_udp_rx_t *rx = &s->udp_rx[s->udp_rx_head];
        uint32_t n = pbuf_copy_partial(rx->p, ptr, len, 0);
        s->last_peer = rx->addr;
        s->last_peer_port = rx->port;
        s->have_last_peer = 1;
        pbuf_free(rx->p);
        s->udp_rx_head = (uint16_t)((s->udp_rx_head + 1u) % METAL_UDP_RX_MAX);
        s->udp_rx_n--;
        return n;
    }
}

uint32_t pm_metal_net_ip_try_recvfrom(pm_metal_net_ip_sock_h h, void *ptr, uint32_t len,
                                      char *peer_host, uint32_t peer_cap, uint32_t *peer_port)
{
    uint32_t n = pm_metal_net_ip_try_recv(h, ptr, len);
    msock_t *s;
    if (n == 0 || n == (uint32_t)-1 || h == 0 || h > METAL_NET_MAX_SOCKS) {
        return n;
    }
    s = &g_metal_socks[h];
    if (s->have_last_peer) {
        if (peer_host != NULL && peer_cap > 0) {
            ipaddr_ntoa_r(&s->last_peer, peer_host, (int)peer_cap);
        }
        if (peer_port != NULL) {
            *peer_port = s->last_peer_port;
        }
    }
    return n;
}

uint32_t pm_metal_net_ip_recv(pm_metal_net_ip_sock_h h, void *ptr, uint32_t len)
{
    msock_t *s;
    uint32_t n, ah;
    n = pm_metal_net_ip_try_recv(h, ptr, len);
    if (n != 0) {
        return pm_metal_async_completed_u32(n);
    }
    if (h == 0 || h > METAL_NET_MAX_SOCKS) {
        return 0;
    }
    s = &g_metal_socks[h];
    if (!s->used) {
        return 0;
    }
    ah = pm_metal_async_park();
    if (ah == 0u) {
        return 0;
    }
    s->recv_buf = ptr;
    s->recv_cap = len;
    s->recv_got = 0;
    s->recv_done = 0;
    s->wait_recv = ah;
    return ah;
}

uint32_t pm_metal_net_ip_dns_lookup(const char *host)
{
    ip_addr_t addr;
    uint32_t ah;
    err_t e;
    if (host == NULL) {
        return 0;
    }
    if (pm_metal_ip_parse_host(host, &addr) == 0) {
        ipaddr_ntoa_r(&addr, g_metal_dns_last, sizeof(g_metal_dns_last));
        return pm_metal_async_completed_u32(1u);
    }
    ah = pm_metal_async_park();
    if (ah == 0u) {
        return 0;
    }
    e = dns_gethostbyname(host, &addr, dns_found, (void *)(uintptr_t)ah);
    if (e == ERR_OK) {
        ipaddr_ntoa_r(&addr, g_metal_dns_last, sizeof(g_metal_dns_last));
        pm_metal_async_set_result_u32(ah, 1u);
        pm_metal_async_wake(ah);
        return ah;
    }
    if (e == ERR_INPROGRESS) {
        return ah;
    }
    pm_metal_async_set_result_u32(ah, 0u);
    pm_metal_async_wake(ah);
    return ah;
}

pm_metal_net_ip_sock_h pm_metal_net_ip_try_accept(pm_metal_net_ip_sock_h listen_h)
{
    msock_t *s;
    pm_metal_net_ip_sock_h out;
    if (listen_h == 0 || listen_h > METAL_NET_MAX_SOCKS) {
        return PM_METAL_NET_IP_SOCK_INVALID;
    }
    s = &g_metal_socks[listen_h];
    if (!s->used || !s->listening || s->accept_q_n == 0) {
        return PM_METAL_NET_IP_SOCK_INVALID;
    }
    out = s->accept_q[s->accept_q_r];
    s->accept_q_r = (uint8_t)((s->accept_q_r + 1u) % METAL_NET_ACCEPT_Q);
    s->accept_q_n--;
    return out;
}

int32_t pm_metal_net_ip_sock_connected(pm_metal_net_ip_sock_h h)
{
    msock_t *s;
    if (h == 0 || h > METAL_NET_MAX_SOCKS) {
        return 0;
    }
    s = &g_metal_socks[h];
    return (s->used && s->type == PM_METAL_NET_IP_SOCK_STREAM && s->conn_ok && s->tcp != NULL &&
            !s->listening)
               ? 1
               : 0;
}

int32_t pm_metal_net_ip_sock_listening(pm_metal_net_ip_sock_h h)
{
    msock_t *s;
    if (h == 0 || h > METAL_NET_MAX_SOCKS) {
        return 0;
    }
    s = &g_metal_socks[h];
    return (s->used && s->listening) ? 1 : 0;
}

int32_t pm_metal_net_ip_sock_peer_closed(pm_metal_net_ip_sock_h h)
{
    msock_t *s;
    if (h == 0 || h > METAL_NET_MAX_SOCKS) {
        return 0;
    }
    s = &g_metal_socks[h];
    return (s->used && s->recv_err) ? 1 : 0;
}

uint32_t pm_metal_net_ip_sock_rx_avail(pm_metal_net_ip_sock_h h)
{
    msock_t *s;
    if (h == 0 || h > METAL_NET_MAX_SOCKS) {
        return 0;
    }
    s = &g_metal_socks[h];
    if (!s->used) {
        return 0;
    }
    if (s->type == PM_METAL_NET_IP_SOCK_STREAM) {
        return s->rx_q != NULL ? s->rx_q->tot_len : 0u;
    }
    if (s->udp_rx_n == 0) {
        return 0;
    }
    return s->udp_rx[s->udp_rx_head].p != NULL ? s->udp_rx[s->udp_rx_head].p->tot_len : 0u;
}

int32_t pm_metal_net_ip_connect_ip4(pm_metal_net_ip_sock_h h, uint32_t dst_ip, uint16_t port)
{
    msock_t *s;
    ip_addr_t addr;
    ip4_addr_t a4;
    if (h == 0 || h > METAL_NET_MAX_SOCKS || dst_ip == 0u || port == 0u) {
        return -1;
    }
    s = &g_metal_socks[h];
    if (!s->used || s->tcp == NULL) {
        return -1;
    }
    a4.addr = lwip_htonl(dst_ip);
    ip_addr_copy_from_ip4(addr, a4);
    s->remote = addr;
    s->have_remote = 1;
    s->remote_port = port;
    if (start_tcp_connect(s, &addr, port) != 0) {
        return -1;
    }
    return 0;
}

uint32_t pm_metal_net_ip_sendto_ip4(pm_metal_net_ip_sock_h h, uint32_t dst_ip, uint16_t dst_port,
                                    const void *ptr, uint32_t len)
{
    char host[16];
    ip4_addr_t a4;
    if (dst_ip == 0u || dst_port == 0u) {
        return 0;
    }
    a4.addr = lwip_htonl(dst_ip);
    if (ip4addr_ntoa_r(&a4, host, (int)sizeof(host)) == NULL) {
        return 0;
    }
    return pm_metal_net_ip_sendto(h, ptr, len, host, dst_port);
}

int32_t pm_metal_net_ip_try_recvfrom_ip4(pm_metal_net_ip_sock_h h, uint32_t *src_ip,
                                         uint16_t *src_port, void *buf, uint32_t cap,
                                         uint32_t *len_out)
{
    uint32_t n;
    msock_t *s;
    if (buf == NULL || len_out == NULL || cap == 0u) {
        return -1;
    }
    *len_out = 0;
    n = pm_metal_net_ip_try_recv(h, buf, cap);
    if (n == 0u) {
        return 0;
    }
    if (n == (uint32_t)-1) {
        return -1;
    }
    *len_out = n;
    if (h == 0 || h > METAL_NET_MAX_SOCKS) {
        return 1;
    }
    s = &g_metal_socks[h];
    if (s->have_last_peer) {
        if (src_ip != NULL) {
            *src_ip = lwip_ntohl(ip_2_ip4(&s->last_peer)->addr);
        }
        if (src_port != NULL) {
            *src_port = s->last_peer_port;
        }
    } else {
        if (src_ip != NULL) {
            *src_ip = 0;
        }
        if (src_port != NULL) {
            *src_port = 0;
        }
    }
    return 1;
}
