/* pymergetic.metal.boot.tree — floor tree + quit/reboot/shutdown. */
#include "pymergetic/metal/boot/tree/__exports__.h"

#include "pymergetic/metal/boot/__types__.h"
#include "pymergetic/metal/boot/externals.h"
#include "pymergetic/metal/async.h"
#include "pymergetic/metal/console.h"
#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/util/ascii.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/boot.h"
#include "pymergetic/wasmmod/net/cdn/__exports__.h"
#include "pymergetic/wasmmod/registry.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef PM_METAL_DT_WALK
#define PM_METAL_DT_WALK 128
#endif

#define PM_METAL_BOOT_TREE_NAME_PAD 13
#define PM_METAL_BOOT_TREE_LINE 384
#define PM_METAL_BOOT_TREE_VERSION "0.1.0"
#define PM_METAL_BOOT_UTIL_MAX 16
#define PM_METAL_BOOT_UTIL_NAME 24
#define PM_METAL_BOOT_SGR_DIM "\033[2m"
#define PM_METAL_BOOT_SGR_OK "\033[32m"
#define PM_METAL_BOOT_SGR_SIM "\033[36m"
#define PM_METAL_BOOT_SGR_FAIL "\033[31m"
#define PM_METAL_BOOT_SGR_WARN "\033[33m"
#define PM_METAL_BOOT_SGR_RST "\033[0m"

/* Nodes that printed FAIL during this walk. `ready` is the sum, not a wish. */
static uint32_t s_nfail;

static void note_fail(void) {
    s_nfail++;
}

static int32_t pm_metal_boot_tree_init(pm_util_mem_arena_t *arena) {
    (void)arena;
    return 0;
}

static void pm_metal_boot_tree_deinit(void) {}

/* Same grammar everywhere: "1 region(s)", "4 runner(s)", "2 node(s)". */
static void fmt_count(char *dst, unsigned cap, const char *lead, unsigned n, const char *unit) {
    if (lead == NULL) {
        lead = "";
    }
    snprintf(dst, cap, "%s%u %s(s)", lead, n, unit);
}

#if !defined(PM_METAL_FIRMWARE) && !defined(__EMSCRIPTEN__)
/* POSIX hosted halt. Do not include <unistd.h>: firmware fwinc/unistd.h is
 * on the clangd path for this TU and does not declare these. */
int execv(const char *path, char *const argv[]);
void _exit(int status);
#endif

