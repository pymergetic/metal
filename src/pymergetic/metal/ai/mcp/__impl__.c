/* pymergetic.metal.ai.mcp -- Model Context Protocol client.
 *
 * JSON-RPC 2.0 client over stdio or SSE transport.
 * Manages up to 4 MCP servers; each one maintains a set of
 * discovered tools. discover/discover_all are stubs -- protocol
 * implementation follows in a future change.
 */
#include "pymergetic/metal/ai/mcp/__exports__.h"

#include "pymergetic/util/limits.h"

#include <stdint.h>
#include <string.h>

#ifndef PM_METAL_AI_MCP_SERVERS_MAX
#define PM_METAL_AI_MCP_SERVERS_MAX 4u
#endif

/* ---------------- server slot --------------- */
typedef struct {
    pm_metal_ai_mcp_server_t cfg;
    int32_t status; /* 0=starting, 1=ready, -1=dead */
    uint8_t active;
} server_slot_t;

static server_slot_t s_servers[PM_METAL_AI_MCP_SERVERS_MAX];
static uint32_t s_server_count; /* number of active servers */

PM_UTIL_LIMIT_C(pm_mcp_servers_limit, pymergetic.metal.ai.mcp, mcp_servers,
    PM_METAL_AI_MCP_SERVERS_MAX, 0u, &s_server_count);
PM_UTIL_LIMIT_C(pm_mcp_tools_limit, pymergetic.metal.ai.mcp, mcp_tools_per_server,
    32u, 0u, NULL);
PM_UTIL_LIMIT_C(pm_mcp_result_limit, pymergetic.metal.ai.mcp, mcp_result_bytes,
    262144u, 0u, NULL);

/* ---------------- find server by name --------------- */
static int32_t fnd_server(const char *name) {
    uint32_t i;
    if (name == NULL) {
        return -1;
    }
    for (i = 0; i < PM_METAL_AI_MCP_SERVERS_MAX; i++) {
        if (s_servers[i].active && strcmp(s_servers[i].cfg.name, name) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

/* ---------------- lifecycle --------------- */
int32_t pm_metal_ai_mcp_init(pm_util_mem_arena_t *arena) {
    (void)arena;
    s_server_count = 0;
    memset(s_servers, 0, sizeof(s_servers));
    return 0;
}

void pm_metal_ai_mcp_deinit(void) {
    s_server_count = 0;
    memset(s_servers, 0, sizeof(s_servers));
}

/* ---------------- server start --------------- */
int32_t pm_metal_ai_mcp_server_start(const pm_metal_ai_mcp_server_t *cfg) {
    uint32_t i;

    if (cfg == NULL || cfg->name[0] == '\0') {
        return -1;
    }

    /* Reject duplicate name. */
    if (fnd_server(cfg->name) >= 0) {
        return -2;
    }

    /* Find an empty slot. */
    for (i = 0; i < PM_METAL_AI_MCP_SERVERS_MAX; i++) {
        if (!s_servers[i].active) {
            memcpy(&s_servers[i].cfg, cfg, sizeof(pm_metal_ai_mcp_server_t));
            s_servers[i].cfg.name[sizeof(s_servers[i].cfg.name) - 1] = '\0';
            s_servers[i].status = 0; /* starting */
            s_servers[i].active = 1;
            s_server_count++;
            return (int32_t)i;
        }
    }

    return -3; /* no free slots */
}

/* ---------------- server stop --------------- */
int32_t pm_metal_ai_mcp_server_stop(const char *name) {
    int32_t idx = fnd_server(name);
    if (idx < 0) return -1;

    s_servers[idx].status = -1;
    s_servers[idx].active = 0;
    memset(&s_servers[idx].cfg, 0, sizeof(s_servers[idx].cfg));
    s_server_count--;
    return 0;
}

/* ---------------- server status --------------- */
int32_t pm_metal_ai_mcp_server_status(const char *name) {
    int32_t idx = fnd_server(name);
    if (idx < 0) return -1;
    return s_servers[idx].status;
}

/* ---------------- discover (stubs) --------------- */
int32_t pm_metal_ai_mcp_discover(const char *name) {
    int32_t idx = fnd_server(name);
    (void)idx;
    return 0;
}

int32_t pm_metal_ai_mcp_discover_all(void) {
    return 0;
}

/* ---------------- call tool (stub) --------------- */
int32_t pm_metal_ai_mcp_call_tool(const char *server, const char *tool,
    const char *args, char *result, uint32_t result_max) {
    (void)server;
    (void)tool;
    (void)args;
    (void)result;
    (void)result_max;
    return 0;
}

/* ---------------- server count / nth --------------- */
uint32_t pm_metal_ai_mcp_server_count(void) {
    return s_server_count;
}

int32_t pm_metal_ai_mcp_server_nth(uint32_t n, pm_metal_ai_mcp_server_t *out) {
    uint32_t i;
    uint32_t active_idx = 0;

    if (out == NULL) return -1;

    for (i = 0; i < PM_METAL_AI_MCP_SERVERS_MAX; i++) {
        if (s_servers[i].active) {
            if (active_idx == n) {
                memcpy(out, &s_servers[i].cfg, sizeof(pm_metal_ai_mcp_server_t));
                return 0;
            }
            active_idx++;
        }
    }

    return -1; /* n past end */
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.ai.mcp, pm_metal_ai_mcp_init, pm_metal_ai_mcp_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.mcp, pm_metal_ai_mcp_deinit, pm_metal_ai_mcp_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.ai.mcp, pm_metal_ai_mcp_server_start, pm_metal_ai_mcp_server_start, int32_t(const pm_metal_ai_mcp_server_t *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.mcp, pm_metal_ai_mcp_server_stop, pm_metal_ai_mcp_server_stop, int32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.mcp, pm_metal_ai_mcp_server_status, pm_metal_ai_mcp_server_status, int32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.mcp, pm_metal_ai_mcp_discover, pm_metal_ai_mcp_discover, int32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.metal.ai.mcp, pm_metal_ai_mcp_discover_all, pm_metal_ai_mcp_discover_all, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.ai.mcp, pm_metal_ai_mcp_call_tool, pm_metal_ai_mcp_call_tool, int32_t(const char *, const char *, const char *, char *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.ai.mcp, pm_metal_ai_mcp_server_count, pm_metal_ai_mcp_server_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.ai.mcp, pm_metal_ai_mcp_server_nth, pm_metal_ai_mcp_server_nth, int32_t(uint32_t, pm_metal_ai_mcp_server_t *));