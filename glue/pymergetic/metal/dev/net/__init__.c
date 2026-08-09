/*
 * pymergetic.metal.dev.net — package node.
 */
#include "py/obj.h"

#include "../../modules.h"

static const mp_rom_map_elem_t dev_net_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_net) },
    { MP_ROM_QSTR(MP_QSTR_bge), MP_ROM_PTR(&mp_module_pymergetic_metal_dev_net_bge) },
    { MP_ROM_QSTR(MP_QSTR_virtio_net), MP_ROM_PTR(&mp_module_pymergetic_metal_dev_net_virtio_net) },
};
static MP_DEFINE_CONST_DICT(dev_net_globals, dev_net_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_dev_net = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&dev_net_globals,
};
