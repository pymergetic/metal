/* pymergetic.metal.rt — µPy face (pointer-safe). */
#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include <pymergetic/metal/rt/__init__.h>
#include <pymergetic/metal/reg/seats.h>

static mp_obj_t rt_halt(void) {
    return mp_obj_new_int((mp_int_t)pm_metal_rt_halt());
}
static MP_DEFINE_CONST_FUN_OBJ_0(rt_halt_obj, rt_halt);

static mp_obj_t rt_panic(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_buffer_info_t msg_bi;
    const uint8_t *msg;
    if (args[0] == mp_const_none) { msg = NULL; }
    else if (mp_obj_is_str_or_bytes(args[0])) {
        size_t _msg_n; const char *_msg_s = mp_obj_str_get_data(args[0], &_msg_n); msg=(const uint8_t*)_msg_s;
    } else {
        mp_get_buffer_raise(args[0], &msg_bi, MP_BUFFER_READ); msg=(const uint8_t*)msg_bi.buf;
    }
    return mp_obj_new_int((mp_int_t)pm_metal_rt_panic(msg));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rt_panic_obj, 1, 1, rt_panic);

static mp_obj_t rt_panic_at(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_buffer_info_t file_bi;
    const uint8_t *file;
    if (args[0] == mp_const_none) { file = NULL; }
    else if (mp_obj_is_str_or_bytes(args[0])) {
        size_t _file_n; const char *_file_s = mp_obj_str_get_data(args[0], &_file_n); file=(const uint8_t*)_file_s;
    } else {
        mp_get_buffer_raise(args[0], &file_bi, MP_BUFFER_READ); file=(const uint8_t*)file_bi.buf;
    }
    uint32_t line = (uint32_t)mp_obj_get_int(args[1]);
    mp_buffer_info_t msg_bi;
    const uint8_t *msg;
    if (args[2] == mp_const_none) { msg = NULL; }
    else if (mp_obj_is_str_or_bytes(args[2])) {
        size_t _msg_n; const char *_msg_s = mp_obj_str_get_data(args[2], &_msg_n); msg=(const uint8_t*)_msg_s;
    } else {
        mp_get_buffer_raise(args[2], &msg_bi, MP_BUFFER_READ); msg=(const uint8_t*)msg_bi.buf;
    }
    return mp_obj_new_int((mp_int_t)pm_metal_rt_panic_at(file, line, msg));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(rt_panic_at_obj, 3, 3, rt_panic_at);

static mp_obj_t rt_register_symbols(void) {
    return mp_obj_new_int((mp_int_t)pm_metal_rt_register_symbols());
}
static MP_DEFINE_CONST_FUN_OBJ_0(rt_register_symbols_obj, rt_register_symbols);

static mp_obj_t rt_connect_symbols(void) {
    return mp_obj_new_int((mp_int_t)pm_metal_rt_connect_symbols());
}
static MP_DEFINE_CONST_FUN_OBJ_0(rt_connect_symbols_obj, rt_connect_symbols);

static const mp_rom_map_elem_t rt_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_rt) },
    { MP_ROM_QSTR(MP_QSTR_halt), MP_ROM_PTR(&rt_halt_obj) },
    { MP_ROM_QSTR(MP_QSTR_panic), MP_ROM_PTR(&rt_panic_obj) },
    { MP_ROM_QSTR(MP_QSTR_panic_at), MP_ROM_PTR(&rt_panic_at_obj) },
    { MP_ROM_QSTR(MP_QSTR_register_symbols), MP_ROM_PTR(&rt_register_symbols_obj) },
    { MP_ROM_QSTR(MP_QSTR_connect_symbols), MP_ROM_PTR(&rt_connect_symbols_obj) },
};
static MP_DEFINE_CONST_DICT(rt_globals, rt_globals_table);
const mp_obj_module_t mp_module_pymergetic_metal_rt = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&rt_globals,
};

PM_METAL_REG_SEAT(g_pm_seat_rt, "pymergetic.metal.rt", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
