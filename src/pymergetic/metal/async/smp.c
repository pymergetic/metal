/*
 * SMP bringup — BIOS: INIT-SIPI; UEFI: EFI MP Services.
 * Each AP enters pm_metal_async_run_loop_cpu forever.
 */
#include "pymergetic/metal/async/smp.h"
#include "pymergetic/metal/async/runner.h"
#include "pymergetic/metal/dev/acpi/__init__.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef PM_METAL_ASYNC_MAX_RUNNERS
#define PM_METAL_ASYNC_MAX_RUNNERS 8
#endif

#if METAL_BOARD_UEFI
#include "uefi_mp.h"
#endif

#define AP_TRAMP_PHYS 0x8000u
#define AP_PARAM_PHYS 0x7000u
#define AP_STACK_SIZE 0x4000u

extern const uint8_t pm_metal_ap_tramp[];
extern const uint64_t pm_metal_ap_tramp_size;

static uint8_t g_ap_stacks[PM_METAL_ASYNC_MAX_RUNNERS][AP_STACK_SIZE]
    __attribute__((aligned(16)));
static volatile uint32_t g_online_mask;
static uint32_t g_n_online = 1;
static int g_smp_started;

static uint32_t *lapic_reg(uint32_t off)
{
    return (uint32_t *)(uintptr_t)(pm_metal_dev_acpi_lapic_base() + off);
}

static uint32_t lapic_read(uint32_t off)
{
    return *lapic_reg(off);
}

#if METAL_BOARD_UEFI
static PM_EFI_MP_SERVICES_PROTOCOL *g_mp;

void uart_puts(const char *s);

uint32_t pm_metal_smp_cpu_index(void)
{
    UINTN n = 0;

    if (g_mp != NULL && g_mp->WhoAmI != NULL &&
        g_mp->WhoAmI(g_mp, &n) == 0) {
        return (uint32_t)n;
    }
    return 0;
}

static void EFIAPI pm_mp_event_notify(EFI_EVENT event, VOID *ctx)
{
    (void)event;
    (void)ctx;
}
#else
uint32_t pm_metal_smp_cpu_index(void)
{
    uint32_t apic_id;
    uint32_t i;
    uint32_t n;

    apic_id = (lapic_read(0x20) >> 24) & 0xffu;
    n = pm_metal_dev_acpi_cpu_count();
    for (i = 0; i < n; i++) {
        if (pm_metal_dev_acpi_apic_id(i) == apic_id) {
            return i;
        }
    }
    return 0;
}
#endif

uint32_t pm_metal_smp_online_count(void)
{
    return g_n_online;
}

