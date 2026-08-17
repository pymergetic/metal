/* pymergetic.metal.console — ring + one viewport after boot. */
#include "pymergetic/metal/console.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.console test: %s\n", why);
    return 1;
}

static uint32_t s_sink_n;

static void test_sink(const char *s, uint32_t n) {
    if (s != NULL) {
        s_sink_n += n;
    }
}

int32_t pm_metal_console_tests(void) {
    uint32_t before;
    const char *kind;
    if (!pm_metal_console_ready()) {
        return fail("ready");
    }
    if (pm_metal_console_id() != 0u) {
        return fail("id");
    }
    if (pm_metal_console_viewport_count() == 0u) {
        return fail("viewport count");
    }
    kind = pm_metal_console_viewport_kind(0);
    if (kind == NULL || kind[0] == 0) {
        return fail("viewport kind");
    }
    if (pm_metal_console_viewport_attach(NULL, test_sink) == 0) {
        return fail("attach null kind");
    }
    if (pm_metal_console_viewport_attach("x", NULL) == 0) {
        return fail("attach null sink");
    }
    before = pm_metal_console_line_count();
    if (pm_metal_console_write("console-prove\n", 14) != 0) {
        return fail("write");
    }
    if (pm_metal_console_line_count() != before + 1u) {
        return fail("line count");
    }
    s_sink_n = 0;
    if (pm_metal_console_viewport_attach("test", test_sink) != 0) {
        return fail("attach test");
    }
    if (s_sink_n == 0u) {
        return fail("replay");
    }
    {
        uint32_t after_replay = s_sink_n;
        if (pm_metal_console_write("ab", 2) != 0 || s_sink_n < after_replay + 2u) {
            return fail("fanout");
        }
    }
    if (pm_metal_console_viewport_kind(99) != NULL) {
        return fail("kind oob");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.console, tests, pm_metal_console_tests);
