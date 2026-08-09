/*
 * pymergetic.metal.fs.overlay — µPy face.
 */
#include "py/obj.h"
#include "py/runtime.h"
#include <pymergetic/metal/fs/overlay/__init__.h>
#include <string.h>


static mp_obj_t overlay_mount(size_t n_args, const mp_obj_t *args)
{
    size_t n;
    const char *s;
    uint8_t path[256];
    (void)n_args;
    s = mp_obj_str_get_data(args[0], &n);
    if (n + 1u > sizeof path) {
        mp_raise_ValueError(MP_ERROR_TEXT("overlay target"));
    }
    memcpy(path, s, n);
    path[n] = 0;
    return mp_obj_new_int(pm_metal_fs_overlay_mount(
        path, (const pm_metal_fs_ops_t *)(uintptr_t)mp_obj_get_int(args[1]),
        (void *)(uintptr_t)mp_obj_get_int(args[2]),
        (const pm_metal_fs_ops_t *)(uintptr_t)mp_obj_get_int(args[3]),
        (void *)(uintptr_t)mp_obj_get_int(args[4])));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(overlay_mount_obj, 5, 5, overlay_mount);

static const mp_rom_map_elem_t overlay_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_fs_dot_overlay) },
    { MP_ROM_QSTR(MP_QSTR_mount), MP_ROM_PTR(&overlay_mount_obj) },
};
static MP_DEFINE_CONST_DICT(overlay_globals, overlay_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_fs_overlay = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&overlay_globals,
};

