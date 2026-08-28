#include "pymergetic/metal/jit/cpp/__types__.h"
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
    if (pm_metal_jit_cpp_lex(arena, src, src_len, &toks, err, sizeof(err)) != 0) {
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
            /* a template class wraps the CLASS node in the TEMPLATE_DECL */
            if (unit->kids[i]->n_kids > 0
                && unit->kids[i]->kids[0]->kind == PM_JIT_CPP_AST_CLASS) {
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
    if (pm_metal_jit_cpp_lex(arena, pp_src, strlen(pp_src), &toks, err, sizeof(err)) == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 74;
    }
    if (strncmp(err, "cppx: unsupported", 17) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 75;
    }

    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_cpp_lex(arena, raw_src, strlen(raw_src), &toks, err, sizeof(err)) == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 76;
    }
    if (strncmp(err, "cppx: unsupported", 17) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 77;
    }

    /* switch statements parse-error with the unsupported prefix */
    {
        static const char *sw_src = "int f(int a) {\n    switch (a) { default: break; }\n    return 0;\n}\n";
        memset(&toks, 0, sizeof(toks));
        memset(err, 0, sizeof(err));
        unit = NULL;
        if (pm_metal_jit_cpp_lex(arena, sw_src, strlen(sw_src), &toks, err, sizeof(err)) != 0) {
            pm_util_mem_arena_destroy(arena); free(backing); return 78;
        }
        if (pm_metal_jit_cpp_parse(arena, &toks, &unit, err, sizeof(err)) == 0) {
            pm_util_mem_arena_destroy(arena); free(backing); return 79;
        }
        if (strncmp(err, "cppx: unsupported", 17) != 0) {
            pm_util_mem_arena_destroy(arena); free(backing); return 80;
        }
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

static int32_t pm_metal_jit_cpp_tests(void) {
    int32_t rc;
    rc = test_lex_minimal();
    if (rc) return rc;
    rc = test_lex_templates();
    if (rc) return rc;
    rc = test_lex_errors();
    if (rc) return rc;
    rc = test_parse_minimal();
    if (rc) return rc;
    rc = test_parse_templates();
    if (rc) return rc;
    rc = test_parse_broken();
    if (rc) return rc;
    rc = test_parse_exprs();
    if (rc) return rc;
    rc = test_unsupported();
    if (rc) return rc;
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.jit.cpp, tests, pm_metal_jit_cpp_tests);
