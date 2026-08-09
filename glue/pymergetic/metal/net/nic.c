/*
 * pymergetic.metal.net.nic — µPy face (callee: src/.../net/nic).
 * Firmware seats only. register/ops need C ops tables — not on this face.
 */
#include <string.h>

#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/net/nic/__init__.h>
#include <pymergetic/metal/reg/seats.h>

static mp_obj_t nic_name(void)
{
    const char *n = pm_metal_net_nic_name();
    if (n == NULL) {
        return mp_const_none;
    }
    return mp_obj_new_str(n, strlen(n));
}
static MP_DEFINE_CONST_FUN_OBJ_0(nic_name_obj, nic_name);

static mp_obj_t nic_attach_upy(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_nic_attach_upy());
}
static MP_DEFINE_CONST_FUN_OBJ_0(nic_attach_upy_obj, nic_attach_upy);

static const mp_rom_map_elem_t nic_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_net_dot_nic) },
    { MP_ROM_QSTR(MP_QSTR_name), MP_ROM_PTR(&nic_name_obj) },
    { MP_ROM_QSTR(MP_QSTR_attach_upy), MP_ROM_PTR(&nic_attach_upy_obj) },
};
static MP_DEFINE_CONST_DICT(nic_globals, nic_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_net_nic = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&nic_globals,
};

PM_METAL_REG_SEAT(g_pm_seat_net_nic, "pymergetic.metal.net.nic", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
