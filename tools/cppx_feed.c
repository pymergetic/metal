/* cppx_feed.c — the C++ card's object/link prove driver (micro-cxx).
 *
 * Feeds a .cpp file through the jit.cpp card's lex+parse+lower and writes
 * the generated C. With --object it hands that C to the kernel's own C
 * compiler (jit.c TCC) and writes the object bytes — the card's object
 * prove. With --link it goes all the way: the object is linked in-process
 * by the build card's ELF relocator and a function is CALLED straight out
 * of the linked image — the C++ module runs, compiled end to end by the
 * kernel's own C++ -> C -> TCC -> link chain. No host cc/cc1plus anywhere.
 *
 * The link prove takes a symbol name and its expected value (--link
 * SYM EXPECTED); a fixture's use() returning the documented answer is the
 * pass bar, so the script that wraps this dies loudly on a wrong number.
 *
 * Not a prove seat: a debugging convenience owned by tools/, never
 * registered, never shipped. */
#include "pymergetic/metal/jit/cpp/__types__.h"
#include "pymergetic/metal/jit/c/__exports__.h"
#include "pymergetic/metal/build/__exports__.h"
#include "pymergetic/util/mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the same header roots the host build compiles against — the generated C
 * re-includes the source's own headers verbatim */
static const char *g_includes[] = {
    "src",
    "host_inc",
    "/home/ladmin/Devel/os-sdk/packages/metalpython",
    "/home/ladmin/Devel/os-sdk/packages/metalpython/extmod/wasmmod/src",
    "/home/ladmin/Devel/os-sdk/packages/metalpython/extmod/wasmmod",
    "/home/ladmin/Devel/os-sdk/packages/metalpython/ports/unix",
};

