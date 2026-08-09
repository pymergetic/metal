/*
 * pymergetic.metal.mem — package node (nested builtins).
 */
#include "py/obj.h"

#include "../modules.h"

static const mp_rom_map_elem_t mem_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_mem) },
    { MP_ROM_QSTR(MP_QSTR_tlsf), MP_ROM_PTR(&mp_module_pymergetic_metal_mem_tlsf) },
    { MP_ROM_QSTR(MP_QSTR_port), MP_ROM_PTR(&mp_module_pymergetic_metal_mem_port) },
    { MP_ROM_QSTR(MP_QSTR_arena), MP_ROM_PTR(&mp_module_pymergetic_metal_mem_arena) },
    { MP_ROM_QSTR(MP_QSTR_lock), MP_ROM_PTR(&mp_module_pymergetic_metal_mem_lock) },
};
static MP_DEFINE_CONST_DICT(mem_globals, mem_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_mem = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mem_globals,
};
