/* ksweep.c — in-kernel compile sweep over every card in the embedded table.
 *
 * The one-card self-host proofs (selfhost_cycle.sh stage 4c, the build
 * card's test_rebuild_jit_c / test_rebuild_tcc) show the chain works. This
 * driver answers the next question: how much of the whole tree does it
 * already carry? Every impl="c" unit from pm_metal_build_discover goes
 * through pm_metal_build_unit_compile (TCC objects -> ELF relocator link)
 * with the host seat's include/define fill, and the run prints one line
 * per card: OK with object sizes, or the refusal reason.
 *
 * Output is a readiness map, not a prove: a card that fails here is data
 * (what the seat fill still misses), never a regression. Not registered,
 * not shipped — tools/ owns it, same posture as selfhost_feed.c. */
#include "pymergetic/metal/async/__types__.h"
#include "pymergetic/metal/build/__types__.h"
#include "pymergetic/metal/jit/c/__types__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(PM_METAL_BUILD_HAS_ELF) && PM_HAS_TCC && !defined(TCC_TARGET_WASM32)

/* The libgcc long-double helpers (__floatundixf and friends) are defined
 * once per binary by the build card's tests (same TCC lowering note);
 * FEED_CARD_OBJS includes __tests__.o, so ksweep must not redefine them. */

