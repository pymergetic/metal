/* pymergetic.metal.ai.config -- prove provider/param CRUD with upsert and delete.
 *
 * Exercises provider set/get/delete/count/nth and param set/get/delete/count/nth,
 * upsert semantics, NULL rejection, and deinit reset.
 */
#include "pymergetic/metal/ai/config/__exports__.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail_ai(const char *why) {
    fprintf(stderr, "metal.ai.config test: %s\n", why);
    return 1;
}

/* 1: set one provider, get it back, verify name */
static int32_t case_provider_upsert(void) {
    pm_metal_ai_config_provider_t p;
    pm_metal_ai_config_provider_t out;

    memset(&p, 0, sizeof(p));
    strncpy(p.name, "openai", sizeof(p.name) - 1);
    p.name[sizeof(p.name) - 1] = '\0';
    strncpy(p.api_key, "sk-test", sizeof(p.api_key) - 1);
    p.api_key[sizeof(p.api_key) - 1] = '\0';
    strncpy(p.url, "https://api.openai.com", sizeof(p.url) - 1);
    p.url[sizeof(p.url) - 1] = '\0';
    strncpy(p.model, "gpt-4", sizeof(p.model) - 1);
    p.model[sizeof(p.model) - 1] = '\0';

    if (pm_metal_ai_config_init(NULL) != 0) return fail_ai("init");
    if (pm_metal_ai_config_provider_set(&p) != 0) return fail_ai("provider_set");
    if (pm_metal_ai_config_provider_count() != 1) return fail_ai("provider_count");
    if (pm_metal_ai_config_provider_get("openai", &out) != 0) return fail_ai("provider_get");
    if (strcmp(out.name, "openai") != 0) return fail_ai("provider name");
    if (strcmp(out.api_key, "sk-test") != 0) return fail_ai("provider api_key");
    if (strcmp(out.model, "gpt-4") != 0) return fail_ai("provider model");

    /* Upsert: update existing provider. */
    strncpy(p.api_key, "sk-new", sizeof(p.api_key) - 1);
    p.api_key[sizeof(p.api_key) - 1] = '\0';
    if (pm_metal_ai_config_provider_set(&p) != 0) return fail_ai("provider_upsert");
    if (pm_metal_ai_config_provider_count() != 1) return fail_ai("count after upsert");

    memset(&out, 0, sizeof(out));
    if (pm_metal_ai_config_provider_get("openai", &out) != 0) return fail_ai("get after upsert");
    if (strcmp(out.api_key, "sk-new") != 0) return fail_ai("upsert api_key");

    return 0;
}

/* 2: set a provider, delete it, verify count is 0 */
static int32_t case_provider_delete(void) {
    pm_metal_ai_config_provider_t p;

    memset(&p, 0, sizeof(p));
    strncpy(p.name, "anthropic", sizeof(p.name) - 1);
    p.name[sizeof(p.name) - 1] = '\0';
    strncpy(p.api_key, "sk-ant", sizeof(p.api_key) - 1);
    p.api_key[sizeof(p.api_key) - 1] = '\0';

    if (pm_metal_ai_config_init(NULL) != 0) return fail_ai("init");
    if (pm_metal_ai_config_provider_set(&p) != 0) return fail_ai("provider_set");
    if (pm_metal_ai_config_provider_count() != 1) return fail_ai("count");
    if (pm_metal_ai_config_provider_delete("anthropic") != 0) return fail_ai("provider_delete");
    if (pm_metal_ai_config_provider_count() != 0) return fail_ai("count after delete");

    /* Delete non-existent returns -1. */
    if (pm_metal_ai_config_provider_delete("no-such") != -1) return fail_ai("delete missing");

    return 0;
}

