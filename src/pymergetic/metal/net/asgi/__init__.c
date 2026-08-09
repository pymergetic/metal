/*
 * C ASGI HTTP: Inspect contract + static from wasmmod pack VFS (/mods/…).
 * Owns its own TCP listen sock on :port — never a shared passive PCB.
 */
#include "pymergetic/metal/net/asgi/__init__.h"

#include <string.h>

#include "pymergetic/metal/async/handle.h"
#include "pymergetic/metal/fs.h"
#include "pymergetic/metal/inspect/__init__.h"
#include "pymergetic/metal/net/http/__init__.h"
#include "pymergetic/metal/net/ip/sock.h"
#include "pymergetic/metal/net/tls/__init__.h"
#include "pymergetic/metal/pack/mod_packs.h"
#include "ws.h"

#ifndef PM_METAL_ASGI_VFS_BUF
#define PM_METAL_ASGI_VFS_BUF 4096u
#endif

typedef struct {
    const char *url;
    const char *root;
    const char *theme;
} asgi_mount_t;

/* Mirrors httpd.json — roots are pack VFS paths. */
static const asgi_mount_t g_mounts[] = {
    {"/inspect", "/mods/pymergetic.metal.inspect/www/inspect", "metal"},
};
static const unsigned g_mount_count =
    (unsigned)(sizeof(g_mounts) / sizeof(g_mounts[0]));

static int g_ready;
static uint16_t g_port;
static uint16_t g_tls_port;
static pm_metal_net_ip_sock_h g_listen;
static pm_metal_net_ip_sock_h g_listen_tls;
static pm_metal_net_ip_sock_h g_conn;
static pm_metal_net_tls_h g_tls;
static uint32_t g_hs_ah;
static int g_hs_done;
static int g_conn_tls;
static int g_ws;
static uint8_t g_vfs_buf[PM_METAL_ASGI_VFS_BUF];

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

static void sock_send_all(const uint8_t *data, uint32_t len)
{
    if (g_conn == PM_METAL_NET_IP_SOCK_INVALID || data == NULL || len == 0u) {
        return;
    }
    if (g_conn_tls && g_tls != PM_METAL_NET_TLS_INVALID) {
        (void)pm_metal_net_tls_write(g_tls, data, len);
    } else {
        (void)pm_metal_net_ip_send(g_conn, data, len);
    }
}

static void send_resp_bin(int status, const char *ctype, const uint8_t *body,
                          uint32_t blen)
{
    char hdr[192];
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
    if (o == 0) {
        return;
    }
    if (blen > 0u && body != NULL && o + blen <= 768u) {
        char msg[768];
        memcpy(msg, hdr, o);
        memcpy(msg + o, body, blen);
        sock_send_all((const uint8_t *)msg, (uint32_t)(o + blen));
        return;
    }
    sock_send_all((const uint8_t *)hdr, (uint32_t)o);
    if (blen > 0u && body != NULL) {
        sock_send_all(body, blen);
    }
}

static void send_resp(int status, const char *ctype, const char *body)
{
    send_resp_bin(status, ctype, (const uint8_t *)body,
                  body ? (uint32_t)strlen(body) : 0u);
}

static const char *ctype_for(const char *rel)
{
    size_t n = strlen(rel);
    if (n >= 5 && strcmp(rel + n - 5, ".html") == 0) {
        return "text/html; charset=utf-8";
    }
    if (n >= 4 && strcmp(rel + n - 4, ".css") == 0) {
        return "text/css; charset=utf-8";
    }
    if (n >= 3 && strcmp(rel + n - 3, ".js") == 0) {
        return "application/javascript; charset=utf-8";
    }
    if (n >= 5 && strcmp(rel + n - 5, ".json") == 0) {
        return "application/json";
    }
    return "application/octet-stream";
}

static int join_path(char *out, size_t out_len, const char *root, const char *rel)
{
    size_t n;

    if (out == NULL || root == NULL || rel == NULL || out_len < 4u) {
        return -1;
    }
    n = strlen(root);
    if (n + 1u + strlen(rel) + 1u > out_len) {
        return -1;
    }
    memcpy(out, root, n);
    out[n++] = '/';
    memcpy(out + n, rel, strlen(rel) + 1u);
    return 0;
}

