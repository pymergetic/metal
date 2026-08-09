/*
 * pymergetic.metal.shell — package node (nested builtins; firmware leaves).
 */
#include "py/obj.h"

#include "../modules.h"

#if !defined(PM_METAL_CFG_FW_BROWSER) || !PM_METAL_CFG_FW_BROWSER

static const mp_rom_map_elem_t shell_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_shell) },
    { MP_ROM_QSTR(MP_QSTR_vt), MP_ROM_PTR(&mp_module_pymergetic_metal_shell_vt) },
    { MP_ROM_QSTR(MP_QSTR_tui), MP_ROM_PTR(&mp_module_pymergetic_metal_shell_tui) },
    { MP_ROM_QSTR(MP_QSTR_ui), MP_ROM_PTR(&mp_module_pymergetic_metal_shell_ui) },
};
static MP_DEFINE_CONST_DICT(shell_globals, shell_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_shell = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&shell_globals,
};

#endif
