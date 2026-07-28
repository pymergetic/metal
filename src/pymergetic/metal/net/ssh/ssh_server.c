/*
 * sshd listen/accept + Dropbear session over Metal net/PTY.
 * Password auth: pm_metal_auth_user_check. Autostarts on :22.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "ssh_dropbear.h"

#include <pymergetic/metal/net/ip/ip.h>
#include <pymergetic/metal/net/ssh/ssh.h>
#include <pymergetic/metal/net/ssh/ssh_config.h>
#include <pymergetic/metal/dev/stream/stream.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/runtime/async/async.h>

#include "wasm_export.h"

#define SSH_SRV_MAX 2u

typedef enum {
  SSH_ST_LISTEN = 0,
  SSH_ST_LISTEN_AW,
  SSH_ST_ACCEPT,
  SSH_ST_ACCEPT_AW,
  SSH_ST_SESS,
  SSH_ST_SESS_SLEEP
} ssh_step_t;

typedef struct {
  int32_t                 used;
  uint32_t                port;
  pm_metal_net_ip_sock_h  listen_sock;
  pm_metal_async_handle_t coro;
  pm_metal_async_handle_t task;
} ssh_srv_t;

typedef struct {
  ssh_step_t              step;
  pm_metal_net_ssh_srv_h  srv_h;
  pm_metal_async_handle_t aw;
  pm_metal_net_ip_sock_h  csock;
  pm_metal_stream_h       pty_m;
  pm_metal_stream_h       pty_s;
  uint32_t                sess;
} ssh_listen_t;

static ssh_srv_t g_ssh[SSH_SRV_MAX];
static int32_t   g_ssh_autoloaded;

static pm_metal_net_ssh_srv_h ssh_alloc(void)
{
  uint32_t i;

  for (i = 1; i < SSH_SRV_MAX; i++) {
    if (!g_ssh[i].used) {
      memset(&g_ssh[i], 0, sizeof(g_ssh[i]));
      g_ssh[i].used = 1;
      return i;
    }
  }
  return PM_METAL_NET_SSH_SRV_INVALID;
}

static ssh_srv_t *ssh_slot(pm_metal_net_ssh_srv_h h)
{
  if (h == 0 || h >= SSH_SRV_MAX || !g_ssh[h].used) {
    return NULL;
  }
  return &g_ssh[h];
}

static void ssh_conn_cleanup(ssh_listen_t *st)
{
  if (st->sess != 0) {
    metal_dropbear_session_close(st->sess);
    st->sess = 0;
  }
  if (st->pty_m != PM_METAL_STREAM_INVALID) {
    pm_metal_stream_close(st->pty_m);
    st->pty_m = PM_METAL_STREAM_INVALID;
  }
  if (st->pty_s != PM_METAL_STREAM_INVALID) {
    pm_metal_stream_close(st->pty_s);
    st->pty_s = PM_METAL_STREAM_INVALID;
  }
  if (st->csock != PM_METAL_NET_IP_SOCK_INVALID) {
    pm_metal_net_ip_close(st->csock);
    st->csock = PM_METAL_NET_IP_SOCK_INVALID;
  }
}

static pm_metal_status_t SshListenStep(pm_metal_async_handle_t self_h)
{
  ssh_listen_t *st;
  ssh_srv_t    *srv;
  uint32_t      r;

  st = (ssh_listen_t *)pm_metal_async_coro_state(self_h);
  if (st == NULL) {
    return PM_METAL_ERROR;
  }
  srv = ssh_slot(st->srv_h);
  if (srv == NULL) {
    return PM_METAL_ERROR;
  }

  for (;;) {
    switch (st->step) {
    case SSH_ST_LISTEN:
      st->aw   = pm_metal_net_ip_listen(srv->listen_sock, srv->port);
      st->step = SSH_ST_LISTEN_AW;
      return pm_metal_async_await(self_h, st->aw);

    case SSH_ST_LISTEN_AW:
      r = pm_metal_async_result_u32(self_h);
      if (r != 1u) {
        pm_metal_logf("sshd: listen failed port=%u", (unsigned)srv->port);
        return PM_METAL_ERROR;
      }
      pm_metal_logf("sshd: listening :%u (dropbear)", (unsigned)srv->port);
      (void)metal_dropbear_ensure_hostkeys();
      st->step = SSH_ST_ACCEPT;
      break;

    case SSH_ST_ACCEPT:
      st->aw = pm_metal_net_ip_accept(srv->listen_sock);
      if (st->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
        return PM_METAL_ERROR;
      }
      st->step = SSH_ST_ACCEPT_AW;
      return pm_metal_async_await(self_h, st->aw);

    case SSH_ST_ACCEPT_AW:
      st->csock = (pm_metal_net_ip_sock_h)pm_metal_async_result_u32(self_h);
      if (st->csock == PM_METAL_NET_IP_SOCK_INVALID) {
        st->step = SSH_ST_ACCEPT;
        break;
      }
      if (pm_metal_stream_pty(&st->pty_m, &st->pty_s) != 0) {
        ssh_conn_cleanup(st);
        st->step = SSH_ST_ACCEPT;
        break;
      }
      st->sess = metal_dropbear_session_start(st->csock, st->pty_m, st->pty_s);
      if (st->sess == 0) {
        pm_metal_logf("sshd: dropbear session start failed");
        ssh_conn_cleanup(st);
        st->step = SSH_ST_ACCEPT;
        break;
      }
      st->step = SSH_ST_SESS;
      break;

    case SSH_ST_SESS:
      if (metal_dropbear_session_poll(st->sess) != 0) {
        ssh_conn_cleanup(st);
        st->step = SSH_ST_ACCEPT;
        break;
      }
      st->aw   = pm_metal_async_sleep_us(2000);
      st->step = SSH_ST_SESS_SLEEP;
      return pm_metal_async_await(self_h, st->aw);

    case SSH_ST_SESS_SLEEP:
      st->step = SSH_ST_SESS;
      break;

    default:
      return PM_METAL_ERROR;
    }
  }
}

pm_metal_net_ssh_srv_h pm_metal_net_ssh_listen(uint32_t port)
{
  pm_metal_net_ssh_srv_h h;
  ssh_srv_t             *srv;
  ssh_listen_t          *st;

  h = ssh_alloc();
  if (h == PM_METAL_NET_SSH_SRV_INVALID) {
    return h;
  }
  srv              = &g_ssh[h];
  srv->port        = (port != 0u) ? port : 22u;
  srv->listen_sock = pm_metal_net_ip_socket(PM_METAL_NET_IP_AF_INET, PM_METAL_NET_IP_SOCK_STREAM);
  if (srv->listen_sock == PM_METAL_NET_IP_SOCK_INVALID) {
    srv->used = 0;
    return PM_METAL_NET_SSH_SRV_INVALID;
  }
  srv->coro = pm_metal_async_coro_create(SshListenStep, sizeof(ssh_listen_t));
  if (srv->coro == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_net_ip_close(srv->listen_sock);
    srv->used = 0;
    return PM_METAL_NET_SSH_SRV_INVALID;
  }
  st = (ssh_listen_t *)pm_metal_async_coro_state(srv->coro);
  memset(st, 0, sizeof(*st));
  st->step  = SSH_ST_LISTEN;
  st->srv_h = h;
  st->csock = PM_METAL_NET_IP_SOCK_INVALID;
  st->pty_m = PM_METAL_STREAM_INVALID;
  st->pty_s = PM_METAL_STREAM_INVALID;
  st->sess  = 0;
  srv->task = pm_metal_async_create_task(srv->coro);
  if (srv->task == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_net_ip_close(srv->listen_sock);
    srv->used = 0;
    return PM_METAL_NET_SSH_SRV_INVALID;
  }
  return h;
}

void pm_metal_net_ssh_close(pm_metal_net_ssh_srv_h s)
{
  ssh_srv_t *srv;

  srv = ssh_slot(s);
  if (srv == NULL) {
    return;
  }
  if (srv->listen_sock != PM_METAL_NET_IP_SOCK_INVALID) {
    pm_metal_net_ip_close(srv->listen_sock);
  }
  memset(srv, 0, sizeof(*srv));
}

int32_t pm_metal_net_ssh_autoload(void)
{
  pm_metal_sshd_cfg_t *cfg;
  uint32_t             port;

  if (g_ssh_autoloaded) {
    return 0;
  }
  (void)pm_metal_net_ssh_cfg_load();
  cfg  = pm_metal_net_ssh_cfg();
  port = (cfg != NULL && cfg->port != 0u) ? cfg->port : 22u;
  if (pm_metal_net_ssh_listen(port) == PM_METAL_NET_SSH_SRV_INVALID) {
    return -1;
  }
  g_ssh_autoloaded = 1;
  return 0;
}

int32_t pm_metal_net_ssh_status(char *buf, uint32_t buf_len)
{
  pm_metal_sshd_cfg_t *cfg;
  uint32_t             i;
  uint32_t             nlisten;
  uint32_t             port;
  int                  n;

  if (buf == NULL || buf_len == 0u) {
    return -1;
  }
  cfg     = pm_metal_net_ssh_cfg();
  port    = (cfg != NULL && cfg->port != 0u) ? cfg->port : 22u;
  nlisten = 0;
  for (i = 1; i < SSH_SRV_MAX; i++) {
    if (g_ssh[i].used) {
      nlisten++;
    }
  }
  n = snprintf(buf,
               buf_len,
               "sshd listen=%u port=%u passwd=%d pubkey=%d sslcert=%d autoload=%d",
               (unsigned)nlisten,
               (unsigned)port,
               cfg != NULL ? (int)cfg->auth_passwd : 0,
               cfg != NULL ? (int)cfg->auth_pubkey : 0,
               cfg != NULL ? (int)cfg->auth_sslcert : 0,
               (int)g_ssh_autoloaded);
  if (n < 0 || (uint32_t)n >= buf_len) {
    return -1;
  }
  return n;
}

static uint32_t ssh_listen_native(wasm_exec_env_t exec_env, uint32_t port)
{
  (void)exec_env;
  return pm_metal_net_ssh_listen(port);
}

static void ssh_close_native(wasm_exec_env_t exec_env, uint32_t s)
{
  (void)exec_env;
  pm_metal_net_ssh_close(s);
}

static int32_t ssh_autoload_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_net_ssh_autoload();
}

static int32_t ssh_status_native(wasm_exec_env_t exec_env, uint32_t buf_ptr, uint32_t buf_len)
{
  wasm_module_inst_t inst;
  char              *buf;

  (void)exec_env;
  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL || buf_len == 0u) {
    return -1;
  }
  if (!wasm_runtime_validate_app_addr(inst, buf_ptr, buf_len)) {
    return -1;
  }
  buf = (char *)wasm_runtime_addr_app_to_native(inst, buf_ptr);
  return pm_metal_net_ssh_status(buf, buf_len);
}

static NativeSymbol g_pm_metal_net_ssh_native_symbols[] = {
  { "pm_metal_net_ssh_listen", (void *)ssh_listen_native, "(i)i", NULL },
  { "pm_metal_net_ssh_close", (void *)ssh_close_native, "(i)", NULL },
  { "pm_metal_net_ssh_autoload", (void *)ssh_autoload_native, "()i", NULL },
  { "pm_metal_net_ssh_status", (void *)ssh_status_native, "(ii)i", NULL },
};

int pm_metal_net_ssh_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_NET_SSH_WASI_MODULE,
                                     g_pm_metal_net_ssh_native_symbols,
                                     sizeof(g_pm_metal_net_ssh_native_symbols) /
                                       sizeof(g_pm_metal_net_ssh_native_symbols[0]))) {
    return -1;
  }
  return 0;
}
