/* pymergetic.metal.console — ring + one viewport after boot; fb glyphs. */
#include "pymergetic/metal/console.h"
#include "pymergetic/metal/display.h"
#include "pymergetic/metal/drivers/gfx.h"
#include "pymergetic/metal/drivers/gfx/sim.h"
#include "pymergetic/util/limits.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Screens this file works on. 0 is the seat's own and 1 and the last one are
 * taken by the cases above, so the knob cases get their own. */
#define VP_CONSOLE 3
#define DEEP_CONSOLE 4

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

static void quiet_sink(const char *s, uint32_t n) {
    (void)s;
    (void)n;
}

/* The screens a seat offers, and what an unused one costs. A console is six
 * screens of scrollback only if six screens are printed on: the knob says how
 * many there are to select, and the memory follows the printing. */
static int32_t case_screen_knob(void) {
    int32_t slot = pm_util_limits_find("console.screen");
    uint32_t offered = pm_metal_console_count();
    if (slot < 0) {
        return fail("the screen knob is not on this seat");
    }
    if (pm_util_limits_default(slot) != 6u) {
        return fail("console shipped with another screen default");
    }
    if (pm_util_limits_used(slot) >= offered) {
        return fail("every screen took memory whether it was printed on or not");
    }
    if (pm_metal_console_write_id((int32_t)offered, "x\n", 2) == 0) {
        return fail("wrote to a screen the seat does not offer");
    }
    if (pm_util_limits_set("console.screen", offered + 2u) != 0) {
        return fail("set the screen knob");
    }
    if (pm_metal_console_count() != offered + 2u) {
        return fail("the seat does not offer the screens it was given");
    }
    if (pm_metal_console_write_id((int32_t)offered, "new screen\n", 11) != 0
        || pm_metal_console_line_count_id((int32_t)offered) != 1u) {
        return fail("the new screen kept nothing");
    }
    if (pm_util_limits_reset("console.screen") != 0
        || pm_metal_console_count() != offered) {
        return fail("reset did not put the screens back");
    }
    return 0;
}

/* Scrollback is a knob, not a shape: a seat that wants to keep more of its
 * own output says so and the ring follows, up and back down.
 *
 * The ring is reshaped when the knob moves, not as lines arrive — printing
 * never reaches the allocator — so this walks the knob and then the output. */
static int32_t case_scrollback_knob(void) {
    char buf[PM_METAL_CONSOLE_READ_MAX];
    int32_t slot = pm_util_limits_find("console.scrollback");
    uint32_t base;
    uint32_t i;
    if (slot < 0 || pm_util_limits_default(slot) != PM_METAL_CONSOLE_LINES_DEFAULT) {
        return fail("console shipped with another scrollback default");
    }
    if (pm_util_limits_set("console.scrollback", 200u) != 0) {
        return fail("set the scrollback knob");
    }
    base = pm_metal_console_seq_id(DEEP_CONSOLE);
    for (i = 0; i < 150u; i++) {
        char msg[24];
        int n = snprintf(msg, sizeof(msg), "deep %u\n", (unsigned)i);
        if (n <= 0 || pm_metal_console_write_id(DEEP_CONSOLE, msg, (uint32_t)n) != 0) {
            return fail("write to the deep console");
        }
    }
    if (pm_metal_console_line_count_id(DEEP_CONSOLE) != 150u) {
        return fail("the deeper ring did not keep every line");
    }
    /* At the shipped depth this line scrolled out eighty-six lines ago. */
    if (pm_metal_console_line_at_id(DEEP_CONSOLE, base, buf, sizeof(buf)) != 6
        || strcmp(buf, "deep 0") != 0) {
        return fail("a line only a deeper ring could still hold");
    }
    /* And back down: the newest lines survive, the oldest are let go, and the
     * reader is told which is which rather than handed a stale slot. */
    if (pm_util_limits_reset("console.scrollback") != 0) {
        return fail("reset the scrollback knob");
    }
    if (pm_metal_console_line_count_id(DEEP_CONSOLE) != PM_METAL_CONSOLE_LINES_DEFAULT) {
        return fail("the ring did not come back down");
    }
    if (pm_metal_console_write_id(DEEP_CONSOLE, "after\n", 6) != 0) {
        return fail("write after the knob came down");
    }
    if (pm_metal_console_line_at_id(DEEP_CONSOLE, base, buf, sizeof(buf)) != -1) {
        return fail("a line the narrower ring dropped is still claimed");
    }
    {
        uint32_t last = pm_metal_console_seq_id(DEEP_CONSOLE) - 1u;
        if (pm_metal_console_line_at_id(DEEP_CONSOLE, last, buf, sizeof(buf)) != 5
            || strcmp(buf, "after") != 0) {
            return fail("the newest line did not survive the narrowing");
        }
    }
    return 0;
}

