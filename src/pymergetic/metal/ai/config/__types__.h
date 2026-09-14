/* pymergetic.metal.ai.config -- AI configuration types. */
#ifndef PYMERGETIC_METAL_AI_CONFIG_TYPES_H
#define PYMERGETIC_METAL_AI_CONFIG_TYPES_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PM_METAL_AI_CONFIG_NAME_MAX 64
#define PM_METAL_AI_CONFIG_VALUE_MAX 256
typedef struct { char name[64]; char api_key[256]; char url[256]; char model[64]; } pm_metal_ai_config_provider_t;
typedef struct { char key[64]; char value[256]; } pm_metal_ai_config_param_t;
#ifdef __cplusplus
}
#endif
#endif