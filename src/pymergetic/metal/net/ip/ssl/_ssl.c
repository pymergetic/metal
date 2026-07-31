/* Metal SSL sessions over bio callbacks (mbedTLS). */
#include <stdint.h>
#include <string.h>

#include "mbedtls_metal_config.h"
#include <mbedtls/build_info.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#define SSL_SESS_MAX  16u
#define SSL_CREDS_MAX 4u
#define SSL_SNI_MAX   128u
#define SSL_CA_MAX    (32u * 1024u)

typedef int32_t (*ssl_bio_send_fn)(void *ctx, const uint8_t *buf, size_t len);
typedef int32_t (*ssl_bio_recv_fn)(void *ctx, uint8_t *buf, size_t len);

typedef struct {
  int32_t valid;
  int32_t loaded;
  uint32_t refs;
  mbedtls_x509_crt cert;
  mbedtls_pk_context key;
  mbedtls_x509_crt client_ca;
  int32_t client_auth;
} ssl_creds_t;

typedef struct {
  int32_t valid;
  int32_t server;
  void *bio_ctx;
  ssl_bio_send_fn bio_send;
  ssl_bio_recv_fn bio_recv;
  char sni[SSL_SNI_MAX];
  uint32_t creds_h;
  int32_t insecure;
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config conf;
  mbedtls_x509_crt ca;
  int32_t ca_inited;
} ssl_sess_t;

static ssl_sess_t g_sess[SSL_SESS_MAX + 1u];
static ssl_creds_t g_creds[SSL_CREDS_MAX + 1u];
static int32_t g_global;
static mbedtls_entropy_context g_entropy;
static mbedtls_ctr_drbg_context g_drbg;
static uint8_t g_ca_pem[SSL_CA_MAX];
static uint32_t g_ca_pem_len;

static int32_t entropy_poll(void *ctx, unsigned char *out, size_t len, size_t *olen)
{
  size_t i;
#if defined(__x86_64__) || defined(_M_X64)
  (void)ctx;
  for (i = 0; i < len;) {
    unsigned long long v = 0;
    unsigned char ok = 0;
    __asm__ volatile("rdrand %0; setc %1" : "=r"(v), "=qm"(ok) : : "cc");
    if (!ok) {
      return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }
    while (i < len && v != 0) {
      out[i++] = (unsigned char)(v & 0xffu);
      v >>= 8;
    }
    if (i < len && v == 0) {
      /* still need bytes; loop for another rdrand */
    }
  }
  *olen = len;
  return 0;
#else
  extern uint64_t pm_metal_time_mono_us(void);
  uint64_t t;
  (void)ctx;
  t = pm_metal_time_mono_us();
  for (i = 0; i < len; i++) {
    t = t * 6364136223846793005ull + 1ull;
    out[i] = (unsigned char)(t >> 33);
  }
  *olen = len;
  return 0;
#endif
}

static int32_t global_init(void)
{
  int32_t e;

  if (g_global) {
    return 0;
  }
  pm_metal_net_ip_ssl_mbedtls_runtime_init();
  mbedtls_entropy_init(&g_entropy);
  mbedtls_ctr_drbg_init(&g_drbg);
  e = mbedtls_entropy_add_source(
      &g_entropy, entropy_poll, NULL, 32, MBEDTLS_ENTROPY_SOURCE_STRONG);
  if (e != 0) {
    return -1;
  }
  e = mbedtls_ctr_drbg_seed(
      &g_drbg, mbedtls_entropy_func, &g_entropy, (const unsigned char *)"metal-ssl", 9);
  if (e != 0) {
    return -1;
  }
  g_global = 1;
  return 0;
}

static int32_t bio_send(void *ctx, const unsigned char *buf, size_t len)
{
  ssl_sess_t *s = (ssl_sess_t *)ctx;
  int32_t n;

  if (s == NULL || s->bio_send == NULL || buf == NULL) {
    return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
  }
  n = s->bio_send(s->bio_ctx, buf, len);
  if (n == MBEDTLS_ERR_SSL_WANT_WRITE || n == MBEDTLS_ERR_SSL_WANT_READ) {
    return n;
  }
  if (n < 0) {
    return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
  }
  if ((size_t)n < len) {
    return MBEDTLS_ERR_SSL_WANT_WRITE;
  }
  return n;
}

static int32_t bio_recv(void *ctx, unsigned char *buf, size_t len)
{
  ssl_sess_t *s = (ssl_sess_t *)ctx;
  int32_t n;

  if (s == NULL || s->bio_recv == NULL || buf == NULL || len == 0) {
    return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
  }
  n = s->bio_recv(s->bio_ctx, buf, len);
  if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
    return n;
  }
  if (n < 0) {
    return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
  }
  return n;
}

static ssl_sess_t *sess_from(uint32_t h)
{
  if (h == 0 || h > SSL_SESS_MAX) {
    return NULL;
  }
  if (!g_sess[h].valid) {
    return NULL;
  }
  return &g_sess[h];
}

