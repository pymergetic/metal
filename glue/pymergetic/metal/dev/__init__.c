/*
 * pymergetic.metal.dev — package node (nested builtins; firmware leaves).
 */
#include "py/obj.h"

#include "../modules.h"

#if !defined(PM_METAL_CFG_FW_BROWSER) || !PM_METAL_CFG_FW_BROWSER

static const mp_rom_map_elem_t dev_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_dev) },
    { MP_ROM_QSTR(MP_QSTR_serial), MP_ROM_PTR(&mp_module_pymergetic_metal_dev_serial) },
};
static MP_DEFINE_CONST_DICT(dev_globals, dev_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_dev = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&dev_globals,
};

#endif
