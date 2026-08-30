#include "pymergetic/metal/jit/cpp/__types__.h"
#include "pymergetic/metal/jit/c/__exports__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/guest.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* fixtures are read from the card dir relative to __FILE__, never the cwd */
#define CPPX_FIXTURE_REL "fixtures/"

static char *cppx_read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    rewind(f);
    if (n <= 0) { fclose(f); return NULL; }
    buf = (char *)malloc((size_t)n + 1);
    if (buf == NULL) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f); free(buf); return NULL;
    }
    fclose(f);
    buf[n] = '\0';
    *out_len = (size_t)n;
    return buf;
}

static int cppx_fixture_path(char *out, size_t cap, const char *name) {
    char dir[512];
    size_t n;
    snprintf(dir, sizeof(dir), "%s", __FILE__);
    {
        char *slash = strrchr(dir, '/');
        if (slash == NULL) return -1;
        *slash = '\0';
    }
    n = (size_t)snprintf(out, cap, "%s/" CPPX_FIXTURE_REL "%s", dir, name);
    return n < cap ? 0 : -1;
}

/* lexer: every token kind the fixtures use must round-trip with kind and text */
static int32_t test_lex_minimal(void) {
    void *backing = malloc(1u << 20);
    pm_util_mem_arena_t *arena;
    char path[600];
    char *src;
    size_t src_len = 0;
    pm_jit_cpp_toklist_t toks;
    char err[256];
    uint32_t i;
    uint32_t n_kw = 0;
    uint32_t n_int = 0;
    int saw_for = 0;
    int saw_lessthan = 0;

    if (backing == NULL) return 1;
    arena = pm_util_mem_arena_create(backing, 1u << 20);
    if (arena == NULL) { free(backing); return 2; }
    if (cppx_fixture_path(path, sizeof(path), "minimal.cpp") != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 3;
    }
    src = cppx_read_file(path, &src_len);
    if (src == NULL) { pm_util_mem_arena_destroy(arena); free(backing); return 4; }

    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err)); /* lex signals failures via errbuf[0] */
    if (pm_metal_jit_cpp_lex(arena, src, src_len, &toks, err, sizeof(err)) != 0) {
        printf("cppx lex err: %s\n", err);
        pm_util_mem_arena_destroy(arena); free(backing); return 5;
    }
    if (toks.n_toks < 60) { pm_util_mem_arena_destroy(arena); free(backing); return 6; }
    for (i = 0; i < toks.n_toks; i++) {
        const pm_jit_cpp_token_t *t = &toks.toks[i];
        if (t->kind == PM_JIT_CPP_TOK_KEYWORD) {
            n_kw++;
            if (t->text_len == 3 && memcmp(t->text, "for", 3) == 0) saw_for = 1;
        }
        if (t->kind == PM_JIT_CPP_TOK_INT_LITERAL) n_int++;
        if (t->kind == PM_JIT_CPP_TOK_PUNCT && t->text_len == 1
            && t->text[0] == '<') saw_lessthan = 1;
        if (t->line == 0) {
            pm_util_mem_arena_destroy(arena); free(backing); return 7;
        }
    }
    if (!saw_for || !saw_lessthan || n_int < 5) {
        pm_util_mem_arena_destroy(arena); free(backing); return 8;
    }
    /* trailing END token */
    if (toks.toks[toks.n_toks - 1].kind != PM_JIT_CPP_TOK_END) {
        pm_util_mem_arena_destroy(arena); free(backing); return 9;
    }
    free(src);
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* lexer: template fixture exercises :: < > <>:: and keywords */
static int32_t test_lex_templates(void) {
    void *backing = malloc(1u << 20);
    pm_util_mem_arena_t *arena;
    char path[600];
    char *src;
    size_t src_len = 0;
    pm_jit_cpp_toklist_t toks;
    char err[256];
    uint32_t i;
    int saw_double_colon = 0;
    int saw_template_kw = 0;
    int saw_virtual_kw = 0;

    if (backing == NULL) return 10;
    arena = pm_util_mem_arena_create(backing, 1u << 20);
    if (arena == NULL) { free(backing); return 11; }
    if (cppx_fixture_path(path, sizeof(path), "templates_virtual.cpp") != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 12;
    }
    src = cppx_read_file(path, &src_len);
    if (src == NULL) { pm_util_mem_arena_destroy(arena); free(backing); return 13; }

    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err)); /* lex signals failures via errbuf[0] */
    if (pm_metal_jit_cpp_lex(arena, src, src_len, &toks, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 14;
    }
    for (i = 0; i < toks.n_toks; i++) {
        const pm_jit_cpp_token_t *t = &toks.toks[i];
        if (t->kind == PM_JIT_CPP_TOK_DOUBLE_COLON) saw_double_colon = 1;
        if (t->kind == PM_JIT_CPP_TOK_KEYWORD) {
            if (t->text_len == 8 && memcmp(t->text, "template", 8) == 0) saw_template_kw = 1;
            if (t->text_len == 7 && memcmp(t->text, "virtual", 7) == 0) saw_virtual_kw = 1;
        }
    }
    if (!saw_double_colon || !saw_template_kw || !saw_virtual_kw) {
        pm_util_mem_arena_destroy(arena); free(backing); return 15;
    }
    free(src);
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* lexer: unterminated input errors with a line number, not a crash */
static int32_t test_lex_errors(void) {
    void *backing = malloc(1u << 20);
    pm_util_mem_arena_t *arena;
    pm_jit_cpp_toklist_t toks;
    char err[256];

    if (backing == NULL) return 20;
    arena = pm_util_mem_arena_create(backing, 1u << 20);
    if (arena == NULL) { free(backing); return 21; }

    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_cpp_lex(arena, "int x = \"abc", 12, &toks, err, sizeof(err)) == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 22;
    }
    if (strstr(err, "at line") == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 23;
    }
    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_cpp_lex(arena, "int x = 1 @ 2;", 14, &toks, err, sizeof(err)) == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 24;
    }
    if (strncmp(err, "cppx: unsupported", 17) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 25;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* parser: minimal fixture parses to a translation unit with 2 functions */
static int32_t test_parse_minimal(void) {
    void *backing = malloc(2u << 20);
    pm_util_mem_arena_t *arena;
    char path[600];
    char *src;
    size_t src_len = 0;
    pm_jit_cpp_toklist_t toks;
    pm_jit_cpp_ast_t *unit = NULL;
    char err[256];
    char dump[8192];
    uint32_t i;
    uint32_t n_functions = 0;

    if (backing == NULL) return 30;
    arena = pm_util_mem_arena_create(backing, 2u << 20);
    if (arena == NULL) { free(backing); return 31; }
    if (cppx_fixture_path(path, sizeof(path), "minimal.cpp") != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 32;
    }
    src = cppx_read_file(path, &src_len);
    if (src == NULL) { pm_util_mem_arena_destroy(arena); free(backing); return 33; }

    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_cpp_lex(arena, src, src_len, &toks, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 34;
    }
    if (pm_metal_jit_cpp_parse(arena, &toks, &unit, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 35;
    }
    if (unit == NULL || unit->kind != PM_JIT_CPP_AST_TRANSLATION_UNIT) {
        pm_util_mem_arena_destroy(arena); free(backing); return 36;
    }
    for (i = 0; i < unit->n_kids; i++) {
        if (unit->kids[i]->kind == PM_JIT_CPP_AST_FUNCTION) n_functions++;
    }
    if (n_functions != 2) {
        pm_util_mem_arena_destroy(arena); free(backing); return 37;
    }
    /* ast_dump renders every node; check it names both functions */
    if (pm_metal_jit_cpp_ast_dump(unit, dump, sizeof(dump), err, sizeof(err)) < 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 38;
    }
    if (strstr(dump, "function add") == NULL
        || strstr(dump, "function main") == NULL
        || strstr(dump, "for") == NULL
        || strstr(dump, "while") == NULL
        || strstr(dump, "if") == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 39;
    }
    free(src);
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* parser: templates+virtual fixture — class, template decl, pure virtual */
static int32_t test_parse_templates(void) {
    void *backing = malloc(4u << 20);
    pm_util_mem_arena_t *arena;
    char path[600];
    char *src;
    size_t src_len = 0;
    pm_jit_cpp_toklist_t toks;
    pm_jit_cpp_ast_t *unit = NULL;
    char err[256];
    char dump[16384];
    uint32_t i;
    uint32_t n_class = 0;
    uint32_t n_template = 0;

    if (backing == NULL) return 40;
    arena = pm_util_mem_arena_create(backing, 4u << 20);
    if (arena == NULL) { free(backing); return 41; }
    if (cppx_fixture_path(path, sizeof(path), "templates_virtual.cpp") != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 42;
    }
    src = cppx_read_file(path, &src_len);
    if (src == NULL) { pm_util_mem_arena_destroy(arena); free(backing); return 43; }

    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_cpp_lex(arena, src, src_len, &toks, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 44;
    }
    if (pm_metal_jit_cpp_parse(arena, &toks, &unit, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 45;
    }
    for (i = 0; i < unit->n_kids; i++) {
        if (unit->kids[i]->kind == PM_JIT_CPP_AST_CLASS) n_class++;
        if (unit->kids[i]->kind == PM_JIT_CPP_AST_TEMPLATE_DECL) {
            n_template++;
            /* a template class wraps the CLASS node in the TEMPLATE_DECL —
             * the entity is the LAST kid (param NAMEs lead) */
            if (unit->kids[i]->n_kids > 0
                && unit->kids[i]->kids[unit->kids[i]->n_kids - 1]->kind
                    == PM_JIT_CPP_AST_CLASS) {
                n_class++;
            }
        }
    }
    if (n_class != 2) {
        pm_util_mem_arena_destroy(arena); free(backing); return 46;
    }
    if (n_template != 2) {
        pm_util_mem_arena_destroy(arena); free(backing); return 47;
    }
    if (pm_metal_jit_cpp_ast_dump(unit, dump, sizeof(dump), err, sizeof(err)) < 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 48;
    }
    if (strstr(dump, "class Box") == NULL
        || strstr(dump, "class LoudBox") == NULL
        || strstr(dump, "template-decl") == NULL
        || strstr(dump, "pure-virtual") == NULL
        || strstr(dump, "access-spec public") == NULL
        || strstr(dump, "access-spec private") == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 49;
    }
    free(src);
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* parser: broken fixture must fail with "cppx: ... at line N" */
static int32_t test_parse_broken(void) {
    void *backing = malloc(1u << 20);
    pm_util_mem_arena_t *arena;
    char path[600];
    char *src;
    size_t src_len = 0;
    pm_jit_cpp_toklist_t toks;
    pm_jit_cpp_ast_t *unit = NULL;
    char err[256];

    if (backing == NULL) return 50;
    arena = pm_util_mem_arena_create(backing, 1u << 20);
    if (arena == NULL) { free(backing); return 51; }
    if (cppx_fixture_path(path, sizeof(path), "broken.cpp") != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 52;
    }
    src = cppx_read_file(path, &src_len);
    if (src == NULL) { pm_util_mem_arena_destroy(arena); free(backing); return 53; }

    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_cpp_lex(arena, src, src_len, &toks, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 54;
    }
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_cpp_parse(arena, &toks, &unit, err, sizeof(err)) == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 55;
    }
    if (unit != NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 56;
    }
    if (strncmp(err, "cppx: ", 6) != 0 || strstr(err, "at line") == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 57;
    }
    /* the error must point at line 4 (the missing ';' is noticed there) */
    if (strstr(err, "line 4") == NULL && strstr(err, "line 5") == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 58;
    }
    free(src);
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* parse+dump on a small inline source: expressions with precedence */
static int32_t test_parse_exprs(void) {
    void *backing = malloc(1u << 20);
    pm_util_mem_arena_t *arena;
    pm_jit_cpp_toklist_t toks;
    pm_jit_cpp_ast_t *unit = NULL;
    char err[256];
    char dump[4096];
    static const char *src =
        "int f(int a, int b) {\n"
        "    int c = a + b * 2;\n"
        "    bool t = a < b && b > 3 || c == 4;\n"
        "    int *p = &c;\n"
        "    int d = p[0] + *p;\n"
        "    return t ? c : d;\n"
        "}\n";

    if (backing == NULL) return 60;
    arena = pm_util_mem_arena_create(backing, 1u << 20);
    if (arena == NULL) { free(backing); return 61; }
    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_cpp_lex(arena, src, strlen(src), &toks, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 62;
    }
    if (pm_metal_jit_cpp_parse(arena, &toks, &unit, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 63;
    }
    if (pm_metal_jit_cpp_ast_dump(unit, dump, sizeof(dump), err, sizeof(err)) < 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 64;
    }
    if (strstr(dump, "binary *") == NULL
        || strstr(dump, "binary +") == NULL
        || strstr(dump, "binary &&") == NULL
        || strstr(dump, "binary ||") == NULL
        || strstr(dump, "unary &") == NULL
        || strstr(dump, "unary *") == NULL
        || strstr(dump, "binary []") == NULL
        || strstr(dump, "decl-stmt p") == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 65;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* unsupported constructs error with the exact prefix, never silently pass */
static int32_t test_unsupported(void) {
    void *backing = malloc(1u << 20);
    pm_util_mem_arena_t *arena;
    pm_jit_cpp_toklist_t toks;
    pm_jit_cpp_ast_t *unit = NULL;
    char err[256];
    static const char *ns_src = "namespace a { int x; }\n";
    static const char *pp_src = "int x;\n#define Y 1\n";
    static const char *raw_src = "const char *s = R\"(hi)\";\n";

    if (backing == NULL) return 70;
    arena = pm_util_mem_arena_create(backing, 1u << 20);
    if (arena == NULL) { free(backing); return 71; }

    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_cpp_lex(arena, ns_src, strlen(ns_src), &toks, err, sizeof(err)) == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 72;
    }
    if (strncmp(err, "cppx: unsupported", 17) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 73;
    }

    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    /* preprocessor directives now lex (PP_DIRECTIVE tokens) — the card
     * passes them through for TCC's cpp. Roundtrip: lex must succeed and
     * produce a PP_DIRECTIVE token carrying the verbatim text. */
    if (pm_metal_jit_cpp_lex(arena, pp_src, strlen(pp_src), &toks, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 74;
    }
    {
        uint32_t k;
        int saw_pp = 0;
        for (k = 0; k < toks.n_toks; k++) {
            if (toks.toks[k].kind == PM_JIT_CPP_TOK_PP_DIRECTIVE
                && toks.toks[k].text_len == 11
                && memcmp(toks.toks[k].text, "#define Y 1", 11) == 0) {
                saw_pp = 1;
            }
        }
        if (!saw_pp) {
            pm_util_mem_arena_destroy(arena); free(backing); return 75;
        }
    }

    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_cpp_lex(arena, raw_src, strlen(raw_src), &toks, err, sizeof(err)) == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 76;
    }
    if (strncmp(err, "cppx: unsupported", 17) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 77;
    }

    /* switch statements now parse — verify the SWITCH/CASE/DEFAULT nodes */
    {
        static const char *sw_src = "int f(int a) {\n    switch (a) { default: break; }\n    return 0;\n}\n";
        memset(&toks, 0, sizeof(toks));
        memset(err, 0, sizeof(err));
        unit = NULL;
        if (pm_metal_jit_cpp_lex(arena, sw_src, strlen(sw_src), &toks, err, sizeof(err)) != 0) {
            pm_util_mem_arena_destroy(arena); free(backing); return 78;
        }
        if (pm_metal_jit_cpp_parse(arena, &toks, &unit, err, sizeof(err)) != 0) {
            pm_util_mem_arena_destroy(arena); free(backing); return 79;
        }
        {
            /* function body -> compound -> switch stmt */
            const pm_jit_cpp_ast_t *fn = unit->n_kids > 0 ? unit->kids[0] : NULL;
            const pm_jit_cpp_ast_t *body = fn && fn->n_kids > 0
                ? fn->kids[fn->n_kids - 1] : NULL;
            const pm_jit_cpp_ast_t *sw = body && body->n_kids > 0
                ? body->kids[0] : NULL;
            if (sw == NULL || sw->kind != PM_JIT_CPP_AST_SWITCH) {
                pm_util_mem_arena_destroy(arena); free(backing); return 80;
            }
        }
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* lower minimal.cpp: full function subset through the C backend — the C text
 * must carry both signatures, the forward decls, and the control flow. */
static int32_t test_lower_minimal(void) {
    void *backing = malloc(4u << 20);
    pm_util_mem_arena_t *arena;
    char path[600];
    char *src;
    size_t src_len = 0;
    pm_jit_cpp_toklist_t toks;
    pm_jit_cpp_ast_t *unit = NULL;
    char err[256];
    char *c_out = NULL;
    size_t c_out_len = 0;

    if (backing == NULL) return 100;
    arena = pm_util_mem_arena_create(backing, 4u << 20);
    if (arena == NULL) { free(backing); return 101; }
    if (cppx_fixture_path(path, sizeof(path), "minimal.cpp") != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 102;
    }
    src = cppx_read_file(path, &src_len);
    if (src == NULL) { pm_util_mem_arena_destroy(arena); free(backing); return 103; }

    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_cpp_lex(arena, src, src_len, &toks, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 104;
    }
    if (pm_metal_jit_cpp_parse(arena, &toks, &unit, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 105;
    }
    if (pm_metal_jit_cpp_lower(arena, unit, &c_out, &c_out_len,
            err, sizeof(err)) != 0) {
        printf("cppx lower err: %s\n", err);
        pm_util_mem_arena_destroy(arena); free(backing); return 106;
    }
    if (c_out == NULL || c_out_len < 200) {
        pm_util_mem_arena_destroy(arena); free(backing); return 107;
    }
    /* both forward decls, both definitions, the for/if/while bodies */
    if (strstr(c_out, "int add(int a, int b);") == NULL
        || strstr(c_out, "int main();") == NULL
        || strstr(c_out, "int add(int a, int b) {") == NULL
        || strstr(c_out, "int main() {") == NULL
        || strstr(c_out, "return a + b;") == NULL
        || strstr(c_out, "total = total + i;") == NULL
        || strstr(c_out, "total += 10;") == NULL
        /* the inc is the third for-clause, so no semicolon follows */
        || strstr(c_out, "i++)") == NULL
        || strstr(c_out, "return add(total, 1);") == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 108;
    }
    free(src);
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* lower refusals: every C++-only construct fails with the unsupported
 * prefix, never silently miscompiles. */
static int32_t test_lower_refuses(void) {
    static const char *cases[] = {
        "auto f() {\n    return 1;\n}\n",                       /* auto return */
        "int g() {\n    auto x = 1;\n    return x;\n}\n",       /* auto local */
        "int g() {\n    int *p = nullptr;\n    return 0;\n}\n", /* nullptr */
        NULL
    };
    void *backing = malloc(2u << 20);
    pm_util_mem_arena_t *arena;
    uint32_t i;

    if (backing == NULL) return 120;
    arena = pm_util_mem_arena_create(backing, 2u << 20);
    if (arena == NULL) { free(backing); return 121; }
    for (i = 0; cases[i] != NULL; i++) {
        pm_jit_cpp_toklist_t toks;
        pm_jit_cpp_ast_t *unit = NULL;
        char err[256];
        char *c_out = NULL;
        size_t c_out_len = 0;
        memset(&toks, 0, sizeof(toks));
        memset(err, 0, sizeof(err));
        if (pm_metal_jit_cpp_lex(arena, cases[i], strlen(cases[i]),
                &toks, err, sizeof(err)) != 0) {
            pm_util_mem_arena_destroy(arena); free(backing); return 122;
        }
        if (pm_metal_jit_cpp_parse(arena, &toks, &unit, err, sizeof(err)) != 0) {
            /* parser-level refusal (class member shapes) is fine too */
            if (strncmp(err, "cppx: unsupported", 17) != 0) {
                pm_util_mem_arena_destroy(arena); free(backing); return 123;
            }
            continue;
        }
        if (pm_metal_jit_cpp_lower(arena, unit, &c_out, &c_out_len,
                err, sizeof(err)) == 0) {
            printf("cppx lower accepted case %u\n", (unsigned)i);
            pm_util_mem_arena_destroy(arena); free(backing); return 124;
        }
        if (strncmp(err, "cppx: unsupported", 17) != 0) {
            printf("cppx lower wrong refusal %u: %s\n", (unsigned)i, err);
            pm_util_mem_arena_destroy(arena); free(backing); return 125;
        }
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* the object prove: minimal.cpp -> lower -> TCC object bytes, in-kernel.
 * Seats without native object output refuse politely and skip. */
static int32_t test_lower_object(void) {
    void *backing = malloc(4u << 20);
    pm_util_mem_arena_t *arena;
    char path[600];
    char *src;
    size_t src_len = 0;
    pm_jit_cpp_toklist_t toks;
    pm_jit_cpp_ast_t *unit = NULL;
    char err[256];
    char oerr[256];
    char *c_out = NULL;
    size_t c_out_len = 0;
    uint8_t *obj = NULL;
    size_t obj_len = 0;
    void *obacking;
    pm_util_mem_arena_t *oarena;
    int32_t rc;

    if (backing == NULL) return 140;
    arena = pm_util_mem_arena_create(backing, 4u << 20);
    if (arena == NULL) { free(backing); return 141; }
    if (cppx_fixture_path(path, sizeof(path), "minimal.cpp") != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 142;
    }
    src = cppx_read_file(path, &src_len);
    if (src == NULL) { pm_util_mem_arena_destroy(arena); free(backing); return 143; }
    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_cpp_lex(arena, src, src_len, &toks, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 144;
    }
    if (pm_metal_jit_cpp_parse(arena, &toks, &unit, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 145;
    }
    if (pm_metal_jit_cpp_lower(arena, unit, &c_out, &c_out_len,
            err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 146;
    }

    obacking = malloc(1u << 26);
    if (obacking == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 147;
    }
    oarena = pm_util_mem_arena_create(obacking, 1u << 26);
    if (oarena == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); free(obacking);
        return 148;
    }
    memset(oerr, 0, sizeof(oerr));
    rc = pm_metal_jit_c_object_compile(oarena, c_out, c_out_len,
        &obj, &obj_len, oerr, sizeof(oerr));
    if (rc != 0) {
        if (strstr(oerr, "no native object output on this seat") != NULL) {
            /* browser/wasm32, firmware: face wired, fill says no */
            pm_util_mem_arena_destroy(arena); free(backing);
            pm_util_mem_arena_destroy(oarena); free(obacking);
            return 0;
        }
        printf("cppx object err: %s\n", oerr);
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking);
        return 149;
    }
    if (obj == NULL || obj_len < 4) {
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking);
        return 150;
    }
    if (!(obj[0] == 0x7f && obj[1] == 'E' && obj[2] == 'L' && obj[3] == 'F')) {
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking);
        return 151;
    }
    free(src);
    pm_util_mem_arena_destroy(arena);
    pm_util_mem_arena_destroy(oarena);
    free(backing);
    free(obacking);
    return 0;
}

/* lower templates_virtual.cpp: classes, inheritance, template
 * instantiation, vtables, new/delete, ctor init lists — then compile the
 * generated C to an object (host seats) proving the C is well-formed. */
static int32_t test_lower_classes(void) {
    void *backing = malloc(4u << 20);
    pm_util_mem_arena_t *arena;
    char path[600];
    char *src;
    size_t src_len = 0;
    pm_jit_cpp_toklist_t toks;
    pm_jit_cpp_ast_t *unit = NULL;
    char err[256];
    char oerr[256];
    char *c_out = NULL;
    size_t c_out_len = 0;
    uint8_t *obj = NULL;
    size_t obj_len = 0;
    void *obacking;
    pm_util_mem_arena_t *oarena;
    int32_t rc;

    if (backing == NULL) return 160;
    arena = pm_util_mem_arena_create(backing, 4u << 20);
    if (arena == NULL) { free(backing); return 161; }
    if (cppx_fixture_path(path, sizeof(path), "templates_virtual.cpp") != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 162;
    }
    src = cppx_read_file(path, &src_len);
    if (src == NULL) { pm_util_mem_arena_destroy(arena); free(backing); return 163; }
    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_cpp_lex(arena, src, src_len, &toks, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 164;
    }
    if (pm_metal_jit_cpp_parse(arena, &toks, &unit, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 165;
    }
    if (pm_metal_jit_cpp_lower(arena, unit, &c_out, &c_out_len,
            err, sizeof(err)) != 0) {
        printf("cppx classes err: %s\n", err);
        pm_util_mem_arena_destroy(arena); free(backing); return 166;
    }
    /* the C face of the lowering: template instantiation, vtable, methods,
     * ctor, new/delete helpers, virtual dispatch. Box_int_dtor is the dtor
     * the source declares (~Box); LoudBox declares none, so none is emitted. */
    if (strstr(c_out, "struct Box_int") == NULL
        || strstr(c_out, "Box_int_get") == NULL
        || strstr(c_out, "LoudBox_describe") == NULL
        || strstr(c_out, "_vtable") == NULL
        || strstr(c_out, "LoudBox_ctor") == NULL
        || strstr(c_out, "Box_int_dtor") == NULL
        || strstr(c_out, "LoudBox_new") == NULL
        || strstr(c_out, "identity_int") == NULL) {
        printf("cppx classes C missing expected symbols\n");
        pm_util_mem_arena_destroy(arena); free(backing); return 167;
    }

    obacking = malloc(1u << 26);
    if (obacking == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 168;
    }
    oarena = pm_util_mem_arena_create(obacking, 1u << 26);
    if (oarena == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); free(obacking);
        return 169;
    }
    memset(oerr, 0, sizeof(oerr));
    rc = pm_metal_jit_c_object_compile(oarena, c_out, c_out_len,
        &obj, &obj_len, oerr, sizeof(oerr));
    if (rc != 0) {
        if (strstr(oerr, "no native object output on this seat") != NULL) {
            pm_util_mem_arena_destroy(arena); free(backing);
            pm_util_mem_arena_destroy(oarena); free(obacking);
            return 0;
        }
        printf("cppx classes object err: %s\n", oerr);
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking);
        return 170;
    }
    if (obj == NULL || obj_len < 4
        || !(obj[0] == 0x7f && obj[1] == 'E' && obj[2] == 'L' && obj[3] == 'F')) {
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking);
        return 171;
    }
    free(src);
    pm_util_mem_arena_destroy(arena);
    pm_util_mem_arena_destroy(oarena);
    free(backing);
    free(obacking);
    return 0;
}

static int32_t pm_metal_jit_cpp_tests(void) {
    int32_t rc;
    rc = test_lex_minimal();
    if (rc) { printf("cppx fail lex_minimal %d\n", (int)rc); return rc; }
    rc = test_lex_templates();
    if (rc) { printf("cppx fail lex_templates %d\n", (int)rc); return rc; }
    rc = test_lex_errors();
    if (rc) { printf("cppx fail lex_errors %d\n", (int)rc); return rc; }
    rc = test_parse_minimal();
    if (rc) { printf("cppx fail parse_minimal %d\n", (int)rc); return rc; }
    rc = test_parse_templates();
    if (rc) { printf("cppx fail parse_templates %d\n", (int)rc); return rc; }
    rc = test_parse_broken();
    if (rc) { printf("cppx fail parse_broken %d\n", (int)rc); return rc; }
    rc = test_parse_exprs();
    if (rc) { printf("cppx fail parse_exprs %d\n", (int)rc); return rc; }
    rc = test_unsupported();
    if (rc) { printf("cppx fail unsupported %d\n", (int)rc); return rc; }
    rc = test_lower_minimal();
    if (rc) { printf("cppx fail lower_minimal %d\n", (int)rc); return rc; }
    rc = test_lower_refuses();
    if (rc) { printf("cppx fail lower_refuses %d\n", (int)rc); return rc; }
    rc = test_lower_object();
    if (rc) { printf("cppx fail lower_object %d\n", (int)rc); return rc; }
    rc = test_lower_classes();
    if (rc) { printf("cppx fail lower_classes %d\n", (int)rc); return rc; }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.jit.cpp, tests, pm_metal_jit_cpp_tests);
