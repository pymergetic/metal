/*
 * pymergetic.metal.shell.tui — µPy face.
 * Firmware seats only.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/shell/tui/__init__.h>

static mp_obj_t tui_init(void)
{
    return mp_obj_new_int((mp_int_t)pm_metal_tui_init());
}
static MP_DEFINE_CONST_FUN_OBJ_0(tui_init_obj, tui_init);

static mp_obj_t tui_paint_vt(mp_obj_t vt_index_obj)
{
    mp_int_t vt_index = mp_obj_get_int(vt_index_obj);
    return mp_obj_new_int((mp_int_t)pm_metal_tui_paint_vt((int32_t)vt_index));
}
static MP_DEFINE_CONST_FUN_OBJ_1(tui_paint_vt_obj, tui_paint_vt);

static mp_obj_t tui_render_vt(mp_obj_t vt_index_obj)
{
    mp_int_t vt_index = mp_obj_get_int(vt_index_obj);
    return mp_obj_new_int((mp_int_t)pm_metal_tui_render_vt((int32_t)vt_index));
}
static MP_DEFINE_CONST_FUN_OBJ_1(tui_render_vt_obj, tui_render_vt);

static mp_obj_t tui_render_draw(mp_obj_t ds_obj)
{
    void *ds = (ds_obj == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(ds_obj);
    return mp_obj_new_int((mp_int_t)pm_metal_tui_render_draw(ds));
}
static MP_DEFINE_CONST_FUN_OBJ_1(tui_render_draw_obj, tui_render_draw);

static const mp_rom_map_elem_t tui_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_shell_dot_tui) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&tui_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_paint_vt), MP_ROM_PTR(&tui_paint_vt_obj) },
    { MP_ROM_QSTR(MP_QSTR_render_vt), MP_ROM_PTR(&tui_render_vt_obj) },
    { MP_ROM_QSTR(MP_QSTR_render_draw), MP_ROM_PTR(&tui_render_draw_obj) },
};
static MP_DEFINE_CONST_DICT(tui_globals, tui_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_shell_tui = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&tui_globals,
};