static void ap_c_entry(uint32_t cpu)
{
    __atomic_or_fetch(&g_online_mask, 1u << cpu, __ATOMIC_SEQ_CST);
    (void)pm_metal_async_run_loop_cpu(cpu);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

#if METAL_BOARD_UEFI
static void EFIAPI ap_uefi_procedure(VOID *arg)
{
    ap_c_entry((uint32_t)(uintptr_t)arg);
}

static int32_t smp_start_uefi(uint32_t n)
{
    EFI_SYSTEM_TABLE *st = pm_metal_uefi_system_table();
    EFI_GUID guid = PM_EFI_MP_SERVICES_PROTOCOL_GUID;
    EFI_STATUS status;
    UINTN total = 0;
    UINTN enabled = 0;
    UINTN bsp = 0;
    UINTN i;
    uint32_t started = 1;

    if (st == NULL || st->BootServices == NULL) {
        uart_puts("smp uefi: no st\n");
        return -1;
    }
    status = st->BootServices->LocateProtocol(&guid, NULL, (VOID **)&g_mp);
    if (status != 0 || g_mp == NULL) {
        uart_puts("smp uefi: no mp\n");
        return -1;
    }
    if (g_mp->GetNumberOfProcessors(g_mp, &total, &enabled) != 0 || enabled < 2) {
        uart_puts("smp uefi: enabled<2\n");
        return -1;
    }
    if (g_mp->WhoAmI(g_mp, &bsp) != 0) {
        uart_puts("smp uefi: whoami\n");
        return -1;
    }
    if ((uint32_t)enabled < n) {
        n = (uint32_t)enabled;
    }

    g_online_mask = 1u << (uint32_t)bsp;
    g_n_online = 1;

    for (i = 0; i < total && started < n; i++) {
        EFI_EVENT ev = NULL;
        uint32_t cpu;

        if (i == bsp) {
            continue;
        }
        cpu = (uint32_t)i; /* EFI processor number == runner index on QEMU */
        status = st->BootServices->CreateEvent(
            0x00000200 /* EVT_NOTIFY_SIGNAL */, 16 /* TPL_NOTIFY */,
            pm_mp_event_notify, NULL, &ev);
        if (status != 0 || ev == NULL) {
            uart_puts("smp uefi: event\n");
            return -1;
        }
        status = g_mp->StartupThisAP(g_mp, ap_uefi_procedure, i, ev, 0,
                                     (VOID *)(uintptr_t)cpu, NULL);
        if (status != 0) {
            (void)st->BootServices->CloseEvent(ev);
            uart_puts("smp uefi: startup\n");
            return -1;
        }
        {
            uint32_t guard = 0;
            while (((g_online_mask >> cpu) & 1u) == 0u && guard < 10000000u) {
                __asm__ volatile("pause");
                guard++;
            }
            if (((g_online_mask >> cpu) & 1u) == 0u) {
                uart_puts("smp uefi: ap wait\n");
                return -1;
            }
        }
        started++;
        g_n_online++;
    }
    return (g_n_online >= 2u) ? 0 : -1;
}
#else
static void delay_iters(uint32_t n)
{
    volatile uint32_t i;
    for (i = 0; i < n; i++) {
        __asm__ volatile("pause");
    }
}

static void lapic_write(uint32_t off, uint32_t v)
{
    *lapic_reg(off) = v;
}

static void lapic_wait_idle(void)
{
    uint32_t guard = 0;
    while ((lapic_read(0x300) & (1u << 12)) != 0 && guard < 1000000u) {
        delay_iters(50);
        guard++;
    }
}

static void lapic_enable(void)
{
    uint32_t svr = lapic_read(0xF0);
    lapic_write(0xF0, svr | 0x100u);
}

static void send_ipi(uint32_t apic_id, uint32_t icr_lo)
{
    lapic_wait_idle();
    lapic_write(0x310, apic_id << 24);
    lapic_write(0x300, icr_lo);
    lapic_wait_idle();
}

static void fill_gdt(uint8_t *gdt)
{
    uint64_t *q = (uint64_t *)(void *)gdt;
    q[0] = 0;
    q[1] = 0x00CF9A000000FFFFull;
    q[2] = 0x00CF92000000FFFFull;
    q[3] = 0x00AF9A000000FFFFull;
}

static int prepare_ap_image(uint32_t cpu)
{
    uint8_t *param = (uint8_t *)(uintptr_t)AP_PARAM_PHYS;
    uint8_t *tramp = (uint8_t *)(uintptr_t)AP_TRAMP_PHYS;
    uint64_t cr3;
    uint64_t stack;
    size_t tsz;

    tsz = (size_t)pm_metal_ap_tramp_size;
    if (tsz == 0 || tsz > 0x1000u) {
        return -1;
    }
    memcpy(tramp, pm_metal_ap_tramp, tsz);

    memset(param, 0, 0x1000);
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    *(uint32_t *)(void *)param = (uint32_t)cr3;
    *(uint16_t *)(void *)(param + 4) = 32 - 1;
    *(uint32_t *)(void *)(param + 6) = AP_PARAM_PHYS + 36;
    fill_gdt(param + 36);

    stack = (uint64_t)(uintptr_t)&g_ap_stacks[cpu][AP_STACK_SIZE];
    *(uint64_t *)(void *)(param + 16) = stack;
    *(uint64_t *)(void *)(param + 24) = (uint64_t)(uintptr_t)ap_c_entry;
    *(uint32_t *)(void *)(param + 32) = cpu;
    return 0;
}

static int start_one_ap(uint32_t cpu)
{
    uint32_t apic_id;
    uint32_t vector;
    uint32_t guard;

    apic_id = pm_metal_dev_acpi_apic_id(cpu);
    if (apic_id == 0xffffffffu) {
        return -1;
    }
    if (prepare_ap_image(cpu) != 0) {
        return -1;
    }

    vector = AP_TRAMP_PHYS >> 12;
    send_ipi(apic_id, 0x00004500u);
    delay_iters(200000);
    send_ipi(apic_id, 0x00000500u);
    delay_iters(200000);
    send_ipi(apic_id, 0x00004600u | vector);
    delay_iters(200000);
    send_ipi(apic_id, 0x00004600u | vector);

    guard = 0;
    while (((g_online_mask >> cpu) & 1u) == 0u && guard < 5000000u) {
        delay_iters(100);
        guard++;
    }
    return ((g_online_mask >> cpu) & 1u) ? 0 : -1;
}

static int32_t smp_start_bios(uint32_t n)
{
    uint32_t i;

    g_online_mask = 1u;
    g_n_online = 1;
    lapic_enable();

    for (i = 1; i < n; i++) {
        if (start_one_ap(i) != 0) {
            return -1;
        }
        g_n_online++;
    }
    return 0;
}
#endif

int32_t pm_metal_smp_start(void)
{
    uint32_t n;

    if (g_smp_started) {
        return 0;
    }
    if (pm_metal_dev_acpi_init() != 0) {
        return -1;
    }
    n = pm_metal_dev_acpi_cpu_count();
    if (n < 2u) {
        return -1;
    }
    if (n > pm_metal_async_n_runners()) {
        n = pm_metal_async_n_runners();
    }
    if (n < 2u) {
        return -1;
    }

#if METAL_BOARD_UEFI
    if (smp_start_uefi(n) != 0) {
        return -1;
    }
#else
    if (smp_start_bios(n) != 0) {
        return -1;
    }
#endif
    g_smp_started = 1;
    return 0;
}
