/*
 * ASGI HTTP/1.1 server — listen/accept/dispatch (cleartext + TLS).
 */
#include "asgi_internal.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/auth/auth.h>
#include <pymergetic/metal/dev/net/http_parse.h>
#include <pymergetic/metal/dev/net/tls.h>
#include <pymergetic/metal/guest/mod/mod.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/runtime/mem/mem.h>

#include "wasm_export.h"

typedef enum {
  ASGI_ST_LISTEN = 0,
  ASGI_ST_LISTEN_AW,
  ASGI_ST_ACCEPT,
  ASGI_ST_ACCEPT_AW,
  ASGI_ST_TLS_OPEN,
  ASGI_ST_TLS_HS,
  ASGI_ST_TLS_WIRE_AW,
  ASGI_ST_CONN_RECV,
  ASGI_ST_CONN_RECV_AW,
  ASGI_ST_CONN_HANDLE,
  ASGI_ST_PY_IMPORT_AW,
  ASGI_ST_PY_WAIT_IMPORT,
  ASGI_ST_PY_CALL_AW,
  ASGI_ST_WASM_AW,
  ASGI_ST_WS_RECV,
  ASGI_ST_WS_RECV_AW
} asgi_step_t;

typedef struct {
  asgi_step_t             step;
  pm_metal_net_asgi_srv_h srv_h;
  pm_metal_async_handle_t aw;
  pm_metal_net_sock_h     csock;
  pm_metal_tls_h          tls_h;
  pm_metal_tls_wire_t     wire;
  int32_t                 use_tls;
  int32_t                 in_budget;
  int32_t                 keepalive;
  int32_t                 http_ver; /* minor: 0 or 1 */
  char                    hdr[ASGI_HDR_MAX];
  uint32_t                hdr_len;
  uint8_t                 iobuf[ASGI_IO_MAX];
  pm_metal_py_fn_h_t      py_fn;
  asgi_app_slot_t        *py_slot;
  pm_metal_mod_fn_t       wasm_fn;
} asgi_listen_t;

static pm_metal_py_fn_h_t g_microdot_fn;
static int32_t            g_microdot_importing;

static asgi_srv_t g_srvs[ASGI_SRV_MAX];

asgi_srv_t *pm_metal_net_asgi_srv_slot(pm_metal_net_asgi_srv_h h)
{
  if (h == 0 || h >= ASGI_SRV_MAX || !g_srvs[h].used) {
    return NULL;
  }
  return &g_srvs[h];
}

static pm_metal_net_asgi_srv_h srv_alloc(void)
{
  uint32_t i;

  for (i = 1; i < ASGI_SRV_MAX; i++) {
    if (!g_srvs[i].used) {
      memset(&g_srvs[i], 0, sizeof(g_srvs[i]));
      g_srvs[i].used = 1;
      return i;
    }
  }
  return PM_METAL_NET_ASGI_SRV_INVALID;
}

static asgi_mount_t *mount_find(asgi_srv_t *srv, const char *target)
{
  asgi_mount_t *best;
  uint32_t      best_len;
  uint32_t      i;

  best     = NULL;
  best_len = 0;
  for (i = 0; i < ASGI_MOUNT_MAX; i++) {
    uint32_t plen;

    if (!srv->mounts[i].used) {
      continue;
    }
    plen = (uint32_t)strlen(srv->mounts[i].path);
    if (plen == 0) {
      continue;
    }
    if (strcmp(srv->mounts[i].path, "/") == 0) {
      if (best == NULL) {
        best     = &srv->mounts[i];
        best_len = 1;
      }
      continue;
    }
    if (strncmp(target, srv->mounts[i].path, plen) == 0 &&
        (target[plen] == '\0' || target[plen] == '/') && plen >= best_len) {
      best     = &srv->mounts[i];
      best_len = plen;
    }
  }
  return best;
}

static void conn_cleanup(asgi_listen_t *st)
{
  if (st->tls_h != PM_METAL_TLS_INVALID) {
    pm_metal_net_tls_close(st->tls_h);
    st->tls_h = PM_METAL_TLS_INVALID;
  }
  if (st->csock != PM_METAL_NET_SOCK_INVALID) {
    pm_metal_net_close(st->csock);
    st->csock = PM_METAL_NET_SOCK_INVALID;
  }
  if (st->in_budget) {
    pm_metal_net_asgi_budget_leave();
    st->in_budget = 0;
  }
  st->use_tls   = 0;
  st->keepalive = 0;
  st->hdr_len   = 0;
  st->py_slot   = NULL;
  st->py_fn     = PM_METAL_PY_FN_H_INVALID;
}

static int32_t want_keepalive(asgi_listen_t *st, uint32_t ver_minor)
{
  char conn[64];

  st->http_ver = (int32_t)ver_minor;
  if (pm_metal_http_hdr_get(st->hdr, st->hdr_len, "Connection", conn, sizeof(conn)) == 0) {
    if (strstr(conn, "close") != NULL || strstr(conn, "Close") != NULL) {
      return 0;
    }
    if (strstr(conn, "keep-alive") != NULL || strstr(conn, "Keep-Alive") != NULL) {
      return 1;
    }
  }
  /* HTTP/1.1 default keep-alive; HTTP/1.0 default close. */
  return (ver_minor >= 1u) ? 1 : 0;
}

