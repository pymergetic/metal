/*
 * pymergetic.metal.dev.gfx — package node.
 */
#include "py/obj.h"

#include "../../modules.h"

static const mp_rom_map_elem_t gfx_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_gfx) },
    { MP_ROM_QSTR(MP_QSTR_compositor), MP_ROM_PTR(&mp_module_pymergetic_metal_dev_gfx_compositor) },
    { MP_ROM_QSTR(MP_QSTR_scanout), MP_ROM_PTR(&mp_module_pymergetic_metal_dev_gfx_scanout) },
    { MP_ROM_QSTR(MP_QSTR_text), MP_ROM_PTR(&mp_module_pymergetic_metal_dev_gfx_text) },
};
static MP_DEFINE_CONST_DICT(gfx_globals, gfx_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_dev_gfx = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&gfx_globals,
};
