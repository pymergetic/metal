/*
 * pymergetic.metal.fs.mtar — µPy face.
 */
#include "py/obj.h"
#include "py/runtime.h"
#include <pymergetic/metal/fs/mtar/__init__.h>
#include <string.h>


static const uint8_t *path_z(mp_obj_t path_obj, uint8_t *buf, size_t cap)
{
    size_t n;
    const char *s = mp_obj_str_get_data(path_obj, &n);
    if (n + 1u > cap) {
        mp_raise_ValueError(MP_ERROR_TEXT("mtar path"));
    }
    memcpy(buf, s, n);
    buf[n] = 0;
    return buf;
}

static mp_obj_t mtar_open_blob(mp_obj_t blob_obj)
{
    mp_buffer_info_t blob;
    mp_get_buffer_raise(blob_obj, &blob, MP_BUFFER_READ);
    return mp_obj_new_int_from_uint(pm_metal_fs_mtar_open_blob((const uint8_t *)blob.buf, blob.len));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mtar_open_blob_obj, mtar_open_blob);

static mp_obj_t mtar_open_owned(mp_obj_t blob_obj, mp_obj_t cap_obj)
{
    mp_buffer_info_t blob;
    mp_get_buffer_raise(blob_obj, &blob, MP_BUFFER_READ);
    return mp_obj_new_int_from_uint(pm_metal_fs_mtar_open_owned(
        (const uint8_t *)blob.buf, blob.len, (size_t)mp_obj_get_int(cap_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(mtar_open_owned_obj, mtar_open_owned);

static mp_obj_t mtar_mount(mp_obj_t target_obj, mp_obj_t blob_obj)
{
    uint8_t path[256];
    mp_buffer_info_t blob;
    mp_get_buffer_raise(blob_obj, &blob, MP_BUFFER_READ);
    return mp_obj_new_int(pm_metal_fs_mtar_mount(path_z(target_obj, path, sizeof path),
                                                 (const uint8_t *)blob.buf, blob.len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(mtar_mount_obj, mtar_mount);

static mp_obj_t mtar_mount_rw(mp_obj_t target_obj, mp_obj_t blob_obj, mp_obj_t cap_obj)
{
    uint8_t path[256];
    mp_buffer_info_t blob;
    mp_get_buffer_raise(blob_obj, &blob, MP_BUFFER_WRITE);
    return mp_obj_new_int(pm_metal_fs_mtar_mount_rw(path_z(target_obj, path, sizeof path),
                                                    (uint8_t *)blob.buf, blob.len,
                                                    (size_t)mp_obj_get_int(cap_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_3(mtar_mount_rw_obj, mtar_mount_rw);

static mp_obj_t mtar_empty(mp_obj_t cap_obj)
{
    size_t cap = (size_t)mp_obj_get_int(cap_obj);
    vstr_t vstr;
    size_t out_len = 0;
    int32_t rc;
    if (cap == 0u) {
        mp_raise_ValueError(MP_ERROR_TEXT("mtar empty"));
    }
    vstr_init_len(&vstr, cap);
    rc = pm_metal_fs_mtar_empty((uint8_t *)vstr.buf, cap, &out_len);
    if (rc != 0) {
        vstr_clear(&vstr);
        mp_raise_ValueError(MP_ERROR_TEXT("mtar empty"));
    }
    {
        mp_obj_t out = mp_obj_new_bytes((const byte *)vstr.buf, out_len);
        vstr_clear(&vstr);
        return out;
    }
}
static MP_DEFINE_CONST_FUN_OBJ_1(mtar_empty_obj, mtar_empty);

static mp_obj_t mtar_pack_simple(mp_obj_t files_obj, mp_obj_t cap_obj)
{
    size_t n, i, cap, out_len = 0;
    mp_obj_t *items;
    const uint8_t *names[32];
    const uint8_t *datas[32];
    uint32_t lens[32];
    uint8_t name_storage[32][128];
    mp_buffer_info_t data_bi[32];
    vstr_t vstr;
    int32_t rc;

    cap = (size_t)mp_obj_get_int(cap_obj);
    if (cap == 0u) {
        mp_raise_ValueError(MP_ERROR_TEXT("mtar pack"));
    }
    mp_obj_get_array(files_obj, &n, &items);
    if (n > 32u) {
        mp_raise_ValueError(MP_ERROR_TEXT("mtar pack"));
    }
    for (i = 0; i < n; i++) {
        size_t nlen, pn;
        const char *nm;
        mp_obj_t *pair;
        mp_obj_get_array(items[i], &pn, &pair);
        if (pn != 2u) {
            mp_raise_ValueError(MP_ERROR_TEXT("mtar pack pair"));
        }
        nm = mp_obj_str_get_data(pair[0], &nlen);
        if (nlen + 1u > sizeof name_storage[i]) {
            mp_raise_ValueError(MP_ERROR_TEXT("mtar pack name"));
        }
        memcpy(name_storage[i], nm, nlen);
        name_storage[i][nlen] = 0;
        names[i] = name_storage[i];
        mp_get_buffer_raise(pair[1], &data_bi[i], MP_BUFFER_READ);
        datas[i] = (const uint8_t *)data_bi[i].buf;
        lens[i] = (uint32_t)data_bi[i].len;
    }
    vstr_init_len(&vstr, cap);
    rc = pm_metal_fs_mtar_pack_simple(names, datas, lens, (uint32_t)n, (uint8_t *)vstr.buf, cap,
                                      &out_len);
    if (rc != 0) {
        vstr_clear(&vstr);
        mp_raise_ValueError(MP_ERROR_TEXT("mtar pack"));
    }
    {
        mp_obj_t out = mp_obj_new_bytes((const byte *)vstr.buf, out_len);
        vstr_clear(&vstr);
        return out;
    }
}
static MP_DEFINE_CONST_FUN_OBJ_2(mtar_pack_simple_obj, mtar_pack_simple);

static const mp_rom_map_elem_t mtar_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_fs_dot_mtar) },
    { MP_ROM_QSTR(MP_QSTR_open_blob), MP_ROM_PTR(&mtar_open_blob_obj) },
    { MP_ROM_QSTR(MP_QSTR_open_owned), MP_ROM_PTR(&mtar_open_owned_obj) },
    { MP_ROM_QSTR(MP_QSTR_mount), MP_ROM_PTR(&mtar_mount_obj) },
    { MP_ROM_QSTR(MP_QSTR_mount_rw), MP_ROM_PTR(&mtar_mount_rw_obj) },
    { MP_ROM_QSTR(MP_QSTR_empty), MP_ROM_PTR(&mtar_empty_obj) },
    { MP_ROM_QSTR(MP_QSTR_pack_simple), MP_ROM_PTR(&mtar_pack_simple_obj) },
};
static MP_DEFINE_CONST_DICT(mtar_globals, mtar_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_fs_mtar = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mtar_globals,
};

