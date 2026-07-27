/*
 * Shared auth — Argon2id/bcrypt passwords + SSH public keys.
 */
#include <pymergetic/metal/auth/auth.h>

#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/fs/fs.h>
#include <pymergetic/metal/runtime/mem/mem.h>
#include <pymergetic/metal/dev/net/mbedtls_metal_config.h>

#include "external/monocypher/src/monocypher.h"
#include "external/crypt_blowfish/ow-crypt.h"

#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/x509_crt.h>

#define AUTH_KEYS_FILE_MAX (8u * 1024u)

static pm_metal_auth_user_t   g_users[PM_METAL_AUTH_USERS_MAX];
static pm_metal_auth_pubkey_t g_pubkeys[PM_METAL_AUTH_PUBKEYS_MAX];
static int32_t                g_pubkeys_loaded;

static int32_t ct_eq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
  uint8_t d;
  uint32_t i;

  d = 0;
  for (i = 0; i < n; i++) {
    d = (uint8_t)(d | (a[i] ^ b[i]));
  }
  return d == 0 ? 1 : 0;
}

static int32_t b64_val(char c)
{
  if (c >= 'A' && c <= 'Z') {
    return c - 'A';
  }
  if (c >= 'a' && c <= 'z') {
    return c - 'a' + 26;
  }
  if (c >= '0' && c <= '9') {
    return c - '0' + 52;
  }
  if (c == '+') {
    return 62;
  }
  if (c == '/') {
    return 63;
  }
  return -1;
}

static int32_t b64_decode(const char *in, uint8_t *out, uint32_t out_cap, uint32_t *out_len)
{
  uint32_t i;
  uint32_t o;
  uint32_t len;

  if (in == NULL || out == NULL || out_len == NULL) {
    return -1;
  }
  len = (uint32_t)strlen(in);
  o   = 0;
  for (i = 0; i + 3 < len; i += 4) {
    int32_t a, b, c, d;
    uint32_t v;
    uint32_t nout;

    a = b64_val(in[i]);
    b = b64_val(in[i + 1]);
    if (a < 0 || b < 0) {
      return -1;
    }
    c    = (in[i + 2] == '=') ? 0 : b64_val(in[i + 2]);
    d    = (in[i + 3] == '=') ? 0 : b64_val(in[i + 3]);
    if (c < 0 || d < 0) {
      return -1;
    }
    v    = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | (uint32_t)d;
    nout = 3;
    if (in[i + 2] == '=') {
      nout = 1;
    } else if (in[i + 3] == '=') {
      nout = 2;
    }
    if (o + nout > out_cap) {
      return -1;
    }
    out[o++] = (uint8_t)((v >> 16) & 0xffu);
    if (nout >= 2) {
      out[o++] = (uint8_t)((v >> 8) & 0xffu);
    }
    if (nout >= 3) {
      out[o++] = (uint8_t)(v & 0xffu);
    }
  }
  *out_len = o;
  return 0;
}

static const char *field_after(const char *s, char sep)
{
  const char *p;

  p = strchr(s, sep);
  if (p == NULL) {
    return NULL;
  }
  return p + 1;
}

static int32_t parse_u32(const char *s, const char *end, uint32_t *out)
{
  uint32_t v;

  if (s == NULL || out == NULL || s >= end) {
    return -1;
  }
  v = 0;
  while (s < end && *s >= '0' && *s <= '9') {
    v = v * 10u + (uint32_t)(*s - '0');
    s++;
  }
  *out = v;
  return 0;
}

