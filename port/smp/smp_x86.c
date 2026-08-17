/* x86 INIT-SIPI-SIPI — QEMU -smp N APs enter the async runner. */
#include "smp.h"

#include "pm_cpu.h"
#include "pymergetic/util/mem.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PM_METAL_SMP_STACK (512u * 1024u)
#define PM_METAL_SMP_DATA 0x7000ull
#define PM_METAL_SMP_TRAMP 0x8000ull
#define PM_METAL_SMP_GDT 0x7100ull
#define PM_METAL_SMP_GDTR 0x70F0ull
#define PM_METAL_SMP_STACKS 0x7200ull
#define PM_METAL_SMP_STACKS_N 256u
#define PM_METAL_LAPIC 0xFEE00000ull

extern char pm_metal_smp_tramp[];
extern char pm_metal_smp_tramp_end[];

static int s_started;

uint32_t pm_metal_async_fill_ncpu(void) {
    return 4u;
}

static void delay_us(uint64_t us) {
    uint64_t t0 = pm_cpu_mono_us();
    while (pm_cpu_mono_us() - t0 < us) {
        pm_cpu_pause();
    }
}

static uint32_t lapic_read(uint32_t off) {
    return *(volatile uint32_t *)(uintptr_t)(PM_METAL_LAPIC + off);
}

static void lapic_write(uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(uintptr_t)(PM_METAL_LAPIC + off) = v;
}

static void icr(uint32_t val) {
    lapic_write(0x300u, val);
    while (lapic_read(0x300u) & (1u << 12)) {
        pm_cpu_pause();
    }
}

int32_t pm_metal_async_fill_start_aps(pm_util_mem_arena_t *arena, uint32_t ncpu,
    void (*entry)(void *)) {
    uint8_t *data;
    uint8_t *tramp;
    uint64_t *stacks;
    uint64_t *gdt;
    uint32_t i;
    size_t ntramp;
    uint64_t cr3;

    if (s_started) {
        return 0;
    }
    if (arena == NULL || entry == NULL || ncpu < 2u) {
        return -1;
    }
    if (ncpu > PM_METAL_SMP_STACKS_N) {
        ncpu = PM_METAL_SMP_STACKS_N;
    }
    ntramp = (size_t)(pm_metal_smp_tramp_end - pm_metal_smp_tramp);
    if (ntramp == 0 || ntramp > 4096u) {
        return -1;
    }
    data = (uint8_t *)(uintptr_t)PM_METAL_SMP_DATA;
    tramp = (uint8_t *)(uintptr_t)PM_METAL_SMP_TRAMP;
    stacks = (uint64_t *)(uintptr_t)PM_METAL_SMP_STACKS;
    memset(data, 0, 0x200);
    memset(stacks, 0, (size_t)PM_METAL_SMP_STACKS_N * sizeof(*stacks));
    memcpy(tramp, pm_metal_smp_tramp, ntramp);

    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    *(uint64_t *)(void *)data = cr3;
    *(uint32_t *)(void *)(data + 8) = 1u;
    *(uint64_t *)(void *)(data + 16) = (uint64_t)(uintptr_t)entry;
    *(uint32_t *)(void *)(data + 32) = ncpu;
    for (i = 1; i < ncpu; i++) {
        uint8_t *stk = (uint8_t *)pm_util_mem_alloc(arena, PM_METAL_SMP_STACK);
        if (stk == NULL) {
            return -1;
        }
        stacks[i] = (uint64_t)(uintptr_t)(stk + PM_METAL_SMP_STACK);
    }

    gdt = (uint64_t *)(uintptr_t)PM_METAL_SMP_GDT;
    gdt[0] = 0;
    gdt[1] = 0x00AF9A000000FFFFull;
    gdt[2] = 0x00CF92000000FFFFull;
    gdt[3] = 0x00CF9A000000FFFFull;
    *(uint16_t *)(uintptr_t)PM_METAL_SMP_GDTR = (uint16_t)(4u * 8u - 1u);
    *(uint32_t *)(uintptr_t)(PM_METAL_SMP_GDTR + 2u) = (uint32_t)PM_METAL_SMP_GDT;

    lapic_write(0xF0u, lapic_read(0xF0u) | 0x100u);
    icr(0x000C4500u);
    delay_us(10000ull);
    icr(0x000C4608u);
    delay_us(200ull);
    icr(0x000C4608u);
    s_started = 1;
    return 0;
}
