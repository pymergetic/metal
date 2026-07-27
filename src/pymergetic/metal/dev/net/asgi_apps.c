/*
 * Built-in C ASGI apps: c:health, c:static.
 */
#include "asgi_internal.h"

#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/dev/net/http_parse.h>
#include <pymergetic/metal/fs/fs.h>
#include <pymergetic/metal/runtime/mem/mem.h>
#include <pymergetic/metal/shell/hwinfo/hwinfo.h>
#include <pymergetic/metal/version.h>

typedef struct {
  pm_metal_net_sock_h sock;
  pm_metal_tls_h      tls;
  char                method[16];
  char                target[ASGI_PATH_MAX];
  char                mount_prefix[ASGI_PATH_MAX];
  const char         *static_root;
  int32_t             keepalive;
  char                hdr[ASGI_HDR_MAX];
  uint32_t            hdr_len;
} asgi_conn_t;

static asgi_conn_t g_conn;
static int32_t     g_budget_conns;

void pm_metal_net_asgi_conn_begin(pm_metal_net_sock_h sock,
                                  pm_metal_tls_h      tls,
                                  const char         *method,
                                  const char         *target,
                                  const char         *mount_prefix,
                                  const char         *static_root)
{
  int32_t ka;

  ka = g_conn.keepalive;
  memset(&g_conn, 0, sizeof(g_conn));
  g_conn.sock      = sock;
  g_conn.tls       = tls;
  g_conn.keepalive = ka;
  if (method != NULL) {
    strncpy(g_conn.method, method, sizeof(g_conn.method) - 1u);
  }
  if (target != NULL) {
    strncpy(g_conn.target, target, sizeof(g_conn.target) - 1u);
  }
  if (mount_prefix != NULL) {
    strncpy(g_conn.mount_prefix, mount_prefix, sizeof(g_conn.mount_prefix) - 1u);
  }
  g_conn.static_root = static_root;
}

void pm_metal_net_asgi_conn_set_keepalive(int32_t on)
{
  g_conn.keepalive = on ? 1 : 0;
}

int32_t pm_metal_net_asgi_conn_keepalive(void)
{
  return g_conn.keepalive;
}

void pm_metal_net_asgi_conn_set_hdr(const char *hdr, uint32_t hdr_len)
{
  if (hdr == NULL || hdr_len == 0) {
    g_conn.hdr_len = 0;
    g_conn.hdr[0]  = '\0';
    return;
  }
  if (hdr_len >= sizeof(g_conn.hdr)) {
    hdr_len = (uint32_t)sizeof(g_conn.hdr) - 1u;
  }
  memcpy(g_conn.hdr, hdr, hdr_len);
  g_conn.hdr[hdr_len] = '\0';
  g_conn.hdr_len      = hdr_len;
}

const char *pm_metal_net_asgi_conn_method(void)
{
  return g_conn.method;
}

const char *pm_metal_net_asgi_conn_target(void)
{
  return g_conn.target;
}

const char *pm_metal_net_asgi_conn_hdr(void)
{
  return g_conn.hdr;
}

uint32_t pm_metal_net_asgi_conn_hdr_len(void)
{
  return g_conn.hdr_len;
}

int32_t pm_metal_net_asgi_budget_try_enter(uint32_t budget_pct)
{
  size_t   heap;
  uint32_t max_c;

  heap = pm_metal_mem_heap_bytes();
  if (budget_pct == 0 || budget_pct > 90u) {
    budget_pct = 10;
  }
  max_c = (uint32_t)((heap / 100u) * budget_pct / ASGI_CONN_SLOT_EST);
  if (max_c < 1u) {
    max_c = 1u;
  }
  if (max_c > 32u) {
    max_c = 32u;
  }
  if ((uint32_t)g_budget_conns >= max_c) {
    return -1;
  }
  g_budget_conns++;
  return 0;
}

void pm_metal_net_asgi_budget_leave(void)
{
  if (g_budget_conns > 0) {
    g_budget_conns--;
  }
}

int32_t pm_metal_net_asgi_conn_send(const void *buf, uint32_t len)
{
  const uint8_t *p;
  uint32_t       off;

  if (buf == NULL || len == 0) {
    return 0;
  }
  p   = (const uint8_t *)buf;
  off = 0;
  while (off < len) {
    if (g_conn.tls != PM_METAL_TLS_INVALID) {
      int32_t n;

      n = pm_metal_net_tls_write(g_conn.tls, p + off, len - off);
      if (n <= 0) {
        return -1;
      }
      off += (uint32_t)n;
    } else {
      uint32_t n;

      n = pm_metal_net_send(g_conn.sock, p + off, len - off);
      if (n == 0) {
        return -1;
      }
      off += n;
    }
  }
  return 0;
}

