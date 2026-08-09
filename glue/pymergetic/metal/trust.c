/*
 * pymergetic.metal.trust — µPy face (callee: src/.../trust).
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/trust/__init__.h>

#include <string.h>

static mp_obj_t trust_mode(void)
{
    const char *s = pm_metal_trust_mode_str();
    return mp_obj_new_str(s, strlen(s));
}
static MP_DEFINE_CONST_FUN_OBJ_0(trust_mode_obj, trust_mode);

static mp_obj_t trust_ready(void)
{
    return pm_metal_trust_ready() ? mp_const_true : mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_0(trust_ready_obj, trust_ready);

static mp_obj_t trust_verify_mods(mp_obj_t data_obj, mp_obj_t sig_obj)
{
    mp_buffer_info_t data;
    mp_buffer_info_t sig;
    mp_get_buffer_raise(data_obj, &data, MP_BUFFER_READ);
    mp_get_buffer_raise(sig_obj, &sig, MP_BUFFER_READ);
    return pm_metal_trust_verify_mods(data.buf, (uint32_t)data.len, sig.buf,
                                      (uint32_t)sig.len) == 0
               ? mp_const_true
               : mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_2(trust_verify_mods_obj, trust_verify_mods);

static const mp_rom_map_elem_t trust_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_trust) },
    { MP_ROM_QSTR(MP_QSTR_mode), MP_ROM_PTR(&trust_mode_obj) },
    { MP_ROM_QSTR(MP_QSTR_ready), MP_ROM_PTR(&trust_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_verify_mods), MP_ROM_PTR(&trust_verify_mods_obj) },
};
static MP_DEFINE_CONST_DICT(trust_globals, trust_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_trust = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&trust_globals,
};
