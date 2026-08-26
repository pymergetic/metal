/* pymergetic.metal.jit.py — host prove (C API surface only; no µPy VM here).
 *
 * The host test binary (host_test.c) does not link MicroPython — it is a
 * pure-C async runner for metal cards. The compile step requires the µPy
 * bytecode VM, so the compile functions are stubs here (always return
 * NULL/ERROR). The real compile verify lives as a Python prove script
 * in the upy seat.
 *
 * This test proves the stub surface compiles and links. */
#include "pymergetic/metal/async.h"
#include "pymergetic/metal/jit/py/__types__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdlib.h>
#include <string.h>

static int32_t test_stubs(void) {
    /* Host binary has no µPy — compile_alloc always returns NULL. */
    pm_metal_async_coro_t *coro = pm_metal_jit_py_compile_alloc(
        NULL, "x = 42\n", 6, "test_module");
    if (coro != NULL) {
        return 1;
    }
    /* result_free is a no-op, including on NULL pointers. */
    pm_metal_jit_py_result_free(NULL, NULL);
    return 0;
}

static int32_t test_step_error(void) {
    /* compile_step on invalid frame is ERROR. */
    pm_metal_async_status_t st = pm_metal_jit_py_compile_step(NULL);
    if (st != PM_METAL_ASYNC_ERROR) {
        return 1;
    }
    return 0;
}

static int32_t pm_metal_jit_py_tests(void) {
    if (test_stubs() != 0) {
        return 1;
    }
    if (test_step_error() != 0) {
        return 1;
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.jit.py, tests, pm_metal_jit_py_tests);