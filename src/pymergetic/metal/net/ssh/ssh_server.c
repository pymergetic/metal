/*
 * sshd listen/accept + Dropbear session over Metal net/PTY.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ssh_config.h"
#include "ssh_dropbear.h"

#include <pymergetic/metal/async/handle.h>
#include <pymergetic/metal/async/await.h>
#include <pymergetic/metal/async/coro.h>
#include <pymergetic/metal/async/task.h>
#include <pymergetic/metal/async/time.h>
#include <pymergetic/metal/dev/stream/__init__.h>
#include <pymergetic/metal/log/__init__.h>
#include <pymergetic/metal/net/ip/tcp/__init__.h>
#include <pymergetic/metal/net/ssh/__init__.h>

typedef uint32_t pm_metal_net_ssh_srv_h;
#define PM_METAL_NET_SSH_SRV_INVALID 0u

#define SSH_SRV_MAX 2u

typedef enum {
  SSH_ST_ACCEPT = 0,
  SSH_ST_ACCEPT_AW,
  SSH_ST_SESS,
  SSH_ST_SESS_SLEEP
} ssh_step_t;

typedef struct {
  int32_t used;
  uint32_t port;
  uint32_t listen_h;
  uint32_t coro;
  uint32_t task;
} ssh_srv_t;

typedef struct {
  ssh_step_t step;
  pm_metal_net_ssh_srv_h srv_h;
  uint32_t aw;
  uint32_t csock;
  pm_metal_stream_h pty_m;
  pm_metal_stream_h pty_s;
  uint32_t sess;
} ssh_listen_t;

static ssh_srv_t g_ssh[SSH_SRV_MAX];
static int32_t g_ssh_autoloaded;

static void ssh_log(const char *msg)
{
  if (msg != NULL) {
    pm_metal_log((const uint8_t *)msg);
  }
}

static pm_metal_net_ssh_srv_h ssh_alloc(void)
{
  uint32_t i;

  for (i = 1u; i < SSH_SRV_MAX; i++) {
    if (g_ssh[i].used == 0) {
      memset(&g_ssh[i], 0, sizeof(g_ssh[i]));
      g_ssh[i].used = 1;
      return i;
    }
  }
  return PM_METAL_NET_SSH_SRV_INVALID;
}

static ssh_srv_t *ssh_slot(pm_metal_net_ssh_srv_h h)
{
  if (h == 0u || h >= SSH_SRV_MAX || g_ssh[h].used == 0) {
    return NULL;
  }
  return &g_ssh[h];
}

static void ssh_conn_cleanup(ssh_listen_t *st)
{
  if (st->sess != 0u) {
    metal_dropbear_session_close(st->sess);
    st->sess = 0u;
  }
  if (st->pty_m != PM_METAL_STREAM_INVALID) {
    pm_metal_stream_close(st->pty_m);
    st->pty_m = PM_METAL_STREAM_INVALID;
  }
  if (st->pty_s != PM_METAL_STREAM_INVALID) {
    pm_metal_stream_close(st->pty_s);
    st->pty_s = PM_METAL_STREAM_INVALID;
  }
  if (st->csock != 0u) {
    pm_metal_net_ip_tcp_close(st->csock);
    st->csock = 0u;
  }
}

static uint32_t ssh_listen_step(uint32_t self_h)
{
  ssh_listen_t *st;
  ssh_srv_t *srv;

  st = (ssh_listen_t *)pm_metal_async_coro_state(self_h);
  if (st == NULL) {
    return (uint32_t)PM_METAL_ASYNC_ERROR;
  }
  srv = ssh_slot(st->srv_h);
  if (srv == NULL) {
    return (uint32_t)PM_METAL_ASYNC_ERROR;
  }

  for (;;) {
    switch (st->step) {
    case SSH_ST_ACCEPT:
      st->aw = pm_metal_net_ip_tcp_accept(srv->listen_h, 0u);
      if (st->aw == 0u) {
        return (uint32_t)PM_METAL_ASYNC_ERROR;
      }
      st->step = SSH_ST_ACCEPT_AW;
      return (uint32_t)pm_metal_async_await(self_h, st->aw);

    case SSH_ST_ACCEPT_AW:
      st->csock = pm_metal_async_result_u32(self_h);
      if (st->csock == 0u) {
        st->step = SSH_ST_ACCEPT;
        break;
      }
      if (pm_metal_stream_pty(&st->pty_m, &st->pty_s) != 0) {
        ssh_conn_cleanup(st);
        st->step = SSH_ST_ACCEPT;
        break;
      }
      st->sess = metal_dropbear_session_start(st->csock, st->pty_m, st->pty_s);
      if (st->sess == 0u) {
        ssh_log("sshd: dropbear session start failed\n");
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
      st->aw = pm_metal_async_sleep_us(2000ull);
      st->step = SSH_ST_SESS_SLEEP;
      return (uint32_t)pm_metal_async_await(self_h, st->aw);

    case SSH_ST_SESS_SLEEP:
      st->step = SSH_ST_SESS;
      break;

    default:
      return (uint32_t)PM_METAL_ASYNC_ERROR;
    }
  }
}

/* Rust wrappers export pm_metal_net_ssh_*; keep Metal C names internal. */
pm_metal_net_ssh_srv_h metal_ssh_listen(uint32_t port)
{
  pm_metal_net_ssh_srv_h h;
  ssh_srv_t *srv;
  ssh_listen_t *st;

  h = ssh_alloc();
  if (h == PM_METAL_NET_SSH_SRV_INVALID) {
    return h;
  }
  srv = &g_ssh[h];
  srv->port = (port != 0u) ? port : 22u;
  srv->listen_h = pm_metal_net_ip_tcp_listen((uint16_t)srv->port);
  if (srv->listen_h == 0u) {
    srv->used = 0;
    return PM_METAL_NET_SSH_SRV_INVALID;
  }
  (void)metal_dropbear_ensure_hostkeys();
  srv->coro = pm_metal_async_coro_create(ssh_listen_step, (uint32_t)sizeof(ssh_listen_t));
  if (srv->coro == 0u) {
    pm_metal_net_ip_tcp_listen_close(srv->listen_h);
    srv->used = 0;
    return PM_METAL_NET_SSH_SRV_INVALID;
  }
  st = (ssh_listen_t *)pm_metal_async_coro_state(srv->coro);
  memset(st, 0, sizeof(*st));
  st->step = SSH_ST_ACCEPT;
  st->srv_h = h;
  st->csock = 0u;
  st->pty_m = PM_METAL_STREAM_INVALID;
  st->pty_s = PM_METAL_STREAM_INVALID;
  st->sess = 0u;
  srv->task = pm_metal_async_create_task(srv->coro);
  if (srv->task == 0u) {
    pm_metal_async_coro_close(srv->coro);
    pm_metal_net_ip_tcp_listen_close(srv->listen_h);
    srv->used = 0;
    return PM_METAL_NET_SSH_SRV_INVALID;
  }
  return h;
}

