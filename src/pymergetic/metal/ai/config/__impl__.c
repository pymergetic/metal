/* pymergetic.metal.ai.config -- AI provider and generic parameter configuration.
 *
 * Two-level key-value store: provider configs (api_key, url, model)
 * and generic key-value parameters. Persistence stubs (load/save).
 */
#include "pymergetic/metal/ai/config/__exports__.h"

#include "pymergetic/util/limits.h"

#include <stdint.h>
#include <string.h>

#ifndef PM_METAL_AI_CONFIG_PROVIDERS_MAX
#define PM_METAL_AI_CONFIG_PROVIDERS_MAX 8u
#endif

#ifndef PM_METAL_AI_CONFIG_PARAMS_MAX
#define PM_METAL_AI_CONFIG_PARAMS_MAX 32u
#endif

static pm_metal_ai_config_provider_t s_providers[PM_METAL_AI_CONFIG_PROVIDERS_MAX];
static uint32_t s_provider_n;

static pm_metal_ai_config_param_t s_params[PM_METAL_AI_CONFIG_PARAMS_MAX];
static uint32_t s_param_n;

PM_UTIL_LIMIT_C(pm_ai_config_providers_limit, pymergetic.metal.ai.config,
    config_providers, PM_METAL_AI_CONFIG_PROVIDERS_MAX, 0u, &s_provider_n);
PM_UTIL_LIMIT_C(pm_ai_config_params_limit, pymergetic.metal.ai.config,
    config_params, PM_METAL_AI_CONFIG_PARAMS_MAX, 0u, &s_param_n);

int32_t pm_metal_ai_config_init(pm_util_mem_arena_t *arena) {
    (void)arena;
    s_provider_n = 0;
    memset(s_providers, 0, sizeof(s_providers));
    s_param_n = 0;
    memset(s_params, 0, sizeof(s_params));
    return 0;
}

void pm_metal_ai_config_deinit(void) {
    s_provider_n = 0;
    memset(s_providers, 0, sizeof(s_providers));
    s_param_n = 0;
    memset(s_params, 0, sizeof(s_params));
}

static int32_t find_provider(const char *name) {
    uint32_t i;
    if (name == NULL) return -1;
    for (i = 0; i < s_provider_n; i++) {
        if (strcmp(s_providers[i].name, name) == 0) return (int32_t)i;
    }
    return -1;
}

static int32_t find_param(const char *key) {
    uint32_t i;
    if (key == NULL) return -1;
    for (i = 0; i < s_param_n; i++) {
        if (strcmp(s_params[i].key, key) == 0) return (int32_t)i;
    }
    return -1;
}

int32_t pm_metal_ai_config_provider_set(const pm_metal_ai_config_provider_t *cfg) {
    int32_t idx;
    if (cfg == NULL || cfg->name[0] == '\0') return -1;
    idx = find_provider(cfg->name);
    if (idx >= 0) {
        memcpy(&s_providers[idx], cfg, sizeof(pm_metal_ai_config_provider_t));
        return 0;
    }
    if (s_provider_n >= PM_METAL_AI_CONFIG_PROVIDERS_MAX) return -2;
    memcpy(&s_providers[s_provider_n], cfg, sizeof(pm_metal_ai_config_provider_t));
    s_provider_n++;
    return 0;
}

int32_t pm_metal_ai_config_provider_get(const char *name,
    pm_metal_ai_config_provider_t *out) {
    int32_t idx;
    if (name == NULL || out == NULL) return -1;
    idx = find_provider(name);
    if (idx < 0) return -2;
    memcpy(out, &s_providers[idx], sizeof(pm_metal_ai_config_provider_t));
    return 0;
}

int32_t pm_metal_ai_config_provider_delete(const char *name) {
    int32_t idx = find_provider(name);
    if (idx < 0) return -1;
    if ((uint32_t)idx + 1 < s_provider_n) {
        memmove(&s_providers[idx], &s_providers[idx + 1],
            (s_provider_n - (uint32_t)idx - 1)
            * sizeof(pm_metal_ai_config_provider_t));
    }
    s_provider_n--;
    memset(&s_providers[s_provider_n], 0, sizeof(pm_metal_ai_config_provider_t));
    return 0;
}

