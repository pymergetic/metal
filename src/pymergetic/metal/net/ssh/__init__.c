/*
 * DIY SSH — version exchange now; KEX/auth/channels next (no wolfSSH).
 */
#include "pymergetic/metal/net/ssh/__init__.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/net/ip/tcp.h"

typedef enum {
    SSH_ST_IDLE = 0,
    SSH_ST_IDENT_SENT,
    SSH_ST_IDENT_DONE,
    SSH_ST_ERROR
} ssh_st_t;

static ssh_st_t g_st;
static uint32_t g_listen_port;
static uint8_t g_peer_ident[128];
static uint32_t g_peer_len;
static int32_t g_served;

static const char k_ident[] = "SSH-2.0-metal\r\n";

int32_t pm_metal_net_ssh_available(void)
{
    /* DIY path present; crypto not yet — µPy smoke still prints stub. */
    return 0;
}

int32_t pm_metal_net_ssh_init(void)
{
    g_listen_port = 0;
    g_st = SSH_ST_IDLE;
    g_peer_len = 0;
    g_served = 0;
    memset(g_peer_ident, 0, sizeof(g_peer_ident));
    return 0;
}

int32_t pm_metal_net_ssh_autoload(void)
{
    return pm_metal_net_ssh_init();
}

uint32_t pm_metal_net_ssh_listen(uint32_t port)
{
    g_listen_port = port;
    g_st = SSH_ST_IDLE;
    g_peer_len = 0;
    g_served = 0;
    if (pm_metal_net_ip_tcp_listen((uint16_t)port) != 0) {
        return 0;
    }
    return 1;
}

void pm_metal_net_ssh_close(uint32_t s)
{
    (void)s;
    pm_metal_net_ip_tcp_abort();
    g_st = SSH_ST_IDLE;
    g_peer_len = 0;
}

static int32_t send_ident(void)
{
    if (pm_metal_net_ip_tcp_send(k_ident, (uint32_t)(sizeof(k_ident) - 1u)) != 0) {
        return -1;
    }
    return 0;
}

static int32_t feed_peer_ident(void)
{
    uint8_t chunk[64];
    uint32_t n = 0;
    uint32_t i;
    int32_t rc;

    rc = pm_metal_net_ip_tcp_recv(chunk, sizeof(chunk), &n);
    if (rc != 1 || n == 0u) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (g_peer_len + 1u >= sizeof(g_peer_ident)) {
            g_st = SSH_ST_ERROR;
            return -1;
        }
        g_peer_ident[g_peer_len++] = chunk[i];
        if (g_peer_len >= 2u
            && g_peer_ident[g_peer_len - 2u] == (uint8_t)'\r'
            && g_peer_ident[g_peer_len - 1u] == (uint8_t)'\n') {
            if (g_peer_len < 9u
                || memcmp(g_peer_ident, "SSH-", 4) != 0) {
                g_st = SSH_ST_ERROR;
                return -1;
            }
            g_st = SSH_ST_IDENT_DONE;
            g_served = 1;
            return 1;
        }
    }
    return 0;
}

int32_t pm_metal_net_ssh_poll(void)
{
    if (!pm_metal_net_ip_tcp_established()) {
        return 0;
    }
    if (g_st == SSH_ST_IDLE) {
        if (send_ident() != 0) {
            g_st = SSH_ST_ERROR;
            return -1;
        }
        g_st = SSH_ST_IDENT_SENT;
    }
    if (g_st == SSH_ST_IDENT_SENT) {
        (void)feed_peer_ident();
    }
    if (g_st == SSH_ST_IDENT_DONE) {
        return 1;
    }
    if (g_st == SSH_ST_ERROR) {
        return -1;
    }
    return 0;
}

int32_t pm_metal_net_ssh_served(void)
{
    return g_served;
}

int32_t pm_metal_net_ssh_status(uint8_t *buf, uint32_t buf_len)
{
    static const char msg[] = "ssh: diy-ident";
    uint32_t i;

    if (buf == NULL || buf_len == 0u) {
        return -1;
    }
    for (i = 0; i + 1u < buf_len && msg[i] != '\0'; i++) {
        buf[i] = (uint8_t)msg[i];
    }
    buf[i] = 0;
    return 0;
}

uint32_t pm_metal_net_ssh_listen_port(void)
{
    return g_listen_port;
}

int32_t pm_metal_net_ssh_hostkey_label(uint8_t *buf, uint32_t buf_len)
{
    if (buf != NULL && buf_len > 0u) {
        buf[0] = 0;
    }
    return -1;
}

int32_t pm_metal_net_ssh_client_exec(const char *host, uint16_t port,
    const char *user, const char *cmd, uint8_t *buf, uint32_t cap, uint32_t *len_out)
{
    (void)host;
    (void)port;
    (void)user;
    (void)cmd;
    (void)buf;
    (void)cap;
    if (len_out != NULL) {
        *len_out = 0;
    }
    return -1;
}

int32_t pm_metal_net_ssh_banner_send(void)
{
    return pm_metal_net_ssh_poll() >= 0 ? 0 : -1;
}

int32_t pm_metal_net_ssh_banner_sent(void)
{
    return (g_st == SSH_ST_IDENT_SENT || g_st == SSH_ST_IDENT_DONE) ? 1 : 0;
}

void pm_metal_net_ssh_banner_reset(void)
{
    g_st = SSH_ST_IDLE;
    g_peer_len = 0;
    g_served = 0;
    memset(g_peer_ident, 0, sizeof(g_peer_ident));
}

int32_t pm_metal_net_ssh_bind_reg(void)
{
    return 0;
}
