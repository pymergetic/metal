/*
 * pymergetic.metal.net.ip — µPy face (callee: src/.../net/ip).
 * Firmware seats only.
 */
#include <string.h>

#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"

#include "pymergetic/metal/net/ip/cfg.h"
#include "pymergetic/metal/net/ip/sock.h"
#include "services.h"

static mp_obj_t net_ip_if_count(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_ip_if_count());
}
static MP_DEFINE_CONST_FUN_OBJ_0(net_ip_if_count_obj, net_ip_if_count);

static mp_obj_t net_ip_if_status(void)
{
    char buf[512];
    if (pm_metal_net_ip_if_status(buf, sizeof(buf)) != 0) {
        return mp_const_none;
    }
    return mp_obj_new_str(buf, strlen(buf));
}
static MP_DEFINE_CONST_FUN_OBJ_0(net_ip_if_status_obj, net_ip_if_status);

static mp_obj_t net_ip_iface(mp_obj_t name_obj)
{
    pm_metal_net_ip_ifcfg_t cfg;
    const char *name = NULL;
    mp_obj_t dict;
    if (name_obj != mp_const_none) {
        name = mp_obj_str_get_str(name_obj);
    }
    if (pm_metal_net_ip_if_get_named(name, &cfg) != 0) {
        return mp_const_none;
    }
    dict = mp_obj_new_dict(8);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_name), mp_obj_new_str(cfg.name, strlen(cfg.name)));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_ip), mp_obj_new_str(cfg.ip, strlen(cfg.ip)));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_mask), mp_obj_new_str(cfg.mask, strlen(cfg.mask)));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_gw), mp_obj_new_str(cfg.gw, strlen(cfg.gw)));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_dns), mp_obj_new_str(cfg.dns, strlen(cfg.dns)));
    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_1(net_ip_iface_obj, net_ip_iface);

static mp_obj_t net_ip_services_start(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_services_start());
}
static MP_DEFINE_CONST_FUN_OBJ_0(net_ip_services_start_obj, net_ip_services_start);

static const mp_rom_map_elem_t net_ip_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_net_dot_ip) },
    { MP_ROM_QSTR(MP_QSTR_if_count), MP_ROM_PTR(&net_ip_if_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_if_status), MP_ROM_PTR(&net_ip_if_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_iface), MP_ROM_PTR(&net_ip_iface_obj) },
    { MP_ROM_QSTR(MP_QSTR_services_start), MP_ROM_PTR(&net_ip_services_start_obj) },
};
static MP_DEFINE_CONST_DICT(net_ip_globals, net_ip_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_net_ip = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&net_ip_globals,
};