static ssl_creds_t *creds_from(uint32_t h)
{
  if (h == 0 || h > SSL_CREDS_MAX) {
    return NULL;
  }
  if (!g_creds[h].valid) {
    return NULL;
  }
  return &g_creds[h];
}

static uint32_t sess_alloc(void)
{
  uint32_t i;
  for (i = 1; i <= SSL_SESS_MAX; i++) {
    if (!g_sess[i].valid) {
      memset(&g_sess[i], 0, sizeof(g_sess[i]));
      g_sess[i].valid = 1;
      return i;
    }
  }
  return 0;
}

int32_t ssl_metal_set_ca(const uint8_t *pem, uint32_t len)
{
  if (pem == NULL || len == 0 || len > SSL_CA_MAX) {
    return -1;
  }
  memcpy(g_ca_pem, pem, len);
  g_ca_pem_len = len;
  return 0;
}

uint32_t ssl_metal_creds_open(void)
{
  uint32_t i;
  if (global_init() != 0) {
    return 0;
  }
  for (i = 1; i <= SSL_CREDS_MAX; i++) {
    if (!g_creds[i].valid) {
      memset(&g_creds[i], 0, sizeof(g_creds[i]));
      g_creds[i].valid = 1;
      mbedtls_x509_crt_init(&g_creds[i].cert);
      mbedtls_pk_init(&g_creds[i].key);
      mbedtls_x509_crt_init(&g_creds[i].client_ca);
      return i;
    }
  }
  return 0;
}

static size_t pem_len(const void *buf, uint32_t len)
{
  const unsigned char *p = (const unsigned char *)buf;
  if (len == 0) {
    return 0;
  }
  /* PEM parse requires trailing NUL in the length. */
  if (p[len - 1u] == 0) {
    return len;
  }
  return (size_t)len + 1u;
}

int32_t ssl_metal_creds_load_buffers(uint32_t h, const void *cert, uint32_t cert_len,
                                     const void *key, uint32_t key_len, const void *client_ca,
                                     uint32_t client_ca_len, int32_t client_auth)
{
  ssl_creds_t *c = creds_from(h);
  int32_t e;

  if (c == NULL || cert == NULL || key == NULL || cert_len == 0 || key_len == 0) {
    return -1;
  }
  e = mbedtls_x509_crt_parse(&c->cert, cert, pem_len(cert, cert_len));
  if (e != 0) {
    return -1;
  }
  e = mbedtls_pk_parse_key(
      &c->key, key, pem_len(key, key_len), NULL, 0, mbedtls_ctr_drbg_random, &g_drbg);
  if (e != 0) {
    return -1;
  }
  if (client_ca != NULL && client_ca_len > 0) {
    e = mbedtls_x509_crt_parse(&c->client_ca, client_ca, pem_len(client_ca, client_ca_len));
    if (e != 0) {
      return -1;
    }
  }
  c->client_auth = client_auth;
  c->loaded = 1;
  return 0;
}

int32_t ssl_metal_creds_close(uint32_t h)
{
  ssl_creds_t *c = creds_from(h);
  if (c == NULL) {
    return -1;
  }
  if (c->refs != 0) {
    return -1;
  }
  mbedtls_x509_crt_free(&c->cert);
  mbedtls_pk_free(&c->key);
  mbedtls_x509_crt_free(&c->client_ca);
  memset(c, 0, sizeof(*c));
  return 0;
}

static int32_t sess_setup_client(ssl_sess_t *s)
{
  int32_t e;

  mbedtls_ssl_init(&s->ssl);
  mbedtls_ssl_config_init(&s->conf);
  e = mbedtls_ssl_config_defaults(
      &s->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
  if (e != 0) {
    return -1;
  }
  if (s->insecure) {
    mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_NONE);
  } else {
    mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    if (g_ca_pem_len > 0) {
      mbedtls_x509_crt_init(&s->ca);
      s->ca_inited = 1;
      e = mbedtls_x509_crt_parse(&s->ca, g_ca_pem, g_ca_pem_len);
      if (e != 0) {
        e = mbedtls_x509_crt_parse(&s->ca, g_ca_pem, g_ca_pem_len + 1);
      }
      if (e != 0) {
        return -1;
      }
      mbedtls_ssl_conf_ca_chain(&s->conf, &s->ca, NULL);
    } else {
      /* No CA loaded: fail closed unless insecure. */
      return -1;
    }
  }
  mbedtls_ssl_conf_rng(&s->conf, mbedtls_ctr_drbg_random, &g_drbg);
  e = mbedtls_ssl_setup(&s->ssl, &s->conf);
  if (e != 0) {
    return -1;
  }
  if (s->sni[0] != 0) {
    e = mbedtls_ssl_set_hostname(&s->ssl, s->sni);
    if (e != 0) {
      return -1;
    }
  }
  mbedtls_ssl_set_bio(&s->ssl, s, bio_send, bio_recv, NULL);
  return 0;
}

