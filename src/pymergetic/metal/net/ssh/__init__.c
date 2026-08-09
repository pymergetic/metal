/*
 * DIY SSH — curve25519-sha256 KEX, chacha20-poly1305@openssh.com,
 * SERVICE_ACCEPT + USERAUTH (none / password "metal").
 */
#include "pymergetic/metal/net/ssh/__init__.h"

#include "crypto.h"
#include "kex.h"
#include "packet.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/net/ip/sock.h"
#include "pymergetic/metal/process/__init__.h"

typedef enum {
    SSH_ST_IDLE = 0,
    SSH_ST_IDENT_SENT,
    SSH_ST_IDENT_DONE,
    SSH_ST_KEXINIT_SENT,
    SSH_ST_WAIT_ECDH,
    SSH_ST_WAIT_NEWKEYS,
    SSH_ST_SERVICE,
    SSH_ST_AUTH,
    SSH_ST_SESSION,
    SSH_ST_DONE,
    SSH_ST_ERROR
} ssh_st_t;

static ssh_st_t g_st;
static uint32_t g_listen_port;
static pm_metal_net_ip_sock_h g_listen_h;
static pm_metal_net_ip_sock_h g_conn_h;
static uint8_t g_peer_ident[PM_METAL_SSH_IDENT_MAX];
static uint32_t g_peer_len;
static int32_t g_served;
static int32_t g_crypto_ready;
static pm_metal_net_ssh_kex_t g_kex;
static uint32_t g_peer_chan;
static uint32_t g_our_chan;

static const char k_ident[] = "SSH-2.0-metal\r\n";
static const char k_ident_bare[] = "SSH-2.0-metal";

#define SSH_MSG_DISCONNECT 1
#define SSH_MSG_IGNORE 2
#define SSH_MSG_SERVICE_REQUEST 5
#define SSH_MSG_SERVICE_ACCEPT 6
#define SSH_MSG_USERAUTH_REQUEST 50
#define SSH_MSG_USERAUTH_FAILURE 51
#define SSH_MSG_USERAUTH_SUCCESS 52
#define SSH_MSG_GLOBAL_REQUEST 80
#define SSH_MSG_REQUEST_SUCCESS 81
#define SSH_MSG_REQUEST_FAILURE 82
#define SSH_MSG_CHANNEL_OPEN 90
#define SSH_MSG_CHANNEL_OPEN_CONFIRMATION 91
#define SSH_MSG_CHANNEL_WINDOW_ADJUST 93
#define SSH_MSG_CHANNEL_DATA 94
#define SSH_MSG_CHANNEL_EOF 96
#define SSH_MSG_CHANNEL_CLOSE 97
#define SSH_MSG_CHANNEL_REQUEST 98
#define SSH_MSG_CHANNEL_SUCCESS 99
#define SSH_MSG_CHANNEL_FAILURE 100
#define SSH_MSG_KEXINIT 20
#define SSH_MSG_NEWKEYS 21
#define SSH_MSG_KEX_ECDH_INIT 30

static uint32_t get_u32b(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8)
           | (uint32_t)p[3];
}

