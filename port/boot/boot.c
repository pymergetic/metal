/*
 * Live product boot — identical stages on every seat.
 * HAL (bios/efi/wasm) supplies drivers; tree emits as each stage finishes.
 */
#include "pymergetic/metal/boot/product.h"

#include "api.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pymergetic/metal/arch.h"
#include "pymergetic/metal/boot/externals.h"
#include "pymergetic/metal/boot/tree.h"
#include "pymergetic/metal/cdn.h"

#if !(defined(PM_METAL_CFG_ARCH_WASM) && PM_METAL_CFG_ARCH_WASM)
#include "pymergetic/metal/async/runner.h"
#include "pymergetic/metal/async/smp.h"
#include "pymergetic/metal/console.h"
#include "pymergetic/metal/dev/acpi/__init__.h"
#include "pymergetic/metal/dev/serial.h"
#include "pymergetic/metal/mem.h"
#include "pymergetic/metal/net/dhcp/__init__.h"
#include "pymergetic/metal/net/ip/__init__.h"
#include "pymergetic/metal/net/pump/__init__.h"
#include "pymergetic/metal/net/ssh/__init__.h"
#include "pymergetic/metal/pack/mod_packs.h"
#include "pymergetic/metal/rt.h"

#include "gfx_bringup.h"
#include "metal_heap_buf.h"
#include "nic_bringup.h"

static uint8_t g_console_ring[8 * 1024];
static uint8_t g_metal_heap[PM_METAL_PORT_HEAP_BYTES] __attribute__((aligned(4096)));
#endif

#ifndef PM_METAL_VERSION
#define PM_METAL_VERSION "0.1.0"
#endif

static void tree_print_bridge(const char *line, void *user)
{
    (void)user;
    pm_metal_hal_console_puts(line);
}

#if !defined(PM_METAL_WASM_GUEST)
static void boot_tree_emit_externals(void)
{
    pm_metal_external_t e;
    uint32_t i;
    uint32_t n;
    char detail[72];
    const char *ver;

    pm_metal_externals_init();
    n = pm_metal_external_count();
    snprintf(detail, sizeof(detail), "%u", (unsigned)n);
    pm_metal_boot_tree_enter_ex("externals", PM_METAL_BOOT_TREE_OK, detail);
    for (i = 0u; i < n; i++) {
        if (pm_metal_external_get(i, &e) != 0 || e.id == NULL || e.id[0] == '\0') {
            continue;
        }
        ver = (e.version != NULL) ? e.version : "";
        pm_metal_boot_tree_item(e.id, PM_METAL_BOOT_TREE_OK, ver);
    }
    pm_metal_boot_tree_leave();
}
#endif

#if defined(PM_METAL_CFG_ARCH_WASM) && PM_METAL_CFG_ARCH_WASM

#include "pymergetic/metal/mem.h"

static void fmt_bytes(char *out, size_t out_len, size_t n)
{
    if (n >= 1024u * 1024u) {
        snprintf(out, out_len, "%u MiB", (unsigned)(n / (1024u * 1024u)));
    } else if (n >= 1024u) {
        snprintf(out, out_len, "%u KiB", (unsigned)(n / 1024u));
    } else {
        snprintf(out, out_len, "%u B", (unsigned)n);
    }
}

/* Forge-shaped region line: base=0x… size=N MiB */
static void fmt_region(char *out, size_t out_len, uintptr_t base, size_t bytes)
{
    char sz[24];
    fmt_bytes(sz, sizeof(sz), bytes);
    snprintf(out, out_len, "base=0x%08lx size=%s", (unsigned long)base, sz);
}

static void boot_tree_emit_metal_mem(size_t kernel_bytes)
{
    char detail[96];
    char sz[24];

    snprintf(detail, sizeof(detail), "2 region(s)");
    pm_metal_boot_tree_enter_ex("mem", PM_METAL_BOOT_TREE_OK, detail);

    fmt_region(detail, sizeof(detail), 0u, kernel_bytes);
    pm_metal_boot_tree_item("kernel", PM_METAL_BOOT_TREE_OK, detail);

    fmt_region(detail, sizeof(detail), pm_metal_mem_base(), pm_metal_mem_span_bytes());
    pm_metal_boot_tree_enter_ex("area", PM_METAL_BOOT_TREE_DIM, detail);
    {
        /* map starts empty; hole is the room map↑ / heap↓ both grow into. */
        char room[24];
        fmt_bytes(sz, sizeof(sz), pm_metal_mem_map_used());
        fmt_bytes(room, sizeof(room), pm_metal_mem_hole());
        snprintf(detail, sizeof(detail), "%s · room %s", sz, room);
        pm_metal_boot_tree_item("map", PM_METAL_BOOT_TREE_DIM, detail);
    }
    fmt_bytes(sz, sizeof(sz), pm_metal_mem_hole());
    pm_metal_boot_tree_item("hole", PM_METAL_BOOT_TREE_DIM, sz);
    fmt_bytes(sz, sizeof(sz), pm_metal_mem_heap_bytes());
    pm_metal_boot_tree_item("tlsf (heap)", PM_METAL_BOOT_TREE_OK, sz);
    pm_metal_boot_tree_leave(); /* area */
    pm_metal_boot_tree_leave(); /* mem */
}