static int32_t sess_setup_server(ssl_sess_t *s)
{
  ssl_creds_t *c = creds_from(s->creds_h);
  int32_t e;

  if (c == NULL || !c->loaded) {
    return -1;
  }
  mbedtls_ssl_init(&s->ssl);
  mbedtls_ssl_config_init(&s->conf);
  e = mbedtls_ssl_config_defaults(
      &s->conf, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
  if (e != 0) {
    return -1;
  }
  mbedtls_ssl_conf_rng(&s->conf, mbedtls_ctr_drbg_random, &g_drbg);
  e = mbedtls_ssl_conf_own_cert(&s->conf, &c->cert, &c->key);
  if (e != 0) {
    return -1;
  }
  if (c->client_auth != 0) {
    mbedtls_ssl_conf_authmode(
        &s->conf, c->client_auth == 2 ? MBEDTLS_SSL_VERIFY_REQUIRED : MBEDTLS_SSL_VERIFY_OPTIONAL);
    mbedtls_ssl_conf_ca_chain(&s->conf, &c->client_ca, NULL);
  } else {
    mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_NONE);
  }
  e = mbedtls_ssl_setup(&s->ssl, &s->conf);
  if (e != 0) {
    return -1;
  }
  mbedtls_ssl_set_bio(&s->ssl, s, bio_send, bio_recv, NULL);
  c->refs++;
  return 0;
}

uint32_t ssl_metal_wrap_client(void *bio_ctx, ssl_bio_send_fn send, ssl_bio_recv_fn recv,
                               const char *sni, int32_t insecure)
{
  uint32_t h;
  ssl_sess_t *s;

  if (global_init() != 0 || send == NULL || recv == NULL) {
    return 0;
  }
  h = sess_alloc();
  if (h == 0) {
    return 0;
  }
  s = &g_sess[h];
  s->server = 0;
  s->bio_ctx = bio_ctx;
  s->bio_send = send;
  s->bio_recv = recv;
  s->insecure = insecure;
  if (sni != NULL) {
    strncpy(s->sni, sni, SSL_SNI_MAX - 1u);
  }
  if (sess_setup_client(s) != 0) {
    mbedtls_ssl_free(&s->ssl);
    mbedtls_ssl_config_free(&s->conf);
    if (s->ca_inited) {
      mbedtls_x509_crt_free(&s->ca);
    }
    memset(s, 0, sizeof(*s));
    return 0;
  }
  return h;
}

uint32_t ssl_metal_wrap_server(void *bio_ctx, ssl_bio_send_fn send, ssl_bio_recv_fn recv,
                               uint32_t creds_h)
{
  uint32_t h;
  ssl_sess_t *s;

  if (global_init() != 0 || send == NULL || recv == NULL || creds_h == 0) {
    return 0;
  }
  h = sess_alloc();
  if (h == 0) {
    return 0;
  }
  s = &g_sess[h];
  s->server = 1;
  s->bio_ctx = bio_ctx;
  s->bio_send = send;
  s->bio_recv = recv;
  s->creds_h = creds_h;
  if (sess_setup_server(s) != 0) {
    mbedtls_ssl_free(&s->ssl);
    mbedtls_ssl_config_free(&s->conf);
    memset(s, 0, sizeof(*s));
    return 0;
  }
  return h;
}

void ssl_metal_close(uint32_t h)
{
  ssl_sess_t *s = sess_from(h);
  ssl_creds_t *c;

  if (s == NULL) {
    return;
  }
  mbedtls_ssl_free(&s->ssl);
  mbedtls_ssl_config_free(&s->conf);
  if (s->ca_inited) {
    mbedtls_x509_crt_free(&s->ca);
  }
  if (s->server && s->creds_h != 0) {
    c = creds_from(s->creds_h);
    if (c != NULL && c->refs > 0) {
      c->refs--;
    }
  }
  memset(s, 0, sizeof(*s));
}

int32_t ssl_metal_handshake_step(uint32_t h)
{
  ssl_sess_t *s = sess_from(h);
  int32_t e;

  if (s == NULL) {
    return -1;
  }
  e = mbedtls_ssl_handshake(&s->ssl);
  if (e == 0) {
    return 0;
  }
  if (e == MBEDTLS_ERR_SSL_WANT_READ || e == MBEDTLS_ERR_SSL_WANT_WRITE) {
    return 1;
  }
  return -1;
}

int32_t ssl_metal_read(uint32_t h, void *buf, uint32_t cap)
{
  ssl_sess_t *s = sess_from(h);
  int32_t e;

  if (s == NULL || buf == NULL || cap == 0) {
    return -1;
  }
  e = mbedtls_ssl_read(&s->ssl, buf, cap);
  if (e == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
    return 0;
  }
  return e;
}

int32_t ssl_metal_write(uint32_t h, const void *buf, uint32_t len)
{
  ssl_sess_t *s = sess_from(h);

  if (s == NULL || buf == NULL || len == 0) {
    return -1;
  }
  return mbedtls_ssl_write(&s->ssl, buf, len);
}