static void emit_line(const char *s) {
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

static int token_is(const char *s, unsigned n, const char *w) {
    unsigned i = 0;
    if (w == NULL) {
        return 0;
    }
    while (w[i] != 0) {
        if (i >= n || s[i] != w[i]) {
            return 0;
        }
        i++;
    }
    return i == n;
}

static void paint_detail(char *dst, unsigned cap, const char *detail) {
    unsigned oi = 0;
    const char *p;
    if (dst == NULL || cap == 0) {
        return;
    }
    dst[0] = 0;
    if (detail == NULL) {
        return;
    }
    p = detail;
    while (p[0] != 0 && oi + 24u < cap) {
        const char *tok;
        unsigned n;
        const char *col;
        if (p[0] == ' ') {
            dst[oi++] = *p++;
            continue;
        }
        tok = p;
        while (p[0] != 0 && p[0] != ' ') {
            p++;
        }
        n = (unsigned)(p - tok);
        col = NULL;
        if (token_is(tok, n, "ok")) {
            col = PM_METAL_BOOT_SGR_OK;
        } else if (token_is(tok, n, "sim")) {
            col = PM_METAL_BOOT_SGR_SIM;
        } else if (token_is(tok, n, "FAIL")) {
            col = PM_METAL_BOOT_SGR_FAIL;
        }
        if (col != NULL) {
            unsigned cl = 0;
            unsigned rl = 0;
            while (col[cl] != 0) {
                cl++;
            }
            while (PM_METAL_BOOT_SGR_RST[rl] != 0) {
                rl++;
            }
            if (oi + cl + n + rl >= cap) {
                break;
            }
            memcpy(dst + oi, col, cl);
            oi += cl;
            memcpy(dst + oi, tok, n);
            oi += n;
            memcpy(dst + oi, PM_METAL_BOOT_SGR_RST, rl);
            oi += rl;
        } else {
            if (oi + n >= cap) {
                break;
            }
            memcpy(dst + oi, tok, n);
            oi += n;
        }
    }
    if (oi < cap) {
        dst[oi] = 0;
    } else {
        dst[cap - 1u] = 0;
    }
}

static const char *class_name(int32_t class) {
    switch (class) {
    case PM_METAL_DT_CLASS_NET:
        return "net";
    case PM_METAL_DT_CLASS_BLK:
        return "blk";
    case PM_METAL_DT_CLASS_RTC:
        return "rtc";
    case PM_METAL_DT_CLASS_MEM:
        return "mem";
    case PM_METAL_DT_CLASS_GFX:
        return "gfx";
    case PM_METAL_DT_CLASS_AUDIO:
        return "audio";
    case PM_METAL_DT_CLASS_INPUT:
        return "input";
    default:
        return "?";
    }
}

static const char *bus_name(int32_t bus) {
    switch (bus) {
    case PM_METAL_DT_BUS_PLATFORM:
        return "plat";
    case PM_METAL_DT_BUS_PCI:
        return "pci";
    case PM_METAL_DT_BUS_ISA:
        return "isa";
    case PM_METAL_DT_BUS_VIRTIO:
        return "virtio";
    case PM_METAL_DT_BUS_MMIO:
        return "mmio";
    default:
        return "?";
    }
}

static void fmt_size64(char *out, unsigned cap, uint64_t n) {
    if (n >= (1024ull * 1024ull)) {
        snprintf(out, cap, "%u MiB", (unsigned)(n / (1024ull * 1024ull)));
        return;
    }
    if (n >= 1024ull) {
        snprintf(out, cap, "%u KiB", (unsigned)(n / 1024ull));
        return;
    }
    snprintf(out, cap, "%u B", (unsigned)n);
}

static void fmt_base_size(char *out, unsigned cap, uint64_t base, uint64_t len) {
    char human[24];
    fmt_size64(human, sizeof(human), len);
    snprintf(out, cap, "base=0x%08x%08x size=%s", (unsigned)(base >> 32), (unsigned)base, human);
}

static void item(int last, int depth, int parent_cont, const char *name, const char *detail) {
    char line[PM_METAL_BOOT_TREE_LINE];
    char painted[160];
    unsigned nlen = 0;
    const char *br = last ? "`--" : "+--";
    if (name == NULL) {
        name = "?";
    }
    while (name[nlen] != 0) {
        nlen++;
    }
    paint_detail(painted, sizeof(painted), detail);
    /* Dim the glyph+name as one span so prove can still grep "`-- ready". */
    if (depth == 0) {
        if (detail != NULL && detail[0] != 0 && nlen < PM_METAL_BOOT_TREE_NAME_PAD) {
            char padded[PM_METAL_BOOT_TREE_NAME_PAD + 1];
            unsigned p;
            for (p = 0; p < PM_METAL_BOOT_TREE_NAME_PAD; p++) {
                padded[p] = (p < nlen) ? name[p] : ' ';
            }
            padded[PM_METAL_BOOT_TREE_NAME_PAD] = 0;
            snprintf(line, sizeof(line), "%s%s %s%s%s", PM_METAL_BOOT_SGR_DIM, br, padded,
                PM_METAL_BOOT_SGR_RST, painted);
        } else if (detail != NULL && detail[0] != 0) {
            snprintf(line, sizeof(line), "%s%s %s %s%s", PM_METAL_BOOT_SGR_DIM, br, name,
                PM_METAL_BOOT_SGR_RST, painted);
        } else {
            snprintf(line, sizeof(line), "%s%s %s%s", PM_METAL_BOOT_SGR_DIM, br, name,
                PM_METAL_BOOT_SGR_RST);
        }
    } else if (depth == 1) {
        const char *pad = parent_cont ? "|   " : "    ";
        if (detail != NULL && detail[0] != 0) {
            snprintf(line, sizeof(line), "%s%s%s %s  %s%s", PM_METAL_BOOT_SGR_DIM, pad, br, name,
                PM_METAL_BOOT_SGR_RST, painted);
        } else {
            snprintf(line, sizeof(line), "%s%s%s %s%s", PM_METAL_BOOT_SGR_DIM, pad, br, name,
                PM_METAL_BOOT_SGR_RST);
        }
    } else if (depth == 2) {
        /* Area children. parent_cont = area has a following sibling (spare /
         * root). Mem/fs are never last among the floor-tree roots. */
        const char *stem = parent_cont ? "|   " : "    ";
        if (detail != NULL && detail[0] != 0) {
            snprintf(line, sizeof(line), "%s|   %s%s %s  %s%s", PM_METAL_BOOT_SGR_DIM, stem, br, name,
                PM_METAL_BOOT_SGR_RST, painted);
        } else {
            snprintf(line, sizeof(line), "%s|   %s%s %s%s", PM_METAL_BOOT_SGR_DIM, stem, br, name,
                PM_METAL_BOOT_SGR_RST);
        }
    } else {
        /* Depth 3: family leaves under mods. parent_cont = that family has a
         * following sibling. fs and mods always continue (net / root follow). */
        const char *fam = parent_cont ? "|   " : "    ";
        if (detail != NULL && detail[0] != 0) {
            snprintf(line, sizeof(line), "%s|   |   %s%s %s  %s%s", PM_METAL_BOOT_SGR_DIM, fam, br,
                name, PM_METAL_BOOT_SGR_RST, painted);
        } else {
            snprintf(line, sizeof(line), "%s|   |   %s%s %s%s", PM_METAL_BOOT_SGR_DIM, fam, br, name,
                PM_METAL_BOOT_SGR_RST);
        }
    }
    emit_line(line);
}

static int32_t collect_class(int32_t want, int32_t *ids, int32_t cap) {
    int32_t i;
    int32_t n = 0;
    for (i = 0; i < PM_METAL_DT_WALK && n < cap; i++) {
        int32_t class;
        if (pm_metal_dt_compat(i) == NULL) {
            continue;
        }
        class = pm_metal_dt_class(i);
        if (want == PM_METAL_DT_CLASS_MEM) {
            if (class != PM_METAL_DT_CLASS_MEM) {
                continue;
            }
        } else if (class == PM_METAL_DT_CLASS_MEM) {
            continue;
        }
        ids[n++] = i;
    }
    return n;
}

static void emit_mem(int last) {
    pm_util_mem_arena_t *arena = pm_metal_boot_arena();
    uint64_t kbase = 0;
    uint64_t klen = 0;
    int have_k = pm_metal_boot_fill_kernel(&kbase, &klen) == 0 && klen != 0;
    int nreg = 0;
    int have_spare = 0;
    char loc[80];
    char detail[sizeof(loc) + 8];
    char human[24];
    size_t bytes;
    size_t mapped;
    size_t hole;
    size_t heap;
    size_t spare = 0;
    const char *tag;
    uint64_t abase;

    if (arena != NULL) {
        nreg++;
        spare = pm_util_mem_arena_spare(arena);
        if (spare != 0) {
            have_spare = 1;
            nreg++;
        }
    }
    if (have_k) {
        nreg++;
    }
    if (nreg == 0) {
        item(last, 0, 0, "mem", "-");
        return;
    }
    fmt_count(detail, sizeof(detail), "ok  ", (unsigned)nreg, "region");
    item(last, 0, 0, "mem", detail);
    if (have_k) {
        fmt_base_size(loc, sizeof(loc), kbase, klen);
        snprintf(detail, sizeof(detail), "ok  %s", loc);
        item(arena == NULL, 1, !last, "kernel", detail);
    }
    if (arena == NULL) {
        return;
    }
    bytes = pm_util_mem_arena_bytes(arena);
    mapped = pm_util_mem_arena_map_used(arena);
    hole = pm_util_mem_arena_hole(arena);
    heap = pm_util_mem_arena_heap_used(arena);
    abase = (uint64_t)(uintptr_t)arena;
    fmt_base_size(loc, sizeof(loc), abase, (uint64_t)bytes);
    item(!have_spare, 1, !last, "area", loc);
    if (mapped != 0) {
        fmt_size64(human, sizeof(human), (uint64_t)mapped);
        tag = pm_metal_boot_fill_map_label();
        if (tag != NULL && tag[0] != 0) {
            snprintf(detail, sizeof(detail), "%s  %s", human, tag);
        } else {
            snprintf(detail, sizeof(detail), "%s", human);
        }
        item(0, 2, have_spare, "map", detail);
    }
    fmt_size64(human, sizeof(human), (uint64_t)hole);
    item(0, 2, have_spare, "hole", human);
    fmt_size64(human, sizeof(human), (uint64_t)heap);
    snprintf(detail, sizeof(detail), "ok  %s", human);
    item(1, 2, have_spare, "tlsf (heap)", detail);
    if (have_spare) {
        fmt_size64(human, sizeof(human), (uint64_t)spare);
        item(1, 1, !last, "spare", human);
    }
}

static void emit_cpu(int last) {
    uint32_t n = pm_metal_async_n_runners();
    const char *kind;
    char detail[48];
    char smp[64];
    if (n == 0) {
        n = 1;
    }
    kind = pm_metal_async_runner_kind();
    fmt_count(detail, sizeof(detail), "ok  ", n, "runner");
    item(last, 0, 0, "cpu", detail);
    if (kind != NULL && kind[0] == 's' && kind[1] == 'i' && kind[2] == 'm') {
        fmt_count(smp, sizeof(smp), "sim  ", n, "runner");
    } else {
        fmt_count(smp, sizeof(smp), "ok  ", n, "runner");
    }
    item(1, 1, !last, "smp", smp);
}

static void emit_devices(int last) {
    int32_t ids[PM_METAL_DT_WALK];
    int32_t n;
    int32_t i;
    char detail[32];
    char cat[32];
    char cons[48];
    uint32_t nvp;
    uint32_t v;
    int cons_last;
    n = collect_class(-1, ids, PM_METAL_DT_WALK);
    fmt_count(detail, sizeof(detail), "", (unsigned)n, "node");
    item(last, 0, 0, "devices", detail);
    fmt_count(cat, sizeof(cat), "ok  ", (unsigned)n, "node");
    item(0, 1, !last, "catalog", cat);
    nvp = pm_metal_console_ready() ? pm_metal_console_viewport_count() : 0u;
    cons_last = (n == 0);
    if (pm_metal_console_ready()) {
        char vpc[24];
        fmt_count(vpc, sizeof(vpc), "", nvp, "viewport");
        snprintf(cons, sizeof(cons), "ok  #%u  %s", (unsigned)pm_metal_console_id(), vpc);
    } else {
        snprintf(cons, sizeof(cons), "FAIL");
        note_fail();
    }
    item(cons_last, 1, !last, "console", cons);
    for (v = 0; v < nvp; v++) {
        char vp[48];
        const char *kind = pm_metal_console_viewport_kind(v);
        snprintf(vp, sizeof(vp), "ok  %s", kind != NULL ? kind : "?");
        item(v + 1u == nvp, 2, n != 0, "viewport", vp);
    }
    for (i = 0; i < n; i++) {
        char name[48];
        char bus[24];
        const char *compat = pm_metal_dt_compat(ids[i]);
        snprintf(name, sizeof(name), "%s/%s", class_name(pm_metal_dt_class(ids[i])),
            compat != NULL ? compat : "-");
        snprintf(bus, sizeof(bus), "bus=%s", bus_name(pm_metal_dt_bus(ids[i])));
        item(i + 1 == n, 1, !last, name, bus);
    }
}

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

/* Registered module families. This was printed under an `fs` node with a
 * `root sim memfs` leaf, and both were literals: there is no filesystem card in
 * the tree, so nothing could answer for either. What the registry does know is
 * which modules registered, so that is all this reports. */
static void emit_mods(int last) {
    char leaves[PM_METAL_BOOT_UTIL_MAX][PM_METAL_BOOT_UTIL_NAME];
    uint32_t nleaf = 0;
    uint32_t nmetal = 0;
    uint32_t nwasm = 0;
    uint32_t nmod;
    uint32_t i;
    uint32_t nfam;
    uint32_t fi;
    char detail[48];
    int have_metal;
    int have_util;
    int have_wasm;

    memset(leaves, 0, sizeof(leaves));
    nmod = pm_wasmmod_registry_module_count();
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
        item(last, 0, 0, "mods", "-");
        return;
    }
    fmt_count(detail, sizeof(detail), "ok  ", nfam, "family");
    item(last, 0, 0, "mods", detail);

    fi = 0;
    if (have_metal) {
        char det[40];
        int fam_last = (fi + 1u == nfam);
        fmt_count(det, sizeof(det), "ok  ", nmetal, "card");
        item(fam_last, 1, !last, "metal*", det);
        fi++;
    }
    if (have_util) {
        char det[40];
        int fam_last = (fi + 1u == nfam);
        uint32_t u;
        fmt_count(det, sizeof(det), "ok  ", nleaf, "card");
        item(fam_last, 1, !last, "util", det);
        for (u = 0; u < nleaf; u++) {
            item(u + 1u == nleaf, 2, !fam_last, leaves[u], "ok");
        }
        fi++;
    }
    if (have_wasm) {
        char det[40];
        int fam_last = (fi + 1u == nfam);
        fmt_count(det, sizeof(det), "ok  ", nwasm, "card");
        item(fam_last, 1, !last, "wasmmod*", det);
        fi++;
    }
}

