/* pymergetic.metal.ai -- AI service hub and tool registry.
 *
 * Tool table is arena-allocated, sized by a PM_UTIL_LIMIT_C knob.
 * No static BSS arrays beyond counter variables. */
#include "pymergetic/metal/ai/__exports__.h"

#include "pymergetic/metal/ai/provider/__exports__.h"
#include "pymergetic/metal/ai/session/__exports__.h"
#include "pymergetic/metal/ai/config/__exports__.h"
#include "pymergetic/metal/ai/mcp/__exports__.h"
#include "pymergetic/util/limits.h"
#include "pymergetic/util/mem.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#ifndef PM_METAL_AI_TOOL_DEFAULT
#define PM_METAL_AI_TOOL_DEFAULT 8u
#endif
#ifndef PM_METAL_AI_TOOL_HARD
#define PM_METAL_AI_TOOL_HARD 64u
#endif

static pm_util_mem_arena_t *s_arena;
static pm_metal_ai_tool_t *s_tools;
static uint32_t s_tools_cap;
static uint32_t s_tool_n;
static uint8_t s_ready;

PM_UTIL_LIMIT_C(pm_ai_limit_tools, pymergetic.metal.ai, tools,
    PM_METAL_AI_TOOL_DEFAULT, PM_METAL_AI_TOOL_HARD, &s_tool_n);

typedef struct {
    void (*chunk)(const char *, uint32_t, void *);
    void (*done)(int32_t, const char *, void *);
    void *user;
} chat_ctx_t;

static void chat_chunk_adapter(uint32_t req, const char *d, uint32_t l, void *c) {
    chat_ctx_t *ctx = (chat_ctx_t *)c;
    (void)req;
    if (ctx->chunk) ctx->chunk(d, l, ctx->user);
}

static void chat_done_adapter(uint32_t req, int32_t s, const char *e, void *c) {
    chat_ctx_t *ctx = (chat_ctx_t *)c;
    (void)req;
    if (ctx->done) ctx->done(s, e, ctx->user);
}

int32_t pm_metal_ai_init(pm_util_mem_arena_t *arena) {
    s_arena = arena;
    s_tools = NULL;
    s_tools_cap = 0;
    s_tool_n = 0;
    if (pm_metal_ai_provider_init(arena) != 0) return -1;
    if (pm_metal_ai_session_init(arena) != 0) { pm_metal_ai_provider_deinit(); return -1; }
    if (pm_metal_ai_config_init(arena) != 0) { pm_metal_ai_session_deinit(); pm_metal_ai_provider_deinit(); return -1; }
    if (pm_metal_ai_mcp_init(arena) != 0) { pm_metal_ai_config_deinit(); pm_metal_ai_session_deinit(); pm_metal_ai_provider_deinit(); return -1; }
    s_ready = 1;
    return 0;
}

void pm_metal_ai_deinit(void) {
    pm_metal_ai_mcp_deinit();
    pm_metal_ai_config_deinit();
    pm_metal_ai_session_deinit();
    pm_metal_ai_provider_deinit();
    if (s_tools != NULL) {
        pm_util_mem_free(s_arena, s_tools);
    }
    s_tools = NULL;
    s_tools_cap = 0;
    s_tool_n = 0;
    s_ready = 0;
}

int32_t pm_metal_ai_tool_register(const char *name, const char *description,
    const char *params_schema, pm_metal_ai_tool_fn handler, void *user)
{
    uint32_t i;
    pm_metal_ai_tool_t *t;
    void *grown;
    if (!name || !name[0]) return -1;
    if (!handler) return -1;
    for (i = 0; i < s_tool_n; i++) {
        if (strcmp(s_tools[i].name, name) == 0) return -3;
    }
    if (s_tool_n >= s_tools_cap) {
        if (s_arena == NULL || !PM_UTIL_LIMIT_ROOM(pm_ai_limit_tools, s_tool_n))
            return -2;
        grown = pm_util_limits_grow(s_arena, s_tools, &s_tools_cap,
            (uint32_t)sizeof(*s_tools), &pm_ai_limit_tools);
        if (grown == NULL) return -2;
        s_tools = grown;
    }
    t = &s_tools[s_tool_n];
    memset(t, 0, sizeof(*t));
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->name[sizeof(t->name) - 1] = '\0';
    if (description) {
        strncpy(t->description, description, sizeof(t->description) - 1);
        t->description[sizeof(t->description) - 1] = '\0';
    }
    if (params_schema) {
        strncpy(t->params_schema, params_schema, sizeof(t->params_schema) - 1);
        t->params_schema[sizeof(t->params_schema) - 1] = '\0';
    }
    t->handler = handler;
    t->user = user;
    s_tool_n++;
    return 0;
}

