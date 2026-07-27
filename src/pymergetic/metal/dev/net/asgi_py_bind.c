/*
 * pymergetic.metal.net.asgi — Python control plane for Metal ASGI httpd.
 */
#include <stdint.h>
#include <string.h>

#include "asgi_internal.h"

#include <pymergetic/metal/dev/net/asgi.h>
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/py/py_obj.h>

#include "py/obj.h"

static mp_obj_t py_asgi_listen(size_t n_args, const mp_obj_t *args)
{
  uint32_t              port;
  pm_metal_tls_creds_h  creds;
  pm_metal_net_asgi_srv_h srv;

  port  = (uint32_t)mp_obj_get_int(args[0]);
  creds = PM_METAL_TLS_CREDS_INVALID;
  if (n_args >= 2) {
    creds = (pm_metal_tls_creds_h)mp_obj_get_int(args[1]);
  }
  srv = pm_metal_net_asgi_listen(port, NULL, 0, creds);
  if (srv == PM_METAL_NET_ASGI_SRV_INVALID) {
    pm_metal_py_raise_runtime_error("net.asgi: listen failed");
  }
  return pm_metal_py_int_new((int64_t)srv);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_asgi_listen_obj, 1, 2, py_asgi_listen);
PM_METAL_PY_BIND(g_py_bind_asgi_listen, "pymergetic.metal.net.asgi", "listen", py_asgi_listen_obj,
                 PM_METAL_PY_SYNC);

static mp_obj_t py_asgi_mount(mp_obj_t srv_obj, mp_obj_t path_obj, mp_obj_t app_obj)
{
  if (pm_metal_net_asgi_mount((pm_metal_net_asgi_srv_h)mp_obj_get_int(srv_obj),
                              mp_obj_str_get_str(path_obj),
                              (pm_metal_net_asgi_app_h)mp_obj_get_int(app_obj)) != 0) {
    pm_metal_py_raise_runtime_error("net.asgi: mount failed");
  }
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(py_asgi_mount_obj, py_asgi_mount);
PM_METAL_PY_BIND(g_py_bind_asgi_mount, "pymergetic.metal.net.asgi", "mount", py_asgi_mount_obj,
                 PM_METAL_PY_SYNC);

static mp_obj_t py_asgi_unmount(mp_obj_t srv_obj, mp_obj_t path_obj)
{
  if (pm_metal_net_asgi_unmount((pm_metal_net_asgi_srv_h)mp_obj_get_int(srv_obj),
                                mp_obj_str_get_str(path_obj)) != 0) {
    pm_metal_py_raise_runtime_error("net.asgi: unmount failed");
  }
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(py_asgi_unmount_obj, py_asgi_unmount);
PM_METAL_PY_BIND(g_py_bind_asgi_unmount, "pymergetic.metal.net.asgi", "unmount",
                 py_asgi_unmount_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_asgi_close(mp_obj_t srv_obj)
{
  pm_metal_net_asgi_close((pm_metal_net_asgi_srv_h)mp_obj_get_int(srv_obj));
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_asgi_close_obj, py_asgi_close);
PM_METAL_PY_BIND(g_py_bind_asgi_close, "pymergetic.metal.net.asgi", "close", py_asgi_close_obj,
                 PM_METAL_PY_SYNC);

static mp_obj_t py_asgi_autoload(void)
{
  if (pm_metal_net_asgi_autoload() != 0) {
    pm_metal_py_raise_runtime_error("net.asgi: autoload failed");
  }
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_asgi_autoload_obj, py_asgi_autoload);
PM_METAL_PY_BIND(g_py_bind_asgi_autoload, "pymergetic.metal.net.asgi", "autoload",
                 py_asgi_autoload_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_asgi_app_health(void)
{
  return pm_metal_py_int_new((int64_t)pm_metal_net_asgi_app_health());
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_asgi_app_health_obj, py_asgi_app_health);
PM_METAL_PY_BIND(g_py_bind_asgi_app_health, "pymergetic.metal.net.asgi", "app_health",
                 py_asgi_app_health_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_asgi_app_static(mp_obj_t root_obj)
{
  return pm_metal_py_int_new((int64_t)pm_metal_net_asgi_app_static(mp_obj_str_get_str(root_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_asgi_app_static_obj, py_asgi_app_static);
PM_METAL_PY_BIND(g_py_bind_asgi_app_static, "pymergetic.metal.net.asgi", "app_static",
                 py_asgi_app_static_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_asgi_register_py(mp_obj_t cookie_obj)
{
  return pm_metal_py_int_new(
    (int64_t)pm_metal_net_asgi_register_py((uint32_t)mp_obj_get_int(cookie_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_asgi_register_py_obj, py_asgi_register_py);
PM_METAL_PY_BIND(g_py_bind_asgi_register_py, "pymergetic.metal.net.asgi", "register_py",
                 py_asgi_register_py_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_asgi_reload(void)
{
  if (pm_metal_net_asgi_reload() != 0) {
    pm_metal_py_raise_runtime_error("net.asgi: reload failed");
  }
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_asgi_reload_obj, py_asgi_reload);
PM_METAL_PY_BIND(g_py_bind_asgi_reload, "pymergetic.metal.net.asgi", "reload", py_asgi_reload_obj,
                 PM_METAL_PY_SYNC);

static mp_obj_t py_asgi_conn_method(void)
{
  const char *m = pm_metal_net_asgi_conn_method();

  if (m == NULL || m[0] == '\0') {
    return mp_const_none;
  }
  return mp_obj_new_str(m, strlen(m));
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_asgi_conn_method_obj, py_asgi_conn_method);
PM_METAL_PY_BIND(g_py_bind_asgi_conn_method, "pymergetic.metal.net.asgi", "conn_method",
                 py_asgi_conn_method_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_asgi_conn_path(void)
{
  const char *p = pm_metal_net_asgi_conn_target();

  if (p == NULL || p[0] == '\0') {
    return mp_const_none;
  }
  return mp_obj_new_str(p, strlen(p));
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_asgi_conn_path_obj, py_asgi_conn_path);
PM_METAL_PY_BIND(g_py_bind_asgi_conn_path, "pymergetic.metal.net.asgi", "conn_path",
                 py_asgi_conn_path_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_asgi_reply(mp_obj_t code_obj, mp_obj_t ctype_obj, mp_obj_t body_obj)
{
  uint32_t    code;
  const char *ctype;
  const char *body;

  code  = (uint32_t)mp_obj_get_int(code_obj);
  ctype = mp_obj_str_get_str(ctype_obj);
  body  = mp_obj_str_get_str(body_obj);
  if (pm_metal_net_asgi_send_simple(code, "OK", ctype, body) != 0) {
    pm_metal_py_raise_runtime_error("net.asgi: reply failed");
  }
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(py_asgi_reply_obj, py_asgi_reply);
PM_METAL_PY_BIND(g_py_bind_asgi_reply, "pymergetic.metal.net.asgi", "reply", py_asgi_reply_obj,
                 PM_METAL_PY_SYNC);
