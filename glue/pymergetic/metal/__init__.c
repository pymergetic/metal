/*
 * pymergetic.metal — package node.
 * Builtin C/RS faces nest here; frozen CORE Py (inspect/arch/net.microdot) via __path__.
 *
 * Globals are mutable so frozen submodules can bind via import store_attr.
 */
#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"

#include "modules.h"

static const MP_DEFINE_STR_OBJ(metal_path_obj, ".frozen/pymergetic/metal");

static mp_obj_dict_t metal_globals;
static int metal_globals_ready;

void pm_metal_globals_init(void) {
    if (metal_globals_ready) {
        return;
    }
    mp_obj_dict_init(&metal_globals, 28);
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR___name__),
                      MP_OBJ_NEW_QSTR(MP_QSTR_pymergetic_dot_metal));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR___path__),
                      MP_OBJ_FROM_PTR(&metal_path_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR_util),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_util));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR_externals),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_externals));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR_auth),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_auth));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR_trust),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_trust));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR_mem),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_mem));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR_async),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_async));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR_process),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_process));
    /* Cold ledger + seat table (same store smoke / Inspect / REPL use). */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR_reg),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_reg));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR_boot),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_boot));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR_console),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_console));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR_rt),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_rt));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR_fs),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_fs));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR_pack),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_pack));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR_hwtree),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_hwtree));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR_net),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_net));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR_dev),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_dev));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR_draw),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_draw));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR_shell),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_shell));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR_bus),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_bus));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR_wamr_host),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_wamr_host));
    metal_globals_ready = 1;
}

const mp_obj_module_t mp_module_pymergetic_metal = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&metal_globals,
};
