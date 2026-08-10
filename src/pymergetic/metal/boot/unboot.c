/*
 * boot.unboot — reverse-boot spine (mirror of boot.tree); shutdown/reboot shims.
 */
#include <pymergetic/metal/boot/unboot.h>
#include <pymergetic/metal/boot/__init__.h>
#include <pymergetic/metal/boot/tree.h>
#include <pymergetic/metal/process/__init__.h>
#include <pymergetic/metal/async/time.h>

#include <stdio.h>

#include <pymergetic/metal/reg/mod.h>

/* RegMod declare (C SoT) — loaded via pm_metal_boot_mod_reg_load. */
static pm_metal_reg_export_t boot_mod_exports[] = {
    PM_METAL_REG_EXPORT(banner),
    PM_METAL_REG_EXPORT(emit),
    PM_METAL_REG_EXPORT(tree_ready_ok),
    PM_METAL_REG_EXPORT(tree_print),
    PM_METAL_REG_EXPORT(unboot),
    PM_METAL_REG_EXPORT(shutdown),
    PM_METAL_REG_EXPORT(reboot),
    PM_METAL_REG_EXPORT(is_dead),
    PM_METAL_REG_EXPORT(clear_dead),
    PM_METAL_REG_EXPORT(shutting_down),
};
PM_METAL_REG_REF(boot_mod, banner, 0);
PM_METAL_REG_REF(boot_mod, emit, 1);
PM_METAL_REG_REF(boot_mod, tree_ready_ok, 2);
PM_METAL_REG_REF(boot_mod, tree_print, 3);
PM_METAL_REG_REF(boot_mod, unboot, 4);
PM_METAL_REG_REF(boot_mod, shutdown, 5);
PM_METAL_REG_REF(boot_mod, reboot, 6);
PM_METAL_REG_REF(boot_mod, is_dead, 7);
PM_METAL_REG_REF(boot_mod, clear_dead, 8);
PM_METAL_REG_REF(boot_mod, shutting_down, 9);
PM_METAL_REG_MOD(boot_mod, "pymergetic.metal.boot")

static int32_t boot_mod_register_symbols(void *ctx)
{
    (void)ctx;
    pm_metal_reg_export_publish(boot_mod_banner, (void *)pm_metal_boot_banner);
    pm_metal_reg_export_publish(boot_mod_emit, (void *)pm_metal_boot_emit);
    pm_metal_reg_export_publish(boot_mod_tree_ready_ok, (void *)pm_metal_boot_tree_ready_ok);
    pm_metal_reg_export_publish(boot_mod_tree_print, (void *)pm_metal_boot_tree_print);
    pm_metal_reg_export_publish(boot_mod_unboot, (void *)pm_metal_boot_unboot);
    pm_metal_reg_export_publish(boot_mod_shutdown, (void *)pm_metal_boot_shutdown);
    pm_metal_reg_export_publish(boot_mod_reboot, (void *)pm_metal_boot_reboot);
    pm_metal_reg_export_publish(boot_mod_is_dead, (void *)pm_metal_boot_is_dead);
    pm_metal_reg_export_publish(boot_mod_clear_dead, (void *)pm_metal_boot_clear_dead);
    pm_metal_reg_export_publish(boot_mod_shutting_down, (void *)pm_metal_boot_shutting_down);
    return 0;
}

#ifndef PM_METAL_VERSION
#define PM_METAL_VERSION "0.1.0"
#endif

static pm_metal_boot_seat_power_fn g_shutdown_hook;
static pm_metal_boot_seat_power_fn g_reboot_hook;
static int32_t g_dead;

#if defined(PM_METAL_CFG_ARCH_WASM) && PM_METAL_CFG_ARCH_WASM
/* Browser: no platform power muscle. */
#else
/* Strong defs in bios/efi power.c when linked; weak park if not. */
void pm_metal_boot_halt(void) __attribute__((weak));
void pm_metal_boot_reset(int32_t reboot) __attribute__((weak));
#endif

void pm_metal_boot_set_shutdown_hook(pm_metal_boot_seat_power_fn fn)
{
    g_shutdown_hook = fn;
}

void pm_metal_boot_set_reboot_hook(pm_metal_boot_seat_power_fn fn)
{
    g_reboot_hook = fn;
}

int32_t pm_metal_boot_is_dead(void)
{
    return g_dead;
}

void pm_metal_boot_clear_dead(void)
{
    g_dead = 0;
    pm_metal_process_set_shutting_down(0);
}

int32_t pm_metal_boot_shutting_down(void)
{
    return pm_metal_process_shutting_down();
}

static const char *unboot_cpu(void)
{
#if defined(PM_METAL_CFG_ARCH_X86) && PM_METAL_CFG_ARCH_X86
    return "x86";
#elif defined(PM_METAL_CFG_ARCH_WASM) && PM_METAL_CFG_ARCH_WASM
    return "wasm32";
#else
    return "x86_64";
#endif
}

/*
 * Reverse of the live boot tree:
 *   boot:  arch → host → mem → net → services/repl → ready → rainbow
 *   unboot: shutting_down → quit procs → repl → net → mem → host → arch
 * Shutdown then: `-- dead` + red DEAD art. Reboot skips dead art.
 */
