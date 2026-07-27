/*
 * /etc/sshd.json — schema-specific loader (no general JSON library).
 * Missing file is seeded from the shipped default blob.
 */
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/auth/auth.h>
#include <pymergetic/metal/dev/net/ssh_config.h>
#include <pymergetic/metal/fs/fs.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/runtime/mem/mem.h>

#define SSHD_JSON_PATH "/etc/sshd.json"
#define SSHD_JSON_MAX  (4u * 1024u)
#define SSH_AUTHKEYS_PATH PM_METAL_AUTH_PUBKEY_PATH

/* Mirrors mods/etc/sshd.json (keep in sync). */
static const char g_sshd_json_default[] =
  "{\n"
  "  \"port\": 22,\n"
  "  \"budget_pct\": 10,\n"
  "  \"auth\": {\n"
  "    \"methods\": [\"passwd\", \"pubkey\"],\n"
  "    \"client_ca\": \"\"\n"
  "  },\n"
  "  \"host_key\": \"/etc/ssh/dropbear_ed25519_host_key\"\n"
  "}\n";

static pm_metal_sshd_cfg_t g_cfg;

pm_metal_sshd_cfg_t *pm_metal_net_ssh_cfg(void)
{
  return &g_cfg;
}

static void cfg_defaults(void)
{
  memset(&g_cfg, 0, sizeof(g_cfg));
  g_cfg.port        = 22u;
  g_cfg.budget_pct  = 10u;
  g_cfg.auth_passwd = 1;
  g_cfg.auth_pubkey = 1;
  g_cfg.auth_sslcert = 0;
  snprintf(g_cfg.host_key, sizeof(g_cfg.host_key), "%s", "/etc/ssh/dropbear_ed25519_host_key");
}

static const char *skip_ws(const char *p)
{
  while (p != NULL && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
    p++;
  }
  return p;
}

static const char *find_key(const char *json, const char *key)
{
  char        pat[64];
  size_t      klen;
  const char *p;

  if (json == NULL || key == NULL) {
    return NULL;
  }
  klen = strlen(key);
  if (klen + 3u >= sizeof(pat)) {
    return NULL;
  }
  pat[0] = '"';
  memcpy(pat + 1, key, klen);
  pat[1u + klen] = '"';
  pat[2u + klen] = '\0';
  p = strstr(json, pat);
  if (p == NULL) {
    return NULL;
  }
  p = strchr(p + (klen + 2u), ':');
  if (p == NULL) {
    return NULL;
  }
  return skip_ws(p + 1);
}

static int32_t parse_u32(const char *p, uint32_t *out)
{
  uint32_t v;

  if (p == NULL || out == NULL || *p < '0' || *p > '9') {
    return -1;
  }
  v = 0;
  while (*p >= '0' && *p <= '9') {
    v = v * 10u + (uint32_t)(*p - '0');
    p++;
  }
  *out = v;
  return 0;
}

static int32_t parse_string(const char *p, char *out, uint32_t cap)
{
  uint32_t n;

  if (p == NULL || out == NULL || cap == 0 || *p != '"') {
    return -1;
  }
  p++;
  n = 0;
  while (*p != '\0' && *p != '"') {
    if (n + 1u >= cap) {
      return -1;
    }
    out[n++] = *p++;
  }
  if (*p != '"') {
    return -1;
  }
  out[n] = '\0';
  return 0;
}

