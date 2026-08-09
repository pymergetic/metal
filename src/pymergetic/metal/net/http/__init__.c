#include "pymergetic/metal/net/http/__init__.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/async/handle.h"
#include "pymergetic/metal/net/dns/__init__.h"
#include "pymergetic/metal/net/ip/__init__.h"
#include "pymergetic/metal/net/ip/sock.h"
#include "pymergetic/metal/net/tls/__init__.h"

static int32_t g_ready;
static int32_t g_served;
static pm_metal_net_ip_sock_h g_listen;
static pm_metal_net_ip_sock_h g_listen_tls;
static pm_metal_net_ip_sock_h g_conn;
static pm_metal_net_tls_h g_tls;
static int32_t g_conn_tls;
static uint32_t g_hs_ah;
static int32_t g_hs_done;

static const char k_resp[] =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 9\r\n"
    "Connection: close\r\n"
    "\r\n"
    "metal ok\n";

static void conn_close(void)
{
    if (g_hs_ah != 0u) {
        pm_metal_async_coro_close(g_hs_ah);
        g_hs_ah = 0;
    }
    if (g_tls != PM_METAL_NET_TLS_INVALID) {
        pm_metal_net_tls_close(g_tls);
        g_tls = PM_METAL_NET_TLS_INVALID;
    }
    if (g_conn != PM_METAL_NET_IP_SOCK_INVALID) {
        pm_metal_net_ip_close(g_conn);
        g_conn = PM_METAL_NET_IP_SOCK_INVALID;
    }
    g_conn_tls = 0;
    g_hs_done = 0;
}

void pm_metal_net_http_shutdown(void)
{
    conn_close();
    if (g_listen != PM_METAL_NET_IP_SOCK_INVALID) {
        pm_metal_net_ip_close(g_listen);
        g_listen = PM_METAL_NET_IP_SOCK_INVALID;
    }
    if (g_listen_tls != PM_METAL_NET_IP_SOCK_INVALID) {
        pm_metal_net_ip_close(g_listen_tls);
        g_listen_tls = PM_METAL_NET_IP_SOCK_INVALID;
    }
    g_ready = 0;
    g_served = 0;
}

int32_t pm_metal_net_http_init(void)
{
    if (g_listen != PM_METAL_NET_IP_SOCK_INVALID) {
        g_ready = 1;
        return 0;
    }
    g_listen = pm_metal_net_ip_socket(PM_METAL_NET_IP_AF_INET, PM_METAL_NET_IP_SOCK_STREAM);
    if (g_listen == PM_METAL_NET_IP_SOCK_INVALID) {
        return -1;
    }
    if (pm_metal_net_ip_listen(g_listen, 80) == 0u) {
        pm_metal_net_ip_close(g_listen);
        g_listen = PM_METAL_NET_IP_SOCK_INVALID;
        return -1;
    }
    g_ready = 1;
    g_served = 0;
    g_conn = PM_METAL_NET_IP_SOCK_INVALID;
    g_tls = PM_METAL_NET_TLS_INVALID;
    return 0;
}

int32_t pm_metal_net_http_init_tls(void)
{
    if (g_listen_tls != PM_METAL_NET_IP_SOCK_INVALID) {
        g_ready = 1;
        return 0;
    }
    if (pm_metal_net_tls_init() != 0) {
        return -1;
    }
    if (pm_metal_net_tls_load_smoke_server() != 0) {
        return -1;
    }
    g_listen_tls = pm_metal_net_ip_socket(PM_METAL_NET_IP_AF_INET, PM_METAL_NET_IP_SOCK_STREAM);
    if (g_listen_tls == PM_METAL_NET_IP_SOCK_INVALID) {
        return -1;
    }
    if (pm_metal_net_ip_listen(g_listen_tls, 443) == 0u) {
        pm_metal_net_ip_close(g_listen_tls);
        g_listen_tls = PM_METAL_NET_IP_SOCK_INVALID;
        return -1;
    }
    g_ready = 1;
    return 0;
}

static int32_t serve_get_plain(void)
{
    uint8_t buf[256];
    uint32_t n;

    n = pm_metal_net_ip_try_recv(g_conn, buf, sizeof(buf) - 1u);
    if (n == 0u) {
        return 0;
    }
    if (n == (uint32_t)-1 || n < 3u) {
        conn_close();
        return 0;
    }
    buf[n] = '\0';
    if (buf[0] != 'G' || buf[1] != 'E' || buf[2] != 'T') {
        return 0;
    }
    if (pm_metal_net_ip_send(g_conn, k_resp, (uint32_t)(sizeof(k_resp) - 1u)) == 0u) {
        return -1;
    }
    conn_close();
    g_served = 1;
    return 1;
}