int main(int argc, char **argv) {
    int argi = 1;
    int do_object = 0;
    int do_link = 0;
    int do_selfhost = 0;
    const char *link_sym = NULL;
    long link_expect = 0;
    int has_expect = 0;
    const char *path;
    const char *out_path;
    FILE *f;
    static char src[1u << 21];
    size_t n;
    void *backing;
    pm_util_mem_arena_t *arena;
    pm_jit_cpp_toklist_t toks;
    pm_jit_cpp_ast_t *unit = NULL;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[256];
    int rc;
    FILE *o;

    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] == '-') {
        if (strcmp(argv[argi], "--object") == 0) { do_object = 1; argi++; }
        else if (strcmp(argv[argi], "--selfhost") == 0) { do_selfhost = 1; do_link = 1; do_object = 1; argi++; }
        else if (strncmp(argv[argi], "--link", 6) == 0) {
            do_link = 1;
            /* --link SYM EXPECTED or --link=SYM,EXPECTED */
            if (argv[argi][6] == '=') {
                char buf[128];
                char *comma;
                snprintf(buf, sizeof(buf), "%s", argv[argi] + 7);
                comma = strchr(buf, ',');
                if (comma != NULL) {
                    *comma = '\0';
                    link_expect = strtol(comma + 1, NULL, 0);
                    has_expect = 1;
                }
                link_sym = strdup(buf);
            } else {
                argi++;
                if (argi < argc) {
                    link_sym = strdup(argv[argi]);
                    if (argi + 1 < argc) {
                        link_expect = strtol(argv[argi + 1], NULL, 0);
                        has_expect = 1;
                        argi++;
                    }
                }
            }
            argi++;
        }
        else if (strcmp(argv[argi], "--") == 0)  { argi++; break; }
        else { fprintf(stderr, "unknown flag %s\n", argv[argi]); return 2; }
    }
    path = argi < argc ? argv[argi]
        : "src/pymergetic/metal/jit/cpp/fixtures/templates_virtual.cpp";
    out_path = argi + 1 < argc ? argv[argi + 1] : "/tmp/cppx_out.c";

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
    rc = pm_metal_jit_cpp_lex(arena, src, n, &toks, err, sizeof(err));
    if (rc != 0) { printf("LEX REFUSED: %s\n", err); return 1; }
    printf("lex ok: %u tokens\n", toks.n_toks);
    if (getenv("CPPX_TOKENS") != NULL) {
        uint32_t ti;
        for (ti = 0; ti < toks.n_toks; ti++) {
            printf("  tok[%u] kind=%d line=%u text='%.*s'\n", ti,
                (int)toks.toks[ti].kind, toks.toks[ti].line,
                (int)toks.toks[ti].text_len, toks.toks[ti].text);
        }
    }

    rc = pm_metal_jit_cpp_parse(arena, &toks, &unit, err, sizeof(err));
    if (rc != 0) { printf("PARSE REFUSED: %s\n", err); return 1; }
    printf("parse ok\n");

    rc = pm_metal_jit_cpp_lower(arena, unit, &c_out, &c_out_len, err, sizeof(err));
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
        const char *obj_path = argi + 2 < argc ? argv[argi + 2] : "/tmp/cppx_out.o";

        if (!obacking) { printf("no object backing\n"); return 2; }
        oarena = pm_util_mem_arena_create(obacking, 1u << 26);
        if (!oarena) { printf("no object arena\n"); return 2; }
        memset(oerr, 0, sizeof(oerr));
        rc = pm_metal_jit_c_object_compile_opts(oarena, c_out, c_out_len,
            g_includes, sizeof(g_includes) / sizeof(g_includes[0]),
            NULL, 0, &obj, &obj_len, oerr, sizeof(oerr));
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
         * resolver) -> the linked image's function is CALLED. The C++ module
         * runs, and its return value is checked against the documented
         * answer — templates instantiated, virtual dispatch through the
         * vtable, all of it executing out of the kernel-linked image. */
        void *obacking = malloc(1u << 26);
        pm_util_mem_arena_t *oarena;
        uint8_t *obj = NULL;
        size_t obj_len = 0;
        char oerr[256];
        pm_metal_build_unit_t bunit;
        pm_metal_build_artifact_t art;
        uint8_t *objs[1];
        size_t lens[1];
        int32_t (*fn)(void);
        int32_t got;

        if (!obacking) { printf("no link backing\n"); return 2; }
        oarena = pm_util_mem_arena_create(obacking, 1u << 26);
        if (!oarena) { printf("no link arena\n"); return 2; }
        memset(oerr, 0, sizeof(oerr));
        rc = pm_metal_jit_c_object_compile_opts(oarena, c_out, c_out_len,
            g_includes, sizeof(g_includes) / sizeof(g_includes[0]),
            NULL, 0, &obj, &obj_len, oerr, sizeof(oerr));
        if (rc != 0) { printf("LINK OBJECT REFUSED: %s\n", oerr); return 1; }
        printf("link: object ok: %zu bytes\n", obj_len);

        memset(&bunit, 0, sizeof(bunit));
        snprintf(bunit.fqn, sizeof(bunit.fqn), "%s", "selfhost.cppx.linked");
        objs[0] = obj;
        lens[0] = obj_len;
        memset(oerr, 0, sizeof(oerr));
        rc = pm_metal_build_link(oarena, &bunit, objs, lens, 1, &art,
                                 oerr, sizeof(oerr));
        if (rc != PM_METAL_BUILD_OK) {
            printf("LINK REFUSED: %s\n", oerr); return 1;
        }
        printf("link: image ok: %zu bytes\n", art.len);

        if (link_sym == NULL) link_sym = "use";
        if (!do_selfhost) {
            fn = (int32_t (*)(void))
                pm_metal_build_artifact_lookup(&art, link_sym);
            if (fn == NULL) {
                printf("LINK LOOKUP FAILED (%s not in image)\n", link_sym);
                return 1;
            }
            got = fn();
            printf("linked run ok: %s() = %d\n", link_sym, (int)got);
            if (has_expect && (long)got != link_expect) {
                printf("WRONG ANSWER: %s() = %d, expected %ld\n",
                    link_sym, (int)got, link_expect);
                return 1;
            }
        } else {
            /* the selfhost prove: the linked image IS the cppx card
             * (compiled from the C this feed just emitted), so its own
             * lex/parse/lower entries can transpile the same source again.
             * The out_path is overwritten with THAT output so the cycle
             * script byte-compares against the boot output — the compiler
             * that ran here was produced by the kernel's own
             * C++ -> C -> TCC -> link chain. No host cc. */
            int32_t (*l_lex)(pm_util_mem_arena_t *, const char *, size_t,
                pm_jit_cpp_toklist_t *, char *, size_t);
            int32_t (*l_parse)(pm_util_mem_arena_t *,
                const pm_jit_cpp_toklist_t *, pm_jit_cpp_ast_t **,
                char *, size_t);
            int32_t (*l_lower)(pm_util_mem_arena_t *,
                const pm_jit_cpp_ast_t *, char **, size_t *, char *, size_t);
            pm_jit_cpp_toklist_t l_toks;
            pm_jit_cpp_ast_t *l_unit = NULL;
            char *l_c = NULL;
            size_t l_c_len = 0;
            FILE *lo;

            l_lex = (int32_t (*)(pm_util_mem_arena_t *, const char *,
                size_t, pm_jit_cpp_toklist_t *, char *, size_t))
                pm_metal_build_artifact_lookup(&art, "pm_metal_jit_cpp_lex");
            l_parse = (int32_t (*)(pm_util_mem_arena_t *,
                const pm_jit_cpp_toklist_t *, pm_jit_cpp_ast_t **,
                char *, size_t))
                pm_metal_build_artifact_lookup(&art, "pm_metal_jit_cpp_parse");
            l_lower = (int32_t (*)(pm_util_mem_arena_t *,
                const pm_jit_cpp_ast_t *, char **, size_t *, char *, size_t))
                pm_metal_build_artifact_lookup(&art, "pm_metal_jit_cpp_lower");
            if (l_lex == NULL || l_parse == NULL || l_lower == NULL) {
                printf("LINK LOOKUP FAILED (lex=%p parse=%p lower=%p)\n",
                    (void *)l_lex, (void *)l_parse, (void *)l_lower);
                return 1;
            }
            memset(&l_toks, 0, sizeof(l_toks));
            memset(err, 0, sizeof(err));
            /* a fresh arena for the linked compiler's own lex/parse/lower —
             * the object-compile arena above already served TCC's pools and
             * the link image; the linked card starts from a clean slate. */
            {
                void *lbacking = malloc(1u << 26);
                pm_util_mem_arena_t *larena;
                if (!lbacking) { printf("no linked-run backing\n"); return 2; }
                larena = pm_util_mem_arena_create(lbacking, 1u << 26);
                if (!larena) { printf("no linked-run arena\n"); return 2; }
                rc = l_lex(larena, src, n, &l_toks, err, sizeof(err));
            }
            if (rc != 0) { printf("LINKED LEX REFUSED: %s\n", err); return 1; }
            if (rc != 0) { printf("LINKED LEX REFUSED: %s\n", err); return 1; }
            {
                void *lbacking2 = malloc(1u << 26);
                pm_util_mem_arena_t *larena2;
                if (!lbacking2) { printf("no parse arena\n"); return 2; }
                larena2 = pm_util_mem_arena_create(lbacking2, 1u << 26);
                if (!larena2) { printf("no parse arena obj\n"); return 2; }
                memset(err, 0, sizeof(err));
                rc = l_parse(larena2, &l_toks, &l_unit, err, sizeof(err));
            }
            if (rc != 0) { printf("LINKED PARSE REFUSED: %s\n", err); return 1; }
            {
                void *lbacking3 = malloc(1u << 26);
                pm_util_mem_arena_t *larena3;
                if (!lbacking3) { printf("no lower arena\n"); return 2; }
                larena3 = pm_util_mem_arena_create(lbacking3, 1u << 26);
                if (!larena3) { printf("no lower arena obj\n"); return 2; }
                memset(err, 0, sizeof(err));
                rc = l_lower(larena3, l_unit, &l_c, &l_c_len, err, sizeof(err));
            }
            if (rc != 0) { printf("LINKED LOWER REFUSED: %s\n", err); return 1; }
            printf("linked lower ok: %zu bytes of C\n", l_c_len);
            lo = fopen(out_path, "wb");
            if (lo) { fwrite(l_c, 1, l_c_len, lo); fclose(lo); printf("written %s\n", out_path); }
            else { perror(out_path); return 2; }
        }
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(oarena);
        free(obacking);
    }
    return 0;
}
