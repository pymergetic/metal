/*
 * pymergetic.metal.net.pump — µPy face (callee: src/.../net/pump).
 * Firmware seats only.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/net/pump/__init__.h>

static mp_obj_t pump_once(void)
{
    pm_metal_net_pump_once();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(pump_once_obj, pump_once);

static mp_obj_t pump_bind_async(void)
{
    pm_metal_net_pump_bind_async();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(pump_bind_async_obj, pump_bind_async);

static mp_obj_t pump_await_tcp_established_h(mp_obj_t sock_obj)
{
    return mp_obj_new_int_from_uint(
        pm_metal_net_await_tcp_established_h((pm_metal_net_ip_sock_h)mp_obj_get_int(sock_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(pump_await_tcp_established_h_obj, pump_await_tcp_established_h);

static mp_obj_t pump_await_tcp_rx_h(mp_obj_t sock_obj, mp_obj_t min_obj)
{
    return mp_obj_new_int_from_uint(pm_metal_net_await_tcp_rx_h(
        (pm_metal_net_ip_sock_h)mp_obj_get_int(sock_obj), (uint32_t)mp_obj_get_int(min_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(pump_await_tcp_rx_h_obj, pump_await_tcp_rx_h);

static mp_obj_t pump_await_tcp_established(mp_obj_t sock_obj, mp_obj_t iters_obj)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_await_tcp_established(
        (pm_metal_net_ip_sock_h)mp_obj_get_int(sock_obj), (uint32_t)mp_obj_get_int(iters_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(pump_await_tcp_established_obj, pump_await_tcp_established);

static mp_obj_t pump_await_tcp_rx(mp_obj_t sock_obj, mp_obj_t min_obj, mp_obj_t iters_obj)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_await_tcp_rx(
        (pm_metal_net_ip_sock_h)mp_obj_get_int(sock_obj), (uint32_t)mp_obj_get_int(min_obj),
        (uint32_t)mp_obj_get_int(iters_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_3(pump_await_tcp_rx_obj, pump_await_tcp_rx);

static mp_obj_t pump_wake_tcp(void)
{
    pm_metal_net_pump_wake_tcp();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(pump_wake_tcp_obj, pump_wake_tcp);

static const mp_rom_map_elem_t pump_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_net_dot_pump) },
    { MP_ROM_QSTR(MP_QSTR_once), MP_ROM_PTR(&pump_once_obj) },
    { MP_ROM_QSTR(MP_QSTR_bind_async), MP_ROM_PTR(&pump_bind_async_obj) },
    { MP_ROM_QSTR(MP_QSTR_await_tcp_established_h), MP_ROM_PTR(&pump_await_tcp_established_h_obj) },
    { MP_ROM_QSTR(MP_QSTR_await_tcp_rx_h), MP_ROM_PTR(&pump_await_tcp_rx_h_obj) },
    { MP_ROM_QSTR(MP_QSTR_await_tcp_established), MP_ROM_PTR(&pump_await_tcp_established_obj) },
    { MP_ROM_QSTR(MP_QSTR_await_tcp_rx), MP_ROM_PTR(&pump_await_tcp_rx_obj) },
    { MP_ROM_QSTR(MP_QSTR_wake_tcp), MP_ROM_PTR(&pump_wake_tcp_obj) },
};
static MP_DEFINE_CONST_DICT(pump_globals, pump_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_net_pump = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&pump_globals,
};
