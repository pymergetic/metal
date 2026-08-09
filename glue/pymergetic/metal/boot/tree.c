/*
 * pymergetic.metal.boot.tree — µPy face (callee: src/.../boot/tree).
 */
#include <string.h>

#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/boot/tree.h>

static mp_obj_t boot_print_cb = MP_OBJ_NULL;
static void boot_print_tramp(const char *line, void *user)
{
    (void)user;
    if (boot_print_cb != MP_OBJ_NULL && boot_print_cb != mp_const_none && line != NULL) {
        mp_call_function_1(boot_print_cb, mp_obj_new_str(line, strlen(line)));
    }
}

static mp_obj_t boot_set_print(mp_obj_t fn_obj)
{
    if (fn_obj == mp_const_none) {
        boot_print_cb = MP_OBJ_NULL;
        pm_metal_boot_set_print(NULL, NULL);
    } else {
        boot_print_cb = fn_obj;
        pm_metal_boot_set_print(boot_print_tramp, NULL);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(boot_set_print_obj, boot_set_print);

static mp_obj_t boot_emit(mp_obj_t line_obj)
{
    pm_metal_boot_emit(mp_obj_str_get_str(line_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(boot_emit_obj, boot_emit);

static mp_obj_t boot_banner(mp_obj_t ver_obj, mp_obj_t cpu_obj)
{
    pm_metal_boot_banner(mp_obj_str_get_str(ver_obj), mp_obj_str_get_str(cpu_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(boot_banner_obj, boot_banner);

static mp_obj_t boot_tree_reset(void)
{
    pm_metal_boot_tree_reset();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(boot_tree_reset_obj, boot_tree_reset);

static mp_obj_t boot_tree_enter(mp_obj_t name_obj)
{
    pm_metal_boot_tree_enter(mp_obj_str_get_str(name_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(boot_tree_enter_obj, boot_tree_enter);

static mp_obj_t boot_tree_enter_ex(mp_obj_t name_obj, mp_obj_t st_obj, mp_obj_t detail_obj)
{
    const char *detail = NULL;
    if (detail_obj != mp_const_none) {
        detail = mp_obj_str_get_str(detail_obj);
    }
    pm_metal_boot_tree_enter_ex(mp_obj_str_get_str(name_obj),
                                (pm_metal_boot_tree_status_t)mp_obj_get_int(st_obj), detail);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(boot_tree_enter_ex_obj, boot_tree_enter_ex);

static mp_obj_t boot_tree_item(mp_obj_t name_obj, mp_obj_t st_obj, mp_obj_t detail_obj)
{
    const char *detail = NULL;
    if (detail_obj != mp_const_none) {
        detail = mp_obj_str_get_str(detail_obj);
    }
    pm_metal_boot_tree_item(mp_obj_str_get_str(name_obj),
                            (pm_metal_boot_tree_status_t)mp_obj_get_int(st_obj), detail);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(boot_tree_item_obj, boot_tree_item);

static mp_obj_t boot_tree_leave(void)
{
    pm_metal_boot_tree_leave();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(boot_tree_leave_obj, boot_tree_leave);

static mp_obj_t boot_tree_ready_ok(void)
{
    pm_metal_boot_tree_ready_ok();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(boot_tree_ready_ok_obj, boot_tree_ready_ok);

static mp_obj_t boot_rainbow_metalpython(mp_obj_t ver_obj, mp_obj_t cpu_obj)
{
    pm_metal_boot_rainbow_metalpython(mp_obj_str_get_str(ver_obj), mp_obj_str_get_str(cpu_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(boot_rainbow_metalpython_obj, boot_rainbow_metalpython);

static mp_obj_t boot_tree_print(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_boot_tree_print());
}
static MP_DEFINE_CONST_FUN_OBJ_0(boot_tree_print_obj, boot_tree_print);

static const mp_rom_map_elem_t boot_tree_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_boot_dot_tree) },
    { MP_ROM_QSTR(MP_QSTR_set_print), MP_ROM_PTR(&boot_set_print_obj) },
    { MP_ROM_QSTR(MP_QSTR_emit), MP_ROM_PTR(&boot_emit_obj) },
    { MP_ROM_QSTR(MP_QSTR_banner), MP_ROM_PTR(&boot_banner_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset), MP_ROM_PTR(&boot_tree_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_enter), MP_ROM_PTR(&boot_tree_enter_obj) },
    { MP_ROM_QSTR(MP_QSTR_enter_ex), MP_ROM_PTR(&boot_tree_enter_ex_obj) },
    { MP_ROM_QSTR(MP_QSTR_item), MP_ROM_PTR(&boot_tree_item_obj) },
    { MP_ROM_QSTR(MP_QSTR_leave), MP_ROM_PTR(&boot_tree_leave_obj) },
    { MP_ROM_QSTR(MP_QSTR_ready_ok), MP_ROM_PTR(&boot_tree_ready_ok_obj) },
    { MP_ROM_QSTR(MP_QSTR_rainbow_metalpython), MP_ROM_PTR(&boot_rainbow_metalpython_obj) },
    { MP_ROM_QSTR(MP_QSTR_print), MP_ROM_PTR(&boot_tree_print_obj) },
};
static MP_DEFINE_CONST_DICT(boot_tree_globals, boot_tree_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_boot_tree = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&boot_tree_globals,
};