int32_t pm_metal_ai_tool_unregister(const char *name) {
    uint32_t i, j;
    if (!name || !name[0]) return -1;
    for (i = 0; i < s_tool_n; i++) {
        if (strcmp(s_tools[i].name, name) == 0) {
            for (j = i; j + 1 < s_tool_n; j++)
                memcpy(&s_tools[j], &s_tools[j + 1], sizeof(*s_tools));
            memset(&s_tools[s_tool_n - 1], 0, sizeof(*s_tools));
            s_tool_n--;
            return 0;
        }
    }
    return -1;
}

int32_t pm_metal_ai_tool_invoke(const char *name, const char *json_params,
    char *out_result, uint32_t out_size)
{
    uint32_t i;
    if (!name || !name[0]) return -1;
    for (i = 0; i < s_tool_n; i++) {
        if (strcmp(s_tools[i].name, name) == 0)
            return s_tools[i].handler(json_params, out_result, out_size, s_tools[i].user);
    }
    return -1;
}

uint32_t pm_metal_ai_tool_count(void) { return s_tool_n; }

int32_t pm_metal_ai_tool_nth(uint32_t n, pm_metal_ai_tool_t *out) {
    if (!out || n >= s_tool_n) return -1;
    memcpy(out, &s_tools[n], sizeof(*out));
    out->next = NULL;
    return 0;
}

int32_t pm_metal_ai_chat(uint32_t session_id, const char *provider_name,
    const char *content,
    void (*chunk_cb)(const char *, uint32_t, void *),
    void (*done_cb)(int32_t, const char *, void *), void *user)
{
    pm_metal_ai_provider_t prov;
    chat_ctx_t ctx;
    char body[PM_METAL_AI_PROVIDER_REQUEST_MAX];
    uint32_t rid;
    int32_t len;

    if (!s_ready || !provider_name || !content) return -1;
    if (pm_metal_ai_provider_find(provider_name, &prov) != 0) return -2;
    if (pm_metal_ai_session_append(session_id, PM_METAL_AI_ROLE_USER, content,
         (uint32_t)strlen(content)) != 0) return -1;

    len = snprintf(body, sizeof(body),
        "{\"model\":\"%s\",\"messages\":[],\"stream\":true}",
        prov.model[0] ? prov.model : "gpt-4o");
    if (len < 0 || (uint32_t)len >= sizeof(body)) return -1;

    ctx.chunk = chunk_cb;
    ctx.done = done_cb;
    ctx.user = user;
    if (pm_metal_ai_provider_send(provider_name, prov.model, body,
         (uint32_t)len, chat_chunk_adapter, chat_done_adapter, &ctx, &rid) != 0)
        return -3;
    return 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.ai, pm_metal_ai_init, pm_metal_ai_init,
    int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.ai, pm_metal_ai_deinit, pm_metal_ai_deinit,
    void(void));
PM_MOD_EXPORT_C(pymergetic.metal.ai, pm_metal_ai_chat, pm_metal_ai_chat,
    int32_t(uint32_t, const char *, const char *,
    void (*)(const char *, uint32_t, void *),
    void (*)(int32_t, const char *, void *), void *));
PM_MOD_EXPORT_C(pymergetic.metal.ai, pm_metal_ai_tool_register,
    pm_metal_ai_tool_register,
    int32_t(const char *, const char *, const char *,
    pm_metal_ai_tool_fn, void *));
PM_MOD_EXPORT_C(pymergetic.metal.ai, pm_metal_ai_tool_unregister,
    pm_metal_ai_tool_unregister, int32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.metal.ai, pm_metal_ai_tool_invoke,
    pm_metal_ai_tool_invoke,
    int32_t(const char *, const char *, char *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.ai, pm_metal_ai_tool_count,
    pm_metal_ai_tool_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.ai, pm_metal_ai_tool_nth,
    pm_metal_ai_tool_nth, int32_t(uint32_t, pm_metal_ai_tool_t *));