static void emit_cdn_urls(int last, int depth, int parent_cont) {
    uint32_t n = pm_wasmmod_net_cdn_base_count();
    uint32_t i;
    char detail[48];
    if (n == 0u) {
        item(last, depth, parent_cont, "cdn", "-");
        return;
    }
    fmt_count(detail, sizeof(detail), "ok  ", n, "base");
    item(last, depth, parent_cont, "cdn", detail);
    for (i = 0; i < n; i++) {
        const char *b = pm_wasmmod_net_cdn_base_at(i);
        if (b == NULL || b[0] == 0) {
            b = "?";
        }
        item(i + 1u == n, depth + 1, !last, b, NULL);
    }
}

static void emit_net(int last) {
    int32_t n = pm_metal_drivers_net_count();
    int32_t i;
    int32_t seen = 0;
    int lo = pm_metal_net_ip_lo_ready() != 0;
    char detail[48];
    if (n == 0 && !lo) {
        item(last, 0, 0, "net", "-");
        emit_cdn_urls(1, 1, !last);
        return;
    }
    fmt_count(detail, sizeof(detail), "ok  ", (unsigned)(n + (int32_t)lo), "interface");
    item(last, 0, 0, "net", detail);
    if (lo) {
        item(0, 1, !last, "lo", "127.0.0.1");
    }
    for (i = 0; i < PM_METAL_DT_WALK && seen < n; i++) {
        int32_t h;
        uint32_t addr;
        uint8_t mac[6];
        char name[48];
        char det[64];
        const char *compat;
        if (pm_metal_dt_class(i) != PM_METAL_DT_CLASS_NET) {
            continue;
        }
        compat = pm_metal_dt_compat(i);
        h = pm_metal_drivers_net_by_dt(i);
        if (h < 0) {
            h = pm_metal_drivers_net_by_compat(compat != NULL ? compat : "", 0);
        }
        addr = h >= 0 ? pm_metal_net_ip_if_addr(h) : 0;
        if (h >= 0) {
            pm_metal_drivers_net_mac(h, mac);
        } else {
            mac[0] = mac[1] = mac[2] = mac[3] = mac[4] = mac[5] = 0;
        }
        snprintf(name, sizeof(name), "%s", compat != NULL ? compat : "net");
        if (addr != 0) {
            snprintf(det, sizeof(det), "%u.%u.%u.%u  %02x:%02x:%02x:%02x:%02x:%02x",
                (unsigned)((addr >> 24) & 0xffu), (unsigned)((addr >> 16) & 0xffu),
                (unsigned)((addr >> 8) & 0xffu), (unsigned)(addr & 0xffu), (unsigned)mac[0],
                (unsigned)mac[1], (unsigned)mac[2], (unsigned)mac[3], (unsigned)mac[4],
                (unsigned)mac[5]);
        } else {
            snprintf(det, sizeof(det), "%02x:%02x:%02x:%02x:%02x:%02x", (unsigned)mac[0],
                (unsigned)mac[1], (unsigned)mac[2], (unsigned)mac[3], (unsigned)mac[4],
                (unsigned)mac[5]);
        }
        item(0, 1, !last, name, det);
        seen++;
    }
    emit_cdn_urls(1, 1, !last);
}