static void send_401(asgi_listen_t *st, const char *path, const char *body)
{
  char              hdr[512];
  int32_t           n;
  char              realm_hdr[96];
  char              cl[16];
  uint32_t          blen;
  asgi_httpd_cfg_t *cfg;

  if (body == NULL) {
    body = "auth failed\n";
  }
  blen = (uint32_t)strlen(body);
  cfg  = pm_metal_net_asgi_cfg();
  pm_metal_net_asgi_conn_begin(st->csock, st->tls_h, "GET", path, path, NULL);
  pm_metal_net_asgi_conn_set_keepalive(0);
  n = pm_metal_http_fmt_status(hdr, sizeof(hdr), 401, "Unauthorized");
  if (n < 0) {
    return;
  }
  n = pm_metal_http_hdr_append(hdr, sizeof(hdr), (uint32_t)n, "Server", "metal-asgi");
  if (n < 0) {
    return;
  }
  snprintf(realm_hdr,
           sizeof(realm_hdr),
           "Basic realm=\"%s\"",
           (cfg != NULL && cfg->realm[0] != '\0') ? cfg->realm : "metal");
  n = pm_metal_http_hdr_append(hdr, sizeof(hdr), (uint32_t)n, "WWW-Authenticate", realm_hdr);
  if (n < 0) {
    return;
  }
  n = pm_metal_http_hdr_append(hdr, sizeof(hdr), (uint32_t)n, "Content-Type", "text/plain");
  if (n < 0) {
    return;
  }
  snprintf(cl, sizeof(cl), "%u", (unsigned)blen);
  n = pm_metal_http_hdr_append(hdr, sizeof(hdr), (uint32_t)n, "Content-Length", cl);
  if (n < 0) {
    return;
  }
  n = pm_metal_http_hdr_append(hdr, sizeof(hdr), (uint32_t)n, "Connection", "close");
  if (n < 0) {
    return;
  }
  n = pm_metal_http_hdr_end(hdr, sizeof(hdr), (uint32_t)n);
  if (n < 0) {
    return;
  }
  (void)pm_metal_net_asgi_conn_send(hdr, (uint32_t)n);
  (void)pm_metal_net_asgi_conn_send(body, blen);
}

/*
 * Returns:
 *  0  = finished sync (C / error response)
 *  1  = need Py import await
 *  2  = need Py call await
 *  3  = need Wasm mod coro await
 *  4  = WebSocket echo loop
 */
static int32_t handle_conn(asgi_srv_t *srv, asgi_listen_t *st)
{
  char             method[16];
  char             target[ASGI_PATH_MAX];
  uint32_t         ver;
  asgi_mount_t    *m;
  asgi_app_slot_t *slot;
  const char      *static_root;

  if (pm_metal_http_parse_request_line(
        st->hdr, st->hdr_len, method, sizeof(method), target, sizeof(target), &ver) != 0) {
    pm_metal_net_asgi_conn_begin(st->csock, st->tls_h, NULL, NULL, NULL, NULL);
    pm_metal_net_asgi_conn_set_keepalive(0);
    (void)pm_metal_net_asgi_send_simple(400, "Bad Request", "text/plain", "bad request\n");
    st->keepalive = 0;
    return 0;
  }
  st->keepalive = want_keepalive(st, ver);
  if (pm_metal_net_asgi_ws_wanted(st->hdr, st->hdr_len)) {
    pm_metal_net_asgi_conn_begin(st->csock, st->tls_h, method, target, NULL, NULL);
    pm_metal_net_asgi_conn_set_keepalive(0);
    if (pm_metal_net_asgi_ws_handshake(st->hdr, st->hdr_len) != 0) {
      (void)pm_metal_net_asgi_send_simple(400, "Bad Request", "text/plain", "ws fail\n");
      st->keepalive = 0;
      return 0;
    }
    st->keepalive = 0;
    return 4; /* WS echo loop */
  }
  m = mount_find(srv, target);
  if (m == NULL) {
    pm_metal_net_asgi_conn_begin(st->csock, st->tls_h, method, target, NULL, NULL);
    pm_metal_net_asgi_conn_set_keepalive(st->keepalive);
    (void)pm_metal_net_asgi_send_simple(404, "Not Found", "text/plain", "no routes\n");
    return 0;
  }
  if (m->auth_basic) {
    char        auth[256];
    char        user[PM_METAL_AUTH_USER_MAX];
    char        pass[96];
    const char *v;

    if (pm_metal_http_hdr_get(st->hdr, st->hdr_len, "Authorization", auth, sizeof(auth)) != 0) {
      send_401(st, m->path, "auth required\n");
      st->keepalive = 0;
      return 0;
    }
    v = auth;
    if (strncmp(v, "Basic ", 6) == 0 || strncmp(v, "basic ", 6) == 0) {
      v += 6;
    }
    if (pm_metal_auth_basic_decode(v, user, sizeof(user), pass, sizeof(pass)) != 0 ||
        pm_metal_auth_user_check(user, pass) == 0) {
      send_401(st, m->path, "auth failed\n");
      st->keepalive = 0;
      return 0;
    }
  }

  slot = pm_metal_net_asgi_app_slot(m->app);
  if (slot == NULL) {
    pm_metal_net_asgi_conn_begin(st->csock, st->tls_h, method, target, m->path, NULL);
    pm_metal_net_asgi_conn_set_keepalive(st->keepalive);
    (void)pm_metal_net_asgi_send_simple(500, "Error", "text/plain", "bad app\n");
    return 0;
  }
  static_root = NULL;
  if (slot->kind == PM_METAL_NET_ASGI_RUNNER_C && slot->c_ctx != NULL) {
    static_root = (const char *)slot->c_ctx;
  }
  pm_metal_net_asgi_conn_begin(st->csock, st->tls_h, method, target, m->path, static_root);
  pm_metal_net_asgi_conn_set_keepalive(st->keepalive);
  pm_metal_net_asgi_conn_set_hdr(st->hdr, st->hdr_len);
  if (slot->kind == PM_METAL_NET_ASGI_RUNNER_C) {
    if (pm_metal_net_asgi_dispatch_c(slot, 0, method, target, st->hdr, st->hdr_len) != 0) {
      (void)pm_metal_net_asgi_send_simple(500, "Error", "text/plain", "app fail\n");
    }
    return 0;
  }
  if (slot->kind == PM_METAL_NET_ASGI_RUNNER_PY) {
    st->py_slot = slot;
    if (slot->py_cookie != 0) {
      st->py_fn = (pm_metal_py_fn_h_t)slot->py_cookie;
      return 2;
    }
    if (g_microdot_fn != PM_METAL_PY_FN_H_INVALID) {
      st->py_fn       = g_microdot_fn;
      slot->py_cookie = (uint32_t)g_microdot_fn;
      return 2;
    }
    if (!pm_metal_py_ready()) {
      (void)pm_metal_net_asgi_send_simple(503, "Unavailable", "text/plain", "py not ready\n");
      return 0;
    }
    return 1;
  }
  if (slot->kind == PM_METAL_NET_ASGI_RUNNER_WASM) {
    memset(&st->wasm_fn, 0, sizeof(st->wasm_fn));
    if (pm_metal_mod_func_resolve(slot->wasm_mod, slot->wasm_func, &st->wasm_fn) != 0) {
      (void)pm_metal_net_asgi_send_simple(502, "Bad Gateway", "text/plain", "wasm app missing\n");
      return 0;
    }
    return 3;
  }
  (void)pm_metal_net_asgi_send_simple(501, "Not Implemented", "text/plain", "runner not ready\n");
  return 0;
}

