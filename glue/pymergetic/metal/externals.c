/*
 * pymergetic.metal.externals — µPy face (callee: src/.../boot/externals).
 */
#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"

#include <pymergetic/metal/boot/externals.h>
#include <string.h>

static mp_obj_t ExtToDict(const pm_metal_external_t *e)
{
    mp_obj_t d = mp_obj_new_dict(4);
    const char *id = (e->id != NULL) ? e->id : "";
    const char *version = (e->version != NULL) ? e->version : "";
    const char *url = (e->url != NULL) ? e->url : "";
    const char *note = (e->note != NULL) ? e->note : "";

    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_id), mp_obj_new_str(id, strlen(id)));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_version),
                      mp_obj_new_str(version, strlen(version)));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_url), mp_obj_new_str(url, strlen(url)));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_note), mp_obj_new_str(note, strlen(note)));
    return d;
}

static mp_obj_t externals_init(void)
{
    pm_metal_externals_init();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(externals_init_obj, externals_init);

static mp_obj_t externals_seed_fallback(void)
{
    pm_metal_externals_seed_fallback();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(externals_seed_fallback_obj, externals_seed_fallback);

static mp_obj_t externals_count(void)
{
    return mp_obj_new_int_from_uint((mp_uint_t)pm_metal_external_count());
}
static MP_DEFINE_CONST_FUN_OBJ_0(externals_count_obj, externals_count);

static mp_obj_t externals_get(mp_obj_t idx_obj)
{
    pm_metal_external_t e;
    uint32_t idx = (uint32_t)mp_obj_get_int(idx_obj);

    if (pm_metal_external_get(idx, &e) != 0) {
        return mp_const_none;
    }
    return ExtToDict(&e);
}
static MP_DEFINE_CONST_FUN_OBJ_1(externals_get_obj, externals_get);

static mp_obj_t externals_find(mp_obj_t id_obj)
{
    const char *id = mp_obj_str_get_str(id_obj);
    pm_metal_external_t e;

    if (pm_metal_external_find(id, &e) != 0) {
        return mp_const_none;
    }
    return ExtToDict(&e);
}
static MP_DEFINE_CONST_FUN_OBJ_1(externals_find_obj, externals_find);

static mp_obj_t externals_register(size_t n_args, const mp_obj_t *args)
{
    const char *id = mp_obj_str_get_str(args[0]);
    const char *version = mp_obj_str_get_str(args[1]);
    const char *url = "";
    const char *note = "";

    if (n_args >= 3 && args[2] != mp_const_none) {
        url = mp_obj_str_get_str(args[2]);
    }
    if (n_args >= 4 && args[3] != mp_const_none) {
        note = mp_obj_str_get_str(args[3]);
    }
    if (pm_metal_external_register(id, version, url, note) != 0) {
        return mp_const_false;
    }
    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(externals_register_obj, 2, 4, externals_register);

static const mp_rom_map_elem_t externals_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_externals) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&externals_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_seed_fallback), MP_ROM_PTR(&externals_seed_fallback_obj) },
    { MP_ROM_QSTR(MP_QSTR_count), MP_ROM_PTR(&externals_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_get), MP_ROM_PTR(&externals_get_obj) },
    { MP_ROM_QSTR(MP_QSTR_find), MP_ROM_PTR(&externals_find_obj) },
    { MP_ROM_QSTR(MP_QSTR_register), MP_ROM_PTR(&externals_register_obj) },
};
static MP_DEFINE_CONST_DICT(externals_module_globals, externals_module_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_externals = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&externals_module_globals,
};
