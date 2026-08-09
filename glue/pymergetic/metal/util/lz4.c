/*
 * pymergetic.metal.util.lz4 — µPy face (callee: src/.../util/lz4).
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/util/lz4/__init__.h>
#include <pymergetic/metal/reg/seats.h>

static mp_obj_t lz4_compress_bound(mp_obj_t n_obj)
{
    size_t n = (size_t)mp_obj_get_int(n_obj);
    return mp_obj_new_int((mp_int_t)pm_metal_util_lz4_compress_bound(n));
}
static MP_DEFINE_CONST_FUN_OBJ_1(lz4_compress_bound_obj, lz4_compress_bound);

static mp_obj_t lz4_decompress(mp_obj_t src_obj, mp_obj_t dst_cap_obj)
{
    mp_buffer_info_t src;
    size_t dst_cap;
    size_t out_len = 0;
    vstr_t vstr;
    int32_t rc;

    mp_get_buffer_raise(src_obj, &src, MP_BUFFER_READ);
    dst_cap = (size_t)mp_obj_get_int(dst_cap_obj);
    if (dst_cap == 0u) {
        mp_raise_ValueError(MP_ERROR_TEXT("dst_cap"));
    }
    vstr_init_len(&vstr, dst_cap);
    rc = pm_metal_util_lz4_decompress_safe((const uint8_t *)src.buf, src.len,
                                           (uint8_t *)vstr.buf, dst_cap, &out_len);
    if (rc != 0) {
        vstr_clear(&vstr);
        mp_raise_ValueError(MP_ERROR_TEXT("lz4 decompress"));
    }
    {
        mp_obj_t out = mp_obj_new_bytes((const byte *)vstr.buf, out_len);
        vstr_clear(&vstr);
        return out;
    }
}
static MP_DEFINE_CONST_FUN_OBJ_2(lz4_decompress_obj, lz4_decompress);

static mp_obj_t lz4_compress(mp_obj_t src_obj)
{
    mp_buffer_info_t src;
    size_t bound;
    vstr_t vstr;
    int32_t n;

    mp_get_buffer_raise(src_obj, &src, MP_BUFFER_READ);
    bound = pm_metal_util_lz4_compress_bound(src.len);
    if (bound == 0u) {
        mp_raise_ValueError(MP_ERROR_TEXT("lz4 bound"));
    }
    vstr_init_len(&vstr, bound);
    n = pm_metal_util_lz4_compress((const uint8_t *)src.buf, src.len, (uint8_t *)vstr.buf,
                                   bound);
    if (n < 0) {
        vstr_clear(&vstr);
        mp_raise_ValueError(MP_ERROR_TEXT("lz4 compress"));
    }
    {
        mp_obj_t out = mp_obj_new_bytes((const byte *)vstr.buf, (size_t)n);
        vstr_clear(&vstr);
        return out;
    }
}
static MP_DEFINE_CONST_FUN_OBJ_1(lz4_compress_obj, lz4_compress);

static const mp_rom_map_elem_t lz4_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_util_dot_lz4) },
    { MP_ROM_QSTR(MP_QSTR_compress_bound), MP_ROM_PTR(&lz4_compress_bound_obj) },
    { MP_ROM_QSTR(MP_QSTR_compress), MP_ROM_PTR(&lz4_compress_obj) },
    { MP_ROM_QSTR(MP_QSTR_decompress), MP_ROM_PTR(&lz4_decompress_obj) },
};
static MP_DEFINE_CONST_DICT(lz4_globals, lz4_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_util_lz4 = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&lz4_globals,
};

PM_METAL_REG_SEAT(g_pm_seat_util_lz4, "pymergetic.metal.util.lz4", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
