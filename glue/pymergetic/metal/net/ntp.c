/*
 * pymergetic.metal.net.ntp — µPy face (callee: src/.../net/ntp).
 * Firmware seats only.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/net/ntp/__init__.h>

static mp_obj_t ntp_query(mp_obj_t server_obj)
{
    uint32_t secs = 0;
    int32_t rc = pm_metal_net_ntp_query((uint32_t)mp_obj_get_int(server_obj), &secs);
    mp_obj_t items[2];

    items[0] = MP_OBJ_NEW_SMALL_INT(rc);
    items[1] = mp_obj_new_int_from_uint(secs);
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(ntp_query_obj, ntp_query);

static mp_obj_t ntp_query_host(mp_obj_t host_obj)
{
    uint32_t secs = 0;
    int32_t rc = pm_metal_net_ntp_query_host(mp_obj_str_get_str(host_obj), &secs);
    mp_obj_t items[2];

    items[0] = MP_OBJ_NEW_SMALL_INT(rc);
    items[1] = mp_obj_new_int_from_uint(secs);
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(ntp_query_host_obj, ntp_query_host);

static const mp_rom_map_elem_t ntp_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_net_dot_ntp) },
    { MP_ROM_QSTR(MP_QSTR_query), MP_ROM_PTR(&ntp_query_obj) },
    { MP_ROM_QSTR(MP_QSTR_query_host), MP_ROM_PTR(&ntp_query_host_obj) },
};
static MP_DEFINE_CONST_DICT(ntp_globals, ntp_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_net_ntp = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&ntp_globals,
};
