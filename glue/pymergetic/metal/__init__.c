/*
 * pymergetic.metal — package node.
 * Builtin C/RS faces nest here; frozen CORE Py (inspect/arch) via __path__.
 */
#include "py/obj.h"
#include "py/objstr.h"

#include "modules.h"

static const MP_DEFINE_STR_OBJ(metal_path_obj, "pymergetic/metal");

static const mp_rom_map_elem_t metal_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal) },
    { MP_ROM_QSTR(MP_QSTR___path__), MP_ROM_PTR(&metal_path_obj) },
    { MP_ROM_QSTR(MP_QSTR_util), MP_ROM_PTR(&mp_module_pymergetic_metal_util) },
    { MP_ROM_QSTR(MP_QSTR_externals), MP_ROM_PTR(&mp_module_pymergetic_metal_externals) },
    { MP_ROM_QSTR(MP_QSTR_auth), MP_ROM_PTR(&mp_module_pymergetic_metal_auth) },
    { MP_ROM_QSTR(MP_QSTR_trust), MP_ROM_PTR(&mp_module_pymergetic_metal_trust) },
#if !defined(PM_METAL_CFG_FW_BROWSER) || !PM_METAL_CFG_FW_BROWSER
    { MP_ROM_QSTR(MP_QSTR_net), MP_ROM_PTR(&mp_module_pymergetic_metal_net) },
#endif
};
static MP_DEFINE_CONST_DICT(metal_globals, metal_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&metal_globals,
};
