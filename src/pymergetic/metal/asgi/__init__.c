/*
 * Minimal C ASGI-facing HTTP server: Inspect contract + embedded static.
 * Mount table comes from embedded httpd.json (product LIVE has no VFS).
 */
#include "pymergetic/metal/asgi/__init__.h"

#include <string.h>

#include "pymergetic/metal/inspect/__init__.h"
#include "pymergetic/metal/net/ip/tcp.h"
#include "static_embed.h"

static int g_ready;
static uint16_t g_port;

static int parse_req(const char *buf, char *method, size_t mlen, char *path,
                     size_t plen)
{
    size_t i = 0;
    size_t j = 0;

    while (buf[i] && buf[i] != ' ' && j + 1 < mlen) {
        method[j++] = buf[i++];
    }
    method[j] = 0;
    if (buf[i] != ' ') {
        return -1;
    }
    i++;
    j = 0;
    while (buf[i] && buf[i] != ' ' && buf[i] != '\r' && j + 1 < plen) {
        path[j++] = buf[i++];
    }
    path[j] = 0;
    return (method[0] && path[0]) ? 0 : -1;
}

static void u32_to_dec(unsigned v, char *out, size_t out_len)
{
    char tmp[16];
    int n = 0;
    int i;

    if (out_len == 0) {
        return;
    }
    if (v == 0) {
        out[0] = '0';
        out[1] = 0;
        return;
    }
    while (v > 0 && n < 15) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    if ((size_t)n + 1u > out_len) {
        out[0] = 0;
        return;
    }
    for (i = 0; i < n; i++) {
        out[i] = tmp[n - 1 - i];
    }
    out[n] = 0;
}

static void send_resp_bin(int status, const char *ctype, const uint8_t *body,
                          uint32_t blen)
{
    char hdr[256];
    char clen[16];
    char scode[8];
    size_t o = 0;

    u32_to_dec((unsigned)status, scode, sizeof(scode));
    u32_to_dec((unsigned)blen, clen, sizeof(clen));
#define APP(s)                                                                 \
    do {                                                                       \
        size_t l = strlen(s);                                                  \
        if (o + l < sizeof(hdr)) {                                             \
            memcpy(hdr + o, s, l);                                             \
            o += l;                                                            \
        }                                                                      \
    } while (0)
    APP("HTTP/1.0 ");
    APP(scode);
    APP(" OK\r\nContent-Type: ");
    APP(ctype ? ctype : "text/plain");
    APP("\r\nContent-Length: ");
    APP(clen);
    APP("\r\nConnection: close\r\n\r\n");
#undef APP
    if (o > 0) {
        (void)pm_metal_net_ip_tcp_send((const uint8_t *)hdr, (uint32_t)o);
    }
    if (blen > 0u && body != NULL) {
        (void)pm_metal_net_ip_tcp_send(body, blen);
    }
}

static void send_resp(int status, const char *ctype, const char *body)
{
    send_resp_bin(status, ctype, (const uint8_t *)body,
                  body ? (uint32_t)strlen(body) : 0u);
}

static int serve_httpd_static(const char *path)
{
    unsigned mi;
    const char *rel;
    const pm_metal_asgi_static_file_t *f;
    size_t ulen;

    for (mi = 0; mi < pm_metal_asgi_mount_count; mi++) {
        const char *url = pm_metal_asgi_mounts[mi].url;
        ulen = strlen(url);
        if (strcmp(path, url) == 0) {
            rel = "index.html";
        } else if (path[ulen] == '/' && strncmp(path, url, ulen) == 0) {
            rel = path + ulen + 1u;
            if (rel[0] == 0) {
                rel = "index.html";
            }
        } else {
            continue;
        }
        f = pm_metal_asgi_static_lookup(rel);
        if (f == NULL) {
            return 0;
        }
        send_resp_bin(200, f->ctype, f->data, f->len);
        return 1;
    }
    return 0;
}

int32_t pm_metal_asgi_init(uint16_t port)
{
    if (port == 0) {
        port = 80;
    }
    if (pm_metal_inspect_init() != 0) {
        return -1;
    }
    if (pm_metal_net_ip_tcp_listen(port) != 0) {
        return -1;
    }
    g_port = port;
    g_ready = 1;
    (void)g_port;
    return 0;
}

int32_t pm_metal_asgi_ready(void)
{
    return g_ready ? 1 : 0;
}

int32_t pm_metal_asgi_poll(void)
{
    uint8_t buf[512];
    uint32_t n = 0;
    char method[8];
    char path[128];
    char body[512];
    int status = 404;
    int handled;

    if (!g_ready) {
        return 0;
    }
    pm_metal_net_ip_tcp_focus_passive();
    if (!pm_metal_net_ip_tcp_established()) {
        return 0;
    }
    if (pm_metal_net_ip_tcp_recv(buf, sizeof(buf) - 1u, &n) != 1 || n < 5u) {
        return 0;
    }
    buf[n] = 0;
    if (parse_req((char *)buf, method, sizeof(method), path, sizeof(path)) != 0) {
        send_resp(400, "text/plain", "bad request");
        (void)pm_metal_net_ip_tcp_passive_relisten(g_port);
        return 1;
    }
    body[0] = 0;
    handled = pm_metal_inspect_handle(method, path, &status, body, sizeof(body));
    if (handled == 1) {
        send_resp(status, "application/json", body);
        (void)pm_metal_net_ip_tcp_passive_relisten(g_port);
        return 1;
    }
    if (strcmp(method, "GET") == 0 && serve_httpd_static(path)) {
        (void)pm_metal_net_ip_tcp_passive_relisten(g_port);
        return 1;
    }
    send_resp(404, "application/json", "{\"error\":\"not found\"}");
    (void)pm_metal_net_ip_tcp_passive_relisten(g_port);
    return 1;
}
