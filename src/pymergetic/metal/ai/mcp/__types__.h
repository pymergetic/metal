/* pymergetic.metal.ai.mcp -- Model Context Protocol client types. */
#ifndef PYMERGETIC_METAL_AI_MCP_TYPES_H
#define PYMERGETIC_METAL_AI_MCP_TYPES_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PM_METAL_AI_MCP_NAME_MAX 128
#define PM_METAL_AI_MCP_COMMAND_MAX 256
#define PM_METAL_AI_MCP_URL_MAX 256
#define PM_METAL_AI_MCP_TOOL_NAME_MAX 128
#define PM_METAL_AI_MCP_TOOL_DESC_MAX 512
#define PM_METAL_AI_MCP_SCHEMA_MAX 2048
#define PM_METAL_AI_MCP_RESULT_MAX 262144
typedef enum { PM_METAL_AI_MCP_TRANSPORT_STDIO = 0, PM_METAL_AI_MCP_TRANSPORT_SSE } pm_metal_ai_mcp_transport_t;
typedef struct { char name[128]; pm_metal_ai_mcp_transport_t transport; char command[256]; char sse_url[256]; uint32_t timeout_ms; } pm_metal_ai_mcp_server_t;
#ifdef __cplusplus
}
#endif
#endif