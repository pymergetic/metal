/*
 * pymergetic.metal.net.ssh — µPy face (callee: src/.../net/ssh).
 * Firmware seats only.
 */
#include <string.h>

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
    return mp_obj_new_int_from_uint(h);
}
static MP_DEFINE_CONST_FUN_OBJ_1(ssh_listen_obj, ssh_listen);

static mp_obj_t ssh_release(void)
{
    pm_metal_net_ssh_release();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(ssh_release_obj, ssh_release);

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

static mp_obj_t ssh_status(mp_obj_t cap_obj)
{
    uint32_t cap = (uint32_t)mp_obj_get_int(cap_obj);
    vstr_t vstr;
    int32_t rc;

    if (cap == 0u) {
        mp_raise_ValueError(MP_ERROR_TEXT("ssh status"));
    }
    vstr_init_len(&vstr, (size_t)cap);
    rc = pm_metal_net_ssh_status((uint8_t *)vstr.buf, cap);
    if (rc != 0) {
        vstr_clear(&vstr);
        return mp_const_none;
    }
    {
        mp_obj_t out = mp_obj_new_str(vstr.buf, strlen(vstr.buf));
        vstr_clear(&vstr);
        return out;
    }
}
static MP_DEFINE_CONST_FUN_OBJ_1(ssh_status_obj, ssh_status);

static mp_obj_t ssh_listen_port(void)
{
    return mp_obj_new_int_from_uint(pm_metal_net_ssh_listen_port());
}
static MP_DEFINE_CONST_FUN_OBJ_0(ssh_listen_port_obj, ssh_listen_port);

static mp_obj_t ssh_hostkey_label(mp_obj_t cap_obj)
{
    uint32_t cap = (uint32_t)mp_obj_get_int(cap_obj);
    vstr_t vstr;
    int32_t rc;

    if (cap == 0u) {
        mp_raise_ValueError(MP_ERROR_TEXT("ssh hostkey"));
    }
    vstr_init_len(&vstr, (size_t)cap);
    rc = pm_metal_net_ssh_hostkey_label((uint8_t *)vstr.buf, cap);
    if (rc != 0) {
        vstr_clear(&vstr);
        return mp_const_none;
    }
    {
        mp_obj_t out = mp_obj_new_str(vstr.buf, strlen(vstr.buf));
        vstr_clear(&vstr);
        return out;
    }
}
static MP_DEFINE_CONST_FUN_OBJ_1(ssh_hostkey_label_obj, ssh_hostkey_label);

static mp_obj_t ssh_client_exec(size_t n_args, const mp_obj_t *args)
{
    const char *host = mp_obj_str_get_str(args[0]);
    uint16_t port = (uint16_t)mp_obj_get_int(args[1]);
    const char *user = mp_obj_str_get_str(args[2]);
    const char *cmd = mp_obj_str_get_str(args[3]);
    mp_buffer_info_t buf;
    uint32_t len_out = 0;
    int32_t rc;

    if (n_args < 5 || args[4] == mp_const_none) {
        mp_raise_OSError(MP_EOPNOTSUPP);
    }
    mp_get_buffer_raise(args[4], &buf, MP_BUFFER_WRITE);
    rc = pm_metal_net_ssh_client_exec(host, port, user, cmd, (uint8_t *)buf.buf,
                                      (uint32_t)buf.len, &len_out);
    if (rc != 0) {
        return mp_obj_new_int(rc);
    }
    return mp_obj_new_int_from_uint(len_out);
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

static mp_obj_t ssh_bind_reg(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_ssh_bind_reg());
}
static MP_DEFINE_CONST_FUN_OBJ_0(ssh_bind_reg_obj, ssh_bind_reg);

static const mp_rom_map_elem_t ssh_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_net_dot_ssh) },
    { MP_ROM_QSTR(MP_QSTR_available), MP_ROM_PTR(&ssh_available_obj) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&ssh_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_autoload), MP_ROM_PTR(&ssh_autoload_obj) },
    { MP_ROM_QSTR(MP_QSTR_listen), MP_ROM_PTR(&ssh_listen_obj) },
    { MP_ROM_QSTR(MP_QSTR_release), MP_ROM_PTR(&ssh_release_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&ssh_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_poll), MP_ROM_PTR(&ssh_poll_obj) },
    { MP_ROM_QSTR(MP_QSTR_served), MP_ROM_PTR(&ssh_served_obj) },
    { MP_ROM_QSTR(MP_QSTR_status), MP_ROM_PTR(&ssh_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_listen_port), MP_ROM_PTR(&ssh_listen_port_obj) },
    { MP_ROM_QSTR(MP_QSTR_hostkey_label), MP_ROM_PTR(&ssh_hostkey_label_obj) },
    { MP_ROM_QSTR(MP_QSTR_client_exec), MP_ROM_PTR(&ssh_client_exec_obj) },
    { MP_ROM_QSTR(MP_QSTR_banner_send), MP_ROM_PTR(&ssh_banner_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_banner_sent), MP_ROM_PTR(&ssh_banner_sent_obj) },
    { MP_ROM_QSTR(MP_QSTR_banner_reset), MP_ROM_PTR(&ssh_banner_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_bind_reg), MP_ROM_PTR(&ssh_bind_reg_obj) },
};
static MP_DEFINE_CONST_DICT(ssh_module_globals, ssh_module_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_net_ssh = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&ssh_module_globals,
};
