/*
 * Unix host seat bring-up — same live boot.tree as FW/browser, stdout sink.
 * Called from metal_unix_wrap before µPy when argv is bare.
 */
#include "pymergetic/metal/boot/tree.h"

#include <stdio.h>

#ifndef PM_METAL_VERSION
#define PM_METAL_VERSION "0.1.0"
#endif

#ifndef METAL_CDN_URL
#define METAL_CDN_URL ""
#endif

static void unix_print(const char *line, void *user)
{
    (void)user;
    fputs(line ? line : "", stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

void pm_metal_unix_orch_init(void);

void pm_metal_unix_boot_tree(void)
{
    pm_metal_unix_orch_init();

    const char *cdn = METAL_CDN_URL;
#if defined(PM_METAL_CFG_ARCH_X86) && PM_METAL_CFG_ARCH_X86
    const char *cpu = "x86";
    const char *seat = "unix · x86 · curl-and-run";
#else
    const char *cpu = "x86_64";
    const char *seat = "unix · x86_64 · curl-and-run";
#endif

    pm_metal_boot_set_print(unix_print, NULL);
    pm_metal_boot_banner(PM_METAL_VERSION, cpu);

    pm_metal_boot_tree_enter("arch");
    pm_metal_boot_tree_item("seat", PM_METAL_BOOT_TREE_OK, seat);
    pm_metal_boot_tree_leave();

    pm_metal_boot_tree_enter("host");
    pm_metal_boot_tree_item("os", PM_METAL_BOOT_TREE_OK, "linux");
    pm_metal_boot_tree_item("face", PM_METAL_BOOT_TREE_OK, "userspace µPy");
    pm_metal_boot_tree_leave();

    pm_metal_boot_tree_enter("mem");
    pm_metal_boot_tree_item("heap", PM_METAL_BOOT_TREE_OK, "MICROPY_HEAP_SIZE / -X heapsize");
    pm_metal_boot_tree_leave();

    pm_metal_boot_tree_enter("net");
    pm_metal_boot_tree_item("nic", PM_METAL_BOOT_TREE_SIM, "host stack (no auto-listen)");
    pm_metal_boot_tree_item(
        "cdn",
        (cdn && cdn[0]) ? PM_METAL_BOOT_TREE_OK : PM_METAL_BOOT_TREE_DIM,
        (cdn && cdn[0]) ? cdn : "unset");
    pm_metal_boot_tree_item("httpd", PM_METAL_BOOT_TREE_DIM,
                            "start: Microdot/ASGI listen :80/:443");
    pm_metal_boot_tree_item("sshd", PM_METAL_BOOT_TREE_DIM,
                            "start: pymergetic.metal.net.ssh.listen");
    pm_metal_boot_tree_leave();

    pm_metal_boot_tree_enter("repl");
    pm_metal_boot_tree_item("mode", PM_METAL_BOOT_TREE_OK, "friendly (-i after seat boot)");
    pm_metal_boot_tree_item("leave", PM_METAL_BOOT_TREE_OK,
                            "Ctrl-D REPL; quit=process; shutdown/reboot=unboot");
    pm_metal_boot_tree_leave();

    pm_metal_boot_tree_ready_ok();
    pm_metal_boot_rainbow_metalpython(PM_METAL_VERSION, cpu);
}
