#ifndef PM_METAL_INSPECT_H_
#define PM_METAL_INSPECT_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shared Inspect contract (CDN + guest). Impl or honest NotImpl. */
int32_t pm_metal_inspect_init(void);

/* JSON body for GET /capabilities (role + feature flags). */
int32_t pm_metal_inspect_capabilities_json(char *buf, size_t buf_len);

/* Dispatch Inspect route; returns 0 if handled (fills status + body). */
int32_t pm_metal_inspect_handle(const char *method, const char *path,
                                int *status, char *body, size_t body_len);


#ifdef __cplusplus
}
#endif

#endif