static int serve_httpd_static(const char *path)
{
    unsigned mi;
    const char *rel;
    char vfs_path[192];
    size_t ulen;
    uint32_t ah;
    uint32_t n;

    for (mi = 0; mi < g_mount_count; mi++) {
        const char *url = g_mounts[mi].url;
        const char *root = g_mounts[mi].root;
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
        if (join_path(vfs_path, sizeof(vfs_path), root, rel) != 0) {
            return 0;
        }
        ah = pm_metal_fs_read_async((const uint8_t *)vfs_path, g_vfs_buf,
                                    (uint32_t)sizeof(g_vfs_buf));
        n = pm_metal_async_result_u32(ah);
        pm_metal_async_coro_close(ah);
        if (n == 0u || n == PM_METAL_FS_INVALID || n >= sizeof(g_vfs_buf)) {
            return 0;
        }
        send_resp_bin(200, ctype_for(rel), g_vfs_buf, n);
        return 1;
    }
    return 0;
}

static void conn_done(void)
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
    g_ws = 0;
}

int32_t pm_metal_asgi_init(uint16_t port)
{
    if (port == 0) {
        port = 80;
    }
    if (g_ready && g_port == port && pm_metal_net_ip_sock_listening(g_listen)) {
        return 0;
    }
    if (pm_metal_inspect_init() != 0) {
        return -2;
    }
    (void)pm_metal_mod_packs_mount_all();
    /* DIY HTTP smoke may hold :80/:443 — release so ASGI owns those ports. */
    pm_metal_net_http_shutdown();
    conn_done();
    if (g_listen != PM_METAL_NET_IP_SOCK_INVALID) {
        pm_metal_net_ip_close(g_listen);
        g_listen = PM_METAL_NET_IP_SOCK_INVALID;
    }
    g_listen = pm_metal_net_ip_socket(PM_METAL_NET_IP_AF_INET, PM_METAL_NET_IP_SOCK_STREAM);
    if (g_listen == PM_METAL_NET_IP_SOCK_INVALID) {
        return -3;
    }
    if (pm_metal_net_ip_listen(g_listen, port) == 0u) {
        pm_metal_net_ip_close(g_listen);
        g_listen = PM_METAL_NET_IP_SOCK_INVALID;
        return -3;
    }
    g_port = port;
    g_ready = 1;
    return 0;
}

int32_t pm_metal_asgi_init_tls(uint16_t port)
{
    if (port == 0) {
        port = 443;
    }
    if (g_listen_tls != PM_METAL_NET_IP_SOCK_INVALID && g_tls_port == port &&
        pm_metal_net_ip_sock_listening(g_listen_tls)) {
        g_ready = 1;
        return 0;
    }
    if (pm_metal_net_tls_init() != 0) {
        return -2;
    }
    if (pm_metal_net_tls_load_smoke_server() != 0) {
        return -2;
    }
    pm_metal_net_http_shutdown();
    if (g_listen_tls != PM_METAL_NET_IP_SOCK_INVALID) {
        pm_metal_net_ip_close(g_listen_tls);
        g_listen_tls = PM_METAL_NET_IP_SOCK_INVALID;
    }
    g_listen_tls = pm_metal_net_ip_socket(PM_METAL_NET_IP_AF_INET, PM_METAL_NET_IP_SOCK_STREAM);
    if (g_listen_tls == PM_METAL_NET_IP_SOCK_INVALID) {
        return -3;
    }
    if (pm_metal_net_ip_listen(g_listen_tls, port) == 0u) {
        pm_metal_net_ip_close(g_listen_tls);
        g_listen_tls = PM_METAL_NET_IP_SOCK_INVALID;
        return -3;
    }
    g_tls_port = port;
    g_ready = 1;
    return 0;
}

int32_t pm_metal_asgi_ready(void)
{
    return g_ready ? 1 : 0;
}

