/*
 * pymergetic.metal.net.wg — µPy face (callee: src/.../net/wg).
 * Firmware seats only.
 */
#include <string.h>

#include "py/obj.h"
#include "py/runtime.h"

#include "pymergetic/metal/net/wg/__init__.h"

static mp_obj_t net_wg_status(void)
{
    pm_metal_net_wg_status_t st;
    mp_obj_t dict;
    if (pm_metal_net_wg_status(&st) != 0) {
        return mp_const_none;
    }
    dict = mp_obj_new_dict(6);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_up), st.up ? mp_const_true : mp_const_false);
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_ip), mp_obj_new_str(st.ip, strlen(st.ip)));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_listen_port), MP_OBJ_NEW_SMALL_INT(st.listen_port));
    mp_obj_dict_store(dict, MP_OBJ_NEW_QSTR(MP_QSTR_last_handshake),
                      MP_OBJ_NEW_SMALL_INT((mp_int_t)st.last_handshake_sec));
    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_0(net_wg_status_obj, net_wg_status);

static mp_obj_t net_wg_up(size_t n_args, const mp_obj_t *args)
{
    const char *priv = mp_obj_str_get_str(args[0]);
    mp_int_t port = mp_obj_get_int(args[1]);
    const char *tip = mp_obj_str_get_str(args[2]);
    const char *mask = mp_obj_str_get_str(args[3]);
    (void)n_args;
    return MP_OBJ_NEW_SMALL_INT(pm_metal_net_wg_up(priv, (uint16_t)port, tip, mask));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(net_wg_up_obj, 4, 4, net_wg_up);

static mp_obj_t net_wg_peer_add(size_t n_args, const mp_obj_t *args)
{
    const char *pub = mp_obj_str_get_str(args[0]);
    const char *ep = mp_obj_str_get_str(args[1]);
    mp_int_t eport = mp_obj_get_int(args[2]);
    const char *aip = mp_obj_str_get_str(args[3]);
    const char *amask = mp_obj_str_get_str(args[4]);
    (void)n_args;
    return MP_OBJ_NEW_SMALL_INT(
        pm_metal_net_wg_peer_add(pub, ep, (uint16_t)eport, aip, amask));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(net_wg_peer_add_obj, 5, 5, net_wg_peer_add);

static const mp_rom_map_elem_t net_wg_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_net_dot_wg) },
    { MP_ROM_QSTR(MP_QSTR_status), MP_ROM_PTR(&net_wg_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_up), MP_ROM_PTR(&net_wg_up_obj) },
    { MP_ROM_QSTR(MP_QSTR_peer_add), MP_ROM_PTR(&net_wg_peer_add_obj) },
};
static MP_DEFINE_CONST_DICT(net_wg_globals, net_wg_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_net_wg = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&net_wg_globals,
};
