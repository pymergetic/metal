/*
 * pymergetic.metal.util.tar — µPy face (callee: src/.../util/tar RS).
 * Firmware seats only (not nested on browser).
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/util/tar/__init__.h>

typedef struct {
    mp_obj_t list;
} tar_list_ctx_t;

static int tar_list_cb(uint8_t *ctx, const uint8_t *name, uint64_t size, int32_t is_dir,
                       const uint8_t *data, size_t data_len)
{
    tar_list_ctx_t *c = (tar_list_ctx_t *)ctx;
    mp_obj_t tup[3];
    size_t nlen = 0;
    (void)data;
    (void)data_len;

    if (name == NULL) {
        return -1;
    }
    while (name[nlen] != 0 && nlen < 100u) {
        nlen++;
    }
    tup[0] = mp_obj_new_str((const char *)name, nlen);
    tup[1] = mp_obj_new_int_from_ull(size);
    tup[2] = is_dir ? mp_const_true : mp_const_false;
    mp_obj_list_append(c->list, mp_obj_new_tuple(3, tup));
    return 0;
}

static mp_obj_t util_tar_list(mp_obj_t archive_obj)
{
    mp_buffer_info_t arc;
    tar_list_ctx_t ctx;
    int32_t rc;

    mp_get_buffer_raise(archive_obj, &arc, MP_BUFFER_READ);
    ctx.list = mp_obj_new_list(0, NULL);
    rc = pm_metal_util_tar_foreach((const uint8_t *)arc.buf, arc.len, tar_list_cb,
                                   (uint8_t *)&ctx);
    if (rc < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("tar list"));
    }
    return ctx.list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(util_tar_list_obj, util_tar_list);

static mp_obj_t util_tar_pad_len(mp_obj_t size_obj)
{
    uint64_t size = (uint64_t)mp_obj_get_int(size_obj);
    return mp_obj_new_int((mp_int_t)pm_metal_util_tar_pad_len(size));
}
static MP_DEFINE_CONST_FUN_OBJ_1(util_tar_pad_len_obj, util_tar_pad_len);

static const mp_rom_map_elem_t util_tar_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_util_dot_tar) },
    { MP_ROM_QSTR(MP_QSTR_list), MP_ROM_PTR(&util_tar_list_obj) },
    { MP_ROM_QSTR(MP_QSTR_pad_len), MP_ROM_PTR(&util_tar_pad_len_obj) },
};
static MP_DEFINE_CONST_DICT(util_tar_globals, util_tar_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_util_tar = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&util_tar_globals,
};
