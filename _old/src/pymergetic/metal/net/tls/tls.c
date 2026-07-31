/** @file
  General TLS client/server (mbedTLS) over pm_metal_net_* sockets.
  (impl: efi|bios)
**/
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/net/tls/tls.h>
#include <pymergetic/metal/net/ip/ip.h>
#include <pymergetic/metal/net/ip/ip_ops.h>
#include <pymergetic/metal/net/tls/mbedtls_metal_config.h>
#include <pymergetic/metal/dev/random/random.h>
#include <pymergetic/metal/fs/fs.h>
#include <pymergetic/metal/runtime/mem/mem.h>

#include <mbedtls/build_info.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/platform.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include <stddef.h>
#include <stdint.h>

#define TLS_SESS_MAX  16u
#define TLS_CREDS_MAX 4u
#define TLS_SNI_MAX   128u

typedef struct {
  int32_t                        valid;
  int32_t                        loaded;
  uint32_t                       refs;
  pm_metal_net_tls_client_auth_t client_auth;
  mbedtls_x509_crt               cert;
  mbedtls_x509_crt               client_ca;
  mbedtls_pk_context             key;
} tls_creds_t;

typedef struct {
  int32_t                  valid;
  int32_t                  server;
  int32_t                  initialized;
  pm_metal_net_ip_sock_h   sock;
  pm_metal_net_tls_wire_t *wire;
  char                     sni[TLS_SNI_MAX];
  pm_metal_net_tls_creds_h creds_h;
  mbedtls_ssl_context      ssl;
  mbedtls_ssl_config       conf;
  int32_t                  ready;
  int32_t                  done;
} tls_sess_t;

static tls_sess_t               mTls[TLS_SESS_MAX + 1];
static tls_creds_t              mTlsCreds[TLS_CREDS_MAX + 1];
static int32_t                  mTlsGlobal;
static mbedtls_entropy_context  mEntropy;
static mbedtls_ctr_drbg_context mCtrDrbg;

static int32_t TlsEntropyPoll(void *ctx, uint8_t *out, size_t len, size_t *olen)
{
  uint32_t got;

  (void)ctx;
  if (out == NULL || olen == NULL) {
    return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
  }

  got   = pm_metal_random(out, (uint32_t)len);
  *olen = got;
  return (got == len) ? 0 : MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
}

static int32_t TlsGlobalInit(void)
{
  int32_t e;

  if (mTlsGlobal) {
    return 0;
  }

  pm_metal_net_tls_mbedtls_runtime_init();
  mbedtls_entropy_init(&mEntropy);
  mbedtls_ctr_drbg_init(&mCtrDrbg);
  e =
    mbedtls_entropy_add_source(&mEntropy, TlsEntropyPoll, NULL, 32, MBEDTLS_ENTROPY_SOURCE_STRONG);
  if (e != 0) {
    return -1;
  }

  e = mbedtls_ctr_drbg_seed(
    &mCtrDrbg, mbedtls_entropy_func, &mEntropy, (const uint8_t *)"metal-tls", 9);
  if (e != 0) {
    return -1;
  }

  mTlsGlobal = 1;
  return 0;
}

static int32_t TlsSockSend(void *ctx, const uint8_t *buf, size_t len)
{
  tls_sess_t *t;
  uint32_t    n;

  t = (tls_sess_t *)ctx;
  if (t == NULL || buf == NULL) {
    return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
  }

  n = pm_metal_net_ip_send(t->sock, buf, (uint32_t)len);
  if (n == 0) {
    return MBEDTLS_ERR_SSL_WANT_WRITE;
  }

  if (n < len) {
    return MBEDTLS_ERR_SSL_WANT_WRITE;
  }

  return (int32_t)len;
}

static int32_t TlsSockRecv(void *ctx, uint8_t *buf, size_t len)
{
  tls_sess_t *t;
  size_t      n;

  t = (tls_sess_t *)ctx;
  if (t == NULL || buf == NULL || len == 0 || t->wire == NULL) {
    return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
  }

  if (t->wire->off < t->wire->len) {
    n = t->wire->len - t->wire->off;
    if (n > len) {
      n = len;
    }

    memcpy(buf, t->wire->buf + t->wire->off, n);
    t->wire->off += (uint32_t)n;
    return (int32_t)n;
  }

  return MBEDTLS_ERR_SSL_WANT_READ;
}

