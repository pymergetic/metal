/* pymergetic.metal.boot.tree — surfaces + the backend cards print through. */
#ifndef PYMERGETIC_METAL_BOOT_TREE_TYPES_H
#define PYMERGETIC_METAL_BOOT_TREE_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A card attaches one function per surface. print/motd walk what registered. */
#define PM_METAL_BOOT_SURF_TREE 0u
#define PM_METAL_BOOT_SURF_MOTD 1u

/* Stable order so the floor tree does not reshuffle when a new card attaches. */
#define PM_METAL_BOOT_MSG_MEM 10u
#define PM_METAL_BOOT_MSG_CPU 20u
#define PM_METAL_BOOT_MSG_DEVICES 30u
#define PM_METAL_BOOT_MSG_CONSOLE 35u
#define PM_METAL_BOOT_MSG_MODS 40u
#define PM_METAL_BOOT_MSG_NET 50u
#define PM_METAL_BOOT_MSG_ASYNC 60u
#define PM_METAL_BOOT_MSG_WASM 70u
#define PM_METAL_BOOT_MSG_EXTERNALS 80u
#define PM_METAL_BOOT_MSG_MOTD_CONSOLE 10u
#define PM_METAL_BOOT_MSG_MOTD_CDN 20u
#define PM_METAL_BOOT_MSG_MOTD_REPL 30u

typedef void (*pm_metal_boot_msg_fn)(int last);

int32_t pm_metal_boot_tree_print(void);
void pm_metal_boot_motd(void);
void pm_metal_boot_shutdown(int reboot, unsigned delay_s);

int32_t pm_metal_boot_msg_attach(uint32_t surf, uint32_t order, pm_metal_boot_msg_fn fn);
uint32_t pm_metal_boot_msg_attached(uint32_t surf);

/* Backend: same grammar every card uses. */
void pm_metal_boot_msg_item(int last, int depth, int parent_cont, const char *name, const char *detail);
void pm_metal_boot_msg_line(const char *s);
void pm_metal_boot_msg_fail(void);
void pm_metal_boot_msg_count(char *dst, unsigned cap, const char *lead, unsigned n, const char *unit);

#define PM_METAL_BOOT_MSG_C(surf, order, fn) \
    static void __attribute__((constructor)) pm_metal_boot_msg_reg_##fn(void) { \
        (void)pm_metal_boot_msg_attach((surf), (order), (fn)); \
    }

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_TREE_TYPES_H */
