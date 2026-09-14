/* pymergetic.metal.ai.provider -- border prove.
 *
 * Exercises add/find/remove/count/nth lifecycle, edge cases, NULL rejection,
 * and the send stub without requiring a running network. */
#include "pymergetic/metal/ai/provider/__exports__.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail_p(const char *why) {
    fprintf(stderr, "metal.ai.provider test: %s\n", why);
    return 1;
}

/* 1: add one provider, find it, verify name */
static int32_t case_provider_add_find(void) {
    pm_metal_ai_provider_t p;
    pm_metal_ai_provider_t out;

    if (pm_metal_ai_provider_init(NULL) != 0) return fail_p("init");
    if (pm_metal_ai_provider_count() != 0) return fail_p("count after init");

    memset(&p, 0, sizeof(p));
    strncpy(p.name, "openai", sizeof(p.name) - 1);
    strncpy(p.url, "https://api.openai.com", sizeof(p.url) - 1);
    strncpy(p.api_key, "sk-test", sizeof(p.api_key) - 1);
    strncpy(p.model, "gpt-4", sizeof(p.model) - 1);

    if (pm_metal_ai_provider_add(&p) != 0) return fail_p("add openai");
    if (pm_metal_ai_provider_count() != 1) return fail_p("count after add");

    memset(&out, 0, sizeof(out));
    if (pm_metal_ai_provider_find("openai", &out) != 0) return fail_p("find openai");
    if (strcmp(out.name, "openai") != 0) return fail_p("provider name mismatch");
    if (strcmp(out.url, "https://api.openai.com") != 0) return fail_p("provider url mismatch");

    /* nth(0) should match find. */
    memset(&out, 0, sizeof(out));
    if (pm_metal_ai_provider_nth(0, &out) != 0) return fail_p("nth 0");
    if (strcmp(out.name, "openai") != 0) return fail_p("nth name mismatch");

    /* nth out of bounds. */
    if (pm_metal_ai_provider_nth(1, &out) != -1) return fail_p("nth out of bounds not rejected");
    if (pm_metal_ai_provider_nth(2, NULL) != -1) return fail_p("nth NULL out not rejected");

    /* find nonexistent. */
    if (pm_metal_ai_provider_find("nonexistent", &out) != -1) return fail_p("find nonexistent not rejected");

    /* find NULL name. */
    if (pm_metal_ai_provider_find(NULL, &out) != -1) return fail_p("find NULL name not rejected");

    /* find NULL out. */
    if (pm_metal_ai_provider_find("openai", NULL) != -1) return fail_p("find NULL out not rejected");

    pm_metal_ai_provider_deinit();
    return 0;
}

/* 2: add one provider, remove it, verify count becomes 0 */
static int32_t case_provider_remove(void) {
    pm_metal_ai_provider_t p;
    pm_metal_ai_provider_t out;

    if (pm_metal_ai_provider_init(NULL) != 0) return fail_p("init");

    memset(&p, 0, sizeof(p));
    strncpy(p.name, "anthropic", sizeof(p.name) - 1);
    strncpy(p.url, "https://api.anthropic.com", sizeof(p.url) - 1);
    strncpy(p.api_key, "sk-ant", sizeof(p.api_key) - 1);

    if (pm_metal_ai_provider_add(&p) != 0) return fail_p("add anthropic");
    if (pm_metal_ai_provider_count() != 1) return fail_p("count after add");

    if (pm_metal_ai_provider_remove("anthropic") != 0) return fail_p("remove anthropic");
    if (pm_metal_ai_provider_count() != 0) return fail_p("count after remove");

    /* Remove nonexistent. */
    if (pm_metal_ai_provider_remove("nonexistent") != -1) return fail_p("remove nonexistent not rejected");

    /* Remove NULL. */
    if (pm_metal_ai_provider_remove(NULL) != -1) return fail_p("remove NULL not rejected");

    /* find after remove should fail. */
    if (pm_metal_ai_provider_find("anthropic", &out) != -1) return fail_p("find after remove");

    pm_metal_ai_provider_deinit();
    return 0;
}

/* 3: add with NULL name returns -1 */
static int32_t case_null_reject(void) {
    pm_metal_ai_provider_t p;

    if (pm_metal_ai_provider_init(NULL) != 0) return fail_p("init");

    /* NULL provider pointer. */
    if (pm_metal_ai_provider_add(NULL) != -1) return fail_p("add NULL provider not rejected");

    /* Empty name. */
    memset(&p, 0, sizeof(p));
    if (pm_metal_ai_provider_add(&p) != -1) return fail_p("add empty name not rejected");

    pm_metal_ai_provider_deinit();
    return 0;
}

/* 4: call send, verify it returns 0 */
static int32_t case_send(void) {
    uint32_t req_id;
    int32_t rc;

    if (pm_metal_ai_provider_init(NULL) != 0) return fail_p("init");

    rc = pm_metal_ai_provider_send("openai",
        "You are a helpful assistant.",
        "What is the capital of France?",
        1024, NULL, NULL, NULL, &req_id);
    if (rc != 0) return fail_p("send returned non-zero");
    if (req_id == 0) return fail_p("request id was zero");

    /* send without out_request_id should work too. */
    rc = pm_metal_ai_provider_send("openai",
        NULL, NULL, 0, NULL, NULL, NULL, NULL);
    if (rc != 0) return fail_p("send without out_request_id failed");

    pm_metal_ai_provider_deinit();
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.ai.provider, case_provider_add_find, case_provider_add_find);
PM_MOD_TEST_C(pymergetic.metal.ai.provider, case_provider_remove, case_provider_remove);
PM_MOD_TEST_C(pymergetic.metal.ai.provider, case_null_reject, case_null_reject);
PM_MOD_TEST_C(pymergetic.metal.ai.provider, case_send, case_send);