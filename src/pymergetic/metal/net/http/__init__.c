#include "pymergetic/metal/net/http/__init__.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/net/dns/__init__.h"
#include "pymergetic/metal/net/ip/__init__.h"
#include "pymergetic/metal/net/ip/tcp.h"

static int32_t g_ready;
static int32_t g_served;

static const char k_resp[] =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 9\r\n"
    "Connection: close\r\n"
    "\r\n"
    "metal ok\n";

int32_t pm_metal_net_http_init(void)
{
    g_ready = 1;
    g_served = 0;
    return 0;
}

int32_t pm_metal_net_http_poll(void)
{
    uint8_t buf[256];
    uint32_t n;
    int32_t rc;

    if (!g_ready || !pm_metal_net_ip_tcp_established() || g_served) {
        return 0;
    }
    rc = pm_metal_net_ip_tcp_recv(buf, sizeof(buf) - 1u, &n);
    if (rc != 1 || n < 3u) {
        return 0;
    }
    buf[n] = '\0';
    if (buf[0] != 'G' || buf[1] != 'E' || buf[2] != 'T') {
        return 0;
    }
    if (pm_metal_net_ip_tcp_send(k_resp, (uint32_t)(sizeof(k_resp) - 1u)) != 0) {
        return -1;
    }
    g_served = 1;
    return 1;
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
    int32_t rc;
    int32_t out = -1;
    size_t rlen;
    size_t o;

    if (host == NULL || path == NULL || buf == NULL || len_out == NULL || cap < 16u || port == 0u) {
        return -1;
    }
    *len_out = 0;
    if (pm_metal_net_dns_resolve(host, &addr) != 0 || addr == 0u) {
        return -1;
    }

    for (i = 0; i < 32; i++) {
        rc = pm_metal_net_ip_tcp_connect(addr, port);
        if (rc == 0) {
            break;
        }
        if (rc != -2) {
            goto done;
        }
        pm_metal_net_ip_poll();
    }
    if (rc != 0) {
        goto done;
    }

    for (i = 0; i < 20000 && !pm_metal_net_ip_tcp_established(); i++) {
        pm_metal_net_ip_poll();
    }
    if (!pm_metal_net_ip_tcp_established()) {
        out = -3; /* connect handshake timeout */
        goto done;
    }

    /* GET path HTTP/1.0\r\nHost: host\r\nConnection: close\r\n\r\n */
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

    for (i = 0; i < 32; i++) {
        rc = pm_metal_net_ip_tcp_send(req, (uint32_t)o);
        if (rc == 0) {
            break;
        }
        if (rc != -2) {
            goto done;
        }
        pm_metal_net_ip_poll();
    }
    if (rc != 0) {
        goto done;
    }

    for (i = 0; i < 20000; i++) {
        pm_metal_net_ip_poll();
        chunk = 0;
        rc = pm_metal_net_ip_tcp_recv(buf + got, cap - got, &chunk);
        if (rc == 1 && chunk > 0u) {
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
    pm_metal_net_ip_tcp_abort();
    return out;
}