static tls_sess_t *TlsFromHandle(pm_metal_net_tls_h h)
{
  if (h == 0 || h > TLS_SESS_MAX) {
    return NULL;
  }

  if (!mTls[h].valid) {
    return NULL;
  }

  return &mTls[h];
}

static tls_creds_t *TlsCredsFromHandle(pm_metal_net_tls_creds_h h)
{
  if (h == PM_METAL_TLS_CREDS_INVALID || h > TLS_CREDS_MAX || !mTlsCreds[h].valid) {
    return NULL;
  }

  return &mTlsCreds[h];
}

static int32_t TlsIsPem(const uint8_t *data, uint32_t len)
{
  static const char prefix[] = "-----BEGIN";

  return data != NULL && len >= sizeof(prefix) - 1u &&
         memcmp(data, prefix, sizeof(prefix) - 1u) == 0;
}

static int32_t TlsParseCert(mbedtls_x509_crt *crt, const uint8_t *data, uint32_t len)
{
  uint8_t *copy;
  int32_t  e;

  if (crt == NULL || data == NULL || len == 0) {
    return -1;
  }

  if (!TlsIsPem(data, len)) {
    return mbedtls_x509_crt_parse_der(crt, data, len) == 0 ? 0 : -1;
  }

  copy = (uint8_t *)pm_metal_mem_alloc((size_t)len + 1u, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (copy == NULL) {
    return -1;
  }

  memcpy(copy, data, len);
  copy[len] = '\0';
  e         = mbedtls_x509_crt_parse(crt, copy, (size_t)len + 1u);
  pm_metal_mem_free(copy);
  return e == 0 ? 0 : -1;
}

static int32_t TlsParseKey(mbedtls_pk_context *key, const uint8_t *data, uint32_t len)
{
  uint8_t *copy;
  int32_t  e;

  if (key == NULL || data == NULL || len == 0) {
    return -1;
  }

  if (!TlsIsPem(data, len)) {
    return mbedtls_pk_parse_key(key, data, len, NULL, 0, mbedtls_ctr_drbg_random, &mCtrDrbg) == 0
             ? 0
             : -1;
  }

  copy = (uint8_t *)pm_metal_mem_alloc((size_t)len + 1u, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (copy == NULL) {
    return -1;
  }

  memcpy(copy, data, len);
  copy[len] = '\0';
  e =
    mbedtls_pk_parse_key(key, copy, (size_t)len + 1u, NULL, 0, mbedtls_ctr_drbg_random, &mCtrDrbg);
  pm_metal_mem_free(copy);
  return e == 0 ? 0 : -1;
}

static void TlsCredsReset(tls_creds_t *creds)
{
  if (creds == NULL) {
    return;
  }

  mbedtls_x509_crt_free(&creds->cert);
  mbedtls_x509_crt_free(&creds->client_ca);
  mbedtls_pk_free(&creds->key);
  mbedtls_x509_crt_init(&creds->cert);
  mbedtls_x509_crt_init(&creds->client_ca);
  mbedtls_pk_init(&creds->key);
  creds->loaded      = 0;
  creds->client_auth = PM_METAL_TLS_CLIENT_AUTH_NONE;
}

void pm_metal_net_tls_wire_reset(pm_metal_net_tls_wire_t *wire)
{
  if (wire != NULL) {
    wire->len = 0;
    wire->off = 0;
  }
}

void pm_metal_net_tls_wire_feed(pm_metal_net_tls_wire_t *wire, const void *data, uint32_t len)
{
  if (wire == NULL || data == NULL || len == 0) {
    return;
  }

  if (len > PM_METAL_TLS_WIRE_MAX) {
    len = PM_METAL_TLS_WIRE_MAX;
  }

  if (data != wire->buf) {
    memcpy(wire->buf, data, len);
  }
  wire->len = len;
  wire->off = 0;
}

pm_metal_net_tls_creds_h pm_metal_net_tls_creds_open(void)
{
  uint32_t i;

  if (TlsGlobalInit() != 0) {
    return PM_METAL_TLS_CREDS_INVALID;
  }

  for (i = 1; i <= TLS_CREDS_MAX; i++) {
    if (mTlsCreds[i].valid) {
      continue;
    }

    memset(&mTlsCreds[i], 0, sizeof(mTlsCreds[i]));
    mbedtls_x509_crt_init(&mTlsCreds[i].cert);
    mbedtls_x509_crt_init(&mTlsCreds[i].client_ca);
    mbedtls_pk_init(&mTlsCreds[i].key);
    mTlsCreds[i].valid = 1;
    return (pm_metal_net_tls_creds_h)i;
  }

  return PM_METAL_TLS_CREDS_INVALID;
}

int32_t pm_metal_net_tls_creds_load_buffers(pm_metal_net_tls_creds_h       h,
                                            const void                    *cert,
                                            uint32_t                       cert_len,
                                            const void                    *key,
                                            uint32_t                       key_len,
                                            const void                    *client_ca,
                                            uint32_t                       client_ca_len,
                                            pm_metal_net_tls_client_auth_t client_auth)
{
  tls_creds_t *creds;

  creds = TlsCredsFromHandle(h);
  if (creds == NULL || creds->refs != 0 || cert == NULL || key == NULL || cert_len == 0 ||
      key_len == 0 || client_auth > PM_METAL_TLS_CLIENT_AUTH_REQUIRED ||
      (client_auth != PM_METAL_TLS_CLIENT_AUTH_NONE && (client_ca == NULL || client_ca_len == 0))) {
    return -1;
  }

  TlsCredsReset(creds);
  if (TlsParseCert(&creds->cert, cert, cert_len) != 0 ||
      TlsParseKey(&creds->key, key, key_len) != 0 ||
      (client_auth != PM_METAL_TLS_CLIENT_AUTH_NONE &&
       TlsParseCert(&creds->client_ca, client_ca, client_ca_len) != 0)) {
    TlsCredsReset(creds);
    return -1;
  }

  creds->client_auth = client_auth;
  creds->loaded      = 1;
  return 0;
}

static int32_t TlsReadFile(const char *path, uint8_t **out, uint32_t *out_len)
{
  pm_metal_fs_stat_t stat;
  uint8_t           *data;

  if (path == NULL || out == NULL || out_len == NULL || pm_metal_fs_stat(path, &stat) != 0 ||
      stat.type != PM_METAL_FS_TYPE_FILE || stat.size == 0) {
    return -1;
  }

  data = (uint8_t *)pm_metal_mem_alloc(stat.size, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (data == NULL || pm_metal_fs_read(path, data, stat.size) != stat.size) {
    pm_metal_mem_free(data);
    return -1;
  }

  *out     = data;
  *out_len = stat.size;
  return 0;
}

int32_t pm_metal_net_tls_creds_load_paths(pm_metal_net_tls_creds_h       h,
                                          const char                    *cert_path,
                                          const char                    *key_path,
                                          const char                    *client_ca_path,
                                          pm_metal_net_tls_client_auth_t client_auth)
{
  uint8_t *cert;
  uint8_t *key;
  uint8_t *client_ca;
  uint32_t cert_len;
  uint32_t key_len;
  uint32_t client_ca_len;
  int32_t  rc;

  cert          = NULL;
  key           = NULL;
  client_ca     = NULL;
  cert_len      = 0;
  key_len       = 0;
  client_ca_len = 0;
  if (TlsReadFile(cert_path, &cert, &cert_len) != 0 || TlsReadFile(key_path, &key, &key_len) != 0 ||
      (client_auth != PM_METAL_TLS_CLIENT_AUTH_NONE &&
       TlsReadFile(client_ca_path, &client_ca, &client_ca_len) != 0)) {
    pm_metal_mem_free(cert);
    pm_metal_mem_free(key);
    pm_metal_mem_free(client_ca);
    return -1;
  }

  rc = pm_metal_net_tls_creds_load_buffers(
    h, cert, cert_len, key, key_len, client_ca, client_ca_len, client_auth);
  pm_metal_mem_free(cert);
  pm_metal_mem_free(key);
  pm_metal_mem_free(client_ca);
  return rc;
}

int32_t pm_metal_net_tls_creds_close(pm_metal_net_tls_creds_h h)
{
  tls_creds_t *creds;

  creds = TlsCredsFromHandle(h);
  if (creds == NULL || creds->refs != 0) {
    return -1;
  }

  TlsCredsReset(creds);
  creds->valid = 0;
  return 0;
}

pm_metal_net_tls_h pm_metal_net_tls_open(const char *sni_host)
{
  uint32_t i;

  if (sni_host == NULL || sni_host[0] == '\0') {
    return PM_METAL_TLS_INVALID;
  }

  if (TlsGlobalInit() != 0) {
    return PM_METAL_TLS_INVALID;
  }

  for (i = 1; i <= TLS_SESS_MAX; i++) {
    if (mTls[i].valid) {
      continue;
    }

    memset(&mTls[i], 0, sizeof(mTls[i]));
    mTls[i].valid = 1;
    mTls[i].sock  = PM_METAL_NET_IP_SOCK_INVALID;
    snprintf(mTls[i].sni, sizeof(mTls[i].sni), "%s", sni_host);
    return (pm_metal_net_tls_h)i;
  }

  return PM_METAL_TLS_INVALID;
}

pm_metal_net_tls_h pm_metal_net_tls_open_server(pm_metal_net_tls_creds_h creds_h)
{
  tls_creds_t *creds;
  uint32_t     i;

  creds = TlsCredsFromHandle(creds_h);
  if (creds == NULL || !creds->loaded) {
    return PM_METAL_TLS_INVALID;
  }

  for (i = 1; i <= TLS_SESS_MAX; i++) {
    if (mTls[i].valid) {
      continue;
    }

    memset(&mTls[i], 0, sizeof(mTls[i]));
    mTls[i].valid   = 1;
    mTls[i].server  = 1;
    mTls[i].sock    = PM_METAL_NET_IP_SOCK_INVALID;
    mTls[i].creds_h = creds_h;
    creds->refs++;
    return (pm_metal_net_tls_h)i;
  }

  return PM_METAL_TLS_INVALID;
}

static void TlsTeardown(tls_sess_t *t)
{
  if (t == NULL) {
    return;
  }

  if (t->initialized) {
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->conf);
    t->initialized = 0;
    t->ready       = 0;
  }

  t->done = 0;
  t->wire = NULL;
  t->sock = PM_METAL_NET_IP_SOCK_INVALID;
}

static void TlsReleaseCreds(tls_sess_t *t)
{
  tls_creds_t *creds;

  if (t == NULL || !t->server || t->creds_h == PM_METAL_TLS_CREDS_INVALID) {
    return;
  }

  creds = TlsCredsFromHandle(t->creds_h);
  if (creds != NULL && creds->refs > 0) {
    creds->refs--;
  }

  t->creds_h = PM_METAL_TLS_CREDS_INVALID;
}

void pm_metal_net_tls_close(pm_metal_net_tls_h h)
{
  tls_sess_t *t;

  t = TlsFromHandle(h);
  if (t == NULL) {
    return;
  }

  TlsTeardown(t);
  TlsReleaseCreds(t);
  t->valid = 0;
}

static int32_t TlsBind(pm_metal_net_tls_h       h,
                       pm_metal_net_ip_sock_h   sock,
                       pm_metal_net_tls_wire_t *wire)
{
  tls_sess_t  *t;
  tls_creds_t *creds;
  int32_t      e;

  t = TlsFromHandle(h);
  if (t == NULL || sock == PM_METAL_NET_IP_SOCK_INVALID || wire == NULL) {
    return -1;
  }

  TlsTeardown(t);
  t->sock = sock;
  t->wire = wire;
  t->done = 0;
  pm_metal_net_tls_wire_reset(wire);

  mbedtls_ssl_init(&t->ssl);
  mbedtls_ssl_config_init(&t->conf);
  t->initialized = 1;
  e              = mbedtls_ssl_config_defaults(&t->conf,
                                  t->server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
                                  MBEDTLS_SSL_TRANSPORT_STREAM,
                                  MBEDTLS_SSL_PRESET_DEFAULT);
  if (e != 0) {
    TlsTeardown(t);
    return -1;
  }

  if (t->server) {
    creds = TlsCredsFromHandle(t->creds_h);
    if (creds == NULL || !creds->loaded ||
        mbedtls_ssl_conf_own_cert(&t->conf, &creds->cert, &creds->key) != 0) {
      TlsTeardown(t);
      return -1;
    }

    if (creds->client_auth != PM_METAL_TLS_CLIENT_AUTH_NONE) {
      mbedtls_ssl_conf_ca_chain(&t->conf, &creds->client_ca, NULL);
    }

    mbedtls_ssl_conf_authmode(&t->conf,
                              creds->client_auth == PM_METAL_TLS_CLIENT_AUTH_REQUIRED
                                ? MBEDTLS_SSL_VERIFY_REQUIRED
                                : (creds->client_auth == PM_METAL_TLS_CLIENT_AUTH_OPTIONAL
                                     ? MBEDTLS_SSL_VERIFY_OPTIONAL
                                     : MBEDTLS_SSL_VERIFY_NONE));
#if defined(MBEDTLS_SSL_ALPN)
    {
      static const char *alpn_list[] = { "http/1.1", NULL };

      (void)mbedtls_ssl_conf_alpn_protocols(&t->conf, alpn_list);
    }
#endif
  } else {
    mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
  }

  mbedtls_ssl_conf_rng(&t->conf, mbedtls_ctr_drbg_random, &mCtrDrbg);
  e = mbedtls_ssl_setup(&t->ssl, &t->conf);
  if (e != 0) {
    TlsTeardown(t);
    return -1;
  }

  if (!t->server) {
    e = mbedtls_ssl_set_hostname(&t->ssl, t->sni);
    if (e != 0) {
      TlsTeardown(t);
      return -1;
    }
  }

  mbedtls_ssl_set_bio(&t->ssl, t, TlsSockSend, TlsSockRecv, NULL);
  t->ready = 1;
  return 0;
}

int32_t pm_metal_net_tls_bind(pm_metal_net_tls_h       h,
                              pm_metal_net_ip_sock_h   sock,
                              pm_metal_net_tls_wire_t *wire)
{
  tls_sess_t *t = TlsFromHandle(h);

  return t == NULL || t->server ? -1 : TlsBind(h, sock, wire);
}

int32_t pm_metal_net_tls_bind_server(pm_metal_net_tls_h       h,
                                     pm_metal_net_ip_sock_h   sock,
                                     pm_metal_net_tls_wire_t *wire)
{
  tls_sess_t *t = TlsFromHandle(h);

  return t == NULL || !t->server ? -1 : TlsBind(h, sock, wire);
}

int32_t pm_metal_net_tls_handshake_step(pm_metal_net_tls_h h)
{
  tls_sess_t *t;
  int32_t     e;

  t = TlsFromHandle(h);
  if (t == NULL || !t->ready) {
    return -1;
  }

  e = mbedtls_ssl_handshake(&t->ssl);
  if (e == 0) {
    t->done = 1;
    return 0;
  }

  if (e == MBEDTLS_ERR_SSL_WANT_READ || e == MBEDTLS_ERR_SSL_WANT_WRITE) {
    return 1;
  }

  return -1;
}

int32_t pm_metal_net_tls_handshake_done(pm_metal_net_tls_h h)
{
  tls_sess_t *t;

  t = TlsFromHandle(h);
  if (t == NULL) {
    return 0;
  }

  return t->done;
}

int32_t pm_metal_net_tls_read(pm_metal_net_tls_h h, void *buf, uint32_t cap)
{
  tls_sess_t *t;

  t = TlsFromHandle(h);
  if (t == NULL || !t->ready || !t->done || buf == NULL || cap == 0) {
    return -1;
  }

  return mbedtls_ssl_read(&t->ssl, buf, cap);
}

int32_t pm_metal_net_tls_write(pm_metal_net_tls_h h, const void *buf, uint32_t len)
{
  tls_sess_t *t;
  int32_t     e;

  t = TlsFromHandle(h);
  if (t == NULL || !t->ready || !t->done || buf == NULL || len == 0) {
    return -1;
  }

  e = mbedtls_ssl_write(&t->ssl, buf, len);
  if (e > 0) {
    return e;
  }

  if (e == MBEDTLS_ERR_SSL_WANT_READ) {
    return PM_METAL_TLS_WANT_READ;
  }

  if (e == MBEDTLS_ERR_SSL_WANT_WRITE) {
    return PM_METAL_TLS_WANT_WRITE;
  }

  return -1;
}

uint32_t pm_metal_net_tls_peer_cert_der(pm_metal_net_tls_h h, void *dest, uint32_t cap)
{
  tls_sess_t             *t;
  const mbedtls_x509_crt *cert;

  t = TlsFromHandle(h);
  if (t == NULL || !t->server || !t->done || dest == NULL || cap == 0) {
    return 0;
  }

  {
    tls_creds_t *creds = TlsCredsFromHandle(t->creds_h);

    if (creds == NULL || creds->client_auth == PM_METAL_TLS_CLIENT_AUTH_NONE) {
      return 0;
    }
  }

  cert = mbedtls_ssl_get_peer_cert(&t->ssl);
  if (cert == NULL || cert->raw.p == NULL || cert->raw.len == 0 || cert->raw.len > cap) {
    return 0;
  }

  memcpy(dest, cert->raw.p, cert->raw.len);
  return (uint32_t)cert->raw.len;
}

/*
 * TLS is host-only (http.c). Stubs keep wasm.c call sites; no WASI natives —
 * guests use pm_metal_net_http_get for HTTPS.
 */
int32_t pm_metal_net_tls_native_register(void)
{
  return 0;
}
