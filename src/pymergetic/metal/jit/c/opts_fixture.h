/* jit.c object_compile_opts prove fixture: a real header the test compiles a
 * source against via include_dirs (never a string pasted into the test). */
#ifndef PYMERGETIC_METAL_JIT_C_OPTS_FIXTURE_H
#define PYMERGETIC_METAL_JIT_C_OPTS_FIXTURE_H

#define PM_JIT_C_TEST_VAL 7

static inline int pm_jit_c_fixture_scale(int v) {
    return v * PM_JIT_C_TEST_VAL;
}

#endif
