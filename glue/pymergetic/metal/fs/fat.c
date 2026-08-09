/*
 * pymergetic.metal.fs.fat — µPy face.
 */
#include "py/obj.h"
#include "py/runtime.h"
#include <pymergetic/metal/fs/fat/__init__.h>
#include <string.h>

#if !defined(PM_METAL_CFG_FW_BROWSER) || !PM_METAL_CFG_FW_BROWSER

static const uint8_t *path_z(mp_obj_t path_obj, uint8_t *buf, size_t cap)
{
    size_t n;
    const char *s = mp_obj_str_get_data(path_obj, &n);
    if (n + 1u > cap) {
        mp_raise_ValueError(MP_ERROR_TEXT("fat path"));
    }
    memcpy(buf, s, n);
    buf[n] = 0;
    return buf;
}

static mp_obj_t fat_format_buf(mp_obj_t buf_obj)
{
    mp_buffer_info_t buf;
    mp_get_buffer_raise(buf_obj, &buf, MP_BUFFER_WRITE);
    return mp_obj_new_int(pm_metal_fs_fat_format_buf((uint8_t *)buf.buf, buf.len));
}
static MP_DEFINE_CONST_FUN_OBJ_1(fat_format_buf_obj, fat_format_buf);

static mp_obj_t fat_open_buf(mp_obj_t buf_obj)
{
    mp_buffer_info_t buf;
    mp_get_buffer_raise(buf_obj, &buf, MP_BUFFER_WRITE);
    return mp_obj_new_int_from_uint(pm_metal_fs_fat_open_buf((uint8_t *)buf.buf, buf.len));
}
static MP_DEFINE_CONST_FUN_OBJ_1(fat_open_buf_obj, fat_open_buf);

static mp_obj_t fat_close(mp_obj_t vol_obj)
{
    return mp_obj_new_int(pm_metal_fs_fat_close((uint32_t)mp_obj_get_int(vol_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(fat_close_obj, fat_close);

static mp_obj_t fat_mount(mp_obj_t target_obj, mp_obj_t buf_obj)
{
    uint8_t path[256];
    mp_buffer_info_t buf;
    mp_get_buffer_raise(buf_obj, &buf, MP_BUFFER_WRITE);
    return mp_obj_new_int(pm_metal_fs_fat_mount(path_z(target_obj, path, sizeof path),
                                                 (uint8_t *)buf.buf, buf.len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(fat_mount_obj, fat_mount);

static mp_obj_t fat_seed_simple(mp_obj_t buf_obj, mp_obj_t files_obj)
{
    mp_buffer_info_t buf;
    size_t n, i;
    mp_obj_t *items;
    const uint8_t *names[32];
    const uint8_t *datas[32];
    uint32_t lens[32];
    uint8_t name_storage[32][128];
    mp_buffer_info_t data_bi[32];

    mp_get_buffer_raise(buf_obj, &buf, MP_BUFFER_WRITE);
    mp_obj_get_array(files_obj, &n, &items);
    if (n > 32u) {
        mp_raise_ValueError(MP_ERROR_TEXT("fat seed"));
    }
    for (i = 0; i < n; i++) {
        size_t nlen;
        const char *nm;
        mp_obj_t *pair;
        size_t pn;
        mp_obj_get_array(items[i], &pn, &pair);
        if (pn != 2u) {
            mp_raise_ValueError(MP_ERROR_TEXT("fat seed pair"));
        }
        nm = mp_obj_str_get_data(pair[0], &nlen);
        if (nlen + 1u > sizeof name_storage[i]) {
            mp_raise_ValueError(MP_ERROR_TEXT("fat seed name"));
        }
        memcpy(name_storage[i], nm, nlen);
        name_storage[i][nlen] = 0;
        names[i] = name_storage[i];
        mp_get_buffer_raise(pair[1], &data_bi[i], MP_BUFFER_READ);
        datas[i] = (const uint8_t *)data_bi[i].buf;
        lens[i] = (uint32_t)data_bi[i].len;
    }
    return mp_obj_new_int(pm_metal_fs_fat_seed_simple((uint8_t *)buf.buf, buf.len, names, datas, lens,
                                                      (uint32_t)n));
}
static MP_DEFINE_CONST_FUN_OBJ_2(fat_seed_simple_obj, fat_seed_simple);

static mp_obj_t fat_mount_ram(mp_obj_t target_obj, mp_obj_t ram_h_obj)
{
    uint8_t path[256];
    return mp_obj_new_int(pm_metal_fs_fat_mount_ram(path_z(target_obj, path, sizeof path),
                                                    (uint32_t)mp_obj_get_int(ram_h_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(fat_mount_ram_obj, fat_mount_ram);

static const mp_rom_map_elem_t fat_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_fs_dot_fat) },
    { MP_ROM_QSTR(MP_QSTR_format_buf), MP_ROM_PTR(&fat_format_buf_obj) },
    { MP_ROM_QSTR(MP_QSTR_open_buf), MP_ROM_PTR(&fat_open_buf_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&fat_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_mount), MP_ROM_PTR(&fat_mount_obj) },
    { MP_ROM_QSTR(MP_QSTR_seed_simple), MP_ROM_PTR(&fat_seed_simple_obj) },
    { MP_ROM_QSTR(MP_QSTR_mount_ram), MP_ROM_PTR(&fat_mount_ram_obj) },
};
static MP_DEFINE_CONST_DICT(fat_globals, fat_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_fs_fat = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&fat_globals,
};

#endif
