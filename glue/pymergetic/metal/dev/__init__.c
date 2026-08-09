/*
 * pymergetic.metal.dev — package node (mutable nest; firmware leaves).
 */
#include "py/obj.h"
#include "py/runtime.h"

#include "../modules.h"

#if !defined(PM_METAL_CFG_FW_BROWSER) || !PM_METAL_CFG_FW_BROWSER

static mp_obj_dict_t dev_globals;
static int dev_globals_ready;

void pm_metal_dev_globals_init(void) {
    if (dev_globals_ready) {
        return;
    }
    mp_obj_dict_init(&dev_globals, 12);
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&dev_globals), MP_OBJ_NEW_QSTR(MP_QSTR___name__),
                      MP_OBJ_NEW_QSTR(MP_QSTR_pymergetic_dot_metal_dot_dev));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&dev_globals), MP_OBJ_NEW_QSTR(MP_QSTR_serial),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_dev_serial));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&dev_globals), MP_OBJ_NEW_QSTR(MP_QSTR_acpi),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_dev_acpi));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&dev_globals), MP_OBJ_NEW_QSTR(MP_QSTR_gfx),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_dev_gfx));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&dev_globals), MP_OBJ_NEW_QSTR(MP_QSTR_input),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_dev_input));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&dev_globals), MP_OBJ_NEW_QSTR(MP_QSTR_net),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_dev_net));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&dev_globals), MP_OBJ_NEW_QSTR(MP_QSTR_stream),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_dev_stream));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&dev_globals), MP_OBJ_NEW_QSTR(MP_QSTR_blk),
                      MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_dev_blk));
    dev_globals_ready = 1;
}

const mp_obj_module_t mp_module_pymergetic_metal_dev = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&dev_globals,
};

#endif
