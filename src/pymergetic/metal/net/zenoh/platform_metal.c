/*
 * pymergetic.metal.net.zenoh — zenoh-pico network platform on top of the
 * Metal net.ip card. Maps every _z_*_tcp/_udp socket primitive to the
 * pm_metal_net_ip_* face. Same file on every seat (unix, emcc, firmware):
 * net.ip is the only underlying stack.
 *
 * Cooperative open: a connect/read that has no data yet calls
 * pm_metal_net_zenoh_yield() (which pumps net.ip and spins the peer sessions'
 * executors) in bounded rounds instead of busy-waiting to block the whole card.
 * This is what lets a listener and a connector on one _lo_ instance finish the
 * zenoh handshake in one thread. All spins carry a monotonic deadline so no
 * call can run unbounded.
 */
#include "zenoh-pico/system/platform.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "zenoh-pico/system/link/tcp.h"
#include "zenoh-pico/system/link/udp.h"
#include "zenoh-pico/utils/logging.h"
#include "zenoh-pico/utils/result.h"

#include "pymergetic/metal/net/zenoh/__priv__.h"

#include <stdio.h>

/* Bounded cooperative-spin budget, in microseconds (matches the net.ip ping4
 * pattern). A monotonic-counter seat ticks quickly; the spin count caps it. */
#define PM_METAL_NET_ZENOH_CONNECT_WAIT_US 2000000u   /* 2 s for TCP ESTAB */
#define PM_METAL_NET_ZENOH_YIELD_SPINS 128u           /* hard cap per wait round */

/*------------------ IPv4 helpers ------------------*/

static uint16_t host_port(const char *s_port) {
    unsigned long v = 0;
    const char *p = s_port;
    while (p != NULL && *p >= '0' && *p <= '9') {
        v = v * 10u + (unsigned long)(*p - '0');
        p++;
    }
    return (uint16_t)(v & 0xffffu);
}

int pm_metal_net_zenoh_parse_ipv4(const char *host, uint32_t *addr_be_out) {
    unsigned int oct[4];
    int chars = 0;
    int n = 0;
    const char *p = host;
    int i;
    if (host == NULL || addr_be_out == NULL) {
        return -1;
    }
    for (i = 0; i < 4; i++) {
        unsigned int v = 0;
        if (*p < '0' || *p > '9') {
            return -1;
        }
        while (*p >= '0' && *p <= '9') {
            v = v * 10u + (unsigned int)(*p - '0');
            p++;
            n++;
            chars++;
            if (v > 255u) {
                return -1;
            }
        }
        oct[i] = v;
        if (i < 3) {
            if (*p != '.') {
                return -1;
            }
            p++;
            chars++;
        }
    }
    if (chars == 0 || *p != '\0' || n == 0) {
        return -1;
    }
    *addr_be_out = ((uint32_t)oct[0] << 24) | ((uint32_t)oct[1] << 16) | ((uint32_t)oct[2] << 8) | (uint32_t)oct[3];
    return 0;
}

static z_result_t make_endpoint(_z_sys_net_endpoint_t *ep, const char *s_address, const char *s_port) {
    if (ep == NULL) {
        _Z_ERROR_RETURN(_Z_ERR_INVALID);
    }
    ep->_addr_be = 0;
    ep->_port_host = 0;
    if (pm_metal_net_zenoh_parse_ipv4(s_address, &ep->_addr_be) != 0) {
        _Z_ERROR_RETURN(_Z_ERR_GENERIC);
    }
    ep->_port_host = host_port(s_port);
    return _Z_RES_OK;
}

/*------------------ socket management ------------------*/

static int32_t net_socket(int stream) {
    return pm_metal_net_ip_socket(stream ? PM_METAL_NET_IP_SOCK_STREAM : PM_METAL_NET_IP_SOCK_DGRAM);
}

/*------------------ socket control (common) ------------------*/

z_result_t _z_socket_set_blocking(const _z_sys_net_socket_t *sock, bool blocking) {
    /* net.ip sockets are always non-blocking (poll-driven). Accept and ignore
     * so the link layer's set_nonblock call after open is a no-op. */
    _ZP_UNUSED(sock);
    _ZP_UNUSED(blocking);
    return _Z_RES_OK;
}

