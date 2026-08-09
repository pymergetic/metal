/*
 * pymergetic.metal.util.fourcc — µPy face (callee: src/.../util/fourcc).
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/util/fourcc/__init__.h>

static mp_obj_t fourcc_from_string(mp_obj_t s_obj)
{
    pm_metal_util_fourcc_t tag;
    const char *s = mp_obj_str_get_str(s_obj);

    if (pm_metal_util_fourcc_from_string(s, &tag) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("fourcc"));
    }
    return mp_obj_new_int_from_uint((mp_uint_t)pm_metal_util_fourcc_to_u32(&tag));
}
static MP_DEFINE_CONST_FUN_OBJ_1(fourcc_from_string_obj, fourcc_from_string);

static mp_obj_t fourcc_label(mp_obj_t magic_obj)
{
    char out[PM_METAL_UTIL_FOURCC_LEN + 1];
    uint32_t magic = (uint32_t)mp_obj_get_int(magic_obj);

    if (pm_metal_util_fourcc_label(magic, out) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("fourcc label"));
    }
    return mp_obj_new_str(out, PM_METAL_UTIL_FOURCC_LEN);
}
static MP_DEFINE_CONST_FUN_OBJ_1(fourcc_label_obj, fourcc_label);

static const mp_rom_map_elem_t fourcc_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_util_dot_fourcc) },
    { MP_ROM_QSTR(MP_QSTR_from_string), MP_ROM_PTR(&fourcc_from_string_obj) },
    { MP_ROM_QSTR(MP_QSTR_label), MP_ROM_PTR(&fourcc_label_obj) },
};
static MP_DEFINE_CONST_DICT(fourcc_globals, fourcc_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_util_fourcc = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&fourcc_globals,
};
