/*
 * pymergetic.metal.net.dhcp — µPy face (callee: src/.../net/dhcp).
 * Firmware seats only.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/net/dhcp/__init__.h>

static mp_obj_t dhcp_run(void)
{
    pm_metal_net_dhcp_lease_t lease;
    mp_obj_t dict;
    int32_t rc = pm_metal_net_dhcp_run(&lease);

    if (rc != 0) {
        return mp_obj_new_int(rc);
    }
    dict = mp_obj_new_dict(5);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_yiaddr),
                      mp_obj_new_int_from_uint(lease.yiaddr));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_mask), mp_obj_new_int_from_uint(lease.mask));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_gw), mp_obj_new_int_from_uint(lease.gw));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_dns), mp_obj_new_int_from_uint(lease.dns));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_server),
                      mp_obj_new_int_from_uint(lease.server));
    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_0(dhcp_run_obj, dhcp_run);

static const mp_rom_map_elem_t dhcp_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_net_dot_dhcp) },
    { MP_ROM_QSTR(MP_QSTR_run), MP_ROM_PTR(&dhcp_run_obj) },
};
static MP_DEFINE_CONST_DICT(dhcp_globals, dhcp_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_net_dhcp = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&dhcp_globals,
};
