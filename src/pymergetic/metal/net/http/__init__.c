#include "pymergetic/metal/net/http/__init__.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/async/board_time.h"
#include "pymergetic/metal/async/handle.h"
#include "pymergetic/metal/async/runner.h"
#include "pymergetic/metal/net/ip/__init__.h"
#include "pymergetic/metal/net/ip/sock.h"
#include "pymergetic/metal/net/pump/__init__.h"
#include "pymergetic/metal/net/tls/__init__.h"

#include <pymergetic/metal/reg/mod.h>

/* RegMod declare (C SoT) — loaded via pm_metal_net_http_reg_load. */
static pm_metal_reg_export_t net_http_exports[] = {
    PM_METAL_REG_EXPORT(init),
    PM_METAL_REG_EXPORT(init_tls),
    PM_METAL_REG_EXPORT(shutdown),
    PM_METAL_REG_EXPORT(poll),
    PM_METAL_REG_EXPORT(served),
    PM_METAL_REG_EXPORT(get),
    PM_METAL_REG_EXPORT(status),
    PM_METAL_REG_EXPORT(body_len),
};
PM_METAL_REG_REF(net_http, init, 0);
PM_METAL_REG_REF(net_http, init_tls, 1);
PM_METAL_REG_REF(net_http, shutdown, 2);
PM_METAL_REG_REF(net_http, poll, 3);
PM_METAL_REG_REF(net_http, served, 4);
PM_METAL_REG_REF(net_http, get, 5);
PM_METAL_REG_REF(net_http, status, 6);
PM_METAL_REG_REF(net_http, body_len, 7);
PM_METAL_REG_MOD(net_http, "pymergetic.metal.net.http")

static int32_t net_http_register_symbols(void *ctx)
{
    (void)ctx;
    pm_metal_reg_export_publish(net_http_init, (void *)pm_metal_net_http_init);
    pm_metal_reg_export_publish(net_http_init_tls, (void *)pm_metal_net_http_init_tls);
    pm_metal_reg_export_publish(net_http_shutdown, (void *)pm_metal_net_http_shutdown);
    pm_metal_reg_export_publish(net_http_poll, (void *)pm_metal_net_http_poll);
    pm_metal_reg_export_publish(net_http_served, (void *)pm_metal_net_http_served);
    pm_metal_reg_export_publish(net_http_get, (void *)pm_metal_net_http_get);
    pm_metal_reg_export_publish(net_http_status, (void *)pm_metal_net_http_status);
    pm_metal_reg_export_publish(net_http_body_len, (void *)pm_metal_net_http_body_len);
    return 0;
}

#ifndef PM_METAL_HTTP_CLIENT_WAIT_ITERS
#define PM_METAL_HTTP_CLIENT_WAIT_ITERS 40000u
#endif

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
    char url[320];
    size_t o = 0;
    size_t i;
    uint32_t h;
    uint32_t n;
    uint32_t copy;
    const uint8_t *body;

    if (host == NULL || path == NULL || buf == NULL || len_out == NULL || cap < 16u || port == 0u) {
        return -1;
    }
    *len_out = 0;
    /* Build http://host[:port]/path for the async client. */
    memcpy(url + o, "http://", 7);
    o += 7;
    for (i = 0; host[i] != '\0' && o + 1u < sizeof(url); i++) {
        url[o++] = host[i];
    }
    if (port != 80u) {
        char pbuf[8];
        unsigned v = port;
        unsigned pn = 0;
        url[o++] = ':';
        if (o >= sizeof(url)) {
            return -1;
        }
        do {
            pbuf[pn++] = (char)('0' + (v % 10u));
            v /= 10u;
        } while (v != 0u && pn < sizeof(pbuf));
        while (pn > 0u && o + 1u < sizeof(url)) {
            url[o++] = pbuf[--pn];
        }
    }
    if (path[0] != '/') {
        if (o + 1u >= sizeof(url)) {
            return -1;
        }
        url[o++] = '/';
    }
    for (i = 0; path[i] != '\0' && o + 1u < sizeof(url); i++) {
        url[o++] = path[i];
    }
    url[o] = '\0';

    h = pm_metal_net_http_get(url);
    if (h == 0u) {
        return -1;
    }
    for (n = 0; n < PM_METAL_HTTP_CLIENT_WAIT_ITERS; n++) {
        pm_metal_net_pump_once();
        pm_metal_board_time_advance_us(1000);
        (void)pm_metal_async_run_poll();
        if (pm_metal_async_status(h) == PM_METAL_ASYNC_DONE) {
            break;
        }
    }
    if (pm_metal_async_status(h) != PM_METAL_ASYNC_DONE) {
        pm_metal_async_coro_close(h);
        return -2;
    }
    if (pm_metal_async_result_u32(h) != 1u) {
        pm_metal_async_coro_close(h);
        return -1;
    }
    body = pm_metal_net_http_body();
    copy = pm_metal_net_http_body_len();
    if (body == NULL || copy == 0u || !str_has_http(body, copy)) {
        pm_metal_async_coro_close(h);
        return -1;
    }
    if (copy > cap) {
        copy = cap;
    }
    memcpy(buf, body, copy);
    *len_out = copy;
    pm_metal_async_coro_close(h);
    return 0;
}