int32_t pm_metal_boot_unboot(void)
{
    char detail[64];
    uint32_t nquit;

    pm_metal_boot_tree_reset();
    pm_metal_boot_emit("\033[2munboot\033[0m");

    pm_metal_boot_tree_enter("gate");
    pm_metal_process_set_shutting_down(1);
    pm_metal_boot_tree_item("shutting_down", PM_METAL_BOOT_TREE_OK, "refuse new spawn/crown");
    pm_metal_boot_tree_leave();

    pm_metal_boot_tree_enter("process");
    nquit = pm_metal_process_quit_all(0);
    (void)snprintf(detail, sizeof(detail), "%u quit", (unsigned)nquit);
    pm_metal_boot_tree_item("quit_all", PM_METAL_BOOT_TREE_OK, detail);
    pm_metal_boot_tree_item("table", PM_METAL_BOOT_TREE_OK, "traceless");
    pm_metal_boot_tree_leave();

    /* Boot-opposite seat stages (same section names as bring-up). */
    pm_metal_boot_tree_enter("repl");
    pm_metal_boot_tree_item("leave", PM_METAL_BOOT_TREE_OK, "console released");
    pm_metal_boot_tree_leave();

    pm_metal_boot_tree_enter("net");
    pm_metal_boot_tree_item("listeners", PM_METAL_BOOT_TREE_OK, "service faces down");
    pm_metal_boot_tree_item("nic", PM_METAL_BOOT_TREE_DIM, "park / drop");
    pm_metal_boot_tree_leave();

    pm_metal_boot_tree_enter("mem");
    pm_metal_boot_tree_item("runners", PM_METAL_BOOT_TREE_OK, "async park");
    pm_metal_boot_tree_item("heap", PM_METAL_BOOT_TREE_DIM, "arena kept until seat end");
    pm_metal_boot_tree_leave();

    pm_metal_boot_tree_enter("host");
    pm_metal_boot_tree_item("face", PM_METAL_BOOT_TREE_OK, "releasing");
    pm_metal_boot_tree_leave();

    pm_metal_boot_tree_enter("arch");
    pm_metal_boot_tree_item("seat", PM_METAL_BOOT_TREE_OK, "endpoint via shim");
    pm_metal_boot_tree_leave();

    return 0;
}

static void seat_park_forever(void)
{
#if defined(PM_METAL_CFG_ARCH_WASM) && PM_METAL_CFG_ARCH_WASM
    /* Browser soft-dead: return to JS; reload revives. */
    return;
#else
    for (;;) {
#if defined(__x86_64__) || defined(__i386__) || defined(__i686__)
        __asm__ volatile("cli; hlt");
#else
        /* spin */
#endif
    }
#endif
}

#if !(defined(PM_METAL_CFG_ARCH_WASM) && PM_METAL_CFG_ARCH_WASM)
static void seat_kbc_restart(void)
{
    uint32_t spins;
    uint8_t st;

    for (spins = 0; spins < 100000u; spins++) {
        __asm__ volatile("inb %1, %0" : "=a"(st) : "Nd"((uint16_t)0x64));
        if ((st & 0x02u) == 0u) {
            break;
        }
    }
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xFEu), "Nd"((uint16_t)0x64));
}
#endif

static void default_shutdown(void)
{
    pm_metal_time_sleep_ms(2000u);
#if !(defined(PM_METAL_CFG_ARCH_WASM) && PM_METAL_CFG_ARCH_WASM)
    /* Platform OWN: ACPI S5 (BIOS) / EFI ResetShutdown — not a QEMU debug port. */
    if (pm_metal_boot_reset) {
        pm_metal_boot_reset(0);
    }
#endif
    g_dead = 1;
    seat_park_forever();
}

static void default_reboot(void)
{
    pm_metal_time_sleep_ms(2000u);
#if !(defined(PM_METAL_CFG_ARCH_WASM) && PM_METAL_CFG_ARCH_WASM)
    if (pm_metal_boot_reset) {
        pm_metal_boot_reset(1);
    } else {
        seat_kbc_restart();
    }
#endif
    g_dead = 0;
    pm_metal_process_set_shutting_down(0);
    seat_park_forever();
}

int32_t pm_metal_boot_shutdown(void)
{
    if (g_dead) {
        /* Already shut down — do not re-print the reverse tree. */
        seat_park_forever();
        return 0;
    }

    (void)pm_metal_boot_unboot();
    pm_metal_boot_tree_dead();
    pm_metal_boot_dead_art(PM_METAL_VERSION, unboot_cpu());
    g_dead = 1;
    if (g_shutdown_hook) {
        g_shutdown_hook();
    } else {
        default_shutdown();
    }
    /* Hooks must not return into a live REPL. */
    seat_park_forever();
    return 0;
}

int32_t pm_metal_boot_reboot(void)
{
    char line[96];

    if (g_dead) {
        seat_park_forever();
        return 0;
    }

    (void)pm_metal_boot_unboot();
    /* No dead art — seat comes back after countdown. */
    pm_metal_boot_emit("`-- revive       \033[33mok\033[0m");
    snprintf(line, sizeof(line), "\033[33mversion %s @ %s\033[0m", PM_METAL_VERSION,
             unboot_cpu());
    pm_metal_boot_emit(line);
    pm_metal_boot_emit("\033[33m*** reboot in 2s ***\033[0m");
    g_dead = 0;
    if (g_reboot_hook) {
        g_reboot_hook();
    } else {
        default_reboot();
    }
    seat_park_forever();
    return 0;
}
