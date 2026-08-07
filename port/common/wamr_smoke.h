#ifndef PM_METAL_WAMR_SMOKE_H_
#define PM_METAL_WAMR_SMOKE_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Requires floor mem already inited. Prints "wamr ok\n" on success. */
int pm_metal_wamr_smoke(void);

#ifdef __cplusplus
}
#endif

#endif
