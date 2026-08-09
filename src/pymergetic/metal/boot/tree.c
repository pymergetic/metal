/*
 * Live boot-tree: each leave() flushes that section to the console as boot runs.
 */
#include "pymergetic/metal/boot/tree.h"

#include "pymergetic/metal/arch.h"
#include "pymergetic/metal/util/ascii.h"

#include <stdio.h>
#include <string.h>

#ifndef PM_METAL_VERSION
#define PM_METAL_VERSION "0.1.0"
#endif

enum { MAX_DEPTH = 8, MAX_SIBLINGS = 16, NAME_CAP = 48, DETAIL_CAP = 96 };

typedef struct {
    char name[NAME_CAP];
    char detail[DETAIL_CAP];
    pm_metal_boot_tree_status_t st;
} sib_t;

static pm_metal_boot_print_fn g_print;
static void *g_print_user;
static int g_depth;
static char g_sec_name[MAX_DEPTH][NAME_CAP];
static sib_t g_sibs[MAX_DEPTH][MAX_SIBLINGS];
static int g_nsib[MAX_DEPTH];
/* Which depths still have more siblings coming (for │ continuum). */
static int g_open_cont[MAX_DEPTH];

static void default_print(const char *line, void *user)
{
    (void)user;
    extern void uart_puts(const char *s) __attribute__((weak));
    if (uart_puts) {
        uart_puts(line);
        uart_puts("\n");
        return;
    }
#if defined(__EMSCRIPTEN__) || defined(__wasm__)
    /* Browser seat before HAL set_print: hosted stdio. */
    fputs(line, stdout);
    fputc('\n', stdout);
    fflush(stdout);
#else
    (void)line;
#endif
}

void pm_metal_boot_set_print(pm_metal_boot_print_fn fn, void *user)
{
    g_print = fn;
    g_print_user = user;
}

void pm_metal_boot_emit(const char *line)
{
    pm_metal_boot_print_fn fn = g_print ? g_print : default_print;
    fn(line, g_print_user);
}

void pm_metal_boot_tree_reset(void)
{
    g_depth = 0;
    memset(g_nsib, 0, sizeof(g_nsib));
    memset(g_open_cont, 0, sizeof(g_open_cont));
}

void pm_metal_boot_banner(const char *version, const char *cpu)
{
    char line[160];
    const char *ver = version ? version : PM_METAL_VERSION;
    const char *c = cpu && cpu[0] ? cpu : "cpu";
    /* Classic cyan FIGlet, then one-line seat stamp. */
    pm_metal_util_ascii_log_cyan("Metal");
    snprintf(line, sizeof(line), "\033[36m%s @ %s\033[0m", ver, c);
    pm_metal_boot_emit(line);
    pm_metal_boot_tree_reset();
}

static const char *status_word(pm_metal_boot_tree_status_t st)
{
    switch (st) {
    case PM_METAL_BOOT_TREE_OK:
        return "ok";
    case PM_METAL_BOOT_TREE_WARN:
        return "warn";
    case PM_METAL_BOOT_TREE_FAIL:
        return "fail";
    case PM_METAL_BOOT_TREE_SIM:
        return "sim";
    default:
        return "";
    }
}

static const char *status_ansi(pm_metal_boot_tree_status_t st)
{
    switch (st) {
    case PM_METAL_BOOT_TREE_OK:
        return "\033[32m";
    case PM_METAL_BOOT_TREE_WARN:
        return "\033[33m";
    case PM_METAL_BOOT_TREE_FAIL:
        return "\033[31m";
    case PM_METAL_BOOT_TREE_SIM:
        return "\033[36m";
    default:
        return "\033[2m";
    }
}

static void emit_line_at(int depth, int last, const char *name, pm_metal_boot_tree_status_t st,
    const char *detail)
{
    char prefix[64];
    char line[220];
    int p = 0;
    int d;
    const char *sw = status_word(st);
    const char *branch = last ? "`--" : "+--";

    prefix[0] = '\0';
    for (d = 0; d < depth; d++) {
        if (g_open_cont[d]) {
            p += snprintf(prefix + p, sizeof(prefix) - (size_t)p, "│   ");
        } else {
            p += snprintf(prefix + p, sizeof(prefix) - (size_t)p, "    ");
        }
    }
    if (sw[0] != '\0') {
        snprintf(
            line,
            sizeof(line),
            "%s%s %-12s %s%-4s\033[0m%s%s",
            prefix,
            branch,
            name,
            status_ansi(st),
            sw,
            detail && detail[0] ? " " : "",
            detail ? detail : "");
    } else if (detail && detail[0]) {
        /* DIM rows still carry detail (forge: base=… size=…). */
        snprintf(
            line,
            sizeof(line),
            "%s%s %-12s %s%s\033[0m",
            prefix,
            branch,
            name,
            status_ansi(st),
            detail);
    } else {
        snprintf(
            line,
            sizeof(line),
            "%s%s %-12s %s\033[0m",
            prefix,
            branch,
            name,
            status_ansi(st));
    }
    pm_metal_boot_emit(line);
}

