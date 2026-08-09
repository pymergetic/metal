/*
 * pymergetic.metal.util.eightcc — µPy face (callee: src/.../util/eightcc).
 * Tag values are semantic BE u64 ints on this face.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/util/eightcc/__init__.h>
#include <pymergetic/metal/reg/seats.h>

static mp_obj_t eightcc_from_u64(mp_obj_t v_obj)
{
    pm_metal_util_eightcc_t tag;
    uint64_t v = (uint64_t)mp_obj_get_int(v_obj);

    pm_metal_util_eightcc_from_u64(&tag, v);
    return mp_obj_new_int_from_ull(pm_metal_util_eightcc_to_u64(&tag));
}
static MP_DEFINE_CONST_FUN_OBJ_1(eightcc_from_u64_obj, eightcc_from_u64);

static mp_obj_t eightcc_to_u64(mp_obj_t tag_obj)
{
    pm_metal_util_eightcc_t tag;
    uint64_t v = (uint64_t)mp_obj_get_int(tag_obj);

    pm_metal_util_eightcc_from_u64(&tag, v);
    return mp_obj_new_int_from_ull(pm_metal_util_eightcc_to_u64(&tag));
}
static MP_DEFINE_CONST_FUN_OBJ_1(eightcc_to_u64_obj, eightcc_to_u64);

static mp_obj_t eightcc_from_wire_bytes(mp_obj_t src_obj)
{
    mp_buffer_info_t src;
    pm_metal_util_eightcc_t tag;

    mp_get_buffer_raise(src_obj, &src, MP_BUFFER_READ);
    if (src.len < PM_METAL_UTIL_EIGHTCC_LEN) {
        mp_raise_ValueError(MP_ERROR_TEXT("eightcc wire"));
    }
    pm_metal_util_eightcc_from_wire_bytes((const uint8_t *)src.buf, &tag);
    return mp_obj_new_int_from_ull(pm_metal_util_eightcc_to_u64(&tag));
}
static MP_DEFINE_CONST_FUN_OBJ_1(eightcc_from_wire_bytes_obj, eightcc_from_wire_bytes);

static mp_obj_t eightcc_to_wire_bytes(mp_obj_t tag_obj)
{
    pm_metal_util_eightcc_t tag;
    pm_metal_util_eightcc_wire_t out;
    uint64_t v = (uint64_t)mp_obj_get_int(tag_obj);

    pm_metal_util_eightcc_from_u64(&tag, v);
    pm_metal_util_eightcc_to_wire_bytes(&tag, out);
    return mp_obj_new_bytes(out, PM_METAL_UTIL_EIGHTCC_LEN);
}
static MP_DEFINE_CONST_FUN_OBJ_1(eightcc_to_wire_bytes_obj, eightcc_to_wire_bytes);

static mp_obj_t eightcc_from_bytes(mp_obj_t src_obj)
{
    mp_buffer_info_t src;
    pm_metal_util_eightcc_t tag;

    mp_get_buffer_raise(src_obj, &src, MP_BUFFER_READ);
    if (src.len < PM_METAL_UTIL_EIGHTCC_LEN) {
        mp_raise_ValueError(MP_ERROR_TEXT("eightcc bytes"));
    }
    pm_metal_util_eightcc_from_bytes((const uint8_t *)src.buf, &tag);
    return mp_obj_new_int_from_ull(pm_metal_util_eightcc_to_u64(&tag));
}
static MP_DEFINE_CONST_FUN_OBJ_1(eightcc_from_bytes_obj, eightcc_from_bytes);

static mp_obj_t eightcc_to_bytes(mp_obj_t tag_obj)
{
    pm_metal_util_eightcc_t tag;
    uint8_t out[PM_METAL_UTIL_EIGHTCC_LEN];
    uint64_t v = (uint64_t)mp_obj_get_int(tag_obj);

    pm_metal_util_eightcc_from_u64(&tag, v);
    pm_metal_util_eightcc_to_bytes(&tag, out);
    return mp_obj_new_bytes(out, PM_METAL_UTIL_EIGHTCC_LEN);
}
static MP_DEFINE_CONST_FUN_OBJ_1(eightcc_to_bytes_obj, eightcc_to_bytes);

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

static mp_obj_t eightcc_to_string(mp_obj_t tag_obj)
{
    char out[PM_METAL_UTIL_EIGHTCC_LEN + 1];
    pm_metal_util_eightcc_t tag;
    uint64_t v = (uint64_t)mp_obj_get_int(tag_obj);

    pm_metal_util_eightcc_from_u64(&tag, v);
    if (pm_metal_util_eightcc_to_string(&tag, out) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("eightcc string"));
    }
    return mp_obj_new_str(out, PM_METAL_UTIL_EIGHTCC_LEN);
}
static MP_DEFINE_CONST_FUN_OBJ_1(eightcc_to_string_obj, eightcc_to_string);

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
    { MP_ROM_QSTR(MP_QSTR_from_u64), MP_ROM_PTR(&eightcc_from_u64_obj) },
    { MP_ROM_QSTR(MP_QSTR_to_u64), MP_ROM_PTR(&eightcc_to_u64_obj) },
    { MP_ROM_QSTR(MP_QSTR_from_wire_bytes), MP_ROM_PTR(&eightcc_from_wire_bytes_obj) },
    { MP_ROM_QSTR(MP_QSTR_to_wire_bytes), MP_ROM_PTR(&eightcc_to_wire_bytes_obj) },
    { MP_ROM_QSTR(MP_QSTR_from_bytes), MP_ROM_PTR(&eightcc_from_bytes_obj) },
    { MP_ROM_QSTR(MP_QSTR_to_bytes), MP_ROM_PTR(&eightcc_to_bytes_obj) },
    { MP_ROM_QSTR(MP_QSTR_from_string), MP_ROM_PTR(&eightcc_from_string_obj) },
    { MP_ROM_QSTR(MP_QSTR_to_string), MP_ROM_PTR(&eightcc_to_string_obj) },
    { MP_ROM_QSTR(MP_QSTR_label), MP_ROM_PTR(&eightcc_label_obj) },
};
static MP_DEFINE_CONST_DICT(eightcc_globals, eightcc_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_util_eightcc = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&eightcc_globals,
};

PM_METAL_REG_SEAT(g_pm_seat_util_eightcc, "pymergetic.metal.util.eightcc", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
