/*
 * pymergetic.metal.fs.tmpfs — µPy face.
 */
#include "py/obj.h"
#include "py/runtime.h"
#include <pymergetic/metal/fs/tmpfs/__init__.h>
#include <string.h>
#include <pymergetic/metal/reg/seats.h>


static mp_obj_t tmpfs_mount(mp_obj_t target_obj)
{
    size_t n;
    const char *s = mp_obj_str_get_data(target_obj, &n);
    uint8_t path[256];
    if (n + 1u > sizeof path) {
        mp_raise_ValueError(MP_ERROR_TEXT("tmpfs target"));
    }
    memcpy(path, s, n);
    path[n] = 0;
    return mp_obj_new_int(pm_metal_fs_tmpfs_mount(path));
}
static MP_DEFINE_CONST_FUN_OBJ_1(tmpfs_mount_obj, tmpfs_mount);

static const mp_rom_map_elem_t tmpfs_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_fs_dot_tmpfs) },
    { MP_ROM_QSTR(MP_QSTR_mount), MP_ROM_PTR(&tmpfs_mount_obj) },
};
static MP_DEFINE_CONST_DICT(tmpfs_globals, tmpfs_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_fs_tmpfs = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&tmpfs_globals,
};


PM_METAL_REG_SEAT(g_pm_seat_fs_tmpfs, "pymergetic.metal.fs.tmpfs", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