static pm_metal_status_t AsgiListenStep(pm_metal_async_handle_t self_h)
{
  asgi_listen_t *st;
  asgi_srv_t    *srv;
  uint32_t       r;

  st = (asgi_listen_t *)pm_metal_async_coro_state(self_h);
  if (st == NULL) {
    return PM_METAL_ERROR;
  }
  srv = pm_metal_net_asgi_srv_slot(st->srv_h);
  if (srv == NULL) {
    return PM_METAL_ERROR;
  }

  for (;;) {
    switch (st->step) {
    case ASGI_ST_LISTEN:
      st->aw   = pm_metal_net_listen(srv->listen_sock, srv->port);
      st->step = ASGI_ST_LISTEN_AW;
      return pm_metal_async_await(self_h, st->aw);

    case ASGI_ST_LISTEN_AW:
      r = pm_metal_async_result_u32(self_h);
      if (r != 1u) {
        pm_metal_logf("asgi: listen failed port=%u", (unsigned)srv->port);
        return PM_METAL_ERROR;
      }
      pm_metal_logf("asgi: listening :%u%s",
                    (unsigned)srv->port,
                    srv->creds != PM_METAL_TLS_CREDS_INVALID ? " tls" : "");
      st->step = ASGI_ST_ACCEPT;
      break;

    case ASGI_ST_ACCEPT:
      st->aw = pm_metal_net_accept(srv->listen_sock);
      if (st->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
        pm_metal_logf("asgi: accept start failed sock=%u", (unsigned)srv->listen_sock);
        return PM_METAL_ERROR;
      }
      st->step = ASGI_ST_ACCEPT_AW;
      return pm_metal_async_await(self_h, st->aw);

    case ASGI_ST_ACCEPT_AW:
      st->csock = (pm_metal_net_sock_h)pm_metal_async_result_u32(self_h);
      if (st->csock == PM_METAL_NET_SOCK_INVALID) {
        st->step = ASGI_ST_ACCEPT;
        break;
      }
      if (pm_metal_net_asgi_budget_try_enter(srv->budget_pct) != 0) {
        pm_metal_net_asgi_conn_begin(st->csock, PM_METAL_TLS_INVALID, NULL, NULL, NULL, NULL);
        pm_metal_net_asgi_conn_set_keepalive(0);
        (void)pm_metal_net_asgi_send_simple(
          503, "Unavailable", "text/plain", "budget\n");
        pm_metal_net_close(st->csock);
        st->csock = PM_METAL_NET_SOCK_INVALID;
        st->step  = ASGI_ST_ACCEPT;
        break;
      }
      st->in_budget = 1;
      st->hdr_len   = 0;
      st->tls_h     = PM_METAL_TLS_INVALID;
      st->keepalive = 0;
      st->use_tls   = (srv->creds != PM_METAL_TLS_CREDS_INVALID) ? 1 : 0;
      st->step      = st->use_tls ? ASGI_ST_TLS_OPEN : ASGI_ST_CONN_RECV;
      break;

    case ASGI_ST_TLS_OPEN:
      st->tls_h = pm_metal_net_tls_open_server(srv->creds);
      if (st->tls_h == PM_METAL_TLS_INVALID ||
          pm_metal_net_tls_bind_server(st->tls_h, st->csock, &st->wire) != 0) {
        conn_cleanup(st);
        st->step = ASGI_ST_ACCEPT;
        break;
      }
      st->step = ASGI_ST_TLS_HS;
      break;

    case ASGI_ST_TLS_HS: {
      int32_t he;

      he = pm_metal_net_tls_handshake_step(st->tls_h);
      if (he == 0) {
        st->step = ASGI_ST_CONN_RECV;
        break;
      }
      if (he < 0) {
        conn_cleanup(st);
        st->step = ASGI_ST_ACCEPT;
        break;
      }
      st->wire.len = 0;
      st->wire.off = 0;
      st->aw       = pm_metal_net_recv(st->csock, st->wire.buf, sizeof(st->wire.buf));
      st->step     = ASGI_ST_TLS_WIRE_AW;
      return pm_metal_async_await(self_h, st->aw);
    }

    case ASGI_ST_TLS_WIRE_AW:
      r = pm_metal_async_result_u32(self_h);
      if (r == 0) {
        conn_cleanup(st);
        st->step = ASGI_ST_ACCEPT;
        break;
      }
      st->wire.len = r;
      st->wire.off = 0;
      if (!pm_metal_net_tls_handshake_done(st->tls_h)) {
        st->step = ASGI_ST_TLS_HS;
      } else {
        st->step = ASGI_ST_CONN_RECV;
      }
      break;

    case ASGI_ST_CONN_RECV:
      if (st->use_tls) {
        int32_t  e;
        uint32_t room;

        room = (uint32_t)(sizeof(st->hdr) - 1u - st->hdr_len);
        if (room == 0) {
          conn_cleanup(st);
          st->step = ASGI_ST_ACCEPT;
          break;
        }
        e = pm_metal_net_tls_read(st->tls_h, st->hdr + st->hdr_len, room);
        if (e > 0) {
          st->hdr_len += (uint32_t)e;
          st->hdr[st->hdr_len] = '\0';
          st->step             = ASGI_ST_CONN_HANDLE;
          break;
        }
        if (e == 0) {
          conn_cleanup(st);
          st->step = ASGI_ST_ACCEPT;
          break;
        }
        if (e == PM_METAL_TLS_WANT_READ || e == PM_METAL_TLS_WANT_WRITE) {
          st->wire.len = 0;
          st->wire.off = 0;
          st->aw       = pm_metal_net_recv(st->csock, st->wire.buf, sizeof(st->wire.buf));
          st->step     = ASGI_ST_CONN_RECV_AW;
          return pm_metal_async_await(self_h, st->aw);
        }
        conn_cleanup(st);
        st->step = ASGI_ST_ACCEPT;
        break;
      }
      st->aw   = pm_metal_net_recv(st->csock, st->iobuf, sizeof(st->iobuf));
      st->step = ASGI_ST_CONN_RECV_AW;
      return pm_metal_async_await(self_h, st->aw);

    case ASGI_ST_CONN_RECV_AW: {
      int32_t he;

      r = pm_metal_async_result_u32(self_h);
      if (r == 0) {
        conn_cleanup(st);
        st->step = ASGI_ST_ACCEPT;
        break;
      }
      if (st->use_tls) {
        st->wire.len = r;
        st->wire.off = 0;
        st->step     = ASGI_ST_CONN_RECV;
        break;
      }
      if (st->hdr_len + r > sizeof(st->hdr) - 1u) {
        r = (uint32_t)(sizeof(st->hdr) - 1u - st->hdr_len);
      }
      memcpy(st->hdr + st->hdr_len, st->iobuf, r);
      st->hdr_len += r;
      st->hdr[st->hdr_len] = '\0';
      he                   = pm_metal_http_find_hdr_end(st->hdr, st->hdr_len);
      if (he < 0) {
        st->step = ASGI_ST_CONN_RECV;
        break;
      }
      st->step = ASGI_ST_CONN_HANDLE;
      break;
    }

    case ASGI_ST_CONN_HANDLE: {
      int32_t he;
      int32_t hr;

      if (st->use_tls) {
        he = pm_metal_http_find_hdr_end(st->hdr, st->hdr_len);
        if (he < 0) {
          st->step = ASGI_ST_CONN_RECV;
          break;
        }
      }
      hr = handle_conn(srv, st);
      if (hr == 1) {
        if (g_microdot_importing) {
          st->aw   = pm_metal_async_sleep_us(2000);
          st->step = ASGI_ST_PY_WAIT_IMPORT;
          return pm_metal_async_await(self_h, st->aw);
        }
        g_microdot_importing = 1;
        pm_metal_logf("asgi: py import metal_asgi_launcher");
        st->aw = pm_metal_py_run_str("import metal_asgi_launcher\n");
        if (st->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
          g_microdot_importing = 0;
          (void)pm_metal_net_asgi_send_simple(
            500, "Error", "text/plain", "py import fail\n");
          conn_cleanup(st);
          st->step = ASGI_ST_ACCEPT;
          break;
        }
        st->step = ASGI_ST_PY_IMPORT_AW;
        /* py_run_str returns a TASK handle, not a coro. */
        return pm_metal_async_await_task(self_h, st->aw);
      }
      if (hr == 2) {
        st->aw = pm_metal_py_fn_call_async(st->py_fn, 0);
        if (st->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
          (void)pm_metal_net_asgi_send_simple(500, "Error", "text/plain", "py call fail\n");
          conn_cleanup(st);
          st->step = ASGI_ST_ACCEPT;
          break;
        }
        st->step = ASGI_ST_PY_CALL_AW;
        return pm_metal_async_await_task(self_h, st->aw);
      }
      if (hr == 3) {
        st->aw = pm_metal_mod_fn_coro(&st->wasm_fn);
        if (st->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
          (void)pm_metal_net_asgi_send_simple(500, "Error", "text/plain", "wasm coro fail\n");
          conn_cleanup(st);
          st->step = ASGI_ST_ACCEPT;
          break;
        }
        st->step = ASGI_ST_WASM_AW;
        return pm_metal_async_await(self_h, st->aw);
      }
      if (hr == 4) {
        st->hdr_len = 0;
        st->step    = ASGI_ST_WS_RECV;
        break;
      }
      /* Keepalive parks the single accept coro; close until per-conn coros exist. */
      st->keepalive = 0;
      conn_cleanup(st);
      st->step = ASGI_ST_ACCEPT;
      break;
    }

    case ASGI_ST_PY_IMPORT_AW:
      g_microdot_importing = 0;
      pm_metal_logf("asgi: py import done");
      g_microdot_fn = pm_metal_py_fn_resolve("metal_asgi_launcher.handle");
      if (g_microdot_fn == PM_METAL_PY_FN_H_INVALID) {
        pm_metal_logf("asgi: py resolve fail (metal_asgi_launcher.handle)");
        (void)pm_metal_net_asgi_send_simple(
          500, "Error", "text/plain", "py resolve fail\n");
        conn_cleanup(st);
        st->step = ASGI_ST_ACCEPT;
        break;
      }
      if (st->py_slot != NULL) {
        st->py_slot->py_cookie = (uint32_t)g_microdot_fn;
      }
      st->py_fn = g_microdot_fn;
      st->aw    = pm_metal_py_fn_call_async(st->py_fn, 0);
      if (st->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
        (void)pm_metal_net_asgi_send_simple(500, "Error", "text/plain", "py call fail\n");
        conn_cleanup(st);
        st->step = ASGI_ST_ACCEPT;
        break;
      }
      st->step = ASGI_ST_PY_CALL_AW;
      return pm_metal_async_await_task(self_h, st->aw);

    case ASGI_ST_PY_CALL_AW:
    case ASGI_ST_WASM_AW:
      st->keepalive = 0;
      conn_cleanup(st);
      st->step = ASGI_ST_ACCEPT;
      break;

    case ASGI_ST_WS_RECV:
      if (st->use_tls) {
        int32_t e;

        e = pm_metal_net_tls_read(st->tls_h, st->iobuf, sizeof(st->iobuf));
        if (e > 0) {
          uint8_t  out[ASGI_IO_MAX];
          uint32_t olen;
          int32_t  er;

          er = pm_metal_net_asgi_ws_echo_frame(st->iobuf, (uint32_t)e, out, sizeof(out), &olen);
          if (er < 0) {
            conn_cleanup(st);
            st->step = ASGI_ST_ACCEPT;
            break;
          }
          (void)pm_metal_net_asgi_conn_send(out, olen);
          if (er == 1) {
            conn_cleanup(st);
            st->step = ASGI_ST_ACCEPT;
            break;
          }
          break;
        }
        if (e == PM_METAL_TLS_WANT_READ || e == PM_METAL_TLS_WANT_WRITE) {
          st->wire.len = 0;
          st->wire.off = 0;
          st->aw       = pm_metal_net_recv(st->csock, st->wire.buf, sizeof(st->wire.buf));
          st->step     = ASGI_ST_WS_RECV_AW;
          return pm_metal_async_await(self_h, st->aw);
        }
        conn_cleanup(st);
        st->step = ASGI_ST_ACCEPT;
        break;
      }
      st->aw   = pm_metal_net_recv(st->csock, st->iobuf, sizeof(st->iobuf));
      st->step = ASGI_ST_WS_RECV_AW;
      return pm_metal_async_await(self_h, st->aw);

    case ASGI_ST_WS_RECV_AW:
      r = pm_metal_async_result_u32(self_h);
      if (r == 0) {
        conn_cleanup(st);
        st->step = ASGI_ST_ACCEPT;
        break;
      }
      if (st->use_tls) {
        st->wire.len = r;
        st->wire.off = 0;
        st->step     = ASGI_ST_WS_RECV;
        break;
      }
      {
        uint8_t  out[ASGI_IO_MAX];
        uint32_t olen;
        int32_t  er;

        er = pm_metal_net_asgi_ws_echo_frame(st->iobuf, r, out, sizeof(out), &olen);
        if (er < 0) {
          conn_cleanup(st);
          st->step = ASGI_ST_ACCEPT;
          break;
        }
        (void)pm_metal_net_asgi_conn_send(out, olen);
        if (er == 1) {
          conn_cleanup(st);
          st->step = ASGI_ST_ACCEPT;
          break;
        }
      }
      st->step = ASGI_ST_WS_RECV;
      break;

    case ASGI_ST_PY_WAIT_IMPORT:
      if (g_microdot_fn != PM_METAL_PY_FN_H_INVALID) {
        if (st->py_slot != NULL) {
          st->py_slot->py_cookie = (uint32_t)g_microdot_fn;
        }
        st->py_fn = g_microdot_fn;
        st->aw    = pm_metal_py_fn_call_async(st->py_fn, 0);
        if (st->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
          (void)pm_metal_net_asgi_send_simple(500, "Error", "text/plain", "py call fail\n");
          conn_cleanup(st);
          st->step = ASGI_ST_ACCEPT;
          break;
        }
        st->step = ASGI_ST_PY_CALL_AW;
        return pm_metal_async_await_task(self_h, st->aw);
      }
      if (g_microdot_importing) {
        st->aw = pm_metal_async_sleep_us(2000);
        return pm_metal_async_await(self_h, st->aw);
      }
      (void)pm_metal_net_asgi_send_simple(500, "Error", "text/plain", "py import fail\n");
      conn_cleanup(st);
      st->step = ASGI_ST_ACCEPT;
      break;

    default:
      return PM_METAL_ERROR;
    }
  }
}

