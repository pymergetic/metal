/* pymergetic.metal.input — push, pop, feed console #0. */
#include "pymergetic/metal/console.h"
#include "pymergetic/metal/input.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.input test: %s\n", why);
    return 1;
}

int32_t pm_metal_input_tests(void) {
    uint32_t before;
    if (pm_metal_input_init(NULL) != -1) {
        return fail("init null");
    }
    if (pm_metal_input_pop() != -1) {
        return fail("empty pop");
    }
    if (pm_metal_input_push(-1) == 0) {
        return fail("push neg");
    }
    if (pm_metal_input_push((int32_t)'A') != 0 || pm_metal_input_count() < 1) {
        return fail("push");
    }
    if (pm_metal_input_pop() != (int32_t)'A' || pm_metal_input_count() != 0) {
        return fail("pop");
    }
    before = pm_metal_console_line_count();
    if (pm_metal_input_push((int32_t)'Z') != 0 || pm_metal_input_push((int32_t)'\n') != 0) {
        return fail("push line");
    }
    if (pm_metal_input_feed_console() != 2) {
        return fail("feed");
    }
    if (pm_metal_console_line_count() != before + 1u) {
        return fail("console line");
    }
    if (pm_metal_input_up() != 0) {
        return fail("up");
    }
    {
        uint32_t n0 = pm_metal_console_line_count();
        if (pm_metal_input_push(PM_METAL_INPUT_KEY_F2) != 0
            || pm_metal_input_feed_console() < 1
            || pm_metal_console_id() != 1u) {
            return fail("f2");
        }
        if (pm_metal_input_push((int32_t)'B') != 0 || pm_metal_input_push((int32_t)'\n') != 0
            || pm_metal_input_feed_console() != 2) {
            (void)pm_metal_console_select(0);
            return fail("feed f2");
        }
        if (pm_metal_console_line_count() != n0 || pm_metal_console_line_count_id(1) < 1u) {
            (void)pm_metal_console_select(0);
            return fail("f2 isolated");
        }
        if (pm_metal_console_select(0) != 0 || pm_metal_console_id() != 0u) {
            return fail("f1");
        }
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.input, tests, pm_metal_input_tests);
