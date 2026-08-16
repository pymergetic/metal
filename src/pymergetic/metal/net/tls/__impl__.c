/* pymergetic.metal.net.tls — mbedtls records on net.ip socks.
 * Same lib/mbedtls as µPy unix (MICROPY_SSL_MBEDTLS). Not modtls_mbedtls. */
#include "pymergetic/metal/net/tls/__exports__.h"

#include "pymergetic/metal/net/ip.h"
#include "pymergetic/util/mem.h"

#include <string.h>

#if defined(MICROPY_SSL_MBEDTLS) && MICROPY_SSL_MBEDTLS

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/pk.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

struct pm_metal_net_tls_session {
    int32_t fd;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cert;
    mbedtls_pk_context pkey;
};

static pm_util_mem_arena_t *s_arena;
static mbedtls_entropy_context s_entropy;
static mbedtls_ctr_drbg_context s_drbg;
static uint32_t s_ready;

static int bio_send(void *ctx, const unsigned char *buf, size_t len) {
    pm_metal_net_tls_session_t *s = (pm_metal_net_tls_session_t *)ctx;
    if (len > 0xffffffffu) {
        len = 0xffffffffu;
    }
    int32_t n = pm_metal_net_ip_send(s->fd, buf, (uint32_t)len);
    if (n == 0) {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    if (n < 0) {
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    return (int)n;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len) {
    pm_metal_net_tls_session_t *s = (pm_metal_net_tls_session_t *)ctx;
    if (len > 0xffffffffu) {
        len = 0xffffffffu;
    }
    int32_t n = pm_metal_net_ip_recv(s->fd, buf, (uint32_t)len);
    if (n == 0) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    if (n == -2) {
        return 0;
    }
    if (n < 0) {
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    return (int)n;
}

static pm_metal_net_tls_session_t *sess_new(int32_t fd, int endpoint, const char *sni,
    const uint8_t *cert, size_t cert_len, const uint8_t *key, size_t key_len) {
    if (!s_ready || fd < 0) {
        return NULL;
    }
    pm_metal_net_tls_session_t *s =
        (pm_metal_net_tls_session_t *)pm_util_mem_alloc(s_arena, sizeof(*s));
    if (s == NULL) {
        return NULL;
    }
    memset(s, 0, sizeof(*s));
    s->fd = fd;
    mbedtls_ssl_init(&s->ssl);
    mbedtls_ssl_config_init(&s->conf);
    mbedtls_x509_crt_init(&s->cert);
    mbedtls_pk_init(&s->pkey);
    if (mbedtls_ssl_config_defaults(&s->conf, endpoint, MBEDTLS_SSL_TRANSPORT_STREAM,
            MBEDTLS_SSL_PRESET_DEFAULT)
        != 0) {
        pm_metal_net_tls_close(s);
        return NULL;
    }
    mbedtls_ssl_conf_rng(&s->conf, mbedtls_ctr_drbg_random, &s_drbg);
    mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_NONE);
    if (endpoint == MBEDTLS_SSL_IS_SERVER) {
        int pret = -1;
        if (cert == NULL || key == NULL || cert_len == 0 || key_len == 0
            || (pret = mbedtls_x509_crt_parse(&s->cert, cert, cert_len)) != 0
            || (pret = mbedtls_pk_parse_key(&s->pkey, key, key_len, NULL, 0,
                    mbedtls_ctr_drbg_random, &s_drbg))
                != 0
            || (pret = mbedtls_ssl_conf_own_cert(&s->conf, &s->cert, &s->pkey)) != 0) {
            (void)pret;
            pm_metal_net_tls_close(s);
            return NULL;
        }
    }
    if (mbedtls_ssl_setup(&s->ssl, &s->conf) != 0) {
        pm_metal_net_tls_close(s);
        return NULL;
    }
    mbedtls_ssl_set_bio(&s->ssl, s, bio_send, bio_recv, NULL);
    if (endpoint == MBEDTLS_SSL_IS_CLIENT && sni != NULL && sni[0] != 0) {
        if (mbedtls_ssl_set_hostname(&s->ssl, sni) != 0) {
            pm_metal_net_tls_close(s);
            return NULL;
        }
    }
    return s;
}

int32_t pm_metal_net_tls_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    mbedtls_entropy_init(&s_entropy);
    mbedtls_ctr_drbg_init(&s_drbg);
    const char pers[] = "pm_metal_net_tls";
    if (mbedtls_ctr_drbg_seed(&s_drbg, mbedtls_entropy_func, &s_entropy,
            (const unsigned char *)pers, sizeof(pers) - 1u)
        != 0) {
        mbedtls_ctr_drbg_free(&s_drbg);
        mbedtls_entropy_free(&s_entropy);
        s_arena = NULL;
        return -1;
    }
    s_ready = 1;
    return 0;
}

void pm_metal_net_tls_deinit(void) {
    if (s_ready) {
        mbedtls_ctr_drbg_free(&s_drbg);
        mbedtls_entropy_free(&s_entropy);
    }
    s_ready = 0;
    s_arena = NULL;
}

int32_t pm_metal_net_tls_ready(void) {
    return s_ready ? 1 : 0;
}

pm_metal_net_tls_session_t *pm_metal_net_tls_client(int32_t fd, const char *sni) {
    return sess_new(fd, MBEDTLS_SSL_IS_CLIENT, sni, NULL, 0, NULL, 0);
}

pm_metal_net_tls_session_t *pm_metal_net_tls_server(int32_t fd, const uint8_t *cert, size_t cert_len,
    const uint8_t *key, size_t key_len) {
    return sess_new(fd, MBEDTLS_SSL_IS_SERVER, NULL, cert, cert_len, key, key_len);
}

int32_t pm_metal_net_tls_handshake(pm_metal_net_tls_session_t *s) {
    if (s == NULL) {
        return -1;
    }
    int ret = mbedtls_ssl_handshake(&s->ssl);
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        return 1;
    }
    if (ret == 0) {
        return 0;
    }
    return -1;
}