/* Viewports and taps are per console and both grow. Four of each is where a
 * screen starts; a seat with a panel, a serial line, an ssh session and a
 * recorder on one screen raises it instead of losing the last one. */
static int32_t case_view_knobs(void) {
    static const char *kinds[6] = { "v0", "v1", "v2", "v3", "v4", "v5" };
    const uint32_t t = 60000u;
    uint32_t i;
    if (pm_util_limits_default(pm_util_limits_find("console.viewport")) != 4u
        || pm_util_limits_default(pm_util_limits_find("console.tap")) != 4u) {
        return fail("console shipped with other view defaults");
    }
    for (i = 0; i < 4u; i++) {
        if (pm_metal_console_viewport_attach_id(VP_CONSOLE, kinds[i], quiet_sink) != 0) {
            return fail("attach up to the shipped viewport count");
        }
    }
    if (pm_metal_console_viewport_attach_id(VP_CONSOLE, kinds[4], quiet_sink) == 0) {
        return fail("a viewport attached past its knob");
    }
    if (pm_util_limits_set("console.viewport", 6u) != 0) {
        return fail("set the viewport knob");
    }
    if (pm_metal_console_viewport_attach_id(VP_CONSOLE, kinds[4], quiet_sink) != 0
        || pm_metal_console_viewport_count_id(VP_CONSOLE) != 5u) {
        return fail("raising the knob did not take the viewport");
    }
    if (pm_util_limits_reset("console.viewport") != 0) {
        return fail("reset the viewport knob");
    }
    /* Taps: six readers on one screen, which the shipped four would have
     * turned into four by evicting the quietest. */
    if (pm_util_limits_set("console.tap", 8u) != 0) {
        return fail("set the tap knob");
    }
    for (i = 0; i < 6u; i++) {
        if (pm_metal_console_tap_id(DEEP_CONSOLE, 200u + i, t, "http cursor") != 0) {
            return fail("tap on the deep console");
        }
    }
    if (pm_metal_console_tap_count_id(DEEP_CONSOLE, t) != 6u) {
        return fail("a raised tap knob still evicted a reader");
    }
    if (pm_util_limits_reset("console.tap") != 0) {
        return fail("reset the tap knob");
    }
    return 0;
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
    /* The cursor face: a reader that was not attached when a line was written
     * still gets it, by sequence, and is told plainly when a sequence is one
     * the ring no longer holds. */
    {
        /* On the last console, not the selected one: this scrolls a ring
         * right over its own tail, and console 0 is the one the seat and the
         * tests after this one are reading. No viewport is attached there
         * either, so the scroll does not come out on anybody's terminal. */
        const int32_t ncon = (int32_t)pm_metal_console_count();
        const int32_t scratch = ncon - 1;
        const uint32_t selected = pm_metal_console_id();
        char buf[PM_METAL_CONSOLE_READ_MAX];
        uint32_t at = pm_metal_console_seq_id(scratch);
        int32_t n;
        if (pm_metal_console_write_id(scratch, "cursor-prove\n", 13) != 0) {
            return fail("cursor write");
        }
        if (pm_metal_console_seq_id(scratch) != at + 1u) {
            return fail("seq advances one per line");
        }
        n = pm_metal_console_line_at_id(scratch, at, buf, sizeof(buf));
        if (n != 12 || strcmp(buf, "cursor-prove") != 0) {
            return fail("line_at reads the line back");
        }
        if (pm_metal_console_line_at_id(scratch, pm_metal_console_seq_id(scratch), buf,
                sizeof(buf)) != -1) {
            return fail("line_at refuses a line not written yet");
        }
        if (pm_metal_console_line_at_id(scratch, at, buf, 4) != 3
            || strcmp(buf, "cur") != 0) {
            return fail("line_at fits the caller's buffer");
        }
        /* Scroll the ring right past `at` and ask for it again. */
        {
            uint32_t i;
            for (i = 0; i <= PM_METAL_CONSOLE_LINES_DEFAULT; i++) {
                (void)pm_metal_console_write_id(scratch, "scroll\n", 7);
            }
        }
        if (pm_metal_console_line_at_id(scratch, at, buf, sizeof(buf)) != -1) {
            return fail("line_at refuses a line that scrolled out");
        }
        if (pm_metal_console_line_at_id(-1, 0, buf, sizeof(buf)) != -1
            || pm_metal_console_line_at_id(ncon, 0, buf, sizeof(buf)) != -1) {
            return fail("line_at oob console");
        }
        /* And the half-written line, which has no sequence of its own yet. */
        if (pm_metal_console_write_id(scratch, "pend", 4) != 0) {
            return fail("pending write");
        }
        if (pm_metal_console_pending_id(scratch, buf, sizeof(buf)) != 4u
            || strcmp(buf, "pend") != 0) {
            return fail("pending reads the unterminated line");
        }
        (void)pm_metal_console_write_id(scratch, "\n", 1);
        if (pm_metal_console_pending_id(scratch, buf, sizeof(buf)) != 0u || buf[0] != 0) {
            return fail("pending empties on newline");
        }
        if (pm_metal_console_seq_id(1) == 0u || pm_metal_console_seq_id(ncon) != 0u) {
            return fail("seq per console");
        }
        /* The selected console is still the one we came in on, untouched. */
        if (pm_metal_console_id() != selected) {
            return fail("cursor face left the selection alone");
        }
    }
    /* Taps: a reader that pulls the ring counts as a view of this console
     * while it keeps asking, and stops counting when it goes quiet. Time is
     * handed in rather than read, so this walks a clock instead of sleeping. */
    {
        const int32_t scratch = (int32_t)pm_metal_console_count() - 1;
        const uint32_t t0 = 1000u;
        uint32_t i;
        if (pm_metal_console_tap_count_id(scratch, t0) != 0u) {
            return fail("a console nobody reads has no taps");
        }
        if (pm_metal_console_tap_id(scratch, 7u, t0, "http cursor") != 0) {
            return fail("tap");
        }
        if (pm_metal_console_tap_count_id(scratch, t0) != 1u) {
            return fail("a reader that asked is counted");
        }
        if (pm_metal_console_tap_id(scratch, 7u, t0 + 100u, "http cursor") != 0
            || pm_metal_console_tap_count_id(scratch, t0 + 100u) != 1u) {
            return fail("the same reader asking again is still one reader");
        }
        if (pm_metal_console_tap_id(scratch, 8u, t0 + 100u, "http cursor") != 0
            || pm_metal_console_tap_count_id(scratch, t0 + 100u) != 2u) {
            return fail("a second reader is a second view");
        }
        {
            const char *k = pm_metal_console_tap_kind_id(scratch, 0, t0 + 100u);
            if (k == NULL || strcmp(k, "http cursor") != 0) {
                return fail("a tap says what kind of reader it is");
            }
            if (pm_metal_console_tap_kind_id(scratch, 2, t0 + 100u) != NULL) {
                return fail("and there is no third one to name");
            }
        }
        /* Past the window with nobody asking: both are gone, and the console
         * is back to whatever is written into it. */
        if (pm_metal_console_tap_count_id(scratch,
                t0 + 100u + PM_METAL_CONSOLE_TAP_TTL_MS + 1u) != 0u) {
            return fail("a reader that stopped asking stops counting");
        }
        /* More readers than slots: the newcomer gets in, and the count never
         * claims more views than the table can hold. */
        for (i = 0; i < PM_METAL_CONSOLE_TAP_DEFAULT + 2u; i++) {
            if (pm_metal_console_tap_id(scratch, 100u + i, t0 + 5000u + i, "http cursor") != 0) {
                return fail("tap table full");
            }
        }
        if (pm_metal_console_tap_count_id(scratch, t0 + 5000u + i)
                != PM_METAL_CONSOLE_TAP_DEFAULT) {
            return fail("a full table counts its slots, not its callers");
        }
        if (pm_metal_console_tap_id(-1, 1u, t0, "http cursor") == 0
            || pm_metal_console_tap_id(scratch, 1u, t0, NULL) == 0) {
            return fail("tap refuses a console it has not got and a nameless kind");
        }
        /* Console 0 is what the seat prints on and what the tree counts; this
         * walk must not have added a reader to it. */
        if (pm_metal_console_tap_count_id(0, t0 + 5000u) != 0u) {
            return fail("taps are per console");
        }
    }
    if (case_screen_knob() != 0) {
        return 1;
    }
    if (case_scrollback_knob() != 0) {
        return 1;
    }
    if (case_view_knobs() != 0) {
        return 1;
    }
    if (pm_metal_console_up() != 0 || pm_metal_console_id() != 0u) {
        return fail("up");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.console, tests, pm_metal_console_tests);
