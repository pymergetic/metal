/* pymergetic.metal.mem.tlsf — µPy face (pointer-safe). */
#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"

#include <pymergetic/metal/mem/tlsf/__init__.h>
static mp_obj_t tlsf_size(void) {
    return mp_obj_new_int((mp_int_t)pm_metal_mem_tlsf_size());
}
static MP_DEFINE_CONST_FUN_OBJ_0(tlsf_size_obj, tlsf_size);

static mp_obj_t tlsf_pool_overhead(void) {
    return mp_obj_new_int((mp_int_t)pm_metal_mem_tlsf_pool_overhead());
}
static MP_DEFINE_CONST_FUN_OBJ_0(tlsf_pool_overhead_obj, tlsf_pool_overhead);

static mp_obj_t tlsf_align_size(void) {
    return mp_obj_new_int((mp_int_t)pm_metal_mem_tlsf_align_size());
}
static MP_DEFINE_CONST_FUN_OBJ_0(tlsf_align_size_obj, tlsf_align_size);

static mp_obj_t tlsf_alloc_overhead(void) {
    return mp_obj_new_int((mp_int_t)pm_metal_mem_tlsf_alloc_overhead());
}
static MP_DEFINE_CONST_FUN_OBJ_0(tlsf_alloc_overhead_obj, tlsf_alloc_overhead);

