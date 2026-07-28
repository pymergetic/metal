/*
 * /etc/httpd.json — schema-specific loader (no general JSON library).
 * Missing file is seeded from the shipped default blob so BIOS (no ESP)
 * and a fresh ESP both get a useful mount table.
 */
#include "asgi_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pymergetic/metal/auth/auth.h>
#include <pymergetic/metal/net/tls/tls.h>
#include <pymergetic/metal/fs/fs.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/runtime/mem/mem.h>

#define HTTPD_JSON_PATH "/etc/httpd.json"
#define HTTPD_JSON_MAX  (16u * 1024u)

/* Mirrors mods/etc/httpd.json (keep in sync). */
static const char g_httpd_json_default[] =
  "{\n"
  "  \"port\": 8000,\n"
  "  \"tls_port\": 8443,\n"
  "  \"ifaces\": [],\n"
  "  \"tls\": {\n"
  "    \"cert\": \"/etc/httpd-cert.pem\",\n"
  "    \"key\": \"/etc/httpd-key.pem\",\n"
  "    \"client_ca\": \"\",\n"
  "    \"client_auth\": \"none\"\n"
  "  },\n"
  "  \"budget_pct\": 10,\n"
  "  \"keepalive_s\": 30,\n"
  "  \"auth\": {\n"
  "    \"default\": \"off\",\n"
  "    \"type\": \"basic\",\n"
  "    \"realm\": \"metal\",\n"
  "    \"hash\": \"argon2id\",\n"
  "    \"users\": [\n"
  "      {\n"
  "        \"user\": \"test\",\n"
  "        \"pass_hash\": \"$argon2id$v=19$m=1024,t=2,p=1$AQIDBAUGBwgJCgsMDQ4PEA==$"
  "LBzI9SPuUEmMhImURHurnrHHKsJQDcpQI3n7HZ702SY=\"\n"
  "      },\n"
  "      {\n"
  "        \"user\": \"testbcrypt\",\n"
  "        \"pass_hash\": \"$2b$04$05mgiRuks.3l02Sjz2KwgegyoxT7e6GeuuuUyJbYCcW.DIdlPvdbW\"\n"
  "      }\n"
  "    ],\n"
  "    \"public\": [\"/health\", \"/static\", \"/docs\", \"/iface\", \"/about\", \"/externals\", \"/limits\", \"/api\"]\n"
  "  },\n"
  "  \"mounts\": [\n"
  "    { \"path\": \"/health\", \"app\": \"c:health\" },\n"
  "    { \"path\": \"/static\", \"app\": \"c:static\", \"root\": \"/mods/www\" },\n"
  "    { \"path\": \"/\", \"app\": \"py:httpd\" },\n"
  "    { \"path\": \"/secure\", \"app\": \"c:health\", \"auth\": \"basic\" }\n"
  "  ]\n"
  "}\n";

static asgi_httpd_cfg_t g_cfg;

asgi_httpd_cfg_t *pm_metal_net_asgi_cfg(void)
{
  return &g_cfg;
}

static const char *skip_ws(const char *p)
{
  while (p != NULL && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
    p++;
  }
  return p;
}

/* Search for "key": within [json, end). end==NULL means to NUL. */
static const char *find_key_range(const char *json, const char *end, const char *key)
{
  size_t      klen;
  const char *p;
  const char *limit;

  if (json == NULL || key == NULL) {
    return NULL;
  }
  klen = strlen(key);
  if (klen == 0u || klen > 60u) {
    return NULL;
  }
  limit = (end != NULL) ? end : json + strlen(json);
  p     = json;
  while (p + klen + 2u <= limit) {
    const char *colon;

    if (p[0] != '"' || memcmp(p + 1, key, klen) != 0 || p[1u + klen] != '"') {
      p++;
      continue;
    }
    colon = p + klen + 2u;
    while (colon < limit && (*colon == ' ' || *colon == '\t' || *colon == '\n' || *colon == '\r')) {
      colon++;
    }
    if (colon >= limit || *colon != ':') {
      p++;
      continue;
    }
    return skip_ws(colon + 1);
  }
  return NULL;
}

