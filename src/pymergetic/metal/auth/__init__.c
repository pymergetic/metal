/*
 * Shared auth — Argon2id passwords + SSH public keys.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/auth/__init__.h>
#include <pymergetic/metal/mem.h>

#include "monocypher.h"

static pm_metal_auth_user_t g_users[PM_METAL_AUTH_USERS_MAX];
static pm_metal_auth_pubkey_t g_pubkeys[PM_METAL_AUTH_PUBKEYS_MAX];

static int32_t ct_eq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
  uint8_t d;
  uint32_t i;

  d = 0;
  for (i = 0u; i < n; i++) {
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
  o = 0u;
  for (i = 0u; i + 3u < len; i += 4u) {
    int32_t a;
    int32_t b;
    int32_t c;
    int32_t d;
    uint32_t v;
    uint32_t nout;

    a = b64_val(in[i]);
    b = b64_val(in[i + 1u]);
    if (a < 0 || b < 0) {
      return -1;
    }
    c = (in[i + 2u] == '=') ? 0 : b64_val(in[i + 2u]);
    d = (in[i + 3u] == '=') ? 0 : b64_val(in[i + 3u]);
    if (c < 0 || d < 0) {
      return -1;
    }
    v = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | (uint32_t)d;
    nout = 3u;
    if (in[i + 2u] == '=') {
      nout = 1u;
    } else if (in[i + 3u] == '=') {
      nout = 2u;
    }
    if (o + nout > out_cap) {
      return -1;
    }
    out[o++] = (uint8_t)((v >> 16) & 0xffu);
    if (nout >= 2u) {
      out[o++] = (uint8_t)((v >> 8) & 0xffu);
    }
    if (nout >= 3u) {
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
  v = 0u;
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
  uint32_t m;
  uint32_t t;
  uint32_t lanes;
  uint8_t salt[64];
  uint8_t expect[64];
  uint8_t got[64];
  uint32_t salt_len;
  uint32_t hash_len;
  void *work;
  crypto_argon2_config cfg;
  crypto_argon2_inputs in;

  if (strncmp(encoded, "$argon2id$", 10) != 0) {
    return 0;
  }
  p = encoded + 10;
  if (strncmp(p, "v=19$", 5) != 0) {
    return 0;
  }
  p = p + 5;
  m_s = p;
  if (strncmp(m_s, "m=", 2) != 0) {
    return 0;
  }
  m_s += 2;
  comma = strchr(m_s, ',');
  if (comma == NULL || parse_u32(m_s, comma, &m) != 0 || m < 8u) {
    return 0;
  }
  t_s = comma + 1;
  if (strncmp(t_s, "t=", 2) != 0) {
    return 0;
  }
  t_s += 2;
  comma = strchr(t_s, ',');
  if (comma == NULL || parse_u32(t_s, comma, &t) != 0 || t < 1u) {
    return 0;
  }
  p_s = comma + 1;
  if (strncmp(p_s, "p=", 2) != 0) {
    return 0;
  }
  p_s += 2;
  comma = strchr(p_s, '$');
  if (comma == NULL || parse_u32(p_s, comma, &lanes) != 0 || lanes < 1u) {
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
    uint32_t sl;
    uint32_t hl;

    sl = (uint32_t)(hash_b64 - salt_b64 - 1u);
    hl = (uint32_t)strlen(hash_b64);
    if (sl >= sizeof(salt_tmp) || hl >= sizeof(hash_tmp)) {
      return 0;
    }
    memcpy(salt_tmp, salt_b64, sl);
    salt_tmp[sl] = '\0';
    memcpy(hash_tmp, hash_b64, hl + 1u);
    if (b64_decode(salt_tmp, salt, sizeof(salt), &salt_len) != 0 ||
        b64_decode(hash_tmp, expect, sizeof(expect), &hash_len) != 0 || hash_len == 0u ||
        hash_len > sizeof(got)) {
      return 0;
    }
  }

  work = pm_metal_mem_alloc((size_t)m * 1024u);
  if (work == NULL) {
    return 0;
  }
  cfg.algorithm = CRYPTO_ARGON2_ID;
  cfg.nb_blocks = m;
  cfg.nb_passes = t;
  cfg.nb_lanes = lanes;
  in.pass = (const uint8_t *)pass;
  in.salt = salt;
  in.pass_size = (uint32_t)strlen(pass);
  in.salt_size = salt_len;
  crypto_argon2(got, hash_len, work, cfg, in, crypto_argon2_no_extras);
  pm_metal_mem_free((uint8_t *)work);
  return ct_eq(got, expect, hash_len);
}

int32_t pm_metal_auth_hash_verify(const char *encoded, const char *pass)
{
  if (encoded == NULL || pass == NULL) {
    return 0;
  }
  if (strncmp(encoded, "$argon2id$", 10) == 0) {
    return verify_argon2id(encoded, pass);
  }
  /* bcrypt needs external/crypt_blowfish — not vendored in this tree yet. */
  return 0;
}

