#ifndef PYMERGETIC_METAL_EXTERNALS_H_
#define PYMERGETIC_METAL_EXTERNALS_H_
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void pm_metal_externals_init(void);
void pm_metal_externals_seed_fallback(void);
uint32_t pm_metal_external_count(void);
int32_t pm_metal_external_get(uint32_t idx, void *out);
int32_t pm_metal_external_find(const char *id, void *out);
int32_t pm_metal_external_register(const char *id, const char *version, const char *url, const char *note);
#ifdef __cplusplus
}
#endif
#endif
