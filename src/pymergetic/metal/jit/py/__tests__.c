/* pymergetic.metal.jit.py — prove for the µPy object faces.
 *
 * Seat-split, same contract on both sides:
 * - seats without the µPy kernel (or without persistent-code save): the
 *   stub surface — compile_alloc refuses NULL args, compile_step refuses
 *   a NULL/foreign frame, result_free is a no-op. The real compile verify
 *   lives as a Python prove script in the upy seat.
 * - seats WITH the kernel (the host binary since the ports/embed route,
 *   the upy/browser/firmware seats): additionally the full object loop —
 *   compile a module body to mpy bytes, load it back, and prove one
 *   exported face answers. That is the same chain ksweep's unit_compile_py
 *   drives for impl="py" cards, plus the load and the invocation ksweep
 *   deliberately does not do. */
#include "pymergetic/metal/coop.h"
#include "pymergetic/metal/jit/py/__types__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdlib.h>
#include <string.h>

static int32_t test_stubs(void) {
    /* NULL args: compile_alloc always refuses (arena==NULL -> NULL). */
    pm_metal_coop_coro_t *coro = pm_metal_jit_py_compile_alloc(
        NULL, "x = 42\n", 6, "test_module");
    if (coro != NULL) {
        return 1;
    }
    /* result_free is a no-op, including on NULL pointers. */
    pm_metal_jit_py_result_free(NULL, NULL);
    return 0;
}

static int32_t test_step_error(void) {
    /* compile_step on invalid frame is ERROR — seat-neutral contract that
     * must hold on the real path too, not just the stub. */
    pm_metal_coop_status_t st = pm_metal_jit_py_compile_step(NULL);
    if (st != PM_METAL_COOP_ERROR) {
        return 1;
    }
    return 0;
}

#if MICROPY_PY_WASM && !PM_WASMMOD_GUEST && MICROPY_PERSISTENT_CODE_SAVE
#include "py/obj.h"
#include "py/objmodule.h"
#include "py/runtime.h"

static int32_t test_object_loop(void) {
    static const char src[] = "def add(a, b):\n    return a + b\n";
    pm_util_mem_arena_t *arena;
    void *backing;
    uint8_t *mpy = NULL;
    size_t mpy_len = 0;
    char err[128] = {0};

    backing = malloc(1u << 20);
    if (backing == NULL) {
        return 1;
    }
    arena = pm_util_mem_arena_create(backing, 1u << 20);
    if (arena == NULL) {
        free(backing);
        return 1;
    }

    if (pm_metal_jit_py_object_compile(arena, src, sizeof(src) - 1u,
            "loop_test_module", &mpy, &mpy_len, err, sizeof(err)) != 0) {
        free(backing);
        return 1;
    }
    /* artifact shape: the mpy header is at least 4 bytes (version+flags) */
    if (mpy == NULL || mpy_len < 4) {
        free(backing);
        return 1;
    }

    if (pm_metal_jit_py_object_load(arena, mpy, mpy_len,
            "loop_test_module", err, sizeof(err)) != 0) {
        free(backing);
        return 1;
    }

    /* one real invocation: fetch add from the loaded module and call it */
    {
        mp_obj_t module = mp_import_name(qstr_from_str("loop_test_module"),
            mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t add = mp_load_attr(module, MP_QSTR_add);
        mp_obj_t got = mp_call_function_2(add,
            MP_OBJ_NEW_SMALL_INT(20), MP_OBJ_NEW_SMALL_INT(22));
        if (mp_obj_get_int(got) != 42) {
            free(backing);
            return 1;
        }
    }

    free(backing);
    return 0;
}
#endif

static int32_t pm_metal_jit_py_tests(void) {
    if (test_stubs() != 0) {
        return 1;
    }
    if (test_step_error() != 0) {
        return 1;
    }
#if MICROPY_PY_WASM && !PM_WASMMOD_GUEST && MICROPY_PERSISTENT_CODE_SAVE
    if (test_object_loop() != 0) {
        return 1;
    }
#endif
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.jit.py, tests, pm_metal_jit_py_tests);
