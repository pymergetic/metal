/*
 * pymergetic.metal.fs — package + async fd µPy face (RS callee).
 * Firmware seats only. Globals mutable for nested builtins.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include "../modules.h"

#include <pymergetic/metal/fs/__init__.h>
#include <string.h>
static const uint8_t *fs_path_z(mp_obj_t path_obj, uint8_t *buf, size_t cap)
{
    size_t n;
    const char *s = mp_obj_str_get_data(path_obj, &n);
    if (n + 1u > cap) {
        mp_raise_ValueError(MP_ERROR_TEXT("fs path"));
    }
    memcpy(buf, s, n);
    buf[n] = 0;
    return buf;
}

static mp_obj_t fs_open_async(mp_obj_t path_obj, mp_obj_t flags_obj)
{
    uint8_t path[256];
    return mp_obj_new_int_from_uint(pm_metal_fs_open_async(fs_path_z(path_obj, path, sizeof path),
                                                           (uint32_t)mp_obj_get_int(flags_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(fs_open_async_obj, fs_open_async);

static mp_obj_t fs_close_async(mp_obj_t h_obj)
{
    return mp_obj_new_int_from_uint(pm_metal_fs_close_async((pm_metal_fs_h)mp_obj_get_int(h_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(fs_close_async_obj, fs_close_async);

static mp_obj_t fs_fread_async(mp_obj_t h_obj, mp_obj_t dest_obj)
{
    mp_buffer_info_t dest;
    mp_get_buffer_raise(dest_obj, &dest, MP_BUFFER_WRITE);
    return mp_obj_new_int_from_uint(pm_metal_fs_fread_async((pm_metal_fs_h)mp_obj_get_int(h_obj),
                                                            (uint8_t *)dest.buf, (uint32_t)dest.len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(fs_fread_async_obj, fs_fread_async);

static mp_obj_t fs_fwrite_async(mp_obj_t h_obj, mp_obj_t src_obj)
{
    mp_buffer_info_t src;
    mp_get_buffer_raise(src_obj, &src, MP_BUFFER_READ);
    return mp_obj_new_int_from_uint(pm_metal_fs_fwrite_async((pm_metal_fs_h)mp_obj_get_int(h_obj),
                                                             (const uint8_t *)src.buf,
                                                             (uint32_t)src.len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(fs_fwrite_async_obj, fs_fwrite_async);

static mp_obj_t fs_fpread_async(size_t n_args, const mp_obj_t *args)
{
    mp_buffer_info_t dest;
    (void)n_args;
    mp_get_buffer_raise(args[2], &dest, MP_BUFFER_WRITE);
    return mp_obj_new_int_from_uint(pm_metal_fs_fpread_async((pm_metal_fs_h)mp_obj_get_int(args[0]),
                                                             (uint32_t)mp_obj_get_int(args[1]),
                                                             (uint8_t *)dest.buf, (uint32_t)dest.len));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(fs_fpread_async_obj, 3, 3, fs_fpread_async);

static mp_obj_t fs_fpwrite_async(size_t n_args, const mp_obj_t *args)
{
    mp_buffer_info_t src;
    (void)n_args;
    mp_get_buffer_raise(args[2], &src, MP_BUFFER_READ);
    return mp_obj_new_int_from_uint(pm_metal_fs_fpwrite_async((pm_metal_fs_h)mp_obj_get_int(args[0]),
                                                              (uint32_t)mp_obj_get_int(args[1]),
                                                              (const uint8_t *)src.buf,
                                                              (uint32_t)src.len));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(fs_fpwrite_async_obj, 3, 3, fs_fpwrite_async);

static mp_obj_t fs_lseek(mp_obj_t h_obj, mp_obj_t off_obj, mp_obj_t whence_obj)
{
    return mp_obj_new_int(pm_metal_fs_lseek((pm_metal_fs_h)mp_obj_get_int(h_obj),
                                            (int32_t)mp_obj_get_int(off_obj),
                                            (uint32_t)mp_obj_get_int(whence_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_3(fs_lseek_obj, fs_lseek);

static mp_obj_t fs_stat_async(mp_obj_t path_obj, mp_obj_t dest_obj)
{
    uint8_t path[256];
    mp_buffer_info_t dest;
    mp_get_buffer_raise(dest_obj, &dest, MP_BUFFER_WRITE);
    return mp_obj_new_int_from_uint(pm_metal_fs_stat_async(fs_path_z(path_obj, path, sizeof path),
                                                           (uint8_t *)dest.buf));
}
static MP_DEFINE_CONST_FUN_OBJ_2(fs_stat_async_obj, fs_stat_async);

static mp_obj_t fs_readdir_async(mp_obj_t h_obj, mp_obj_t name_obj)
{
    mp_buffer_info_t name;
    mp_get_buffer_raise(name_obj, &name, MP_BUFFER_WRITE);
    return mp_obj_new_int_from_uint(pm_metal_fs_readdir_async((pm_metal_fs_h)mp_obj_get_int(h_obj),
                                                              (uint8_t *)name.buf,
                                                              (uint32_t)name.len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(fs_readdir_async_obj, fs_readdir_async);

static mp_obj_t fs_mkdir_async(mp_obj_t path_obj)
{
    uint8_t path[256];
    return mp_obj_new_int_from_uint(pm_metal_fs_mkdir_async(fs_path_z(path_obj, path, sizeof path)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(fs_mkdir_async_obj, fs_mkdir_async);

static mp_obj_t fs_unlink_async(mp_obj_t path_obj)
{
    uint8_t path[256];
    return mp_obj_new_int_from_uint(pm_metal_fs_unlink_async(fs_path_z(path_obj, path, sizeof path)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(fs_unlink_async_obj, fs_unlink_async);

static mp_obj_t fs_rename_async(mp_obj_t old_obj, mp_obj_t new_obj)
{
    uint8_t oldp[256], newp[256];
    return mp_obj_new_int_from_uint(pm_metal_fs_rename_async(fs_path_z(old_obj, oldp, sizeof oldp),
                                                             fs_path_z(new_obj, newp, sizeof newp)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(fs_rename_async_obj, fs_rename_async);

static mp_obj_t fs_fsync_async(mp_obj_t h_obj)
{
    return mp_obj_new_int_from_uint(pm_metal_fs_fsync_async((pm_metal_fs_h)mp_obj_get_int(h_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(fs_fsync_async_obj, fs_fsync_async);

static mp_obj_t fs_fstat_async(mp_obj_t h_obj, mp_obj_t dest_obj)
{
    mp_buffer_info_t dest;
    mp_get_buffer_raise(dest_obj, &dest, MP_BUFFER_WRITE);
    return mp_obj_new_int_from_uint(pm_metal_fs_fstat_async((pm_metal_fs_h)mp_obj_get_int(h_obj),
                                                            (uint8_t *)dest.buf));
}
static MP_DEFINE_CONST_FUN_OBJ_2(fs_fstat_async_obj, fs_fstat_async);

static mp_obj_t fs_size_async(mp_obj_t path_obj)
{
    uint8_t path[256];
    return mp_obj_new_int_from_uint(pm_metal_fs_size_async(fs_path_z(path_obj, path, sizeof path)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(fs_size_async_obj, fs_size_async);

static mp_obj_t fs_read_async(mp_obj_t path_obj, mp_obj_t dest_obj)
{
    uint8_t path[256];
    mp_buffer_info_t dest;
    mp_get_buffer_raise(dest_obj, &dest, MP_BUFFER_WRITE);
    return mp_obj_new_int_from_uint(pm_metal_fs_read_async(fs_path_z(path_obj, path, sizeof path),
                                                           (uint8_t *)dest.buf, (uint32_t)dest.len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(fs_read_async_obj, fs_read_async);

static mp_obj_t fs_write_async(mp_obj_t path_obj, mp_obj_t src_obj)
{
    uint8_t path[256];
    mp_buffer_info_t src;
    mp_get_buffer_raise(src_obj, &src, MP_BUFFER_READ);
    return mp_obj_new_int_from_uint(pm_metal_fs_write_async(fs_path_z(path_obj, path, sizeof path),
                                                            (const uint8_t *)src.buf,
                                                            (uint32_t)src.len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(fs_write_async_obj, fs_write_async);

static mp_obj_t fs_read_mem_async(mp_obj_t path_obj, mp_obj_t cookie_obj, mp_obj_t len_obj)
{
    uint8_t path[256];
    return mp_obj_new_int_from_uint(pm_metal_fs_read_mem_async(fs_path_z(path_obj, path, sizeof path),
                                                               (uint32_t)mp_obj_get_int(cookie_obj),
                                                               (uint32_t)mp_obj_get_int(len_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_3(fs_read_mem_async_obj, fs_read_mem_async);

static mp_obj_t fs_write_mem_async(mp_obj_t path_obj, mp_obj_t cookie_obj, mp_obj_t len_obj)
{
    uint8_t path[256];
    return mp_obj_new_int_from_uint(pm_metal_fs_write_mem_async(fs_path_z(path_obj, path, sizeof path),
                                                                (uint32_t)mp_obj_get_int(cookie_obj),
                                                                (uint32_t)mp_obj_get_int(len_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_3(fs_write_mem_async_obj, fs_write_mem_async);

static mp_obj_t fs_result(mp_obj_t h_obj)
{
    return mp_obj_new_int_from_uint(pm_metal_fs_result((uint32_t)mp_obj_get_int(h_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(fs_result_obj, fs_result);

static mp_obj_t fs_mount_statfs(mp_obj_t index_obj)
{
    pm_metal_fs_statfs_t st;
    mp_obj_t tup[3];
    if (pm_metal_fs_mount_statfs((uint32_t)mp_obj_get_int(index_obj), &st) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("fs mount_statfs"));
    }
    tup[0] = mp_obj_new_int_from_ull(st.total);
    tup[1] = mp_obj_new_int_from_ull(st.used);
    tup[2] = mp_obj_new_int_from_uint(st.flags);
    return mp_obj_new_tuple(3, tup);
}
static MP_DEFINE_CONST_FUN_OBJ_1(fs_mount_statfs_obj, fs_mount_statfs);

static mp_obj_t fs_set_active_ops(mp_obj_t ops_obj, mp_obj_t ctx_obj)
{
    const pm_metal_fs_ops_t *ops = (const pm_metal_fs_ops_t *)(uintptr_t)mp_obj_get_int(ops_obj);
    void *ctx = (void *)(uintptr_t)mp_obj_get_int(ctx_obj);
    pm_metal_fs_set_active_ops(ops, ctx);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(fs_set_active_ops_obj, fs_set_active_ops);

static mp_obj_t fs_ops_register(mp_obj_t ops_obj)
{
    const pm_metal_fs_ops_t *ops = (const pm_metal_fs_ops_t *)(uintptr_t)mp_obj_get_int(ops_obj);
    return mp_obj_new_int(pm_metal_fs_ops_register(ops));
}
static MP_DEFINE_CONST_FUN_OBJ_1(fs_ops_register_obj, fs_ops_register);

static mp_obj_t fs_ops_lookup(mp_obj_t name_obj)
{
    uint8_t name[64];
    const pm_metal_fs_ops_t *ops = pm_metal_fs_ops_lookup(fs_path_z(name_obj, name, sizeof name));
    return mp_obj_new_int((mp_int_t)(uintptr_t)ops);
}
static MP_DEFINE_CONST_FUN_OBJ_1(fs_ops_lookup_obj, fs_ops_lookup);

static mp_obj_dict_t fs_globals;
static int fs_globals_ready;

void pm_metal_fs_globals_init(void)
{
    if (fs_globals_ready) {
        return;
    }
    mp_obj_dict_init(&fs_globals, 40);
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR___name__),
                      MP_OBJ_NEW_QSTR(MP_QSTR_pymergetic_dot_metal_dot_fs));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_open_async),
                      MP_OBJ_FROM_PTR(&fs_open_async_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_close_async),
                      MP_OBJ_FROM_PTR(&fs_close_async_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_fread_async),
                      MP_OBJ_FROM_PTR(&fs_fread_async_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_fwrite_async),
                      MP_OBJ_FROM_PTR(&fs_fwrite_async_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_fpread_async),
                      MP_OBJ_FROM_PTR(&fs_fpread_async_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_fpwrite_async),
                      MP_OBJ_FROM_PTR(&fs_fpwrite_async_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_lseek),
                      MP_OBJ_FROM_PTR(&fs_lseek_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_stat_async),
                      MP_OBJ_FROM_PTR(&fs_stat_async_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_readdir_async),
                      MP_OBJ_FROM_PTR(&fs_readdir_async_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_mkdir_async),
                      MP_OBJ_FROM_PTR(&fs_mkdir_async_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_unlink_async),
                      MP_OBJ_FROM_PTR(&fs_unlink_async_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_rename_async),
                      MP_OBJ_FROM_PTR(&fs_rename_async_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_fsync_async),
                      MP_OBJ_FROM_PTR(&fs_fsync_async_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_fstat_async),
                      MP_OBJ_FROM_PTR(&fs_fstat_async_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_size_async),
                      MP_OBJ_FROM_PTR(&fs_size_async_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_read_async),
                      MP_OBJ_FROM_PTR(&fs_read_async_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_write_async),
                      MP_OBJ_FROM_PTR(&fs_write_async_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_read_mem_async),
                      MP_OBJ_FROM_PTR(&fs_read_mem_async_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_write_mem_async),
                      MP_OBJ_FROM_PTR(&fs_write_mem_async_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_result),
                      MP_OBJ_FROM_PTR(&fs_result_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_mount_statfs),
                      MP_OBJ_FROM_PTR(&fs_mount_statfs_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_set_active_ops),
                      MP_OBJ_FROM_PTR(&fs_set_active_ops_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_ops_register),
                      MP_OBJ_FROM_PTR(&fs_ops_register_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_ops_lookup),
                      MP_OBJ_FROM_PTR(&fs_ops_lookup_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_embed),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_fs_embed));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_fat),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_fs_fat));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_littlefs),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_fs_littlefs));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_mtar),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_fs_mtar));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_overlay),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_fs_overlay));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_tmpfs),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_fs_tmpfs));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_vfs),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_fs_vfs));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_wasmmod),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_fs_wasmmod));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&fs_globals), MP_OBJ_NEW_QSTR(MP_QSTR_zip),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_fs_zip));
    fs_globals_ready = 1;
}

const mp_obj_module_t mp_module_pymergetic_metal_fs = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&fs_globals,
};
