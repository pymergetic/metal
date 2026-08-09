/* pymergetic.metal.mem.lock — µPy face (pointer-safe). */
#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include <pymergetic/metal/mem/lock/__init__.h>
#include <pymergetic/metal/reg/seats.h>

static mp_obj_t lock_mutex_init(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_m_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    pm_metal_mem_lock_mutex_t * m = (pm_metal_mem_lock_mutex_t *)_m_v;
    pm_metal_mem_lock_mutex_init(m); return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(lock_mutex_init_obj, 1, 1, lock_mutex_init);

static mp_obj_t lock_mutex_lock(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_m_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    const pm_metal_mem_lock_mutex_t * m = (pm_metal_mem_lock_mutex_t *)_m_v;
    pm_metal_mem_lock_mutex_lock(m); return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(lock_mutex_lock_obj, 1, 1, lock_mutex_lock);

static mp_obj_t lock_mutex_try_lock(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_m_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    const pm_metal_mem_lock_mutex_t * m = (pm_metal_mem_lock_mutex_t *)_m_v;
    return mp_obj_new_int((mp_int_t)pm_metal_mem_lock_mutex_try_lock(m));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(lock_mutex_try_lock_obj, 1, 1, lock_mutex_try_lock);

static mp_obj_t lock_mutex_unlock(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_m_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    const pm_metal_mem_lock_mutex_t * m = (pm_metal_mem_lock_mutex_t *)_m_v;
    pm_metal_mem_lock_mutex_unlock(m); return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(lock_mutex_unlock_obj, 1, 1, lock_mutex_unlock);

static mp_obj_t lock_spin_init(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_s_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    pm_metal_mem_lock_spin_t * s = (pm_metal_mem_lock_spin_t *)_s_v;
    pm_metal_mem_lock_spin_init(s); return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(lock_spin_init_obj, 1, 1, lock_spin_init);

static mp_obj_t lock_spin_lock(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_s_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    const pm_metal_mem_lock_spin_t * s = (pm_metal_mem_lock_spin_t *)_s_v;
    pm_metal_mem_lock_spin_lock(s); return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(lock_spin_lock_obj, 1, 1, lock_spin_lock);

static mp_obj_t lock_spin_try_lock(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_s_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    const pm_metal_mem_lock_spin_t * s = (pm_metal_mem_lock_spin_t *)_s_v;
    return mp_obj_new_int((mp_int_t)pm_metal_mem_lock_spin_try_lock(s));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(lock_spin_try_lock_obj, 1, 1, lock_spin_try_lock);

static mp_obj_t lock_spin_unlock(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_s_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    const pm_metal_mem_lock_spin_t * s = (pm_metal_mem_lock_spin_t *)_s_v;
    pm_metal_mem_lock_spin_unlock(s); return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(lock_spin_unlock_obj, 1, 1, lock_spin_unlock);

static const mp_rom_map_elem_t lock_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_mem_dot_lock) },
    { MP_ROM_QSTR(MP_QSTR_mutex_init), MP_ROM_PTR(&lock_mutex_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_mutex_lock), MP_ROM_PTR(&lock_mutex_lock_obj) },
    { MP_ROM_QSTR(MP_QSTR_mutex_try_lock), MP_ROM_PTR(&lock_mutex_try_lock_obj) },
    { MP_ROM_QSTR(MP_QSTR_mutex_unlock), MP_ROM_PTR(&lock_mutex_unlock_obj) },
    { MP_ROM_QSTR(MP_QSTR_spin_init), MP_ROM_PTR(&lock_spin_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_spin_lock), MP_ROM_PTR(&lock_spin_lock_obj) },
    { MP_ROM_QSTR(MP_QSTR_spin_try_lock), MP_ROM_PTR(&lock_spin_try_lock_obj) },
    { MP_ROM_QSTR(MP_QSTR_spin_unlock), MP_ROM_PTR(&lock_spin_unlock_obj) },
};
static MP_DEFINE_CONST_DICT(lock_globals, lock_globals_table);
const mp_obj_module_t mp_module_pymergetic_metal_mem_lock = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&lock_globals,
};

PM_METAL_REG_SEAT(g_pm_seat_mem_lock, "pymergetic.metal.mem.lock", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
