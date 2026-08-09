/*
 * pymergetic.metal.fs.vfs — µPy face.
 */
#include "py/obj.h"
#include "py/runtime.h"
#include <pymergetic/metal/fs/vfs/__init__.h>
#include <string.h>
#include <pymergetic/metal/reg/seats.h>


static const uint8_t *path_z(mp_obj_t path_obj, uint8_t *buf, size_t cap)
{
    size_t n;
    const char *s = mp_obj_str_get_data(path_obj, &n);
    if (n + 1u > cap) {
        mp_raise_ValueError(MP_ERROR_TEXT("vfs path"));
    }
    memcpy(buf, s, n);
    buf[n] = 0;
    return buf;
}

static mp_obj_t vfs_mount(mp_obj_t target_obj, mp_obj_t ops_obj, mp_obj_t ctx_obj)
{
    uint8_t path[256];
    return mp_obj_new_int_from_uint(pm_metal_fs_vfs_mount(
        path_z(target_obj, path, sizeof path), (const void *)(uintptr_t)mp_obj_get_int(ops_obj),
        (void *)(uintptr_t)mp_obj_get_int(ctx_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_3(vfs_mount_obj, vfs_mount);

static mp_obj_t vfs_umount(mp_obj_t target_obj)
{
    uint8_t path[256];
    return mp_obj_new_int(pm_metal_fs_vfs_umount(path_z(target_obj, path, sizeof path)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(vfs_umount_obj, vfs_umount);

static mp_obj_t vfs_mount_count(void)
{
    return mp_obj_new_int_from_uint(pm_metal_fs_vfs_mount_count());
}
static MP_DEFINE_CONST_FUN_OBJ_0(vfs_mount_count_obj, vfs_mount_count);

static mp_obj_t vfs_mount_info(mp_obj_t index_obj)
{
    uint8_t target[128];
    uint8_t fstype[32];
    mp_obj_t tup[2];
    if (pm_metal_fs_vfs_mount_info((uint32_t)mp_obj_get_int(index_obj), target, sizeof target,
                                   fstype, sizeof fstype) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("vfs mount_info"));
    }
    tup[0] = mp_obj_new_str((const char *)target, strlen((const char *)target));
    tup[1] = mp_obj_new_str((const char *)fstype, strlen((const char *)fstype));
    return mp_obj_new_tuple(2, tup);
}
static MP_DEFINE_CONST_FUN_OBJ_1(vfs_mount_info_obj, vfs_mount_info);

static mp_obj_t vfs_mount_get(mp_obj_t index_obj)
{
    const void *ops = NULL;
    void *ctx = NULL;
    mp_obj_t tup[2];
    if (pm_metal_fs_vfs_mount_get((uint32_t)mp_obj_get_int(index_obj), &ops, &ctx) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("vfs mount_get"));
    }
    tup[0] = mp_obj_new_int((mp_int_t)(uintptr_t)ops);
    tup[1] = mp_obj_new_int((mp_int_t)(uintptr_t)ctx);
    return mp_obj_new_tuple(2, tup);
}
static MP_DEFINE_CONST_FUN_OBJ_1(vfs_mount_get_obj, vfs_mount_get);

static mp_obj_t vfs_resolve(mp_obj_t path_obj)
{
    uint8_t path[256];
    pm_metal_fs_vfs_resolve_t r;
    mp_obj_t tup[4];
    if (pm_metal_fs_vfs_resolve(path_z(path_obj, path, sizeof path), &r) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("vfs resolve"));
    }
    tup[0] = mp_obj_new_int((mp_int_t)(uintptr_t)r.ops);
    tup[1] = mp_obj_new_int((mp_int_t)(uintptr_t)r.ctx);
    tup[2] = r.rel ? mp_obj_new_str((const char *)r.rel, strlen((const char *)r.rel))
                   : mp_const_none;
    tup[3] = mp_obj_new_int_from_uint(r.mount);
    return mp_obj_new_tuple(4, tup);
}
static MP_DEFINE_CONST_FUN_OBJ_1(vfs_resolve_obj, vfs_resolve);

static const mp_rom_map_elem_t vfs_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_fs_dot_vfs) },
    { MP_ROM_QSTR(MP_QSTR_mount), MP_ROM_PTR(&vfs_mount_obj) },
    { MP_ROM_QSTR(MP_QSTR_umount), MP_ROM_PTR(&vfs_umount_obj) },
    { MP_ROM_QSTR(MP_QSTR_mount_count), MP_ROM_PTR(&vfs_mount_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_mount_info), MP_ROM_PTR(&vfs_mount_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_mount_get), MP_ROM_PTR(&vfs_mount_get_obj) },
    { MP_ROM_QSTR(MP_QSTR_resolve), MP_ROM_PTR(&vfs_resolve_obj) },
};
static MP_DEFINE_CONST_DICT(vfs_globals, vfs_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_fs_vfs = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&vfs_globals,
};


PM_METAL_REG_SEAT(g_pm_seat_fs_vfs, "pymergetic.metal.fs.vfs", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
