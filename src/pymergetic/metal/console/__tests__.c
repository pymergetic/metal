/* pymergetic.metal.console — ring + one viewport after boot; fb glyphs. */
#include "pymergetic/metal/console.h"
#include "pymergetic/metal/display.h"
#include "pymergetic/metal/drivers/gfx.h"
#include "pymergetic/metal/drivers/gfx/sim.h"
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
    if (pm_metal_console_count() != 6u) {
        return fail("count");
    }
    if (pm_metal_console_id() != 0u) {
        return fail("id");
    }
    if (pm_metal_console_select(-1) == 0 || pm_metal_console_select(6) == 0) {
        return fail("select oob");
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
    {
        uint8_t pix[64 * 32 * 3];
        int32_t h;
        uint32_t x;
        uint32_t lit;
        memset(pix, 0, sizeof(pix));
        if (pm_metal_display_attach(pix, 64, 32, 64 * 3u) != 0) {
            return fail("display attach");
        }
        h = pm_metal_drivers_gfx_sim_probe();
        if (h < 0 || pm_metal_display_attach_h(h) != 0) {
            return fail("display gfx");
        }
        if (pm_metal_console_fb_attach() != 0) {
            return fail("fb attach");
        }
        if (pm_metal_console_write("A\n", 2) != 0) {
            return fail("fb write");
        }
        lit = 0;
        for (x = 0; x < 64u * 32u; x++) {
            uint32_t px = x % 64u;
            uint32_t py = x / 64u;
            if (pm_metal_display_get(px, py) != 0u) {
                lit++;
            }
        }
        if (lit == 0u) {
            return fail("fb glyph");
        }
        if (pm_metal_drivers_gfx_present_n(h) == 0u) {
            return fail("fb present");
        }
    }
    {
        uint32_t n0 = pm_metal_console_line_count();
        uint32_t n1;
        if (pm_metal_console_write_id(1, "F2\n", 3) != 0) {
            return fail("write id");
        }
        if (pm_metal_console_line_count() != n0) {
            return fail("id0 untouched");
        }
        n1 = pm_metal_console_line_count_id(1);
        if (n1 < 1u) {
            return fail("id1 line");
        }
        s_sink_n = 0;
        if (pm_metal_console_viewport_attach_id(1, "test-1", test_sink) != 0) {
            return fail("attach id");
        }
        if (s_sink_n == 0u) {
            return fail("replay id");
        }
        if (pm_metal_console_viewport_count_id(1) < 1u
            || pm_metal_console_viewport_count() < 1u) {
            return fail("vp split");
        }
    }
    if (pm_metal_console_up() != 0 || pm_metal_console_id() != 0u) {
        return fail("up");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.console, tests, pm_metal_console_tests);
