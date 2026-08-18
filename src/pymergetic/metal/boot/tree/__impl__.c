/* pymergetic.metal.boot.tree — surfaces, backend, walk. Cards attach the text. */
#include "pymergetic/metal/boot/tree/__exports__.h"
#include "pymergetic/metal/boot/tree/__types__.h"

#include "pymergetic/metal/boot/__types__.h"
#include "pymergetic/metal/console.h"
#include "pymergetic/metal/services.h"
#include "pymergetic/metal/util/ascii.h"
#include "pymergetic/metal/util/tree.h"
#include "pymergetic/wasmmod/boot.h"
#include "pymergetic/wasmmod/registry.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PM_METAL_BOOT_TREE_VERSION "0.1.0"
#define PM_METAL_BOOT_UTIL_MAX 16
#define PM_METAL_BOOT_UTIL_NAME 24
#define PM_METAL_BOOT_MSG_MAX 16

/* Style shorthand used by motd/shutdown/print — the shared palettes from the
 * pymergetic.metal.util.tree card (the only place SGR codes are defined). */
#define PM_METAL_BOOT_SGR_DIM PM_METAL_UTIL_TREE_SGR_DIM
#define PM_METAL_BOOT_SGR_OK PM_METAL_UTIL_TREE_SGR_OK
#define PM_METAL_BOOT_SGR_SIM PM_METAL_UTIL_TREE_SGR_SIM
#define PM_METAL_BOOT_SGR_FAIL PM_METAL_UTIL_TREE_SGR_FAIL
#define PM_METAL_BOOT_SGR_WARN PM_METAL_UTIL_TREE_SGR_WARN
#define PM_METAL_BOOT_SGR_RST PM_METAL_UTIL_TREE_SGR_RST

#if !defined(PM_METAL_FIRMWARE) && !defined(__EMSCRIPTEN__)
int execv(const char *path, char *const argv[]);
void _exit(int status);
#endif

typedef struct {
    uint32_t used;
    uint32_t surf;
    uint32_t order;
    pm_metal_boot_msg_fn fn;
} pm_metal_boot_msg_slot_t;

static pm_metal_boot_msg_slot_t s_msg[PM_METAL_BOOT_MSG_MAX];
static uint32_t s_nfail;

void pm_metal_boot_msg_fail(void) {
    s_nfail++;
}

void pm_metal_boot_msg_count(char *dst, unsigned cap, const char *lead, unsigned n, const char *unit) {
    if (lead == NULL) {
        lead = "";
    }
    snprintf(dst, cap, "%s%u %s(s)", lead, n, unit);
}

void pm_metal_boot_msg_line(const char *s) {
    uint32_t n = 0;
    if (s == NULL) {
        s = "";
    }
    while (s[n] != 0) {
        n++;
    }
    (void)pm_metal_console_write(s, n);
    (void)pm_metal_console_write("\n", 1);
}

int32_t pm_metal_boot_msg_attach(uint32_t surf, uint32_t order, pm_metal_boot_msg_fn fn) {
    uint32_t i;
    if (fn == NULL || (surf != PM_METAL_BOOT_SURF_TREE && surf != PM_METAL_BOOT_SURF_MOTD)) {
        return -1;
    }
    for (i = 0; i < PM_METAL_BOOT_MSG_MAX; i++) {
        if (s_msg[i].used && s_msg[i].surf == surf && s_msg[i].fn == fn) {
            s_msg[i].order = order;
            return 0;
        }
    }
    for (i = 0; i < PM_METAL_BOOT_MSG_MAX; i++) {
        if (!s_msg[i].used) {
            s_msg[i].used = 1;
            s_msg[i].surf = surf;
            s_msg[i].order = order;
            s_msg[i].fn = fn;
            return 0;
        }
    }
    return -1;
}

uint32_t pm_metal_boot_msg_attached(uint32_t surf) {
    uint32_t i;
    uint32_t n = 0;
    for (i = 0; i < PM_METAL_BOOT_MSG_MAX; i++) {
        if (s_msg[i].used && s_msg[i].surf == surf) {
            n++;
        }
    }
    return n;
}

static uint32_t collect_surf(uint32_t surf, pm_metal_boot_msg_fn *out, uint32_t cap) {
    uint32_t i;
    uint32_t n = 0;
    uint32_t order[PM_METAL_BOOT_MSG_MAX];
    for (i = 0; i < PM_METAL_BOOT_MSG_MAX; i++) {
        uint32_t j;
        if (!s_msg[i].used || s_msg[i].surf != surf || n >= cap) {
            continue;
        }
        j = n;
        while (j > 0 && order[j - 1u] > s_msg[i].order) {
            out[j] = out[j - 1u];
            order[j] = order[j - 1u];
            j--;
        }
        out[j] = s_msg[i].fn;
        order[j] = s_msg[i].order;
        n++;
    }
    return n;
}

