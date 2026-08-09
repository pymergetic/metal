/*
 * pymergetic.metal.dev.blk — µPy face (callee: src/.../dev/blk).
 * Expose the six public symbols; matrix API recount → 6.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/dev/blk/__init__.h>

static mp_obj_t blk_detect(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_dev_blk_detect());
}
static MP_DEFINE_CONST_FUN_OBJ_0(blk_detect_obj, blk_detect);

static mp_obj_t blk_open(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_dev_blk_open());
}
static MP_DEFINE_CONST_FUN_OBJ_0(blk_open_obj, blk_open);

static mp_obj_t blk_capacity_sectors(void)
{
    return mp_obj_new_int_from_ull(pm_metal_dev_blk_capacity_sectors());
}
static MP_DEFINE_CONST_FUN_OBJ_0(blk_capacity_sectors_obj, blk_capacity_sectors);

static mp_obj_t blk_read(mp_obj_t lba_obj, mp_obj_t buf_obj, mp_obj_t nsec_obj)
{
    mp_buffer_info_t buf;
    mp_get_buffer_raise(buf_obj, &buf, MP_BUFFER_WRITE);
    return MP_OBJ_NEW_SMALL_INT(pm_metal_dev_blk_read((uint64_t)mp_obj_get_int(lba_obj), buf.buf,
                                                      (uint32_t)mp_obj_get_int(nsec_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_3(blk_read_obj, blk_read);

static mp_obj_t blk_read_async(mp_obj_t lba_obj, mp_obj_t buf_obj, mp_obj_t nsec_obj)
{
    mp_buffer_info_t buf;
    mp_get_buffer_raise(buf_obj, &buf, MP_BUFFER_WRITE);
    return mp_obj_new_int_from_uint(pm_metal_dev_blk_read_async(
        (uint64_t)mp_obj_get_int(lba_obj), buf.buf, (uint32_t)mp_obj_get_int(nsec_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_3(blk_read_async_obj, blk_read_async);

static mp_obj_t blk_result(mp_obj_t h_obj)
{
    return mp_obj_new_int_from_uint(pm_metal_dev_blk_result((uint32_t)mp_obj_get_int(h_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(blk_result_obj, blk_result);

static const mp_rom_map_elem_t blk_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_blk) },
    { MP_ROM_QSTR(MP_QSTR_detect), MP_ROM_PTR(&blk_detect_obj) },
    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&blk_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_capacity_sectors), MP_ROM_PTR(&blk_capacity_sectors_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&blk_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_async), MP_ROM_PTR(&blk_read_async_obj) },
    { MP_ROM_QSTR(MP_QSTR_result), MP_ROM_PTR(&blk_result_obj) },
};
static MP_DEFINE_CONST_DICT(blk_globals, blk_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_dev_blk = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&blk_globals,
};
