/* pymergetic.metal.ai -- prove the AI hub and tool registry.
 *
 * Each test creates its own arena so init/deinit and tool lifecycle
 * are tested in isolation. */
#include "pymergetic/metal/ai/__exports__.h"
#include "pymergetic/wasmmod/guest.h"
#include "pymergetic/util/mem.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TST_SPAN (64u * 1024u)

static int32_t fail_ai(const char *why) {
    fprintf(stderr, "metal.ai test: %s\n", why);
    return 1;
}

static int32_t s_mock_result;

static int32_t mock_tool(const char *json_params, char *out_result,
    uint32_t out_size, void *user)
{
    (void)json_params;
    (void)user;
    s_mock_result = 1;
    if (out_result != NULL && out_size > 0) {
        strncpy(out_result, "mock ok", out_size - 1);
        out_result[out_size - 1] = '\0';
    }
    return 42;
}

static int32_t with_init(int32_t (*body)(void)) {
    void *backing = malloc(TST_SPAN);
    pm_util_mem_arena_t *arena;
    int32_t rc;
    if (backing == NULL) return fail_ai("malloc");
    arena = pm_util_mem_arena_create(backing, TST_SPAN);
    if (arena == NULL) { free(backing); return fail_ai("arena-create"); }
    if (pm_metal_ai_init(arena) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing);
        return fail_ai("init");
    }
    rc = body();
    pm_metal_ai_deinit();
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return rc;
}

static int32_t body_init_deinit(void) { return 0; }

static int32_t case_init_deinit(void) { return with_init(body_init_deinit); }

static int32_t body_tool_register_invoke(void) {
    char out_buf[256];
    pm_metal_ai_tool_t info;
    s_mock_result = 0;
    if (pm_metal_ai_tool_register("mock", "a mock tool", "{}",
         mock_tool, NULL) != 0) return fail_ai("register");
    if (pm_metal_ai_tool_count() != 1) return fail_ai("count");
    memset(out_buf, 0, sizeof(out_buf));
    if (pm_metal_ai_tool_invoke("mock", "{}", out_buf,
         (uint32_t)sizeof(out_buf)) != 42) return fail_ai("invoke ret");
    if (s_mock_result != 1) return fail_ai("mock not called");
    if (strcmp(out_buf, "mock ok") != 0) return fail_ai("invoke output");
    if (pm_metal_ai_tool_nth(0, &info) != 0) return fail_ai("nth");
    if (strcmp(info.name, "mock") != 0) return fail_ai("nth name");
    return 0;
}

static int32_t case_tool_register_invoke(void) {
    return with_init(body_tool_register_invoke);
}

static int32_t body_tool_unregister(void) {
    if (pm_metal_ai_tool_register("t1", "desc", "{}",
         mock_tool, NULL) != 0) return fail_ai("register");
    if (pm_metal_ai_tool_count() != 1) return fail_ai("count before unreg");
    if (pm_metal_ai_tool_unregister("t1") != 0) return fail_ai("unregister");
    if (pm_metal_ai_tool_count() != 0) return fail_ai("count after unreg");
    return 0;
}

static int32_t case_tool_unregister(void) {
    return with_init(body_tool_unregister);
}

static int32_t body_tool_nth(void) {
    pm_metal_ai_tool_t info;
    uint32_t i;
    char name[16];
    for (i = 0; i < 3; i++) {
        snprintf(name, sizeof(name), "nth%u", (unsigned)i);
        if (pm_metal_ai_tool_register(name, "desc", "{}",
             mock_tool, NULL) != 0) return fail_ai("register");
    }
    if (pm_metal_ai_tool_count() != 3) return fail_ai("count");
    for (i = 0; i < 3; i++) {
        if (pm_metal_ai_tool_nth(i, &info) != 0) return fail_ai("nth");
    }
    if (pm_metal_ai_tool_nth(99, &info) == 0) return fail_ai("nth past end");
    if (pm_metal_ai_tool_nth(0, NULL) == 0) return fail_ai("nth NULL out");
    return 0;
}

static int32_t case_tool_nth(void) { return with_init(body_tool_nth); }

static int32_t body_null_reject(void) {
    char buf[16];
    if (pm_metal_ai_tool_register(NULL, "desc", "{}",
         mock_tool, NULL) != -1) return fail_ai("register NULL name");
    if (pm_metal_ai_tool_register("n", "desc", "{}",
         NULL, NULL) != -1) return fail_ai("register NULL handler");
    if (pm_metal_ai_tool_unregister(NULL) != -1) return fail_ai("unregister NULL");
    if (pm_metal_ai_tool_invoke("no-such", "{}", buf, 16) != -1)
        return fail_ai("invoke missing");
    return 0;
}

static int32_t case_null_reject(void) { return with_init(body_null_reject); }

static int32_t case_deinit_resets_tools(void) {
    void *backing = malloc(TST_SPAN);
    pm_util_mem_arena_t *arena;
    int32_t rc;
    if (backing == NULL) return fail_ai("malloc");
    arena = pm_util_mem_arena_create(backing, TST_SPAN);
    if (arena == NULL) { free(backing); return fail_ai("arena-create"); }
    if (pm_metal_ai_init(arena) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing);
        return fail_ai("init");
    }
    if (pm_metal_ai_tool_register("t", "desc", "{}", mock_tool, NULL) != 0) {
        pm_metal_ai_deinit(); pm_util_mem_arena_destroy(arena); free(backing);
        return fail_ai("register");
    }
    if (pm_metal_ai_tool_count() != 1) {
        pm_metal_ai_deinit(); pm_util_mem_arena_destroy(arena); free(backing);
        return fail_ai("count before deinit");
    }
    pm_metal_ai_deinit();
    if (pm_metal_ai_init(arena) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing);
        return fail_ai("re-init");
    }
    if (pm_metal_ai_tool_count() != 0) rc = fail_ai("count after re-init");
    else rc = 0;
    pm_metal_ai_deinit();
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return rc;
}

PM_MOD_TEST_C(pymergetic.metal.ai, case_init_deinit, case_init_deinit);
PM_MOD_TEST_C(pymergetic.metal.ai, case_tool_register_invoke, case_tool_register_invoke);
PM_MOD_TEST_C(pymergetic.metal.ai, case_tool_unregister, case_tool_unregister);
PM_MOD_TEST_C(pymergetic.metal.ai, case_tool_nth, case_tool_nth);
PM_MOD_TEST_C(pymergetic.metal.ai, case_null_reject, case_null_reject);
PM_MOD_TEST_C(pymergetic.metal.ai, case_deinit_resets_tools, case_deinit_resets_tools);