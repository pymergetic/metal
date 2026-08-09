/*
 * pymergetic.metal.auth — µPy face (callee: src/.../auth).
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/auth/__init__.h>

static mp_obj_t auth_user_check(mp_obj_t user_obj, mp_obj_t pass_obj)
{
    const char *user = mp_obj_str_get_str(user_obj);
    const char *pass = mp_obj_str_get_str(pass_obj);
    return pm_metal_auth_user_check(user, pass) ? mp_const_true : mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_2(auth_user_check_obj, auth_user_check);

static mp_obj_t auth_hash_verify(mp_obj_t enc_obj, mp_obj_t pass_obj)
{
    const char *enc = mp_obj_str_get_str(enc_obj);
    const char *pass = mp_obj_str_get_str(pass_obj);
    return pm_metal_auth_hash_verify(enc, pass) ? mp_const_true : mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_2(auth_hash_verify_obj, auth_hash_verify);

static const mp_rom_map_elem_t auth_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_auth) },
    { MP_ROM_QSTR(MP_QSTR_user_check), MP_ROM_PTR(&auth_user_check_obj) },
    { MP_ROM_QSTR(MP_QSTR_hash_verify), MP_ROM_PTR(&auth_hash_verify_obj) },
};
static MP_DEFINE_CONST_DICT(auth_globals, auth_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_auth = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&auth_globals,
};
