/*
 * pymergetic.metal.util.endian — µPy face (callee: src/.../util/endian).
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/util/endian/__init__.h>

static mp_obj_t endian_host_is_le(void)
{
    return mp_obj_new_bool(pm_metal_util_endian_host_is_le());
}
static MP_DEFINE_CONST_FUN_OBJ_0(endian_host_is_le_obj, endian_host_is_le);

static mp_obj_t endian_load_u16_le(mp_obj_t src_obj)
{
    mp_buffer_info_t src;

    mp_get_buffer_raise(src_obj, &src, MP_BUFFER_READ);
    if (src.len < 2u) {
        mp_raise_ValueError(MP_ERROR_TEXT("endian u16"));
    }
    return mp_obj_new_int_from_uint((mp_uint_t)pm_metal_util_endian_load_u16_le((const uint8_t *)src.buf));
}
static MP_DEFINE_CONST_FUN_OBJ_1(endian_load_u16_le_obj, endian_load_u16_le);

static mp_obj_t endian_store_u16_le(mp_obj_t v_obj)
{
    uint8_t out[2];
    uint16_t v = (uint16_t)mp_obj_get_int(v_obj);

    pm_metal_util_endian_store_u16_le(out, v);
    return mp_obj_new_bytes(out, 2);
}
static MP_DEFINE_CONST_FUN_OBJ_1(endian_store_u16_le_obj, endian_store_u16_le);

static mp_obj_t endian_load_u32_le(mp_obj_t src_obj)
{
    mp_buffer_info_t src;

    mp_get_buffer_raise(src_obj, &src, MP_BUFFER_READ);
    if (src.len < 4u) {
        mp_raise_ValueError(MP_ERROR_TEXT("endian u32"));
    }
    return mp_obj_new_int_from_uint((mp_uint_t)pm_metal_util_endian_load_u32_le((const uint8_t *)src.buf));
}
static MP_DEFINE_CONST_FUN_OBJ_1(endian_load_u32_le_obj, endian_load_u32_le);

static mp_obj_t endian_store_u32_le(mp_obj_t v_obj)
{
    uint8_t out[4];
    uint32_t v = (uint32_t)mp_obj_get_int(v_obj);

    pm_metal_util_endian_store_u32_le(out, v);
    return mp_obj_new_bytes(out, 4);
}
static MP_DEFINE_CONST_FUN_OBJ_1(endian_store_u32_le_obj, endian_store_u32_le);

static mp_obj_t endian_load_u64_le(mp_obj_t src_obj)
{
    mp_buffer_info_t src;

    mp_get_buffer_raise(src_obj, &src, MP_BUFFER_READ);
    if (src.len < 8u) {
        mp_raise_ValueError(MP_ERROR_TEXT("endian u64"));
    }
    return mp_obj_new_int_from_ull(pm_metal_util_endian_load_u64_le((const uint8_t *)src.buf));
}
static MP_DEFINE_CONST_FUN_OBJ_1(endian_load_u64_le_obj, endian_load_u64_le);

static mp_obj_t endian_store_u64_le(mp_obj_t v_obj)
{
    uint8_t out[8];
    uint64_t v = (uint64_t)mp_obj_get_int(v_obj);

    pm_metal_util_endian_store_u64_le(out, v);
    return mp_obj_new_bytes(out, 8);
}
static MP_DEFINE_CONST_FUN_OBJ_1(endian_store_u64_le_obj, endian_store_u64_le);

static const mp_rom_map_elem_t endian_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_util_dot_endian) },
    { MP_ROM_QSTR(MP_QSTR_WIRE_IS_LE), MP_ROM_INT(PM_METAL_UTIL_WIRE_ENDIAN_IS_LE) },
    { MP_ROM_QSTR(MP_QSTR_host_is_le), MP_ROM_PTR(&endian_host_is_le_obj) },
    { MP_ROM_QSTR(MP_QSTR_load_u16_le), MP_ROM_PTR(&endian_load_u16_le_obj) },
    { MP_ROM_QSTR(MP_QSTR_store_u16_le), MP_ROM_PTR(&endian_store_u16_le_obj) },
    { MP_ROM_QSTR(MP_QSTR_load_u32_le), MP_ROM_PTR(&endian_load_u32_le_obj) },
    { MP_ROM_QSTR(MP_QSTR_store_u32_le), MP_ROM_PTR(&endian_store_u32_le_obj) },
    { MP_ROM_QSTR(MP_QSTR_load_u64_le), MP_ROM_PTR(&endian_load_u64_le_obj) },
    { MP_ROM_QSTR(MP_QSTR_store_u64_le), MP_ROM_PTR(&endian_store_u64_le_obj) },
};
static MP_DEFINE_CONST_DICT(endian_globals, endian_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_util_endian = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&endian_globals,
};