static void parse_json(const char *json)
{
  const char *p;
  uint32_t    v;

  cfg_defaults();
  if (json == NULL) {
    return;
  }
  p = find_key(json, "port");
  if (p != NULL && parse_u32(p, &v) == 0 && v > 0u && v < 65536u) {
    g_cfg.port = v;
  }
  p = find_key(json, "budget_pct");
  if (p != NULL && parse_u32(p, &v) == 0 && v > 0u && v <= 100u) {
    g_cfg.budget_pct = v;
  }
  p = find_key(json, "host_key");
  if (p != NULL) {
    (void)parse_string(p, g_cfg.host_key, (uint32_t)sizeof(g_cfg.host_key));
  }
  p = find_key(json, "client_ca");
  if (p != NULL) {
    (void)parse_string(p, g_cfg.client_ca, (uint32_t)sizeof(g_cfg.client_ca));
  }
  /* methods: ["passwd","pubkey","sslcert"] — if present, only listed ones win. */
  if (strstr(json, "\"methods\"") != NULL) {
    g_cfg.auth_passwd = (strstr(json, "\"passwd\"") != NULL) ? 1 : 0;
    g_cfg.auth_pubkey = (strstr(json, "\"pubkey\"") != NULL) ? 1 : 0;
    g_cfg.auth_sslcert =
      (strstr(json, "\"sslcert\"") != NULL && g_cfg.client_ca[0] != '\0') ? 1 : 0;
  }
}

static int32_t ensure_authorized_keys_file(void)
{
  static const char seed[] =
    "# Metal SSH authorized_keys (OpenSSH format).\n"
    "# Add: ssh-ed25519 AAAA... comment\n"
    "# Or:  user=alice ssh-ed25519 AAAA... comment\n"
    "# Per-user: /etc/ssh/authorized_keys.d/<user>\n";
  uint32_t sz;
  uint32_t want;

  (void)pm_metal_fs_mkdir("/etc");
  (void)pm_metal_fs_mkdir("/etc/ssh");
  sz = pm_metal_fs_size(SSH_AUTHKEYS_PATH);
  if (sz > 0u && sz != (uint32_t)-1) {
    return 0;
  }
  want = (uint32_t)(sizeof(seed) - 1u);
  if (pm_metal_fs_write(SSH_AUTHKEYS_PATH, seed, want) != want) {
    return -1;
  }
  return 0;
}

static int32_t ensure_sshd_json_file(void)
{
  uint32_t sz;
  uint32_t want;

  sz = pm_metal_fs_size(SSHD_JSON_PATH);
  if (sz > 0) {
    return 0;
  }
  want = (uint32_t)(sizeof(g_sshd_json_default) - 1u);
  if (pm_metal_fs_write(SSHD_JSON_PATH, g_sshd_json_default, want) != want) {
    pm_metal_logf("sshd: seed %s failed", SSHD_JSON_PATH);
    return -1;
  }
  pm_metal_logf("sshd: seeded %s", SSHD_JSON_PATH);
  return 0;
}

int32_t pm_metal_net_ssh_cfg_load(void)
{
  uint8_t *buf;
  uint32_t sz;
  uint32_t n;

  cfg_defaults();
  (void)ensure_sshd_json_file();
  (void)ensure_authorized_keys_file();
  sz = pm_metal_fs_size(SSHD_JSON_PATH);
  if (sz == 0 || sz > SSHD_JSON_MAX) {
    parse_json(g_sshd_json_default);
    (void)pm_metal_auth_pubkey_reload();
    return 0;
  }
  buf = (uint8_t *)pm_metal_mem_alloc(sz + 1u, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (buf == NULL) {
    parse_json(g_sshd_json_default);
    (void)pm_metal_auth_pubkey_reload();
    return -1;
  }
  n = pm_metal_fs_read(SSHD_JSON_PATH, buf, sz);
  if (n == 0) {
    pm_metal_mem_free(buf);
    parse_json(g_sshd_json_default);
    (void)pm_metal_auth_pubkey_reload();
    return 0;
  }
  buf[n] = '\0';
  parse_json((const char *)buf);
  pm_metal_mem_free(buf);
  (void)pm_metal_auth_pubkey_reload();
  pm_metal_logf("sshd: loaded %s port=%u passwd=%d pubkey=%d sslcert=%d", SSHD_JSON_PATH,
                (unsigned)g_cfg.port, (int)g_cfg.auth_passwd, (int)g_cfg.auth_pubkey,
                (int)g_cfg.auth_sslcert);
  return 0;
}
