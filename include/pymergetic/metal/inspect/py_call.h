#ifndef PM_METAL_INSPECT_PY_CALL_H_
#define PM_METAL_INSPECT_PY_CALL_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * In-process Microdot dispatch (frozen pymergetic.metal.inspect).
 * Returns: 1 handled, 0 not an Inspect route, -1 error/unavailable.
 */
int32_t pm_metal_inspect_py_handle(const char *method, const char *path,
                                   int *status, char *body, size_t body_len);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_INSPECT_PY_CALL_H_ */