int32_t pm_metal_net_asgi_send_simple(uint32_t    code,
                                      const char *reason,
                                      const char *ctype,
                                      const char *body)
{
  char     hdr[512];
  int32_t  n;
  uint32_t blen;
  char     cl[16];

  blen = (body != NULL) ? (uint32_t)strlen(body) : 0;
  n    = pm_metal_http_fmt_status(hdr, sizeof(hdr), code, reason);
  if (n < 0) {
    return -1;
  }
  n = pm_metal_http_hdr_append(hdr, sizeof(hdr), (uint32_t)n, "Server", "metal-asgi");
  if (n < 0) {
    return -1;
  }
  if (ctype != NULL) {
    n = pm_metal_http_hdr_append(hdr, sizeof(hdr), (uint32_t)n, "Content-Type", ctype);
    if (n < 0) {
      return -1;
    }
  }
  snprintf(cl, sizeof(cl), "%u", (unsigned)blen);
  n = pm_metal_http_hdr_append(hdr, sizeof(hdr), (uint32_t)n, "Content-Length", cl);
  if (n < 0) {
    return -1;
  }
  n = pm_metal_http_hdr_append(hdr,
                               sizeof(hdr),
                               (uint32_t)n,
                               "Connection",
                               g_conn.keepalive ? "keep-alive" : "close");
  if (n < 0) {
    return -1;
  }
  n = pm_metal_http_hdr_end(hdr, sizeof(hdr), (uint32_t)n);
  if (n < 0) {
    return -1;
  }
  if (pm_metal_net_asgi_conn_send(hdr, (uint32_t)n) != 0) {
    return -1;
  }
  if (blen > 0) {
    return pm_metal_net_asgi_conn_send(body, blen);
  }
  return 0;
}

static int32_t health_fn(void *ctx, uint32_t conn_id)
{
  (void)ctx;
  (void)conn_id;
  return pm_metal_net_asgi_send_simple(200, "OK", "text/plain", "ok\n");
}

static int32_t sysinfo_fn(void *ctx, uint32_t conn_id)
{
  char body[320];
  char cpu[96];

  (void)ctx;
  (void)conn_id;
  cpu[0] = '\0';
  pm_metal_hwinfo_cpu_brand(cpu, sizeof(cpu));
  /* Keep this free of Python locks -- runs inside the accept coro. */
  snprintf(body, sizeof(body),
           "{\"runtime\":\"metal\",\"version\":\"%s\",\"cpu\":\"%s\"}\n", PM_METAL_VERSION,
           cpu[0] != '\0' ? cpu : "unknown");
  return pm_metal_net_asgi_send_simple(200, "OK", "application/json", body);
}

static void path_join(char *out, uint32_t out_cap, const char *root, const char *rel)
{
  uint32_t rl;
  uint32_t i;

  if (out == NULL || out_cap == 0) {
    return;
  }
  out[0] = '\0';
  if (root == NULL) {
    root = "";
  }
  if (rel == NULL) {
    rel = "";
  }
  while (*rel == '/') {
    rel++;
  }
  for (i = 0; rel[i] != '\0'; i++) {
    if (rel[i] == '.' && rel[i + 1] == '.' && (rel[i + 2] == '/' || rel[i + 2] == '\0')) {
      return;
    }
  }
  rl = (uint32_t)strlen(root);
  if (rl + 1u + (uint32_t)strlen(rel) + 1u > out_cap) {
    return;
  }
  memcpy(out, root, rl);
  if (rl > 0 && root[rl - 1] != '/') {
    out[rl++] = '/';
  }
  memcpy(out + rl, rel, strlen(rel) + 1);
}

static const char *mime_for(const char *path)
{
  const char *dot;

  dot = strrchr(path, '.');
  if (dot == NULL) {
    return "application/octet-stream";
  }
  if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0) {
    return "text/html";
  }
  if (strcmp(dot, ".css") == 0) {
    return "text/css";
  }
  if (strcmp(dot, ".js") == 0) {
    return "application/javascript";
  }
  if (strcmp(dot, ".png") == 0) {
    return "image/png";
  }
  if (strcmp(dot, ".txt") == 0) {
    return "text/plain";
  }
  if (strcmp(dot, ".json") == 0) {
    return "application/json";
  }
  return "application/octet-stream";
}