static int32_t verify_argon2id(const char *encoded, const char *pass)
{
  const char *p;
  const char *m_s;
  const char *t_s;
  const char *p_s;
  const char *salt_b64;
  const char *hash_b64;
  const char *comma;
  uint32_t    m, t, lanes;
  uint8_t     salt[64];
  uint8_t     expect[64];
  uint8_t     got[64];
  uint32_t    salt_len;
  uint32_t    hash_len;
  void       *work;
  crypto_argon2_config cfg;
  crypto_argon2_inputs in;

  if (strncmp(encoded, "$argon2id$", 10) != 0) {
    return 0;
  }
  p = encoded + 10;
  if (strncmp(p, "v=19$", 5) != 0) {
    return 0;
  }
  p   = p + 5;
  m_s = p;
  if (strncmp(m_s, "m=", 2) != 0) {
    return 0;
  }
  m_s += 2;
  comma = strchr(m_s, ',');
  if (comma == NULL || parse_u32(m_s, comma, &m) != 0 || m < 8) {
    return 0;
  }
  t_s = comma + 1;
  if (strncmp(t_s, "t=", 2) != 0) {
    return 0;
  }
  t_s += 2;
  comma = strchr(t_s, ',');
  if (comma == NULL || parse_u32(t_s, comma, &t) != 0 || t < 1) {
    return 0;
  }
  p_s = comma + 1;
  if (strncmp(p_s, "p=", 2) != 0) {
    return 0;
  }
  p_s += 2;
  comma = strchr(p_s, '$');
  if (comma == NULL || parse_u32(p_s, comma, &lanes) != 0 || lanes < 1) {
    return 0;
  }
  salt_b64 = comma + 1;
  hash_b64 = field_after(salt_b64, '$');
  if (hash_b64 == NULL) {
    return 0;
  }
  {
    char salt_tmp[96];
    char hash_tmp[96];
    uint32_t sl, hl;

    sl = (uint32_t)(hash_b64 - salt_b64 - 1u);
    hl = (uint32_t)strlen(hash_b64);
    if (sl >= sizeof(salt_tmp) || hl >= sizeof(hash_tmp)) {
      return 0;
    }
    memcpy(salt_tmp, salt_b64, sl);
    salt_tmp[sl] = '\0';
    memcpy(hash_tmp, hash_b64, hl + 1u);
    if (b64_decode(salt_tmp, salt, sizeof(salt), &salt_len) != 0 ||
        b64_decode(hash_tmp, expect, sizeof(expect), &hash_len) != 0 || hash_len == 0 ||
        hash_len > sizeof(got)) {
      return 0;
    }
  }

  work = pm_metal_mem_alloc(m * 1024u, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (work == NULL) {
    return 0;
  }
  cfg.algorithm = CRYPTO_ARGON2_ID;
  cfg.nb_blocks = m;
  cfg.nb_passes = t;
  cfg.nb_lanes  = lanes;
  in.pass       = (const uint8_t *)pass;
  in.salt       = salt;
  in.pass_size  = (uint32_t)strlen(pass);
  in.salt_size  = salt_len;
  crypto_argon2(got, hash_len, work, cfg, in, crypto_argon2_no_extras);
  pm_metal_mem_free(work);
  return ct_eq(got, expect, hash_len);
}

static int32_t verify_bcrypt(const char *encoded, const char *pass)
{
  char   data[128];
  char  *out;
  size_t n;

  if (encoded == NULL || pass == NULL) {
    return 0;
  }
  if (strncmp(encoded, "$2a$", 4) != 0 && strncmp(encoded, "$2b$", 4) != 0 &&
      strncmp(encoded, "$2y$", 4) != 0) {
    return 0;
  }
  memset(data, 0, sizeof(data));
  out = crypt_rn(pass, encoded, data, (int)sizeof(data));
  if (out == NULL) {
    return 0;
  }
  n = strlen(encoded);
  if (strlen(out) != n) {
    return 0;
  }
  return ct_eq((const uint8_t *)out, (const uint8_t *)encoded, (uint32_t)n);
}

int32_t pm_metal_auth_hash_verify(const char *encoded, const char *pass)
{
  if (encoded == NULL || pass == NULL) {
    return 0;
  }
  if (strncmp(encoded, "$argon2id$", 10) == 0) {
    return verify_argon2id(encoded, pass);
  }
  if (encoded[0] == '$' && encoded[1] == '2') {
    return verify_bcrypt(encoded, pass);
  }
  return 0;
}

void pm_metal_auth_users_set(const pm_metal_auth_user_t *users, uint32_t n)
{
  uint32_t i;

  memset(g_users, 0, sizeof(g_users));
  if (users == NULL || n == 0) {
    return;
  }
  if (n > PM_METAL_AUTH_USERS_MAX) {
    n = PM_METAL_AUTH_USERS_MAX;
  }
  for (i = 0; i < n; i++) {
    g_users[i] = users[i];
    g_users[i].used = 1;
  }
}

int32_t pm_metal_auth_user_check(const char *user, const char *pass)
{
  uint32_t i;

  if (user == NULL || pass == NULL) {
    return 0;
  }
  for (i = 0; i < PM_METAL_AUTH_USERS_MAX; i++) {
    if (!g_users[i].used) {
      continue;
    }
    if (strcmp(g_users[i].name, user) != 0) {
      continue;
    }
    return pm_metal_auth_hash_verify(g_users[i].hash, pass);
  }
  return 0;
}

int32_t pm_metal_auth_basic_decode(const char *b64, char *user, uint32_t user_cap, char *pass,
                                   uint32_t pass_cap)
{
  uint8_t  raw[128];
  uint32_t raw_len;
  uint32_t i;
  char    *colon;

  if (b64 == NULL || user == NULL || pass == NULL || user_cap == 0 || pass_cap == 0) {
    return -1;
  }
  while (*b64 == ' ') {
    b64++;
  }
  if (b64_decode(b64, raw, sizeof(raw) - 1u, &raw_len) != 0 || raw_len == 0) {
    return -1;
  }
  raw[raw_len] = 0;
  colon = strchr((char *)raw, ':');
  if (colon == NULL) {
    return -1;
  }
  *colon = '\0';
  if (strlen((char *)raw) >= user_cap || strlen(colon + 1) >= pass_cap) {
    return -1;
  }
  for (i = 0; raw[i] != '\0'; i++) {
    user[i] = (char)raw[i];
  }
  user[i] = '\0';
  strncpy(pass, colon + 1, pass_cap - 1u);
  pass[pass_cap - 1u] = '\0';
  return 0;
}

void pm_metal_auth_pubkeys_clear(void)
{
  memset(g_pubkeys, 0, sizeof(g_pubkeys));
}

int32_t pm_metal_auth_pubkey_add(const char *user, const char *algo, const uint8_t *blob,
                                 uint32_t blob_len)
{
  uint32_t i;

  if (user == NULL || user[0] == '\0' || algo == NULL || algo[0] == '\0' || blob == NULL ||
      blob_len == 0u || blob_len > PM_METAL_AUTH_PUBKEY_BLOB_MAX) {
    return -1;
  }
  if (strlen(user) >= PM_METAL_AUTH_USER_MAX || strlen(algo) >= PM_METAL_AUTH_ALGO_MAX) {
    return -1;
  }
  for (i = 0; i < PM_METAL_AUTH_PUBKEYS_MAX; i++) {
    if (g_pubkeys[i].used) {
      continue;
    }
    memset(&g_pubkeys[i], 0, sizeof(g_pubkeys[i]));
    strncpy(g_pubkeys[i].user, user, sizeof(g_pubkeys[i].user) - 1u);
    strncpy(g_pubkeys[i].algo, algo, sizeof(g_pubkeys[i].algo) - 1u);
    memcpy(g_pubkeys[i].blob, blob, blob_len);
    g_pubkeys[i].blob_len = blob_len;
    g_pubkeys[i].used     = 1;
    return 0;
  }
  return -1;
}

static const char *skip_ws_token(const char *p)
{
  while (p != NULL && (*p == ' ' || *p == '\t')) {
    p++;
  }
  return p;
}

static const char *token_end(const char *p)
{
  while (p != NULL && *p != '\0' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
    p++;
  }
  return p;
}

int32_t pm_metal_auth_pubkey_add_line(const char *user, const char *line)
{
  const char *p;
  const char *algo_s;
  const char *algo_e;
  const char *b64_s;
  const char *b64_e;
  char        algo[PM_METAL_AUTH_ALGO_MAX];
  char        b64tmp[900];
  uint8_t     blob[PM_METAL_AUTH_PUBKEY_BLOB_MAX];
  uint32_t    blob_len;
  uint32_t    alen;
  uint32_t    blen;

  if (user == NULL || line == NULL) {
    return -1;
  }
  p = skip_ws_token(line);
  if (*p == '\0' || *p == '#') {
    return -1;
  }
  /* Optional "user=<name>" overrides the caller user for shared files. */
  if (strncmp(p, "user=", 5) == 0) {
    const char *ue;
    uint32_t    ul;
    static char uh[PM_METAL_AUTH_USER_MAX];

    p += 5;
    ue = token_end(p);
    ul = (uint32_t)(ue - p);
    if (ul == 0u || ul >= sizeof(uh)) {
      return -1;
    }
    memcpy(uh, p, ul);
    uh[ul] = '\0';
    user   = uh;
    p      = skip_ws_token(ue);
  }
  /* Skip optional leading options (contain '=' or ',' before the key type). */
  for (;;) {
    const char *te;
    uint32_t    tlen;
    int32_t     looks_algo;

    te   = token_end(p);
    tlen = (uint32_t)(te - p);
    if (tlen == 0) {
      return -1;
    }
    looks_algo = 0;
    if (tlen >= 4u && strncmp(p, "ssh-", 4) == 0) {
      looks_algo = 1;
    } else if (tlen >= 7u && strncmp(p, "sk-ssh-", 7) == 0) {
      looks_algo = 1;
    }
    if (looks_algo) {
      break;
    }
    p = skip_ws_token(te);
    if (*p == '\0') {
      return -1;
    }
  }
  algo_s = p;
  algo_e = token_end(algo_s);
  alen   = (uint32_t)(algo_e - algo_s);
  if (alen == 0u || alen >= sizeof(algo)) {
    return -1;
  }
  memcpy(algo, algo_s, alen);
  algo[alen] = '\0';
  b64_s      = skip_ws_token(algo_e);
  if (*b64_s == '\0') {
    return -1;
  }
  b64_e = token_end(b64_s);
  blen  = (uint32_t)(b64_e - b64_s);
  if (blen == 0u || blen >= sizeof(b64tmp)) {
    return -1;
  }
  memcpy(b64tmp, b64_s, blen);
  b64tmp[blen] = '\0';
  if (b64_decode(b64tmp, blob, sizeof(blob), &blob_len) != 0 || blob_len == 0u) {
    return -1;
  }
  return pm_metal_auth_pubkey_add(user, algo, blob, blob_len);
}

int32_t pm_metal_auth_pubkey_load_text(const char *user, const char *text, uint32_t text_len)
{
  uint32_t    i;
  uint32_t    start;
  int32_t     added;
  char        line[4200];
  uint32_t    llen;

  if (user == NULL || text == NULL) {
    return 0;
  }
  added = 0;
  start = 0;
  for (i = 0; i <= text_len; i++) {
    if (i < text_len && text[i] != '\n' && text[i] != '\r') {
      continue;
    }
    llen = i - start;
    if (llen > 0u && llen + 1u < sizeof(line)) {
      memcpy(line, text + start, llen);
      line[llen] = '\0';
      if (pm_metal_auth_pubkey_add_line(user, line) == 0) {
        added++;
      }
    }
    if (i < text_len && text[i] == '\r' && (i + 1u) < text_len && text[i + 1u] == '\n') {
      i++;
    }
    start = i + 1u;
  }
  return added;
}

static int32_t pubkey_load_path(const char *path, const char *user)
{
  uint8_t *buf;
  uint32_t sz;
  uint32_t n;

  if (path == NULL || user == NULL) {
    return -1;
  }
  sz = pm_metal_fs_size(path);
  if (sz == 0 || sz > AUTH_KEYS_FILE_MAX) {
    return -1;
  }
  buf = (uint8_t *)pm_metal_mem_alloc(sz + 1u, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (buf == NULL) {
    return -1;
  }
  n = pm_metal_fs_read(path, buf, sz);
  if (n == 0) {
    pm_metal_mem_free(buf);
    return -1;
  }
  buf[n] = '\0';
  (void)pm_metal_auth_pubkey_load_text(user, (const char *)buf, n);
  pm_metal_mem_free(buf);
  return 0;
}

int32_t pm_metal_auth_pubkey_reload(void)
{
  uint32_t i;
  char     path[96];

  pm_metal_auth_pubkeys_clear();
  g_pubkeys_loaded = 0;
  /*
   * Shared file: lines apply to user "test" by default (lab account),
   * or "user=<name> ssh-ed25519 AAAA..." for other accounts.
   */
  (void)pubkey_load_path(PM_METAL_AUTH_PUBKEY_PATH, "test");
  for (i = 0; i < PM_METAL_AUTH_USERS_MAX; i++) {
    if (!g_users[i].used) {
      continue;
    }
    snprintf(path, sizeof(path), "%s.d/%s", PM_METAL_AUTH_PUBKEY_PATH, g_users[i].name);
    (void)pubkey_load_path(path, g_users[i].name);
  }
  g_pubkeys_loaded = 1;
  return 0;
}

int32_t pm_metal_auth_pubkey_check(const char *user, const char *algo, const uint8_t *key_blob,
                                   uint32_t key_len)
{
  uint32_t i;

  if (user == NULL || key_blob == NULL || key_len == 0u) {
    return 0;
  }
  if (!g_pubkeys_loaded) {
    (void)pm_metal_auth_pubkey_reload();
  }
  for (i = 0; i < PM_METAL_AUTH_PUBKEYS_MAX; i++) {
    if (!g_pubkeys[i].used) {
      continue;
    }
    if (strcmp(g_pubkeys[i].user, user) != 0) {
      continue;
    }
    if (algo != NULL && algo[0] != '\0' && strcmp(g_pubkeys[i].algo, algo) != 0) {
      continue;
    }
    if (g_pubkeys[i].blob_len != key_len) {
      continue;
    }
    if (ct_eq(g_pubkeys[i].blob, key_blob, key_len)) {
      return 1;
    }
  }
  return 0;
}

static int32_t sslcert_parse_ca(mbedtls_x509_crt *ca, const uint8_t *data, uint32_t len)
{
  static const char pem_prefix[] = "-----BEGIN";
  uint8_t          *copy;
  int32_t           rc;

  if (ca == NULL || data == NULL || len == 0u) {
    return -1;
  }
  if (len < sizeof(pem_prefix) - 1u || memcmp(data, pem_prefix, sizeof(pem_prefix) - 1u) != 0) {
    return mbedtls_x509_crt_parse_der(ca, data, len) == 0 ? 0 : -1;
  }
  copy = (uint8_t *)pm_metal_mem_alloc((size_t)len + 1u, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (copy == NULL) {
    return -1;
  }
  memcpy(copy, data, len);
  copy[len] = '\0';
  rc        = mbedtls_x509_crt_parse(ca, copy, (size_t)len + 1u);
  pm_metal_mem_free(copy);
  return rc == 0 ? 0 : -1;
}

static int32_t sslcert_subject_user(const mbedtls_x509_crt *cert, char *user, uint32_t user_cap)
{
  char identity[PM_METAL_AUTH_USER_MAX + 4u];
  int32_t n;

  if (cert == NULL || user == NULL || user_cap == 0u) {
    return 0;
  }
  n = mbedtls_x509_dn_gets(identity, sizeof(identity), &cert->subject);
  if (n <= 3 || (uint32_t)n >= sizeof(identity) || strncmp(identity, "CN=", 3) != 0 ||
      strchr(identity + 3, ',') != NULL || (uint32_t)n - 3u >= user_cap) {
    return 0;
  }
  memcpy(user, identity + 3, (uint32_t)n - 3u);
  user[n - 3] = '\0';
  return 1;
}

int32_t pm_metal_auth_sslcert_check(const uint8_t *cert_der, uint32_t cert_len,
                                    const char *client_ca_path, char *user, uint32_t user_cap)
{
  mbedtls_x509_crt cert;
  mbedtls_x509_crt ca;
  uint8_t         *ca_data;
  uint32_t         ca_len;
  uint32_t         flags;
  int32_t          ok;

  if (cert_der == NULL || cert_len == 0u || cert_len > PM_METAL_AUTH_SSLCERT_MAX ||
      client_ca_path == NULL || client_ca_path[0] == '\0' || user == NULL || user_cap == 0u) {
    return 0;
  }
  ca_len = pm_metal_fs_size(client_ca_path);
  if (ca_len == 0u || ca_len == (uint32_t)-1 || ca_len > PM_METAL_AUTH_CLIENT_CA_MAX) {
    return 0;
  }
  ca_data = (uint8_t *)pm_metal_mem_alloc(ca_len, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (ca_data == NULL || pm_metal_fs_read(client_ca_path, ca_data, ca_len) != ca_len) {
    pm_metal_mem_free(ca_data);
    return 0;
  }

  pm_metal_mbedtls_runtime_init();
  mbedtls_x509_crt_init(&cert);
  mbedtls_x509_crt_init(&ca);
  ok = 0;
  if (mbedtls_x509_crt_parse_der(&cert, cert_der, cert_len) == 0 &&
      sslcert_parse_ca(&ca, ca_data, ca_len) == 0 &&
      mbedtls_x509_crt_verify(&cert, &ca, NULL, NULL, &flags, NULL, NULL) == 0 &&
      sslcert_subject_user(&cert, user, user_cap)) {
    ok = 1;
  }
  mbedtls_x509_crt_free(&cert);
  mbedtls_x509_crt_free(&ca);
  pm_metal_mem_free(ca_data);
  return ok;
}

int32_t pm_metal_auth_sslcert_verify(const uint8_t *cert_der, uint32_t cert_len,
                                     const uint8_t *signed_data, uint32_t signed_len,
                                     const uint8_t *signature, uint32_t signature_len)
{
  mbedtls_x509_crt cert;
  uint8_t          hash[32];
  int32_t          ok;

  if (cert_der == NULL || cert_len == 0u || cert_len > PM_METAL_AUTH_SSLCERT_MAX ||
      signed_data == NULL || signed_len == 0u || signature == NULL || signature_len == 0u) {
    return 0;
  }
  pm_metal_mbedtls_runtime_init();
  mbedtls_x509_crt_init(&cert);
  ok = 0;
  if (mbedtls_x509_crt_parse_der(&cert, cert_der, cert_len) == 0 &&
      mbedtls_sha256(signed_data, signed_len, hash, 0) == 0 &&
      mbedtls_pk_verify(&cert.pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), signature, signature_len) ==
        0) {
    ok = 1;
  }
  mbedtls_x509_crt_free(&cert);
  return ok;
}
