/*
 * pymergetic.metal.dev.net.bge — µPy face (callee: bge_netif).
 */
#include <string.h>

#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/dev/net/bge/bge_netif.h>

static mp_obj_t bge_detect(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_bge_netif_detect());
}
static MP_DEFINE_CONST_FUN_OBJ_0(bge_detect_obj, bge_detect);

static mp_obj_t bge_open(void)
{
    uint8_t mac[6];
    int rc = pm_metal_bge_netif_open(mac);
    mp_obj_t items[2];
    items[0] = MP_OBJ_NEW_SMALL_INT(rc);
    items[1] = (rc == 0) ? mp_obj_new_bytes(mac, 6) : mp_const_none;
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(bge_open_obj, bge_open);

static mp_obj_t bge_ready(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_bge_netif_ready());
}
static MP_DEFINE_CONST_FUN_OBJ_0(bge_ready_obj, bge_ready);

static mp_obj_t bge_mac(void)
{
    const uint8_t *m = pm_metal_bge_netif_mac();
    if (m == NULL) {
        return mp_const_none;
    }
    return mp_obj_new_bytes(m, 6);
}
static MP_DEFINE_CONST_FUN_OBJ_0(bge_mac_obj, bge_mac);

static const mp_rom_map_elem_t bge_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),
      MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_net_dot_bge) },
    { MP_ROM_QSTR(MP_QSTR_detect), MP_ROM_PTR(&bge_detect_obj) },
    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&bge_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_ready), MP_ROM_PTR(&bge_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_mac), MP_ROM_PTR(&bge_mac_obj) },
};
static MP_DEFINE_CONST_DICT(bge_globals, bge_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_dev_net_bge = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&bge_globals,
};