static int32_t static_fn(void *ctx, uint32_t conn_id)
{
  char               path[ASGI_PATH_MAX];
  char               rel[ASGI_PATH_MAX];
  uint32_t           plen;
  const char        *root;
  pm_metal_fs_stat_t st;
  pm_metal_fs_h      fh;
  uint8_t            buf[ASGI_IO_MAX];
  uint32_t           n;
  uint32_t           left;
  char               hdr[512];
  int32_t            hn;
  char               cl[16];

  (void)ctx;
  (void)conn_id;
  root = g_conn.static_root;
  if (root == NULL || root[0] == '\0') {
    return pm_metal_net_asgi_send_simple(404, "Not Found", "text/plain", "no static root\n");
  }
  plen = (uint32_t)strlen(g_conn.mount_prefix);
  if (strncmp(g_conn.target, g_conn.mount_prefix, plen) != 0) {
    return pm_metal_net_asgi_send_simple(404, "Not Found", "text/plain", "bad path\n");
  }
  strncpy(rel, g_conn.target + plen, sizeof(rel) - 1u);
  rel[sizeof(rel) - 1u] = '\0';
  if (rel[0] == '\0' || strcmp(rel, "/") == 0) {
    strncpy(rel, "index.html", sizeof(rel) - 1u);
  }
  path_join(path, sizeof(path), root, rel);
  if (path[0] == '\0') {
    return pm_metal_net_asgi_send_simple(403, "Forbidden", "text/plain", "forbidden\n");
  }
  if (pm_metal_fs_stat(path, &st) != 0 || st.type != PM_METAL_FS_TYPE_FILE) {
    return pm_metal_net_asgi_send_simple(404, "Not Found", "text/plain", "not found\n");
  }

  hn = pm_metal_http_fmt_status(hdr, sizeof(hdr), 200, "OK");
  if (hn < 0) {
    return -1;
  }
  hn = pm_metal_http_hdr_append(hdr, sizeof(hdr), (uint32_t)hn, "Server", "metal-asgi");
  if (hn < 0) {
    return -1;
  }
  hn = pm_metal_http_hdr_append(hdr, sizeof(hdr), (uint32_t)hn, "Content-Type", mime_for(path));
  if (hn < 0) {
    return -1;
  }
  snprintf(cl, sizeof(cl), "%u", (unsigned)st.size);
  hn = pm_metal_http_hdr_append(hdr, sizeof(hdr), (uint32_t)hn, "Content-Length", cl);
  if (hn < 0) {
    return -1;
  }
  hn = pm_metal_http_hdr_append(hdr,
                                sizeof(hdr),
                                (uint32_t)hn,
                                "Connection",
                                g_conn.keepalive ? "keep-alive" : "close");
  if (hn < 0) {
    return -1;
  }
  hn = pm_metal_http_hdr_end(hdr, sizeof(hdr), (uint32_t)hn);
  if (hn < 0) {
    return -1;
  }
  if (pm_metal_net_asgi_conn_send(hdr, (uint32_t)hn) != 0) {
    return -1;
  }

  fh = pm_metal_fs_open(path, PM_METAL_FS_O_RDONLY);
  if (fh == PM_METAL_FS_INVALID) {
    return -1;
  }
  left = st.size;
  while (left > 0) {
    uint32_t want;

    want = left > sizeof(buf) ? (uint32_t)sizeof(buf) : left;
    n    = pm_metal_fs_fread(fh, buf, want);
    if (n == 0) {
      pm_metal_fs_close(fh);
      return -1;
    }
    if (pm_metal_net_asgi_conn_send(buf, n) != 0) {
      pm_metal_fs_close(fh);
      return -1;
    }
    left -= n;
  }
  pm_metal_fs_close(fh);
  return 0;
}

pm_metal_net_asgi_app_h pm_metal_net_asgi_app_health(void)
{
  static pm_metal_net_asgi_app_h h;

  if (h == PM_METAL_NET_ASGI_APP_INVALID) {
    h = pm_metal_net_asgi_register_c(health_fn, NULL);
  }
  return h;
}

pm_metal_net_asgi_app_h pm_metal_net_asgi_app_sysinfo(void)
{
  static pm_metal_net_asgi_app_h h;

  if (h == PM_METAL_NET_ASGI_APP_INVALID) {
    h = pm_metal_net_asgi_register_c(sysinfo_fn, NULL);
  }
  return h;
}

pm_metal_net_asgi_app_h pm_metal_net_asgi_app_static(const char *root)
{
  static char                    root_buf[ASGI_PATH_MAX];
  static pm_metal_net_asgi_app_h h;

  if (root != NULL) {
    strncpy(root_buf, root, sizeof(root_buf) - 1u);
    root_buf[sizeof(root_buf) - 1u] = '\0';
  } else if (root_buf[0] == '\0') {
    strncpy(root_buf, "/mods/www", sizeof(root_buf) - 1u);
  }
  if (h == PM_METAL_NET_ASGI_APP_INVALID) {
    h = pm_metal_net_asgi_register_c(static_fn, root_buf);
  }
  return h;
}

pm_metal_net_asgi_app_h pm_metal_net_asgi_app_microdot(void)
{
  static pm_metal_net_asgi_app_h h;

  if (h != PM_METAL_NET_ASGI_APP_INVALID) {
    return h;
  }
  /* Cookie 0: server resolves metal_asgi_launcher.handle on first use. */
  h = pm_metal_net_asgi_register_py(0);
  if (h == PM_METAL_NET_ASGI_APP_INVALID) {
    return pm_metal_net_asgi_app_sysinfo();
  }
  return h;
}

int32_t pm_metal_net_asgi_dispatch_c(asgi_app_slot_t *slot,
                                     uint32_t         conn_id,
                                     const char      *method,
                                     const char      *target,
                                     const char      *hdr,
                                     uint32_t         hdr_len)
{
  (void)hdr;
  (void)hdr_len;
  if (slot == NULL || slot->kind != PM_METAL_NET_ASGI_RUNNER_C || slot->c_fn == NULL) {
    return -1;
  }
  (void)method;
  (void)target;
  return slot->c_fn(slot->c_ctx, conn_id);
}
