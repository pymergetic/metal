/*
 * pymergetic.metal.util.ascii — µPy face (callee: src/.../util/ascii).
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/util/ascii.h>

#include <string.h>
#include <pymergetic/metal/reg/seats.h>

static mp_obj_t ascii_bound(mp_obj_t text_len_obj)
{
    size_t n = (size_t)mp_obj_get_int(text_len_obj);
    return mp_obj_new_int((mp_int_t)pm_metal_util_ascii_bound(n));
}
static MP_DEFINE_CONST_FUN_OBJ_1(ascii_bound_obj, ascii_bound);

static mp_obj_t ascii_render(size_t n_args, const mp_obj_t *args)
{
    const char *text = mp_obj_str_get_str(args[0]);
    char ink = '#';
    size_t bound;
    vstr_t vstr;
    int n;

    if (n_args >= 2) {
        const char *ink_s = mp_obj_str_get_str(args[1]);
        if (ink_s[0] != '\0') {
            ink = ink_s[0];
        }
    }
    bound = pm_metal_util_ascii_bound(strlen(text));
    if (bound == 0u) {
        mp_raise_ValueError(MP_ERROR_TEXT("ascii bound"));
    }
    vstr_init_len(&vstr, bound);
    n = pm_metal_util_ascii_render(text, ink, vstr.buf, bound);
    if (n < 0) {
        vstr_clear(&vstr);
        mp_raise_ValueError(MP_ERROR_TEXT("ascii render"));
    }
    {
        mp_obj_t out = mp_obj_new_str(vstr.buf, (size_t)n);
        vstr_clear(&vstr);
        return out;
    }
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ascii_render_obj, 1, 2, ascii_render);

static mp_obj_t ascii_log(mp_obj_t text_obj)
{
    pm_metal_util_ascii_log(mp_obj_str_get_str(text_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ascii_log_obj, ascii_log);

static mp_obj_t ascii_log_cyan(mp_obj_t text_obj)
{
    pm_metal_util_ascii_log_cyan(mp_obj_str_get_str(text_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ascii_log_cyan_obj, ascii_log_cyan);

static mp_obj_t ascii_log_rainbow(mp_obj_t text_obj)
{
    pm_metal_util_ascii_log_rainbow(mp_obj_str_get_str(text_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ascii_log_rainbow_obj, ascii_log_rainbow);

static const mp_rom_map_elem_t ascii_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_util_dot_ascii) },
    { MP_ROM_QSTR(MP_QSTR_bound), MP_ROM_PTR(&ascii_bound_obj) },
    { MP_ROM_QSTR(MP_QSTR_render), MP_ROM_PTR(&ascii_render_obj) },
    { MP_ROM_QSTR(MP_QSTR_log), MP_ROM_PTR(&ascii_log_obj) },
    { MP_ROM_QSTR(MP_QSTR_log_cyan), MP_ROM_PTR(&ascii_log_cyan_obj) },
    { MP_ROM_QSTR(MP_QSTR_log_rainbow), MP_ROM_PTR(&ascii_log_rainbow_obj) },
};
static MP_DEFINE_CONST_DICT(ascii_globals, ascii_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_util_ascii = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&ascii_globals,
};

PM_METAL_REG_SEAT(g_pm_seat_util_ascii, "pymergetic.metal.util.ascii", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
