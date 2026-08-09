/*
 * pymergetic.metal.dev.gfx.text — µPy face (callee: gfx text helpers).
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/dev/gfx/gfx.h>

#if !defined(__wasm__)

static mp_obj_t text_font_width(void)
{
    return mp_obj_new_int_from_uint(pm_metal_gfx_font_width());
}
static MP_DEFINE_CONST_FUN_OBJ_0(text_font_width_obj, text_font_width);

static mp_obj_t text_font_height(void)
{
    return mp_obj_new_int_from_uint(pm_metal_gfx_font_height());
}
static MP_DEFINE_CONST_FUN_OBJ_0(text_font_height_obj, text_font_height);

static mp_obj_t text_draw(size_t n_args, const mp_obj_t *args)
{
    int transparent = 0;
    (void)n_args;
    if (n_args >= 6) {
        transparent = mp_obj_is_true(args[5]);
    }
    pm_metal_gfx_draw_text((int32_t)mp_obj_get_int(args[0]), (int32_t)mp_obj_get_int(args[1]),
                           mp_obj_str_get_str(args[2]), (pm_metal_gfx_color_t)mp_obj_get_int(args[3]),
                           (pm_metal_gfx_color_t)mp_obj_get_int(args[4]), transparent);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(text_draw_obj, 5, 6, text_draw);

static mp_obj_t text_rgb(mp_obj_t r_obj, mp_obj_t g_obj, mp_obj_t b_obj)
{
    return mp_obj_new_int_from_uint(
        PM_METAL_GFX_RGB(mp_obj_get_int(r_obj), mp_obj_get_int(g_obj), mp_obj_get_int(b_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_3(text_rgb_obj, text_rgb);

static const mp_rom_map_elem_t text_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),
      MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_gfx_dot_text) },
    { MP_ROM_QSTR(MP_QSTR_font_width), MP_ROM_PTR(&text_font_width_obj) },
    { MP_ROM_QSTR(MP_QSTR_font_height), MP_ROM_PTR(&text_font_height_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw), MP_ROM_PTR(&text_draw_obj) },
};
static MP_DEFINE_CONST_DICT(text_globals, text_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_dev_gfx_text = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&text_globals,
};

#else

static const mp_rom_map_elem_t text_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),
      MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_gfx_dot_text) },
};
static MP_DEFINE_CONST_DICT(text_globals, text_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_dev_gfx_text = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&text_globals,
};

#endif
