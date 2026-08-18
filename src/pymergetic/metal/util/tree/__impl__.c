/* pymergetic.metal.util.tree — shared colored box-drawing tree backend.
 * The boot banner (pymergetic.metal.boot.tree) and the CDN pack tree both fold
 * through here so every tree on the seat speaks the same ANSI grammar. */
#include "pymergetic/metal/util/tree/__exports__.h"
#include "pymergetic/metal/util/tree/__types__.h"

#include "pymergetic/metal/console.h"
#include "pymergetic/util/mem.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

void pm_metal_util_tree_line(const char *s) {
    emit_line(s);
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

void pm_metal_util_tree_paint_detail(char *dst, unsigned cap, const char *detail) {
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
            col = PM_METAL_UTIL_TREE_SGR_OK;
        } else if (token_is(tok, n, "sim")) {
            col = PM_METAL_UTIL_TREE_SGR_SIM;
        } else if (token_is(tok, n, "FAIL")) {
            col = PM_METAL_UTIL_TREE_SGR_FAIL;
        }
        if (col != NULL) {
            unsigned cl = 0;
            unsigned rl = 0;
            while (col[cl] != 0) {
                cl++;
            }
            while (PM_METAL_UTIL_TREE_SGR_RST[rl] != 0) {
                rl++;
            }
            if (oi + cl + n + rl >= cap) {
                break;
            }
            memcpy(dst + oi, col, cl);
            oi += cl;
            memcpy(dst + oi, tok, n);
            oi += n;
            memcpy(dst + oi, PM_METAL_UTIL_TREE_SGR_RST, rl);
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

void pm_metal_util_tree_item(int last, int depth, int parent_cont, const char *name,
    const char *detail) {
    char line[PM_METAL_UTIL_TREE_LINE];
    char painted[160];
    unsigned nlen = 0;
    const char *br = last ? "`--" : "+--";
    if (name == NULL) {
        name = "?";
    }
    while (name[nlen] != 0) {
        nlen++;
    }
    pm_metal_util_tree_paint_detail(painted, sizeof(painted), detail);

    if (depth <= 0) {
        /* Root line: `<dim>+-- <name>[ pad][ <detail>]`. */
        if (detail != NULL && detail[0] != 0 && nlen < PM_METAL_UTIL_TREE_NAME_PAD) {
            char padded[PM_METAL_UTIL_TREE_NAME_PAD + 1];
            unsigned p;
            for (p = 0; p < PM_METAL_UTIL_TREE_NAME_PAD; p++) {
                padded[p] = (p < nlen) ? name[p] : ' ';
            }
            padded[PM_METAL_UTIL_TREE_NAME_PAD] = 0;
            snprintf(line, sizeof(line), "%s%s %s%s%s", PM_METAL_UTIL_TREE_SGR_DIM, br,
                padded, PM_METAL_UTIL_TREE_SGR_RST, painted);
        } else if (detail != NULL && detail[0] != 0) {
            snprintf(line, sizeof(line), "%s%s %s %s%s", PM_METAL_UTIL_TREE_SGR_DIM, br,
                name, PM_METAL_UTIL_TREE_SGR_RST, painted);
        } else {
            snprintf(line, sizeof(line), "%s%s %s%s", PM_METAL_UTIL_TREE_SGR_DIM, br,
                name, PM_METAL_UTIL_TREE_SGR_RST);
        }
        pm_metal_util_tree_line(line);
        return;
    }

    /* Depth >= 1: `depth` stem segments then the branch. The deepest stem is
     * parent_cont ? "|   " : "    "; every shallower ancestor is assumed to
     * continue (it has deeper siblings along this path). depth 1 and 2 emit
     * exactly what the boot tree rendered before (dim pad br name rst painted),
     * so its lines are unchanged; deeper depths now draw correct bars too. */
    {
        char stems[PM_METAL_UTIL_TREE_LINE];
        unsigned n = 0;
        unsigned i;
        for (i = 0; i < (unsigned)depth; i++) {
            const char *seg = (i + 1u == (unsigned)depth && !parent_cont) ? "    " : "|   ";
            unsigned k;
            for (k = 0; seg[k] != 0 && n + 3u < (unsigned)sizeof(stems); k++) {
                stems[n++] = seg[k];
            }
        }
        stems[n] = 0;
        pm_metal_util_tree_item_at(last, stems, name, detail);
    }
}

void pm_metal_util_tree_item_at(int last, const char *stems, const char *name,
    const char *detail) {
    /* Exact-stem variant: `stems` is the full leading bar run (already typed
     * by the caller, e.g. built node-by-node so non-continuing ancestors show
     * "    "). Renders the same dim stem/branch/name + painted detail as
     * pm_metal_util_tree_item but with caller-owned stems, so deeper trees
     * get precisely correct bars. */
    char line[PM_METAL_UTIL_TREE_LINE];
    char painted[160];
    unsigned nlen = 0;
    const char *br = last ? "`--" : "+--";
    if (name == NULL) {
        name = "?";
    }
    while (name[nlen] != 0) {
        nlen++;
    }
    pm_metal_util_tree_paint_detail(painted, sizeof(painted), detail);
    {
        /* Stems + branch + name, plain. Bounded well under `line`, so the
         * dim/detail snprintf below cannot trip -Werror=format-truncation. */
        char body[PM_METAL_UTIL_TREE_LINE - 200u];
        unsigned n = 0;
        unsigned cap = (unsigned)sizeof(body);
        const char *s = (stems == NULL) ? "" : stems;
        unsigned k;
        for (k = 0; s[k] != 0 && n + 3u < cap; k++) {
            body[n++] = s[k];
        }
        for (k = 0; br[k] != 0 && n + 3u < cap; k++) {
            body[n++] = br[k];
        }
        body[n++] = ' ';
        for (k = 0; k < nlen && n + 3u < cap; k++) {
            body[n++] = name[k];
        }
        body[n] = 0;
        if (detail != NULL && detail[0] != 0) {
            snprintf(line, sizeof(line), "%s%s  %s%s",
                PM_METAL_UTIL_TREE_SGR_DIM, body, PM_METAL_UTIL_TREE_SGR_RST, painted);
        } else {
            snprintf(line, sizeof(line), "%s%s%s", PM_METAL_UTIL_TREE_SGR_DIM, body,
                PM_METAL_UTIL_TREE_SGR_RST);
        }
        pm_metal_util_tree_line(line);
    }
}

static int32_t pm_metal_util_tree_init(pm_util_mem_arena_t *arena) {
    (void)arena;
    return 0;
}

static void pm_metal_util_tree_deinit(void) {}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.util.tree, pm_metal_util_tree_line, pm_metal_util_tree_line,
    void(const char *));
PM_MOD_EXPORT_C(pymergetic.metal.util.tree, pm_metal_util_tree_item, pm_metal_util_tree_item,
    void(int, int, int, const char *, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.util.tree, pm_metal_util_tree_item_at, pm_metal_util_tree_item_at,
    void(int, const char *, const char *, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.util.tree, pm_metal_util_tree_paint_detail,
    pm_metal_util_tree_paint_detail, void(char *, unsigned, const char *));

PM_MOD_BOOT_C(pymergetic.metal.util.tree, pm_metal_util_tree_init, pm_metal_util_tree_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.util.tree, pymergetic.metal.console);
