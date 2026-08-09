/*
 * pymergetic.metal.util.tar — µPy face.
 * Muscle: RS on firmware; C twin tar_block.c on browser/wasm.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/util/tar/__init__.h>

#include <string.h>

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

static int tar_list_ex_cb(uint8_t *ctx, const uint8_t *name, uint64_t size, int32_t is_dir,
                          uint64_t header_off, uint64_t payload_off, const uint8_t *data,
                          size_t data_len)
{
    tar_list_ctx_t *c = (tar_list_ctx_t *)ctx;
    mp_obj_t tup[5];
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
    tup[3] = mp_obj_new_int_from_ull(header_off);
    tup[4] = mp_obj_new_int_from_ull(payload_off);
    mp_obj_list_append(c->list, mp_obj_new_tuple(5, tup));
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

static mp_obj_t util_tar_list_ex(mp_obj_t archive_obj)
{
    mp_buffer_info_t arc;
    tar_list_ctx_t ctx;
    int32_t rc;

    mp_get_buffer_raise(archive_obj, &arc, MP_BUFFER_READ);
    ctx.list = mp_obj_new_list(0, NULL);
    rc = pm_metal_util_tar_foreach_ex((const uint8_t *)arc.buf, arc.len, tar_list_ex_cb,
                                      (uint8_t *)&ctx);
    if (rc < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("tar list_ex"));
    }
    return ctx.list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(util_tar_list_ex_obj, util_tar_list_ex);

static mp_obj_t util_tar_pad_len(mp_obj_t size_obj)
{
    uint64_t size = (uint64_t)mp_obj_get_int(size_obj);
    return mp_obj_new_int((mp_int_t)pm_metal_util_tar_pad_len(size));
}
static MP_DEFINE_CONST_FUN_OBJ_1(util_tar_pad_len_obj, util_tar_pad_len);

static mp_obj_t util_tar_write_header(size_t n_args, const mp_obj_t *args)
{
    const char *name;
    size_t name_len;
    uint8_t name_z[101];
    uint64_t size;
    uint8_t typeflag;
    uint8_t hdr[512];
    int32_t n;

    (void)n_args;
    name = mp_obj_str_get_data(args[0], &name_len);
    if (name_len == 0u || name_len >= sizeof name_z) {
        mp_raise_ValueError(MP_ERROR_TEXT("tar name"));
    }
    memcpy(name_z, name, name_len);
    name_z[name_len] = 0;
    size = (uint64_t)mp_obj_get_int(args[1]);
    typeflag = (uint8_t)mp_obj_get_int(args[2]);
    n = pm_metal_util_tar_write_header(hdr, sizeof hdr, name_z, size, typeflag);
    if (n < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("tar write_header"));
    }
    return mp_obj_new_bytes(hdr, (size_t)n);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(util_tar_write_header_obj, 3, 3, util_tar_write_header);

static mp_obj_t util_tar_write_end(void)
{
    uint8_t end[1024];
    int32_t n;

    n = pm_metal_util_tar_write_end(end, sizeof end);
    if (n < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("tar write_end"));
    }
    return mp_obj_new_bytes(end, (size_t)n);
}
static MP_DEFINE_CONST_FUN_OBJ_0(util_tar_write_end_obj, util_tar_write_end);

static const mp_rom_map_elem_t util_tar_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_util_dot_tar) },
    { MP_ROM_QSTR(MP_QSTR_list), MP_ROM_PTR(&util_tar_list_obj) },
    { MP_ROM_QSTR(MP_QSTR_list_ex), MP_ROM_PTR(&util_tar_list_ex_obj) },
    { MP_ROM_QSTR(MP_QSTR_pad_len), MP_ROM_PTR(&util_tar_pad_len_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_header), MP_ROM_PTR(&util_tar_write_header_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_end), MP_ROM_PTR(&util_tar_write_end_obj) },
};
static MP_DEFINE_CONST_DICT(util_tar_globals, util_tar_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_util_tar = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&util_tar_globals,
};
