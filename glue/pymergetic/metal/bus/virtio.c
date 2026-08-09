/*
 * pymergetic.metal.bus.virtio — µPy face.
 * Firmware seats only.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/bus/virtio.h>

static mp_obj_t virtio_virtio_pages_free(mp_obj_t buf_obj, mp_obj_t pages_obj)
{
    void *buf = (buf_obj == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(buf_obj);
    mp_int_t pages = mp_obj_get_int(pages_obj);
    pm_metal_virtio_pages_free(buf, (int32_t)pages);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(virtio_virtio_pages_free_obj, virtio_virtio_pages_free);

static mp_obj_t virtio_virtio_open(mp_obj_t pci_device_id_obj, mp_obj_t out_obj)
{
    mp_int_t pci_device_id = mp_obj_get_int(pci_device_id_obj);
    void *out = (out_obj == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(out_obj);
    return mp_obj_new_int((mp_int_t)pm_metal_virtio_open((uint16_t)pci_device_id, out));
}
static MP_DEFINE_CONST_FUN_OBJ_2(virtio_virtio_open_obj, virtio_virtio_open);

static mp_obj_t virtio_virtio_close(mp_obj_t dev_obj)
{
    void *dev = (dev_obj == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(dev_obj);
    pm_metal_virtio_close(dev);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(virtio_virtio_close_obj, virtio_virtio_close);

static mp_obj_t virtio_virtio_get_features(mp_obj_t dev_obj)
{
    void *dev = (dev_obj == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(dev_obj);
    return mp_obj_new_int((mp_int_t)pm_metal_virtio_get_features(dev));
}
static MP_DEFINE_CONST_FUN_OBJ_1(virtio_virtio_get_features_obj, virtio_virtio_get_features);

static mp_obj_t virtio_virtio_set_features(mp_obj_t dev_obj, mp_obj_t features_obj)
{
    void *dev = (dev_obj == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(dev_obj);
    mp_int_t features = mp_obj_get_int(features_obj);
    return mp_obj_new_int((mp_int_t)pm_metal_virtio_set_features(dev, (uint64_t)features));
}
static MP_DEFINE_CONST_FUN_OBJ_2(virtio_virtio_set_features_obj, virtio_virtio_set_features);

static mp_obj_t virtio_virtio_set_status(mp_obj_t dev_obj, mp_obj_t status_obj)
{
    void *dev = (dev_obj == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(dev_obj);
    mp_int_t status = mp_obj_get_int(status_obj);
    pm_metal_virtio_set_status(dev, (uint8_t)status);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(virtio_virtio_set_status_obj, virtio_virtio_set_status);

static mp_obj_t virtio_virtio_get_status(mp_obj_t dev_obj)
{
    void *dev = (dev_obj == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(dev_obj);
    return mp_obj_new_int((mp_int_t)pm_metal_virtio_get_status(dev));
}
static MP_DEFINE_CONST_FUN_OBJ_1(virtio_virtio_get_status_obj, virtio_virtio_get_status);

static mp_obj_t virtio_virtio_setup_queue(mp_obj_t dev_obj, mp_obj_t qidx_obj, mp_obj_t want_size_obj)
{
    void *dev = (dev_obj == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(dev_obj);
    mp_int_t qidx = mp_obj_get_int(qidx_obj);
    mp_int_t want_size = mp_obj_get_int(want_size_obj);
    return mp_obj_new_int((mp_int_t)pm_metal_virtio_setup_queue(dev, (uint16_t)qidx, (uint16_t)want_size));
}
static MP_DEFINE_CONST_FUN_OBJ_3(virtio_virtio_setup_queue_obj, virtio_virtio_setup_queue);

static mp_obj_t virtio_virtio_driver_ok(mp_obj_t dev_obj)
{
    void *dev = (dev_obj == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(dev_obj);
    return mp_obj_new_int((mp_int_t)pm_metal_virtio_driver_ok(dev));
}
static MP_DEFINE_CONST_FUN_OBJ_1(virtio_virtio_driver_ok_obj, virtio_virtio_driver_ok);

static mp_obj_t virtio_virtq_kick(mp_obj_t dev_obj, mp_obj_t vq_obj)
{
    void *dev = (dev_obj == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(dev_obj);
    void *vq = (vq_obj == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(vq_obj);
    pm_metal_virtq_kick(dev, vq);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(virtio_virtq_kick_obj, virtio_virtq_kick);

static mp_obj_t virtio_virtq_get_used(mp_obj_t vq_obj, mp_obj_t head_obj, mp_obj_t len_obj)
{
    void *vq = (vq_obj == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(vq_obj);
    void *head = (head_obj == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(head_obj);
    void *len = (len_obj == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(len_obj);
    return mp_obj_new_int((mp_int_t)pm_metal_virtq_get_used(vq, head, len));
}
static MP_DEFINE_CONST_FUN_OBJ_3(virtio_virtq_get_used_obj, virtio_virtq_get_used);

static mp_obj_t virtio_virtq_free_chain(mp_obj_t vq_obj, mp_obj_t head_obj)
{
    void *vq = (vq_obj == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(vq_obj);
    mp_int_t head = mp_obj_get_int(head_obj);
    pm_metal_virtq_free_chain(vq, (uint16_t)head);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(virtio_virtq_free_chain_obj, virtio_virtq_free_chain);


static mp_obj_t virtio_pages_alloc(mp_obj_t pages_obj)
{
    return mp_obj_new_int_from_ull((uintptr_t)pm_metal_virtio_pages_alloc((unsigned)mp_obj_get_int(pages_obj)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(virtio_pages_alloc_obj, virtio_pages_alloc);

static mp_obj_t virtio_cfg_read(size_t n_args, const mp_obj_t *args)
{
    mp_buffer_info_t buf;
    (void)n_args;
    mp_get_buffer_raise(args[2], &buf, MP_BUFFER_WRITE);
    return MP_OBJ_NEW_SMALL_INT(pm_metal_virtio_cfg_read(
        (pm_metal_virtio_dev_t *)(uintptr_t)mp_obj_get_int(args[0]), (uint32_t)mp_obj_get_int(args[1]),
        buf.buf, (uint32_t)buf.len));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(virtio_cfg_read_obj, 3, 3, virtio_cfg_read);

static mp_obj_t virtq_add(size_t n_args, const mp_obj_t *args)
{
    uint16_t head = 0;
    int rc;
    (void)n_args;
    rc = pm_metal_virtq_add((pm_metal_virtq_t *)(uintptr_t)mp_obj_get_int(args[0]),
                            (void *)(uintptr_t)mp_obj_get_int(args[1]), (uint32_t)mp_obj_get_int(args[2]),
                            (int)mp_obj_get_int(args[3]), &head);
    {
        mp_obj_t tup[2];
        tup[0] = MP_OBJ_NEW_SMALL_INT(rc);
        tup[1] = MP_OBJ_NEW_SMALL_INT(head);
        return mp_obj_new_tuple(2, tup);
    }
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(virtq_add_obj, 4, 4, virtq_add);

static const mp_rom_map_elem_t virtio_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_bus_dot_virtio) },
    { MP_ROM_QSTR(MP_QSTR_pages_alloc), MP_ROM_PTR(&virtio_pages_alloc_obj) },
    { MP_ROM_QSTR(MP_QSTR_virtio_pages_free), MP_ROM_PTR(&virtio_virtio_pages_free_obj) },
    { MP_ROM_QSTR(MP_QSTR_virtio_open), MP_ROM_PTR(&virtio_virtio_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_virtio_close), MP_ROM_PTR(&virtio_virtio_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_virtio_get_features), MP_ROM_PTR(&virtio_virtio_get_features_obj) },
    { MP_ROM_QSTR(MP_QSTR_virtio_set_features), MP_ROM_PTR(&virtio_virtio_set_features_obj) },
    { MP_ROM_QSTR(MP_QSTR_virtio_set_status), MP_ROM_PTR(&virtio_virtio_set_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_virtio_get_status), MP_ROM_PTR(&virtio_virtio_get_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_virtio_setup_queue), MP_ROM_PTR(&virtio_virtio_setup_queue_obj) },
    { MP_ROM_QSTR(MP_QSTR_virtio_driver_ok), MP_ROM_PTR(&virtio_virtio_driver_ok_obj) },
    { MP_ROM_QSTR(MP_QSTR_cfg_read), MP_ROM_PTR(&virtio_cfg_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_virtq_add), MP_ROM_PTR(&virtq_add_obj) },
    { MP_ROM_QSTR(MP_QSTR_virtq_kick), MP_ROM_PTR(&virtio_virtq_kick_obj) },
    { MP_ROM_QSTR(MP_QSTR_virtq_get_used), MP_ROM_PTR(&virtio_virtq_get_used_obj) },
    { MP_ROM_QSTR(MP_QSTR_virtq_free_chain), MP_ROM_PTR(&virtio_virtq_free_chain_obj) },
};
static MP_DEFINE_CONST_DICT(virtio_globals, virtio_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_bus_virtio = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&virtio_globals,
};