static void put_u32b(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static int32_t send_channel_open_confirm(uint32_t peer_chan)
{
    uint8_t pl[17];

    pl[0] = SSH_MSG_CHANNEL_OPEN_CONFIRMATION;
    put_u32b(pl + 1, peer_chan);
    put_u32b(pl + 5, g_our_chan);
    put_u32b(pl + 9, 32768u);
    put_u32b(pl + 13, 16384u);
    return pm_metal_net_ssh_pkt_send(pl, 17);
}

static int32_t send_channel_success(uint32_t peer_chan)
{
    uint8_t pl[5];

    pl[0] = SSH_MSG_CHANNEL_SUCCESS;
    put_u32b(pl + 1, peer_chan);
    return pm_metal_net_ssh_pkt_send(pl, 5);
}

static int32_t send_channel_failure(uint32_t peer_chan)
{
    uint8_t pl[5];

    pl[0] = SSH_MSG_CHANNEL_FAILURE;
    put_u32b(pl + 1, peer_chan);
    return pm_metal_net_ssh_pkt_send(pl, 5);
}

static int32_t send_exit_and_close(uint32_t peer_chan)
{
    uint8_t pl[64];
    uint32_t o;
    const char *req = "exit-status";

    pl[0] = SSH_MSG_CHANNEL_REQUEST;
    put_u32b(pl + 1, peer_chan);
    o = 5;
    put_u32b(pl + o, (uint32_t)strlen(req));
    o += 4;
    memcpy(pl + o, req, strlen(req));
    o += (uint32_t)strlen(req);
    pl[o++] = 0; /* want_reply = false */
    put_u32b(pl + o, 0); /* exit status 0 */
    o += 4;
    if (pm_metal_net_ssh_pkt_send(pl, o) != 0) {
        return -1;
    }
    pl[0] = SSH_MSG_CHANNEL_EOF;
    put_u32b(pl + 1, peer_chan);
    if (pm_metal_net_ssh_pkt_send(pl, 5) != 0) {
        return -1;
    }
    pl[0] = SSH_MSG_CHANNEL_CLOSE;
    put_u32b(pl + 1, peer_chan);
    return pm_metal_net_ssh_pkt_send(pl, 5);
}

static int32_t send_service_accept(void)
{
    uint8_t pl[64];
    uint32_t n;
    const char *svc = "ssh-userauth";

    pl[0] = SSH_MSG_SERVICE_ACCEPT;
    n = (uint32_t)strlen(svc);
    pl[1] = (uint8_t)(n >> 24);
    pl[2] = (uint8_t)(n >> 16);
    pl[3] = (uint8_t)(n >> 8);
    pl[4] = (uint8_t)n;
    memcpy(pl + 5, svc, n);
    return pm_metal_net_ssh_pkt_send(pl, 5u + n);
}

static int32_t send_auth_success(void)
{
    uint8_t pl = SSH_MSG_USERAUTH_SUCCESS;
    return pm_metal_net_ssh_pkt_send(&pl, 1);
}

static int32_t send_auth_failure(void)
{
    uint8_t pl[32];
    const char *methods = "password,none";
    uint32_t n = (uint32_t)strlen(methods);

    pl[0] = SSH_MSG_USERAUTH_FAILURE;
    pl[1] = (uint8_t)(n >> 24);
    pl[2] = (uint8_t)(n >> 16);
    pl[3] = (uint8_t)(n >> 8);
    pl[4] = (uint8_t)n;
    memcpy(pl + 5, methods, n);
    pl[5u + n] = 0; /* partial success = false */
    return pm_metal_net_ssh_pkt_send(pl, 6u + n);
}

static int32_t send_kexinit(void)
{
    uint8_t pl[PM_METAL_SSH_KEXINIT_MAX];
    uint32_t n;

    n = pm_metal_net_ssh_kex_build_init(pl, sizeof(pl));
    if (n == 0u) {
        return -1;
    }
    if (n > sizeof(g_kex.i_s)) {
        return -1;
    }
    memcpy(g_kex.i_s, pl, n);
    g_kex.i_s_len = n;
    return pm_metal_net_ssh_pkt_send(pl, n);
}

static int32_t send_newkeys(void)
{
    uint8_t pl = SSH_MSG_NEWKEYS;

    return pm_metal_net_ssh_pkt_send(&pl, 1);
}

int32_t pm_metal_net_ssh_available(void)
{
    return g_crypto_ready;
}

int32_t pm_metal_net_ssh_init(void)
{
    g_listen_port = 0;
    g_listen_h = PM_METAL_NET_IP_SOCK_INVALID;
    g_conn_h = PM_METAL_NET_IP_SOCK_INVALID;
    g_st = SSH_ST_IDLE;
    g_peer_len = 0;
    g_served = 0;
    g_peer_chan = 0;
    g_our_chan = 0;
    memset(g_peer_ident, 0, sizeof(g_peer_ident));
    pm_metal_net_ssh_kex_reset(&g_kex);
    pm_metal_net_ssh_pkt_reset();
    pm_metal_net_ssh_pkt_bind_sock(PM_METAL_NET_IP_SOCK_INVALID);
    if (pm_metal_net_ssh_crypto_init() != 0) {
        g_crypto_ready = 0;
        return -1;
    }
    g_crypto_ready = 1;
    g_kex.v_s_len = (uint32_t)(sizeof(k_ident_bare) - 1u);
    memcpy(g_kex.v_s, k_ident_bare, g_kex.v_s_len);
    return 0;
}

int32_t pm_metal_net_ssh_autoload(void)
{
    return pm_metal_net_ssh_init();
}

static uint32_t g_sshd_pid;

static void sshd_teardown(uint32_t pid, void *user)
{
    (void)pid;
    (void)user;
    g_sshd_pid = 0;
    pm_metal_net_ssh_release();
}

uint32_t pm_metal_net_ssh_listen(uint32_t port)
{
    if (port == 0u) {
        return 0;
    }
    if (g_conn_h != PM_METAL_NET_IP_SOCK_INVALID) {
        pm_metal_net_ip_close(g_conn_h);
        g_conn_h = PM_METAL_NET_IP_SOCK_INVALID;
    }
    if (g_listen_h != PM_METAL_NET_IP_SOCK_INVALID) {
        pm_metal_net_ip_close(g_listen_h);
        g_listen_h = PM_METAL_NET_IP_SOCK_INVALID;
    }
    g_listen_h = pm_metal_net_ip_socket(PM_METAL_NET_IP_AF_INET, PM_METAL_NET_IP_SOCK_STREAM);
    if (g_listen_h == PM_METAL_NET_IP_SOCK_INVALID) {
        g_listen_port = 0;
        return 0;
    }
    if (pm_metal_net_ip_listen(g_listen_h, port) == 0u) {
        pm_metal_net_ip_close(g_listen_h);
        g_listen_h = PM_METAL_NET_IP_SOCK_INVALID;
        g_listen_port = 0;
        return 0;
    }
    g_listen_port = port;
    g_st = SSH_ST_IDLE;
    g_peer_len = 0;
    g_served = 0;
    g_peer_chan = 0;
    g_our_chan = 0;
    pm_metal_net_ssh_kex_reset(&g_kex);
    g_kex.v_s_len = (uint32_t)(sizeof(k_ident_bare) - 1u);
    memcpy(g_kex.v_s, k_ident_bare, g_kex.v_s_len);
    pm_metal_net_ssh_pkt_reset();
    pm_metal_net_ssh_pkt_bind_sock(PM_METAL_NET_IP_SOCK_INVALID);
    /* Crown sshd as daemon intent root (table-only until accept is a task). */
    if (g_sshd_pid != 0u) {
        (void)pm_metal_process_quit(g_sshd_pid, 0);
        g_sshd_pid = 0;
    }
    g_sshd_pid = pm_metal_process_crown(0, PM_METAL_PROCESS_MODE_DAEMON, "sshd", sshd_teardown,
                                        NULL);
    return 1;
}

/* Close SSH socks only — never touches other services' listeners. */
void pm_metal_net_ssh_release(void)
{
    if (g_conn_h != PM_METAL_NET_IP_SOCK_INVALID) {
        pm_metal_net_ip_close(g_conn_h);
        g_conn_h = PM_METAL_NET_IP_SOCK_INVALID;
    }
    if (g_listen_h != PM_METAL_NET_IP_SOCK_INVALID) {
        pm_metal_net_ip_close(g_listen_h);
        g_listen_h = PM_METAL_NET_IP_SOCK_INVALID;
    }
    g_listen_port = 0;
    g_st = SSH_ST_IDLE;
    g_peer_len = 0;
    g_served = 0;
    g_peer_chan = 0;
    g_our_chan = 0;
    pm_metal_net_ssh_pkt_reset();
    pm_metal_net_ssh_pkt_bind_sock(PM_METAL_NET_IP_SOCK_INVALID);
}

void pm_metal_net_ssh_close(uint32_t s)
{
    (void)s;
    pm_metal_net_ssh_release();
}

static int32_t send_ident(void)
{
    if (g_conn_h == PM_METAL_NET_IP_SOCK_INVALID) {
        return -1;
    }
    if (pm_metal_net_ip_send(g_conn_h, k_ident, (uint32_t)(sizeof(k_ident) - 1u)) == 0u) {
        return -1;
    }
    return 0;
}

static void store_peer_ident_bare(void)
{
    uint32_t n = g_peer_len;

    while (n > 0u
        && (g_peer_ident[n - 1u] == (uint8_t)'\n'
            || g_peer_ident[n - 1u] == (uint8_t)'\r')) {
        n--;
    }
    if (n > sizeof(g_kex.v_c)) {
        n = sizeof(g_kex.v_c);
    }
    memcpy(g_kex.v_c, g_peer_ident, n);
    g_kex.v_c_len = n;
}

static int32_t feed_peer_ident(void)
{
    uint8_t chunk[64];
    uint32_t n = 0;
    uint32_t i;

    if (g_conn_h == PM_METAL_NET_IP_SOCK_INVALID) {
        return 0;
    }
    n = pm_metal_net_ip_try_recv(g_conn_h, chunk, sizeof(chunk));
    if (n == 0u || n == (uint32_t)-1) {
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
            if (g_peer_len < 9u || memcmp(g_peer_ident, "SSH-", 4) != 0) {
                g_st = SSH_ST_ERROR;
                return -1;
            }
            store_peer_ident_bare();
            if (i + 1u < n) {
                (void)pm_metal_net_ssh_pkt_push(chunk + i + 1u, n - (i + 1u));
            }
            g_st = SSH_ST_IDENT_DONE;
            return 1;
        }
    }
    return 0;
}

