/* selfhost_feed.c — minimal driver for the micro-rustc self-host cycle.
 *
 * Feeds a .rs file through lex+parse+lower and writes the generated C.
 * With --object it then feeds that C to the kernel's own C compiler
 * (jit.c TCC card) and writes the object bytes.
 * With --link it goes one step further: the object is linked in-process
 * by the build card's ELF relocator (pm_metal_build_link) and the rsx
 * entries are resolved out of the linked image — the compiler that
 * produced gen-2 is itself a product of the kernel's Rust->C->object
 * ->link chain, and the file it writes proves the fixed point. No host
 * cc anywhere in that chain.
 *
 * The selfhost cycle (tools/selfhost_cycle.sh) links this against:
 *   1. the boot library (real rustc build of the rsx card) -> gen-1
 *   2. gen-1's own emitted C                    -> gen-2 (self)
 * then byte-compares the outputs. Fixed point = gen-2 == gen-1.
 *
 * Not a prove seat: a debugging convenience owned by tools/, never
 * registered, never shipped. */
#include "pymergetic/metal/jit/rs/compiler/__types__.h"
#include "pymergetic/metal/jit/c/__exports__.h"
#include "pymergetic/metal/build/__exports__.h"
#include "pymergetic/util/mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    int argi = 1;
    int do_object = 0;
    int do_link = 0;
    const char *path;
    const char *out_path;
    FILE *f;
    static char src[1u << 21];
    size_t n;
    void *backing;
    pm_util_mem_arena_t *arena;
    pm_jit_rsx_toklist_t toks;
    pm_jit_rsx_ast_t *unit = NULL;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    int rc;
    FILE *o;

    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] == '-') {
        if (strcmp(argv[argi], "--object") == 0) { do_object = 1; argi++; }
        else if (strcmp(argv[argi], "--link") == 0) { do_link = 1; argi++; }
        else if (strcmp(argv[argi], "--") == 0)  { argi++; break; }
        else { fprintf(stderr, "unknown flag %s\n", argv[argi]); return 2; }
    }
    path = argi < argc ? argv[argi]
        : "src/pymergetic/metal/jit/rs/compiler/__impl__.rs";
    out_path = argi + 1 < argc ? argv[argi + 1] : "/tmp/selfhost_out.c";

    f = fopen(path, "rb");
    if (!f) { perror(path); return 2; }
    n = fread(src, 1, sizeof(src) - 1, f);
    fclose(f);
    src[n] = 0;
    printf("source: %zu bytes\n", n);

    backing = malloc(1u << 26);
    if (!backing) { printf("no backing\n"); return 2; }
    arena = pm_util_mem_arena_create(backing, 1u << 26);
    if (!arena) { printf("no arena\n"); return 2; }

    memset(err, 0, sizeof(err));
    memset(&toks, 0, sizeof(toks));
    rc = pm_metal_jit_rsx_lex(arena, src, n, &toks, err, sizeof(err));
    if (rc != 0) { printf("LEX REFUSED: %s\n", err); return 1; }
    printf("lex ok: %u tokens\n", toks.n_toks);

    rc = pm_metal_jit_rsx_parse(arena, &toks, &unit, err, sizeof(err));
    if (rc != 0) { printf("PARSE REFUSED: %s\n", err); return 1; }
    printf("parse ok\n");

    rc = pm_metal_jit_rsx_lower(arena, unit, &c_out, &c_out_len, err, sizeof(err));
    if (rc != 0) { printf("LOWER REFUSED: %s\n", err); return 1; }
    printf("lower ok: %zu bytes of C\n", c_out_len);

    o = fopen(out_path, "wb");
    if (o) { fwrite(c_out, 1, c_out_len, o); fclose(o); printf("written %s\n", out_path); }
    else { perror(out_path); return 2; }

    if (do_object) {
        /* 64MB: the in-arena TCC compile (jit.c's arena reallocator) needs
         * the tccpp pools plus tables; small backings corrupt it. */
        void *obacking = malloc(1u << 26);
        pm_util_mem_arena_t *oarena;
        uint8_t *obj = NULL;
        size_t obj_len = 0;
        char oerr[256];
        FILE *oo;
        const char *obj_path = argi + 2 < argc ? argv[argi + 2] : "/tmp/selfhost_out.o";

        if (!obacking) { printf("no object backing\n"); return 2; }
        oarena = pm_util_mem_arena_create(obacking, 1u << 26);
        if (!oarena) { printf("no object arena\n"); return 2; }
        memset(oerr, 0, sizeof(oerr));
        rc = pm_metal_jit_c_object_compile(oarena, c_out, c_out_len,
                                           &obj, &obj_len, oerr, sizeof(oerr));
        if (rc != 0) { printf("OBJECT REFUSED: %s\n", oerr); return 1; }
        printf("object ok: %zu bytes\n", obj_len);
        oo = fopen(obj_path, "wb");
        if (oo) { fwrite(obj, 1, obj_len, oo); fclose(oo); printf("written %s\n", obj_path); }
        else { perror(obj_path); return 2; }
        pm_util_mem_arena_destroy(oarena);
        free(obacking);
    }
    if (do_link) {
        /* The full in-kernel chain, no host cc: the C this feed just emitted
         * -> jit.c TCC object -> build card link (ELF relocator, process
         * resolver) -> the linked image's own rsx entries compile the same
         * source again. The out_path file is written by the LINKED compiler,
         * so a byte-compare against the boot output (stage 4c) proves the
         * fixed point end to end. */
        void *obacking = malloc(1u << 26);
        pm_util_mem_arena_t *oarena;
        uint8_t *obj = NULL;
        size_t obj_len = 0;
        char oerr[256];
        pm_metal_build_unit_t bunit;
        pm_metal_build_artifact_t art;
        uint8_t *objs[1];
        size_t lens[1];
        int32_t (*l_lex)(pm_util_mem_arena_t *, const char *, size_t,
            pm_jit_rsx_toklist_t *, char *, size_t);
        int32_t (*l_parse)(pm_util_mem_arena_t *, const pm_jit_rsx_toklist_t *,
            pm_jit_rsx_ast_t **, char *, size_t);
        int32_t (*l_lower)(pm_util_mem_arena_t *, const pm_jit_rsx_ast_t *,
            char **, size_t *, char *, size_t);
        pm_jit_rsx_ast_t *l_unit = NULL;
        pm_jit_rsx_toklist_t l_toks;
        char *l_c = NULL;
        size_t l_c_len = 0;
        FILE *lo;

        if (!obacking) { printf("no link backing\n"); return 2; }
        oarena = pm_util_mem_arena_create(obacking, 1u << 26);
        if (!oarena) { printf("no link arena\n"); return 2; }
        memset(oerr, 0, sizeof(oerr));
        rc = pm_metal_jit_c_object_compile(oarena, c_out, c_out_len,
                                           &obj, &obj_len, oerr, sizeof(oerr));
        if (rc != 0) { printf("LINK OBJECT REFUSED: %s\n", oerr); return 1; }
        printf("link: object ok: %zu bytes\n", obj_len);

        memset(&bunit, 0, sizeof(bunit));
        snprintf(bunit.fqn, sizeof(bunit.fqn), "%s", "selfhost.rsx.linked");
        objs[0] = obj;
        lens[0] = obj_len;
        memset(oerr, 0, sizeof(oerr));
        rc = pm_metal_build_link(oarena, &bunit, objs, lens, 1, &art,
                                 oerr, sizeof(oerr));
        if (rc != PM_METAL_BUILD_OK) {
            printf("LINK REFUSED: %s\n", oerr); return 1;
        }
        printf("link: image ok: %zu bytes\n", art.len);

        l_lex = (int32_t (*)(pm_util_mem_arena_t *, const char *, size_t,
            pm_jit_rsx_toklist_t *, char *, size_t))
            pm_metal_build_artifact_lookup(&art, "pm_metal_jit_rsx_lex");
        l_parse = (int32_t (*)(pm_util_mem_arena_t *, const pm_jit_rsx_toklist_t *,
            pm_jit_rsx_ast_t **, char *, size_t))
            pm_metal_build_artifact_lookup(&art, "pm_metal_jit_rsx_parse");
        l_lower = (int32_t (*)(pm_util_mem_arena_t *, const pm_jit_rsx_ast_t *,
            char **, size_t *, char *, size_t))
            pm_metal_build_artifact_lookup(&art, "pm_metal_jit_rsx_lower");
        if (l_lex == NULL || l_parse == NULL || l_lower == NULL) {
            printf("LINK LOOKUP FAILED (lex=%p parse=%p lower=%p)\n",
                (void *)l_lex, (void *)l_parse, (void *)l_lower);
            return 1;
        }

        /* run the LINKED compiler on the same source */
        memset(&l_toks, 0, sizeof(l_toks));
        memset(err, 0, sizeof(err));
        rc = l_lex(oarena, src, n, &l_toks, err, sizeof(err));
        if (rc != 0) { printf("LINKED LEX REFUSED: %s\n", err); return 1; }
        memset(err, 0, sizeof(err));
        rc = l_parse(oarena, &l_toks, &l_unit, err, sizeof(err));
        if (rc != 0) { printf("LINKED PARSE REFUSED: %s\n", err); return 1; }
        memset(err, 0, sizeof(err));
        rc = l_lower(oarena, l_unit, &l_c, &l_c_len, err, sizeof(err));
        if (rc != 0) { printf("LINKED LOWER REFUSED: %s\n", err); return 1; }
        printf("linked lower ok: %zu bytes of C\n", l_c_len);

        /* the out_path is overwritten by the linked compiler's output so the
         * cycle script byte-compares against the boot output */
        lo = fopen(out_path, "wb");
        if (lo) { fwrite(l_c, 1, l_c_len, lo); fclose(lo); printf("written %s\n", out_path); }
        else { perror(out_path); return 2; }
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(oarena);
        free(obacking);
    }
    return 0;
}