static uint32_t conn_recv(uint8_t *buf, uint32_t cap)
{
    if (g_conn_tls) {
        if (g_tls == PM_METAL_NET_TLS_INVALID) {
            g_tls = pm_metal_net_tls_server_open(g_conn);
            if (g_tls == PM_METAL_NET_TLS_INVALID) {
                return (uint32_t)-1;
            }
            g_hs_ah = pm_metal_net_tls_handshake(g_tls);
            g_hs_done = 0;
        }
        if (!g_hs_done) {
            if (g_hs_ah == 0u || pm_metal_async_status(g_hs_ah) != PM_METAL_ASYNC_DONE) {
                return 0u;
            }
            if (pm_metal_async_result_u32(g_hs_ah) != 1u) {
                return (uint32_t)-1;
            }
            pm_metal_async_coro_close(g_hs_ah);
            g_hs_ah = 0;
            g_hs_done = 1;
        }
        return pm_metal_net_tls_try_read(g_tls, buf, cap);
    }
    return pm_metal_net_ip_try_recv(g_conn, buf, cap);
}

static int32_t poll_ws(void)
{
    uint8_t buf[512];
    uint8_t payload[256];
    uint8_t out[280];
    uint32_t n;
    uint32_t plen = 0;
    uint8_t opcode = 0;
    int32_t st;
    uint32_t elen;

    n = conn_recv(buf, sizeof(buf));
    if (n == 0u) {
        if (pm_metal_net_ip_sock_peer_closed(g_conn)) {
            conn_done();
        }
        return 0;
    }
    if (n == (uint32_t)-1) {
        conn_done();
        return 0;
    }
    st = pm_metal_asgi_ws_decode(buf, n, &opcode, payload, sizeof(payload), &plen);
    if (st == 0) {
        return 0;
    }
    if (st < 0) {
        conn_done();
        return 0;
    }
    if (opcode == 0x8u) { /* close */
        conn_done();
        return 1;
    }
    if (opcode == 0x9u) { /* ping → pong */
        elen = pm_metal_asgi_ws_encode_server(0xAu, payload, plen, out, sizeof(out));
        sock_send_all(out, elen);
        return 1;
    }
    /* Echo text/binary */
    elen = pm_metal_asgi_ws_encode_server(opcode, payload, plen, out, sizeof(out));
    sock_send_all(out, elen);
    return 1;
}

int32_t pm_metal_asgi_poll(void)
{
    uint8_t buf[512];
    uint32_t n = 0;
    char method[8];
    char path[128];
    char body[512];
    char ws_key[64];
    char ws_accept[40];
    char up_hdr[192];
    int status = 404;
    int handled;
    size_t o;

    if (!g_ready) {
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
    if (g_ws) {
        return poll_ws();
    }
    n = conn_recv(buf, sizeof(buf) - 1u);
    if (n == 0u) {
        if (pm_metal_net_ip_sock_peer_closed(g_conn)) {
            conn_done();
        }
        return 0;
    }
    if (n == (uint32_t)-1 || n < 5u) {
        conn_done();
        return 0;
    }
    buf[n] = 0;
    if (pm_metal_asgi_ws_request((char *)buf, ws_key, sizeof(ws_key))) {
        if (pm_metal_asgi_ws_accept_key(ws_key, ws_accept, sizeof(ws_accept)) != 0) {
            send_resp(400, "text/plain", "bad ws key");
            conn_done();
            return 1;
        }
        o = 0;
#define APP(s)                                                                 \
    do {                                                                       \
        size_t l = strlen(s);                                                  \
        if (o + l < sizeof(up_hdr)) {                                          \
            memcpy(up_hdr + o, s, l);                                          \
            o += l;                                                            \
        }                                                                      \
    } while (0)
        APP("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
            "Connection: Upgrade\r\nSec-WebSocket-Accept: ");
        APP(ws_accept);
        APP("\r\n\r\n");
#undef APP
        sock_send_all((const uint8_t *)up_hdr, (uint32_t)o);
        g_ws = 1;
        return 1;
    }
    if (parse_req((char *)buf, method, sizeof(method), path, sizeof(path)) != 0) {
        send_resp(400, "text/plain", "bad request");
        conn_done();
        return 1;
    }
    body[0] = 0;
    handled = pm_metal_inspect_handle(method, path, &status, body, sizeof(body));
    if (handled == 1) {
        send_resp(status, "application/json", body);
        conn_done();
        return 1;
    }
    if (strcmp(method, "GET") == 0 && serve_httpd_static(path)) {
        conn_done();
        return 1;
    }
    send_resp(404, "application/json", "{\"error\":\"not found\"}");
    conn_done();
    return 1;
}
