/*
 * pymergetic.metal.bus.pci — µPy face.
 * Firmware seats only.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/bus/pci.h>
#include <pymergetic/metal/reg/seats.h>

static mp_obj_t pci_read32(size_t n_args, const mp_obj_t *args)
{
    (void)n_args;
    return mp_obj_new_int_from_uint(pm_metal_bus_pci_read32(
        (uint8_t)mp_obj_get_int(args[0]), (uint8_t)mp_obj_get_int(args[1]),
        (uint8_t)mp_obj_get_int(args[2]), (uint8_t)mp_obj_get_int(args[3])));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pci_read32_obj, 4, 4, pci_read32);

static mp_obj_t pci_read16(size_t n_args, const mp_obj_t *args)
{
    (void)n_args;
    return MP_OBJ_NEW_SMALL_INT(pm_metal_bus_pci_read16(
        (uint8_t)mp_obj_get_int(args[0]), (uint8_t)mp_obj_get_int(args[1]),
        (uint8_t)mp_obj_get_int(args[2]), (uint8_t)mp_obj_get_int(args[3])));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pci_read16_obj, 4, 4, pci_read16);

static mp_obj_t pci_read8(size_t n_args, const mp_obj_t *args)
{
    (void)n_args;
    return MP_OBJ_NEW_SMALL_INT(pm_metal_bus_pci_read8(
        (uint8_t)mp_obj_get_int(args[0]), (uint8_t)mp_obj_get_int(args[1]),
        (uint8_t)mp_obj_get_int(args[2]), (uint8_t)mp_obj_get_int(args[3])));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pci_read8_obj, 4, 4, pci_read8);

static mp_obj_t pci_write16(size_t n_args, const mp_obj_t *args)
{
    (void)n_args;
    pm_metal_bus_pci_write16((uint8_t)mp_obj_get_int(args[0]), (uint8_t)mp_obj_get_int(args[1]),
                             (uint8_t)mp_obj_get_int(args[2]), (uint8_t)mp_obj_get_int(args[3]),
                             (uint16_t)mp_obj_get_int(args[4]));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pci_write16_obj, 5, 5, pci_write16);

static mp_obj_t pci_write32(size_t n_args, const mp_obj_t *args)
{
    (void)n_args;
    pm_metal_bus_pci_write32((uint8_t)mp_obj_get_int(args[0]), (uint8_t)mp_obj_get_int(args[1]),
                             (uint8_t)mp_obj_get_int(args[2]), (uint8_t)mp_obj_get_int(args[3]),
                             (uint32_t)mp_obj_get_int(args[4]));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pci_write32_obj, 5, 5, pci_write32);

static mp_obj_t pci_enable_mem_bm(mp_obj_t bus_obj, mp_obj_t dev_obj, mp_obj_t func_obj)
{
    pm_metal_bus_pci_enable_mem_bm((uint8_t)mp_obj_get_int(bus_obj), (uint8_t)mp_obj_get_int(dev_obj),
                                   (uint8_t)mp_obj_get_int(func_obj));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(pci_enable_mem_bm_obj, pci_enable_mem_bm);

static mp_obj_t pci_bar_mmio(size_t n_args, const mp_obj_t *args)
{
    uint8_t consumed = 0;
    uint64_t bar;
    (void)n_args;
    bar = pm_metal_bus_pci_bar_mmio((uint8_t)mp_obj_get_int(args[0]), (uint8_t)mp_obj_get_int(args[1]),
                                    (uint8_t)mp_obj_get_int(args[2]), (uint8_t)mp_obj_get_int(args[3]),
                                    &consumed);
    {
        mp_obj_t tup[2];
        tup[0] = mp_obj_new_int_from_ull(bar);
        tup[1] = MP_OBJ_NEW_SMALL_INT(consumed);
        return mp_obj_new_tuple(2, tup);
    }
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(pci_bar_mmio_obj, 4, 4, pci_bar_mmio);

static mp_obj_t pci_find(mp_obj_t vendor_obj, mp_obj_t device_obj)
{
    uint8_t bus = 0, dev = 0, func = 0;
    int rc = pm_metal_bus_pci_find((uint16_t)mp_obj_get_int(vendor_obj),
                                   (uint16_t)mp_obj_get_int(device_obj), &bus, &dev, &func);
    {
        mp_obj_t tup[4];
        tup[0] = MP_OBJ_NEW_SMALL_INT(rc);
        tup[1] = MP_OBJ_NEW_SMALL_INT(bus);
        tup[2] = MP_OBJ_NEW_SMALL_INT(dev);
        tup[3] = MP_OBJ_NEW_SMALL_INT(func);
        return mp_obj_new_tuple(4, tup);
    }
}
static MP_DEFINE_CONST_FUN_OBJ_2(pci_find_obj, pci_find);

static const mp_rom_map_elem_t pci_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_bus_dot_pci) },
    { MP_ROM_QSTR(MP_QSTR_read32), MP_ROM_PTR(&pci_read32_obj) },
    { MP_ROM_QSTR(MP_QSTR_read16), MP_ROM_PTR(&pci_read16_obj) },
    { MP_ROM_QSTR(MP_QSTR_read8), MP_ROM_PTR(&pci_read8_obj) },
    { MP_ROM_QSTR(MP_QSTR_write16), MP_ROM_PTR(&pci_write16_obj) },
    { MP_ROM_QSTR(MP_QSTR_write32), MP_ROM_PTR(&pci_write32_obj) },
    { MP_ROM_QSTR(MP_QSTR_enable_mem_bm), MP_ROM_PTR(&pci_enable_mem_bm_obj) },
    { MP_ROM_QSTR(MP_QSTR_bar_mmio), MP_ROM_PTR(&pci_bar_mmio_obj) },
    { MP_ROM_QSTR(MP_QSTR_find), MP_ROM_PTR(&pci_find_obj) },
};
static MP_DEFINE_CONST_DICT(pci_globals, pci_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_bus_pci = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&pci_globals,
};

PM_METAL_REG_SEAT(g_pm_seat_bus_pci, "pymergetic.metal.bus.pci", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