int32_t pm_metal_net_ssh_poll(void)
{
    uint8_t pl[PM_METAL_SSH_KEXINIT_MAX];
    uint8_t reply[512];
    uint32_t n = 0;
    uint32_t reply_len = 0;
    int32_t rc;

    if (g_listen_port == 0u || !g_crypto_ready ||
        g_listen_h == PM_METAL_NET_IP_SOCK_INVALID) {
        return 0;
    }
    if (g_conn_h == PM_METAL_NET_IP_SOCK_INVALID) {
        g_conn_h = pm_metal_net_ip_try_accept(g_listen_h);
        if (g_conn_h == PM_METAL_NET_IP_SOCK_INVALID) {
            return 0;
        }
        pm_metal_net_ssh_pkt_bind_sock(g_conn_h);
        g_st = SSH_ST_IDLE;
        g_peer_len = 0;
        g_served = 0;
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
        if (send_kexinit() != 0) {
            g_st = SSH_ST_ERROR;
            return -1;
        }
        g_st = SSH_ST_KEXINIT_SENT;
    }
    if (g_st == SSH_ST_KEXINIT_SENT) {
        rc = pm_metal_net_ssh_pkt_recv(pl, sizeof(pl), &n);
        if (rc < 0) {
            g_st = SSH_ST_ERROR;
            return -1;
        }
        if (rc == 1 && n > 0u && pl[0] == SSH_MSG_KEXINIT) {
            if (n > sizeof(g_kex.i_c)) {
                g_st = SSH_ST_ERROR;
                return -1;
            }
            memcpy(g_kex.i_c, pl, n);
            g_kex.i_c_len = n;
            g_st = SSH_ST_WAIT_ECDH;
        }
    }
    if (g_st == SSH_ST_WAIT_ECDH) {
        rc = pm_metal_net_ssh_pkt_recv(pl, sizeof(pl), &n);
        if (rc < 0) {
            g_st = SSH_ST_ERROR;
            return -1;
        }
        if (rc == 1 && n > 0u && pl[0] == SSH_MSG_KEX_ECDH_INIT) {
            if (pm_metal_net_ssh_kex_server_reply(&g_kex, pl, n, reply,
                    sizeof(reply), &reply_len)
                != 0) {
                g_st = SSH_ST_ERROR;
                return -1;
            }
            if (pm_metal_net_ssh_pkt_send(reply, reply_len) != 0) {
                g_st = SSH_ST_ERROR;
                return -1;
            }
            if (send_newkeys() != 0) {
                g_st = SSH_ST_ERROR;
                return -1;
            }
            g_st = SSH_ST_WAIT_NEWKEYS;
        }
    }
    if (g_st == SSH_ST_WAIT_NEWKEYS) {
        rc = pm_metal_net_ssh_pkt_recv(pl, sizeof(pl), &n);
        if (rc < 0) {
            g_st = SSH_ST_ERROR;
            return -1;
        }
        if (rc == 1 && n > 0u && pl[0] == SSH_MSG_NEWKEYS) {
            if (pm_metal_net_ssh_kex_derive_keys(&g_kex) != 0) {
                g_st = SSH_ST_ERROR;
                return -1;
            }
            pm_metal_net_ssh_pkt_set_keys(g_kex.key_c2s, g_kex.key_s2c);
            g_st = SSH_ST_SERVICE;
        }
    }
    if (g_st == SSH_ST_SERVICE) {
        rc = pm_metal_net_ssh_pkt_recv(pl, sizeof(pl), &n);
        if (rc < 0) {
            g_st = SSH_ST_ERROR;
            return -1;
        }
        if (rc == 1 && n >= 5u && pl[0] == SSH_MSG_SERVICE_REQUEST) {
            if (send_service_accept() != 0) {
                g_st = SSH_ST_ERROR;
                return -1;
            }
            g_st = SSH_ST_AUTH;
        }
    }
    if (g_st == SSH_ST_AUTH) {
        rc = pm_metal_net_ssh_pkt_recv(pl, sizeof(pl), &n);
        if (rc < 0) {
            g_st = SSH_ST_ERROR;
            return -1;
        }
        if (rc == 1 && n >= 1u) {
            if (pl[0] == SSH_MSG_IGNORE || pl[0] == SSH_MSG_GLOBAL_REQUEST) {
                if (pl[0] == SSH_MSG_GLOBAL_REQUEST && n >= 2u) {
                    /* want_reply at end of name string — best-effort fail. */
                    uint32_t slen = (n >= 5u) ? get_u32b(pl + 1) : 0;
                    if (5u + slen < n && pl[5u + slen] != 0) {
                        uint8_t fail = SSH_MSG_REQUEST_FAILURE;
                        (void)pm_metal_net_ssh_pkt_send(&fail, 1);
                    }
                }
            } else if (pl[0] == SSH_MSG_USERAUTH_REQUEST && n >= 5u) {
                /* Parse method name (user, service, method strings). */
                uint32_t o = 1;
                uint32_t slen;
                int ok = 0;
                const char *method = NULL;
                uint32_t method_len = 0;

                for (int fi = 0; fi < 3; fi++) {
                    if (o + 4u > n) {
                        break;
                    }
                    slen = get_u32b(pl + o);
                    o += 4;
                    if (o + slen > n) {
                        break;
                    }
                    if (fi == 2) {
                        method = (const char *)(pl + o);
                        method_len = slen;
                    }
                    o += slen;
                }
                /* Accept password "metal" only; reject none so client retries. */
                if (method != NULL && method_len == 8u &&
                    memcmp(method, "password", 8) == 0) {
                    if (o < n) {
                        o += 1; /* FALSE = new password follow */
                    }
                    if (o + 4u <= n) {
                        slen = get_u32b(pl + o);
                        o += 4;
                        if (o + slen <= n && slen == 5u &&
                            memcmp(pl + o, "metal", 5) == 0) {
                            ok = 1;
                        }
                    }
                }
                if (ok) {
                    if (send_auth_success() != 0) {
                        g_st = SSH_ST_ERROR;
                        return -1;
                    }
                    g_st = SSH_ST_SESSION;
                } else {
                    (void)send_auth_failure();
                }
            }
        }
    }
    if (g_st == SSH_ST_SESSION) {
        rc = pm_metal_net_ssh_pkt_recv(pl, sizeof(pl), &n);
        if (rc < 0) {
            g_st = SSH_ST_ERROR;
            return -1;
        }
        if (rc == 1 && n >= 1u) {
            if (pl[0] == SSH_MSG_IGNORE || pl[0] == SSH_MSG_CHANNEL_WINDOW_ADJUST
                || pl[0] == SSH_MSG_CHANNEL_DATA || pl[0] == SSH_MSG_CHANNEL_EOF) {
                /* ignore */
            } else if (pl[0] == SSH_MSG_GLOBAL_REQUEST) {
                uint32_t slen = (n >= 5u) ? get_u32b(pl + 1) : 0;
                if (5u + slen < n && pl[5u + slen] != 0) {
                    uint8_t fail = SSH_MSG_REQUEST_FAILURE;
                    (void)pm_metal_net_ssh_pkt_send(&fail, 1);
                }
            } else if (pl[0] == SSH_MSG_CHANNEL_OPEN && n >= 17u) {
                uint32_t slen = get_u32b(pl + 1);
                uint32_t o = 5u + slen;
                if (o + 12u <= n && slen == 7u && memcmp(pl + 5, "session", 7) == 0) {
                    g_peer_chan = get_u32b(pl + o);
                    g_our_chan = 0;
                    if (send_channel_open_confirm(g_peer_chan) != 0) {
                        g_st = SSH_ST_ERROR;
                        return -1;
                    }
                }
            } else if (pl[0] == SSH_MSG_CHANNEL_REQUEST && n >= 10u) {
                uint32_t chan = get_u32b(pl + 1);
                uint32_t slen = get_u32b(pl + 5);
                uint32_t o = 9u + slen;
                uint8_t want = (o < n) ? pl[o] : 0;
                int is_exec = (slen == 4u && memcmp(pl + 9, "exec", 4) == 0);
                int is_shell = (slen == 5u && memcmp(pl + 9, "shell", 5) == 0);
                int is_pty = (slen == 7u && memcmp(pl + 9, "pty-req", 7) == 0);
                int is_env = (slen == 3u && memcmp(pl + 9, "env", 3) == 0);
                if (want) {
                    if (is_exec || is_shell || is_pty || is_env) {
                        if (send_channel_success(chan) != 0) {
                            g_st = SSH_ST_ERROR;
                            return -1;
                        }
                    } else if (send_channel_failure(chan) != 0) {
                        g_st = SSH_ST_ERROR;
                        return -1;
                    }
                }
                if (is_exec || is_shell) {
                    if (send_exit_and_close(chan) != 0) {
                        g_st = SSH_ST_ERROR;
                        return -1;
                    }
                    g_st = SSH_ST_DONE;
                    g_served = 1;
                    return 1;
                }
            } else if (pl[0] == SSH_MSG_CHANNEL_CLOSE) {
                g_st = SSH_ST_DONE;
                g_served = 1;
                return 1;
            }
        }
    }
    if (g_st == SSH_ST_DONE || g_st == SSH_ST_ERROR) {
        if (g_conn_h != PM_METAL_NET_IP_SOCK_INVALID) {
            pm_metal_net_ip_close(g_conn_h);
            g_conn_h = PM_METAL_NET_IP_SOCK_INVALID;
            pm_metal_net_ssh_pkt_bind_sock(PM_METAL_NET_IP_SOCK_INVALID);
        }
        return g_st == SSH_ST_DONE ? 1 : -1;
    }
    return 0;
}

