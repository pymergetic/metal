/*
 * Async HTTP(S) GET — state machine advanced from net pump.
 */
#include "pymergetic/metal/net/http/__init__.h"

#include <string.h>

#include "pymergetic/metal/async/await.h"
#include "pymergetic/metal/async/handle.h"
#include "pymergetic/metal/net/dns/__init__.h"
#include "pymergetic/metal/net/io_budget.h"
#include "pymergetic/metal/net/ip/__init__.h"
#include "pymergetic/metal/net/ip/sock.h"
#include "pymergetic/metal/net/tls/__init__.h"

enum {
    ST_IDLE = 0,
    ST_DNS,
    ST_CONNECT,
    ST_TLS_HS,
    ST_SEND,
    ST_RECV,
    ST_DONE
};

static int32_t g_verify_none;
static uint32_t g_ah;
static uint32_t g_dns_ah;
static int g_st;
static int g_https;
static char g_host[128];
static char g_path[160];
static uint16_t g_port;
static uint32_t g_addr;
static pm_metal_net_ip_sock_h g_sock;
static pm_metal_net_tls_h g_tls;
static uint32_t g_hs_ah;
static uint8_t g_body[PM_METAL_IO_WIRE_MAX];
static uint32_t g_body_len;
static uint32_t g_status;
static uint32_t g_sent;

void pm_metal_net_http_set_tls_verify_none(int32_t on)
{
    g_verify_none = on ? 1 : 0;
}

uint32_t pm_metal_net_http_status(void)
{
    return g_status;
}

uint32_t pm_metal_net_http_body_len(void)
{
    return g_body_len;
}

const uint8_t *pm_metal_net_http_body(void)
{
    return g_body;
}

static void client_fail(void)
{
    if (g_dns_ah != 0u) {
        pm_metal_async_coro_close(g_dns_ah);
        g_dns_ah = 0;
    }
    if (g_tls != PM_METAL_NET_TLS_INVALID) {
        pm_metal_net_tls_close(g_tls);
        g_tls = PM_METAL_NET_TLS_INVALID;
    }
    if (g_sock != PM_METAL_NET_IP_SOCK_INVALID) {
        pm_metal_net_ip_close(g_sock);
        g_sock = PM_METAL_NET_IP_SOCK_INVALID;
    }
    g_st = ST_DONE;
    if (g_ah != 0u) {
        pm_metal_async_set_result_u32(g_ah, 0u);
        pm_metal_async_wake(g_ah);
        g_ah = 0;
    }
}

static void client_ok(void)
{
    if (g_tls != PM_METAL_NET_TLS_INVALID) {
        pm_metal_net_tls_close(g_tls);
        g_tls = PM_METAL_NET_TLS_INVALID;
    }
    if (g_sock != PM_METAL_NET_IP_SOCK_INVALID) {
        pm_metal_net_ip_close(g_sock);
        g_sock = PM_METAL_NET_IP_SOCK_INVALID;
    }
    g_st = ST_DONE;
    if (g_ah != 0u) {
        pm_metal_async_set_result_u32(g_ah, 1u);
        pm_metal_async_wake(g_ah);
        g_ah = 0;
    }
}

static int parse_url(const char *url)
{
    const char *p;
    size_t i;

    g_https = 0;
    g_port = 80;
    g_host[0] = 0;
    g_path[0] = '/';
    g_path[1] = 0;
    if (url == NULL) {
        return -1;
    }
    if (memcmp(url, "https://", 8) == 0) {
        g_https = 1;
        g_port = 443;
        p = url + 8;
    } else if (memcmp(url, "http://", 7) == 0) {
        p = url + 7;
    } else {
        return -1;
    }
    i = 0;
    while (*p && *p != '/' && *p != ':' && i + 1u < sizeof(g_host)) {
        g_host[i++] = *p++;
    }
    g_host[i] = 0;
    if (i == 0) {
        return -1;
    }
    if (*p == ':') {
        uint32_t port = 0;
        p++;
        while (*p >= '0' && *p <= '9') {
            port = port * 10u + (uint32_t)(*p - '0');
            p++;
        }
        if (port == 0u || port > 65535u) {
            return -1;
        }
        g_port = (uint16_t)port;
    }
    if (*p == '/') {
        i = 0;
        while (*p && i + 1u < sizeof(g_path)) {
            g_path[i++] = *p++;
        }
        g_path[i] = 0;
    }
    return 0;
}

static uint32_t parse_status_line(const uint8_t *buf, uint32_t n)
{
    uint32_t i = 0;
    uint32_t code = 0;
    if (n < 12u) {
        return 0;
    }
    if (!(buf[0] == 'H' && buf[1] == 'T' && buf[2] == 'T' && buf[3] == 'P')) {
        return 0;
    }
    while (i < n && buf[i] != ' ') {
        i++;
    }
    if (i >= n) {
        return 0;
    }
    i++;
    while (i < n && buf[i] >= '0' && buf[i] <= '9') {
        code = code * 10u + (uint32_t)(buf[i] - '0');
        i++;
    }
    return code;
}