void metal_ssh_close(pm_metal_net_ssh_srv_h s)
{
  ssh_srv_t *srv;

  srv = ssh_slot(s);
  if (srv == NULL) {
    return;
  }
  if (srv->listen_h != 0u) {
    pm_metal_net_ip_tcp_listen_close(srv->listen_h);
  }
  memset(srv, 0, sizeof(*srv));
}

int32_t metal_ssh_autoload(void)
{
  pm_metal_sshd_cfg_t *cfg;
  uint32_t port;

  if (g_ssh_autoloaded != 0) {
    return 0;
  }
  (void)pm_metal_net_ssh_cfg_load();
  cfg = pm_metal_net_ssh_cfg();
  port = (cfg != NULL && cfg->port != 0u) ? cfg->port : 22u;
  if (metal_ssh_listen(port) == PM_METAL_NET_SSH_SRV_INVALID) {
    return -1;
  }
  g_ssh_autoloaded = 1;
  return 0;
}

int32_t metal_ssh_status(char *buf, uint32_t buf_len)
{
  pm_metal_sshd_cfg_t *cfg;
  uint32_t i;
  uint32_t nlisten;
  uint32_t port;
  int n;

  if (buf == NULL || buf_len == 0u) {
    return -1;
  }
  cfg = pm_metal_net_ssh_cfg();
  port = (cfg != NULL && cfg->port != 0u) ? cfg->port : 22u;
  nlisten = 0u;
  for (i = 1u; i < SSH_SRV_MAX; i++) {
    if (g_ssh[i].used != 0) {
      nlisten++;
    }
  }
  n = snprintf(buf, buf_len,
               "sshd listen=%u port=%u passwd=%d pubkey=%d sslcert=%d autoload=%d",
               (unsigned)nlisten, (unsigned)port, cfg != NULL ? (int)cfg->auth_passwd : 0,
               cfg != NULL ? (int)cfg->auth_pubkey : 0, cfg != NULL ? (int)cfg->auth_sslcert : 0,
               (int)g_ssh_autoloaded);
  if (n < 0 || (uint32_t)n >= buf_len) {
    return -1;
  }
  return n;
}

uint32_t metal_ssh_listen_port(void)
{
  uint32_t i;

  for (i = 1u; i < SSH_SRV_MAX; i++) {
    if (g_ssh[i].used != 0) {
      return g_ssh[i].port;
    }
  }
  return 0u;
}

/* Tree label: "delay" until first KEX materializes keys (Dropbear -R). */
int32_t metal_ssh_hostkey_label(char *buf, uint32_t buf_len)
{
  static const char k_delay[] = "delay";
  uint32_t i;

  if (buf == NULL || buf_len == 0u) {
    return -1;
  }
  if (buf_len < sizeof(k_delay)) {
    return -1;
  }
  for (i = 0; i < sizeof(k_delay); i++) {
    buf[i] = k_delay[i];
  }
  return 0;
}
