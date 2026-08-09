/*
 * pymergetic.metal.util.eightcc — µPy face (callee: src/.../util/eightcc).
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/util/eightcc/__init__.h>

static mp_obj_t eightcc_from_string(mp_obj_t s_obj)
{
    pm_metal_util_eightcc_t tag;
    const char *s = mp_obj_str_get_str(s_obj);

    if (pm_metal_util_eightcc_from_string(s, &tag) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("eightcc"));
    }
    return mp_obj_new_int_from_ull(pm_metal_util_eightcc_to_u64(&tag));
}
static MP_DEFINE_CONST_FUN_OBJ_1(eightcc_from_string_obj, eightcc_from_string);

static mp_obj_t eightcc_label(mp_obj_t magic_obj)
{
    char out[PM_METAL_UTIL_EIGHTCC_LEN + 1];
    uint64_t magic = (uint64_t)mp_obj_get_int(magic_obj);

    if (pm_metal_util_eightcc_label(magic, out) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("eightcc label"));
    }
    return mp_obj_new_str(out, PM_METAL_UTIL_EIGHTCC_LEN);
}
static MP_DEFINE_CONST_FUN_OBJ_1(eightcc_label_obj, eightcc_label);

static const mp_rom_map_elem_t eightcc_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_util_dot_eightcc) },
    { MP_ROM_QSTR(MP_QSTR_from_string), MP_ROM_PTR(&eightcc_from_string_obj) },
    { MP_ROM_QSTR(MP_QSTR_label), MP_ROM_PTR(&eightcc_label_obj) },
};
static MP_DEFINE_CONST_DICT(eightcc_globals, eightcc_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_util_eightcc = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&eightcc_globals,
};
