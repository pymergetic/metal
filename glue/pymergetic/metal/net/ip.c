/*
 * pymergetic.metal.net.ip — µPy face (callee: src/.../net/ip).
 * Firmware seats only.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/net/ip/__init__.h>
#include <pymergetic/metal/reg/seats.h>

static mp_obj_t net_ip_init(mp_obj_t addr_obj, mp_obj_t mask_obj, mp_obj_t gw_obj)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_ip_init((uint32_t)mp_obj_get_int(addr_obj),
                                                     (uint32_t)mp_obj_get_int(mask_obj),
                                                     (uint32_t)mp_obj_get_int(gw_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_3(net_ip_init_obj, net_ip_init);

static mp_obj_t net_ip_ready(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_ip_ready());
}
static MP_DEFINE_CONST_FUN_OBJ_0(net_ip_ready_obj, net_ip_ready);

static mp_obj_t net_ip_set_addrs(mp_obj_t addr_obj, mp_obj_t mask_obj, mp_obj_t gw_obj)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_ip_set_addrs((uint32_t)mp_obj_get_int(addr_obj),
                                                          (uint32_t)mp_obj_get_int(mask_obj),
                                                          (uint32_t)mp_obj_get_int(gw_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_3(net_ip_set_addrs_obj, net_ip_set_addrs);

static mp_obj_t net_ip_set_dns(mp_obj_t dns_obj)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_ip_set_dns((uint32_t)mp_obj_get_int(dns_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(net_ip_set_dns_obj, net_ip_set_dns);

static mp_obj_t net_ip_addr(void)
{
    return mp_obj_new_int_from_uint(pm_metal_net_ip_addr());
}
static MP_DEFINE_CONST_FUN_OBJ_0(net_ip_addr_obj, net_ip_addr);

static mp_obj_t net_ip_gw(void)
{
    return mp_obj_new_int_from_uint(pm_metal_net_ip_gw());
}
static MP_DEFINE_CONST_FUN_OBJ_0(net_ip_gw_obj, net_ip_gw);

static mp_obj_t net_ip_mask(void)
{
    return mp_obj_new_int_from_uint(pm_metal_net_ip_mask());
}
static MP_DEFINE_CONST_FUN_OBJ_0(net_ip_mask_obj, net_ip_mask);

static mp_obj_t net_ip_dns(void)
{
    return mp_obj_new_int_from_uint(pm_metal_net_ip_dns());
}
static MP_DEFINE_CONST_FUN_OBJ_0(net_ip_dns_obj, net_ip_dns);

static mp_obj_t net_ip_arp_resolve(mp_obj_t ip_obj)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_ip_arp_resolve((uint32_t)mp_obj_get_int(ip_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(net_ip_arp_resolve_obj, net_ip_arp_resolve);

static mp_obj_t net_ip_announce(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_ip_announce());
}
static MP_DEFINE_CONST_FUN_OBJ_0(net_ip_announce_obj, net_ip_announce);

static mp_obj_t net_ip_poll(void)
{
    pm_metal_net_ip_poll();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(net_ip_poll_obj, net_ip_poll);

static mp_obj_t net_ip_ping(mp_obj_t dst_obj, mp_obj_t id_obj, mp_obj_t seq_obj)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_ip_ping((uint32_t)mp_obj_get_int(dst_obj),
                                                     (uint16_t)mp_obj_get_int(id_obj),
                                                     (uint16_t)mp_obj_get_int(seq_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_3(net_ip_ping_obj, net_ip_ping);

static mp_obj_t net_ip_ping_replies(void)
{
    return mp_obj_new_int_from_uint(pm_metal_net_ip_ping_replies());
}
static MP_DEFINE_CONST_FUN_OBJ_0(net_ip_ping_replies_obj, net_ip_ping_replies);

static const mp_rom_map_elem_t net_ip_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_net_dot_ip) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&net_ip_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_ready), MP_ROM_PTR(&net_ip_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_addrs), MP_ROM_PTR(&net_ip_set_addrs_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_dns), MP_ROM_PTR(&net_ip_set_dns_obj) },
    { MP_ROM_QSTR(MP_QSTR_addr), MP_ROM_PTR(&net_ip_addr_obj) },
    { MP_ROM_QSTR(MP_QSTR_gw), MP_ROM_PTR(&net_ip_gw_obj) },
    { MP_ROM_QSTR(MP_QSTR_mask), MP_ROM_PTR(&net_ip_mask_obj) },
    { MP_ROM_QSTR(MP_QSTR_dns), MP_ROM_PTR(&net_ip_dns_obj) },
    { MP_ROM_QSTR(MP_QSTR_arp_resolve), MP_ROM_PTR(&net_ip_arp_resolve_obj) },
    { MP_ROM_QSTR(MP_QSTR_announce), MP_ROM_PTR(&net_ip_announce_obj) },
    { MP_ROM_QSTR(MP_QSTR_poll), MP_ROM_PTR(&net_ip_poll_obj) },
    { MP_ROM_QSTR(MP_QSTR_ping), MP_ROM_PTR(&net_ip_ping_obj) },
    { MP_ROM_QSTR(MP_QSTR_ping_replies), MP_ROM_PTR(&net_ip_ping_replies_obj) },
};
static MP_DEFINE_CONST_DICT(net_ip_globals, net_ip_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_net_ip = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&net_ip_globals,
};

PM_METAL_REG_SEAT(g_pm_seat_net_ip, "pymergetic.metal.net.ip", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
