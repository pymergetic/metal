/*
 * pymergetic.metal.pack — µPy face (callee: src/.../pack).
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/pack/mod_packs.h>

static mp_obj_t pack_mount_all(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_mod_packs_mount_all());
}
static MP_DEFINE_CONST_FUN_OBJ_0(pack_mount_all_obj, pack_mount_all);

static mp_obj_t pack_inspect(void)
{
    return mp_obj_new_bytes(pm_metal_pack_inspect, pm_metal_pack_inspect_len);
}
static MP_DEFINE_CONST_FUN_OBJ_0(pack_inspect_obj, pack_inspect);

static mp_obj_t pack_inspect_len(void)
{
    return mp_obj_new_int_from_uint(pm_metal_pack_inspect_len);
}
static MP_DEFINE_CONST_FUN_OBJ_0(pack_inspect_len_obj, pack_inspect_len);

static mp_obj_t pack_metal(void)
{
    return mp_obj_new_bytes(pm_metal_pack_metal, pm_metal_pack_metal_len);
}
static MP_DEFINE_CONST_FUN_OBJ_0(pack_metal_obj, pack_metal);

static mp_obj_t pack_metal_len(void)
{
    return mp_obj_new_int_from_uint(pm_metal_pack_metal_len);
}
static MP_DEFINE_CONST_FUN_OBJ_0(pack_metal_len_obj, pack_metal_len);

static mp_obj_t pack_names(void)
{
    mp_obj_t items[2];
    items[0] = MP_OBJ_NEW_QSTR(MP_QSTR_inspect);
    items[1] = MP_OBJ_NEW_QSTR(MP_QSTR_metal);
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(pack_names_obj, pack_names);

static const mp_rom_map_elem_t pack_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_pack) },
    { MP_ROM_QSTR(MP_QSTR_mount_all), MP_ROM_PTR(&pack_mount_all_obj) },
    { MP_ROM_QSTR(MP_QSTR_names), MP_ROM_PTR(&pack_names_obj) },
    { MP_ROM_QSTR(MP_QSTR_inspect), MP_ROM_PTR(&pack_inspect_obj) },
    { MP_ROM_QSTR(MP_QSTR_inspect_len), MP_ROM_PTR(&pack_inspect_len_obj) },
    { MP_ROM_QSTR(MP_QSTR_metal), MP_ROM_PTR(&pack_metal_obj) },
    { MP_ROM_QSTR(MP_QSTR_metal_len), MP_ROM_PTR(&pack_metal_len_obj) },
};
static MP_DEFINE_CONST_DICT(pack_globals, pack_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_pack = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&pack_globals,
};