static void emit_async(int last) {
    char detail[48];
    char run[64];
    uint32_t n;
    const char *kind;
    if (pm_metal_async_ready() == 0) {
        item(last, 0, 0, "async", "FAIL");
        note_fail();
        return;
    }
    n = pm_metal_async_n_runners();
    kind = pm_metal_async_runner_kind();
    fmt_count(detail, sizeof(detail), "ok  ", n, "runner");
    item(last, 0, 0, "async", detail);
    if (kind != NULL && kind[0] == 's' && kind[1] == 'i' && kind[2] == 'm') {
        snprintf(run, sizeof(run), "%s", kind);
    } else {
        snprintf(run, sizeof(run), "ok  %s", kind != NULL ? kind : "runner");
    }
    item(1, 1, !last, "runners", run);
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

/* Guest packs the loader actually instantiated. A resident module is C or Rust
 * linked into this image and never crossed a container boundary, so counting it
 * here would report the image back to itself. */
static void emit_wasm(int last) {
    static const char *const kind_name[3] = { "wasm", "aot", "elf" };
    uint32_t nmod = pm_wasmmod_registry_module_count();
    uint32_t nkind[3] = { 0, 0, 0 };
    uint32_t npack = 0;
    uint32_t i;
    uint32_t shown = 0;
    char detail[48];

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
        item(last, 0, 0, "wasm", "-  no pack loaded");
        return;
    }
    fmt_count(detail, sizeof(detail), "ok  ", npack, "pack");
    item(last, 0, 0, "wasm", detail);
    for (i = 0; i < 3u; i++) {
        char det[40];
        if (nkind[i] == 0u) {
            continue;
        }
        shown++;
        fmt_count(det, sizeof(det), "ok  ", nkind[i], "pack");
        item(shown == kinds_present(nkind), 1, !last, kind_name[i], det);
    }
}

