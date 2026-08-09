/*
 * pymergetic.metal.shell.ui — µPy face.
 * Firmware seats only.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/shell/ui.h>

static mp_obj_t ui_attach_console0(void)
{
    return mp_obj_new_int((mp_int_t)pm_metal_shell_ui_attach_console0());
}
static MP_DEFINE_CONST_FUN_OBJ_0(ui_attach_console0_obj, ui_attach_console0);

static mp_obj_t ui_present(void)
{
    return mp_obj_new_int((mp_int_t)pm_metal_shell_ui_present());
}
static MP_DEFINE_CONST_FUN_OBJ_0(ui_present_obj, ui_present);

static const mp_rom_map_elem_t ui_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_shell_dot_ui) },
    { MP_ROM_QSTR(MP_QSTR_attach_console0), MP_ROM_PTR(&ui_attach_console0_obj) },
    { MP_ROM_QSTR(MP_QSTR_present), MP_ROM_PTR(&ui_present_obj) },
};
static MP_DEFINE_CONST_DICT(ui_globals, ui_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_shell_ui = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&ui_globals,
};