void _z_socket_close(_z_sys_net_socket_t *sock) {
    if (sock != NULL && sock->_fd >= 0) {
        (void)pm_metal_net_ip_close(sock->_fd);
        sock->_fd = -1;
    }
}

z_result_t _z_socket_accept(const _z_sys_net_socket_t *sock_in, _z_sys_net_socket_t *sock_out) {
    int32_t rc;
    if (sock_in == NULL || sock_out == NULL || sock_in->_fd < 0) {
        _Z_ERROR_RETURN(_Z_ERR_INVALID);
    }
    rc = pm_metal_net_ip_accept(sock_in->_fd);
    if (rc < 0) {
        return _Z_ERR_GENERIC;
    }
    sock_out->_fd = rc;
    return _Z_RES_OK;
}

z_result_t _z_socket_get_endpoints(const _z_sys_net_socket_t *sock, char *local, size_t local_len, char *remote,
                                   size_t remote_len) {
    int32_t fd;
    uint32_t raddr = 0;
    uint16_t rport = 0;
    if (sock == NULL || local == NULL || remote == NULL || local_len == 0 || remote_len == 0) {
        _Z_ERROR_RETURN(_Z_ERR_INVALID);
    }
    fd = sock->_fd;
    if (fd < 0) {
        _Z_ERROR_RETURN(_Z_ERR_INVALID);
    }
    (void)pm_metal_net_ip_recvfrom(fd, NULL, 0, &raddr, &rport); /* not used; keep linker happy on seats without it */
    (void)raddr;
    (void)rport;
    /* net.ip does not expose getsockname/getpeername; report the raw socket on
     * both sides so the peer->link_src/dst fields used by the transport stay
     * valid enough for the connectivity-off build (which never reads them). */
    (void)snprintf(local, local_len, "%d", fd);
    (void)snprintf(remote, remote_len, "%d", fd);
    return _Z_RES_OK;
}

    z_result_t _z_socket_wait_event(void *v_peers, _z_mutex_rec_t *mutex) {
        /* Single-threaded build: readiness is polled by the card, not awaited here. */
        _ZP_UNUSED(v_peers);
        _ZP_UNUSED(mutex);
        return _Z_RES_OK;
    }

/*------------------ TCP ------------------*/

z_result_t _z_create_endpoint_tcp(_z_sys_net_endpoint_t *ep, const char *s_address, const char *s_port) {
    return make_endpoint(ep, s_address, s_port);
}

void _z_free_endpoint_tcp(_z_sys_net_endpoint_t *ep) {
    if (ep != NULL) {
        ep->_addr_be = 0;
        ep->_port_host = 0;
    }
}

z_result_t _z_open_tcp(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t rep, uint32_t tout) {
    int32_t fd;
    int32_t establ;
    uint64_t deadline;
    uint32_t spins = 0;
    if (sock == NULL) {
        _Z_ERROR_RETURN(_Z_ERR_INVALID);
    }
    fd = net_socket(1);
    if (fd < 0) {
        _Z_ERROR_RETURN(_Z_ERR_GENERIC);
    }
    sock->_fd = fd;
    /* pm_metal_net_ip_connect: 1 = TCP ESTAB reached synchronously (loopback
     * listener already up — the SYN/SYN-ACK/ACK completes inside the connect
     * call), 0 = SYN in flight (park until ESTAB), -1 = hard error. Treat the
     * success 1 as already-connected; only a negative result is a failure. The
     * previous `!= 0` misread the loopback fast-path success as an error, so
     * the caller's z_open aborted and retried in an unbounded connect churn
     * that exhausted the shared net.ip socket table. */
    int32_t crc = pm_metal_net_ip_connect(fd, rep._addr_be, rep._port_host);
    if (crc < 0) {
        (void)pm_metal_net_ip_close(fd);
        sock->_fd = -1;
        _Z_ERROR_RETURN(_Z_ERR_GENERIC);
    }
    if (crc == 1) {
        return _Z_RES_OK; /* ESTAB reached synchronously */
    }
    /* crc == 0: SYN in flight. Wait for the SYN/ACK handshake with a bounded
     * cooperative spin. */
    deadline = pm_metal_async_mono_us() + (uint64_t)(tout ? tout : PM_METAL_NET_ZENOH_CONNECT_WAIT_US);
    while (spins < PM_METAL_NET_ZENOH_YIELD_SPINS) {
        (void)pm_metal_net_ip_pump();
        establ = pm_metal_net_ip_established(fd);
        if (establ == 1) {
            return _Z_RES_OK;
        }
        if (establ < 0) {
            (void)pm_metal_net_ip_close(fd);
            sock->_fd = -1;
            _Z_ERROR_RETURN(_Z_ERR_GENERIC);
        }
        if (pm_metal_async_mono_us() >= deadline) {
            break;
        }
        pm_metal_net_zenoh_yield();
        spins++;
    }
    (void)pm_metal_net_ip_close(fd);
    sock->_fd = -1;
    _Z_ERROR_RETURN(_Z_ERR_GENERIC);
}

