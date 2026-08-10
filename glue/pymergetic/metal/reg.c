/*
 * pymergetic.metal.reg — µPy face over cold ledger + seat table.
 *
 * Diagnostic dumps use Metal-heap linked/growable buffers (*_heap APIs) —
 * never oversized stack scratch for inspect text.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/mem/port/__init__.h>
#include <pymergetic/metal/reg/ledger.h>
#include <pymergetic/metal/reg/seats.h>

#include <string.h>

static mp_obj_t heap_bytes_to_str(uint8_t *buf, int32_t n)
{
    mp_obj_t out;

    if (n < 0 || buf == NULL) {
        if (buf) {
            pm_metal_mem_free(buf);
        }
        return MP_OBJ_NULL;
    }
    out = mp_obj_new_str((const char *)buf, (size_t)n);
    pm_metal_mem_free(buf);
    return out;
}

static mp_obj_t reg_ledger_method_count(void)
{
    return mp_obj_new_int((mp_int_t)pm_metal_reg_ledger_method_count());
}
static MP_DEFINE_CONST_FUN_OBJ_0(reg_ledger_method_count_obj, reg_ledger_method_count);

static mp_obj_t reg_ledger_gap_count(void)
{
    return mp_obj_new_int((mp_int_t)pm_metal_reg_ledger_gap_count());
}
static MP_DEFINE_CONST_FUN_OBJ_0(reg_ledger_gap_count_obj, reg_ledger_gap_count);

static mp_obj_t reg_ledger_json(void)
{
    uint8_t *buf = NULL;
    int32_t n = pm_metal_reg_ledger_json_heap(&buf);
    mp_obj_t out = heap_bytes_to_str(buf, n);
    if (out == MP_OBJ_NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("ledger json"));
    }
    return out;
}
static MP_DEFINE_CONST_FUN_OBJ_0(reg_ledger_json_obj, reg_ledger_json);

static mp_obj_t reg_ledger_module_json(mp_obj_t module_obj)
{
    const char *module = mp_obj_str_get_str(module_obj);
    uint8_t *buf = NULL;
    int32_t n = pm_metal_reg_ledger_module_json_heap((const uint8_t *)module, &buf);
    mp_obj_t out = heap_bytes_to_str(buf, n);
    if (out == MP_OBJ_NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("module not in ledger"));
    }
    return out;
}
static MP_DEFINE_CONST_FUN_OBJ_1(reg_ledger_module_json_obj, reg_ledger_module_json);

static mp_obj_t reg_ledger_method_json(mp_obj_t module_obj, mp_obj_t func_obj)
{
    const char *module = mp_obj_str_get_str(module_obj);
    const char *func = mp_obj_str_get_str(func_obj);
    uint8_t *buf = NULL;
    int32_t n =
        pm_metal_reg_ledger_method_json_heap((const uint8_t *)module, (const uint8_t *)func, &buf);
    mp_obj_t out = heap_bytes_to_str(buf, n);
    if (out == MP_OBJ_NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("method not in ledger"));
    }
    return out;
}
static MP_DEFINE_CONST_FUN_OBJ_2(reg_ledger_method_json_obj, reg_ledger_method_json);

static mp_obj_t reg_seat_count(void)
{
    return mp_obj_new_int((mp_int_t)pm_metal_reg_seat_count());
}
static MP_DEFINE_CONST_FUN_OBJ_0(reg_seat_count_obj, reg_seat_count);

static mp_obj_t reg_seat_at(mp_obj_t index_obj)
{
    mp_int_t index = mp_obj_get_int(index_obj);
    char path[PM_METAL_REG_SEAT_PATH_MAX];
    int32_t kind = 0, fw = 0, browser = 0, has_test = 0;
    mp_obj_t items[5];

    if (index < 0
        || pm_metal_reg_seat_at((uint32_t)index, path, (uint32_t)sizeof(path), &kind, &fw, &browser,
                                &has_test)
            != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("seat index"));
    }
    items[0] = mp_obj_new_str(path, strlen(path));
    items[1] = mp_obj_new_int(kind);
    items[2] = mp_obj_new_bool(fw);
    items[3] = mp_obj_new_bool(browser);
    items[4] = mp_obj_new_bool(has_test);
    return mp_obj_new_tuple(5, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(reg_seat_at_obj, reg_seat_at);

static mp_obj_t reg_seats_json(void)
{
    char *buf = NULL;
    int32_t n = pm_metal_reg_seats_json_heap(&buf);
    mp_obj_t out = heap_bytes_to_str((uint8_t *)buf, n);
    if (out == MP_OBJ_NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("seats json"));
    }
    return out;
}
static MP_DEFINE_CONST_FUN_OBJ_0(reg_seats_json_obj, reg_seats_json);

static mp_obj_t reg_run_tests(void)
{
    return mp_obj_new_int((mp_int_t)pm_metal_reg_run_tests());
}
static MP_DEFINE_CONST_FUN_OBJ_0(reg_run_tests_obj, reg_run_tests);

static mp_obj_t reg_completeness(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    enum { ARG_module, ARG_gaps_only, ARG_detail, ARG_fmt };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_module, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_gaps_only, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_detail, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_fmt, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    const char *module = NULL;
    int32_t gaps_only;
    int32_t detail;
    int32_t fmt_json = 0;
    uint8_t *buf = NULL;
    int32_t n;
    mp_obj_t out;

    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    if (args[ARG_module].u_obj != mp_const_none) {
        module = mp_obj_str_get_str(args[ARG_module].u_obj);
    }
    gaps_only = args[ARG_gaps_only].u_bool ? 1 : 0;
    detail = args[ARG_detail].u_bool ? 1 : 0;
    if (args[ARG_fmt].u_obj != mp_const_none) {
        const char *fmt = mp_obj_str_get_str(args[ARG_fmt].u_obj);
        if (strcmp(fmt, "json") == 0) {
            fmt_json = 1;
        } else if (strcmp(fmt, "tree") != 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("fmt tree|json"));
        }
    }

    n = pm_metal_reg_ledger_completeness_heap((const uint8_t *)module, gaps_only, detail, fmt_json,
                                              &buf);
    out = heap_bytes_to_str(buf, n);
    if (out == MP_OBJ_NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("completeness"));
    }
    return out;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(reg_completeness_obj, 0, reg_completeness);

static const mp_rom_map_elem_t reg_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_reg) },
    { MP_ROM_QSTR(MP_QSTR_ledger_method_count), MP_ROM_PTR(&reg_ledger_method_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_ledger_gap_count), MP_ROM_PTR(&reg_ledger_gap_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_ledger_json), MP_ROM_PTR(&reg_ledger_json_obj) },
    { MP_ROM_QSTR(MP_QSTR_ledger_module_json), MP_ROM_PTR(&reg_ledger_module_json_obj) },
    { MP_ROM_QSTR(MP_QSTR_ledger_method_json), MP_ROM_PTR(&reg_ledger_method_json_obj) },
    { MP_ROM_QSTR(MP_QSTR_completeness), MP_ROM_PTR(&reg_completeness_obj) },
    { MP_ROM_QSTR(MP_QSTR_seat_count), MP_ROM_PTR(&reg_seat_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_seat_at), MP_ROM_PTR(&reg_seat_at_obj) },
    { MP_ROM_QSTR(MP_QSTR_seats_json), MP_ROM_PTR(&reg_seats_json_obj) },
    { MP_ROM_QSTR(MP_QSTR_run_tests), MP_ROM_PTR(&reg_run_tests_obj) },
};
static MP_DEFINE_CONST_DICT(reg_module_globals, reg_module_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_reg = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&reg_module_globals,
};