static int32_t serve_get_tls(void)
{
    uint8_t buf[256];
    uint32_t n;
    uint32_t wr;

    if (g_tls == PM_METAL_NET_TLS_INVALID) {
        g_tls = pm_metal_net_tls_server_open(g_conn);
        if (g_tls == PM_METAL_NET_TLS_INVALID) {
            conn_close();
            return 0;
        }
        g_hs_ah = pm_metal_net_tls_handshake(g_tls);
        g_hs_done = 0;
        if (g_hs_ah == 0u) {
            conn_close();
            return 0;
        }
    }
    if (!g_hs_done) {
        if (pm_metal_async_status(g_hs_ah) != PM_METAL_ASYNC_DONE) {
            return 0;
        }
        if (pm_metal_async_result_u32(g_hs_ah) != 1u) {
            conn_close();
            return 0;
        }
        pm_metal_async_coro_close(g_hs_ah);
        g_hs_ah = 0;
        g_hs_done = 1;
    }
    n = pm_metal_net_tls_try_read(g_tls, buf, sizeof(buf) - 1u);
    if (n == 0u) {
        return 0;
    }
    if (n == (uint32_t)-1 || n < 3u) {
        conn_close();
        return 0;
    }
    buf[n] = '\0';
    if (buf[0] != 'G' || buf[1] != 'E' || buf[2] != 'T') {
        return 0;
    }
    wr = pm_metal_net_tls_write(g_tls, k_resp, (uint32_t)(sizeof(k_resp) - 1u));
    if (wr == 0u) {
        return -1;
    }
    conn_close();
    g_served = 1;
    return 1;
}

int32_t pm_metal_net_http_poll(void)
{
    if (!g_ready || g_served) {
        return 0;
    }
    if (g_conn == PM_METAL_NET_IP_SOCK_INVALID) {
        if (g_listen != PM_METAL_NET_IP_SOCK_INVALID) {
            g_conn = pm_metal_net_ip_try_accept(g_listen);
            if (g_conn != PM_METAL_NET_IP_SOCK_INVALID) {
                g_conn_tls = 0;
            }
        }
        if (g_conn == PM_METAL_NET_IP_SOCK_INVALID &&
            g_listen_tls != PM_METAL_NET_IP_SOCK_INVALID) {
            g_conn = pm_metal_net_ip_try_accept(g_listen_tls);
            if (g_conn != PM_METAL_NET_IP_SOCK_INVALID) {
                g_conn_tls = 1;
            }
        }
        if (g_conn == PM_METAL_NET_IP_SOCK_INVALID) {
            return 0;
        }
    }
    if (g_conn_tls) {
        return serve_get_tls();
    }
    return serve_get_plain();
}

int32_t pm_metal_net_http_served(void)
{
    return g_served;
}

static int str_has_http(const uint8_t *buf, uint32_t n)
{
    uint32_t i;

    if (buf == NULL || n < 5u) {
        return 0;
    }
    for (i = 0; i + 5u <= n; i++) {
        if (buf[i] == 'H' && buf[i + 1u] == 'T' && buf[i + 2u] == 'T' && buf[i + 3u] == 'P' &&
            buf[i + 4u] == '/') {
            return 1;
        }
    }
    return 0;
}

int32_t pm_metal_net_http_client_get(const char *host, uint16_t port, const char *path,
                                     uint8_t *buf, uint32_t cap, uint32_t *len_out)
{
    char req[256];
    uint32_t addr = 0;
    uint32_t got = 0;
    uint32_t chunk;
    int i;
    int32_t out = -1;
    size_t rlen;
    size_t o;
    pm_metal_net_ip_sock_h h = PM_METAL_NET_IP_SOCK_INVALID;

    if (host == NULL || path == NULL || buf == NULL || len_out == NULL || cap < 16u || port == 0u) {
        return -1;
    }
    *len_out = 0;
    if (pm_metal_net_dns_resolve(host, &addr) != 0 || addr == 0u) {
        return -1;
    }

    h = pm_metal_net_ip_socket(PM_METAL_NET_IP_AF_INET, PM_METAL_NET_IP_SOCK_STREAM);
    if (h == PM_METAL_NET_IP_SOCK_INVALID) {
        return -1;
    }
    if (pm_metal_net_ip_connect_ip4(h, addr, port) != 0) {
        out = -1;
        goto done;
    }
    for (i = 0; i < 20000 && !pm_metal_net_ip_sock_connected(h); i++) {
        pm_metal_net_ip_poll();
    }
    if (!pm_metal_net_ip_sock_connected(h)) {
        out = -3;
        goto done;
    }

    o = 0;
    memcpy(req + o, "GET ", 4);
    o += 4;
    for (rlen = 0; path[rlen] != '\0' && o + 1u < sizeof(req); rlen++) {
        req[o++] = path[rlen];
    }
    memcpy(req + o, " HTTP/1.0\r\nHost: ", 16);
    o += 16;
    for (rlen = 0; host[rlen] != '\0' && o + 1u < sizeof(req); rlen++) {
        req[o++] = host[rlen];
    }
    memcpy(req + o, "\r\nConnection: close\r\n\r\n", 24);
    o += 24;
    if (o >= sizeof(req)) {
        goto done;
    }
    if (pm_metal_net_ip_send(h, req, (uint32_t)o) == 0u) {
        goto done;
    }

    for (i = 0; i < 20000; i++) {
        pm_metal_net_ip_poll();
        chunk = pm_metal_net_ip_try_recv(h, buf + got, cap - got);
        if (chunk == (uint32_t)-1) {
            break;
        }
        if (chunk > 0u) {
            got += chunk;
            if (str_has_http(buf, got)) {
                *len_out = got;
                out = 0;
                goto done;
            }
            if (got >= cap) {
                break;
            }
        }
    }
    if (str_has_http(buf, got)) {
        *len_out = got;
        out = 0;
    } else {
        out = -2;
    }

done:
    if (h != PM_METAL_NET_IP_SOCK_INVALID) {
        pm_metal_net_ip_close(h);
    }
    return out;
}
