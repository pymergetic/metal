/*
 * pymergetic.metal.net — package node (nested builtins; firmware leaves).
 *
 * Globals are mutable so frozen CORE (net.microdot) can be bound via
 * import's mp_store_attr (ROM maps reject stores → AttributeError).
 *
 * Browser: nest only seats linked on wasm (faces, nic). Firmware: full tree.
 */
#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"

#include "../modules.h"

static const MP_DEFINE_STR_OBJ(net_path_obj, ".frozen/pymergetic/metal/net");

static mp_obj_dict_t net_globals;
static int net_globals_ready;

void pm_metal_net_globals_init(void) {
    if (net_globals_ready) {
        return;
    }
    mp_obj_dict_init(&net_globals, 20);
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&net_globals), MP_OBJ_NEW_QSTR(MP_QSTR___name__),
                      MP_OBJ_NEW_QSTR(MP_QSTR_pymergetic_dot_metal_dot_net));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&net_globals), MP_OBJ_NEW_QSTR(MP_QSTR___path__),
                      MP_OBJ_FROM_PTR(&net_path_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&net_globals), MP_OBJ_NEW_QSTR(MP_QSTR_faces),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_net_faces));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&net_globals), MP_OBJ_NEW_QSTR(MP_QSTR_nic),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_net_nic));
#if !defined(PM_METAL_CFG_FW_BROWSER) || !PM_METAL_CFG_FW_BROWSER
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&net_globals), MP_OBJ_NEW_QSTR(MP_QSTR_ip),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_net_ip));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&net_globals), MP_OBJ_NEW_QSTR(MP_QSTR_wg),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_net_wg));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&net_globals), MP_OBJ_NEW_QSTR(MP_QSTR_ssh),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_net_ssh));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&net_globals), MP_OBJ_NEW_QSTR(MP_QSTR_asgi),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_net_asgi));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&net_globals), MP_OBJ_NEW_QSTR(MP_QSTR_dhcp),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_net_dhcp));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&net_globals), MP_OBJ_NEW_QSTR(MP_QSTR_dns),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_net_dns));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&net_globals), MP_OBJ_NEW_QSTR(MP_QSTR_http),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_net_http));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&net_globals), MP_OBJ_NEW_QSTR(MP_QSTR_ntp),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_net_ntp));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&net_globals), MP_OBJ_NEW_QSTR(MP_QSTR_pump),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_net_pump));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&net_globals), MP_OBJ_NEW_QSTR(MP_QSTR_tftp),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_net_tftp));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&net_globals), MP_OBJ_NEW_QSTR(MP_QSTR_tls),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_net_tls));
#endif
    net_globals_ready = 1;
}

const mp_obj_module_t mp_module_pymergetic_metal_net = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&net_globals,
};