static void flush_items_at(int d, int force_not_last)
{
    int i;
    int n = g_nsib[d];
    for (i = 0; i < n; i++) {
        sib_t *s = &g_sibs[d][i];
        int last = force_not_last ? 0 : (i == n - 1);
        emit_line_at(d, last, s->name, s->st, s->detail);
    }
    g_nsib[d] = 0;
}

void pm_metal_boot_tree_enter_ex(
    const char *name, pm_metal_boot_tree_status_t st, const char *detail)
{
    if (g_depth >= MAX_DEPTH || name == NULL) {
        return;
    }
    /* Nesting under an open section: flush prior siblings so order is kernel → area. */
    if (g_depth > 0 && g_nsib[g_depth] > 0) {
        flush_items_at(g_depth, 1);
    }
    /* Section line — not last until we know; use +-- for now. */
    g_open_cont[g_depth] = 1;
    emit_line_at(g_depth, 0, name, st, detail ? detail : "");
    strncpy(g_sec_name[g_depth], name, NAME_CAP - 1);
    g_sec_name[g_depth][NAME_CAP - 1] = '\0';
    g_nsib[g_depth] = 0;
    g_depth++;
    g_nsib[g_depth] = 0;
}

void pm_metal_boot_tree_enter(const char *name)
{
    pm_metal_boot_tree_enter_ex(name, PM_METAL_BOOT_TREE_DIM, "");
}

void pm_metal_boot_tree_item(
    const char *name, pm_metal_boot_tree_status_t st, const char *detail)
{
    sib_t *s;
    int d = g_depth;
    if (d <= 0 || d >= MAX_DEPTH || name == NULL) {
        return;
    }
    if (g_nsib[d] >= MAX_SIBLINGS) {
        return;
    }
    s = &g_sibs[d][g_nsib[d]++];
    memset(s, 0, sizeof(*s));
    strncpy(s->name, name, NAME_CAP - 1);
    if (detail) {
        strncpy(s->detail, detail, DETAIL_CAP - 1);
    }
    s->st = st;
}

void pm_metal_boot_tree_leave(void)
{
    int d;
    int i;
    int n;
    if (g_depth <= 0) {
        return;
    }
    d = g_depth;
    n = g_nsib[d];
    /* Flush this section's items live (correct last-child). */
    for (i = 0; i < n; i++) {
        sib_t *s = &g_sibs[d][i];
        emit_line_at(d, i == n - 1, s->name, s->st, s->detail);
    }
    g_nsib[d] = 0;
    g_depth--;
    /* Keep g_open_cont[parent] so later sibling sections still draw │. */
}

void pm_metal_boot_tree_ready_ok(void)
{
    /* Close any open sections. */
    while (g_depth > 0) {
        pm_metal_boot_tree_leave();
    }
    pm_metal_boot_emit("`-- ready        \033[32mok\033[0m");
}

void pm_metal_boot_rainbow_metalpython(const char *version, const char *cpu)
{
    char line[160];
    const char *ver = version && version[0] ? version : PM_METAL_VERSION;
    const char *c = cpu && cpu[0] ? cpu : "cpu";

    pm_metal_util_ascii_log_rainbow("MetalPython");
    /* Same shape as Metal cyan stamp under the first FIGlet. */
    snprintf(line, sizeof(line), "\033[35m%s @ %s\033[0m", ver, c);
    pm_metal_boot_emit(line);
}

int pm_metal_boot_tree_print(void)
{
    /* Compat: one-shot dump still used by stress — live-style fake boot. */
    pm_metal_arch_id_t arch = pm_metal_arch_current();
    const char *cpu = arch == PM_METAL_ARCH_ID_WASM ? "wasm32" : "x86_64";
    pm_metal_boot_banner(PM_METAL_VERSION, cpu);
    pm_metal_boot_tree_enter("arch");
    pm_metal_boot_tree_item("seat", PM_METAL_BOOT_TREE_OK, pm_metal_arch_name(arch));
    pm_metal_boot_tree_leave();
    pm_metal_boot_tree_ready_ok();
    return 0;
}
