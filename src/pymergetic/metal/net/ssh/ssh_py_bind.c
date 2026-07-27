/*
 * pymergetic.metal.net.ssh — Python control plane for Metal sshd.
 */
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/net/ssh/ssh.h>
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/py/py_obj.h>

#include "py/obj.h"

static mp_obj_t py_ssh_listen(mp_obj_t port_obj)
{
  pm_metal_net_ssh_srv_h srv;

  srv = pm_metal_net_ssh_listen((uint32_t)mp_obj_get_int(port_obj));
  if (srv == PM_METAL_NET_SSH_SRV_INVALID) {
    pm_metal_py_raise_runtime_error("net.ssh: listen failed");
  }
  return pm_metal_py_int_new((int64_t)srv);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_ssh_listen_obj, py_ssh_listen);
PM_METAL_PY_BIND(g_py_bind_ssh_listen, "pymergetic.metal.net.ssh", "listen", py_ssh_listen_obj,
                 PM_METAL_PY_SYNC);

static mp_obj_t py_ssh_close(mp_obj_t srv_obj)
{
  pm_metal_net_ssh_close((pm_metal_net_ssh_srv_h)mp_obj_get_int(srv_obj));
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_ssh_close_obj, py_ssh_close);
PM_METAL_PY_BIND(g_py_bind_ssh_close, "pymergetic.metal.net.ssh", "close", py_ssh_close_obj,
                 PM_METAL_PY_SYNC);

static mp_obj_t py_ssh_autoload(void)
{
  if (pm_metal_net_ssh_autoload() != 0) {
    pm_metal_py_raise_runtime_error("net.ssh: autoload failed");
  }
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_ssh_autoload_obj, py_ssh_autoload);
PM_METAL_PY_BIND(g_py_bind_ssh_autoload, "pymergetic.metal.net.ssh", "autoload",
                 py_ssh_autoload_obj, PM_METAL_PY_SYNC);

static mp_obj_t py_ssh_status(void)
{
  char line[128];

  if (pm_metal_net_ssh_status(line, (uint32_t)sizeof(line)) < 0) {
    pm_metal_py_raise_runtime_error("net.ssh: status failed");
  }
  return mp_obj_new_str(line, (size_t)strlen(line));
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_ssh_status_obj, py_ssh_status);
PM_METAL_PY_BIND(g_py_bind_ssh_status, "pymergetic.metal.net.ssh", "status", py_ssh_status_obj,
                 PM_METAL_PY_SYNC);