void pm_metal_auth_users_set(const pm_metal_auth_user_t *users, uint32_t n)
{
  uint32_t i;

  memset(g_users, 0, sizeof(g_users));
  if (users == NULL || n == 0u) {
    return;
  }
  if (n > PM_METAL_AUTH_USERS_MAX) {
    n = PM_METAL_AUTH_USERS_MAX;
  }
  for (i = 0u; i < n; i++) {
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
  for (i = 0u; i < PM_METAL_AUTH_USERS_MAX; i++) {
    if (g_users[i].used == 0) {
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
  uint8_t raw[128];
  uint32_t raw_len;
  uint32_t i;
  char *colon;

  if (b64 == NULL || user == NULL || pass == NULL || user_cap == 0u || pass_cap == 0u) {
    return -1;
  }
  while (*b64 == ' ') {
    b64++;
  }
  if (b64_decode(b64, raw, sizeof(raw) - 1u, &raw_len) != 0 || raw_len == 0u) {
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
  for (i = 0u; raw[i] != '\0'; i++) {
    user[i] = (char)raw[i];
  }
  user[i] = '\0';
  {
    size_t pn;

    pn = strlen(colon + 1);
    if (pn >= pass_cap) {
      pn = pass_cap - 1u;
    }
    memcpy(pass, colon + 1, pn);
    pass[pn] = '\0';
  }
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
  for (i = 0u; i < PM_METAL_AUTH_PUBKEYS_MAX; i++) {
    if (g_pubkeys[i].used != 0) {
      continue;
    }
    memset(&g_pubkeys[i], 0, sizeof(g_pubkeys[i]));
    {
      size_t un;
      size_t an;

      un = strlen(user);
      an = strlen(algo);
      memcpy(g_pubkeys[i].user, user, un);
      g_pubkeys[i].user[un] = '\0';
      memcpy(g_pubkeys[i].algo, algo, an);
      g_pubkeys[i].algo[an] = '\0';
    }
    memcpy(g_pubkeys[i].blob, blob, blob_len);
    g_pubkeys[i].blob_len = blob_len;
    g_pubkeys[i].used = 1;
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
  char algo[PM_METAL_AUTH_ALGO_MAX];
  char b64tmp[900];
  uint8_t blob[PM_METAL_AUTH_PUBKEY_BLOB_MAX];
  uint32_t blob_len;
  uint32_t alen;
  uint32_t blen;
  const char *eff_user;
  char ubuf[PM_METAL_AUTH_USER_MAX];

  if (user == NULL || line == NULL) {
    return -1;
  }
  p = skip_ws_token(line);
  if (p == NULL || *p == '\0' || *p == '#') {
    return -1;
  }
  /* Optional "user=<name> " prefix (shared authorized_keys). */
  eff_user = user;
  if (strncmp(p, "user=", 5) == 0) {
    const char *us;
    const char *ue;
    uint32_t ul;

    us = p + 5;
    ue = token_end(us);
    ul = (uint32_t)(ue - us);
    if (ul == 0u || ul >= PM_METAL_AUTH_USER_MAX) {
      return -1;
    }
    memcpy(ubuf, us, ul);
    ubuf[ul] = '\0';
    eff_user = ubuf;
    p = skip_ws_token(ue);
  }
  algo_s = p;
  algo_e = token_end(algo_s);
  alen = (uint32_t)(algo_e - algo_s);
  if (alen == 0u || alen >= PM_METAL_AUTH_ALGO_MAX) {
    return -1;
  }
  memcpy(algo, algo_s, alen);
  algo[alen] = '\0';
  b64_s = skip_ws_token(algo_e);
  b64_e = token_end(b64_s);
  blen = (uint32_t)(b64_e - b64_s);
  if (blen == 0u || blen >= sizeof(b64tmp)) {
    return -1;
  }
  memcpy(b64tmp, b64_s, blen);
  b64tmp[blen] = '\0';
  if (b64_decode(b64tmp, blob, sizeof(blob), &blob_len) != 0 || blob_len == 0u) {
    return -1;
  }
  return pm_metal_auth_pubkey_add(eff_user, algo, blob, blob_len);
}

int32_t pm_metal_auth_pubkey_load_text(const char *user, const char *text, uint32_t text_len)
{
  uint32_t i;
  uint32_t start;
  int32_t added;
  char line[1024];
  uint32_t llen;

  if (user == NULL || text == NULL) {
    return 0;
  }
  added = 0;
  start = 0u;
  for (i = 0u; i <= text_len; i++) {
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
    /* Oversized lines are skipped (EFI stack budget). */
    if (i < text_len && text[i] == '\r' && (i + 1u) < text_len && text[i + 1u] == '\n') {
      i++;
    }
    start = i + 1u;
  }
  return added;
}

int32_t pm_metal_auth_pubkey_check(const char *user, const char *algo, const uint8_t *key_blob,
                                   uint32_t key_len)
{
  uint32_t i;

  if (user == NULL || key_blob == NULL || key_len == 0u) {
    return 0;
  }
  for (i = 0u; i < PM_METAL_AUTH_PUBKEYS_MAX; i++) {
    if (g_pubkeys[i].used == 0) {
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
    if (ct_eq(g_pubkeys[i].blob, key_blob, key_len) != 0) {
      return 1;
    }
  }
  return 0;
}