pm_metal_net_asgi_srv_h pm_metal_net_asgi_listen(uint32_t             port,
                                                 const char *const   *ifnames,
                                                 uint32_t             nif,
                                                 pm_metal_tls_creds_h creds)
{
  pm_metal_net_asgi_srv_h h;
  asgi_srv_t             *srv;
  asgi_listen_t          *st;

  h = srv_alloc();
  if (h == PM_METAL_NET_ASGI_SRV_INVALID) {
    return h;
  }
  srv                = &g_srvs[h];
  srv->port          = port;
  srv->creds         = creds;
  srv->keepalive_s   = 30;
  srv->budget_pct    = 10;
  if (pm_metal_net_asgi_cfg()->loaded) {
    srv->keepalive_s = pm_metal_net_asgi_cfg()->keepalive_s;
    srv->budget_pct  = pm_metal_net_asgi_cfg()->budget_pct;
  }
  srv->listen_sock = pm_metal_net_socket(PM_METAL_NET_AF_INET, PM_METAL_NET_SOCK_STREAM);
  if (srv->listen_sock == PM_METAL_NET_SOCK_INVALID) {
    srv->used = 0;
    return PM_METAL_NET_ASGI_SRV_INVALID;
  }
  if (nif > 0 && ifnames != NULL && ifnames[0] != NULL) {
    strncpy(srv->ifname, ifnames[0], sizeof(srv->ifname) - 1u);
    (void)pm_metal_net_bind_if(srv->listen_sock, srv->ifname);
  }

  srv->coro = pm_metal_async_coro_create(AsgiListenStep, sizeof(asgi_listen_t));
  if (srv->coro == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_net_close(srv->listen_sock);
    srv->used = 0;
    return PM_METAL_NET_ASGI_SRV_INVALID;
  }
  st = (asgi_listen_t *)pm_metal_async_coro_state(srv->coro);
  memset(st, 0, sizeof(*st));
  st->step  = ASGI_ST_LISTEN;
  st->srv_h = h;
  st->csock = PM_METAL_NET_SOCK_INVALID;
  st->tls_h = PM_METAL_TLS_INVALID;
  srv->task = pm_metal_async_create_task(srv->coro);
  if (srv->task == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_net_close(srv->listen_sock);
    srv->used = 0;
    return PM_METAL_NET_ASGI_SRV_INVALID;
  }
  return h;
}

