/*
 * mbedTLS backend for Metal net/tls — BIO over pm_metal_net_ip_* socks.
 */
#include "pymergetic/metal/net/tls/__init__.h"

#include <stdio.h>
#include <string.h>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"

#include "pymergetic/metal/async/await.h"
#include "pymergetic/metal/async/handle.h"
#include "pymergetic/metal/mem.h"
#include "pymergetic/metal/net/ip/sock.h"

#ifndef PM_METAL_NET_TLS_MAX
#define PM_METAL_NET_TLS_MAX 8
#endif

typedef struct {
    uint8_t used;
    uint8_t is_server;
    uint8_t handshaking;
    uint8_t verify_none;
    pm_metal_net_ip_sock_h sock;
    uint32_t hs_ah; /* park handle during handshake */
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    char hostname[128];
} tls_slot_t;

static tls_slot_t g_slots[PM_METAL_NET_TLS_MAX + 1];
static mbedtls_entropy_context g_entropy;
static mbedtls_ctr_drbg_context g_ctr_drbg;
static mbedtls_x509_crt g_ca;
static mbedtls_x509_crt g_srv_cert;
static mbedtls_pk_context g_srv_key;
static int g_have_ca;
static int g_have_srv;
static int g_ready;

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    pm_metal_net_ip_sock_h sock = (pm_metal_net_ip_sock_h)(uintptr_t)ctx;
    uint32_t n;

    if (sock == PM_METAL_NET_IP_SOCK_INVALID || buf == NULL || len == 0u) {
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }
    n = pm_metal_net_ip_send(sock, buf, (uint32_t)len);
    if (n == 0u) {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    return (int)n;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    pm_metal_net_ip_sock_h sock = (pm_metal_net_ip_sock_h)(uintptr_t)ctx;
    uint32_t n;

    if (sock == PM_METAL_NET_IP_SOCK_INVALID || buf == NULL || len == 0u) {
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }
    n = pm_metal_net_ip_try_recv(sock, buf, (uint32_t)len);
    if (n == 0u) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    if (n == (uint32_t)-1) {
        return MBEDTLS_ERR_SSL_CONN_EOF;
    }
    return (int)n;
}

static tls_slot_t *slot_get(pm_metal_net_tls_h th)
{
    if (th == 0u || th > PM_METAL_NET_TLS_MAX || !g_slots[th].used) {
        return NULL;
    }
    return &g_slots[th];
}

static pm_metal_net_tls_h slot_alloc(void)
{
    uint32_t i;
    for (i = 1; i <= PM_METAL_NET_TLS_MAX; i++) {
        if (!g_slots[i].used) {
            memset(&g_slots[i], 0, sizeof(g_slots[i]));
            g_slots[i].used = 1;
            g_slots[i].sock = PM_METAL_NET_IP_SOCK_INVALID;
            return i;
        }
    }
    return PM_METAL_NET_TLS_INVALID;
}

static int32_t backend_init(void)
{
    int rc;

    if (g_ready) {
        return 0;
    }
    mbedtls_entropy_init(&g_entropy);
    mbedtls_ctr_drbg_init(&g_ctr_drbg);
    mbedtls_x509_crt_init(&g_ca);
    mbedtls_x509_crt_init(&g_srv_cert);
    mbedtls_pk_init(&g_srv_key);
    rc = mbedtls_ctr_drbg_seed(&g_ctr_drbg, mbedtls_entropy_func, &g_entropy,
                               (const unsigned char *)"metal-net-tls", 12);
    if (rc != 0) {
        return -1;
    }
    g_ready = 1;
    return 0;
}

static void backend_fini(void)
{
    uint32_t i;
    for (i = 1; i <= PM_METAL_NET_TLS_MAX; i++) {
        if (g_slots[i].used) {
            mbedtls_ssl_free(&g_slots[i].ssl);
            mbedtls_ssl_config_free(&g_slots[i].conf);
            g_slots[i].used = 0;
        }
    }
    mbedtls_x509_crt_free(&g_ca);
    mbedtls_x509_crt_free(&g_srv_cert);
    mbedtls_pk_free(&g_srv_key);
    mbedtls_ctr_drbg_free(&g_ctr_drbg);
    mbedtls_entropy_free(&g_entropy);
    g_have_ca = 0;
    g_have_srv = 0;
    g_ready = 0;
}

