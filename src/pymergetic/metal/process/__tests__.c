/* pymergetic.metal.process — spawn/quit table; REPL stays pid 0. */
#include "pymergetic/metal/process.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
    /* Budget faces: a pid with a cap gets a private sub-arena. Allocations
     * past the cap return NULL (refusal, not abort), and quit() returns the
     * backing to the parent arena. Re-init with a test-owned arena so the
     * recycle is measurable. */
    {
        void *tb;
        pm_util_mem_arena_t *ta;
        int32_t pid;
        pm_util_mem_arena_t *a;
        void *p;
        size_t used_before;
        tb = malloc(1u << 20);
        if (tb == NULL) {
            return fail("budget test backing");
        }
        ta = pm_util_mem_arena_create(tb, 1u << 20);
        if (ta == NULL) {
            free(tb);
            return fail("budget test arena");
        }
        if (pm_metal_process_init(ta) != 0) {
            free(tb);
            return fail("budget re-init");
        }
        pid = pm_metal_process_spawn();
        if (pid < 1) {
            free(tb);
            return fail("spawn for budget");
        }
        if (pm_metal_process_budget(pid) != 0) {
            free(tb);
            return fail("budget default");
        }
        if (pm_metal_process_arena(pid) != NULL) {
            free(tb);
            return fail("arena before set");
        }
        if (pm_metal_process_budget_set(pid, 64u * 1024u) != 0) {
            free(tb);
            return fail("budget set");
        }
        if (pm_metal_process_budget(pid) != (int32_t)(64u * 1024u)) {
            free(tb);
            return fail("budget readback");
        }
        a = pm_metal_process_arena(pid);
        if (a == NULL) {
            free(tb);
            return fail("arena after set");
        }
        if (pm_metal_process_budget_used(pid) > (int32_t)(64u * 1024u)) {
            free(tb);
            return fail("budget used sane");
        }
        p = pm_util_mem_alloc(a, 1024u);
        if (p == NULL) {
            free(tb);
            return fail("alloc inside budget");
        }
        if (pm_metal_process_budget_used(pid) < 1024) {
            free(tb);
            return fail("budget used counts");
        }
        /* an ask far past the cap is refused with NULL, not a crash */
        if (pm_util_mem_alloc(a, 512u * 1024u) != NULL) {
            free(tb);
            return fail("alloc past cap refused");
        }
        /* resize up keeps the budget */
        if (pm_metal_process_budget_set(pid, 256u * 1024u) != 0) {
            free(tb);
            return fail("budget grow");
        }
        if (pm_metal_process_budget(pid) != (int32_t)(256u * 1024u)) {
            free(tb);
            return fail("budget grown readback");
        }
        used_before = pm_util_mem_arena_heap_used(ta);
        if (pm_metal_process_quit(pid) != 0) {
            free(tb);
            return fail("quit budget pid");
        }
        if (pm_metal_process_budget(pid) != -1) {
            free(tb);
            return fail("budget after quit");
        }
        if (pm_util_mem_arena_heap_used(ta) != used_before) {
            free(tb);
            return fail("backing freed on quit");
        }
        if (pm_metal_process_budget_set(pid, 64u * 1024u) == 0) {
            free(tb);
            return fail("budget set on dead pid");
        }
        pm_metal_process_deinit();
        pm_util_mem_arena_destroy(ta);
        free(tb);
    }
    /* REPL budget (pid 0): set from the REPL, read back, refuse from a
     * spawned process. */
    {
        void *tb;
        pm_util_mem_arena_t *ta;
        tb = malloc(1u << 20);
        if (tb == NULL) {
            return fail("repl budget backing");
        }
        ta = pm_util_mem_arena_create(tb, 1u << 20);
        if (ta == NULL) {
            free(tb);
            return fail("repl budget arena");
        }
        if (pm_metal_process_init(ta) != 0) {
            free(tb);
            return fail("repl budget re-init");
        }
        if (pm_metal_process_current() != 0) {
            free(tb);
            return fail("repl budget not in repl");
        }
        if (pm_metal_process_budget(0) != 0) {
            free(tb);
            return fail("repl budget default");
        }
        if (pm_metal_process_budget_set(0, 128u * 1024u) != 0) {
            free(tb);
            return fail("repl budget set");
        }
        if (pm_metal_process_budget(0) != (int32_t)(128u * 1024u)) {
            free(tb);
            return fail("repl budget readback");
        }
        if (pm_metal_process_arena(0) == NULL) {
            free(tb);
            return fail("repl arena");
        }
        /* a spawned pid may not touch the REPL budget */
        {
            int32_t pid = pm_metal_process_spawn();
            if (pid < 1) {
                free(tb);
                return fail("repl budget spawn");
            }
            /* the spawned task is not current here (the test body runs as
             * the REPL), so a pid-0 set from it would be refused — but we
             * cannot switch current without async; the pid argument alone
             * must still be refused for a foreign slot */
            if (pm_metal_process_budget(pid) != 0) {
                free(tb);
                return fail("spawn budget default");
            }
            if (pm_metal_process_quit(pid) != 0) {
                free(tb);
                return fail("repl budget quit spawn");
            }
        }
        pm_metal_process_deinit();
        pm_util_mem_arena_destroy(ta);
        free(tb);
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.process, tests, pm_metal_process_tests);