int32_t pm_metal_net_asgi_mount(pm_metal_net_asgi_srv_h s,
                                const char             *path,
                                pm_metal_net_asgi_app_h app)
{
  asgi_srv_t *srv;
  uint32_t    i;

  srv = pm_metal_net_asgi_srv_slot(s);
  if (srv == NULL || path == NULL || app == PM_METAL_NET_ASGI_APP_INVALID) {
    return -1;
  }
  for (i = 0; i < ASGI_MOUNT_MAX; i++) {
    if (srv->mounts[i].used && strcmp(srv->mounts[i].path, path) == 0) {
      srv->mounts[i].app = app;
      return 0;
    }
  }
  for (i = 0; i < ASGI_MOUNT_MAX; i++) {
    if (!srv->mounts[i].used) {
      srv->mounts[i].used = 1;
      strncpy(srv->mounts[i].path, path, sizeof(srv->mounts[i].path) - 1u);
      srv->mounts[i].app = app;
      return 0;
    }
  }
  return -1;
}

int32_t pm_metal_net_asgi_unmount(pm_metal_net_asgi_srv_h s, const char *path)
{
  asgi_srv_t *srv;
  uint32_t    i;

  srv = pm_metal_net_asgi_srv_slot(s);
  if (srv == NULL || path == NULL) {
    return -1;
  }
  for (i = 0; i < ASGI_MOUNT_MAX; i++) {
    if (srv->mounts[i].used && strcmp(srv->mounts[i].path, path) == 0) {
      memset(&srv->mounts[i], 0, sizeof(srv->mounts[i]));
      return 0;
    }
  }
  return -1;
}

