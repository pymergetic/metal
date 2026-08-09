/*
 * pymergetic.metal.fs.littlefs — µPy face.
 */
#include "py/obj.h"
#include "py/runtime.h"
#include <pymergetic/metal/fs/littlefs/__init__.h>
#include <string.h>


static mp_obj_t littlefs_mount(mp_obj_t target_obj, mp_obj_t buf_obj)
{
    size_t n;
    const char *s = mp_obj_str_get_data(target_obj, &n);
    uint8_t path[256];
    mp_buffer_info_t buf;
    if (n + 1u > sizeof path) {
        mp_raise_ValueError(MP_ERROR_TEXT("littlefs target"));
    }
    memcpy(path, s, n);
    path[n] = 0;
    mp_get_buffer_raise(buf_obj, &buf, MP_BUFFER_WRITE);
    return mp_obj_new_int(pm_metal_fs_littlefs_mount(path, (uint8_t *)buf.buf, buf.len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(littlefs_mount_obj, littlefs_mount);

static const mp_rom_map_elem_t littlefs_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_fs_dot_littlefs) },
    { MP_ROM_QSTR(MP_QSTR_mount), MP_ROM_PTR(&littlefs_mount_obj) },
};
static MP_DEFINE_CONST_DICT(littlefs_globals, littlefs_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_fs_littlefs = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&littlefs_globals,
};

