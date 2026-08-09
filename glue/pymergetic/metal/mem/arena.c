/* pymergetic.metal.mem.arena — µPy face (pointer-safe). */
#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"

#include <pymergetic/metal/mem/arena/__init__.h>
static mp_obj_t arena_empty(void) { return MP_OBJ_NEW_SMALL_INT(-1); }
static MP_DEFINE_CONST_FUN_OBJ_0(arena_empty_obj, arena_empty);

static mp_obj_t arena_init(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_a_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    pm_metal_mem_arena_t * a = (pm_metal_mem_arena_t *)_a_v;
    mp_buffer_info_t base_bi;
    const uint8_t *base;
    if (args[1] == mp_const_none) { base = NULL; }
    else if (mp_obj_is_str_or_bytes(args[1])) {
        size_t _base_n; const char *_base_s = mp_obj_str_get_data(args[1], &_base_n); base=(const uint8_t*)_base_s;
    } else {
        mp_get_buffer_raise(args[1], &base_bi, MP_BUFFER_READ); base=(const uint8_t*)base_bi.buf;
    }
    size_t bytes = (size_t)mp_obj_get_int(args[2]);
    return mp_obj_new_int((mp_int_t)pm_metal_mem_arena_init(a, (uint8_t *)(uintptr_t)base, bytes));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(arena_init_obj, 3, 3, arena_init);

static mp_obj_t arena_ready(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_a_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    const pm_metal_mem_arena_t * a = (pm_metal_mem_arena_t *)_a_v;
    return mp_obj_new_int((mp_int_t)pm_metal_mem_arena_ready(a));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(arena_ready_obj, 1, 1, arena_ready);

static mp_obj_t arena_bytes(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_a_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    const pm_metal_mem_arena_t * a = (pm_metal_mem_arena_t *)_a_v;
    return mp_obj_new_int((mp_int_t)pm_metal_mem_arena_bytes(a));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(arena_bytes_obj, 1, 1, arena_bytes);

static mp_obj_t arena_map_used(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_a_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    const pm_metal_mem_arena_t * a = (pm_metal_mem_arena_t *)_a_v;
    return mp_obj_new_int((mp_int_t)pm_metal_mem_arena_map_used(a));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(arena_map_used_obj, 1, 1, arena_map_used);

static mp_obj_t arena_heap_used(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_a_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    const pm_metal_mem_arena_t * a = (pm_metal_mem_arena_t *)_a_v;
    return mp_obj_new_int((mp_int_t)pm_metal_mem_arena_heap_used(a));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(arena_heap_used_obj, 1, 1, arena_heap_used);

static mp_obj_t arena_hole(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_a_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    const pm_metal_mem_arena_t * a = (pm_metal_mem_arena_t *)_a_v;
    return mp_obj_new_int((mp_int_t)pm_metal_mem_arena_hole(a));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(arena_hole_obj, 1, 1, arena_hole);

static mp_obj_t arena_heap_grow(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_a_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    pm_metal_mem_arena_t * a = (pm_metal_mem_arena_t *)_a_v;
    size_t bytes = (size_t)mp_obj_get_int(args[1]);
    return mp_obj_new_int((mp_int_t)(uintptr_t)pm_metal_mem_arena_heap_grow(a, bytes));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(arena_heap_grow_obj, 2, 2, arena_heap_grow);

static mp_obj_t arena_map(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_a_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    pm_metal_mem_arena_t * a = (pm_metal_mem_arena_t *)_a_v;
    size_t bytes = (size_t)mp_obj_get_int(args[1]);
    return mp_obj_new_int((mp_int_t)(uintptr_t)pm_metal_mem_arena_map(a, bytes));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(arena_map_obj, 2, 2, arena_map);

static mp_obj_t arena_unmap(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_a_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    pm_metal_mem_arena_t * a = (pm_metal_mem_arena_t *)_a_v;
    mp_buffer_info_t ptr_bi;
    const uint8_t *ptr;
    if (args[1] == mp_const_none) { ptr = NULL; }
    else if (mp_obj_is_str_or_bytes(args[1])) {
        size_t _ptr_n; const char *_ptr_s = mp_obj_str_get_data(args[1], &_ptr_n); ptr=(const uint8_t*)_ptr_s;
    } else {
        mp_get_buffer_raise(args[1], &ptr_bi, MP_BUFFER_READ); ptr=(const uint8_t*)ptr_bi.buf;
    }
    size_t bytes = (size_t)mp_obj_get_int(args[2]);
    return mp_obj_new_int((mp_int_t)pm_metal_mem_arena_unmap(a, (uint8_t *)(uintptr_t)ptr, bytes));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(arena_unmap_obj, 3, 3, arena_unmap);

static mp_obj_t arena_align_up(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    size_t x = (size_t)mp_obj_get_int(args[0]);
    size_t a = (size_t)mp_obj_get_int(args[1]);
    return mp_obj_new_int((mp_int_t)pm_metal_mem_arena_align_up(x, a));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(arena_align_up_obj, 2, 2, arena_align_up);

static mp_obj_t arena_align_down(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    size_t x = (size_t)mp_obj_get_int(args[0]);
    size_t a = (size_t)mp_obj_get_int(args[1]);
    return mp_obj_new_int((mp_int_t)pm_metal_mem_arena_align_down(x, a));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(arena_align_down_obj, 2, 2, arena_align_down);

static mp_obj_t arena_page_size(void) {
    return mp_obj_new_int((mp_int_t)pm_metal_mem_arena_page_size());
}
static MP_DEFINE_CONST_FUN_OBJ_0(arena_page_size_obj, arena_page_size);

static const mp_rom_map_elem_t arena_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_mem_dot_arena) },
    { MP_ROM_QSTR(MP_QSTR_empty), MP_ROM_PTR(&arena_empty_obj) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&arena_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_ready), MP_ROM_PTR(&arena_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_bytes), MP_ROM_PTR(&arena_bytes_obj) },
    { MP_ROM_QSTR(MP_QSTR_map_used), MP_ROM_PTR(&arena_map_used_obj) },
    { MP_ROM_QSTR(MP_QSTR_heap_used), MP_ROM_PTR(&arena_heap_used_obj) },
    { MP_ROM_QSTR(MP_QSTR_hole), MP_ROM_PTR(&arena_hole_obj) },
    { MP_ROM_QSTR(MP_QSTR_heap_grow), MP_ROM_PTR(&arena_heap_grow_obj) },
    { MP_ROM_QSTR(MP_QSTR_map), MP_ROM_PTR(&arena_map_obj) },
    { MP_ROM_QSTR(MP_QSTR_unmap), MP_ROM_PTR(&arena_unmap_obj) },
    { MP_ROM_QSTR(MP_QSTR_align_up), MP_ROM_PTR(&arena_align_up_obj) },
    { MP_ROM_QSTR(MP_QSTR_align_down), MP_ROM_PTR(&arena_align_down_obj) },
    { MP_ROM_QSTR(MP_QSTR_page_size), MP_ROM_PTR(&arena_page_size_obj) },
};
static MP_DEFINE_CONST_DICT(arena_globals, arena_globals_table);
const mp_obj_module_t mp_module_pymergetic_metal_mem_arena = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&arena_globals,
};