/* The node renderer lives in the shared pymergetic.metal.util.tree card. It
 * draws the same dim `+--`/`` `-- `` glyphs, pads depth-0 names, and colors
 * `ok`:green, `sim`:cyan, `FAIL`:red tokens — but it supports arbitrary depth,
 * so a boot card could attach a 4-level branch cleanly too. */
void pm_metal_boot_msg_item(int last, int depth, int parent_cont, const char *name, const char *detail) {
    pm_metal_util_tree_item(last, depth, parent_cont, name, detail);
}

static void walk(uint32_t surf) {
    pm_metal_boot_msg_fn fns[PM_METAL_BOOT_MSG_MAX];
    uint32_t n = collect_surf(surf, fns, PM_METAL_BOOT_MSG_MAX);
    uint32_t i;
    for (i = 0; i < n; i++) {
        fns[i](0);
    }
}

static int32_t pm_metal_boot_tree_init(pm_util_mem_arena_t *arena) {
    (void)arena;
    return 0;
}

static void pm_metal_boot_tree_deinit(void) {}

static int fqn_has_pfx(const char *s, uint32_t n, const char *pfx) {
    uint32_t i = 0;
    if (s == NULL || pfx == NULL) {
        return 0;
    }
    while (pfx[i] != 0) {
        if (i >= n || s[i] != pfx[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static int fqn_eq_or_child(const char *s, uint32_t n, const char *pfx) {
    uint32_t i = 0;
    if (!fqn_has_pfx(s, n, pfx)) {
        return 0;
    }
    while (pfx[i] != 0) {
        i++;
    }
    return n == i || s[i] == '.';
}

static int skip_util_leaf(const char *leaf) {
    return strcmp(leaf, "gen") == 0 || strcmp(leaf, "pysample") == 0;
}

static void util_first_leaf(char *dst, unsigned cap, const char *s, uint32_t n) {
    static const char pfx[] = "pymergetic.util.";
    uint32_t skip = (uint32_t)(sizeof(pfx) - 1u);
    unsigned i = 0;
    if (dst == NULL || cap == 0) {
        return;
    }
    dst[0] = 0;
    if (s == NULL || n <= skip) {
        return;
    }
    while (skip + i < n && s[skip + i] != '.' && i + 1u < cap) {
        dst[i] = s[skip + i];
        i++;
    }
    dst[i] = 0;
}

/* This card's own faces: what the registry knows, not what other cards own. */
static void msg_mods(int last) {
    char leaves[PM_METAL_BOOT_UTIL_MAX][PM_METAL_BOOT_UTIL_NAME];
    uint32_t nleaf = 0;
    uint32_t nmetal = 0;
    uint32_t nwasm = 0;
    uint32_t nmod = pm_wasmmod_registry_module_count();
    uint32_t i;
    uint32_t nfam;
    uint32_t fi;
    char detail[48];
    int have_metal;
    int have_util;
    int have_wasm;

    (void)last;
    memset(leaves, 0, sizeof(leaves));
    for (i = 0; i < nmod; i++) {
        uint8_t buf[192];
        uint32_t len = (uint32_t)sizeof(buf);
        char leaf[PM_METAL_BOOT_UTIL_NAME];
        uint32_t k;
        int seen = 0;
        if (pm_wasmmod_registry_module_at(i, buf, &len) == 0 || len == 0) {
            continue;
        }
        if (fqn_eq_or_child((const char *)buf, len, "pymergetic.metal")) {
            nmetal++;
            continue;
        }
        if (fqn_eq_or_child((const char *)buf, len, "pymergetic.wasmmod")) {
            nwasm++;
            continue;
        }
        if (!fqn_has_pfx((const char *)buf, len, "pymergetic.util.")) {
            continue;
        }
        util_first_leaf(leaf, sizeof(leaf), (const char *)buf, len);
        if (leaf[0] == 0 || skip_util_leaf(leaf)) {
            continue;
        }
        for (k = 0; k < nleaf; k++) {
            if (strcmp(leaves[k], leaf) == 0) {
                seen = 1;
                break;
            }
        }
        if (!seen && nleaf < PM_METAL_BOOT_UTIL_MAX) {
            memcpy(leaves[nleaf], leaf, PM_METAL_BOOT_UTIL_NAME);
            nleaf++;
        }
    }
    for (i = 1; i < nleaf; i++) {
        char tmp[PM_METAL_BOOT_UTIL_NAME];
        uint32_t j = i;
        memcpy(tmp, leaves[i], PM_METAL_BOOT_UTIL_NAME);
        while (j > 0 && strcmp(leaves[j - 1u], tmp) > 0) {
            memcpy(leaves[j], leaves[j - 1u], PM_METAL_BOOT_UTIL_NAME);
            j--;
        }
        memcpy(leaves[j], tmp, PM_METAL_BOOT_UTIL_NAME);
    }
    have_metal = nmetal != 0;
    have_util = nleaf != 0;
    have_wasm = nwasm != 0;
    nfam = (uint32_t)have_metal + (uint32_t)have_util + (uint32_t)have_wasm;
    if (nfam == 0u) {
        pm_metal_boot_msg_item(0, 0, 0, "mods", "-");
        return;
    }
    pm_metal_boot_msg_count(detail, sizeof(detail), "ok  ", nfam, "family");
    pm_metal_boot_msg_item(0, 0, 0, "mods", detail);
    fi = 0;
    if (have_metal) {
        char det[40];
        pm_metal_boot_msg_count(det, sizeof(det), "ok  ", nmetal, "card");
        pm_metal_boot_msg_item(fi + 1u == nfam, 1, 1, "metal*", det);
        fi++;
    }
    if (have_util) {
        char det[40];
        uint32_t u;
        int fam_last = (fi + 1u == nfam);
        pm_metal_boot_msg_count(det, sizeof(det), "ok  ", nleaf, "card");
        pm_metal_boot_msg_item(fam_last, 1, 1, "util", det);
        for (u = 0; u < nleaf; u++) {
            pm_metal_boot_msg_item(u + 1u == nleaf, 2, !fam_last, leaves[u], "ok");
        }
        fi++;
    }
    if (have_wasm) {
        char det[40];
        pm_metal_boot_msg_count(det, sizeof(det), "ok  ", nwasm, "card");
        pm_metal_boot_msg_item(fi + 1u == nfam, 1, 1, "wasmmod*", det);
    }
}

static uint32_t kinds_present(const uint32_t *nkind) {
    uint32_t i;
    uint32_t n = 0;
    for (i = 0; i < 3u; i++) {
        if (nkind[i] != 0u) {
            n++;
        }
    }
    return n;
}

static void msg_wasm(int last) {
    static const char *const kind_name[3] = { "wasm", "aot", "elf" };
    uint32_t nmod = pm_wasmmod_registry_module_count();
    uint32_t nkind[3] = { 0, 0, 0 };
    uint32_t npack = 0;
    uint32_t i;
    uint32_t shown = 0;
    char detail[48];

    (void)last;
    for (i = 0; i < nmod; i++) {
        uint8_t buf[192];
        uint32_t len = (uint32_t)sizeof(buf);
        int32_t c;
        if (pm_wasmmod_registry_module_at(i, buf, &len) == 0 || len == 0) {
            continue;
        }
        c = pm_wasmmod_registry_container(buf, len);
        if (c < 0 || c > (int32_t)PM_WASMMOD_REGISTRY_CONTAINER_ELF) {
            continue;
        }
        nkind[c]++;
        npack++;
    }
    if (npack == 0u) {
        pm_metal_boot_msg_item(0, 0, 0, "wasm", "-  no pack loaded");
        return;
    }
    pm_metal_boot_msg_count(detail, sizeof(detail), "ok  ", npack, "pack");
    pm_metal_boot_msg_item(0, 0, 0, "wasm", detail);
    for (i = 0; i < 3u; i++) {
        char det[40];
        if (nkind[i] == 0u) {
            continue;
        }
        shown++;
        pm_metal_boot_msg_count(det, sizeof(det), "ok  ", nkind[i], "pack");
        pm_metal_boot_msg_item(shown == kinds_present(nkind), 1, 1, kind_name[i], det);
    }
}

static void msg_repl(int last) {
    /* Interactive hint on the MOTD surface: list every registered service with
     * its default port and a run sample, plus how many live instances exist.
     * `m.serve()` starts the default instance of all of them on ANY. */
    char detail[192];
    unsigned off = 0;
    uint32_t n = pm_metal_services_count();
    uint32_t i;
    (void)last;
    /* Fits: "import pymergetic.metal as m  run m.serve() -> ssh:2222 asgi:8090" */
    off += (unsigned)snprintf(detail + off, sizeof(detail) - off,
        "import pymergetic.metal as m" PM_METAL_BOOT_SGR_DIM "  run m.serve() ->"
        PM_METAL_BOOT_SGR_RST);
    for (i = 0; i < n && off + 24u < sizeof(detail); i++) {
        const char *name = pm_metal_services_name(i);
        uint16_t port = pm_metal_services_port(i);
        uint32_t up = pm_metal_services_instances(i);
        if (name == NULL) {
            continue;
        }
        off += (unsigned)snprintf(detail + off, sizeof(detail) - off,
            " " PM_METAL_BOOT_SGR_SIM "%s:%u" PM_METAL_BOOT_SGR_RST,
            name, (unsigned)port);
        if (up > 0u) {
            off += (unsigned)snprintf(detail + off, sizeof(detail) - off,
                PM_METAL_BOOT_SGR_OK "(%u up)" PM_METAL_BOOT_SGR_RST, up);
        }
    }
    pm_metal_boot_msg_item(1, 0, 0, "repl", detail);
}

int32_t pm_metal_boot_tree_print(void) {
    const char *seat = pm_metal_boot_fill_seat();
    char ready[32];
    s_nfail = 0;
    pm_metal_boot_msg_line("");
    pm_metal_util_ascii_log_styled(PM_METAL_UTIL_ASCII_STYLE_ACCENT, "METAL");
    pm_metal_boot_msg_line("");
    pm_metal_boot_msg_item(0, 0, 0, "pymergetic metal", PM_METAL_BOOT_TREE_VERSION);
    pm_metal_boot_msg_line(PM_METAL_BOOT_SGR_DIM "|" PM_METAL_BOOT_SGR_RST);
    pm_metal_boot_msg_item(0, 0, 0, "arch", seat != NULL ? seat : "host");
    walk(PM_METAL_BOOT_SURF_TREE);
    if (s_nfail == 0u) {
        pm_metal_boot_msg_item(1, 0, 0, "ready", "ok");
    } else {
        pm_metal_boot_msg_count(ready, sizeof(ready), "FAIL  ", s_nfail, "node");
        pm_metal_boot_msg_item(1, 0, 0, "ready", ready);
    }
    pm_metal_boot_msg_line("");
    return 0;
}

void pm_metal_boot_motd(void) {
    const char *seat = pm_metal_boot_fill_seat();
    char title[96];
    if (seat == NULL) {
        seat = "host";
    }
    pm_metal_boot_msg_line("");
    pm_metal_util_ascii_log_rainbow("MetalPython");
    snprintf(title, sizeof(title), "%sMetalPython%s %s%s%s @ %s", PM_METAL_BOOT_SGR_SIM,
        PM_METAL_BOOT_SGR_RST, PM_METAL_BOOT_SGR_OK, PM_METAL_BOOT_TREE_VERSION,
        PM_METAL_BOOT_SGR_RST, seat);
    pm_metal_boot_msg_line(title);
    walk(PM_METAL_BOOT_SURF_MOTD);
}

/* Portable ~1s busy-wait for the shutdown/reboot countdown. No POSIX
 * nanosleep (firmware has no OS) and no clock card dependency: a volatile
 * NOP-pump is the countdown feel on every seat (unix/emcc/firmware). The loop
 * counter is volatile so the compiler cannot hoist the whole spin; the count
 * is heuristic (≈1s on a ≈3GHz unix host; slower seats pause longer, which is
 * the point — time to read the banner before power off). */
static void pm_metal_boot_tick_sleep(void) {
    volatile uint32_t s;
    for (s = 0; s < 470000000u; s++) {
    }
}

static void pm_metal_boot_countdown(const char *done, unsigned n) {
    unsigned i;
    unsigned dl = 0;
    while (done[dl] != 0) {
        dl++;
    }
    for (i = 0; i < n; i++) {
        unsigned d = n - i;
        char line[24];
        unsigned p = 0;
        line[p++] = (char)('0' + (d % 10u));
        if (i + 1u == n) {
            unsigned k;
            line[p++] = ' ';
            for (k = 0; k < dl; k++) {
                line[p++] = done[k];
            }
        }
        line[p++] = '\n';
        (void)pm_metal_console_write(line, p);
        pm_metal_boot_tick_sleep();
    }
}

void pm_metal_boot_shutdown(int reboot) {
    char title[96];
    char art[160];
    uint32_t nboot;
    pm_metal_boot_msg_line("");
    snprintf(title, sizeof(title), "%smetal-boot: %s%s", PM_METAL_BOOT_SGR_WARN,
        reboot ? "reboot" : "shutdown", PM_METAL_BOOT_SGR_RST);
    pm_metal_boot_msg_line(title);
    nboot = pm_mod_boot_count();
    pm_mod_boot_unwind();
    if (nboot == 0u) {
        pm_metal_boot_msg_item(1, 0, 0, "stop", "-  nothing booted");
    } else {
        char detail[32];
        pm_metal_boot_msg_count(detail, sizeof(detail), "ok  ", nboot, "card");
        pm_metal_boot_msg_item(1, 0, 0, "stop", detail);
    }
    /* Big fat red system-down / rebooting banner + version, then a short
     * countdown so the reverse-boot result is visible before power off. */
    pm_metal_boot_msg_line("");
    snprintf(title, sizeof(title), "%smetal version %s%s", PM_METAL_BOOT_SGR_FAIL,
        PM_METAL_BOOT_TREE_VERSION, PM_METAL_BOOT_SGR_RST);
    pm_metal_boot_msg_line(title);
    snprintf(art, sizeof(art), "%s*** %s — %s in 3s ***%s", PM_METAL_BOOT_SGR_FAIL,
        reboot ? "REBOOTING" : "SYSTEM DOWN", reboot ? "reset" : "power off",
        PM_METAL_BOOT_SGR_RST);
    pm_metal_boot_msg_line(art);
    if (reboot) {
        pm_metal_boot_countdown("REBOOTING", 3u);
    } else {
        pm_metal_boot_countdown("SYSTEM DOWN", 3u);
    }
#if defined(PM_METAL_FIRMWARE)
#if defined(__i386__) || defined(__x86_64__)
    if (reboot) {
        __asm__ volatile("outb %0, %1" : : "a"((unsigned char)0xfe), "Nd"((unsigned short)0x64));
    }
    for (;;) {
        __asm__ volatile("hlt");
    }
#else
    (void)reboot;
    for (;;) {
        __asm__ volatile("wfi");
    }
#endif
#elif defined(__EMSCRIPTEN__)
    (void)reboot;
#else
    if (reboot) {
        char *argv[2];
        argv[0] = (char *)"micropython";
        argv[1] = NULL;
        execv("/proc/self/exe", argv);
    }
    _exit(0);
#endif
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.boot.tree, pm_metal_boot_tree_print, pm_metal_boot_tree_print, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.boot.tree, pm_metal_boot_motd, pm_metal_boot_motd, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.boot.tree, pm_metal_boot_shutdown, pm_metal_boot_shutdown, void(int));
PM_MOD_EXPORT_C(pymergetic.metal.boot.tree, pm_metal_boot_msg_attach, pm_metal_boot_msg_attach,
    int32_t(uint32_t, uint32_t, pm_metal_boot_msg_fn));
PM_MOD_EXPORT_C(pymergetic.metal.boot.tree, pm_metal_boot_msg_attached, pm_metal_boot_msg_attached, uint32_t(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.boot.tree, pm_metal_boot_msg_item, pm_metal_boot_msg_item,
    void(int, int, int, const char *, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.boot.tree, pm_metal_boot_msg_line, pm_metal_boot_msg_line, void(const char *));
PM_MOD_EXPORT_C(pymergetic.metal.boot.tree, pm_metal_boot_msg_fail, pm_metal_boot_msg_fail, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.boot.tree, pm_metal_boot_msg_count, pm_metal_boot_msg_count,
    void(char *, unsigned, const char *, unsigned, const char *));

PM_METAL_BOOT_MSG_C(PM_METAL_BOOT_SURF_TREE, PM_METAL_BOOT_MSG_MODS, msg_mods);
PM_METAL_BOOT_MSG_C(PM_METAL_BOOT_SURF_TREE, PM_METAL_BOOT_MSG_WASM, msg_wasm);
PM_METAL_BOOT_MSG_C(PM_METAL_BOOT_SURF_MOTD, PM_METAL_BOOT_MSG_MOTD_REPL, msg_repl);

PM_MOD_BOOT_C(pymergetic.metal.boot.tree, pm_metal_boot_tree_init, pm_metal_boot_tree_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.boot.tree, pymergetic.metal.console);
PM_MOD_BOOTDEP_C(pymergetic.metal.boot.tree, pymergetic.metal.util.tree);
