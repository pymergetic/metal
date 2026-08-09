/*
 * pymergetic.metal.dev.acpi — µPy face.
 * Firmware seats only.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/dev/acpi/__init__.h>

static mp_obj_t acpi_set_rsdp(mp_obj_t addr_obj)
{
    mp_int_t addr = mp_obj_get_int(addr_obj);
    pm_metal_dev_acpi_set_rsdp((uint64_t)addr);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(acpi_set_rsdp_obj, acpi_set_rsdp);

static mp_obj_t acpi_rsdp(void)
{
    return mp_obj_new_int((mp_int_t)pm_metal_dev_acpi_rsdp());
}
static MP_DEFINE_CONST_FUN_OBJ_0(acpi_rsdp_obj, acpi_rsdp);

static mp_obj_t acpi_init(void)
{
    return mp_obj_new_int((mp_int_t)pm_metal_dev_acpi_init());
}
static MP_DEFINE_CONST_FUN_OBJ_0(acpi_init_obj, acpi_init);

static mp_obj_t acpi_cpu_count(void)
{
    return mp_obj_new_int((mp_int_t)pm_metal_dev_acpi_cpu_count());
}
static MP_DEFINE_CONST_FUN_OBJ_0(acpi_cpu_count_obj, acpi_cpu_count);

static mp_obj_t acpi_apic_id(mp_obj_t cpu_index_obj)
{
    mp_int_t cpu_index = mp_obj_get_int(cpu_index_obj);
    return mp_obj_new_int((mp_int_t)pm_metal_dev_acpi_apic_id((uint32_t)cpu_index));
}
static MP_DEFINE_CONST_FUN_OBJ_1(acpi_apic_id_obj, acpi_apic_id);

static mp_obj_t acpi_lapic_base(void)
{
    return mp_obj_new_int((mp_int_t)pm_metal_dev_acpi_lapic_base());
}
static MP_DEFINE_CONST_FUN_OBJ_0(acpi_lapic_base_obj, acpi_lapic_base);

static const mp_rom_map_elem_t acpi_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_dev_dot_acpi) },
    { MP_ROM_QSTR(MP_QSTR_set_rsdp), MP_ROM_PTR(&acpi_set_rsdp_obj) },
    { MP_ROM_QSTR(MP_QSTR_rsdp), MP_ROM_PTR(&acpi_rsdp_obj) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&acpi_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_cpu_count), MP_ROM_PTR(&acpi_cpu_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_apic_id), MP_ROM_PTR(&acpi_apic_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_lapic_base), MP_ROM_PTR(&acpi_lapic_base_obj) },
};
static MP_DEFINE_CONST_DICT(acpi_globals, acpi_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_dev_acpi = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&acpi_globals,
};
