#ifndef PYMERGETIC_METAL_AI_PROVIDER_TYPES_H
#define PYMERGETIC_METAL_AI_PROVIDER_TYPES_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PM_METAL_AI_PROVIDER_NAME_MAX 64
#define PM_METAL_AI_PROVIDER_URL_MAX 256
#define PM_METAL_AI_PROVIDER_KEY_MAX 256
#define PM_METAL_AI_PROVIDER_MODEL_MAX 64
#define PM_METAL_AI_PROVIDER_REQUEST_MAX 65536
typedef struct { char name[64]; char url[256]; char api_key[256]; char model[64]; } pm_metal_ai_provider_t;
typedef void (*pm_metal_ai_provider_chunk_fn)(uint32_t, const char *, uint32_t, void *);
typedef void (*pm_metal_ai_provider_done_fn)(uint32_t, int32_t, const char *, void *);
#ifdef __cplusplus
}
#endif
#endif