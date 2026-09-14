/* pymergetic.metal.ai.session -- prove the session card.
 *
 * Each test creates its own arena so init/deinit are tested in
 * isolation — the session card now uses arena allocation. */
#include "pymergetic/metal/ai/session/__exports__.h"
#include "pymergetic/wasmmod/guest.h"
#include "pymergetic/util/mem.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TST_SPAN (256u * 1024u)

static int32_t fail_p(const char *why) {
    fprintf(stderr, "metal.ai.session test: %s\n", why);
    return 1;
}

static int32_t with_init(int32_t (*body)(void)) {
    void *backing = malloc(TST_SPAN);
    pm_util_mem_arena_t *arena;
    int32_t rc;
    if (backing == NULL) return fail_p("malloc");
    arena = pm_util_mem_arena_create(backing, TST_SPAN);
    if (arena == NULL) { free(backing); return fail_p("arena-create"); }
    if (pm_metal_ai_session_init(arena) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing);
        return fail_p("init");
    }
    rc = body();
    pm_metal_ai_session_deinit();
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return rc;
}

/* 1: create one session, verify count=1, close, verify count=0 */
static int32_t body_create_close(void) {
    uint32_t id;
    pm_metal_ai_session_t out;
    if (pm_metal_ai_session_create("test", &id) != 0) return fail_p("create");
    if (pm_metal_ai_session_count() != 1) return fail_p("count != 1");
    if (pm_metal_ai_session_find(id, &out) != 0) return fail_p("find");
    if (pm_metal_ai_session_close(id) != 0) return fail_p("close");
    if (pm_metal_ai_session_count() != 0) return fail_p("count != 0 after close");
    return 0;
}

static int32_t case_session_create_close(void) {
    return with_init(body_create_close);
}

/* 2: append a message, verify count, read it back */
static int32_t body_append(void) {
    uint32_t id;
    uint32_t mc;
    pm_metal_ai_message_t msg;
    if (pm_metal_ai_session_create("append-test", &id) != 0) return fail_p("create");
    if (pm_metal_ai_session_append(id, PM_METAL_AI_ROLE_USER,
            "hello world", 11) != 0) return fail_p("append");
    if (pm_metal_ai_session_message_count(id, &mc) != 0) return fail_p("message_count");
    if (mc != 1) return fail_p("message count != 1");
    if (pm_metal_ai_session_message_nth(id, 0, &msg) != 0) return fail_p("message_nth");
    if (msg.role != PM_METAL_AI_ROLE_USER) return fail_p("wrong role");
    if (msg.content_len != 11) return fail_p("wrong content_len");
    if (memcmp(msg.content, "hello world", 11) != 0) return fail_p("wrong content");
    return 0;
}

static int32_t case_session_append(void) {
    return with_init(body_append);
}

/* 3: append 3 messages, truncate to 1, verify count=1 */
static int32_t body_truncate(void) {
    uint32_t id;
    uint32_t mc;
    if (pm_metal_ai_session_create("trunc-test", &id) != 0) return fail_p("create");
    if (pm_metal_ai_session_append(id, PM_METAL_AI_ROLE_USER, "msg0", 4) != 0)
        return fail_p("append 0");
    if (pm_metal_ai_session_append(id, PM_METAL_AI_ROLE_ASSISTANT, "msg1", 4) != 0)
        return fail_p("append 1");
    if (pm_metal_ai_session_append(id, PM_METAL_AI_ROLE_SYSTEM, "msg2", 4) != 0)
        return fail_p("append 2");
    if (pm_metal_ai_session_message_count(id, &mc) != 0) return fail_p("count before truncate");
    if (mc != 3) return fail_p("pre-truncate count");
    if (pm_metal_ai_session_truncate(id, 1u) != 0) return fail_p("truncate");
    if (pm_metal_ai_session_message_count(id, &mc) != 0) return fail_p("count after truncate");
    if (mc != 1) return fail_p("count after truncate != 1");
    return 0;
}

static int32_t case_session_truncate(void) {
    return with_init(body_truncate);
}

/* 4: NULL/empty name rejection on create */
static int32_t body_null_reject(void) {
    uint32_t id;
    if (pm_metal_ai_session_create(NULL, &id) != -1) return fail_p("create NULL name");
    if (pm_metal_ai_session_create("", &id) != -1) return fail_p("create empty name");
    return 0;
}

static int32_t case_null_reject(void) {
    return with_init(body_null_reject);
}

/* 5: load and save return 0 */
static int32_t body_load_save(void) {
    if (pm_metal_ai_session_load() != 0) return fail_p("load != 0");
    if (pm_metal_ai_session_save() != 0) return fail_p("save != 0");
    return 0;
}

static int32_t case_load_save(void) {
    return with_init(body_load_save);
}

PM_MOD_TEST_C(pymergetic.metal.ai.session, case_session_create_close,
    case_session_create_close);
PM_MOD_TEST_C(pymergetic.metal.ai.session, case_session_append,
    case_session_append);
PM_MOD_TEST_C(pymergetic.metal.ai.session, case_session_truncate,
    case_session_truncate);
PM_MOD_TEST_C(pymergetic.metal.ai.session, case_null_reject,
    case_null_reject);
PM_MOD_TEST_C(pymergetic.metal.ai.session, case_load_save,
    case_load_save);