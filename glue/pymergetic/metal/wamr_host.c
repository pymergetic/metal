/* pymergetic.metal.wamr_host — µPy face (pointer-safe). */
#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"

#include <pymergetic/metal/wamr_host/__init__.h>
static mp_obj_t wamr_host_fetch_register(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_buffer_info_t full_module_bi;
    const uint8_t *full_module;
    if (args[0] == mp_const_none) { full_module = NULL; }
    else if (mp_obj_is_str_or_bytes(args[0])) {
        size_t _full_module_n; const char *_full_module_s = mp_obj_str_get_data(args[0], &_full_module_n); full_module=(const uint8_t*)_full_module_s;
    } else {
        mp_get_buffer_raise(args[0], &full_module_bi, MP_BUFFER_READ); full_module=(const uint8_t*)full_module_bi.buf;
    }
    mp_buffer_info_t url_bi;
    const char *url;
    if (args[1] == mp_const_none) { url = NULL; }
    else if (mp_obj_is_str_or_bytes(args[1])) {
        size_t _url_n; url = mp_obj_str_get_data(args[1], &_url_n);
    } else {
        mp_get_buffer_raise(args[1], &url_bi, MP_BUFFER_READ); url=(const char *)url_bi.buf;
    }
    mp_buffer_info_t sig_bi;
    const uint8_t *sig;
    if (args[2] == mp_const_none) { sig = NULL; }
    else if (mp_obj_is_str_or_bytes(args[2])) {
        size_t _sig_n; const char *_sig_s = mp_obj_str_get_data(args[2], &_sig_n); sig=(const uint8_t*)_sig_s;
    } else {
        mp_get_buffer_raise(args[2], &sig_bi, MP_BUFFER_READ); sig=(const uint8_t*)sig_bi.buf;
    }
    uint32_t sig_len = (uint32_t)mp_obj_get_int(args[3]);
    return mp_obj_new_int((mp_int_t)pm_metal_wasm_fetch_register(full_module, url, sig, sig_len));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(wamr_host_fetch_register_obj, 4, 4, wamr_host_fetch_register);

static mp_obj_t wamr_host_proof_fetch(void) {
    return mp_obj_new_int((mp_int_t)pm_metal_wasm_proof_fetch());
}
static MP_DEFINE_CONST_FUN_OBJ_0(wamr_host_proof_fetch_obj, wamr_host_proof_fetch);

static mp_obj_t wamr_host_guest_coro_create_for(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_buffer_info_t full_module_bi;
    const uint8_t *full_module;
    if (args[0] == mp_const_none) { full_module = NULL; }
    else if (mp_obj_is_str_or_bytes(args[0])) {
        size_t _full_module_n; const char *_full_module_s = mp_obj_str_get_data(args[0], &_full_module_n); full_module=(const uint8_t*)_full_module_s;
    } else {
        mp_get_buffer_raise(args[0], &full_module_bi, MP_BUFFER_READ); full_module=(const uint8_t*)full_module_bi.buf;
    }
    uint32_t state_bytes = (uint32_t)mp_obj_get_int(args[1]);
    return mp_obj_new_int((mp_int_t)pm_metal_wasm_guest_coro_create_for(full_module, state_bytes));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(wamr_host_guest_coro_create_for_obj, 2, 2, wamr_host_guest_coro_create_for);

static mp_obj_t wamr_host_ready(void) {
    return mp_obj_new_int((mp_int_t)pm_metal_wasm_ready());
}
static MP_DEFINE_CONST_FUN_OBJ_0(wamr_host_ready_obj, wamr_host_ready);

static mp_obj_t wamr_host_init(void) {
    return mp_obj_new_int((mp_int_t)pm_metal_wasm_init());
}
static MP_DEFINE_CONST_FUN_OBJ_0(wamr_host_init_obj, wamr_host_init);

static mp_obj_t wamr_host_shutdown(void) {
    pm_metal_wasm_shutdown(); return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(wamr_host_shutdown_obj, wamr_host_shutdown);

static mp_obj_t wamr_host_load(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_buffer_info_t full_module_bi;
    const uint8_t *full_module;
    if (args[0] == mp_const_none) { full_module = NULL; }
    else if (mp_obj_is_str_or_bytes(args[0])) {
        size_t _full_module_n; const char *_full_module_s = mp_obj_str_get_data(args[0], &_full_module_n); full_module=(const uint8_t*)_full_module_s;
    } else {
        mp_get_buffer_raise(args[0], &full_module_bi, MP_BUFFER_READ); full_module=(const uint8_t*)full_module_bi.buf;
    }
    mp_buffer_info_t bytes_bi;
    const uint8_t *bytes;
    if (args[1] == mp_const_none) { bytes = NULL; }
    else if (mp_obj_is_str_or_bytes(args[1])) {
        size_t _bytes_n; const char *_bytes_s = mp_obj_str_get_data(args[1], &_bytes_n); bytes=(const uint8_t*)_bytes_s;
    } else {
        mp_get_buffer_raise(args[1], &bytes_bi, MP_BUFFER_READ); bytes=(const uint8_t*)bytes_bi.buf;
    }
    uint32_t len = (uint32_t)mp_obj_get_int(args[2]);
    return mp_obj_new_int((mp_int_t)pm_metal_wasm_load(full_module, bytes, len));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(wamr_host_load_obj, 3, 3, wamr_host_load);

static mp_obj_t wamr_host_image(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_buffer_info_t full_module_bi;
    const uint8_t *full_module;
    if (args[0] == mp_const_none) { full_module = NULL; }
    else if (mp_obj_is_str_or_bytes(args[0])) {
        size_t _full_module_n; const char *_full_module_s = mp_obj_str_get_data(args[0], &_full_module_n); full_module=(const uint8_t*)_full_module_s;
    } else {
        mp_get_buffer_raise(args[0], &full_module_bi, MP_BUFFER_READ); full_module=(const uint8_t*)full_module_bi.buf;
    }
    const uint8_t **out_bytes =
        (args[1] == mp_const_none) ? NULL : (const uint8_t **)(uintptr_t)mp_obj_get_int(args[1]);
    uint32_t *out_len =
        (args[2] == mp_const_none) ? NULL : (uint32_t *)(uintptr_t)mp_obj_get_int(args[2]);
    return mp_obj_new_int((mp_int_t)pm_metal_wasm_image(full_module, out_bytes, out_len));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(wamr_host_image_obj, 3, 3, wamr_host_image);

static mp_obj_t wamr_host_register(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_buffer_info_t full_module_bi;
    const uint8_t *full_module;
    if (args[0] == mp_const_none) { full_module = NULL; }
    else if (mp_obj_is_str_or_bytes(args[0])) {
        size_t _full_module_n; const char *_full_module_s = mp_obj_str_get_data(args[0], &_full_module_n); full_module=(const uint8_t*)_full_module_s;
    } else {
        mp_get_buffer_raise(args[0], &full_module_bi, MP_BUFFER_READ); full_module=(const uint8_t*)full_module_bi.buf;
    }
    return mp_obj_new_int((mp_int_t)pm_metal_wasm_register(full_module));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(wamr_host_register_obj, 1, 1, wamr_host_register);

static mp_obj_t wamr_host_load_register(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_buffer_info_t full_module_bi;
    const uint8_t *full_module;
    if (args[0] == mp_const_none) { full_module = NULL; }
    else if (mp_obj_is_str_or_bytes(args[0])) {
        size_t _full_module_n; const char *_full_module_s = mp_obj_str_get_data(args[0], &_full_module_n); full_module=(const uint8_t*)_full_module_s;
    } else {
        mp_get_buffer_raise(args[0], &full_module_bi, MP_BUFFER_READ); full_module=(const uint8_t*)full_module_bi.buf;
    }
    mp_buffer_info_t bytes_bi;
    const uint8_t *bytes;
    if (args[1] == mp_const_none) { bytes = NULL; }
    else if (mp_obj_is_str_or_bytes(args[1])) {
        size_t _bytes_n; const char *_bytes_s = mp_obj_str_get_data(args[1], &_bytes_n); bytes=(const uint8_t*)_bytes_s;
    } else {
        mp_get_buffer_raise(args[1], &bytes_bi, MP_BUFFER_READ); bytes=(const uint8_t*)bytes_bi.buf;
    }
    uint32_t len = (uint32_t)mp_obj_get_int(args[2]);
    return mp_obj_new_int((mp_int_t)pm_metal_wasm_load_register(full_module, bytes, len));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(wamr_host_load_register_obj, 3, 3, wamr_host_load_register);

static mp_obj_t wamr_host_load_verified(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_buffer_info_t full_module_bi;
    const uint8_t *full_module;
    if (args[0] == mp_const_none) { full_module = NULL; }
    else if (mp_obj_is_str_or_bytes(args[0])) {
        size_t _full_module_n; const char *_full_module_s = mp_obj_str_get_data(args[0], &_full_module_n); full_module=(const uint8_t*)_full_module_s;
    } else {
        mp_get_buffer_raise(args[0], &full_module_bi, MP_BUFFER_READ); full_module=(const uint8_t*)full_module_bi.buf;
    }
    mp_buffer_info_t bytes_bi;
    const uint8_t *bytes;
    if (args[1] == mp_const_none) { bytes = NULL; }
    else if (mp_obj_is_str_or_bytes(args[1])) {
        size_t _bytes_n; const char *_bytes_s = mp_obj_str_get_data(args[1], &_bytes_n); bytes=(const uint8_t*)_bytes_s;
    } else {
        mp_get_buffer_raise(args[1], &bytes_bi, MP_BUFFER_READ); bytes=(const uint8_t*)bytes_bi.buf;
    }
    uint32_t len = (uint32_t)mp_obj_get_int(args[2]);
    mp_buffer_info_t sig_bi;
    const uint8_t *sig;
    if (args[3] == mp_const_none) { sig = NULL; }
    else if (mp_obj_is_str_or_bytes(args[3])) {
        size_t _sig_n; const char *_sig_s = mp_obj_str_get_data(args[3], &_sig_n); sig=(const uint8_t*)_sig_s;
    } else {
        mp_get_buffer_raise(args[3], &sig_bi, MP_BUFFER_READ); sig=(const uint8_t*)sig_bi.buf;
    }
    uint32_t sig_len = (uint32_t)mp_obj_get_int(args[4]);
    return mp_obj_new_int((mp_int_t)pm_metal_wasm_load_verified(full_module, bytes, len, sig, sig_len));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(wamr_host_load_verified_obj, 5, 5, wamr_host_load_verified);

static mp_obj_t wamr_host_unload(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_buffer_info_t full_module_bi;
    const uint8_t *full_module;
    if (args[0] == mp_const_none) { full_module = NULL; }
    else if (mp_obj_is_str_or_bytes(args[0])) {
        size_t _full_module_n; const char *_full_module_s = mp_obj_str_get_data(args[0], &_full_module_n); full_module=(const uint8_t*)_full_module_s;
    } else {
        mp_get_buffer_raise(args[0], &full_module_bi, MP_BUFFER_READ); full_module=(const uint8_t*)full_module_bi.buf;
    }
    return mp_obj_new_int((mp_int_t)pm_metal_wasm_unload(full_module));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(wamr_host_unload_obj, 1, 1, wamr_host_unload);

static mp_obj_t wamr_host_call0(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_buffer_info_t full_module_bi;
    const uint8_t *full_module;
    if (args[0] == mp_const_none) { full_module = NULL; }
    else if (mp_obj_is_str_or_bytes(args[0])) {
        size_t _full_module_n; const char *_full_module_s = mp_obj_str_get_data(args[0], &_full_module_n); full_module=(const uint8_t*)_full_module_s;
    } else {
        mp_get_buffer_raise(args[0], &full_module_bi, MP_BUFFER_READ); full_module=(const uint8_t*)full_module_bi.buf;
    }
    mp_buffer_info_t func_bi;
    const uint8_t *func;
    if (args[1] == mp_const_none) { func = NULL; }
    else if (mp_obj_is_str_or_bytes(args[1])) {
        size_t _func_n; const char *_func_s = mp_obj_str_get_data(args[1], &_func_n); func=(const uint8_t*)_func_s;
    } else {
        mp_get_buffer_raise(args[1], &func_bi, MP_BUFFER_READ); func=(const uint8_t*)func_bi.buf;
    }
    return mp_obj_new_int((mp_int_t)pm_metal_wasm_call0(full_module, func));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(wamr_host_call0_obj, 2, 2, wamr_host_call0);

static mp_obj_t wamr_host_proof_stress(void) {
    return mp_obj_new_int((mp_int_t)pm_metal_wasm_proof_stress());
}
static MP_DEFINE_CONST_FUN_OBJ_0(wamr_host_proof_stress_obj, wamr_host_proof_stress);

static mp_obj_t wamr_host_proof(void) {
    return mp_obj_new_int((mp_int_t)pm_metal_wasm_proof());
}
static MP_DEFINE_CONST_FUN_OBJ_0(wamr_host_proof_obj, wamr_host_proof);

static const mp_rom_map_elem_t wamr_host_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_wamr_host) },
    { MP_ROM_QSTR(MP_QSTR_fetch_register), MP_ROM_PTR(&wamr_host_fetch_register_obj) },
    { MP_ROM_QSTR(MP_QSTR_proof_fetch), MP_ROM_PTR(&wamr_host_proof_fetch_obj) },
    { MP_ROM_QSTR(MP_QSTR_guest_coro_create_for), MP_ROM_PTR(&wamr_host_guest_coro_create_for_obj) },
    { MP_ROM_QSTR(MP_QSTR_ready), MP_ROM_PTR(&wamr_host_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&wamr_host_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_shutdown), MP_ROM_PTR(&wamr_host_shutdown_obj) },
    { MP_ROM_QSTR(MP_QSTR_load), MP_ROM_PTR(&wamr_host_load_obj) },
    { MP_ROM_QSTR(MP_QSTR_image), MP_ROM_PTR(&wamr_host_image_obj) },
    { MP_ROM_QSTR(MP_QSTR_register), MP_ROM_PTR(&wamr_host_register_obj) },
    { MP_ROM_QSTR(MP_QSTR_load_register), MP_ROM_PTR(&wamr_host_load_register_obj) },
    { MP_ROM_QSTR(MP_QSTR_load_verified), MP_ROM_PTR(&wamr_host_load_verified_obj) },
    { MP_ROM_QSTR(MP_QSTR_unload), MP_ROM_PTR(&wamr_host_unload_obj) },
    { MP_ROM_QSTR(MP_QSTR_call0), MP_ROM_PTR(&wamr_host_call0_obj) },
    { MP_ROM_QSTR(MP_QSTR_proof_stress), MP_ROM_PTR(&wamr_host_proof_stress_obj) },
    { MP_ROM_QSTR(MP_QSTR_proof), MP_ROM_PTR(&wamr_host_proof_obj) },
};
static MP_DEFINE_CONST_DICT(wamr_host_globals, wamr_host_globals_table);
const mp_obj_module_t mp_module_pymergetic_metal_wamr_host = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&wamr_host_globals,
};
