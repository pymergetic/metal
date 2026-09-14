/* pymergetic.metal.ai.mcp -- prove MCP server register, discover stubs, and lifecycle.
 *
 * Exercises start, count, nth, status, stop, duplicate rejection,
 * NULL rejection, and secure init reset. discover/call_tool are stubs.
 */
#include "pymergetic/metal/ai/mcp/__exports__.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail_p(const char *why) {
    fprintf(stderr, "metal.ai.mcp test: %s\n", why);
    return 1;
}

/* 1: start, count, nth, status */
static int32_t case_start_one(void) {
    pm_metal_ai_mcp_server_t cfg, out;
    if (pm_metal_ai_mcp_init(NULL) != 0) return fail_p("init");
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.name, "filesystem", sizeof(cfg.name) - 1);
    strncpy(cfg.command, "npx -y @modelcontextprotocol/server-filesystem /tmp", sizeof(cfg.command) - 1);
    cfg.transport = PM_METAL_AI_MCP_TRANSPORT_STDIO;
    cfg.timeout_ms = 10000;
    if (pm_metal_ai_mcp_server_start(&cfg) != 0) return fail_p("start");
    if (pm_metal_ai_mcp_server_count() != 1) return fail_p("count");
    if (pm_metal_ai_mcp_server_nth(0, &out) != 0) return fail_p("nth");
    if (strcmp(out.name, "filesystem") != 0) return fail_p("nth name");
    if (pm_metal_ai_mcp_server_status("filesystem") != 0) return fail_p("status");
    return 0;
}

/* 2: stop removes server */
static int32_t case_stop(void) {
    pm_metal_ai_mcp_server_t cfg;
    if (pm_metal_ai_mcp_init(NULL) != 0) return fail_p("init");
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.name, "test", sizeof(cfg.name) - 1);
    if (pm_metal_ai_mcp_server_start(&cfg) != 0) return fail_p("start");
    if (pm_metal_ai_mcp_server_count() != 1) return fail_p("count before stop");
    if (pm_metal_ai_mcp_server_stop("test") != 0) return fail_p("stop");
    if (pm_metal_ai_mcp_server_count() != 0) return fail_p("count after stop");
    if (pm_metal_ai_mcp_server_status("test") != -1) return fail_p("status after stop");
    return 0;
}

/* 3: duplicate name rejection */
static int32_t case_duplicate_reject(void) {
    pm_metal_ai_mcp_server_t cfg;
    if (pm_metal_ai_mcp_init(NULL) != 0) return fail_p("init");
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.name, "dup", sizeof(cfg.name) - 1);
    if (pm_metal_ai_mcp_server_start(&cfg) != 0) return fail_p("first start");
    if (pm_metal_ai_mcp_server_start(&cfg) != -2) return fail_p("duplicate start");
    return 0;
}

/* 4: NULL rejection */
static int32_t case_null_reject(void) {
    pm_metal_ai_mcp_server_t cfg;
    if (pm_metal_ai_mcp_init(NULL) != 0) return fail_p("init");
    if (pm_metal_ai_mcp_server_start(NULL) != -1) return fail_p("start NULL");
    memset(&cfg, 0, sizeof(cfg));
    if (pm_metal_ai_mcp_server_start(&cfg) != -1) return fail_p("start empty name");
    if (pm_metal_ai_mcp_server_stop(NULL) != -1) return fail_p("stop NULL");
    if (pm_metal_ai_mcp_server_stop("no-such") != -1) return fail_p("stop missing");
    if (pm_metal_ai_mcp_server_status(NULL) != -1) return fail_p("status NULL");
    if (pm_metal_ai_mcp_server_status("no-such") != -1) return fail_p("status missing");
    if (pm_metal_ai_mcp_server_nth(99, NULL) != -1) return fail_p("nth NULL out");
    return 0;
}

/* 5: discover stubs return 0 */
static int32_t case_discover_stub(void) {
    if (pm_metal_ai_mcp_init(NULL) != 0) return fail_p("init");
    if (pm_metal_ai_mcp_discover("no-such") != 0) return fail_p("discover");
    if (pm_metal_ai_mcp_discover_all() != 0) return fail_p("discover_all");
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.ai.mcp, case_start_one, case_start_one);
PM_MOD_TEST_C(pymergetic.metal.ai.mcp, case_stop, case_stop);
PM_MOD_TEST_C(pymergetic.metal.ai.mcp, case_duplicate_reject, case_duplicate_reject);
PM_MOD_TEST_C(pymergetic.metal.ai.mcp, case_null_reject, case_null_reject);
PM_MOD_TEST_C(pymergetic.metal.ai.mcp, case_discover_stub, case_discover_stub);