/*
 * pymergetic.metal.dev.input — package node.
 */
#include "py/obj.h"

#include "../../modules.h"

static const mp_rom_map_elem_t input_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_input) },
    { MP_ROM_QSTR(MP_QSTR_kbd), MP_ROM_PTR(&mp_module_pymergetic_metal_dev_input_kbd) },
};
static MP_DEFINE_CONST_DICT(input_globals, input_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_dev_input = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&input_globals,
};