static int boot_wasm_sim(void)
{
    uint8_t *arena;
    size_t arena_bytes;
    size_t kernel;
    /* Emscripten: static/data/stack end — image region for the tree. */
    extern char __heap_base;
    void pm_metal_globals_init(void);
    void pm_metal_boot_globals_init(void);

    pm_metal_hal_console_init();
    /* Nest util/mem/auth/async/… under pymergetic.metal for dotted imports. */
    pm_metal_globals_init();
    pm_metal_boot_globals_init();
    pm_metal_boot_set_print(tree_print_bridge, NULL);
    pm_metal_boot_banner(PM_METAL_VERSION, pm_metal_hal_cpu_label());

    pm_metal_boot_tree_enter("arch");
    pm_metal_boot_tree_item("seat", PM_METAL_BOOT_TREE_OK, "wasm · browser");
    pm_metal_boot_tree_leave();

    /* Same Metal path as box: HAL claim → dual-span + TLSF. */
    if (pm_metal_hal_mem_claim(&arena, &arena_bytes) != 0 ||
        pm_metal_mem_init(arena, arena_bytes) != 0) {
        pm_metal_boot_tree_enter("mem");
        pm_metal_boot_tree_item("area", PM_METAL_BOOT_TREE_FAIL, "claim/init");
        pm_metal_boot_tree_leave();
        return -1;
    }
    kernel = (size_t)(uintptr_t)&__heap_base;
    boot_tree_emit_metal_mem(kernel);

    pm_metal_boot_tree_enter("cpu");
    pm_metal_boot_tree_item("smp", PM_METAL_BOOT_TREE_SIM, "1 runner");
    pm_metal_boot_tree_leave();

    pm_metal_boot_tree_enter("devices");
    pm_metal_boot_tree_item("catalog", PM_METAL_BOOT_TREE_SIM, "4 nodes");
    pm_metal_boot_tree_item("console", PM_METAL_BOOT_TREE_SIM, "panel stdout");
    pm_metal_boot_tree_leave();

    pm_metal_boot_tree_enter("fs");
    pm_metal_boot_tree_item("mods", PM_METAL_BOOT_TREE_OK, "/mods/pymergetic.metal*");
    pm_metal_boot_tree_item("root", PM_METAL_BOOT_TREE_SIM, "memfs");
    pm_metal_boot_tree_leave();

    pm_metal_boot_tree_enter("net");
    pm_metal_boot_tree_item("nic", PM_METAL_BOOT_TREE_SIM, "js.fetch");
    {
        const char *home = pm_metal_cdn_default_url();
        pm_metal_boot_tree_item(
            "cdn",
            home ? PM_METAL_BOOT_TREE_OK : PM_METAL_BOOT_TREE_DIM,
            home ? home : "unset");
    }
    pm_metal_boot_tree_leave();

    pm_metal_boot_tree_enter("async");
    pm_metal_boot_tree_item("runners", PM_METAL_BOOT_TREE_SIM, "asyncify");
    pm_metal_boot_tree_leave();

    pm_metal_boot_tree_enter("wasm");
    pm_metal_boot_tree_item("wasmmod", PM_METAL_BOOT_TREE_OK, "host");
    pm_metal_boot_tree_leave();

    boot_tree_emit_externals();

    pm_metal_boot_tree_ready_ok();
    pm_metal_boot_rainbow_metalpython(PM_METAL_VERSION, pm_metal_hal_cpu_label());
    return 0;
}

#else /* native firmware */

