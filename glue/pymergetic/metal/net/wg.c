/*
 * pymergetic.metal.net.wg — µPy face (callee: src/.../net/wg).
 * Firmware seats only.
 */
#include <string.h>

#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/net/wg/__init__.h>
#include <pymergetic/metal/reg/seats.h>

static mp_obj_t wg_status_to_dict(const pm_metal_net_wg_status_t *st)
{
    mp_obj_t dict = mp_obj_new_dict(8);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_up), st->up ? mp_const_true : mp_const_false);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_name),
                      mp_obj_new_str(st->name, strlen(st->name)));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_ip), mp_obj_new_str(st->ip, strlen(st->ip)));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_peer_public),
                      mp_obj_new_str(st->peer_public, strlen(st->peer_public)));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_rx_bytes),
                      mp_obj_new_int_from_ull(st->rx_bytes));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_tx_bytes),
                      mp_obj_new_int_from_ull(st->tx_bytes));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_last_handshake),
                      mp_obj_new_int_from_uint(st->last_handshake_sec));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_listen_port),
                      MP_OBJ_NEW_SMALL_INT(st->listen_port));
    return dict;
}

static mp_obj_t net_wg_up_named(size_t n_args, const mp_obj_t *args)
{
    (void)n_args;
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_wg_up_named(
        mp_obj_str_get_str(args[0]), mp_obj_str_get_str(args[1]), (uint16_t)mp_obj_get_int(args[2]),
        mp_obj_str_get_str(args[3]), mp_obj_str_get_str(args[4])));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(net_wg_up_named_obj, 5, 5, net_wg_up_named);

static mp_obj_t net_wg_down_named(mp_obj_t ifname_obj)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_wg_down_named(mp_obj_str_get_str(ifname_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(net_wg_down_named_obj, net_wg_down_named);

static mp_obj_t net_wg_peer_add_named(size_t n_args, const mp_obj_t *args)
{
    (void)n_args;
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_wg_peer_add_named(
        mp_obj_str_get_str(args[0]), mp_obj_str_get_str(args[1]), mp_obj_str_get_str(args[2]),
        (uint16_t)mp_obj_get_int(args[3]), mp_obj_str_get_str(args[4]),
        mp_obj_str_get_str(args[5])));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(net_wg_peer_add_named_obj, 6, 6, net_wg_peer_add_named);

static mp_obj_t net_wg_peer_del_named(mp_obj_t ifname_obj, mp_obj_t pub_obj)
{
    return MP_OBJ_NEW_SMALL_INT(
        pm_metal_net_wg_peer_del_named(mp_obj_str_get_str(ifname_obj), mp_obj_str_get_str(pub_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(net_wg_peer_del_named_obj, net_wg_peer_del_named);

static mp_obj_t net_wg_status_named(mp_obj_t ifname_obj)
{
    pm_metal_net_wg_status_t st;
    if (pm_metal_net_wg_status_named(mp_obj_str_get_str(ifname_obj), &st) != 0) {
        return mp_const_none;
    }
    return wg_status_to_dict(&st);
}
static MP_DEFINE_CONST_FUN_OBJ_1(net_wg_status_named_obj, net_wg_status_named);

static mp_obj_t net_wg_up(size_t n_args, const mp_obj_t *args)
{
    (void)n_args;
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_wg_up(mp_obj_str_get_str(args[0]),
                                                   (uint16_t)mp_obj_get_int(args[1]),
                                                   mp_obj_str_get_str(args[2]),
                                                   mp_obj_str_get_str(args[3])));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(net_wg_up_obj, 4, 4, net_wg_up);

static mp_obj_t net_wg_down(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_wg_down());
}
static MP_DEFINE_CONST_FUN_OBJ_0(net_wg_down_obj, net_wg_down);

static mp_obj_t net_wg_peer_add(size_t n_args, const mp_obj_t *args)
{
    (void)n_args;
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_wg_peer_add(
        mp_obj_str_get_str(args[0]), mp_obj_str_get_str(args[1]), (uint16_t)mp_obj_get_int(args[2]),
        mp_obj_str_get_str(args[3]), mp_obj_str_get_str(args[4])));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(net_wg_peer_add_obj, 5, 5, net_wg_peer_add);

static mp_obj_t net_wg_peer_del(mp_obj_t pub_obj)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_wg_peer_del(mp_obj_str_get_str(pub_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(net_wg_peer_del_obj, net_wg_peer_del);

static mp_obj_t net_wg_status(void)
{
    pm_metal_net_wg_status_t st;
    if (pm_metal_net_wg_status(&st) != 0) {
        return mp_const_none;
    }
    return wg_status_to_dict(&st);
}
static MP_DEFINE_CONST_FUN_OBJ_0(net_wg_status_obj, net_wg_status);

static mp_obj_t net_wg_ready(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_wg_ready());
}
static MP_DEFINE_CONST_FUN_OBJ_0(net_wg_ready_obj, net_wg_ready);

static mp_obj_t net_wg_handshake_smoke(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_wg_handshake_smoke());
}
static MP_DEFINE_CONST_FUN_OBJ_0(net_wg_handshake_smoke_obj, net_wg_handshake_smoke);

static const mp_rom_map_elem_t net_wg_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_net_dot_wg) },
    { MP_ROM_QSTR(MP_QSTR_up_named), MP_ROM_PTR(&net_wg_up_named_obj) },
    { MP_ROM_QSTR(MP_QSTR_down_named), MP_ROM_PTR(&net_wg_down_named_obj) },
    { MP_ROM_QSTR(MP_QSTR_peer_add_named), MP_ROM_PTR(&net_wg_peer_add_named_obj) },
    { MP_ROM_QSTR(MP_QSTR_peer_del_named), MP_ROM_PTR(&net_wg_peer_del_named_obj) },
    { MP_ROM_QSTR(MP_QSTR_status_named), MP_ROM_PTR(&net_wg_status_named_obj) },
    { MP_ROM_QSTR(MP_QSTR_up), MP_ROM_PTR(&net_wg_up_obj) },
    { MP_ROM_QSTR(MP_QSTR_down), MP_ROM_PTR(&net_wg_down_obj) },
    { MP_ROM_QSTR(MP_QSTR_peer_add), MP_ROM_PTR(&net_wg_peer_add_obj) },
    { MP_ROM_QSTR(MP_QSTR_peer_del), MP_ROM_PTR(&net_wg_peer_del_obj) },
    { MP_ROM_QSTR(MP_QSTR_status), MP_ROM_PTR(&net_wg_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_ready), MP_ROM_PTR(&net_wg_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_handshake_smoke), MP_ROM_PTR(&net_wg_handshake_smoke_obj) },
};
static MP_DEFINE_CONST_DICT(net_wg_globals, net_wg_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_net_wg = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&net_wg_globals,
};

PM_METAL_REG_SEAT(g_pm_seat_net_wg, "pymergetic.metal.net.wg", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
