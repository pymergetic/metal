/*
 * pymergetic.metal.trust — µPy face (callee: src/.../trust).
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/trust/__init__.h>
#include <string.h>

static mp_obj_t trust_mode(void)
{
    return mp_obj_new_int(pm_metal_trust_mode());
}
static MP_DEFINE_CONST_FUN_OBJ_0(trust_mode_obj, trust_mode);

static mp_obj_t trust_mode_str(void)
{
    const char *s = pm_metal_trust_mode_str();
    return mp_obj_new_str(s, strlen(s));
}
static MP_DEFINE_CONST_FUN_OBJ_0(trust_mode_str_obj, trust_mode_str);

static mp_obj_t trust_ready(void)
{
    return pm_metal_trust_ready() ? mp_const_true : mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_0(trust_ready_obj, trust_ready);

static mp_obj_t trust_mods_pubkey_set(size_t n_args, const mp_obj_t *args)
{
    const uint8_t *pk = NULL;
    uint32_t pk_len = 0;

    if (n_args >= 1 && args[0] != mp_const_none) {
        mp_buffer_info_t buf;
        mp_get_buffer_raise(args[0], &buf, MP_BUFFER_READ);
        pk = (const uint8_t *)buf.buf;
        pk_len = (uint32_t)buf.len;
    }
    return pm_metal_trust_mods_pubkey_set(pk, pk_len) == 0 ? mp_const_true : mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(trust_mods_pubkey_set_obj, 0, 1, trust_mods_pubkey_set);

static mp_obj_t trust_verify_mods(mp_obj_t data_obj, mp_obj_t sig_obj)
{
    mp_buffer_info_t data;
    mp_buffer_info_t sig;

    mp_get_buffer_raise(data_obj, &data, MP_BUFFER_READ);
    mp_get_buffer_raise(sig_obj, &sig, MP_BUFFER_READ);
    return pm_metal_trust_verify_mods(data.buf, (uint32_t)data.len, sig.buf, (uint32_t)sig.len) == 0
               ? mp_const_true
               : mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_2(trust_verify_mods_obj, trust_verify_mods);

static mp_obj_t trust_accept_mods(size_t n_args, const mp_obj_t *args)
{
    mp_buffer_info_t data;
    const void *sig = NULL;
    uint32_t sig_len = 0;

    mp_get_buffer_raise(args[0], &data, MP_BUFFER_READ);
    if (n_args >= 2 && args[1] != mp_const_none) {
        mp_buffer_info_t sig_buf;
        mp_get_buffer_raise(args[1], &sig_buf, MP_BUFFER_READ);
        sig = sig_buf.buf;
        sig_len = (uint32_t)sig_buf.len;
    }
    return pm_metal_trust_accept_mods(data.buf, (uint32_t)data.len, sig, sig_len) == 0
               ? mp_const_true
               : mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(trust_accept_mods_obj, 1, 2, trust_accept_mods);

static mp_obj_t trust_proof(void)
{
    return pm_metal_trust_proof() == 0 ? mp_const_true : mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_0(trust_proof_obj, trust_proof);

static const mp_rom_map_elem_t trust_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_trust) },
    { MP_ROM_QSTR(MP_QSTR_mode), MP_ROM_PTR(&trust_mode_obj) },
    { MP_ROM_QSTR(MP_QSTR_mode_str), MP_ROM_PTR(&trust_mode_str_obj) },
    { MP_ROM_QSTR(MP_QSTR_ready), MP_ROM_PTR(&trust_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_mods_pubkey_set), MP_ROM_PTR(&trust_mods_pubkey_set_obj) },
    { MP_ROM_QSTR(MP_QSTR_verify_mods), MP_ROM_PTR(&trust_verify_mods_obj) },
    { MP_ROM_QSTR(MP_QSTR_accept_mods), MP_ROM_PTR(&trust_accept_mods_obj) },
    { MP_ROM_QSTR(MP_QSTR_proof), MP_ROM_PTR(&trust_proof_obj) },
};
static MP_DEFINE_CONST_DICT(trust_globals, trust_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_trust = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&trust_globals,
};