static int boot_native(void)
{
    pm_metal_net_dhcp_lease_t lease;
    int i;
    char detail[72];
    uint32_t n_cpus;

    pm_metal_hal_console_init();
    pm_metal_boot_set_print(tree_print_bridge, NULL);
    pm_metal_boot_banner(PM_METAL_VERSION, pm_metal_hal_cpu_label());

    pm_metal_boot_tree_enter("arch");
    {
        pm_metal_arch_firmware_t fw = pm_metal_arch_firmware();
        const char *fw_s = fw == PM_METAL_FW_ID_UEFI ? "uefi" : "bios";
        snprintf(detail, sizeof(detail), "%s · %s", pm_metal_arch_name(pm_metal_arch_current()), fw_s);
        pm_metal_boot_tree_item("seat", PM_METAL_BOOT_TREE_OK, detail);
    }
    pm_metal_boot_tree_leave();

    pm_metal_boot_tree_enter("mem");
    if (pm_metal_console_init(g_console_ring, sizeof(g_console_ring)) != 0 ||
        pm_metal_console_attach(pm_metal_dev_serial_console_sink, NULL) != 0) {
        pm_metal_boot_tree_item("console", PM_METAL_BOOT_TREE_FAIL, "init");
        pm_metal_boot_tree_leave();
        return -1;
    }
    if (pm_metal_mem_init(g_metal_heap, sizeof(g_metal_heap)) != 0) {
        pm_metal_boot_tree_item("heap", PM_METAL_BOOT_TREE_FAIL, "init");
        pm_metal_boot_tree_leave();
        return -1;
    }
    snprintf(detail, sizeof(detail), "%u KiB", (unsigned)(sizeof(g_metal_heap) / 1024u));
    pm_metal_boot_tree_item("heap", PM_METAL_BOOT_TREE_OK, detail);
    if (pm_metal_rt_connect_symbols() != 0) {
        pm_metal_boot_tree_item("alloc", PM_METAL_BOOT_TREE_FAIL, "rt");
        pm_metal_boot_tree_leave();
        return -1;
    }
    pm_metal_boot_tree_item("alloc", PM_METAL_BOOT_TREE_OK, "tlsf");
    pm_metal_boot_tree_leave();

    pm_metal_boot_tree_enter("cpu");
    if (pm_metal_dev_acpi_init() != 0) {
        pm_metal_boot_tree_item("acpi", PM_METAL_BOOT_TREE_FAIL, "init");
        pm_metal_boot_tree_leave();
        return -1;
    }
    n_cpus = pm_metal_dev_acpi_cpu_count();
    if (n_cpus < 2u) {
        pm_metal_boot_tree_item("smp", PM_METAL_BOOT_TREE_FAIL, "n<2");
        pm_metal_boot_tree_leave();
        return -1;
    }
    if (pm_metal_async_start(n_cpus) != 0 || pm_metal_smp_start() != 0 ||
        pm_metal_smp_online_count() < 2u ||
        pm_metal_smp_online_count() != pm_metal_async_n_runners()) {
        pm_metal_boot_tree_item("smp", PM_METAL_BOOT_TREE_FAIL, "online");
        pm_metal_boot_tree_leave();
        return -1;
    }
    snprintf(detail, sizeof(detail), "%u runners", (unsigned)pm_metal_async_n_runners());
    pm_metal_boot_tree_item("smp", PM_METAL_BOOT_TREE_OK, detail);
    pm_metal_boot_tree_leave();

    pm_metal_net_pump_bind_async();

    pm_metal_boot_tree_enter("devices");
    pm_metal_boot_tree_item("acpi", PM_METAL_BOOT_TREE_OK, "tables");
    pm_metal_boot_tree_item("console", PM_METAL_BOOT_TREE_OK, "uart");
    (void)pm_metal_gfx_bringup();
    pm_metal_boot_tree_leave();

    pm_metal_boot_tree_enter("fs");
    if (pm_metal_mod_packs_mount_all() != 0) {
        pm_metal_boot_tree_item("mods", PM_METAL_BOOT_TREE_FAIL, "mount");
        pm_metal_boot_tree_leave();
        return -1;
    }
    pm_metal_boot_tree_item("mods", PM_METAL_BOOT_TREE_OK, "/mods");
    pm_metal_boot_tree_item("root", PM_METAL_BOOT_TREE_OK, "packs");
    pm_metal_boot_tree_leave();

    pm_metal_boot_tree_enter("net");
    if (pm_metal_nic_bringup() != 0) {
        pm_metal_boot_tree_leave();
        return -1;
    }
    if (pm_metal_net_ip_init(0, 0, 0) != 0 || !pm_metal_net_ip_ready()) {
        pm_metal_boot_tree_item("ip", PM_METAL_BOOT_TREE_FAIL, "init");
        pm_metal_boot_tree_leave();
        return -1;
    }
    memset(&lease, 0, sizeof(lease));
    if (pm_metal_net_dhcp_run(&lease) != 0) {
        pm_metal_boot_tree_item("dhcp", PM_METAL_BOOT_TREE_FAIL, "lease");
        pm_metal_boot_tree_leave();
        return -1;
    }
    /* Lease already on lwIP netif; ensure DNS + gratuitous ARP. */
    if (pm_metal_net_ip_set_dns(lease.dns != 0u ? lease.dns : PM_METAL_NET_IP_DEFAULT_DNS) != 0) {
        pm_metal_boot_tree_item("dhcp", PM_METAL_BOOT_TREE_FAIL, "dns");
        pm_metal_boot_tree_leave();
        return -1;
    }
    (void)pm_metal_net_ip_announce();
    {
        char ip[16];
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", (unsigned)((lease.yiaddr >> 24) & 0xffu),
                 (unsigned)((lease.yiaddr >> 16) & 0xffu), (unsigned)((lease.yiaddr >> 8) & 0xffu),
                 (unsigned)(lease.yiaddr & 0xffu));
        pm_metal_boot_tree_item("dhcp", PM_METAL_BOOT_TREE_OK, ip);
    }
    {
        const char *home = pm_metal_cdn_default_url();
        const char *site = pm_metal_cdn_site_url();
        pm_metal_cdn_mode_t mode = pm_metal_cdn_site_mode();
        if (mode == PM_METAL_CDN_MODE_OFF) {
            pm_metal_boot_tree_item("cdn", PM_METAL_BOOT_TREE_DIM, "off");
        } else if (mode == PM_METAL_CDN_MODE_REPLACE && site != NULL) {
            pm_metal_boot_tree_item("cdn", PM_METAL_BOOT_TREE_OK, site);
        } else if (site != NULL && home != NULL) {
            pm_metal_boot_tree_item("cdn", PM_METAL_BOOT_TREE_OK, home);
            pm_metal_boot_tree_item("cdn+site", PM_METAL_BOOT_TREE_OK, site);
        } else {
            pm_metal_boot_tree_item(
                "cdn",
                home ? PM_METAL_BOOT_TREE_OK : PM_METAL_BOOT_TREE_DIM,
                home ? home : "unset");
        }
    }
    if (pm_metal_net_ssh_autoload() != 0) {
        pm_metal_boot_tree_item("ssh", PM_METAL_BOOT_TREE_FAIL, "init");
        pm_metal_boot_tree_leave();
        return -1;
    }
    pm_metal_boot_tree_item("ssh", PM_METAL_BOOT_TREE_OK, "autoload");
    pm_metal_boot_tree_leave();

    pm_metal_boot_tree_enter("async");
    for (i = 0; i < 64; i++) {
        (void)pm_metal_async_run_poll();
    }
    pm_metal_boot_tree_item("runners", PM_METAL_BOOT_TREE_OK, "poll");
    pm_metal_boot_tree_leave();

    pm_metal_boot_tree_enter("wasm");
#if defined(METAL_LINK_WAMR) && METAL_LINK_WAMR
    pm_metal_boot_tree_item("wamr_host", PM_METAL_BOOT_TREE_OK, "linked");
#else
    pm_metal_boot_tree_item("wamr_host", PM_METAL_BOOT_TREE_DIM, "off");
#endif
    pm_metal_boot_tree_leave();

    boot_tree_emit_externals();

    pm_metal_boot_tree_ready_ok();
    pm_metal_boot_rainbow_metalpython(PM_METAL_VERSION, pm_metal_hal_cpu_label());

#if defined(METAL_LIVE) && METAL_LIVE
    {
        extern int32_t pm_metal_net_services_start(void);
        extern void uart_puts(const char *s);
        uart_puts("services...\n");
        if (pm_metal_net_services_start() != 0) {
            uart_puts("services fail\n");
        } else {
            uart_puts("live http\n");
        }
    }
#endif
    return 0;
}

#endif /* ARCH_WASM */

int pm_metal_boot(void)
{
#if defined(PM_METAL_CFG_ARCH_WASM) && PM_METAL_CFG_ARCH_WASM
    return boot_wasm_sim();
#else
    return boot_native();
#endif
}
