/*
 * pymergetic.metal.util.size — µPy face (callee: src/.../util/size).
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/util/size/__init__.h>

static mp_obj_t util_size_format(mp_obj_t bytes_obj)
{
    uint8_t buf[PM_METAL_UTIL_SIZE_FORMAT_MAX];
    int32_t n;
    uint64_t bytes = (uint64_t)mp_obj_get_int(bytes_obj);

    n = pm_metal_util_size_format(buf, sizeof buf, bytes);
    if (n < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("size format"));
    }
    return mp_obj_new_str((const char *)buf, (size_t)n);
}
static MP_DEFINE_CONST_FUN_OBJ_1(util_size_format_obj, util_size_format);

static mp_obj_t util_size_format_bytes(mp_obj_t bytes_obj)
{
    uint8_t buf[48];
    int32_t n;
    uint64_t bytes = (uint64_t)mp_obj_get_int(bytes_obj);

    n = pm_metal_util_size_format_bytes(buf, sizeof buf, bytes);
    if (n < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("size format_bytes"));
    }
    return mp_obj_new_str((const char *)buf, (size_t)n);
}
static MP_DEFINE_CONST_FUN_OBJ_1(util_size_format_bytes_obj, util_size_format_bytes);

static const mp_rom_map_elem_t util_size_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_util_dot_size) },
    { MP_ROM_QSTR(MP_QSTR_format), MP_ROM_PTR(&util_size_format_obj) },
    { MP_ROM_QSTR(MP_QSTR_format_bytes), MP_ROM_PTR(&util_size_format_bytes_obj) },
};
static MP_DEFINE_CONST_DICT(util_size_globals, util_size_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_util_size = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&util_size_globals,
};