void pm_metal_net_asgi_close(pm_metal_net_asgi_srv_h s)
{
  asgi_srv_t *srv;

  srv = pm_metal_net_asgi_srv_slot(s);
  if (srv == NULL) {
    return;
  }
  if (srv->listen_sock != PM_METAL_NET_SOCK_INVALID) {
    pm_metal_net_close(srv->listen_sock);
  }
  memset(srv, 0, sizeof(*srv));
}

static int32_t              g_asgi_autoloaded;
static pm_metal_tls_creds_h g_httpd_tls_creds = PM_METAL_TLS_CREDS_INVALID;
static pm_metal_net_asgi_srv_h g_autoload_srvs[ASGI_SRV_MAX];
static uint32_t                g_autoload_n;

static void asgi_apply_mounts(pm_metal_net_asgi_srv_h srv, const asgi_httpd_cfg_t *cfg)
{
  uint32_t    i;
  asgi_srv_t *s;

  if (cfg == NULL) {
    return;
  }
  for (i = 0; i < cfg->n_mounts; i++) {
    pm_metal_net_asgi_app_h app;

    app = pm_metal_net_asgi_resolve_app(cfg->mounts[i].app, cfg->mounts[i].root);
    if (app == PM_METAL_NET_ASGI_APP_INVALID) {
      pm_metal_logf("asgi: unknown app %s", cfg->mounts[i].app);
      continue;
    }
    if (pm_metal_net_asgi_mount(srv, cfg->mounts[i].path, app) != 0) {
      continue;
    }
    s = pm_metal_net_asgi_srv_slot(srv);
    if (s != NULL) {
      uint32_t j;

      for (j = 0; j < ASGI_MOUNT_MAX; j++) {
        if (s->mounts[j].used && strcmp(s->mounts[j].path, cfg->mounts[i].path) == 0) {
          s->mounts[j].auth_basic = cfg->mounts[i].auth_basic;
        }
      }
    }
  }
}