int32_t pm_metal_net_ssh_served(void)
{
    return g_served;
}

int32_t pm_metal_net_ssh_status(uint8_t *buf, uint32_t buf_len)
{
    static const char msg[] = "ssh: diy-chacha20-poly1305+auth";
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
    static const char lab[] = "ssh-ed25519";
    uint32_t i;

    if (buf == NULL || buf_len == 0u) {
        return -1;
    }
    for (i = 0; i + 1u < buf_len && lab[i] != '\0'; i++) {
        buf[i] = (uint8_t)lab[i];
    }
    buf[i] = 0;
    return 0;
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
    return (g_st >= SSH_ST_IDENT_SENT && g_st != SSH_ST_ERROR) ? 1 : 0;
}

void pm_metal_net_ssh_banner_reset(void)
{
    if (g_conn_h != PM_METAL_NET_IP_SOCK_INVALID) {
        pm_metal_net_ip_close(g_conn_h);
        g_conn_h = PM_METAL_NET_IP_SOCK_INVALID;
    }
    g_st = SSH_ST_IDLE;
    g_peer_len = 0;
    g_served = 0;
    memset(g_peer_ident, 0, sizeof(g_peer_ident));
    pm_metal_net_ssh_kex_reset(&g_kex);
    g_kex.v_s_len = (uint32_t)(sizeof(k_ident_bare) - 1u);
    memcpy(g_kex.v_s, k_ident_bare, g_kex.v_s_len);
    pm_metal_net_ssh_pkt_reset();
    pm_metal_net_ssh_pkt_bind_sock(PM_METAL_NET_IP_SOCK_INVALID);
}

int32_t pm_metal_net_ssh_bind_reg(void)
{
    return 0;
}