static mp_obj_t tlsf_create_with_pool(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_buffer_info_t mem_bi;
    const uint8_t *mem;
    if (args[0] == mp_const_none) { mem = NULL; }
    else if (mp_obj_is_str_or_bytes(args[0])) {
        size_t _mem_n; const char *_mem_s = mp_obj_str_get_data(args[0], &_mem_n); mem=(const uint8_t*)_mem_s;
    } else {
        mp_get_buffer_raise(args[0], &mem_bi, MP_BUFFER_READ); mem=(const uint8_t*)mem_bi.buf;
    }
    size_t bytes = (size_t)mp_obj_get_int(args[1]);
    return mp_obj_new_int((mp_int_t)(uintptr_t)pm_metal_mem_tlsf_create_with_pool((uint8_t *)(uintptr_t)mem, bytes));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(tlsf_create_with_pool_obj, 2, 2, tlsf_create_with_pool);

static mp_obj_t tlsf_get_pool(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_t_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    void * t = (void *)_t_v;
    return mp_obj_new_int((mp_int_t)(uintptr_t)pm_metal_mem_tlsf_get_pool(t));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(tlsf_get_pool_obj, 1, 1, tlsf_get_pool);

static mp_obj_t tlsf_add_pool(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_t_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    void * t = (void *)_t_v;
    mp_buffer_info_t mem_bi;
    const uint8_t *mem;
    if (args[1] == mp_const_none) { mem = NULL; }
    else if (mp_obj_is_str_or_bytes(args[1])) {
        size_t _mem_n; const char *_mem_s = mp_obj_str_get_data(args[1], &_mem_n); mem=(const uint8_t*)_mem_s;
    } else {
        mp_get_buffer_raise(args[1], &mem_bi, MP_BUFFER_READ); mem=(const uint8_t*)mem_bi.buf;
    }
    size_t bytes = (size_t)mp_obj_get_int(args[2]);
    return mp_obj_new_int((mp_int_t)(uintptr_t)pm_metal_mem_tlsf_add_pool(t, (uint8_t *)(uintptr_t)mem, bytes));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(tlsf_add_pool_obj, 3, 3, tlsf_add_pool);

static mp_obj_t tlsf_malloc(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_t_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    void * t = (void *)_t_v;
    size_t size = (size_t)mp_obj_get_int(args[1]);
    return mp_obj_new_int((mp_int_t)(uintptr_t)pm_metal_mem_tlsf_malloc(t, size));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(tlsf_malloc_obj, 2, 2, tlsf_malloc);

static mp_obj_t tlsf_memalign(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_t_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    void * t = (void *)_t_v;
    size_t align = (size_t)mp_obj_get_int(args[1]);
    size_t size = (size_t)mp_obj_get_int(args[2]);
    return mp_obj_new_int((mp_int_t)(uintptr_t)pm_metal_mem_tlsf_memalign(t, align, size));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(tlsf_memalign_obj, 3, 3, tlsf_memalign);

static mp_obj_t tlsf_realloc(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_t_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    void * t = (void *)_t_v;
    mp_buffer_info_t ptr_bi;
    const uint8_t *ptr;
    if (args[1] == mp_const_none) { ptr = NULL; }
    else if (mp_obj_is_str_or_bytes(args[1])) {
        size_t _ptr_n; const char *_ptr_s = mp_obj_str_get_data(args[1], &_ptr_n); ptr=(const uint8_t*)_ptr_s;
    } else {
        mp_get_buffer_raise(args[1], &ptr_bi, MP_BUFFER_READ); ptr=(const uint8_t*)ptr_bi.buf;
    }
    size_t size = (size_t)mp_obj_get_int(args[2]);
    return mp_obj_new_int((mp_int_t)(uintptr_t)pm_metal_mem_tlsf_realloc(t, (uint8_t *)(uintptr_t)ptr, size));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(tlsf_realloc_obj, 3, 3, tlsf_realloc);

static mp_obj_t tlsf_free(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_t_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    void * t = (void *)_t_v;
    mp_buffer_info_t p_bi;
    const uint8_t *p;
    if (args[1] == mp_const_none) { p = NULL; }
    else if (mp_obj_is_str_or_bytes(args[1])) {
        size_t _p_n; const char *_p_s = mp_obj_str_get_data(args[1], &_p_n); p=(const uint8_t*)_p_s;
    } else {
        mp_get_buffer_raise(args[1], &p_bi, MP_BUFFER_READ); p=(const uint8_t*)p_bi.buf;
    }
    pm_metal_mem_tlsf_free(t, (uint8_t *)(uintptr_t)p); return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(tlsf_free_obj, 2, 2, tlsf_free);

static mp_obj_t tlsf_block_size(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_buffer_info_t ptr_bi;
    const uint8_t *ptr;
    if (args[0] == mp_const_none) { ptr = NULL; }
    else if (mp_obj_is_str_or_bytes(args[0])) {
        size_t _ptr_n; const char *_ptr_s = mp_obj_str_get_data(args[0], &_ptr_n); ptr=(const uint8_t*)_ptr_s;
    } else {
        mp_get_buffer_raise(args[0], &ptr_bi, MP_BUFFER_READ); ptr=(const uint8_t*)ptr_bi.buf;
    }
    return mp_obj_new_int((mp_int_t)pm_metal_mem_tlsf_block_size((uint8_t *)(uintptr_t)ptr));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(tlsf_block_size_obj, 1, 1, tlsf_block_size);

static mp_obj_t tlsf_block_size_min(void) {
    return mp_obj_new_int((mp_int_t)pm_metal_mem_tlsf_block_size_min());
}
static MP_DEFINE_CONST_FUN_OBJ_0(tlsf_block_size_min_obj, tlsf_block_size_min);

static mp_obj_t tlsf_block_size_max(void) {
    return mp_obj_new_int((mp_int_t)pm_metal_mem_tlsf_block_size_max());
}
static MP_DEFINE_CONST_FUN_OBJ_0(tlsf_block_size_max_obj, tlsf_block_size_max);

static mp_obj_t tlsf_create(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_buffer_info_t mem_bi;
    const uint8_t *mem;
    if (args[0] == mp_const_none) { mem = NULL; }
    else if (mp_obj_is_str_or_bytes(args[0])) {
        size_t _mem_n; const char *_mem_s = mp_obj_str_get_data(args[0], &_mem_n); mem=(const uint8_t*)_mem_s;
    } else {
        mp_get_buffer_raise(args[0], &mem_bi, MP_BUFFER_READ); mem=(const uint8_t*)mem_bi.buf;
    }
    return mp_obj_new_int((mp_int_t)(uintptr_t)pm_metal_mem_tlsf_create((uint8_t *)(uintptr_t)mem));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(tlsf_create_obj, 1, 1, tlsf_create);

static mp_obj_t tlsf_destroy(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_t_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    void * t = (void *)_t_v;
    pm_metal_mem_tlsf_destroy(t); return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(tlsf_destroy_obj, 1, 1, tlsf_destroy);

static mp_obj_t tlsf_remove_pool(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_t_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    void * t = (void *)_t_v;
    void *_pool_v = (args[1] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[1]);
    void * pool = (void *)_pool_v;
    pm_metal_mem_tlsf_remove_pool(t, pool); return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(tlsf_remove_pool_obj, 2, 2, tlsf_remove_pool);

static mp_obj_t tlsf_walk_pool(void) { return MP_OBJ_NEW_SMALL_INT(-1); }
static MP_DEFINE_CONST_FUN_OBJ_0(tlsf_walk_pool_obj, tlsf_walk_pool);

static mp_obj_t tlsf_check(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_t_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    void * t = (void *)_t_v;
    return mp_obj_new_int((mp_int_t)pm_metal_mem_tlsf_check(t));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(tlsf_check_obj, 1, 1, tlsf_check);

static mp_obj_t tlsf_check_pool(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    void *_pool_v = (args[0] == mp_const_none) ? NULL : (void *)(uintptr_t)mp_obj_get_int(args[0]);
    void * pool = (void *)_pool_v;
    return mp_obj_new_int((mp_int_t)pm_metal_mem_tlsf_check_pool(pool));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(tlsf_check_pool_obj, 1, 1, tlsf_check_pool);

static const mp_rom_map_elem_t tlsf_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_mem_dot_tlsf) },
    { MP_ROM_QSTR(MP_QSTR_size), MP_ROM_PTR(&tlsf_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_pool_overhead), MP_ROM_PTR(&tlsf_pool_overhead_obj) },
    { MP_ROM_QSTR(MP_QSTR_align_size), MP_ROM_PTR(&tlsf_align_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_alloc_overhead), MP_ROM_PTR(&tlsf_alloc_overhead_obj) },
    { MP_ROM_QSTR(MP_QSTR_create_with_pool), MP_ROM_PTR(&tlsf_create_with_pool_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_pool), MP_ROM_PTR(&tlsf_get_pool_obj) },
    { MP_ROM_QSTR(MP_QSTR_add_pool), MP_ROM_PTR(&tlsf_add_pool_obj) },
    { MP_ROM_QSTR(MP_QSTR_malloc), MP_ROM_PTR(&tlsf_malloc_obj) },
    { MP_ROM_QSTR(MP_QSTR_memalign), MP_ROM_PTR(&tlsf_memalign_obj) },
    { MP_ROM_QSTR(MP_QSTR_realloc), MP_ROM_PTR(&tlsf_realloc_obj) },
    { MP_ROM_QSTR(MP_QSTR_free), MP_ROM_PTR(&tlsf_free_obj) },
    { MP_ROM_QSTR(MP_QSTR_block_size), MP_ROM_PTR(&tlsf_block_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_block_size_min), MP_ROM_PTR(&tlsf_block_size_min_obj) },
    { MP_ROM_QSTR(MP_QSTR_block_size_max), MP_ROM_PTR(&tlsf_block_size_max_obj) },
    { MP_ROM_QSTR(MP_QSTR_create), MP_ROM_PTR(&tlsf_create_obj) },
    { MP_ROM_QSTR(MP_QSTR_destroy), MP_ROM_PTR(&tlsf_destroy_obj) },
    { MP_ROM_QSTR(MP_QSTR_remove_pool), MP_ROM_PTR(&tlsf_remove_pool_obj) },
    { MP_ROM_QSTR(MP_QSTR_walk_pool), MP_ROM_PTR(&tlsf_walk_pool_obj) },
    { MP_ROM_QSTR(MP_QSTR_check), MP_ROM_PTR(&tlsf_check_obj) },
    { MP_ROM_QSTR(MP_QSTR_check_pool), MP_ROM_PTR(&tlsf_check_pool_obj) },
};
static MP_DEFINE_CONST_DICT(tlsf_globals, tlsf_globals_table);
const mp_obj_module_t mp_module_pymergetic_metal_mem_tlsf = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&tlsf_globals,
};