static pm_metal_tls_creds_h asgi_load_httpd_creds(const asgi_httpd_cfg_t *cfg)
{
  pm_metal_tls_creds_h h;
  const char          *ca;

  if (g_httpd_tls_creds != PM_METAL_TLS_CREDS_INVALID) {
    return g_httpd_tls_creds;
  }
  if (cfg == NULL || cfg->tls_cert[0] == '\0' || cfg->tls_key[0] == '\0') {
    return PM_METAL_TLS_CREDS_INVALID;
  }
  h = pm_metal_net_tls_creds_open();
  if (h == PM_METAL_TLS_CREDS_INVALID) {
    return h;
  }
  ca = (cfg->tls_client_ca[0] != '\0') ? cfg->tls_client_ca : NULL;
  if (pm_metal_net_tls_creds_load_paths(
        h, cfg->tls_cert, cfg->tls_key, ca, cfg->client_auth) != 0) {
    (void)pm_metal_net_tls_creds_close(h);
    return PM_METAL_TLS_CREDS_INVALID;
  }
  g_httpd_tls_creds = h;
  return h;
}

static void asgi_autoload_close_all(void)
{
  uint32_t i;

  for (i = 0; i < g_autoload_n; i++) {
    if (g_autoload_srvs[i] != PM_METAL_NET_ASGI_SRV_INVALID) {
      pm_metal_net_asgi_close(g_autoload_srvs[i]);
      g_autoload_srvs[i] = PM_METAL_NET_ASGI_SRV_INVALID;
    }
  }
  g_autoload_n = 0;
  if (g_httpd_tls_creds != PM_METAL_TLS_CREDS_INVALID) {
    (void)pm_metal_net_tls_creds_close(g_httpd_tls_creds);
    g_httpd_tls_creds = PM_METAL_TLS_CREDS_INVALID;
  }
}

int32_t pm_metal_net_asgi_autoload(void)
{
  asgi_httpd_cfg_t       *cfg;
  pm_metal_net_asgi_srv_h srv;
  pm_metal_tls_creds_h    creds;
  pm_metal_net_asgi_srv_h tls_srv;

  if (g_asgi_autoloaded) {
    return 0;
  }
  if (pm_metal_net_asgi_cfg_load() != 0) {
    return -1;
  }
  cfg = pm_metal_net_asgi_cfg();
  if (cfg->n_users > 0) {
    pm_metal_auth_users_set(cfg->users, cfg->n_users);
  } else {
    pm_metal_auth_users_set(NULL, 0);
  }

  /* Empty mounts => listen with blank table (404). */
  srv = pm_metal_net_asgi_listen(cfg->port, NULL, 0, PM_METAL_TLS_CREDS_INVALID);
  if (srv == PM_METAL_NET_ASGI_SRV_INVALID) {
    return -1;
  }
  asgi_apply_mounts(srv, cfg);
  if (g_autoload_n < ASGI_SRV_MAX) {
    g_autoload_srvs[g_autoload_n++] = srv;
  }

  creds = asgi_load_httpd_creds(cfg);
  if (creds != PM_METAL_TLS_CREDS_INVALID && cfg->tls_port != 0) {
    tls_srv = pm_metal_net_asgi_listen(cfg->tls_port, NULL, 0, creds);
    if (tls_srv != PM_METAL_NET_ASGI_SRV_INVALID) {
      asgi_apply_mounts(tls_srv, cfg);
      if (g_autoload_n < ASGI_SRV_MAX) {
        g_autoload_srvs[g_autoload_n++] = tls_srv;
      }
    } else {
      pm_metal_logf("asgi: TLS listen :%u failed", (unsigned)cfg->tls_port);
    }
  }

  g_asgi_autoloaded = 1;
  return 0;
}

