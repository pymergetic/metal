/*
 * pymergetic.metal.dev.gfx.scanout — µPy face (callee: scanout.h).
 * Pointers pass as int cookies (same pattern as other metal glue seats).
 */
#include <string.h>

#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/dev/gfx/scanout.h>
#include <pymergetic/metal/reg/seats.h>

static mp_obj_t scanout_bind(mp_obj_t bind_obj)
{
    return mp_obj_new_int(pm_metal_scanout_bind(
        (const pm_metal_scanout_bind_t *)(uintptr_t)mp_obj_get_int(bind_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(scanout_bind_obj, scanout_bind);

static mp_obj_t scanout_ops(void)
{
    return mp_obj_new_int((mp_int_t)(uintptr_t)pm_metal_scanout_ops());
}
static MP_DEFINE_CONST_FUN_OBJ_0(scanout_ops_obj, scanout_ops);

static mp_obj_t scanout_name(void)
{
    const char *n = pm_metal_scanout_name();
    if (n == NULL) {
        return mp_const_none;
    }
    return mp_obj_new_str(n, strlen(n));
}
static MP_DEFINE_CONST_FUN_OBJ_0(scanout_name_obj, scanout_name);

static mp_obj_t scanout_caps(void)
{
    return mp_obj_new_int_from_uint(pm_metal_scanout_caps());
}
static MP_DEFINE_CONST_FUN_OBJ_0(scanout_caps_obj, scanout_caps);

static mp_obj_t scanout_fini(void)
{
    pm_metal_scanout_fini();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(scanout_fini_obj, scanout_fini);

static mp_obj_t scanout_bind_info(void)
{
    return mp_obj_new_int((mp_int_t)(uintptr_t)pm_metal_scanout_bind_info());
}
static MP_DEFINE_CONST_FUN_OBJ_0(scanout_bind_info_obj, scanout_bind_info);

static mp_obj_t scanout_bind_set_shadow(mp_obj_t pixels_obj, mp_obj_t pitch_obj)
{
    pm_metal_scanout_bind_set_shadow((uint32_t *)(uintptr_t)mp_obj_get_int(pixels_obj),
                                     (uint32_t)mp_obj_get_int(pitch_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(scanout_bind_set_shadow_obj, scanout_bind_set_shadow);

static mp_obj_t scanout_copy_rect(size_t n_args, const mp_obj_t *args)
{
    (void)n_args;
    pm_metal_scanout_copy_rect((uint32_t *)(uintptr_t)mp_obj_get_int(args[0]),
                               (uint32_t)mp_obj_get_int(args[1]), (int32_t)mp_obj_get_int(args[2]),
                               (int32_t)mp_obj_get_int(args[3]), (int32_t)mp_obj_get_int(args[4]),
                               (int32_t)mp_obj_get_int(args[5]),
                               (const pm_metal_scanout_bind_t *)(uintptr_t)mp_obj_get_int(args[6]));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(scanout_copy_rect_obj, 7, 7, scanout_copy_rect);

static const mp_rom_map_elem_t scanout_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),
      MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_gfx_dot_scanout) },
    { MP_ROM_QSTR(MP_QSTR_bind), MP_ROM_PTR(&scanout_bind_obj) },
    { MP_ROM_QSTR(MP_QSTR_ops), MP_ROM_PTR(&scanout_ops_obj) },
    { MP_ROM_QSTR(MP_QSTR_name), MP_ROM_PTR(&scanout_name_obj) },
    { MP_ROM_QSTR(MP_QSTR_caps), MP_ROM_PTR(&scanout_caps_obj) },
    { MP_ROM_QSTR(MP_QSTR_fini), MP_ROM_PTR(&scanout_fini_obj) },
    { MP_ROM_QSTR(MP_QSTR_bind_info), MP_ROM_PTR(&scanout_bind_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_bind_set_shadow), MP_ROM_PTR(&scanout_bind_set_shadow_obj) },
    { MP_ROM_QSTR(MP_QSTR_copy_rect), MP_ROM_PTR(&scanout_copy_rect_obj) },
};
static MP_DEFINE_CONST_DICT(scanout_globals, scanout_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_dev_gfx_scanout = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&scanout_globals,
};

PM_METAL_REG_SEAT(g_pm_seat_dev_gfx_scanout, "pymergetic.metal.dev.gfx.scanout", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
