/* pymergetic.metal.ai -- AI service hub and tool registry types. */
#ifndef PYMERGETIC_METAL_AI_TYPES_H
#define PYMERGETIC_METAL_AI_TYPES_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PM_METAL_AI_TOOL_NAME_MAX 64
#define PM_METAL_AI_TOOL_DESC_MAX 256
#define PM_METAL_AI_TOOL_SCHEMA_MAX 2048
typedef int32_t (*pm_metal_ai_tool_fn)(const char *json_params, char *out_result, uint32_t out_size, void *user);
typedef struct pm_metal_ai_tool {
    char name[PM_METAL_AI_TOOL_NAME_MAX];
    char description[PM_METAL_AI_TOOL_DESC_MAX];
    char params_schema[PM_METAL_AI_TOOL_SCHEMA_MAX];
    pm_metal_ai_tool_fn handler;
    void *user;
    struct pm_metal_ai_tool *next;
} pm_metal_ai_tool_t;
#ifdef __cplusplus
}
#endif
#endif