static void emit_externals(int last) {
    uint32_t n = pm_metal_external_count();
    uint32_t i;
    char detail[40];
    if (n == 0u) {
        item(last, 0, 0, "externals", "FAIL");
        note_fail();
        return;
    }
    fmt_count(detail, sizeof(detail), "ok  ", n, "external");
    item(last, 0, 0, "externals", detail);
    for (i = 0; i < n; i++) {
        char ver[48];
        const char *v = pm_metal_external_version(i);
        const char *nm = pm_metal_external_name(i);
        snprintf(ver, sizeof(ver), "ok  %s", v != NULL ? v : "?");
        item(i + 1u == n, 1, !last, nm != NULL ? nm : "?", ver);
    }
}

int32_t pm_metal_boot_tree_print(void) {
    const char *seat = pm_metal_boot_fill_seat();
    char ready[32];
    s_nfail = 0;
    emit_line("");
    pm_metal_util_ascii_log_styled(PM_METAL_UTIL_ASCII_STYLE_ACCENT, "METAL");
    emit_line("");
    item(0, 0, 0, "pymergetic metal", PM_METAL_BOOT_TREE_VERSION);
    emit_line(PM_METAL_BOOT_SGR_DIM "|" PM_METAL_BOOT_SGR_RST);
    item(0, 0, 0, "arch", seat != NULL ? seat : "host");
    emit_mem(0);
    emit_cpu(0);
    emit_devices(0);
    emit_mods(0);
    emit_net(0);
    emit_async(0);
    emit_wasm(0);
    emit_externals(0);
    if (s_nfail == 0u) {
        item(1, 0, 0, "ready", "ok");
    } else {
        fmt_count(ready, sizeof(ready), "FAIL  ", s_nfail, "node");
        item(1, 0, 0, "ready", ready);
    }
    emit_line("");
    /* Printing succeeded either way; a failed node is reported on the `ready`
     * line, not by pretending the print itself broke. */
    return 0;
}

