/*
 * pymergetic.metal.bus — package node (nested builtins; firmware leaves).
 */
#include "py/obj.h"

#include "../modules.h"

static const mp_rom_map_elem_t bus_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_bus) },
    { MP_ROM_QSTR(MP_QSTR_pci), MP_ROM_PTR(&mp_module_pymergetic_metal_bus_pci) },
    { MP_ROM_QSTR(MP_QSTR_virtio), MP_ROM_PTR(&mp_module_pymergetic_metal_bus_virtio) },
};
static MP_DEFINE_CONST_DICT(bus_globals, bus_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_bus = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&bus_globals,
};
