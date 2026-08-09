/*
 * pymergetic — org root on firmware seats.
 * Browser: wasmmod owns root pymergetic and nests mp_module_pymergetic_metal.
 */
#include "py/obj.h"

#include "metal/modules.h"

#if !defined(PM_METAL_CFG_FW_BROWSER) || !PM_METAL_CFG_FW_BROWSER

static const mp_rom_map_elem_t pymergetic_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic) },
    { MP_ROM_QSTR(MP_QSTR_metal), MP_ROM_PTR(&mp_module_pymergetic_metal) },
};
static MP_DEFINE_CONST_DICT(pymergetic_globals, pymergetic_globals_table);

const mp_obj_module_t mp_module_pymergetic = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&pymergetic_globals,
};

MP_REGISTER_MODULE(MP_QSTR_pymergetic, mp_module_pymergetic);

#endif