z_result_t _z_listen_tcp(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t lep) {
    int32_t fd;
    if (sock == NULL) {
        _Z_ERROR_RETURN(_Z_ERR_INVALID);
    }
    fd = net_socket(1);
    if (fd < 0) {
        _Z_ERROR_RETURN(_Z_ERR_GENERIC);
    }
    if (pm_metal_net_ip_bind(fd, lep._addr_be, lep._port_host) != 0 ||
        pm_metal_net_ip_listen(fd, 8) != 0) {
        (void)pm_metal_net_ip_close(fd);
        _Z_ERROR_RETURN(_Z_ERR_GENERIC);
    }
    sock->_fd = fd;
    return _Z_RES_OK;
}

void _z_close_tcp(_z_sys_net_socket_t *sock) {
    _z_socket_close(sock);
}

size_t _z_read_tcp(const _z_sys_net_socket_t sock, uint8_t *ptr, size_t len) {
    int32_t n;
    if (sock._fd < 0) {
        return SIZE_MAX;
    }
    n = pm_metal_net_ip_recv(sock._fd, ptr, (uint32_t)len);
    if (n == -1 || n == -2) {
        return SIZE_MAX;
    }
    if (n > 0) {
        return (size_t)n;
    }
    if (n == 0) {
        /* Would-block (no data in the socket rx buffer yet). This is the
         * steady-state streaming read: it is driven by the card's poll() step,
         * so it must PARK (report 0, non-fatal) and let the executor re-poll
         * this socket on a later zp_spin_once. The synchronous handshake read
         * (connector z_open AND the listener accept task) goes through
         * _z_read_exact_tcp, which performs its own retry (SIZE_MAX) so a
         * would-block inside a handshake never surfaces as a fatal RX here. */
        pm_metal_net_zenoh_yield();
        return (size_t)0;
    }
    return (size_t)n;
}

size_t _z_read_exact_tcp(const _z_sys_net_socket_t sock, uint8_t *ptr, size_t len) {
    /* Exclusive to the synchronous handshake reads (_z_link_recv_t_msg via
     * _z_link_recv_exact_zbuf): the connector's z_open read AND the listener's
     * accept-task read. Both must RETRY on a would-block up to the handshake's
     * own connect/accept deadline, so a would-block here reports SIZE_MAX
     * (Z_ETIMEDOUT) — never a short count (which _z_link_recv_t_msg would read
     * as a fatal _Z_ERR_TRANSPORT_RX_FAILED) and never a parked 0. The
     * cooperative yield immediately before retrying gives the peer's transport
     * task CPU on this one thread to produce the awaited reply bytes. This is
     * what lets a listener + connector on one _lo_ net.ip finish the zenoh
     * handshake within the same bounded poll loop. */
    size_t n = 0;
    uint8_t *pos = ptr;
    do {
        int32_t got;
        got = pm_metal_net_ip_recv(sock._fd, pos, (uint32_t)(len - n));
        if (got < 0) {
            return SIZE_MAX;
        }
        if (got == 0) {
            pm_metal_net_zenoh_yield();
            return SIZE_MAX;
        }
        n += (size_t)got;
        pos += (size_t)got;
    } while (n != len);
    return n;
}

