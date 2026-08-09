#ifndef PYMERGETIC_METAL_REG_LEDGER_H_
#define PYMERGETIC_METAL_REG_LEDGER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Lang */
#define PM_METAL_REG_LANG_C 0
#define PM_METAL_REG_LANG_RS 1
#define PM_METAL_REG_LANG_PY 2

/* Role */
#define PM_METAL_REG_ROLE_MUSCLE 0
#define PM_METAL_REG_ROLE_FACE 1
#define PM_METAL_REG_ROLE_TRAMPOLINE 2
#define PM_METAL_REG_ROLE_SHIM 3

/* Honesty */
#define PM_METAL_REG_HONESTY_OK 0
#define PM_METAL_REG_HONESTY_STUB 1
#define PM_METAL_REG_HONESTY_INCOMPLETE 2

/* Caller via */
#define PM_METAL_REG_VIA_IMPORT_ROW 0
#define PM_METAL_REG_VIA_BIND 1
#define PM_METAL_REG_VIA_PY_ATTR 2
#define PM_METAL_REG_VIA_GUEST_FWD 3

/* Cold ledger — inspect only; never on hot call path. */
int32_t pm_metal_reg_ledger_add_callee(const uint8_t *full_module, const uint8_t *func, uint8_t lang,
                                       uint8_t role, uint8_t honesty, int32_t sync, int32_t async_,
                                       const uint8_t *partner, const uint8_t *label,
                                       const void *ptr);
int32_t pm_metal_reg_ledger_add_caller(const uint8_t *full_module, const uint8_t *func, uint8_t lang,
                                       const uint8_t *caller_module, uint8_t via, uint8_t honesty);
uint32_t pm_metal_reg_ledger_method_count(void);
uint32_t pm_metal_reg_ledger_gap_count(void);
int32_t pm_metal_reg_ledger_json(uint8_t *buf, uint32_t cap);
int32_t pm_metal_reg_ledger_module_json(const uint8_t *full_module, uint8_t *buf, uint32_t cap);
int32_t pm_metal_reg_ledger_method_json(const uint8_t *full_module, const uint8_t *func,
                                        uint8_t *buf, uint32_t cap);
/// Deprecated no-op — ledger filled by RegExport publish.
int32_t pm_metal_reg_ledger_seed_pilot(void);

/**
 * Completeness rollup (tree text or JSON).
 * module: optional NUL-terminated filter (NULL/empty = all).
 * gaps_only / detail: nonzero = true.
 * fmt_json: 0 = tree text, nonzero = JSON.
 * Returns byte length written (no NUL guarantee beyond content) or -1.
 */
int32_t pm_metal_reg_ledger_completeness(const uint8_t *module, int32_t gaps_only, int32_t detail,
                                         int32_t fmt_json, uint8_t *buf, uint32_t cap);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_REG_LEDGER_H_ */
