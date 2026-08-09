/*
 * pymergetic.metal.auth — µPy face (callee: src/.../auth).
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/auth/__init__.h>

#include <string.h>

static mp_obj_t auth_users_set(mp_obj_t list_obj)
{
    size_t n;
    size_t i;
    mp_obj_t *items;
    pm_metal_auth_user_t users[PM_METAL_AUTH_USERS_MAX];

    mp_obj_get_array(list_obj, &n, &items);
    if (n > PM_METAL_AUTH_USERS_MAX) {
        mp_raise_ValueError(MP_ERROR_TEXT("auth users"));
    }
    memset(users, 0, sizeof(users));
    for (i = 0; i < n; i++) {
        mp_obj_t name_obj = mp_obj_dict_get(items[i], MP_OBJ_NEW_QSTR(MP_QSTR_name));
        mp_obj_t hash_obj = mp_obj_dict_get(items[i], MP_OBJ_NEW_QSTR(MP_QSTR_hash));
        const char *name = mp_obj_str_get_str(name_obj);
        const char *hash = mp_obj_str_get_str(hash_obj);

        users[i].used = 1;
        strncpy(users[i].name, name, PM_METAL_AUTH_USER_MAX - 1u);
        strncpy(users[i].hash, hash, PM_METAL_AUTH_HASH_MAX - 1u);
    }
    pm_metal_auth_users_set(users, (uint32_t)n);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(auth_users_set_obj, auth_users_set);

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

static mp_obj_t auth_basic_decode(mp_obj_t b64_obj)
{
    char user[PM_METAL_AUTH_USER_MAX];
    char pass[PM_METAL_AUTH_HASH_MAX];
    mp_obj_t items[2];
    const char *b64 = mp_obj_str_get_str(b64_obj);

    if (pm_metal_auth_basic_decode(b64, user, (uint32_t)sizeof(user), pass,
                                   (uint32_t)sizeof(pass)) != 0) {
        return mp_const_none;
    }
    items[0] = mp_obj_new_str(user, strlen(user));
    items[1] = mp_obj_new_str(pass, strlen(pass));
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(auth_basic_decode_obj, auth_basic_decode);

static mp_obj_t auth_pubkeys_clear(void)
{
    pm_metal_auth_pubkeys_clear();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(auth_pubkeys_clear_obj, auth_pubkeys_clear);

static mp_obj_t auth_pubkey_add(mp_obj_t user_obj, mp_obj_t algo_obj, mp_obj_t blob_obj)
{
    mp_buffer_info_t blob;
    const char *user = mp_obj_str_get_str(user_obj);
    const char *algo = mp_obj_str_get_str(algo_obj);

    mp_get_buffer_raise(blob_obj, &blob, MP_BUFFER_READ);
    return pm_metal_auth_pubkey_add(user, algo, (const uint8_t *)blob.buf, (uint32_t)blob.len) == 0
               ? mp_const_true
               : mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_3(auth_pubkey_add_obj, auth_pubkey_add);

static mp_obj_t auth_pubkey_add_line(mp_obj_t user_obj, mp_obj_t line_obj)
{
    const char *user = mp_obj_str_get_str(user_obj);
    const char *line = mp_obj_str_get_str(line_obj);
    return pm_metal_auth_pubkey_add_line(user, line) == 0 ? mp_const_true : mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_2(auth_pubkey_add_line_obj, auth_pubkey_add_line);

static mp_obj_t auth_pubkey_load_text(mp_obj_t user_obj, mp_obj_t text_obj)
{
    const char *user = mp_obj_str_get_str(user_obj);
    mp_buffer_info_t text;

    if (mp_obj_is_str(text_obj)) {
        const char *s = mp_obj_str_get_str(text_obj);
        return mp_obj_new_int(pm_metal_auth_pubkey_load_text(user, s, (uint32_t)strlen(s)));
    }
    mp_get_buffer_raise(text_obj, &text, MP_BUFFER_READ);
    return mp_obj_new_int(
        pm_metal_auth_pubkey_load_text(user, (const char *)text.buf, (uint32_t)text.len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(auth_pubkey_load_text_obj, auth_pubkey_load_text);

static mp_obj_t auth_pubkey_check(mp_obj_t user_obj, mp_obj_t algo_obj, mp_obj_t blob_obj)
{
    mp_buffer_info_t blob;
    const char *user = mp_obj_str_get_str(user_obj);
    const char *algo = NULL;

    if (algo_obj != mp_const_none) {
        algo = mp_obj_str_get_str(algo_obj);
    }
    mp_get_buffer_raise(blob_obj, &blob, MP_BUFFER_READ);
    return pm_metal_auth_pubkey_check(user, algo, (const uint8_t *)blob.buf, (uint32_t)blob.len)
               ? mp_const_true
               : mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_3(auth_pubkey_check_obj, auth_pubkey_check);

static const mp_rom_map_elem_t auth_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_auth) },
    { MP_ROM_QSTR(MP_QSTR_users_set), MP_ROM_PTR(&auth_users_set_obj) },
    { MP_ROM_QSTR(MP_QSTR_user_check), MP_ROM_PTR(&auth_user_check_obj) },
    { MP_ROM_QSTR(MP_QSTR_hash_verify), MP_ROM_PTR(&auth_hash_verify_obj) },
    { MP_ROM_QSTR(MP_QSTR_basic_decode), MP_ROM_PTR(&auth_basic_decode_obj) },
    { MP_ROM_QSTR(MP_QSTR_pubkeys_clear), MP_ROM_PTR(&auth_pubkeys_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_pubkey_add), MP_ROM_PTR(&auth_pubkey_add_obj) },
    { MP_ROM_QSTR(MP_QSTR_pubkey_add_line), MP_ROM_PTR(&auth_pubkey_add_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_pubkey_load_text), MP_ROM_PTR(&auth_pubkey_load_text_obj) },
    { MP_ROM_QSTR(MP_QSTR_pubkey_check), MP_ROM_PTR(&auth_pubkey_check_obj) },
};
static MP_DEFINE_CONST_DICT(auth_globals, auth_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_auth = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&auth_globals,
};
