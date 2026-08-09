/*
 * pymergetic.metal.dev.net.virtio_net — µPy face.
 */
#include <string.h>

#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/dev/net.h>

static mp_obj_t vnet_probe(void)
{
    uint8_t mac[6];
    int rc = pm_metal_dev_net_virtio_probe(mac);
    mp_obj_t items[2];
    items[0] = MP_OBJ_NEW_SMALL_INT(rc);
    items[1] = (rc == 0) ? mp_obj_new_bytes(mac, 6) : mp_const_none;
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(vnet_probe_obj, vnet_probe);

static mp_obj_t vnet_open(void)
{
    uint8_t mac[6];
    int rc = pm_metal_dev_net_virtio_open(mac);
    mp_obj_t items[2];
    items[0] = MP_OBJ_NEW_SMALL_INT(rc);
    items[1] = (rc == 0) ? mp_obj_new_bytes(mac, 6) : mp_const_none;
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(vnet_open_obj, vnet_open);

static mp_obj_t vnet_ready(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_dev_net_virtio_ready());
}
static MP_DEFINE_CONST_FUN_OBJ_0(vnet_ready_obj, vnet_ready);

static mp_obj_t vnet_mac(void)
{
    const uint8_t *m = pm_metal_dev_net_virtio_mac();
    if (m == NULL) {
        return mp_const_none;
    }
    return mp_obj_new_bytes(m, 6);
}
static MP_DEFINE_CONST_FUN_OBJ_0(vnet_mac_obj, vnet_mac);

static mp_obj_t vnet_tx(mp_obj_t frame_obj)
{
    mp_buffer_info_t frame;
    mp_get_buffer_raise(frame_obj, &frame, MP_BUFFER_READ);
    return MP_OBJ_NEW_SMALL_INT(pm_metal_dev_net_virtio_tx(frame.buf, (uint32_t)frame.len));
}
static MP_DEFINE_CONST_FUN_OBJ_1(vnet_tx_obj, vnet_tx);

static mp_obj_t vnet_reap_tx(void)
{
    return MP_OBJ_NEW_SMALL_INT(pm_metal_dev_net_virtio_reap_tx());
}
static MP_DEFINE_CONST_FUN_OBJ_0(vnet_reap_tx_obj, vnet_reap_tx);

static const mp_rom_map_elem_t vnet_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),
      MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_net_dot_virtio_net) },
    { MP_ROM_QSTR(MP_QSTR_probe), MP_ROM_PTR(&vnet_probe_obj) },
    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&vnet_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_ready), MP_ROM_PTR(&vnet_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_mac), MP_ROM_PTR(&vnet_mac_obj) },
    { MP_ROM_QSTR(MP_QSTR_tx), MP_ROM_PTR(&vnet_tx_obj) },
    { MP_ROM_QSTR(MP_QSTR_reap_tx), MP_ROM_PTR(&vnet_reap_tx_obj) },
};
static MP_DEFINE_CONST_DICT(vnet_globals, vnet_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_dev_net_virtio_net = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&vnet_globals,
};
