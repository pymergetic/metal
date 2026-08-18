/* pymergetic.metal.process — spawn/quit table; REPL stays pid 0. */
#include "pymergetic/metal/process.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.process test: %s\n", why);
    return 1;
}

int32_t pm_metal_process_tests(void) {
    int32_t n0;
    int32_t pid;
    if (pm_metal_process_init(NULL) != -1) {
        return fail("init null");
    }
    if (pm_metal_process_current() != 0) {
        return fail("repl pid");
    }
    if (pm_metal_process_quit(0) == 0) {
        return fail("quit repl");
    }
    if (pm_metal_process_crown() >= 0) {
        return fail("crown empty");
    }
    n0 = pm_metal_process_count();
    pid = pm_metal_process_spawn();
    if (pid < 1) {
        return fail("spawn");
    }
    if (pm_metal_process_count() != n0 + 1) {
        return fail("count");
    }
    if (pm_metal_process_at(n0) != pid) {
        return fail("at");
    }
    if (pm_metal_process_quit(pid) != 0 || pm_metal_process_count() != n0) {
        return fail("quit");
    }
    if (pm_metal_process_quit(pid) == 0) {
        return fail("quit twice");
    }
    {
        int32_t i;
        int32_t extra[20];
        n0 = pm_metal_process_count();
        for (i = 0; i < 20; i++) {
            extra[i] = pm_metal_process_spawn();
            if (extra[i] < 1) {
                return fail("spawn many");
            }
        }
        if (pm_metal_process_count() != n0 + 20) {
            return fail("count many");
        }
        if (pm_metal_process_at(n0 + 19) != extra[19]) {
            return fail("at many");
        }
        for (i = 0; i < 20; i++) {
            if (pm_metal_process_quit(extra[i]) != 0) {
                return fail("quit many");
            }
        }
        if (pm_metal_process_count() != n0) {
            return fail("count after many");
        }
    }
    if (pm_metal_process_up() != 0) {
        return fail("up");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.process, tests, pm_metal_process_tests);
