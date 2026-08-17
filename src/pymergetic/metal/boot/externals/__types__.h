/* pymergetic.metal.boot.externals — linked libraries (section + ctor, like drv). */
#ifndef PYMERGETIC_METAL_BOOT_EXTERNALS_TYPES_H
#define PYMERGETIC_METAL_BOOT_EXTERNALS_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PM_METAL_EXTERNAL_MAX
#define PM_METAL_EXTERNAL_MAX 32u
#endif

typedef struct pm_metal_external {
    const char *name;
    const char *version;
} pm_metal_external_t;

int32_t pm_metal_external_add(const pm_metal_external_t *rec);
uint32_t pm_metal_external_count(void);
const char *pm_metal_external_name(uint32_t i);
const char *pm_metal_external_version(uint32_t i);

#define PM_METAL_EXT_CAT_(a, b) a##b
#define PM_METAL_EXT_CAT(a, b) PM_METAL_EXT_CAT_(a, b)
#define PM_METAL_EXT_STR_(x) #x
#define PM_METAL_EXT_STR(x) PM_METAL_EXT_STR_(x)

#define PM_METAL_EXTERNAL_REG_(sym) \
    static void __attribute__((constructor)) \
        PM_METAL_EXT_CAT(pm_metal_external_reg_, __COUNTER__)(void) { \
        (void)pm_metal_external_add(&(sym)); \
    }

#define PM_METAL_EXTERNAL_C(name, ver) \
    static const pm_metal_external_t __attribute__((section("pm_metal_externals"), used, aligned(8))) \
        PM_METAL_EXT_CAT(pm_metal_external_, name) = { #name, (ver) }; \
    PM_METAL_EXTERNAL_REG_(PM_METAL_EXT_CAT(pm_metal_external_, name))

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_EXTERNALS_TYPES_H */
