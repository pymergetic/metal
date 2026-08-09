/*
 * pymergetic.metal.dev.gfx.compositor — µPy face (callee: compositor.c / gfx.h).
 */
#include <string.h>

#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/dev/gfx/gfx.h>
#include <pymergetic/metal/reg/seats.h>

static mp_obj_t gfx_init(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_gfx_init());
}
static MP_DEFINE_CONST_FUN_OBJ_0(gfx_init_obj, gfx_init);

static mp_obj_t gfx_ready(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_gfx_ready());
}
static MP_DEFINE_CONST_FUN_OBJ_0(gfx_ready_obj, gfx_ready);

static mp_obj_t gfx_present(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_gfx_present());
}
static MP_DEFINE_CONST_FUN_OBJ_0(gfx_present_obj, gfx_present);

static mp_obj_t gfx_clear(mp_obj_t color_obj)
{
    pm_metal_gfx_clear((pm_metal_gfx_color_t)mp_obj_get_int(color_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(gfx_clear_obj, gfx_clear);

static const mp_rom_map_elem_t compositor_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),
      MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_gfx_dot_compositor) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&gfx_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_ready), MP_ROM_PTR(&gfx_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_present), MP_ROM_PTR(&gfx_present_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear), MP_ROM_PTR(&gfx_clear_obj) },
};
static MP_DEFINE_CONST_DICT(compositor_globals, compositor_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_dev_gfx_compositor = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&compositor_globals,
};

PM_METAL_REG_SEAT(g_pm_seat_dev_gfx_compositor, "pymergetic.metal.dev.gfx.compositor", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
