/*
 * pymergetic.metal.util.fourcc — µPy face (callee: src/.../util/fourcc).
 * Tag values are semantic BE u32 ints on this face.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/util/fourcc/__init__.h>

static mp_obj_t fourcc_from_u32(mp_obj_t v_obj)
{
    pm_metal_util_fourcc_t tag;
    uint32_t v = (uint32_t)mp_obj_get_int(v_obj);

    pm_metal_util_fourcc_from_u32(&tag, v);
    return mp_obj_new_int_from_uint((mp_uint_t)pm_metal_util_fourcc_to_u32(&tag));
}
static MP_DEFINE_CONST_FUN_OBJ_1(fourcc_from_u32_obj, fourcc_from_u32);

static mp_obj_t fourcc_to_u32(mp_obj_t tag_obj)
{
    pm_metal_util_fourcc_t tag;
    uint32_t v = (uint32_t)mp_obj_get_int(tag_obj);

    pm_metal_util_fourcc_from_u32(&tag, v);
    return mp_obj_new_int_from_uint((mp_uint_t)pm_metal_util_fourcc_to_u32(&tag));
}
static MP_DEFINE_CONST_FUN_OBJ_1(fourcc_to_u32_obj, fourcc_to_u32);

static mp_obj_t fourcc_from_wire_bytes(mp_obj_t src_obj)
{
    mp_buffer_info_t src;
    pm_metal_util_fourcc_t tag;

    mp_get_buffer_raise(src_obj, &src, MP_BUFFER_READ);
    if (src.len < PM_METAL_UTIL_FOURCC_LEN) {
        mp_raise_ValueError(MP_ERROR_TEXT("fourcc wire"));
    }
    pm_metal_util_fourcc_from_wire_bytes((const uint8_t *)src.buf, &tag);
    return mp_obj_new_int_from_uint((mp_uint_t)pm_metal_util_fourcc_to_u32(&tag));
}
static MP_DEFINE_CONST_FUN_OBJ_1(fourcc_from_wire_bytes_obj, fourcc_from_wire_bytes);

static mp_obj_t fourcc_to_wire_bytes(mp_obj_t tag_obj)
{
    pm_metal_util_fourcc_t tag;
    pm_metal_util_fourcc_wire_t out;
    uint32_t v = (uint32_t)mp_obj_get_int(tag_obj);

    pm_metal_util_fourcc_from_u32(&tag, v);
    pm_metal_util_fourcc_to_wire_bytes(&tag, out);
    return mp_obj_new_bytes(out, PM_METAL_UTIL_FOURCC_LEN);
}
static MP_DEFINE_CONST_FUN_OBJ_1(fourcc_to_wire_bytes_obj, fourcc_to_wire_bytes);

static mp_obj_t fourcc_from_bytes(mp_obj_t src_obj)
{
    mp_buffer_info_t src;
    pm_metal_util_fourcc_t tag;

    mp_get_buffer_raise(src_obj, &src, MP_BUFFER_READ);
    if (src.len < PM_METAL_UTIL_FOURCC_LEN) {
        mp_raise_ValueError(MP_ERROR_TEXT("fourcc bytes"));
    }
    pm_metal_util_fourcc_from_bytes((const uint8_t *)src.buf, &tag);
    return mp_obj_new_int_from_uint((mp_uint_t)pm_metal_util_fourcc_to_u32(&tag));
}
static MP_DEFINE_CONST_FUN_OBJ_1(fourcc_from_bytes_obj, fourcc_from_bytes);

static mp_obj_t fourcc_to_bytes(mp_obj_t tag_obj)
{
    pm_metal_util_fourcc_t tag;
    uint8_t out[PM_METAL_UTIL_FOURCC_LEN];
    uint32_t v = (uint32_t)mp_obj_get_int(tag_obj);

    pm_metal_util_fourcc_from_u32(&tag, v);
    pm_metal_util_fourcc_to_bytes(&tag, out);
    return mp_obj_new_bytes(out, PM_METAL_UTIL_FOURCC_LEN);
}
static MP_DEFINE_CONST_FUN_OBJ_1(fourcc_to_bytes_obj, fourcc_to_bytes);

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

static mp_obj_t fourcc_to_string(mp_obj_t tag_obj)
{
    char out[PM_METAL_UTIL_FOURCC_LEN + 1];
    pm_metal_util_fourcc_t tag;
    uint32_t v = (uint32_t)mp_obj_get_int(tag_obj);

    pm_metal_util_fourcc_from_u32(&tag, v);
    if (pm_metal_util_fourcc_to_string(&tag, out) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("fourcc string"));
    }
    return mp_obj_new_str(out, PM_METAL_UTIL_FOURCC_LEN);
}
static MP_DEFINE_CONST_FUN_OBJ_1(fourcc_to_string_obj, fourcc_to_string);

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
    { MP_ROM_QSTR(MP_QSTR_from_u32), MP_ROM_PTR(&fourcc_from_u32_obj) },
    { MP_ROM_QSTR(MP_QSTR_to_u32), MP_ROM_PTR(&fourcc_to_u32_obj) },
    { MP_ROM_QSTR(MP_QSTR_from_wire_bytes), MP_ROM_PTR(&fourcc_from_wire_bytes_obj) },
    { MP_ROM_QSTR(MP_QSTR_to_wire_bytes), MP_ROM_PTR(&fourcc_to_wire_bytes_obj) },
    { MP_ROM_QSTR(MP_QSTR_from_bytes), MP_ROM_PTR(&fourcc_from_bytes_obj) },
    { MP_ROM_QSTR(MP_QSTR_to_bytes), MP_ROM_PTR(&fourcc_to_bytes_obj) },
    { MP_ROM_QSTR(MP_QSTR_from_string), MP_ROM_PTR(&fourcc_from_string_obj) },
    { MP_ROM_QSTR(MP_QSTR_to_string), MP_ROM_PTR(&fourcc_to_string_obj) },
    { MP_ROM_QSTR(MP_QSTR_label), MP_ROM_PTR(&fourcc_label_obj) },
};
static MP_DEFINE_CONST_DICT(fourcc_globals, fourcc_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_util_fourcc = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&fourcc_globals,
};