void pm_metal_boot_motd(void) {
    const char *seat = pm_metal_boot_fill_seat();
    char title[96];
    char cons[48];
    uint32_t nvp;
    uint32_t v;

    if (seat == NULL) {
        seat = "host";
    }
    emit_line("");
    pm_metal_util_ascii_log_rainbow("MetalPython");
    snprintf(title, sizeof(title), "%sMetalPython%s %s%s%s @ %s", PM_METAL_BOOT_SGR_SIM,
        PM_METAL_BOOT_SGR_RST, PM_METAL_BOOT_SGR_OK, PM_METAL_BOOT_TREE_VERSION,
        PM_METAL_BOOT_SGR_RST, seat);
    emit_line(title);

    nvp = 0u;
    if (pm_metal_console_ready()) {
        char vpc[24];
        nvp = pm_metal_console_viewport_count();
        fmt_count(vpc, sizeof(vpc), "", nvp, "viewport");
        snprintf(cons, sizeof(cons), "ok  #%u  %s", (unsigned)pm_metal_console_id(), vpc);
    } else {
        snprintf(cons, sizeof(cons), "FAIL");
    }
    item(0, 0, 0, "console", cons);
    for (v = 0; v < nvp; v++) {
        char vp[48];
        const char *kind = pm_metal_console_viewport_kind(v);
        snprintf(vp, sizeof(vp), "ok  %s", kind != NULL ? kind : "?");
        item(v + 1u == nvp, 1, 1, "viewport", vp);
    }

    emit_cdn_urls(0, 0, 0);
    item(1, 0, 0, "repl", "packages() | help()");
}

void pm_metal_boot_shutdown(int reboot) {
    char title[64];
    uint32_t nboot;
    emit_line("");
    snprintf(title, sizeof(title), "%smetal-boot: %s%s", PM_METAL_BOOT_SGR_WARN,
        reboot ? "reboot" : "shutdown", PM_METAL_BOOT_SGR_RST);
    emit_line(title);
    /* The boot graph knows the order it came up in and holds each card's
     * deinit, so unwind it instead of printing a subsystem list that no code
     * behind it ever touched. */
    nboot = pm_mod_boot_count();
    pm_mod_boot_unwind();
    if (nboot == 0u) {
        item(1, 0, 0, "stop", "-  nothing booted");
    } else {
        char detail[32];
        fmt_count(detail, sizeof(detail), "ok  ", nboot, "card");
        item(1, 0, 0, "stop", detail);
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

PM_MOD_BOOT_C(pymergetic.metal.boot.tree, pm_metal_boot_tree_init, pm_metal_boot_tree_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.boot.tree, pymergetic.metal.console);
PM_MOD_BOOTDEP_C(pymergetic.metal.boot.tree, pymergetic.metal.boot.externals);
