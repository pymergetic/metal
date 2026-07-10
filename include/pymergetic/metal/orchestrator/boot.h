#ifndef PYMERGETIC_METAL_ORCHESTRATOR_BOOT_H_
#define PYMERGETIC_METAL_ORCHESTRATOR_BOOT_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Orchestrator entry after pm_metal_sys_init — prints bootstrap facts and readiness. */
int pm_metal_orchestrator_boot(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_ORCHESTRATOR_BOOT_H_ */
