/* pymergetic.metal.ai.provider -- AI model provider management.
 *
 * Stores providers in a static array (PM_UTIL_LIMIT_C, max 8). One provider
 * per name (OpenAI-compatible HTTP proxy). Holds API endpoint, API key, model
 * name, and runtime state for inflight SSE streams.
 *
 * All storage is static -- no arena dependency after init.
 */
#include "pymergetic/metal/ai/provider/__exports__.h"

#include "pymergetic/util/limits.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#ifndef PM_METAL_AI_PROVIDER_MAX
#define PM_METAL_AI_PROVIDER_MAX 8u
#endif

/* One provider slot. */
struct ai_provider_slot {
    pm_metal_ai_provider_t provider;
    uint8_t active; /* 1 when occupied */
};

static struct ai_provider_slot s_providers[PM_METAL_AI_PROVIDER_MAX];
static uint32_t s_count;
static uint32_t s_next_request_id = 1;

PM_UTIL_LIMIT_C(pm_ai_provider_limit, pymergetic.metal.ai.provider, providers,
    PM_METAL_AI_PROVIDER_MAX, 0u, &s_count);

/* ---------------- lifecycle ---------------- */
int32_t pm_metal_ai_provider_init(pm_util_mem_arena_t *arena) {
    (void)arena;
    s_count = 0;
    memset(s_providers, 0, sizeof(s_providers));
    return 0;
}

void pm_metal_ai_provider_deinit(void) {
    s_count = 0;
    memset(s_providers, 0, sizeof(s_providers));
}

/* Find a provider slot by name, returning its index or -1. */
static int32_t find_provider(const char *name) {
    uint32_t i;
    if (name == NULL) {
        return -1;
    }
    for (i = 0; i < s_count; i++) {
        if (s_providers[i].active && strcmp(s_providers[i].provider.name, name) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

/* ---------------- add ---------------- */
int32_t pm_metal_ai_provider_add(const pm_metal_ai_provider_t *provider) {
    int32_t slot;
    if (provider == NULL || provider->name[0] == '\0') {
        return -1;
    }
    /* Already exists? Replace. */
    slot = find_provider(provider->name);
    if (slot >= 0) {
        s_providers[slot].provider = *provider;
        return 0;
    }
    if (s_count >= PM_METAL_AI_PROVIDER_MAX) {
        return -1;
    }
    s_providers[s_count].provider = *provider;
    s_providers[s_count].active = 1;
    s_count++;
    return 0;
}

/* ---------------- find ---------------- */
int32_t pm_metal_ai_provider_find(const char *name, pm_metal_ai_provider_t *out) {
    int32_t slot;
    if (name == NULL || out == NULL) {
        return -1;
    }
    slot = find_provider(name);
    if (slot < 0) {
        return -1;
    }
    *out = s_providers[slot].provider;
    return 0;
}

/* ---------------- remove ---------------- */
int32_t pm_metal_ai_provider_remove(const char *name) {
    int32_t slot;
    if (name == NULL) {
        return -1;
    }
    slot = find_provider(name);
    if (slot < 0) {
        return -1;
    }
    s_providers[slot].active = 0;
    /* Compact: swap with last. */
    s_count--;
    if (slot < (int32_t)s_count) {
        s_providers[slot] = s_providers[s_count];
    }
    memset(&s_providers[s_count], 0, sizeof(s_providers[s_count]));
    return 0;
}

/* ---------------- count ---------------- */
uint32_t pm_metal_ai_provider_count(void) {
    return s_count;
}

/* ---------------- nth ---------------- */
int32_t pm_metal_ai_provider_nth(uint32_t idx, pm_metal_ai_provider_t *out) {
    if (out == NULL) {
        return -1;
    }
    if (idx >= s_count) {
        return -1;
    }
    *out = s_providers[idx].provider;
    return 0;
}

/* ---------------- send (stub) ---------------- */
int32_t pm_metal_ai_provider_send(const char *provider_name,
    const char *system_prompt, const char *user_prompt,
    uint32_t max_tokens,
    pm_metal_ai_provider_chunk_fn chunk_fn,
    pm_metal_ai_provider_done_fn done_fn,
    void *user, uint32_t *out_request_id) {
    uint32_t req_id;
    (void)provider_name;
    (void)system_prompt;
    (void)user_prompt;
    (void)max_tokens;
    (void)chunk_fn;
    req_id = s_next_request_id++;
    if (out_request_id != NULL) {
        *out_request_id = req_id;
    }
    if (done_fn != NULL) {
        done_fn(req_id, 0, "", user);
    }
    return 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.ai.provider, pm_metal_ai_provider_init, pm_metal_ai_provider_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.provider, pm_metal_ai_provider_deinit, pm_metal_ai_provider_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.ai.provider, pm_metal_ai_provider_add, pm_metal_ai_provider_add, int32_t(const pm_metal_ai_provider_t *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.provider, pm_metal_ai_provider_find, pm_metal_ai_provider_find, int32_t(const char *, pm_metal_ai_provider_t *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.provider, pm_metal_ai_provider_remove, pm_metal_ai_provider_remove, int32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.provider, pm_metal_ai_provider_count, pm_metal_ai_provider_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.ai.provider, pm_metal_ai_provider_nth, pm_metal_ai_provider_nth, int32_t(uint32_t, pm_metal_ai_provider_t *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.provider, pm_metal_ai_provider_send, pm_metal_ai_provider_send, int32_t(const char *, const char *, const char *, uint32_t, pm_metal_ai_provider_chunk_fn, pm_metal_ai_provider_done_fn, void *, uint32_t *));