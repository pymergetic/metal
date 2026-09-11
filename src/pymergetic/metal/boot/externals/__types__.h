/* pymergetic.metal.boot.externals — linked libraries (section + ctor, like drv). */
#ifndef PYMERGETIC_METAL_BOOT_EXTERNALS_TYPES_H
#define PYMERGETIC_METAL_BOOT_EXTERNALS_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pm_metal_external {
    const char *name;
    const char *version;
} pm_metal_external_t;

/* How an external gets on the list. The record itself lives in a read-only
 * section, so the thread runs through a node beside it: registration happens
 * in a constructor, before any seat has an arena, and a card that registers
 * before there is an allocator cannot be asking one for room. There is
 * therefore no ceiling on externals and nothing reserved for them — the same
 * shape as the module registry and the limits list. */
typedef struct pm_metal_external_node {
    const pm_metal_external_t *rec;
    struct pm_metal_external_node *next;
} pm_metal_external_node_t;

int32_t pm_metal_external_attach(pm_metal_external_node_t *node);
int32_t pm_metal_external_detach(pm_metal_external_node_t *node);
uint32_t pm_metal_external_count(void);
const char *pm_metal_external_name(uint32_t i);
const char *pm_metal_external_version(uint32_t i);

#define PM_METAL_EXT_CAT_(a, b) a##b
#define PM_METAL_EXT_CAT(a, b) PM_METAL_EXT_CAT_(a, b)
#define PM_METAL_EXT_STR_(x) #x
#define PM_METAL_EXT_STR(x) PM_METAL_EXT_STR_(x)

#define PM_METAL_EXTERNAL_REG_(sym) \
    static pm_metal_external_node_t PM_METAL_EXT_CAT(sym, _node) = { &(sym), NULL }; \
    static void __attribute__((constructor)) \
        PM_METAL_EXT_CAT(pm_metal_external_reg_, __COUNTER__)(void) { \
        (void)pm_metal_external_attach(&PM_METAL_EXT_CAT(sym, _node)); \
    }

#define PM_METAL_EXTERNAL_C(name, ver) \
    static const pm_metal_external_t __attribute__((section("pm_metal_externals"), used, aligned(8))) \
        PM_METAL_EXT_CAT(pm_metal_external_, name) = { #name, (ver) }; \
    PM_METAL_EXTERNAL_REG_(PM_METAL_EXT_CAT(pm_metal_external_, name))

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_EXTERNALS_TYPES_H */
