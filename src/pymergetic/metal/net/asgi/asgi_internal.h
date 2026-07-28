#ifndef PYMERGETIC_METAL_DEV_NET_ASGI_INTERNAL_H_
#define PYMERGETIC_METAL_DEV_NET_ASGI_INTERNAL_H_

#include <stdint.h>

#include <pymergetic/metal/auth/auth.h>
#include <pymergetic/metal/net/asgi/asgi.h>
#include <pymergetic/metal/net/io_budget.h>
#include <pymergetic/metal/net/ip/ip.h>
#include <pymergetic/metal/net/tls/tls.h>
#include <pymergetic/metal/runtime/async/async.h>

#if defined(__has_include)
#if __has_include("autoconf.h")
#include "autoconf.h"
#endif
#endif

#ifndef CONFIG_PM_METAL_ASGI_PATH_MAX
#define CONFIG_PM_METAL_ASGI_PATH_MAX 256
#endif
#ifndef CONFIG_PM_METAL_ASGI_MOUNT_MAX
#define CONFIG_PM_METAL_ASGI_MOUNT_MAX 512
#endif
#ifndef CONFIG_PM_METAL_ASGI_SRV_MAX
#define CONFIG_PM_METAL_ASGI_SRV_MAX 16
#endif
#ifndef CONFIG_PM_METAL_ASGI_HDR_MAX
#define CONFIG_PM_METAL_ASGI_HDR_MAX (256u * 1024u)
#endif
#ifndef CONFIG_PM_METAL_ASGI_APP_SLOTS
#define CONFIG_PM_METAL_ASGI_APP_SLOTS 128
#endif

#define ASGI_PATH_MAX  ((uint32_t)CONFIG_PM_METAL_ASGI_PATH_MAX)
#define ASGI_MOUNT_MAX ((uint32_t)CONFIG_PM_METAL_ASGI_MOUNT_MAX)
#define ASGI_SRV_MAX   ((uint32_t)CONFIG_PM_METAL_ASGI_SRV_MAX)
#define ASGI_HDR_MAX   ((uint32_t)CONFIG_PM_METAL_ASGI_HDR_MAX)
#define ASGI_IO_MAX    PM_METAL_ASGI_IO_MAX
#define ASGI_APP_SLOTS ((uint32_t)CONFIG_PM_METAL_ASGI_APP_SLOTS)
#define ASGI_CONN_SLOT_EST PM_METAL_ASGI_IO_MAX

typedef struct {
  int32_t                         used;
  pm_metal_net_asgi_runner_kind_t kind;
  pm_metal_net_asgi_c_fn          c_fn;
  void                           *c_ctx;
  uint32_t                        py_cookie; /* pm_metal_py_fn_h_t */
  char                            wasm_mod[64];
  char                            wasm_func[64];
} asgi_app_slot_t;

typedef struct {
  int32_t                 used;
  char                    path[ASGI_PATH_MAX];
  pm_metal_net_asgi_app_h app;
  int32_t                 auth_basic; /* 1 = require basic */
} asgi_mount_t;

typedef struct {
  int32_t                 used;
  uint32_t                port;
  pm_metal_net_tls_creds_h    creds; /* owned by autoload; not closed per listen */
  pm_metal_net_ip_sock_h     listen_sock;
  pm_metal_async_handle_t coro;
  pm_metal_async_handle_t task;
  asgi_mount_t            mounts[ASGI_MOUNT_MAX];
  char                    ifname[16]; /* empty = all */
  uint32_t                keepalive_s;
  uint32_t                budget_pct;
} asgi_srv_t;

typedef struct {
  char    path[ASGI_PATH_MAX];
  char    app[64];
  char    root[ASGI_PATH_MAX];
  int32_t auth_basic;
} asgi_cfg_mount_t;

typedef struct {
  int32_t              loaded;
  uint32_t             port;
  uint32_t             tls_port;
  uint32_t             budget_pct;
  uint32_t             keepalive_s;
  char                 tls_cert[ASGI_PATH_MAX];
  char                 tls_key[ASGI_PATH_MAX];
  char                 tls_client_ca[ASGI_PATH_MAX];
  pm_metal_net_tls_client_auth_t client_auth;
  char                 realm[64];
  pm_metal_auth_user_t users[PM_METAL_AUTH_USERS_MAX];
  uint32_t             n_users;
  asgi_cfg_mount_t     mounts[ASGI_MOUNT_MAX];
  uint32_t             n_mounts;
} asgi_httpd_cfg_t;

asgi_app_slot_t *pm_metal_net_asgi_app_slot(pm_metal_net_asgi_app_h h);
asgi_srv_t      *pm_metal_net_asgi_srv_slot(pm_metal_net_asgi_srv_h h);

int32_t pm_metal_net_asgi_dispatch_c(asgi_app_slot_t *slot,
                                     uint32_t         conn_id,
                                     const char      *method,
                                     const char      *target,
                                     const char      *hdr,
                                     uint32_t         hdr_len);

void pm_metal_net_asgi_conn_begin(pm_metal_net_ip_sock_h sock,
                                  pm_metal_net_tls_h      tls,
                                  const char         *method,
                                  const char         *target,
                                  const char         *mount_prefix,
                                  const char         *static_root);

void pm_metal_net_asgi_conn_set_keepalive(int32_t on);
int32_t pm_metal_net_asgi_conn_keepalive(void);

/** Active request header block for Py runner (NUL-terminated). */
void        pm_metal_net_asgi_conn_set_hdr(const char *hdr, uint32_t hdr_len);
const char *pm_metal_net_asgi_conn_method(void);
const char *pm_metal_net_asgi_conn_target(void);
const char *pm_metal_net_asgi_conn_hdr(void);
uint32_t    pm_metal_net_asgi_conn_hdr_len(void);

/** Send on the active connection (cleartext or TLS). */
int32_t pm_metal_net_asgi_conn_send(const void *buf, uint32_t len);

int32_t                 pm_metal_net_asgi_cfg_load(void);
asgi_httpd_cfg_t       *pm_metal_net_asgi_cfg(void);
pm_metal_net_asgi_app_h pm_metal_net_asgi_resolve_app(const char *app, const char *root);
pm_metal_net_asgi_app_h pm_metal_net_asgi_app_microdot(void);

int32_t pm_metal_net_asgi_budget_try_enter(uint32_t budget_pct);
void    pm_metal_net_asgi_budget_leave(void);

int32_t pm_metal_net_asgi_ws_wanted(const char *hdr, uint32_t hdr_len);
int32_t pm_metal_net_asgi_ws_handshake(const char *hdr, uint32_t hdr_len);
int32_t pm_metal_net_asgi_ws_echo_frame(const uint8_t *in, uint32_t in_len, uint8_t *out,
                                        uint32_t out_cap, uint32_t *out_len);

#endif
