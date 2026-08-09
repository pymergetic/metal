/*
 * pymergetic.metal.mem.port — µPy face (callee: src/.../mem/port).
 * Query face (raw alloc pointers stay C/RS).
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/mem.h>
#include <pymergetic/metal/reg/seats.h>

static mp_obj_t mem_heap_bytes(void)
{
    return mp_obj_new_int_from_uint((mp_uint_t)pm_metal_mem_heap_bytes());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mem_heap_bytes_obj, mem_heap_bytes);

static mp_obj_t mem_free_bytes(void)
{
    return mp_obj_new_int_from_uint((mp_uint_t)pm_metal_mem_free_bytes());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mem_free_bytes_obj, mem_free_bytes);

static mp_obj_t mem_base(void)
{
    return mp_obj_new_int_from_ull((uint64_t)pm_metal_mem_base());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mem_base_obj, mem_base);

static mp_obj_t mem_span_bytes(void)
{
    return mp_obj_new_int_from_uint((mp_uint_t)pm_metal_mem_span_bytes());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mem_span_bytes_obj, mem_span_bytes);

static const mp_rom_map_elem_t mem_port_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_mem_dot_port) },
    { MP_ROM_QSTR(MP_QSTR_heap_bytes), MP_ROM_PTR(&mem_heap_bytes_obj) },
    { MP_ROM_QSTR(MP_QSTR_free_bytes), MP_ROM_PTR(&mem_free_bytes_obj) },
    { MP_ROM_QSTR(MP_QSTR_base), MP_ROM_PTR(&mem_base_obj) },
    { MP_ROM_QSTR(MP_QSTR_span_bytes), MP_ROM_PTR(&mem_span_bytes_obj) },
};
static MP_DEFINE_CONST_DICT(mem_port_globals, mem_port_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_mem_port = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mem_port_globals,
};

PM_METAL_REG_SEAT(g_pm_seat_mem_port, "pymergetic.metal.mem.port", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