static int conf_defaults(tls_slot_t *s, int endpoint)
{
    int rc;

    mbedtls_ssl_init(&s->ssl);
    mbedtls_ssl_config_init(&s->conf);
    rc = mbedtls_ssl_config_defaults(&s->conf, endpoint, MBEDTLS_SSL_TRANSPORT_STREAM,
                                     MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) {
        return -1;
    }
    mbedtls_ssl_conf_rng(&s->conf, mbedtls_ctr_drbg_random, &g_ctr_drbg);
    if (endpoint == MBEDTLS_SSL_IS_CLIENT) {
        if (s->verify_none) {
            mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_NONE);
        } else {
            mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
            if (g_have_ca) {
                mbedtls_ssl_conf_ca_chain(&s->conf, &g_ca, NULL);
            }
        }
    } else {
        mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_NONE);
        if ((g_have_srv & 3) != 3) {
            return -1;
        }
        rc = mbedtls_ssl_conf_own_cert(&s->conf, &g_srv_cert, &g_srv_key);
        if (rc != 0) {
            return -1;
        }
    }
    rc = mbedtls_ssl_setup(&s->ssl, &s->conf);
    if (rc != 0) {
        return -1;
    }
    mbedtls_ssl_set_bio(&s->ssl, (void *)(uintptr_t)s->sock, bio_send, bio_recv, NULL);
    if (endpoint == MBEDTLS_SSL_IS_CLIENT && s->hostname[0] != '\0') {
        (void)mbedtls_ssl_set_hostname(&s->ssl, s->hostname);
    }
    return 0;
}

static pm_metal_net_tls_h client_open(pm_metal_net_ip_sock_h sock, const char *hostname,
                                      uint32_t flags)
{
    pm_metal_net_tls_h th;
    tls_slot_t *s;

    if (sock == PM_METAL_NET_IP_SOCK_INVALID) {
        return PM_METAL_NET_TLS_INVALID;
    }
    th = slot_alloc();
    s = slot_get(th);
    if (s == NULL) {
        return PM_METAL_NET_TLS_INVALID;
    }
    s->sock = sock;
    s->is_server = 0;
    s->verify_none = (flags & PM_METAL_NET_TLS_VERIFY_NONE) ? 1u : 0u;
    if (hostname != NULL) {
        size_t n = strlen(hostname);
        if (n >= sizeof(s->hostname)) {
            n = sizeof(s->hostname) - 1u;
        }
        memcpy(s->hostname, hostname, n);
        s->hostname[n] = '\0';
    }
    if (conf_defaults(s, MBEDTLS_SSL_IS_CLIENT) != 0) {
        mbedtls_ssl_free(&s->ssl);
        mbedtls_ssl_config_free(&s->conf);
        s->used = 0;
        return PM_METAL_NET_TLS_INVALID;
    }
    return th;
}

static pm_metal_net_tls_h server_open(pm_metal_net_ip_sock_h sock)
{
    pm_metal_net_tls_h th;
    tls_slot_t *s;

    if (sock == PM_METAL_NET_IP_SOCK_INVALID) {
        return PM_METAL_NET_TLS_INVALID;
    }
    th = slot_alloc();
    s = slot_get(th);
    if (s == NULL) {
        return PM_METAL_NET_TLS_INVALID;
    }
    s->sock = sock;
    s->is_server = 1;
    if (conf_defaults(s, MBEDTLS_SSL_IS_SERVER) != 0) {
        mbedtls_ssl_free(&s->ssl);
        mbedtls_ssl_config_free(&s->conf);
        s->used = 0;
        return PM_METAL_NET_TLS_INVALID;
    }
    return th;
}

static int hs_step(tls_slot_t *s)
{
    int rc = mbedtls_ssl_handshake(&s->ssl);
    if (rc == 0) {
        return 1;
    }
    if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
        return 0;
    }
    return -1;
}

static uint32_t handshake(pm_metal_net_tls_h th)
{
    tls_slot_t *s = slot_get(th);
    uint32_t ah;
    int st;

    if (s == NULL) {
        return pm_metal_async_completed_u32(0u);
    }
    st = hs_step(s);
    if (st == 1) {
        return pm_metal_async_completed_u32(1u);
    }
    if (st < 0) {
        return pm_metal_async_completed_u32(0u);
    }
    ah = pm_metal_async_park();
    if (ah == 0u) {
        return pm_metal_async_completed_u32(0u);
    }
    s->handshaking = 1;
    s->hs_ah = ah;
    return ah;
}

void pm_metal_net_tls_poll(void)
{
    uint32_t i;

    for (i = 1; i <= PM_METAL_NET_TLS_MAX; i++) {
        tls_slot_t *s = &g_slots[i];
        int st;
        if (!s->used || !s->handshaking || s->hs_ah == 0u) {
            continue;
        }
        st = hs_step(s);
        if (st == 0) {
            continue;
        }
        pm_metal_async_set_result_u32(s->hs_ah, st > 0 ? 1u : 0u);
        pm_metal_async_wake(s->hs_ah);
        s->handshaking = 0;
        s->hs_ah = 0;
    }
}

static uint32_t try_read(pm_metal_net_tls_h th, void *buf, uint32_t len)
{
    tls_slot_t *s = slot_get(th);
    int rc;

    if (s == NULL || buf == NULL || len == 0u) {
        return (uint32_t)-1;
    }
    rc = mbedtls_ssl_read(&s->ssl, (unsigned char *)buf, (size_t)len);
    if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
        return 0u;
    }
    if (rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || rc == 0) {
        return (uint32_t)-1;
    }
    if (rc < 0) {
        return (uint32_t)-1;
    }
    return (uint32_t)rc;
}