int32_t pm_metal_net_asgi_reload(void)
{
  asgi_autoload_close_all();
  g_asgi_autoloaded    = 0;
  g_microdot_fn        = PM_METAL_PY_FN_H_INVALID;
  g_microdot_importing = 0;
  return pm_metal_net_asgi_autoload();
}

static int32_t asgi_guest_copy(wasm_exec_env_t exec_env,
                               const char     *src,
                               char           *out,
                               uint32_t        out_sz)
{
  wasm_module_inst_t inst;
  uintptr_t          i;

  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL || src == NULL || out == NULL || out_sz == 0) {
    return -1;
  }
  if (!wasm_runtime_validate_native_addr(inst, (void *)src, 1)) {
    return -1;
  }
  for (i = 0; i + 1 < out_sz; i++) {
    if (!wasm_runtime_validate_native_addr(inst, (void *)(src + i), 1)) {
      return -1;
    }
    out[i] = src[i];
    if (src[i] == '\0') {
      return 0;
    }
  }
  return -1;
}

static uint32_t asgi_listen_native(
  wasm_exec_env_t exec_env, uint32_t port, uint32_t ifnames_ptr, uint32_t nif, uint32_t creds)
{
  (void)exec_env;
  (void)ifnames_ptr;
  (void)nif;
  return pm_metal_net_asgi_listen(port, NULL, 0, (pm_metal_tls_creds_h)creds);
}

static int32_t asgi_mount_native(wasm_exec_env_t exec_env,
                                 uint32_t        s,
                                 const char     *path,
                                 uint32_t        app)
{
  char cleaned[ASGI_PATH_MAX];

  if (asgi_guest_copy(exec_env, path, cleaned, sizeof(cleaned)) != 0) {
    return -1;
  }
  return pm_metal_net_asgi_mount(s, cleaned, app);
}

static int32_t asgi_unmount_native(wasm_exec_env_t exec_env, uint32_t s, const char *path)
{
  char cleaned[ASGI_PATH_MAX];

  if (asgi_guest_copy(exec_env, path, cleaned, sizeof(cleaned)) != 0) {
    return -1;
  }
  return pm_metal_net_asgi_unmount(s, cleaned);
}

static void asgi_close_native(wasm_exec_env_t exec_env, uint32_t s)
{
  (void)exec_env;
  pm_metal_net_asgi_close(s);
}

static uint32_t asgi_register_wasm_native(wasm_exec_env_t exec_env,
                                          const char     *mod,
                                          const char     *func)
{
  char m[64];
  char f[64];

  if (asgi_guest_copy(exec_env, mod, m, sizeof(m)) != 0 ||
      asgi_guest_copy(exec_env, func, f, sizeof(f)) != 0) {
    return PM_METAL_NET_ASGI_APP_INVALID;
  }
  return pm_metal_net_asgi_register_wasm(m, f);
}

static void asgi_unregister_native(wasm_exec_env_t exec_env, uint32_t app)
{
  (void)exec_env;
  pm_metal_net_asgi_unregister(app);
}

static int32_t asgi_autoload_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_net_asgi_autoload();
}

static int32_t asgi_send_simple_native(wasm_exec_env_t exec_env,
                                       uint32_t        code,
                                       const char     *reason,
                                       const char     *ctype,
                                       const char     *body)
{
  char r[64];
  char c[64];
  char b[ASGI_IO_MAX];

  if (asgi_guest_copy(exec_env, reason, r, sizeof(r)) != 0 ||
      asgi_guest_copy(exec_env, ctype, c, sizeof(c)) != 0 ||
      asgi_guest_copy(exec_env, body, b, sizeof(b)) != 0) {
    return -1;
  }
  return pm_metal_net_asgi_send_simple(code, r, c, b);
}

static NativeSymbol g_pm_metal_net_asgi_native_symbols[] = {
  { "pm_metal_net_asgi_listen", (void *)asgi_listen_native, "(iiii)i", NULL },
  { "pm_metal_net_asgi_mount", (void *)asgi_mount_native, "(i$i)i", NULL },
  { "pm_metal_net_asgi_unmount", (void *)asgi_unmount_native, "(i$)i", NULL },
  { "pm_metal_net_asgi_close", (void *)asgi_close_native, "(i)", NULL },
  { "pm_metal_net_asgi_register_wasm", (void *)asgi_register_wasm_native, "($$)i", NULL },
  { "pm_metal_net_asgi_unregister", (void *)asgi_unregister_native, "(i)", NULL },
  { "pm_metal_net_asgi_autoload", (void *)asgi_autoload_native, "()i", NULL },
  { "pm_metal_net_asgi_send_simple", (void *)asgi_send_simple_native, "(i$$$)i", NULL },
};

int pm_metal_net_asgi_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_NET_ASGI_WASI_MODULE,
                                     g_pm_metal_net_asgi_native_symbols,
                                     sizeof(g_pm_metal_net_asgi_native_symbols) /
                                       sizeof(g_pm_metal_net_asgi_native_symbols[0]))) {
    return -1;
  }
  return 0;
}