uint32_t pm_metal_ai_config_provider_count(void) {
    return s_provider_n;
}

int32_t pm_metal_ai_config_provider_nth(uint32_t n,
    pm_metal_ai_config_provider_t *out) {
    if (out == NULL || n >= s_provider_n) return -1;
    memcpy(out, &s_providers[n], sizeof(pm_metal_ai_config_provider_t));
    return 0;
}

int32_t pm_metal_ai_config_param_set(const char *key, const char *value) {
    int32_t idx;
    if (key == NULL || key[0] == '\0' || value == NULL) return -1;
    idx = find_param(key);
    if (idx >= 0) {
        strncpy(s_params[idx].value, value,
            sizeof(s_params[idx].value) - 1);
        s_params[idx].value[sizeof(s_params[idx].value) - 1] = '\0';
        return 0;
    }
    if (s_param_n >= PM_METAL_AI_CONFIG_PARAMS_MAX) return -2;
    strncpy(s_params[s_param_n].key, key,
        sizeof(s_params[s_param_n].key) - 1);
    s_params[s_param_n].key[sizeof(s_params[s_param_n].key) - 1] = '\0';
    strncpy(s_params[s_param_n].value, value,
        sizeof(s_params[s_param_n].value) - 1);
    s_params[s_param_n].value[sizeof(s_params[s_param_n].value) - 1] = '\0';
    s_param_n++;
    return 0;
}

int32_t pm_metal_ai_config_param_get(const char *key, char *out,
    uint32_t out_size) {
    int32_t idx;
    if (key == NULL || out == NULL || out_size == 0) return -1;
    idx = find_param(key);
    if (idx < 0) return -2;
    strncpy(out, s_params[idx].value, out_size - 1);
    out[out_size - 1] = '\0';
    return 0;
}

int32_t pm_metal_ai_config_param_delete(const char *key) {
    int32_t idx = find_param(key);
    if (idx < 0) return -1;
    if ((uint32_t)idx + 1 < s_param_n) {
        memmove(&s_params[idx], &s_params[idx + 1],
            (s_param_n - (uint32_t)idx - 1)
            * sizeof(pm_metal_ai_config_param_t));
    }
    s_param_n--;
    memset(&s_params[s_param_n], 0, sizeof(pm_metal_ai_config_param_t));
    return 0;
}

uint32_t pm_metal_ai_config_param_count(void) {
    return s_param_n;
}

int32_t pm_metal_ai_config_param_nth(uint32_t n,
    pm_metal_ai_config_param_t *out) {
    if (out == NULL || n >= s_param_n) return -1;
    memcpy(out, &s_params[n], sizeof(pm_metal_ai_config_param_t));
    return 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.ai.config, pm_metal_ai_config_init,
    pm_metal_ai_config_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.config, pm_metal_ai_config_deinit,
    pm_metal_ai_config_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.ai.config, pm_metal_ai_config_provider_set,
    pm_metal_ai_config_provider_set,
    int32_t(const pm_metal_ai_config_provider_t *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.config, pm_metal_ai_config_provider_get,
    pm_metal_ai_config_provider_get,
    int32_t(const char *, pm_metal_ai_config_provider_t *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.config, pm_metal_ai_config_provider_delete,
    pm_metal_ai_config_provider_delete, int32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.config, pm_metal_ai_config_provider_count,
    pm_metal_ai_config_provider_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.ai.config, pm_metal_ai_config_provider_nth,
    pm_metal_ai_config_provider_nth,
    int32_t(uint32_t, pm_metal_ai_config_provider_t *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.config, pm_metal_ai_config_param_set,
    pm_metal_ai_config_param_set, int32_t(const char *, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.config, pm_metal_ai_config_param_get,
    pm_metal_ai_config_param_get, int32_t(const char *, char *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.ai.config, pm_metal_ai_config_param_delete,
    pm_metal_ai_config_param_delete, int32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.config, pm_metal_ai_config_param_count,
    pm_metal_ai_config_param_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.ai.config, pm_metal_ai_config_param_nth,
    pm_metal_ai_config_param_nth,
    int32_t(uint32_t, pm_metal_ai_config_param_t *));