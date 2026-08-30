/* pymergetic.metal.jit.rs.compiler — prove: lex+parse+lower the kernel Rust
 * subset and check the generated C carries the source's shape. */
#include "pymergetic/metal/jit/rs/compiler/__types__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/metal/inspect/__exports__.h"
#include "pymergetic/metal/jit/c/__exports__.h"
#include "pymergetic/metal/build/__exports__.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- helpers ----------------------------------------------------------- */

static int rsx_strstr(const char *hay, const char *needle) {
    return strstr(hay, needle) != NULL;
}

/* --- lex --------------------------------------------------------------- */

static int32_t test_lex_minimal(void) {
    void *backing = malloc(1u << 20);
    pm_util_mem_arena_t *arena;
    pm_jit_rsx_toklist_t toks;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "extern \"C\" {\n"
        "    fn pm_test_hello() -> i32;\n"
        "}\n";

    if (backing == NULL) return 1;
    arena = pm_util_mem_arena_create(backing, 1u << 20);
    if (arena == NULL) { free(backing); return 2; }

    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_lex(arena, src, strlen(src), &toks, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 3;
    }
    /* extern "C" { fn pm_test_hello ( ) - > i32 ; }
     * keywords lex as IDENT; count sanity: at least 10 tokens. */
    if (toks.n_toks < 10) {
        pm_util_mem_arena_destroy(arena); free(backing); return 4;
    }
    /* the fn name must be present as some token's text */
    {
        uint32_t i;
        int saw_name = 0;
        for (i = 0; i < toks.n_toks; i++) {
            if (toks.toks[i].text_len == 13
                && memcmp(toks.toks[i].text, "pm_test_hello", 13) == 0) {
                saw_name = 1;
                break;
            }
        }
        if (!saw_name) {
            pm_util_mem_arena_destroy(arena); free(backing); return 5;
        }
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* lexer: macro invocations capture whole text; macro_rules! is refused at
 * parse, not lex (the lexer emits MACRO_INVOC for any name!(...) shape). */
static int32_t test_lex_macro(void) {
    void *backing = malloc(1u << 20);
    pm_util_mem_arena_t *arena;
    pm_jit_rsx_toklist_t toks;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "pymergetic_wasmmod::PM_MOD_EXPORT_RS!(\n"
        "    \"pymergetic.metal.jit.rs.compiler\",\n"
        "    pm_metal_jit_rsx_lex,\n"
        "    \"int32_t(void*, const char*, size_t, ...*)\"\n"
        ");\n";

    if (backing == NULL) return 10;
    arena = pm_util_mem_arena_create(backing, 1u << 20);
    if (arena == NULL) { free(backing); return 11; }

    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_lex(arena, src, strlen(src), &toks, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 12;
    }
    {
        uint32_t i;
        int saw_macro_invoc = 0;
        for (i = 0; i < toks.n_toks; i++) {
            if (toks.toks[i].kind == PM_JIT_RSX_TOK_MACRO_INVOC) saw_macro_invoc = 1;
        }
        if (!saw_macro_invoc) {
            pm_util_mem_arena_destroy(arena); free(backing); return 13;
        }
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* lexer: unterminated string errors with a line number, not a crash */
static int32_t test_lex_errors(void) {
    void *backing = malloc(1u << 20);
    pm_util_mem_arena_t *arena;
    pm_jit_rsx_toklist_t toks;
    char err[PM_METAL_JIT_RSX_ERR_MAX];

    if (backing == NULL) return 20;
    arena = pm_util_mem_arena_create(backing, 1u << 20);
    if (arena == NULL) { free(backing); return 21; }

    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_lex(arena, "let s = \"abc", 12, &toks, err, sizeof(err)) == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 22;
    }
    if (strstr(err, "at line") == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 23;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* --- parse ------------------------------------------------------------- */

static int32_t test_parse_minimal(void) {
    void *backing = malloc(1u << 20);
    pm_util_mem_arena_t *arena;
    pm_jit_rsx_toklist_t toks;
    pm_jit_rsx_ast_t *unit = NULL;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "extern \"C\" {\n"
        "    fn pm_test_hello() -> i32;\n"
        "}\n";

    if (backing == NULL) return 30;
    arena = pm_util_mem_arena_create(backing, 1u << 20);
    if (arena == NULL) { free(backing); return 31; }

    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_lex(arena, src, strlen(src), &toks, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 32;
    }
    if (pm_metal_jit_rsx_parse(arena, &toks, &unit, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 33;
    }
    if (unit == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 34;
    }
    /* unit must be a FILE with at least one kid (the extern block) */
    if (unit->kind != PM_JIT_RSX_AST_FILE || unit->n_kids < 1) {
        pm_util_mem_arena_destroy(arena); free(backing); return 35;
    }
    if (unit->kids[0]->kind != PM_JIT_RSX_AST_EXTERN_BLOCK) {
        pm_util_mem_arena_destroy(arena); free(backing); return 36;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* parse: #[repr(C)] pub struct { fields } */
static int32_t test_parse_struct(void) {
    void *backing = malloc(1u << 20);
    pm_util_mem_arena_t *arena;
    pm_jit_rsx_toklist_t toks;
    pm_jit_rsx_ast_t *unit = NULL;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "#[repr(C)]\n"
        "pub struct Foo {\n"
        "    x: i32,\n"
        "    y: i64,\n"
        "}\n";

    if (backing == NULL) return 40;
    arena = pm_util_mem_arena_create(backing, 1u << 20);
    if (arena == NULL) { free(backing); return 41; }

    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_lex(arena, src, strlen(src), &toks, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 42;
    }
    if (pm_metal_jit_rsx_parse(arena, &toks, &unit, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 43;
    }
    if (unit == NULL || unit->kind != PM_JIT_RSX_AST_FILE) {
        pm_util_mem_arena_destroy(arena); free(backing); return 44;
    }
    /* attrs ride on the item: find the STRUCT node among kids */
    {
        uint32_t i;
        int saw_struct = 0;
        for (i = 0; i < unit->n_kids; i++) {
            if (unit->kids[i]->kind == PM_JIT_RSX_AST_STRUCT) saw_struct = 1;
        }
        if (!saw_struct) {
            pm_util_mem_arena_destroy(arena); free(backing); return 45;
        }
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* parse: const fn with body, let + typed pattern, return expr */
static int32_t test_parse_fn(void) {
    void *backing = malloc(1u << 20);
    pm_util_mem_arena_t *arena;
    pm_jit_rsx_toklist_t toks;
    pm_jit_rsx_ast_t *unit = NULL;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "pub fn pm_add(a: i32, b: i32) -> i32 {\n"
        "    let sum: i32 = a + b;\n"
        "    sum\n"
        "}\n";

    if (backing == NULL) return 50;
    arena = pm_util_mem_arena_create(backing, 1u << 20);
    if (arena == NULL) { free(backing); return 51; }

    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_lex(arena, src, strlen(src), &toks, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 52;
    }
    if (pm_metal_jit_rsx_parse(arena, &toks, &unit, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 53;
    }
    if (unit == NULL || unit->kind != PM_JIT_RSX_AST_FILE) {
        pm_util_mem_arena_destroy(arena); free(backing); return 54;
    }
    {
        uint32_t i;
        int saw_fn = 0;
        for (i = 0; i < unit->n_kids; i++) {
            if (unit->kids[i]->kind == PM_JIT_RSX_AST_FN) saw_fn = 1;
        }
        if (!saw_fn) {
            pm_util_mem_arena_destroy(arena); free(backing); return 55;
        }
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* parse: out-of-subset constructs are refused with the rsx: prefix */
static int32_t test_parse_unsupported(void) {
    void *backing = malloc(1u << 20);
    pm_util_mem_arena_t *arena;
    pm_jit_rsx_toklist_t toks;
    pm_jit_rsx_ast_t *unit = NULL;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char mod_src[] =
        "mod inner {\n"
        "}\n";

    if (backing == NULL) return 60;
    arena = pm_util_mem_arena_create(backing, 1u << 20);
    if (arena == NULL) { free(backing); return 61; }

    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_lex(arena, mod_src, strlen(mod_src), &toks, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 62;
    }
    unit = NULL;
    if (pm_metal_jit_rsx_parse(arena, &toks, &unit, err, sizeof(err)) == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 63;
    }
    if (strncmp(err, "rsx: unsupported", 16) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 64;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* --- lower / compile --------------------------------------------------- */

static int32_t test_compile_minimal_fn(void) {
    void *backing = malloc(1u << 20);
    pm_util_mem_arena_t *arena;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "extern \"C\" {\n"
        "    fn pm_test_hello() -> i32;\n"
        "}\n";

    if (backing == NULL) return 70;
    arena = pm_util_mem_arena_create(backing, 1u << 20);
    if (arena == NULL) { free(backing); return 71; }

    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, src, strlen(src),
                                 &c_out, &c_out_len, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 72;
    }
    if (c_out == NULL || c_out_len == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 73;
    }
    if (!rsx_strstr(c_out, "pm_test_hello")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 74;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

static int32_t test_compile_struct(void) {
    void *backing = malloc(1u << 20);
    pm_util_mem_arena_t *arena;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "#[repr(C)]\n"
        "pub struct Foo {\n"
        "    x: i32,\n"
        "    y: i64,\n"
        "}\n";

    if (backing == NULL) return 80;
    arena = pm_util_mem_arena_create(backing, 1u << 20);
    if (arena == NULL) { free(backing); return 81; }

    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, src, strlen(src),
                                 &c_out, &c_out_len, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 82;
    }
    if (c_out == NULL || c_out_len == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 83;
    }
    if (!rsx_strstr(c_out, "typedef struct")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 84;
    }
    if (!rsx_strstr(c_out, "Foo")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 85;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* compile: a fn with a body lowers to a C fn with the same name */
static int32_t test_compile_fn_body(void) {
    void *backing = malloc(1u << 20);
    pm_util_mem_arena_t *arena;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "pub fn pm_add(a: i32, b: i32) -> i32 {\n"
        "    let sum: i32 = a + b;\n"
        "    sum\n"
        "}\n";

    if (backing == NULL) return 90;
    arena = pm_util_mem_arena_create(backing, 1u << 20);
    if (arena == NULL) { free(backing); return 91; }

    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, src, strlen(src),
                                 &c_out, &c_out_len, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 92;
    }
    if (c_out == NULL || c_out_len == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 93;
    }
    if (!rsx_strstr(c_out, "pm_add")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 94;
    }
    /* let lowers to a C local decl: look for the declared name */
    if (!rsx_strstr(c_out, "sum")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 95;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* compile: #line provenance — generated C carries source line mapping */
static int32_t test_compile_provenance(void) {
    void *backing = malloc(1u << 20);
    pm_util_mem_arena_t *arena;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "extern \"C\" {\n"
        "    fn pm_test_hello() -> i32;\n"
        "}\n";

    if (backing == NULL) return 100;
    arena = pm_util_mem_arena_create(backing, 1u << 20);
    if (arena == NULL) { free(backing); return 101; }

    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, src, strlen(src),
                                 &c_out, &c_out_len, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 102;
    }
    if (c_out == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 103;
    }
    /* #line directives exist for authored-source mapping */
    if (!rsx_strstr(c_out, "#line")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 104;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* --- ast_dump ---------------------------------------------------------- */

static int32_t test_ast_dump(void) {
    void *backing = malloc(1u << 20);
    pm_util_mem_arena_t *arena;
    pm_jit_rsx_toklist_t toks;
    pm_jit_rsx_ast_t *unit = NULL;
    char out[4096];
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "extern \"C\" {\n"
        "    fn pm_test_hello() -> i32;\n"
        "}\n";

    if (backing == NULL) return 110;
    arena = pm_util_mem_arena_create(backing, 1u << 20);
    if (arena == NULL) { free(backing); return 111; }

    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_lex(arena, src, strlen(src), &toks, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 112;
    }
    if (pm_metal_jit_rsx_parse(arena, &toks, &unit, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 113;
    }
    memset(out, 0, sizeof(out));
    if (pm_metal_jit_rsx_ast_dump(unit, out, sizeof(out), err, sizeof(err)) < 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 114;
    }
    /* dump shows kind names, not empty output */
    if (!rsx_strstr(out, "FILE") && !rsx_strstr(out, "file")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 115;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* --- self-host ---------------------------------------------------------- */

/* Self-host prove: the compiler compiles its own authored source (embedded
 * by the inspect card's source tree) and the run is deterministic — two
 * independent arenas produce byte-identical C, and that C carries the
 * pipeline's own shape (the lexer entry, the lowerer, a #line mapping).
 * This is the fixed-point harness: when this file grows, this test grows
 * with it. */
static int32_t test_self_host(void) {
    const char *src;
    size_t src_len;
    char *c1 = NULL, *c2 = NULL;
    size_t c1_len = 0, c2_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    int32_t rc;
    /* both arenas stay alive until the compare — the C outputs live in them */
    void *backing1 = NULL, *backing2 = NULL;
    pm_util_mem_arena_t *arena1 = NULL, *arena2 = NULL;

    src = pm_metal_inspect_src_read("pymergetic.metal.jit.rs.compiler", "__impl__.rs");
    if (src == NULL) return 130;
    src_len = strlen(src);
    if (src_len < 100000) return 131; /* the real file is ~380 KB */

    backing1 = malloc(1u << 26);
    if (!backing1) return 132;
    arena1 = pm_util_mem_arena_create(backing1, 1u << 26);
    if (!arena1) { free(backing1); return 133; }
    memset(err, 0, sizeof(err));
    rc = pm_metal_jit_rsx_compile(arena1, src, src_len,
                                 &c1, &c1_len, err, sizeof(err));
    if (rc != 0) {
        pm_util_mem_arena_destroy(arena1); free(backing1);
        return 134;
    }

    backing2 = malloc(1u << 26);
    if (!backing2) {
        pm_util_mem_arena_destroy(arena1); free(backing1);
        return 135;
    }
    arena2 = pm_util_mem_arena_create(backing2, 1u << 26);
    if (!arena2) {
        pm_util_mem_arena_destroy(arena1); free(backing1);
        free(backing2);
        return 136;
    }
    memset(err, 0, sizeof(err));
    rc = pm_metal_jit_rsx_compile(arena2, src, src_len,
                                 &c2, &c2_len, err, sizeof(err));
    if (rc != 0) {
        pm_util_mem_arena_destroy(arena1); free(backing1);
        pm_util_mem_arena_destroy(arena2); free(backing2);
        return 137;
    }

    /* deterministic: both runs agree byte for byte */
    if (c1_len != c2_len) {
        pm_util_mem_arena_destroy(arena1); free(backing1);
        pm_util_mem_arena_destroy(arena2); free(backing2);
        return 138;
    }
    if (memcmp(c1, c2, c1_len) != 0) {
        pm_util_mem_arena_destroy(arena1); free(backing1);
        pm_util_mem_arena_destroy(arena2); free(backing2);
        return 139;
    }

    /* self-shape: the output is this compiler, not some small fixture */
    if (c1_len < 100000) {
        pm_util_mem_arena_destroy(arena1); free(backing1);
        pm_util_mem_arena_destroy(arena2); free(backing2);
        return 140;
    }
    if (!rsx_strstr(c1, "pm_metal_jit_rsx_lex")
        || !rsx_strstr(c1, "pm_metal_jit_rsx_lower")
        || !rsx_strstr(c1, "#line")
        || !rsx_strstr(c1, "Lexer_lex_punct")
        || !rsx_strstr(c1, "Lower_lower_fn")) {
        pm_util_mem_arena_destroy(arena1); free(backing1);
        pm_util_mem_arena_destroy(arena2); free(backing2);
        return 141;
    }

    pm_util_mem_arena_destroy(arena1);
    pm_util_mem_arena_destroy(arena2);
    free(backing1);
    free(backing2);
    return 0;
}

/* In-kernel object prove: the C this compiler emits for its own source
 * must be acceptable to the kernel's own C compiler (jit.c TCC card).
 * This closes the loop inside the kernel — no host cc anywhere: Rust
 * source -> micro-rustc -> C -> TCC -> object bytes, all in-process.
 * On seats without native object output (browser/wasm32, firmware) the
 * card refuses politely and this test skips rather than fails. */
static int32_t test_self_host_object(void) {
    const char *src;
    size_t src_len;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    char oerr[256];
    int32_t rc;
    void *backing = NULL, *obacking = NULL;
    pm_util_mem_arena_t *arena = NULL, *oarena = NULL;
    uint8_t *obj = NULL;
    size_t obj_len = 0;

    src = pm_metal_inspect_src_read("pymergetic.metal.jit.rs.compiler", "__impl__.rs");
    if (src == NULL) return 150;
    src_len = strlen(src);
    if (src_len < 100000) return 151;

    backing = malloc(1u << 26);
    if (!backing) return 152;
    arena = pm_util_mem_arena_create(backing, 1u << 26);
    if (!arena) { free(backing); return 153; }
    memset(err, 0, sizeof(err));
    rc = pm_metal_jit_rsx_compile(arena, src, src_len,
                                 &c_out, &c_out_len, err, sizeof(err));
    if (rc != 0) { pm_util_mem_arena_destroy(arena); free(backing); return 154; }
    if (c_out == NULL || c_out_len < 100000) {
        pm_util_mem_arena_destroy(arena); free(backing); return 155;
    }

    /* TCC in a second arena — the object bytes will live there. 64MB: the
     * in-arena TCC compile needs the tccpp pools (2 x 256KB) plus tables. */
    obacking = malloc(1u << 26);
    if (!obacking) { pm_util_mem_arena_destroy(arena); free(backing); return 156; }
    oarena = pm_util_mem_arena_create(obacking, 1u << 26);
    if (!oarena) {
        pm_util_mem_arena_destroy(arena); free(backing);
        free(obacking);
        return 157;
    }
    memset(oerr, 0, sizeof(oerr));
    rc = pm_metal_jit_c_object_compile(oarena, c_out, c_out_len,
                                      &obj, &obj_len, oerr, sizeof(oerr));
    if (rc != 0) {
        /* polite seat refusal (browser/wasm32, firmware) — skip, not fail */
        if (rsx_strstr(oerr, "no native object output on this seat")) {
            pm_util_mem_arena_destroy(arena); free(backing);
            pm_util_mem_arena_destroy(oarena); free(obacking);
            return 0;
        }
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking);
        return 158;
    }
    if (obj == NULL || obj_len < sizeof(uint32_t)) {
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking);
        return 159;
    }

    /* ELF magic for the native object path (0x7f 'E' 'L' 'F') */
    if (!(obj[0] == 0x7f && obj[1] == 'E' && obj[2] == 'L' && obj[3] == 'F')) {
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking);
        return 160;
    }

    pm_util_mem_arena_destroy(arena);
    pm_util_mem_arena_destroy(oarena);
    free(backing);
    free(obacking);
    return 0;
}

/* In-kernel link prove: the object stage's bytes are linked by the build
 * card's ELF relocator (the software-defined linker) and the rsx entries
 * are called straight out of the linked image. The C the LINKED compiler
 * emits for the same source must equal what the boot (rustc) compiler
 * emitted — the fixed point proven through the kernel's own
 * Rust -> C -> TCC -> link chain, no host cc anywhere. Seats without the
 * ELF link path (browser wasm cell, firmware) refuse politely and this
 * test skips rather than fails. */
static int32_t test_self_host_link(void) {
#if defined(PM_METAL_BUILD_HAS_ELF) && PM_HAS_TCC && !defined(TCC_TARGET_WASM32)
    const char *src;
    size_t src_len;
    char *c_boot = NULL, *c_linked = NULL;
    size_t c_boot_len = 0, c_linked_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    char oerr[256];
    int32_t rc;
    void *backing = NULL, *obacking = NULL;
    pm_util_mem_arena_t *arena = NULL, *oarena = NULL;
    uint8_t *obj = NULL;
    size_t obj_len = 0;
    pm_metal_build_unit_t unit;
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

    src = pm_metal_inspect_src_read("pymergetic.metal.jit.rs.compiler", "__impl__.rs");
    if (src == NULL) return 160;
    src_len = strlen(src);
    if (src_len < 100000) return 161;

    /* boot compiler's output — the reference bytes */
    backing = malloc(1u << 26);
    if (!backing) return 162;
    arena = pm_util_mem_arena_create(backing, 1u << 26);
    if (!arena) { free(backing); return 163; }
    memset(err, 0, sizeof(err));
    rc = pm_metal_jit_rsx_compile(arena, src, src_len,
                                 &c_boot, &c_boot_len, err, sizeof(err));
    if (rc != 0) { pm_util_mem_arena_destroy(arena); free(backing); return 164; }
    if (c_boot == NULL || c_boot_len < 100000) {
        pm_util_mem_arena_destroy(arena); free(backing); return 165;
    }

    /* object + link + run the linked compiler, all in a second arena */
    obacking = malloc(1u << 26);
    if (!obacking) { pm_util_mem_arena_destroy(arena); free(backing); return 166; }
    oarena = pm_util_mem_arena_create(obacking, 1u << 26);
    if (!oarena) {
        pm_util_mem_arena_destroy(arena); free(backing);
        free(obacking);
        return 167;
    }
    memset(oerr, 0, sizeof(oerr));
    rc = pm_metal_jit_c_object_compile(oarena, c_boot, c_boot_len,
                                      &obj, &obj_len, oerr, sizeof(oerr));
    if (rc != 0) {
        if (strstr(oerr, "no native object output on this seat") != NULL) {
            pm_util_mem_arena_destroy(arena); free(backing);
            pm_util_mem_arena_destroy(oarena); free(obacking);
            return 0; /* polite seat refusal — skip */
        }
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking);
        return 168;
    }

    memset(&unit, 0, sizeof(unit));
    snprintf(unit.fqn, sizeof(unit.fqn), "%s", "selfhost.rsx.linked");
    objs[0] = obj;
    lens[0] = obj_len;
    memset(oerr, 0, sizeof(oerr));
    rc = pm_metal_build_link(oarena, &unit, objs, lens, 1, &art,
                             oerr, sizeof(oerr));
    if (rc != PM_METAL_BUILD_OK) {
        if (strstr(oerr, "no ELF loader on this seat") != NULL) {
            pm_util_mem_arena_destroy(arena); free(backing);
            pm_util_mem_arena_destroy(oarena); free(obacking);
            return 0; /* polite seat refusal — skip */
        }
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking);
        return 169;
    }

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
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking);
        return 170;
    }

    memset(&l_toks, 0, sizeof(l_toks));
    memset(err, 0, sizeof(err));
    if (l_lex(oarena, src, src_len, &l_toks, err, sizeof(err)) != 0) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking);
        return 171;
    }
    memset(err, 0, sizeof(err));
    if (l_parse(oarena, &l_toks, &l_unit, err, sizeof(err)) != 0) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking);
        return 172;
    }
    memset(err, 0, sizeof(err));
    if (l_lower(oarena, l_unit, &c_linked, &c_linked_len, err, sizeof(err)) != 0) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking);
        return 173;
    }

    /* the fixed point through the kernel's own chain */
    if (c_linked_len != c_boot_len
        || memcmp(c_linked, c_boot, c_boot_len) != 0) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking);
        return 174;
    }

    pm_metal_build_artifact_destroy(&art);
    pm_util_mem_arena_destroy(arena);
    pm_util_mem_arena_destroy(oarena);
    free(backing);
    free(obacking);
    return 0;
#else
    /* No native TCC object output / no ELF loader on this seat — the
     * in-kernel link loop is a host+unix-seat capability; skip, not fail. */
    return 0;
#endif
}

/* --- introspection ----------------------------------------------------- */

static int32_t test_introspection(void) {
    if (pm_metal_jit_rsx_token_kind_count() == 0) return 120;
    if (pm_metal_jit_rsx_ast_kind_count() == 0) return 121;
    return 0;
}

/* --- registration ------------------------------------------------------ */

static int32_t pm_metal_jit_rsx_tests(void) {
    int32_t rc;
    rc = test_lex_minimal();      if (rc) return rc;
    rc = test_lex_macro();        if (rc) return rc;
    rc = test_lex_errors();       if (rc) return rc;
    rc = test_parse_minimal();    if (rc) return rc;
    rc = test_parse_struct();     if (rc) return rc;
    rc = test_parse_fn();         if (rc) return rc;
    rc = test_parse_unsupported(); if (rc) return rc;
    rc = test_compile_minimal_fn(); if (rc) return rc;
    rc = test_compile_struct();   if (rc) return rc;
    rc = test_compile_fn_body();  if (rc) return rc;
    rc = test_compile_provenance(); if (rc) return rc;
    rc = test_ast_dump();         if (rc) return rc;
    rc = test_self_host();       if (rc) return rc;
    rc = test_self_host_object(); if (rc) return rc;
    rc = test_self_host_link();  if (rc) return rc;
    rc = test_introspection();    if (rc) return rc;
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.jit.rs.compiler, tests, pm_metal_jit_rsx_tests);
