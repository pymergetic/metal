/*
 * boot.unboot — reverse-boot spine (mirror of boot.tree); shutdown/reboot shims.
 */
#include <pymergetic/metal/boot/unboot.h>
#include <pymergetic/metal/boot/__init__.h>
#include <pymergetic/metal/boot/tree.h>
#include <pymergetic/metal/process/__init__.h>

#include <stdio.h>

#ifndef PM_METAL_VERSION
#define PM_METAL_VERSION "0.1.0"
#endif

static pm_metal_boot_seat_power_fn g_shutdown_hook;
static pm_metal_boot_seat_power_fn g_reboot_hook;
static int32_t g_dead;

/* Optional: board power muscle (bios/efi power.c). Weak when not linked. */
void pm_metal_boot_halt(void) __attribute__((weak));
void pm_metal_boot_reset(int32_t reboot) __attribute__((weak));

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

static void default_shutdown(void)
{
    if (pm_metal_boot_halt) {
        pm_metal_boot_halt();
    }
    g_dead = 1;
}

static void default_reboot(void)
{
    if (pm_metal_boot_reset) {
        pm_metal_boot_reset(1);
    }
    g_dead = 0;
    pm_metal_process_set_shutting_down(0);
}

int32_t pm_metal_boot_shutdown(void)
{
    (void)pm_metal_boot_unboot();
    pm_metal_boot_tree_dead();
    pm_metal_boot_dead_art(PM_METAL_VERSION, unboot_cpu());
    if (g_shutdown_hook) {
        g_shutdown_hook();
    } else {
        default_shutdown();
    }
    g_dead = 1;
    return 0;
}

int32_t pm_metal_boot_reboot(void)
{
    (void)pm_metal_boot_unboot();
    /* No dead art — seat comes back. */
    pm_metal_boot_emit("`-- revive       \033[33mok\033[0m");
    pm_metal_boot_emit("\033[33mreboot → boot again\033[0m");
    g_dead = 0;
    if (g_reboot_hook) {
        g_reboot_hook();
    } else {
        default_reboot();
    }
    return 0;
}