int32_t pm_metal_net_tls_send(pm_metal_net_tls_session_t *s, const uint8_t *buf, uint32_t len) {
    if (s == NULL || buf == NULL) {
        return -1;
    }
    int ret = mbedtls_ssl_write(&s->ssl, buf, len);
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        return 0;
    }
    if (ret < 0) {
        return -1;
    }
    return (int32_t)ret;
}

int32_t pm_metal_net_tls_recv(pm_metal_net_tls_session_t *s, uint8_t *buf, uint32_t len) {
    if (s == NULL || buf == NULL) {
        return -1;
    }
    int ret = mbedtls_ssl_read(&s->ssl, buf, len);
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        return 0;
    }
    if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
        return -2;
    }
    if (ret < 0) {
        return -1;
    }
    return (int32_t)ret;
}

void pm_metal_net_tls_close(pm_metal_net_tls_session_t *s) {
    if (s == NULL) {
        return;
    }
    mbedtls_ssl_free(&s->ssl);
    mbedtls_ssl_config_free(&s->conf);
    mbedtls_x509_crt_free(&s->cert);
    mbedtls_pk_free(&s->pkey);
    pm_util_mem_free(s_arena, s);
}

#else /* !MICROPY_SSL_MBEDTLS */

int32_t pm_metal_net_tls_init(pm_util_mem_arena_t *arena) {
    (void)arena;
    return 0;
}

void pm_metal_net_tls_deinit(void) {
}

int32_t pm_metal_net_tls_ready(void) {
    return 0;
}

pm_metal_net_tls_session_t *pm_metal_net_tls_client(int32_t fd, const char *sni) {
    (void)fd;
    (void)sni;
    return NULL;
}

pm_metal_net_tls_session_t *pm_metal_net_tls_server(int32_t fd, const uint8_t *cert, size_t cert_len,
    const uint8_t *key, size_t key_len) {
    (void)fd;
    (void)cert;
    (void)cert_len;
    (void)key;
    (void)key_len;
    return NULL;
}

int32_t pm_metal_net_tls_handshake(pm_metal_net_tls_session_t *s) {
    (void)s;
    return -1;
}

int32_t pm_metal_net_tls_send(pm_metal_net_tls_session_t *s, const uint8_t *buf, uint32_t len) {
    (void)s;
    (void)buf;
    (void)len;
    return -1;
}

int32_t pm_metal_net_tls_recv(pm_metal_net_tls_session_t *s, uint8_t *buf, uint32_t len) {
    (void)s;
    (void)buf;
    (void)len;
    return -1;
}

void pm_metal_net_tls_close(pm_metal_net_tls_session_t *s) {
    (void)s;
}

#endif /* MICROPY_SSL_MBEDTLS */

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.net.tls, pm_metal_net_tls_init, pm_metal_net_tls_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.tls, pm_metal_net_tls_deinit, pm_metal_net_tls_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.tls, pm_metal_net_tls_ready, pm_metal_net_tls_ready, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.tls, pm_metal_net_tls_client, pm_metal_net_tls_client, pm_metal_net_tls_session_t *(int32_t, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.net.tls, pm_metal_net_tls_server, pm_metal_net_tls_server, pm_metal_net_tls_session_t *(int32_t, const uint8_t *, size_t, const uint8_t *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.tls, pm_metal_net_tls_handshake, pm_metal_net_tls_handshake, int32_t(pm_metal_net_tls_session_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.tls, pm_metal_net_tls_send, pm_metal_net_tls_send, int32_t(pm_metal_net_tls_session_t *, const uint8_t *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.tls, pm_metal_net_tls_recv, pm_metal_net_tls_recv, int32_t(pm_metal_net_tls_session_t *, uint8_t *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.tls, pm_metal_net_tls_close, pm_metal_net_tls_close, void(pm_metal_net_tls_session_t *));

PM_MOD_BOOT_C(pymergetic.metal.net.tls, pm_metal_net_tls_init, pm_metal_net_tls_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.net.tls, pymergetic.metal.net.ip);