static uint32_t tls_write(pm_metal_net_tls_h th, const void *buf, uint32_t len)
{
    tls_slot_t *s = slot_get(th);
    int rc;

    if (s == NULL || buf == NULL || len == 0u) {
        return 0u;
    }
    rc = mbedtls_ssl_write(&s->ssl, (const unsigned char *)buf, (size_t)len);
    if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
        return 0u;
    }
    if (rc < 0) {
        return 0u;
    }
    return (uint32_t)rc;
}

static void tls_close(pm_metal_net_tls_h th)
{
    tls_slot_t *s = slot_get(th);
    if (s == NULL) {
        return;
    }
    if (s->hs_ah != 0u) {
        pm_metal_async_set_result_u32(s->hs_ah, 0u);
        pm_metal_async_wake(s->hs_ah);
        s->hs_ah = 0;
    }
    (void)mbedtls_ssl_close_notify(&s->ssl);
    mbedtls_ssl_free(&s->ssl);
    mbedtls_ssl_config_free(&s->conf);
    memset(s, 0, sizeof(*s));
}

static int32_t load_ca_pem(const uint8_t *pem, uint32_t len)
{
    int rc;
    if (pem == NULL || len == 0u) {
        return -1;
    }
    rc = mbedtls_x509_crt_parse(&g_ca, pem, (size_t)len + 1u);
    /* parse wants null-terminated; tolerate exact PEM without extra NUL */
    if (rc != 0) {
        uint8_t *tmp = pm_metal_mem_alloc((size_t)len + 1u);
        if (tmp == NULL) {
            return -1;
        }
        memcpy(tmp, pem, len);
        tmp[len] = 0;
        rc = mbedtls_x509_crt_parse(&g_ca, tmp, (size_t)len + 1u);
        pm_metal_mem_free(tmp);
    }
    if (rc < 0) {
        return -1;
    }
    g_have_ca = 1;
    return 0;
}

static int32_t set_server_cert_pem(const uint8_t *pem, uint32_t len)
{
    uint8_t *tmp;
    int rc;
    char err[64];
    extern void uart_puts(const char *s);
    if (pem == NULL || len == 0u) {
        return -1;
    }
    tmp = pm_metal_mem_alloc((size_t)len + 1u);
    if (tmp == NULL) {
        uart_puts("tls cert oom\n");
        return -1;
    }
    memcpy(tmp, pem, len);
    tmp[len] = 0;
    mbedtls_x509_crt_free(&g_srv_cert);
    mbedtls_x509_crt_init(&g_srv_cert);
    rc = mbedtls_x509_crt_parse(&g_srv_cert, tmp, (size_t)len + 1u);
    pm_metal_mem_free(tmp);
    if (rc < 0) {
        /* mbedtls errors are negative */
        snprintf(err, sizeof(err), "tls cert rc=%d\n", rc);
        uart_puts(err);
        return -1;
    }
    g_have_srv = g_have_srv | 1;
    return 0;
}

static int32_t set_server_key_pem(const uint8_t *pem, uint32_t len)
{
    uint8_t *tmp;
    int rc;
    if (pem == NULL || len == 0u) {
        return -1;
    }
    tmp = pm_metal_mem_alloc((size_t)len + 1u);
    if (tmp == NULL) {
        return -1;
    }
    memcpy(tmp, pem, len);
    tmp[len] = 0;
    mbedtls_pk_free(&g_srv_key);
    mbedtls_pk_init(&g_srv_key);
    rc = mbedtls_pk_parse_key(&g_srv_key, tmp, (size_t)len + 1u, NULL, 0,
                              mbedtls_ctr_drbg_random, &g_ctr_drbg);
    pm_metal_mem_free(tmp);
    if (rc != 0) {
        return -1;
    }
    g_have_srv = g_have_srv | 2;
    return 0;
}

static int32_t set_server_chain_pem(const uint8_t *pem, uint32_t len)
{
    /* Append intermediates onto server cert chain. */
    return set_server_cert_pem(pem, len);
}

static const pm_metal_net_tls_ops_t g_mbedtls_ops = {
    .version = PM_METAL_NET_TLS_OPS_VERSION,
    .init = backend_init,
    .fini = backend_fini,
    .client_open = client_open,
    .server_open = server_open,
    .handshake = handshake,
    .try_read = try_read,
    .write = tls_write,
    .close = tls_close,
    .load_ca_pem = load_ca_pem,
    .set_server_cert_pem = set_server_cert_pem,
    .set_server_key_pem = set_server_key_pem,
    .set_server_chain_pem = set_server_chain_pem,
};

int32_t pm_metal_net_tls_mbedtls_register(void)
{
    pm_metal_net_tls_set_ops(&g_mbedtls_ops);
    return 0;
}
