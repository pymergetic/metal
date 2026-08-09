/*
 * pymergetic.metal.boot — package node + thin UX face over boot.tree.
 */
#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"

#include "../modules.h"
#include <pymergetic/metal/boot/__init__.h>

static const MP_DEFINE_STR_OBJ(boot_path_obj, ".frozen/pymergetic/metal/boot");

static mp_obj_t boot_banner(mp_obj_t version_obj, mp_obj_t cpu_obj)
{
    pm_metal_boot_banner(mp_obj_str_get_str(version_obj), mp_obj_str_get_str(cpu_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(boot_banner_obj, boot_banner);

static mp_obj_t boot_emit(mp_obj_t line_obj)
{
    pm_metal_boot_emit(mp_obj_str_get_str(line_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(boot_emit_obj, boot_emit);

static mp_obj_t boot_tree_ready_ok(void)
{
    pm_metal_boot_tree_ready_ok();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(boot_tree_ready_ok_obj, boot_tree_ready_ok);

static mp_obj_t boot_tree_print(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_boot_tree_print());
}
static MP_DEFINE_CONST_FUN_OBJ_0(boot_tree_print_obj, boot_tree_print);

static mp_obj_dict_t boot_globals;
static int boot_globals_ready;

void pm_metal_boot_globals_init(void) {
    if (boot_globals_ready) {
        return;
    }
    mp_obj_dict_init(&boot_globals, 8);
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&boot_globals), MP_OBJ_NEW_QSTR(MP_QSTR___name__),
                      MP_OBJ_NEW_QSTR(MP_QSTR_pymergetic_dot_metal_dot_boot));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&boot_globals), MP_OBJ_NEW_QSTR(MP_QSTR___path__),
                      MP_OBJ_FROM_PTR(&boot_path_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&boot_globals), MP_OBJ_NEW_QSTR(MP_QSTR_tree),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_boot_tree));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&boot_globals), MP_OBJ_NEW_QSTR(MP_QSTR_banner),
                      MP_OBJ_FROM_PTR(&boot_banner_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&boot_globals), MP_OBJ_NEW_QSTR(MP_QSTR_emit),
                      MP_OBJ_FROM_PTR(&boot_emit_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&boot_globals), MP_OBJ_NEW_QSTR(MP_QSTR_tree_ready_ok),
                      MP_OBJ_FROM_PTR(&boot_tree_ready_ok_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&boot_globals), MP_OBJ_NEW_QSTR(MP_QSTR_tree_print),
                      MP_OBJ_FROM_PTR(&boot_tree_print_obj));
    boot_globals_ready = 1;
}

const mp_obj_module_t mp_module_pymergetic_metal_boot = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&boot_globals,
};
