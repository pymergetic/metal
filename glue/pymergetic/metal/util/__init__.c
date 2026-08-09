/*
 * pymergetic.metal.util — package node (nested builtins).
 */
#include "py/obj.h"

#include "../modules.h"

static const mp_rom_map_elem_t util_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_util) },
    { MP_ROM_QSTR(MP_QSTR_lz4), MP_ROM_PTR(&mp_module_pymergetic_metal_util_lz4) },
    { MP_ROM_QSTR(MP_QSTR_size), MP_ROM_PTR(&mp_module_pymergetic_metal_util_size) },
    { MP_ROM_QSTR(MP_QSTR_endian), MP_ROM_PTR(&mp_module_pymergetic_metal_util_endian) },
    { MP_ROM_QSTR(MP_QSTR_fourcc), MP_ROM_PTR(&mp_module_pymergetic_metal_util_fourcc) },
    { MP_ROM_QSTR(MP_QSTR_eightcc), MP_ROM_PTR(&mp_module_pymergetic_metal_util_eightcc) },
#if !defined(PM_METAL_CFG_FW_BROWSER) || !PM_METAL_CFG_FW_BROWSER
    { MP_ROM_QSTR(MP_QSTR_tar), MP_ROM_PTR(&mp_module_pymergetic_metal_util_tar) },
#endif
};
static MP_DEFINE_CONST_DICT(util_globals, util_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_util = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&util_globals,
};