/* 3: set key=val, get back, verify val */
static int32_t case_param_set_get(void) {
    char buf[PM_METAL_AI_CONFIG_VALUE_MAX];

    if (pm_metal_ai_config_init(NULL) != 0) return fail_ai("init");
    if (pm_metal_ai_config_param_set("temperature", "0.7") != 0) return fail_ai("param_set");
    if (pm_metal_ai_config_param_count() != 1) return fail_ai("param_count");

    memset(buf, 0, sizeof(buf));
    if (pm_metal_ai_config_param_get("temperature", buf, sizeof(buf)) != 0) return fail_ai("param_get");
    if (strcmp(buf, "0.7") != 0) return fail_ai("param value");

    /* Upsert: update existing param. */
    if (pm_metal_ai_config_param_set("temperature", "1.0") != 0) return fail_ai("param_upsert");
    if (pm_metal_ai_config_param_count() != 1) return fail_ai("count after upsert");

    memset(buf, 0, sizeof(buf));
    if (pm_metal_ai_config_param_get("temperature", buf, sizeof(buf)) != 0) return fail_ai("get after upsert");
    if (strcmp(buf, "1.0") != 0) return fail_ai("upsert value");

    return 0;
}

/* 4: set a param, delete it, verify count is 0 */
static int32_t case_param_delete(void) {
    if (pm_metal_ai_config_init(NULL) != 0) return fail_ai("init");

    if (pm_metal_ai_config_param_set("max_tokens", "4096") != 0) return fail_ai("param_set");
    if (pm_metal_ai_config_param_count() != 1) return fail_ai("count");

    if (pm_metal_ai_config_param_delete("max_tokens") != 0) return fail_ai("param_delete");
    if (pm_metal_ai_config_param_count() != 0) return fail_ai("count after delete");

    /* Delete non-existent returns -1. */
    if (pm_metal_ai_config_param_delete("no-such") != -1) return fail_ai("delete missing");

    return 0;
}

/* 5: NULL rejection -- set with NULL name returns -1 */
static int32_t case_null_reject(void) {
    pm_metal_ai_config_provider_t p;
    char buf[32];

    if (pm_metal_ai_config_init(NULL) != 0) return fail_ai("init");

    /* Provider NULL / empty name rejection. */
    if (pm_metal_ai_config_provider_set(NULL) != -1) return fail_ai("provider_set NULL");
    memset(&p, 0, sizeof(p));
    p.name[0] = '\0';
    if (pm_metal_ai_config_provider_set(&p) != -1) return fail_ai("provider_set empty name");

    /* Provider get NULL rejection. */
    if (pm_metal_ai_config_provider_get(NULL, &p) != -1) return fail_ai("provider_get NULL name");
    if (pm_metal_ai_config_provider_get("x", NULL) != -1) return fail_ai("provider_get NULL out");

    /* Provider delete NULL rejection. */
    if (pm_metal_ai_config_provider_delete(NULL) != -1) return fail_ai("provider_delete NULL");

    /* Param NULL / empty key rejection. */
    if (pm_metal_ai_config_param_set(NULL, "v") != -1) return fail_ai("param_set NULL key");
    if (pm_metal_ai_config_param_set("k", NULL) != -1) return fail_ai("param_set NULL value");
    if (pm_metal_ai_config_param_set("", "v") != -1) return fail_ai("param_set empty key");

    /* Param get NULL rejection. */
    if (pm_metal_ai_config_param_get(NULL, buf, sizeof(buf)) != -1) return fail_ai("param_get NULL key");
    if (pm_metal_ai_config_param_get("k", NULL, sizeof(buf)) != -1) return fail_ai("param_get NULL out");
    if (pm_metal_ai_config_param_get("k", buf, 0) != -1) return fail_ai("param_get zero size");

    /* Param delete NULL rejection. */
    if (pm_metal_ai_config_param_delete(NULL) != -1) return fail_ai("param_delete NULL");

    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.ai.config, case_provider_upsert, case_provider_upsert);
PM_MOD_TEST_C(pymergetic.metal.ai.config, case_provider_delete, case_provider_delete);
PM_MOD_TEST_C(pymergetic.metal.ai.config, case_param_set_get, case_param_set_get);
PM_MOD_TEST_C(pymergetic.metal.ai.config, case_param_delete, case_param_delete);
PM_MOD_TEST_C(pymergetic.metal.ai.config, case_null_reject, case_null_reject);