void pm_metal_net_http_client_poll(void)
{
    char req[256];
    size_t o;
    size_t rlen;
    uint32_t n;
    uint32_t flags;

    if (g_st == ST_IDLE || g_st == ST_DONE) {
        return;
    }
    if (g_st == ST_DNS) {
        if (g_dns_ah == 0u) {
            g_dns_ah = pm_metal_net_dns_lookup(g_host);
            if (g_dns_ah == 0u) {
                client_fail();
                return;
            }
        }
        if (pm_metal_async_status(g_dns_ah) != PM_METAL_ASYNC_DONE) {
            return;
        }
        if (pm_metal_async_result_u32(g_dns_ah) != 1u) {
            pm_metal_async_coro_close(g_dns_ah);
            g_dns_ah = 0;
            client_fail();
            return;
        }
        g_addr = pm_metal_net_dns_last_addr();
        pm_metal_async_coro_close(g_dns_ah);
        g_dns_ah = 0;
        if (g_addr == 0u) {
            client_fail();
            return;
        }
        g_sock = pm_metal_net_ip_socket(PM_METAL_NET_IP_AF_INET, PM_METAL_NET_IP_SOCK_STREAM);
        if (g_sock == PM_METAL_NET_IP_SOCK_INVALID) {
            client_fail();
            return;
        }
        if (pm_metal_net_ip_connect_ip4(g_sock, g_addr, g_port) != 0) {
            client_fail();
            return;
        }
        g_st = ST_CONNECT;
    }
    if (g_st == ST_CONNECT) {
        if (!pm_metal_net_ip_sock_connected(g_sock)) {
            if (pm_metal_net_ip_sock_peer_closed(g_sock)) {
                client_fail();
            }
            return;
        }
        if (g_https) {
            flags = g_verify_none ? PM_METAL_NET_TLS_VERIFY_NONE : PM_METAL_NET_TLS_VERIFY_REQUIRED;
            g_tls = pm_metal_net_tls_client_open(g_sock, g_host, flags);
            if (g_tls == PM_METAL_NET_TLS_INVALID) {
                client_fail();
                return;
            }
            g_hs_ah = pm_metal_net_tls_handshake(g_tls);
            g_st = ST_TLS_HS;
        } else {
            g_st = ST_SEND;
            g_sent = 0;
        }
    }
    if (g_st == ST_TLS_HS) {
        if (g_hs_ah == 0u) {
            client_fail();
            return;
        }
        if (pm_metal_async_status(g_hs_ah) != PM_METAL_ASYNC_DONE) {
            return;
        }
        if (pm_metal_async_result_u32(g_hs_ah) != 1u) {
            pm_metal_async_coro_close(g_hs_ah);
            g_hs_ah = 0;
            client_fail();
            return;
        }
        pm_metal_async_coro_close(g_hs_ah);
        g_hs_ah = 0;
        g_st = ST_SEND;
        g_sent = 0;
    }
    if (g_st == ST_SEND) {
        o = 0;
        memcpy(req + o, "GET ", 4);
        o += 4;
        for (rlen = 0; g_path[rlen] != '\0' && o + 1u < sizeof(req); rlen++) {
            req[o++] = g_path[rlen];
        }
        memcpy(req + o, " HTTP/1.0\r\nHost: ", 16);
        o += 16;
        for (rlen = 0; g_host[rlen] != '\0' && o + 1u < sizeof(req); rlen++) {
            req[o++] = g_host[rlen];
        }
        memcpy(req + o, "\r\nConnection: close\r\n\r\n", 24);
        o += 24;
        if (g_https) {
            n = pm_metal_net_tls_write(g_tls, req, (uint32_t)o);
        } else {
            n = pm_metal_net_ip_send(g_sock, req, (uint32_t)o);
        }
        if (n == 0u) {
            return;
        }
        g_body_len = 0;
        g_status = 0;
        g_st = ST_RECV;
    }
    if (g_st == ST_RECV) {
        if (g_https) {
            n = pm_metal_net_tls_try_read(g_tls, g_body + g_body_len,
                                         (uint32_t)sizeof(g_body) - g_body_len);
        } else {
            n = pm_metal_net_ip_try_recv(g_sock, g_body + g_body_len,
                                        (uint32_t)sizeof(g_body) - g_body_len);
        }
        if (n == 0u) {
            return;
        }
        if (n == (uint32_t)-1) {
            if (g_body_len > 0u) {
                g_status = parse_status_line(g_body, g_body_len);
                client_ok();
            } else {
                client_fail();
            }
            return;
        }
        g_body_len += n;
        if (g_status == 0u) {
            g_status = parse_status_line(g_body, g_body_len);
        }
        if (g_body_len >= sizeof(g_body) || pm_metal_net_ip_sock_peer_closed(g_sock)) {
            if (g_status != 0u) {
                client_ok();
            } else {
                client_fail();
            }
        }
    }
}

uint32_t pm_metal_net_http_get(const char *url)
{
    if (g_st != ST_IDLE && g_st != ST_DONE) {
        return pm_metal_async_completed_u32(0u);
    }
    g_body_len = 0;
    g_status = 0;
    g_sock = PM_METAL_NET_IP_SOCK_INVALID;
    g_tls = PM_METAL_NET_TLS_INVALID;
    g_hs_ah = 0;
    g_dns_ah = 0;
    g_addr = 0;
    if (parse_url(url) != 0) {
        return pm_metal_async_completed_u32(0u);
    }
    g_ah = pm_metal_async_park();
    if (g_ah == 0u) {
        return pm_metal_async_completed_u32(0u);
    }
    g_st = ST_DNS;
    return g_ah;
}
