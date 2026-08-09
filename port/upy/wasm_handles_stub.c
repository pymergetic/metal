/*
 * GC-rooted Python object handles when MICROPY_PY_WASM=0 (firmware metal).
 * wasmmod host.c provides the same symbols under MICROPY_PY_WASM=1.
 */
#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#if !MICROPY_PY_WASM

#include "py/obj.h"
#include "py/runtime.h"

#include <stdbool.h>
#include <stdint.h>

MP_REGISTER_ROOT_POINTER(mp_obj_t mp_wasm_handles);

static void handles_ensure(void)
{
    if (MP_STATE_VM(mp_wasm_handles) != MP_OBJ_NULL &&
        mp_obj_is_type(MP_STATE_VM(mp_wasm_handles), &mp_type_list)) {
        return;
    }
    mp_obj_list_t *list = m_new_obj(mp_obj_list_t);
    mp_obj_list_init(list, 0);
    MP_STATE_VM(mp_wasm_handles) = MP_OBJ_FROM_PTR(list);
}

int32_t mp_wasm_handle_register(mp_obj_t obj)
{
    if (obj == mp_const_none) {
        return 0;
    }
    handles_ensure();
    mp_obj_list_t *list = MP_OBJ_TO_PTR(MP_STATE_VM(mp_wasm_handles));
    for (size_t i = 0; i < list->len; ++i) {
        if (list->items[i] == mp_const_none) {
            list->items[i] = obj;
            return (int32_t)(i + 1);
        }
    }
    mp_obj_list_append(MP_STATE_VM(mp_wasm_handles), obj);
    list = MP_OBJ_TO_PTR(MP_STATE_VM(mp_wasm_handles));
    return (int32_t)list->len;
}

mp_obj_t mp_wasm_handle_resolve(int32_t handle)
{
    if (handle <= 0) {
        return mp_const_none;
    }
    handles_ensure();
    mp_obj_list_t *list = MP_OBJ_TO_PTR(MP_STATE_VM(mp_wasm_handles));
    size_t idx = (size_t)handle - 1u;
    if (idx >= list->len) {
        return mp_const_none;
    }
    return list->items[idx];
}

bool mp_wasm_handle_free(int32_t handle)
{
    if (handle <= 0) {
        return false;
    }
    handles_ensure();
    mp_obj_list_t *list = MP_OBJ_TO_PTR(MP_STATE_VM(mp_wasm_handles));
    size_t idx = (size_t)handle - 1u;
    if (idx >= list->len || list->items[idx] == mp_const_none) {
        return false;
    }
    list->items[idx] = mp_const_none;
    return true;
}

void mp_wasm_handle_clear_all(void)
{
    if (MP_STATE_VM(mp_wasm_handles) == MP_OBJ_NULL ||
        !mp_obj_is_type(MP_STATE_VM(mp_wasm_handles), &mp_type_list)) {
        return;
    }
    mp_obj_list_t *list = MP_OBJ_TO_PTR(MP_STATE_VM(mp_wasm_handles));
    for (size_t i = 0; i < list->len; ++i) {
        list->items[i] = mp_const_none;
    }
}

#endif /* !MICROPY_PY_WASM */