size_t _z_send_tcp(const _z_sys_net_socket_t sock, const uint8_t *ptr, size_t len) {
    int32_t n;
    if (sock._fd < 0) {
        return SIZE_MAX;
    }
    n = pm_metal_net_ip_send(sock._fd, ptr, (uint32_t)len);
    if (n < 0) {
        return SIZE_MAX;
    }
    return (size_t)n;
}

/*------------------ UDP unicast ------------------*/

z_result_t _z_create_endpoint_udp(_z_sys_net_endpoint_t *ep, const char *s_address, const char *s_port) {
    return make_endpoint(ep, s_address, s_port);
}

void _z_free_endpoint_udp(_z_sys_net_endpoint_t *ep) {
    if (ep != NULL) {
        ep->_addr_be = 0;
        ep->_port_host = 0;
    }
}

z_result_t _z_open_udp_unicast(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t rep, uint32_t tout) {
    int32_t fd;
    _ZP_UNUSED(tout);
    _ZP_UNUSED(rep); /* UDP client: wildcard local source; sendto picks the path */
    if (sock == NULL) {
        _Z_ERROR_RETURN(_Z_ERR_INVALID);
    }
    fd = net_socket(0);
    if (fd < 0) {
        _Z_ERROR_RETURN(_Z_ERR_GENERIC);
    }
    /* An outbound UDP client: wildcard local source; sendto picks the path. */
    if (pm_metal_net_ip_bind(fd, 0u, 0u) != 0) {
        (void)pm_metal_net_ip_close(fd);
        _Z_ERROR_RETURN(_Z_ERR_GENERIC);
    }
    sock->_fd = fd;
    return _Z_RES_OK;
}

z_result_t _z_listen_udp_unicast(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t lep, uint32_t tout) {
    int32_t fd;
    _ZP_UNUSED(tout);
    if (sock == NULL) {
        _Z_ERROR_RETURN(_Z_ERR_INVALID);
    }
    fd = net_socket(0);
    if (fd < 0) {
        _Z_ERROR_RETURN(_Z_ERR_GENERIC);
    }
    if (pm_metal_net_ip_bind(fd, lep._addr_be, lep._port_host) != 0) {
        (void)pm_metal_net_ip_close(fd);
        _Z_ERROR_RETURN(_Z_ERR_GENERIC);
    }
    sock->_fd = fd;
    return _Z_RES_OK;
}

void _z_close_udp_unicast(_z_sys_net_socket_t *sock) {
    _z_socket_close(sock);
}

size_t _z_read_udp_unicast(const _z_sys_net_socket_t sock, uint8_t *ptr, size_t len) {
    int32_t n;
    uint32_t raddr = 0;
    uint16_t rport = 0;
    if (sock._fd < 0) {
        return SIZE_MAX;
    }
    n = pm_metal_net_ip_recvfrom(sock._fd, ptr, (uint32_t)len, &raddr, &rport);
    (void)raddr;
    (void)rport;
    if (n < 0) {
        return SIZE_MAX;
    }
    if (n == 0) {
        pm_metal_net_zenoh_yield();
        return 0;
    }
    return (size_t)n;
}

size_t _z_read_exact_udp_unicast(const _z_sys_net_socket_t sock, uint8_t *ptr, size_t len) {
    size_t n = 0;
    uint8_t *pos = ptr;
    do {
        size_t rb = _z_read_udp_unicast(sock, pos, len - n);
        if (rb == SIZE_MAX) {
            return SIZE_MAX;
        }
        if (rb == 0) {
            break;
        }
        n += rb;
        pos += rb;
    } while (n != len);
    return n;
}

size_t _z_send_udp_unicast(const _z_sys_net_socket_t sock, const uint8_t *ptr, size_t len,
                           const _z_sys_net_endpoint_t rep) {
    int32_t n;
    if (sock._fd < 0) {
        return SIZE_MAX;
    }
    n = pm_metal_net_ip_sendto(sock._fd, ptr, (uint32_t)len, rep._addr_be, rep._port_host);
    if (n < 0) {
        return SIZE_MAX;
    }
    return (size_t)n;
}

/*------------------ UDP multicast (scout/hello) ------------------*/