typedef struct sweep_row {
    char fqn[128];
    char impl[8];
    int32_t rc;          /* 0 = linked, else the build status */
    char err[PM_METAL_BUILD_ERR_MAX];
    uint32_t n_sources;
    uint32_t n_syms;
    size_t image_len;
    double ms;
} sweep_row_t;

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
    enum { SPAN = 96u * 1024u * 1024u };
    void *backing;
    pm_util_mem_arena_t *arena;
    pm_metal_build_unit_t *units = NULL;
    uint32_t n_units = 0;
    sweep_row_t *rows;
    char err[PM_METAL_BUILD_ERR_MAX];
    int32_t rc;
    uint32_t i;
    const char *report_path = argc > 1 ? argv[1] : "/tmp/ksweep_report.txt";
    char dir[512];
    char src_root[2560], wasmmod_src_root[2560], wasmmod_root[2560],
        top_root[2560], tcc_root[2560], host_inc[2560], mpy_port[2560],
        mbed_inc[2560], zenoh_inc[2560], zenoh_src[2560];
    const char *includes[12];
    uint32_t n_includes = 0;
    const char *defines[12];
    uint32_t n_defines = 0;
    static char libdir_def[2600];
    static char triplet_def[128];
    static char triplet_val[160];
    FILE *rep;
    uint32_t n_ok = 0, n_refused = 0, n_skip = 0;
    uint32_t n_syms_total = 0;
    double t_total;

    /* seat roots, resolved from this file (tools/ksweep.c -> metal/):
     * dir = <metal>/tools; metal = dir/..; metalpython = dir/../../.. */
    snprintf(dir, sizeof(dir), "%s", __FILE__);
    {
        char *slash = strrchr(dir, '/');
        if (!slash) { fprintf(stderr, "ksweep: bad __FILE__\n"); return 2; }
        *slash = '\0';
    }
    /* dir is <= 511 bytes; every root below is dir/top_root + a short
     * suffix, and the buffers are dir-sized + 256 so truncation is
     * impossible (checked once here, snprintf results are provably short). */
    if (strlen(dir) + 256 >= sizeof(src_root)) {
        fprintf(stderr, "ksweep: tree path too deep\n");
        return 2;
    }
    snprintf(src_root, sizeof(src_root), "%s/../src", dir);
    snprintf(host_inc, sizeof(host_inc), "%s/../host_inc", dir);
    snprintf(tcc_root, sizeof(tcc_root), "%s/../externals/tcc", dir);
    snprintf(wasmmod_root, sizeof(wasmmod_root), "%s/../../wasmmod", dir);
    snprintf(wasmmod_src_root, sizeof(wasmmod_src_root), "%s/../../wasmmod/src", dir);
    snprintf(top_root, sizeof(top_root), "%s/../../..", dir);
    snprintf(mpy_port, sizeof(mpy_port), "%s/../../../ports/unix", dir);
    snprintf(mbed_inc, sizeof(mbed_inc), "%s/../../../lib/mbedtls/include", dir);
    snprintf(zenoh_inc, sizeof(zenoh_inc), "%s/../externals/zenoh-pico/include", dir);
    snprintf(zenoh_src, sizeof(zenoh_src), "%s/../externals/zenoh-pico/src", dir);

    includes[n_includes++] = host_inc;
    includes[n_includes++] = src_root;
    includes[n_includes++] = wasmmod_src_root;
    includes[n_includes++] = wasmmod_root;
    includes[n_includes++] = top_root;
    includes[n_includes++] = mpy_port;
    includes[n_includes++] = mbed_inc;
    includes[n_includes++] = zenoh_inc;
    includes[n_includes++] = zenoh_src;
    includes[n_includes++] = tcc_root;

    defines[n_defines++] = "_POSIX_C_SOURCE=200809L";
    defines[n_defines++] = "PM_WASMMOD_GUEST=0";
    defines[n_defines++] = "PM_MOD_TESTS=1";
    defines[n_defines++] = "ZENOH_GENERIC";
    defines[n_defines++] = "MICROPY_SSL_MBEDTLS=1";
    defines[n_defines++] = "MBEDTLS_CONFIG_FILE=\"mbedtls/mbedtls_config_port.h\"";
    defines[n_defines++] = "TCC_TARGET_X86_64";
    defines[n_defines++] = "PM_HAS_TCC=1";
    snprintf(libdir_def, sizeof(libdir_def), "PM_METAL_TCC_LIB_DIR=\"%s\"", tcc_root);
    defines[n_defines++] = libdir_def;
    defines[n_defines++] = "PM_METAL_TCC_CROSS_WASM32=1";
    defines[n_defines++] = "PM_METAL_BUILD_HAS_ELF=1";
    {
        FILE *trip = popen("cc -print-multiarch 2>/dev/null", "r");
        if (trip != NULL) {
            if (fgets(triplet_def, sizeof(triplet_def), trip) != NULL) {
                char *nl = strchr(triplet_def, '\n');
                if (nl) *nl = '\0';
                if (triplet_def[0] != '\0') {
                    snprintf(triplet_val, sizeof(triplet_val),
                        "CONFIG_TRIPLET=\"%s\"", triplet_def);
                    defines[n_defines++] = triplet_val;
                }
            }
            pclose(trip);
        }
    }

    backing = malloc(SPAN);
    if (!backing) { fprintf(stderr, "ksweep: no backing\n"); return 2; }
    arena = pm_util_mem_arena_create(backing, SPAN);
    if (!arena) { fprintf(stderr, "ksweep: no arena\n"); return 2; }

    rc = pm_metal_build_discover(arena, &units, &n_units, err, sizeof(err));
    if (rc != PM_METAL_BUILD_OK) {
        fprintf(stderr, "ksweep: discover refused: %s\n", err);
        return 2;
    }
    printf("ksweep: %u card unit(s) discovered\n", n_units);
    rows = (sweep_row_t *)calloc(n_units, sizeof(sweep_row_t));
    if (!rows) { fprintf(stderr, "ksweep: no rows\n"); return 2; }

    t_total = now_ms();
    for (i = 0; i < n_units; i++) {
        const pm_metal_build_unit_t *u = &units[i];
        pm_metal_build_artifact_t art;
        char unit_root[2560];
        double t0;
        const pm_metal_build_record_t *rec;

        snprintf(rows[i].fqn, sizeof(rows[i].fqn), "%s", u->fqn);
        snprintf(rows[i].impl, sizeof(rows[i].impl), "%s", u->impl);
        rows[i].n_sources = u->n_sources;

        if (strcmp(u->impl, "c") != 0) {
            rows[i].rc = -1000;  /* not buildable yet: rs/cpp/py */
            n_skip++;
            printf("skip  %-44s impl=%s (%u src)\n", u->fqn, u->impl, u->n_sources);
            continue;
        }
        if (u->n_sources > PM_METAL_BUILD_MAX_OBJS) {
            rows[i].rc = -1001;  /* manifest wider than the link face */
            n_refused++;
            printf("wide  %-44s %u sources > MAX_OBJS %u\n",
                u->fqn, u->n_sources, (uint32_t)PM_METAL_BUILD_MAX_OBJS);
            continue;
        }

        /* unit_root: <metal>/src + the full dotted fqn (the same convention
         * as the build card's own rebuild test — pymergetic.metal.jit.c
         * lives at src/pymergetic/metal/jit/c). */
        {
            size_t w = strlen(src_root);
            size_t tl = strlen(u->fqn);
            if (w + tl + 2 > sizeof(unit_root)) {
                rows[i].rc = -1002;
                n_refused++;
                printf("deep  %-44s path overflows unit_root\n", u->fqn);
                continue;
            }
            memcpy(unit_root, src_root, w);
            unit_root[w++] = '/';
            memcpy(unit_root + w, u->fqn, tl);
            unit_root[w + tl] = '\0';
            /* dots -> slashes: pymergetic.metal.jit.c -> pymergetic/metal/jit/c */
            {
                size_t k;
                for (k = w; k < w + tl; k++) {
                    if (unit_root[k] == '.') unit_root[k] = '/';
                }
            }
        }

        memset(&art, 0, sizeof(art));
        memset(err, 0, sizeof(err));
        t0 = now_ms();
        rc = pm_metal_build_unit_compile(arena, u, unit_root,
            includes, n_includes, defines, n_defines, &art, err, sizeof(err));
        rows[i].ms = now_ms() - t0;
        rows[i].rc = rc;
        snprintf(rows[i].err, sizeof(rows[i].err), "%s", err);

        if (rc == PM_METAL_BUILD_OK) {
            rows[i].image_len = art.len;
            rec = pm_metal_build_record_find(u->fqn);
            if (rec != NULL) {
                rows[i].n_syms = rec->n_syms;
                n_syms_total += rec->n_syms;
            }
            n_ok++;
            printf("ok    %-44s %u src, %zu image, %u syms, %.0fms\n",
                u->fqn, u->n_sources, art.len, rows[i].n_syms, rows[i].ms);
            pm_metal_build_artifact_destroy(&art);
        } else {
            n_refused++;
            printf("FAIL  %-44s rc=%d %s\n", u->fqn, rc, err);
        }
        fflush(stdout);
    }
    t_total = now_ms() - t_total;

    rep = fopen(report_path, "w");
    if (rep == NULL) { perror(report_path); rep = stdout; }
    fprintf(rep, "ksweep — in-kernel compile sweep (TCC + ELF relocator)\n");
    fprintf(rep, "cards: %u   ok: %u   refused: %u   not-buildable: %u\n",
        n_units, n_ok, n_refused, n_skip);
    fprintf(rep, "exported syms across linked images: %u\n", n_syms_total);
    fprintf(rep, "wall: %.1fs\n\n", t_total / 1000.0);
    for (i = 0; i < n_units; i++) {
        if (rows[i].rc == 0) {
            fprintf(rep, "ok    %-44s %3u src  %7zu B  %3u syms  %6.0fms\n",
                rows[i].fqn, rows[i].n_sources, rows[i].image_len,
                rows[i].n_syms, rows[i].ms);
        } else if (rows[i].rc == -1000) {
            fprintf(rep, "skip  %-44s impl=%s\n", rows[i].fqn, rows[i].impl);
        } else if (rows[i].rc == -1001) {
            fprintf(rep, "wide  %-44s %u sources > MAX_OBJS\n",
                rows[i].fqn, rows[i].n_sources);
        } else {
            fprintf(rep, "FAIL  %-44s rc=%d %.120s\n",
                rows[i].fqn, rows[i].rc, rows[i].err);
        }
    }
    if (rep != stdout) { fclose(rep); printf("report: %s\n", report_path); }

    free(rows);
    pm_util_mem_arena_destroy(arena);
    free(backing);
    printf("ksweep done: %u/%u c-cards linked in-kernel\n", n_ok,
        n_ok + n_refused);
    return 0;
#else
    (void)argc; (void)argv;
    printf("ksweep: this seat has no in-kernel ELF link (no TCC / wasm32)\n");
    return 0;
#endif
}
