/* pymergetic.metal.hwtree — µPy face (pointer-safe). */
#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include <pymergetic/metal/hwtree/__init__.h>
#include <pymergetic/metal/reg/seats.h>

static mp_obj_t hwtree_print(void) {
    return mp_obj_new_int((mp_int_t)pm_metal_hwtree_print());
}
static MP_DEFINE_CONST_FUN_OBJ_0(hwtree_print_obj, hwtree_print);

static const mp_rom_map_elem_t hwtree_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_hwtree) },
    { MP_ROM_QSTR(MP_QSTR_print), MP_ROM_PTR(&hwtree_print_obj) },
};
static MP_DEFINE_CONST_DICT(hwtree_globals, hwtree_globals_table);
const mp_obj_module_t mp_module_pymergetic_metal_hwtree = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&hwtree_globals,
};

PM_METAL_REG_SEAT(g_pm_seat_hwtree, "pymergetic.metal.hwtree", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