z_result_t _z_open_udp_multicast(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t rep, _z_sys_net_endpoint_t *lep,
                                 uint32_t tout, const char *iface) {
    _ZP_UNUSED(iface);
    _ZP_UNUSED(lep); /* outbound scout socket: same wildcard client as unicast */
    return _z_open_udp_unicast(sock, rep, tout ? tout : 100);
}

z_result_t _z_listen_udp_multicast(_z_sys_net_socket_t *sock, const _z_sys_net_endpoint_t rep, uint32_t tout,
                                   const char *iface, const char *join) {
    int32_t fd;
    _ZP_UNUSED(iface);
    _ZP_UNUSED(tout);
    if (sock == NULL) {
        _Z_ERROR_RETURN(_Z_ERR_INVALID);
    }
    fd = net_socket(0);
    if (fd < 0) {
        _Z_ERROR_RETURN(_Z_ERR_GENERIC);
    }
    if (pm_metal_net_ip_bind(fd, 0, rep._port_host) != 0) {
        (void)pm_metal_net_ip_close(fd);
        _Z_ERROR_RETURN(_Z_ERR_GENERIC);
    }
    /* Join the group so IPv4 multicast datagrams demux to this socket. The
     * net.ip IGMP/join_group task is the scout/hello prerequisite. */
    if (pm_metal_net_ip_join_group(fd, rep._addr_be) != 0) {
        (void)pm_metal_net_ip_close(fd);
        _Z_ERROR_RETURN(_Z_ERR_GENERIC);
    }
    (void)join; /* additional groups come via net.swarm.discovery */
    sock->_fd = fd;
    return _Z_RES_OK;
}

void _z_close_udp_multicast(_z_sys_net_socket_t *sockrecv, _z_sys_net_socket_t *socksend,
                            const _z_sys_net_endpoint_t rep, const _z_sys_net_endpoint_t lep) {
    (void)rep;
    if (sockrecv != NULL && sockrecv->_fd >= 0) {
        (void)pm_metal_net_ip_leave_group(sockrecv->_fd, rep._addr_be);
    }
    _z_socket_close(sockrecv);
    _z_socket_close(socksend);
    (void)lep;
}

size_t _z_read_udp_multicast(const _z_sys_net_socket_t sock, uint8_t *ptr, size_t len, const _z_sys_net_endpoint_t lep,
                             _z_slice_t *addr) {
    int32_t n;
    uint32_t raddr = 0;
    uint16_t rport = 0;
    (void)lep;
    if (sock._fd < 0) {
        return SIZE_MAX;
    }
    n = pm_metal_net_ip_recvfrom(sock._fd, ptr, (uint32_t)len, &raddr, &rport);
    if (n < 0) {
        return SIZE_MAX;
    }
    if (n == 0) {
        pm_metal_net_zenoh_yield();
        return 0;
    }
    if (addr != NULL) {
        /* Source endpoint (network-order IPv4 + host-order port) as zenoh-pico
         * expects for multicast reply routing. */
        if (addr->len >= 6u) {
            uint8_t *a = (uint8_t *)addr->start;
            a[0] = (uint8_t)(raddr >> 24);
            a[1] = (uint8_t)(raddr >> 16);
            a[2] = (uint8_t)(raddr >> 8);
            a[3] = (uint8_t)raddr;
            a[4] = (uint8_t)(rport >> 8);
            a[5] = (uint8_t)rport;
            addr->len = 6u;
        }
    }
    return (size_t)n;
}

size_t _z_read_exact_udp_multicast(const _z_sys_net_socket_t sock, uint8_t *ptr, size_t len,
                                   const _z_sys_net_endpoint_t lep, _z_slice_t *addr) {
    size_t n = 0;
    uint8_t *pos = ptr;
    do {
        size_t rb = _z_read_udp_multicast(sock, pos, len - n, lep, addr);
        if (rb == SIZE_MAX) {
            return SIZE_MAX;
        }
        if (rb == 0) {
            break;
        }
        n += rb;
        pos += rb;
    } while (n != len);
    return n;
}

size_t _z_send_udp_multicast(const _z_sys_net_socket_t sock, const uint8_t *ptr, size_t len,
                             const _z_sys_net_endpoint_t rep) {
    return _z_send_udp_unicast(sock, ptr, len, rep);
}
