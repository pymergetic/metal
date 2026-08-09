/*
 * pymergetic.metal.fs.wasmmod — µPy face.
 */
#include "py/obj.h"
#include "py/runtime.h"
#include <pymergetic/metal/fs/wasmmod/__init__.h>
#include <string.h>


static mp_obj_t wasmmod_mount_mpwp(mp_obj_t target_obj, mp_obj_t mpwp_obj)
{
    mp_buffer_info_t mpwp;
    const uint8_t *target = NULL;
    uint8_t path[256];
    mp_get_buffer_raise(mpwp_obj, &mpwp, MP_BUFFER_READ);
    if (target_obj != mp_const_none) {
        size_t n;
        const char *s = mp_obj_str_get_data(target_obj, &n);
        if (n + 1u > sizeof path) {
            mp_raise_ValueError(MP_ERROR_TEXT("wasmmod target"));
        }
        memcpy(path, s, n);
        path[n] = 0;
        target = path;
    }
    return mp_obj_new_int(pm_metal_fs_wasmmod_mount_mpwp(target, (const uint8_t *)mpwp.buf, mpwp.len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(wasmmod_mount_mpwp_obj, wasmmod_mount_mpwp);

static const mp_rom_map_elem_t wasmmod_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_fs_dot_wasmmod) },
    { MP_ROM_QSTR(MP_QSTR_mount_mpwp), MP_ROM_PTR(&wasmmod_mount_mpwp_obj) },
};
static MP_DEFINE_CONST_DICT(wasmmod_globals, wasmmod_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_fs_wasmmod = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&wasmmod_globals,
};

