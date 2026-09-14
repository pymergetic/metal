/* pymergetic.metal.ai.session -- AI chat session types. */
#ifndef PYMERGETIC_METAL_AI_SESSION_TYPES_H
#define PYMERGETIC_METAL_AI_SESSION_TYPES_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PM_METAL_AI_SESSION_NAME_MAX 64
#define PM_METAL_AI_SESSION_CONTENT_MAX 16384
#define PM_METAL_AI_SESSION_MSGS_MAX 256
typedef enum { PM_METAL_AI_ROLE_USER = 0, PM_METAL_AI_ROLE_ASSISTANT, PM_METAL_AI_ROLE_SYSTEM } pm_metal_ai_role_t;
typedef struct { pm_metal_ai_role_t role; char content[16384]; uint32_t content_len; } pm_metal_ai_message_t;
typedef struct { char name[64]; uint32_t msg_count; uint8_t persistent; } pm_metal_ai_session_t;
#ifdef __cplusplus
}
#endif
#endif