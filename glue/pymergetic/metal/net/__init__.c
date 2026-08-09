/*
 * pymergetic.metal.net — package node (nested builtins; firmware leaves).
 */
#include "py/obj.h"

#include "../modules.h"

#if !defined(PM_METAL_CFG_FW_BROWSER) || !PM_METAL_CFG_FW_BROWSER
static const mp_rom_map_elem_t net_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_net) },
    { MP_ROM_QSTR(MP_QSTR_ip), MP_ROM_PTR(&mp_module_pymergetic_metal_net_ip) },
    { MP_ROM_QSTR(MP_QSTR_wg), MP_ROM_PTR(&mp_module_pymergetic_metal_net_wg) },
    { MP_ROM_QSTR(MP_QSTR_ssh), MP_ROM_PTR(&mp_module_pymergetic_metal_net_ssh) },
};
static MP_DEFINE_CONST_DICT(net_globals, net_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_net = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&net_globals,
};
#endif
