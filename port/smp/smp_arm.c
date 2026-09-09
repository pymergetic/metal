/* armv7 AP bring-up fill for pymergetic.metal.async (QEMU virt + RV1106).
 * Same face as smp_x86.c: ncpu says how many cores the seat runs, start_aps
 * wakes 1..ncpu-1, each parking in the async runner entry on its own stack.
 * The conduit is PSCI 0.2 CPU_ON (0x84000003) over HVC — the virt machine
 * DTB names hvc as the method; RV1106's firmware exposes the same call.
 * The raw .inst encoding bypasses clang's armv7ve-only check on the hvc
 * mnemonic (TCG dispatches it regardless of -march).
 *
 * QEMU's arm PSCI enters the AP at the given entry in ARM state with
 * r0 = context-id. The trampoline swaps in the AP's stack, then tail-calls
 * the runner; runner_entry ignores its argument, so the context slot is
 * only bookkeeping. */
#include "smp.h"

#include "pm_cpu.h"
#include "pymergetic/util/mem.h"

#include <stddef.h>
#include <stdint.h>

#define PM_METAL_SMP_STACK (512u * 1024u)
#define PM_METAL_SMP_MAX 4u

static int s_started;
static void (*s_entry)(void *);
/* Stack tops per AP, written before CPU_ON and only read by the AP that
 * owns the slot — the CPU_ON/entry pair is the release. The naked entry
 * asm references these by name, so both carry external linkage. */
uintptr_t pm_metal_smp_ap_stk[PM_METAL_SMP_MAX] __attribute__((used));

void pm_metal_smp_ap_entry(void);
void pm_metal_smp_ap_runner(void) __attribute__((used, noreturn));

uint32_t pm_metal_async_fill_ncpu(void) {
    return 4u;
}

static int32_t psci_cpu_on(uint32_t target_cpu, void *entry, uintptr_t ctx) {
    register uint32_t r0 __asm__("r0") = 0x84000003u; /* PSCI 0.2 CPU_ON */
    register uint32_t r1 __asm__("r1") = target_cpu; /* mpidr of the AP */
    register uintptr_t r2 __asm__("r2") = (uintptr_t)entry;
    register uintptr_t r3 __asm__("r3") = ctx;
    __asm__ volatile(
        ".inst 0xe1400070" /* hvc #0 */
        : "+r"(r0)
        : "r"(r1), "r"(r2), "r"(r3)
        : "memory");
    return (int32_t)r0;
}

/* The AP lands here from PSCI with r0 = its slot. Switch to the real
 * stack and enter the runner; it never returns. */
__attribute__((naked)) void pm_metal_smp_ap_entry(void) {
    __asm__ volatile(
        "cmp r0, #0\n"
        "bxeq lr\n" /* slot 0 is the boot cpu — refuse */
        "ldr r1, =pm_metal_smp_ap_stk\n"
        "ldr sp, [r1, r0, lsl #2]\n" /* stk table is indexed by slot */
        "mov r0, #0\n"
        "b pm_metal_smp_ap_runner\n");
}

void pm_metal_smp_ap_runner(void) {
    s_entry(NULL);
    for (;;) {
        __asm__ volatile("wfi");
    }
}

int32_t pm_metal_async_fill_start_aps(pm_util_mem_arena_t *arena, uint32_t ncpu,
    void (*entry)(void *)) {
    uint32_t i;
    if (s_started) {
        return 0;
    }
    if (arena == NULL || entry == NULL || ncpu < 2u) {
        return -1;
    }
    if (ncpu > PM_METAL_SMP_MAX) {
        ncpu = PM_METAL_SMP_MAX;
    }
    s_entry = entry;
    for (i = 1; i < ncpu; i++) {
        uint8_t *stk;
        stk = (uint8_t *)pm_util_mem_alloc(arena, PM_METAL_SMP_STACK);
        if (stk == NULL) {
            return -1;
        }
        pm_metal_smp_ap_stk[i] = (uintptr_t)(stk + PM_METAL_SMP_STACK);
        /* QEMU virt mpidr Aff0 = cpu index; cores are 0..ncpu-1. */
        (void)psci_cpu_on(i, pm_metal_smp_ap_entry, (uintptr_t)i);
    }
    s_started = 1;
    return 0;
}