static const char *find_key(const char *json, const char *key)
{
  return find_key_range(json, NULL, key);
}

static int32_t parse_u32(const char *p, uint32_t *out)
{
  char *end;
  unsigned long v;

  if (p == NULL || out == NULL) {
    return -1;
  }
  p = skip_ws(p);
  if (*p < '0' || *p > '9') {
    return -1;
  }
  v = strtoul(p, &end, 10);
  if (end == p) {
    return -1;
  }
  *out = (uint32_t)v;
  return 0;
}

static int32_t parse_string(const char *p, char *out, uint32_t out_cap)
{
  uint32_t n;

  if (p == NULL || out == NULL || out_cap == 0) {
    return -1;
  }
  p = skip_ws(p);
  if (*p != '"') {
    return -1;
  }
  p++;
  n = 0;
  while (*p != '\0' && *p != '"') {
    if (*p == '\\' && p[1] != '\0') {
      p++;
    }
    if (n + 1u >= out_cap) {
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

static const char *array_after(const char *json, const char *key)
{
  const char *p;

  p = find_key(json, key);
  if (p == NULL) {
    return NULL;
  }
  p = skip_ws(p);
  if (*p != '[') {
    return NULL;
  }
  return p + 1;
}

static const char *next_object(const char *p)
{
  p = skip_ws(p);
  while (*p == ',') {
    p = skip_ws(p + 1);
  }
  if (*p == '{') {
    return p;
  }
  return NULL;
}

static const char *object_end(const char *p)
{
  int depth;

  if (p == NULL || *p != '{') {
    return NULL;
  }
  depth = 0;
  for (; *p != '\0'; p++) {
    if (*p == '{') {
      depth++;
    } else if (*p == '}') {
      depth--;
      if (depth == 0) {
        return p + 1;
      }
    } else if (*p == '"') {
      p++;
      while (*p != '\0' && *p != '"') {
        if (*p == '\\' && p[1] != '\0') {
          p++;
        }
        p++;
      }
      if (*p == '\0') {
        return NULL;
      }
    }
  }
  return NULL;
}

static void cfg_reset_defaults(asgi_httpd_cfg_t *cfg)
{
  memset(cfg, 0, sizeof(*cfg));
  cfg->port        = 8000;
  cfg->tls_port    = 8443;
  cfg->budget_pct  = 10;
  cfg->keepalive_s = 30;
  strncpy(cfg->tls_cert, "/etc/httpd-cert.pem", sizeof(cfg->tls_cert) - 1u);
  strncpy(cfg->tls_key, "/etc/httpd-key.pem", sizeof(cfg->tls_key) - 1u);
  strncpy(cfg->realm, "metal", sizeof(cfg->realm) - 1u);
  cfg->client_auth = PM_METAL_TLS_CLIENT_AUTH_NONE;
}

static void parse_users(const char *json, asgi_httpd_cfg_t *cfg)
{
  const char *p;
  const char *obj;
  const char *end;
  char        user[PM_METAL_AUTH_USER_MAX];
  char        hash[PM_METAL_AUTH_HASH_MAX];
  const char *v;

  p = array_after(json, "users");
  if (p == NULL) {
    return;
  }
  for (;;) {
    obj = next_object(p);
    if (obj == NULL) {
      break;
    }
    end = object_end(obj);
    if (end == NULL) {
      break;
    }
    user[0] = '\0';
    hash[0] = '\0';
    v = find_key_range(obj, end, "user");
    if (v != NULL) {
      (void)parse_string(v, user, sizeof(user));
    }
    v = find_key_range(obj, end, "pass_hash");
    if (v != NULL) {
      (void)parse_string(v, hash, sizeof(hash));
    }
    if (user[0] != '\0' && hash[0] != '\0' && cfg->n_users < PM_METAL_AUTH_USERS_MAX) {
      strncpy(cfg->users[cfg->n_users].name, user, sizeof(cfg->users[0].name) - 1u);
      strncpy(cfg->users[cfg->n_users].hash, hash, sizeof(cfg->users[0].hash) - 1u);
      cfg->users[cfg->n_users].used = 1;
      cfg->n_users++;
    }
    p = end;
  }
}

static void parse_mounts(const char *json, asgi_httpd_cfg_t *cfg)
{
  const char *p;
  const char *obj;
  const char *end;
  const char *v;
  char        path[ASGI_PATH_MAX];
  char        app[64];
  char        root[ASGI_PATH_MAX];
  char        auth[16];

  p = array_after(json, "mounts");
  if (p == NULL) {
    return;
  }
  for (;;) {
    asgi_cfg_mount_t *m;

    obj = next_object(p);
    if (obj == NULL) {
      break;
    }
    end = object_end(obj);
    if (end == NULL) {
      break;
    }
    if (cfg->n_mounts >= ASGI_MOUNT_MAX) {
      break;
    }
    path[0] = '\0';
    app[0]  = '\0';
    root[0] = '\0';
    auth[0] = '\0';
    v = find_key_range(obj, end, "path");
    if (v != NULL) {
      (void)parse_string(v, path, sizeof(path));
    }
    v = find_key_range(obj, end, "app");
    if (v != NULL) {
      (void)parse_string(v, app, sizeof(app));
    }
    v = find_key_range(obj, end, "root");
    if (v != NULL) {
      (void)parse_string(v, root, sizeof(root));
    }
    v = find_key_range(obj, end, "auth");
    if (v != NULL) {
      (void)parse_string(v, auth, sizeof(auth));
    }
    if (path[0] != '\0' && app[0] != '\0') {
      m = &cfg->mounts[cfg->n_mounts++];
      memset(m, 0, sizeof(*m));
      strncpy(m->path, path, sizeof(m->path) - 1u);
      strncpy(m->app, app, sizeof(m->app) - 1u);
      strncpy(m->root, root, sizeof(m->root) - 1u);
      m->auth_basic = (strcmp(auth, "basic") == 0) ? 1 : 0;
    }
    p = end;
  }
}

static int32_t parse_httpd_json(const char *json, asgi_httpd_cfg_t *cfg)
{
  const char *v;
  char        tmp[64];

  cfg_reset_defaults(cfg);
  if (json == NULL) {
    return -1;
  }
  v = find_key(json, "port");
  if (v != NULL) {
    (void)parse_u32(v, &cfg->port);
  }
  v = find_key(json, "tls_port");
  if (v != NULL) {
    (void)parse_u32(v, &cfg->tls_port);
  }
  v = find_key(json, "budget_pct");
  if (v != NULL) {
    (void)parse_u32(v, &cfg->budget_pct);
  }
  v = find_key(json, "keepalive_s");
  if (v != NULL) {
    (void)parse_u32(v, &cfg->keepalive_s);
  }
  if (cfg->budget_pct == 0 || cfg->budget_pct > 90u) {
    cfg->budget_pct = 10;
  }
  if (cfg->keepalive_s == 0) {
    cfg->keepalive_s = 30;
  }

  v = find_key(json, "cert");
  if (v != NULL) {
    (void)parse_string(v, cfg->tls_cert, sizeof(cfg->tls_cert));
  }
  v = find_key(json, "key");
  if (v != NULL) {
    (void)parse_string(v, cfg->tls_key, sizeof(cfg->tls_key));
  }
  v = find_key(json, "client_ca");
  if (v != NULL) {
    (void)parse_string(v, cfg->tls_client_ca, sizeof(cfg->tls_client_ca));
  }
  v = find_key(json, "client_auth");
  if (v != NULL && parse_string(v, tmp, sizeof(tmp)) == 0) {
    if (strcmp(tmp, "optional") == 0) {
      cfg->client_auth = PM_METAL_TLS_CLIENT_AUTH_OPTIONAL;
    } else if (strcmp(tmp, "required") == 0) {
      cfg->client_auth = PM_METAL_TLS_CLIENT_AUTH_REQUIRED;
    } else {
      cfg->client_auth = PM_METAL_TLS_CLIENT_AUTH_NONE;
    }
  }
  v = find_key(json, "realm");
  if (v != NULL) {
    (void)parse_string(v, cfg->realm, sizeof(cfg->realm));
  }

  parse_users(json, cfg);
  parse_mounts(json, cfg);
  cfg->loaded = 1;
  return 0;
}

static const char g_www_index_default[] =
  "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
  "<title>metal</title>\n</head>\n<body>\n<h1>metal</h1>\n"
  "<p>static root /mods/www</p>\n</body>\n</html>\n";

static int32_t ensure_httpd_json_file(void)
{
  uint32_t sz;

  sz = pm_metal_fs_size(HTTPD_JSON_PATH);
  if (sz > 0) {
    return 0;
  }
  if (pm_metal_fs_write(HTTPD_JSON_PATH,
                        g_httpd_json_default,
                        (uint32_t)(sizeof(g_httpd_json_default) - 1u)) !=
      (uint32_t)(sizeof(g_httpd_json_default) - 1u)) {
    pm_metal_logf("asgi: seed %s failed", HTTPD_JSON_PATH);
    return -1;
  }
  return 0;
}

static void ensure_www_index(void)
{
  static const char path[] = "/mods/www/index.html";
  uint32_t          sz;

  sz = pm_metal_fs_size(path);
  if (sz > 0) {
    return;
  }
  (void)pm_metal_fs_write(
    path, g_www_index_default, (uint32_t)(sizeof(g_www_index_default) - 1u));
}

int32_t pm_metal_net_asgi_cfg_load(void)
{
  uint8_t *buf;
  uint32_t sz;
  uint32_t n;
  int32_t  rc;

  cfg_reset_defaults(&g_cfg);
  (void)ensure_httpd_json_file();
  ensure_www_index();
  sz = pm_metal_fs_size(HTTPD_JSON_PATH);
  if (sz == 0 || sz > HTTPD_JSON_MAX) {
    /* No usable file: blank mounts (plan). */
    g_cfg.loaded = 1;
    pm_metal_logf("asgi: %s missing/empty - no routes", HTTPD_JSON_PATH);
    return 0;
  }
  buf = (uint8_t *)pm_metal_mem_alloc(sz + 1u, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (buf == NULL) {
    return -1;
  }
  n = pm_metal_fs_read(HTTPD_JSON_PATH, buf, sz);
  if (n == 0) {
    pm_metal_mem_free(buf);
    g_cfg.loaded = 1;
    return 0;
  }
  buf[n] = '\0';
  rc     = parse_httpd_json((const char *)buf, &g_cfg);
  pm_metal_mem_free(buf);
  if (rc != 0) {
    cfg_reset_defaults(&g_cfg);
    g_cfg.loaded = 1;
    pm_metal_logf("asgi: %s parse failed - no routes", HTTPD_JSON_PATH);
    return 0;
  }
  pm_metal_logf("asgi: loaded %s mounts=%u", HTTPD_JSON_PATH, (unsigned)g_cfg.n_mounts);
  return 0;
}

pm_metal_net_asgi_app_h pm_metal_net_asgi_resolve_app(const char *app, const char *root)
{
  if (app == NULL) {
    return PM_METAL_NET_ASGI_APP_INVALID;
  }
  if (strcmp(app, "c:health") == 0) {
    return pm_metal_net_asgi_app_health();
  }
  if (strcmp(app, "c:static") == 0) {
    return pm_metal_net_asgi_app_static((root != NULL && root[0] != '\0') ? root : "/mods/www");
  }
  if (strcmp(app, "c:sysinfo") == 0) {
    return pm_metal_net_asgi_app_sysinfo();
  }
  if (strcmp(app, "py:httpd") == 0) {
    return pm_metal_net_asgi_app_httpd();
  }
  return PM_METAL_NET_ASGI_APP_INVALID;
}
