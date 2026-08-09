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
    mp_obj_dict_init(&metal_globals, 10);
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
#if !defined(PM_METAL_CFG_FW_BROWSER) || !PM_METAL_CFG_FW_BROWSER
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR_net),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_net));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&metal_globals), MP_OBJ_NEW_QSTR(MP_QSTR_dev),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_dev));
#endif
    metal_globals_ready = 1;
}

const mp_obj_module_t mp_module_pymergetic_metal = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&metal_globals,
};
