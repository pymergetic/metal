/*
 * pymergetic.metal.net.dns — µPy face (callee: src/.../net/dns).
 * Firmware seats only.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/net/dns/__init__.h>
#include <pymergetic/metal/reg/seats.h>

static mp_obj_t dns_lookup(mp_obj_t name_obj)
{
    return mp_obj_new_int_from_uint(pm_metal_net_dns_lookup(mp_obj_str_get_str(name_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(dns_lookup_obj, dns_lookup);

static mp_obj_t dns_last_addr(void)
{
    return mp_obj_new_int_from_uint(pm_metal_net_dns_last_addr());
}
static MP_DEFINE_CONST_FUN_OBJ_0(dns_last_addr_obj, dns_last_addr);

static mp_obj_t dns_resolve(mp_obj_t name_obj)
{
    uint32_t addr = 0;
    int32_t rc = pm_metal_net_dns_resolve(mp_obj_str_get_str(name_obj), &addr);
    mp_obj_t items[2];

    items[0] = MP_OBJ_NEW_SMALL_INT(rc);
    items[1] = mp_obj_new_int_from_uint(addr);
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(dns_resolve_obj, dns_resolve);

static const mp_rom_map_elem_t dns_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_net_dot_dns) },
    { MP_ROM_QSTR(MP_QSTR_lookup), MP_ROM_PTR(&dns_lookup_obj) },
    { MP_ROM_QSTR(MP_QSTR_last_addr), MP_ROM_PTR(&dns_last_addr_obj) },
    { MP_ROM_QSTR(MP_QSTR_resolve), MP_ROM_PTR(&dns_resolve_obj) },
};
static MP_DEFINE_CONST_DICT(dns_globals, dns_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_net_dns = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&dns_globals,
};

PM_METAL_REG_SEAT(g_pm_seat_net_dns, "pymergetic.metal.net.dns", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
