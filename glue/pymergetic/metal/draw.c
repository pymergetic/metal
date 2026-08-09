/*
 * pymergetic.metal.draw — µPy face (callee: src/.../draw).
 * Surface is a dict: pixels (buffer), width, height, stride, bpp.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/draw.h>

static void draw_surface_from_obj(mp_obj_t obj, pm_metal_draw_surface_t *s)
{
    mp_obj_t pixels_obj = mp_obj_dict_get(obj, MP_OBJ_NEW_QSTR(MP_QSTR_pixels));
    mp_buffer_info_t pixels;

    mp_get_buffer_raise(pixels_obj, &pixels, MP_BUFFER_RW);
    s->pixels = (uint8_t *)pixels.buf;
    s->width = (uint32_t)mp_obj_get_int(mp_obj_dict_get(obj, MP_OBJ_NEW_QSTR(MP_QSTR_width)));
    s->height = (uint32_t)mp_obj_get_int(mp_obj_dict_get(obj, MP_OBJ_NEW_QSTR(MP_QSTR_height)));
    s->stride = (uint32_t)mp_obj_get_int(mp_obj_dict_get(obj, MP_OBJ_NEW_QSTR(MP_QSTR_stride)));
    s->bpp = (uint8_t)mp_obj_get_int(mp_obj_dict_get(obj, MP_OBJ_NEW_QSTR(MP_QSTR_bpp)));
}

static mp_obj_t draw_soft_init(size_t n_args, const mp_obj_t *args)
{
    mp_buffer_info_t buf;
    pm_metal_draw_surface_t s;
    mp_obj_t dict;
    uint32_t w, h;
    uint8_t bpp;
    int32_t rc;
    (void)n_args;

    mp_get_buffer_raise(args[0], &buf, MP_BUFFER_RW);
    w = (uint32_t)mp_obj_get_int(args[1]);
    h = (uint32_t)mp_obj_get_int(args[2]);
    bpp = (uint8_t)mp_obj_get_int(args[3]);
    rc = pm_metal_draw_soft_init(&s, (uint8_t *)buf.buf, w, h, bpp);
    if (rc != 0) {
        return mp_const_none;
    }
    dict = mp_obj_new_dict(5);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_pixels), args[0]);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_width), mp_obj_new_int_from_uint(s.width));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_height), mp_obj_new_int_from_uint(s.height));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_stride), mp_obj_new_int_from_uint(s.stride));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_bpp), MP_OBJ_NEW_SMALL_INT(s.bpp));
    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(draw_soft_init_obj, 4, 4, draw_soft_init);

static mp_obj_t draw_fill(mp_obj_t surf_obj, mp_obj_t argb_obj)
{
    pm_metal_draw_surface_t s;
    draw_surface_from_obj(surf_obj, &s);
    pm_metal_draw_fill(&s, (uint32_t)mp_obj_get_int(argb_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(draw_fill_obj, draw_fill);

static mp_obj_t draw_pixel(size_t n_args, const mp_obj_t *args)
{
    pm_metal_draw_surface_t s;
    (void)n_args;
    draw_surface_from_obj(args[0], &s);
    pm_metal_draw_pixel(&s, (int32_t)mp_obj_get_int(args[1]), (int32_t)mp_obj_get_int(args[2]),
                        (uint32_t)mp_obj_get_int(args[3]));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(draw_pixel_obj, 4, 4, draw_pixel);

static mp_obj_t draw_glyph8(size_t n_args, const mp_obj_t *args)
{
    pm_metal_draw_surface_t s;
    const char *ch;
    (void)n_args;
    draw_surface_from_obj(args[0], &s);
    ch = mp_obj_str_get_str(args[3]);
    pm_metal_draw_glyph8(&s, (int32_t)mp_obj_get_int(args[1]), (int32_t)mp_obj_get_int(args[2]),
                         ch[0], (uint32_t)mp_obj_get_int(args[4]),
                         (uint32_t)mp_obj_get_int(args[5]));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(draw_glyph8_obj, 6, 6, draw_glyph8);

static mp_obj_t draw_text8(size_t n_args, const mp_obj_t *args)
{
    pm_metal_draw_surface_t s;
    (void)n_args;
    draw_surface_from_obj(args[0], &s);
    pm_metal_draw_text8(&s, (int32_t)mp_obj_get_int(args[1]), (int32_t)mp_obj_get_int(args[2]),
                        mp_obj_str_get_str(args[3]), (uint32_t)mp_obj_get_int(args[4]),
                        (uint32_t)mp_obj_get_int(args[5]));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(draw_text8_obj, 6, 6, draw_text8);

static mp_obj_t draw_checksum(mp_obj_t surf_obj)
{
    pm_metal_draw_surface_t s;
    draw_surface_from_obj(surf_obj, &s);
    return mp_obj_new_int_from_uint(pm_metal_draw_checksum(&s));
}
static MP_DEFINE_CONST_FUN_OBJ_1(draw_checksum_obj, draw_checksum);

static const mp_rom_map_elem_t draw_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_draw) },
    { MP_ROM_QSTR(MP_QSTR_soft_init), MP_ROM_PTR(&draw_soft_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill), MP_ROM_PTR(&draw_fill_obj) },
    { MP_ROM_QSTR(MP_QSTR_pixel), MP_ROM_PTR(&draw_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_glyph8), MP_ROM_PTR(&draw_glyph8_obj) },
    { MP_ROM_QSTR(MP_QSTR_text8), MP_ROM_PTR(&draw_text8_obj) },
    { MP_ROM_QSTR(MP_QSTR_checksum), MP_ROM_PTR(&draw_checksum_obj) },
};
static MP_DEFINE_CONST_DICT(draw_globals, draw_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_draw = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&draw_globals,
};
