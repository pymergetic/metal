/*
 * pymergetic.metal.net — package node (nested builtins; firmware leaves).
 *
 * Globals are mutable so frozen CORE (net.microdot) can be bound via
 * import's mp_store_attr (ROM maps reject stores → AttributeError).
 */
#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"

#include "../modules.h"

#if !defined(PM_METAL_CFG_FW_BROWSER) || !PM_METAL_CFG_FW_BROWSER

static const MP_DEFINE_STR_OBJ(net_path_obj, ".frozen/pymergetic/metal/net");

static mp_obj_dict_t net_globals;
static int net_globals_ready;

void pm_metal_net_globals_init(void) {
    if (net_globals_ready) {
        return;
    }
    mp_obj_dict_init(&net_globals, 8);
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&net_globals), MP_OBJ_NEW_QSTR(MP_QSTR___name__),
                      MP_OBJ_NEW_QSTR(MP_QSTR_pymergetic_dot_metal_dot_net));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&net_globals), MP_OBJ_NEW_QSTR(MP_QSTR___path__),
                      MP_OBJ_FROM_PTR(&net_path_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&net_globals), MP_OBJ_NEW_QSTR(MP_QSTR_ip),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_net_ip));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&net_globals), MP_OBJ_NEW_QSTR(MP_QSTR_wg),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_net_wg));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&net_globals), MP_OBJ_NEW_QSTR(MP_QSTR_ssh),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_net_ssh));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&net_globals), MP_OBJ_NEW_QSTR(MP_QSTR_faces),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_net_faces));
    net_globals_ready = 1;
}

const mp_obj_module_t mp_module_pymergetic_metal_net = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&net_globals,
};

#endif
