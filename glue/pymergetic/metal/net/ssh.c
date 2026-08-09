/*
 * pymergetic.metal.net.ssh — µPy face (callee: src/.../net/ssh).
 * Firmware seats only.
 */
#include "py/obj.h"
#include "py/objstr.h"
#include "py/mperrno.h"
#include "py/runtime.h"

#include <pymergetic/metal/net/ssh/__init__.h>

static mp_obj_t ssh_available(void)
{
    return pm_metal_net_ssh_available() ? mp_const_true : mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_0(ssh_available_obj, ssh_available);

static mp_obj_t ssh_init(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_ssh_init());
}
static MP_DEFINE_CONST_FUN_OBJ_0(ssh_init_obj, ssh_init);

static mp_obj_t ssh_autoload(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_ssh_autoload());
}
static MP_DEFINE_CONST_FUN_OBJ_0(ssh_autoload_obj, ssh_autoload);

static mp_obj_t ssh_listen(mp_obj_t port_in)
{
    uint32_t h = pm_metal_net_ssh_listen((uint32_t)mp_obj_get_int(port_in));
    if (h == 0u) {
        mp_raise_OSError(MP_EOPNOTSUPP);
    }
    return MP_OBJ_NEW_SMALL_INT(h);
}
static MP_DEFINE_CONST_FUN_OBJ_1(ssh_listen_obj, ssh_listen);

static mp_obj_t ssh_close(mp_obj_t s_in)
{
    pm_metal_net_ssh_close((uint32_t)mp_obj_get_int(s_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ssh_close_obj, ssh_close);

static mp_obj_t ssh_poll(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_ssh_poll());
}
static MP_DEFINE_CONST_FUN_OBJ_0(ssh_poll_obj, ssh_poll);

static mp_obj_t ssh_served(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_ssh_served());
}
static MP_DEFINE_CONST_FUN_OBJ_0(ssh_served_obj, ssh_served);

static mp_obj_t ssh_client_exec(size_t n_args, const mp_obj_t *args)
{
    (void)n_args;
    (void)args;
    mp_raise_OSError(MP_EOPNOTSUPP);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ssh_client_exec_obj, 4, 5, ssh_client_exec);

static mp_obj_t ssh_banner_send(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_ssh_banner_send());
}
static MP_DEFINE_CONST_FUN_OBJ_0(ssh_banner_send_obj, ssh_banner_send);

static mp_obj_t ssh_banner_sent(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_ssh_banner_sent());
}
static MP_DEFINE_CONST_FUN_OBJ_0(ssh_banner_sent_obj, ssh_banner_sent);

static mp_obj_t ssh_banner_reset(void)
{
    pm_metal_net_ssh_banner_reset();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(ssh_banner_reset_obj, ssh_banner_reset);

static const MP_DEFINE_STR_OBJ(ssh_version_obj, "0");
static const MP_DEFINE_STR_OBJ(ssh_info_obj, "metal net.ssh diy-kex");

static const mp_rom_map_elem_t ssh_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_net_dot_ssh) },
    { MP_ROM_QSTR(MP_QSTR___version__), MP_ROM_PTR(&ssh_version_obj) },
    { MP_ROM_QSTR(MP_QSTR_info), MP_ROM_PTR(&ssh_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_available), MP_ROM_PTR(&ssh_available_obj) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&ssh_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_autoload), MP_ROM_PTR(&ssh_autoload_obj) },
    { MP_ROM_QSTR(MP_QSTR_listen), MP_ROM_PTR(&ssh_listen_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&ssh_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_poll), MP_ROM_PTR(&ssh_poll_obj) },
    { MP_ROM_QSTR(MP_QSTR_served), MP_ROM_PTR(&ssh_served_obj) },
    { MP_ROM_QSTR(MP_QSTR_client_exec), MP_ROM_PTR(&ssh_client_exec_obj) },
    { MP_ROM_QSTR(MP_QSTR_banner_send), MP_ROM_PTR(&ssh_banner_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_banner_sent), MP_ROM_PTR(&ssh_banner_sent_obj) },
    { MP_ROM_QSTR(MP_QSTR_banner_reset), MP_ROM_PTR(&ssh_banner_reset_obj) },
};
static MP_DEFINE_CONST_DICT(ssh_module_globals, ssh_module_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_net_ssh = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&ssh_module_globals,
};
