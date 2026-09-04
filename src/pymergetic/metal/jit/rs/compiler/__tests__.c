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
    void *backing = malloc(1u << 26);
    pm_util_mem_arena_t *arena;
    pm_jit_rsx_toklist_t toks;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "extern \"C\" {\n"
        "    fn pm_test_hello() -> i32;\n"
        "}\n";

    if (backing == NULL) return 1;
    arena = pm_util_mem_arena_create(backing, 1u << 26);
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
    void *backing = malloc(1u << 26);
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
    arena = pm_util_mem_arena_create(backing, 1u << 26);
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
    void *backing = malloc(1u << 26);
    pm_util_mem_arena_t *arena;
    pm_jit_rsx_toklist_t toks;
    char err[PM_METAL_JIT_RSX_ERR_MAX];

    if (backing == NULL) return 20;
    arena = pm_util_mem_arena_create(backing, 1u << 26);
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
    void *backing = malloc(1u << 26);
    pm_util_mem_arena_t *arena;
    pm_jit_rsx_toklist_t toks;
    pm_jit_rsx_ast_t *unit = NULL;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "extern \"C\" {\n"
        "    fn pm_test_hello() -> i32;\n"
        "}\n";

    if (backing == NULL) return 30;
    arena = pm_util_mem_arena_create(backing, 1u << 26);
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
    void *backing = malloc(1u << 26);
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
    arena = pm_util_mem_arena_create(backing, 1u << 26);
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
    void *backing = malloc(1u << 26);
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
    arena = pm_util_mem_arena_create(backing, 1u << 26);
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
    void *backing = malloc(1u << 26);
    pm_util_mem_arena_t *arena;
    pm_jit_rsx_toklist_t toks;
    pm_jit_rsx_ast_t *unit = NULL;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    /* trait items PARSE now (the dyn plane); async fn is the remaining
     * refused item kind — the negative probe rides it. */
    static const char async_src[] =
        "async fn f() {\n"
        "}\n";

    if (backing == NULL) return 60;
    arena = pm_util_mem_arena_create(backing, 1u << 26);
    if (arena == NULL) { free(backing); return 61; }

    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_lex(arena, async_src, strlen(async_src), &toks, err, sizeof(err)) != 0) {
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

/* parse+lower: `mod` items (path form and inline body) are structure —
 * the flat C translation records them as a comment, never a refusal. */
static int32_t test_compile_mod_item(void) {
    void *backing = malloc(1u << 26);
    pm_util_mem_arena_t *arena;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "#[cfg(test)]\n"
        "#[path = \"__tests__.rs\"]\n"
        "mod __tests__;\n"
        "pub mod sink;\n"
        "pub use sink::{apply_faces, GenSink};\n"
        "fn pm_mod_probe() -> i32 {\n"
        "    7\n"
        "}\n";

    if (backing == NULL) return 70;
    arena = pm_util_mem_arena_create(backing, 1u << 26);
    if (arena == NULL) { free(backing); return 71; }
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, src, strlen(src),
            &c_out, &c_out_len, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 72;
    }
    if (c_out == NULL || c_out_len == 0
        || strstr(c_out, "pm_mod_probe") == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 73;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* --- lower / compile --------------------------------------------------- */

static int32_t test_compile_minimal_fn(void) {
    void *backing = malloc(1u << 26);
    pm_util_mem_arena_t *arena;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "extern \"C\" {\n"
        "    fn pm_test_hello() -> i32;\n"
        "}\n";

    if (backing == NULL) return 70;
    arena = pm_util_mem_arena_create(backing, 1u << 26);
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
    void *backing = malloc(1u << 26);
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
    arena = pm_util_mem_arena_create(backing, 1u << 26);
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

/* compile: an all-zero static initializer elides to a bare tentative
 * definition — the storage rides .bss with no materialized zero blob.
 * Covers every zero leaf: int literal 0, false, None (pointer Option),
 * ptr::null_mut(), a zero enum variant, [0; N] repeats, and the
 * Mut(UnsafeCell::new(..)) wrapper chain. */
static int32_t test_compile_zero_static_elision(void) {
    void *backing = malloc(1u << 26);
    pm_util_mem_arena_t *arena;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "#[repr(C)]\n"
        "pub struct Row {\n"
        "    a: u32,\n"
        "    p: *mut u8,\n"
        "}\n"
        "#[repr(C)]\n"
        "pub enum Kind {\n"
        "    Zero = 0,\n"
        "    One = 1,\n"
        "}\n"
        "#[repr(C)]\n"
        "pub struct Tbl {\n"
        "    rows: [Row; 2],\n"
        "    live: bool,\n"
        "    k: Kind,\n"
        "}\n"
        "struct Mut<T>(core::cell::UnsafeCell<T>);\n"
        "const N: usize = 2;\n"
        "static ZERO_TBL: Mut<Tbl> = Mut(core::cell::UnsafeCell::new(Tbl {\n"
        "    rows: [Row { a: 0, p: core::ptr::null_mut() }; N],\n"
        "    live: false,\n"
        "    k: Kind::Zero,\n"
        "}));\n"
        "static ZERO_ENUM: Kind = Kind::Zero;\n";

    if (backing == NULL) return 130;
    arena = pm_util_mem_arena_create(backing, 1u << 26);
    if (arena == NULL) { free(backing); return 131; }

    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, src, strlen(src),
                                 &c_out, &c_out_len, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 132;
    }
    if (c_out == NULL || c_out_len == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 133;
    }
    /* both statics emit bare: `static Tbl ZERO_TBL;` and
     * `static const Kind ZERO_ENUM;` — no ` = ` initializer */
    if (!rsx_strstr(c_out, "static Tbl ZERO_TBL;")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 134;
    }
    if (!rsx_strstr(c_out, "static const Kind ZERO_ENUM;")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 135;
    }
    /* the zero rows never expand — no compound-literal initializer for
     * the table at all */
    if (rsx_strstr(c_out, "ZERO_TBL =")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 136;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* compile: a nonzero static keeps its explicit initializer (elision is
 * only for provably-zero tables), and an if-let whose arm returns does
 * not double the following tail return. */
static int32_t test_compile_nonzero_static_and_iflet_return(void) {
    void *backing = malloc(1u << 26);
    pm_util_mem_arena_t *arena;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "#[repr(C)]\n"
        "pub struct Row {\n"
        "    a: u32,\n"
        "}\n"
        "#[repr(C)]\n"
        "pub struct Tbl {\n"
        "    rows: [Row; 2],\n"
        "}\n"
        "struct Mut<T>(core::cell::UnsafeCell<T>);\n"
        "static LIVE_TBL: Mut<Tbl> = Mut(core::cell::UnsafeCell::new(Tbl {\n"
        "    rows: [Row { a: 1 }; 2],\n"
        "}));\n"
        "pub type Runner = unsafe extern \"C\" fn() -> i32;\n"
        "static RUNNER: Mut<Option<Runner>> = Mut(core::cell::UnsafeCell::new(None));\n"
        "pub unsafe extern \"C\" fn zz_run() -> i32 {\n"
        "    unsafe {\n"
        "        let runner = *(RUNNER.0.get());\n"
        "        if let Some(runner) = runner {\n"
        "            return runner();\n"
        "        }\n"
        "        return -1;\n"
        "    }\n"
        "}\n";

    if (backing == NULL) return 140;
    arena = pm_util_mem_arena_create(backing, 1u << 26);
    if (arena == NULL) { free(backing); return 141; }

    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, src, strlen(src),
                                 &c_out, &c_out_len, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 142;
    }
    if (c_out == NULL || c_out_len == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 143;
    }
    /* nonzero rows keep the compound literal */
    if (!rsx_strstr(c_out, "static Tbl LIVE_TBL = (Tbl){")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 144;
    }
    /* the RUNNER cell is a zero Option — bare; the typedef name carries
     * the injective `<len>e<hex>` encoding (`Runner` = 52 75 6e 6e 65 72) */
    if (!rsx_strstr(c_out, "static rsx_opt_6e52756e6e6572 RUNNER;")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 145;
    }
    /* no doubled return from the if-let tail */
    if (rsx_strstr(c_out, "return     return")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 146;
    }
    if (!rsx_strstr(c_out, "return runner();")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 147;
    }
    if (!rsx_strstr(c_out, "return (-1);")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 148;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* compile: match range patterns (lo..=hi), literal or-patterns, and
 * Some(bind) — the three pattern lifts (each refuses by name before). */
static int32_t test_compile_match_patterns(void) {
    void *backing = malloc(1u << 26);
    pm_util_mem_arena_t *arena;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "pub fn pm_octal_step(b: u8, val: u64) -> u64 {\n"
        "    match b {\n"
        "        b'0'..=b'7' => val * 8 + (b - b'0') as u64,\n"
        "        b' ' | 0 => val,\n"
        "        _ => 0,\n"
        "    }\n"
        "}\n"
        "pub fn pm_take(opt: *const u8) -> u64 {\n"
        "    match opt {\n"
        "        Some(h) => unsafe { *h as u64 },\n"
        "        None => 0,\n"
        "    }\n"
        "}\n";

    if (backing == NULL) return 96;
    arena = pm_util_mem_arena_create(backing, 1u << 26);
    if (arena == NULL) { free(backing); return 97; }

    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, src, strlen(src),
                                 &c_out, &c_out_len, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 98;
    }
    if (c_out == NULL || c_out_len == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 99;
    }
    if (!rsx_strstr(c_out, ">= '0'") || !rsx_strstr(c_out, "<= '7'")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 100;
    }
    if (!rsx_strstr(c_out, "== ' '") || !rsx_strstr(c_out, "__rsx_m == 0")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 101;
    }
    /* the Some bind is declared as an alias of the scrutinee temp */
    if (!rsx_strstr(c_out, "h = __rsx_m")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 102;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* compile: `expr?` on an Option-of-pointer and range indexes */
static int32_t test_compile_try_and_range_index(void) {
    void *backing = malloc(1u << 26);
    pm_util_mem_arena_t *arena;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "extern \"C\" {\n"
        "    fn pm_test_lookup(k: u32) -> *const u8;\n"
        "}\n"
        "pub fn pm_test_get(k: u32) -> *const u8 {\n"
        "    let p = pm_test_lookup(k)?;\n"
        "    p\n"
        "}\n"
        "pub fn pm_test_sub(buf: *const u8, at: usize) -> *const u8 {\n"
        "    let s = &buf[at..at + 4];\n"
        "    let t = &buf[..at];\n"
        "    s\n"
        "}\n";

    if (backing == NULL) return 130;
    arena = pm_util_mem_arena_create(backing, 1u << 26);
    if (arena == NULL) { free(backing); return 131; }

    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, src, strlen(src),
                                 &c_out, &c_out_len, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 132;
    }
    if (c_out == NULL || c_out_len == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 133;
    }
    /* ? lowers to a statement expression with a null test + early return */
    if (!rsx_strstr(c_out, "__rsx_try") || !rsx_strstr(c_out, "return 0; } __rsx_try; })")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 134;
    }
    /* `&buf[a..b]` lowers to pointer arithmetic, not an address-of */
    if (!rsx_strstr(c_out, "(buf + at)")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 135;
    }
    /* `&buf[..b]` lowers to the base pointer alone */
    if (!rsx_strstr(c_out, "= (buf)")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 136;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* compile: labeled loops — `'l: for/while/loop` plus `continue 'l` and
 * `break 'l`. C has no labeled break/continue, so the lowering emits
 * goto targets (`rsx_lbl_<name>_cont/_end`); the plain forms stay
 * `break`/`continue`. Also proves the for-range binding is the real
 * loop variable (a PATH wrapper bug once emitted a var named "path"). */
static int32_t test_compile_labeled_loops(void) {
    void *backing = malloc(1u << 26);
    pm_util_mem_arena_t *arena;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "pub fn pm_test_scan(h: *const u8, hn: usize, n: *const u8, nn: usize) -> i32 {\n"
        "    let mut i = 0usize;\n"
        "    'outer: while i + nn <= hn {\n"
        "        let mut j = 0usize;\n"
        "        while j < nn {\n"
        "            let a = unsafe { *h.add(i + j) };\n"
        "            let b = unsafe { *n.add(j) };\n"
        "            if a | 32 != b | 32 {\n"
        "                i += 1;\n"
        "                continue 'outer;\n"
        "            }\n"
        "            j += 1;\n"
        "        }\n"
        "        return 1;\n"
        "    }\n"
        "    0\n"
        "}\n"
        "pub fn pm_test_plain(b: *const u8, bn: usize) -> u32 {\n"
        "    let mut c = 0u32;\n"
        "    for k in 0..bn {\n"
        "        c += unsafe { *b.add(k) } as u32;\n"
        "    }\n"
        "    c\n"
        "}\n";

    if (backing == NULL) return 140;
    arena = pm_util_mem_arena_create(backing, 1u << 26);
    if (arena == NULL) { free(backing); return 141; }

    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, src, strlen(src),
                                 &c_out, &c_out_len, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 142;
    }
    if (c_out == NULL || c_out_len == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 143;
    }
    /* labeled while opens with the goto-target prefix label */
    if (!rsx_strstr(c_out, "rsx_lbl_outer_: while")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 144;
    }
    /* `continue 'outer` is a goto to the _cont target inside the loop */
    if (!rsx_strstr(c_out, "goto rsx_lbl_outer_cont") || !rsx_strstr(c_out, "rsx_lbl_outer_cont:")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 145;
    }
    /* the labeled loop emits its _end target after the closing brace */
    if (!rsx_strstr(c_out, "rsx_lbl_outer_end:")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 146;
    }
    /* plain break/continue (no label) stay the C keywords */
    if (!rsx_strstr(c_out, "break;") && !rsx_strstr(c_out, "continue;")) {
        /* none present in this src — both branches use labeled forms or
         * returns; just ensure no stray "goto " outside the label cases */
    }
    /* the for-range binding is the real variable, size_t like its bound */
    if (!rsx_strstr(c_out, "for (size_t k = 0; k < bn; k++)")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 147;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* compile: `union` items — the wasmmod Value convention's shape (kind +
 * union of payloads) is the canonical cross-card use. Same field grammar
 * as a struct; a literal is a designated initializer (sets the active
 * member); field access reads the active member. */
static int32_t test_compile_union_item(void) {
    void *backing = malloc(1u << 26);
    pm_util_mem_arena_t *arena;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "#[repr(C)]\n"
        "pub union pm_test_of {\n"
        "    pub a: i32,\n"
        "    pub b: i64,\n"
        "}\n"
        "#[repr(C)]\n"
        "pub struct pm_test_val {\n"
        "    pub kind: u32,\n"
        "    pub of: pm_test_of,\n"
        "}\n"
        "pub unsafe extern \"C\" fn pm_test_mk(x: i32) -> pm_test_val {\n"
        "    let v = pm_test_val { kind: 0, of: pm_test_of { a: x } };\n"
        "    v\n"
        "}\n"
        "pub unsafe extern \"C\" fn pm_test_rd(v: *const pm_test_val) -> i32 {\n"
        "    unsafe { (*v).of.a }\n"
        "}\n";

    if (backing == NULL) return 150;
    arena = pm_util_mem_arena_create(backing, 1u << 26);
    if (arena == NULL) { free(backing); return 151; }

    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, src, strlen(src),
                                 &c_out, &c_out_len, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 152;
    }
    if (c_out == NULL || c_out_len == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 153;
    }
    /* the union lowers to a C union, not a struct */
    if (!rsx_strstr(c_out, "union pm_test_of") || !rsx_strstr(c_out, "typedef union pm_test_of")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 154;
    }
    /* struct containing it keeps the struct tag */
    if (!rsx_strstr(c_out, "struct pm_test_val")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 155;
    }
    /* nested union literal is a designated initializer */
    if (!rsx_strstr(c_out, "(pm_test_of){ .a = x }")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 156;
    }
    /* field read through the pointer reaches the union member */
    if (!rsx_strstr(c_out, ".of") || !rsx_strstr(c_out, ".a")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 157;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* parse: nested generic types `A<B<C>>` — the `>>` lexes as one SHR token
 * and once hung the generic-list loop (the fix splits the close). */
static int32_t test_parse_nested_generics(void) {
    void *backing = malloc(1u << 26);
    pm_util_mem_arena_t *arena;
    pm_jit_rsx_toklist_t toks;
    pm_jit_rsx_ast_t *unit = NULL;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "pub struct Outer {\n"
        "    slot: crate::util::lock::Mutex<Option<Inner>>,\n"
        "}\n"
        "pub struct Inner {\n"
        "    n: u32,\n"
        "}\n";

    if (backing == NULL) return 104;
    arena = pm_util_mem_arena_create(backing, 1u << 26);
    if (arena == NULL) { free(backing); return 105; }
    memset(&toks, 0, sizeof(toks));
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_lex(arena, src, strlen(src), &toks, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 106;
    }
    if (pm_metal_jit_rsx_parse(arena, &toks, &unit, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 107;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* compile: `unsafe impl Marker for Type {}` (marker-trait impl, empty
 * body) parses and lowers to nothing. */
static int32_t test_compile_unsafe_impl_marker(void) {
    void *backing = malloc(1u << 26);
    pm_util_mem_arena_t *arena;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "pub struct PyHook {\n"
        "    f: u32,\n"
        "}\n"
        "unsafe impl Send for PyHook {}\n"
        "pub fn pm_hook_probe() -> i32 {\n"
        "    1\n"
        "}\n";

    if (backing == NULL) return 108;
    arena = pm_util_mem_arena_create(backing, 1u << 26);
    if (arena == NULL) { free(backing); return 109; }
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, src, strlen(src),
                                 &c_out, &c_out_len, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 110;
    }
    if (c_out == NULL || c_out_len == 0
        || !rsx_strstr(c_out, "pm_hook_probe")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 111;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* compile: a fn with a body lowers to a C fn with the same name */
static int32_t test_compile_fn_body(void) {
    void *backing = malloc(1u << 26);
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
    arena = pm_util_mem_arena_create(backing, 1u << 26);
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
    void *backing = malloc(1u << 26);
    pm_util_mem_arena_t *arena;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char src[] =
        "extern \"C\" {\n"
        "    fn pm_test_hello() -> i32;\n"
        "}\n";

    if (backing == NULL) return 100;
    arena = pm_util_mem_arena_create(backing, 1u << 26);
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
    void *backing = malloc(1u << 26);
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
    arena = pm_util_mem_arena_create(backing, 1u << 26);
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
        fprintf(stderr, "self_host_object: tcc: %s\n", oerr);
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

/* --- atomic runtime: linked execution of RSX-generated + TCC-compiled code ---
 * Audit items:
 * (1) store must assign the desired value to the value tmp BEFORE
 *     __atomic_store reads it;
 * (2) swap's desired and old must be separate objects; the expression must
 *     yield the OLD value with nonzero distinct operands at runtime;
 * (3) a non-atomic receiver naming .store/.load/.swap must REFUSE, never
 *     miscompile to __atomic_* builtins on a plain field.
 * Proven by: rsx compile -> in-kernel TCC object -> ELF link -> calling the
 * exported fn straight out of the linked image. */

static const char ATOMIC_RT_SRC[] =
    "use core::sync::atomic::{AtomicU32, Ordering};\n"
    "#[repr(C)]\n"
    "pub struct T { pub a: AtomicU32 }\n"
    "#[no_mangle]\n"
    "pub extern \"C\" fn rsx_at_rt_main() -> u32 {\n"
    "    let t = T { a: AtomicU32::new(0) };\n"
    "    t.a.store(0x12345678, Ordering::SeqCst);\n"
    "    let old: u32 = t.a.swap(0x87654321, Ordering::SeqCst);\n"
    "    let fin: u32 = t.a.load(Ordering::SeqCst);\n"
    "    if old != 0x12345678 { return 0xDEAD0001; }\n"
    "    if fin != 0x87654321 { return 0xDEAD0002; }\n"
    "    0x600D600D\n"
    "}\n";

/* negative: a plain u32 field named .store must refuse */
static const char ATOMIC_NEG_SRC[] =
    "use core::sync::atomic::{AtomicU32, Ordering};\n"
    "#[repr(C)]\n"
    "pub struct P { pub v: u32 }\n"
    "#[no_mangle]\n"
    "pub extern \"C\" fn rsx_at_neg_main(p: *mut P) -> u32 {\n"
    "    (*p).v.store(0x12345678, Ordering::SeqCst);\n"
    "    0\n"
    "}\n";

static int32_t test_atomic_runtime_linked(void) {
#if defined(PM_METAL_BUILD_HAS_ELF) && PM_HAS_TCC && !defined(TCC_TARGET_WASM32)
    void *backing = NULL, *obacking = NULL;
    pm_util_mem_arena_t *arena = NULL, *oarena = NULL;
    char *c = NULL;
    size_t c_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    char oerr[256];
    int32_t rc;
    uint8_t *obj = NULL;
    size_t obj_len = 0;
    pm_metal_build_unit_t unit;
    pm_metal_build_artifact_t art;
    uint8_t *objs[1];
    size_t lens[1];
    uint32_t (*l_main)(void);
    uint32_t r;

    /* --- (3) negative: non-atomic receiver must REFUSE --- */
    backing = malloc(1u << 24);
    if (!backing) return 180;
    arena = pm_util_mem_arena_create(backing, 1u << 24);
    if (!arena) { free(backing); return 181; }
    memset(err, 0, sizeof(err));
    rc = pm_metal_jit_rsx_compile(arena, ATOMIC_NEG_SRC,
                                  strlen(ATOMIC_NEG_SRC),
                                  &c, &c_len, err, sizeof(err));
    if (rc == 0) {
        /* miscompile slipped through: .store on a plain u32 compiled */
        pm_util_mem_arena_destroy(arena); free(backing);
        return 182;
    }
    if (strstr(err, "non-atomic receiver") == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing);
        return 183;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    arena = NULL;

    /* --- (1)(2) positive: codegen shape + linked execution --- */
    backing = malloc(1u << 24);
    if (!backing) return 184;
    arena = pm_util_mem_arena_create(backing, 1u << 24);
    if (!arena) { free(backing); return 185; }
    memset(err, 0, sizeof(err));
    c = NULL; c_len = 0;
    rc = pm_metal_jit_rsx_compile(arena, ATOMIC_RT_SRC,
                                  strlen(ATOMIC_RT_SRC),
                                  &c, &c_len, err, sizeof(err));
    if (rc != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 186;
    }
    if (c == NULL || c_len < 200) {
        pm_util_mem_arena_destroy(arena); free(backing); return 187;
    }

    /* (1) the desired value must be assigned to the tmp BEFORE
     * __atomic_store reads it: `__rsx_atN = 0x12345678;` must appear
     * before `__atomic_store(...)` in the same statement expression. */
    {
        const char *st = strstr(c, "__atomic_store");
        const char *asg = NULL;
        const char *p;
        if (st == NULL) {
            pm_util_mem_arena_destroy(arena); free(backing); return 188;
        }
        p = st;
        while (p > c) {
            if (p >= c + 14
                && memcmp(p - 14, " = 0x12345678;", 14) == 0) {
                asg = p - 14;
                break;
            }
            p--;
        }
        if (asg == NULL || asg > st) {
            pm_util_mem_arena_destroy(arena); free(backing); return 189;
        }
        /* the tmp assigned must be the tmp whose address is passed */
        {
            const char *amp = strstr(st, "&__rsx_at");
            char tmp_at[32];
            size_t al = 0;
            char asg_at[32];
            size_t bl = 0;
            if (amp == NULL) {
                pm_util_mem_arena_destroy(arena); free(backing); return 190;
            }
            amp += 1; /* skip the & — copy the full `__rsx_atN` name */
            while (*amp != 0 && *amp != ',' && *amp != ' '
                   && al < sizeof(tmp_at) - 1) {
                tmp_at[al++] = *amp++;
            }
            tmp_at[al] = 0;
            /* walk back from the assignment to its LHS start: scan back
             * over the digits to the `__rsx_at` prefix */
            p = asg;
            while (p > c && *(p - 1) >= '0' && *(p - 1) <= '9') p--;
            if (p < c + 8 || memcmp(p - 8, "__rsx_at", 8) != 0) {
                pm_util_mem_arena_destroy(arena); free(backing); return 191;
            }
            p -= 8;
            while (p < asg && bl < sizeof(asg_at) - 1) asg_at[bl++] = *p++;
            asg_at[bl] = 0;
            if (strcmp(asg_at, tmp_at) != 0) {
                pm_util_mem_arena_destroy(arena); free(backing); return 192;
            }
        }
    }

    /* (2) exchange must use two DISTINCT tmp names — desired and old
     * never share storage (the builtin contract forbids aliasing). */
    {
        const char *ex = strstr(c, "__atomic_exchange");
        const char *d1;
        const char *d2;
        char t1[32];
        size_t l1 = 0;
        if (ex == NULL) {
            pm_util_mem_arena_destroy(arena); free(backing); return 193;
        }
        d1 = strstr(ex, "&__rsx_at");
        if (d1 == NULL) {
            pm_util_mem_arena_destroy(arena); free(backing); return 194;
        }
        d1 += 9;
        while (d1[l1] >= '0' && d1[l1] <= '9' && l1 < sizeof(t1) - 1) l1++;
        memcpy(t1, d1, l1);
        t1[l1] = 0;
        d2 = strstr(d1 + l1, "&__rsx_at");
        if (d2 == NULL) {
            pm_util_mem_arena_destroy(arena); free(backing); return 195;
        }
        d2 += 9;
        if (strncmp(d2, t1, l1 + 1) == 0) {
            /* same tmp passed as desired AND old — aliasing */
            pm_util_mem_arena_destroy(arena); free(backing); return 196;
        }
    }

    /* linked execution: TCC object -> ELF link -> call from the image */
    obacking = malloc(1u << 24);
    if (!obacking) {
        pm_util_mem_arena_destroy(arena); free(backing); return 197;
    }
    oarena = pm_util_mem_arena_create(obacking, 1u << 24);
    if (!oarena) {
        pm_util_mem_arena_destroy(arena); free(backing); free(obacking);
        return 198;
    }
    memset(oerr, 0, sizeof(oerr));
    rc = pm_metal_jit_c_object_compile(oarena, c, c_len, &obj, &obj_len,
                                       oerr, sizeof(oerr));
    if (rc != 0) {
        if (strstr(oerr, "no native object output on this seat") != NULL) {
            pm_util_mem_arena_destroy(arena); free(backing);
            pm_util_mem_arena_destroy(oarena); free(obacking);
            return 0; /* polite seat refusal — skip */
        }
        fprintf(stderr, "atomic_rt: object compile failed: %s\n", oerr);
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking); return 199;
    }
    memset(&unit, 0, sizeof(unit));
    snprintf(unit.fqn, sizeof(unit.fqn), "%s", "rsx.atomic.rt");
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
        pm_util_mem_arena_destroy(oarena); free(obacking); return 200;
    }
    l_main = (uint32_t (*)(void))
        pm_metal_build_artifact_lookup(&art, "rsx_at_rt_main");
    if (l_main == NULL) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking); return 201;
    }
    r = l_main();
    pm_metal_build_artifact_destroy(&art);
    pm_util_mem_arena_destroy(arena);
    pm_util_mem_arena_destroy(oarena);
    free(backing);
    free(obacking);
    /* old == 0x12345678 AND fin == 0x87654321, else the linked code
     * reports the mismatch itself (0xDEAD0001/2) */
    if (r != 0x600D600D) return 202;
    return 0;
#else
    /* No native TCC object output / no ELF loader on this seat — skip */
    return 0;
#endif
}

/* --- tuple typedef collision ---------------------------------------------
 * The OLD naming schemes are NOT injective:
 *   join-only: `(Foo_, Bar)` and `(Foo, _Bar)` both sanitized to
 *     `rsx_tuple_Foo__Bar` — two different C types, one typedef name.
 *   `<len>e<sanitized>`: `Foo *` and `Foo__` are both 5 raw bytes and
 *     both sanitize to `Foo__` — `(*mut Foo, u32)` and `(Foo__, u32)`
 *     collided as `rsx_tuple_2_5eFoo__5e75696e7433325f74`.
 * The NEW scheme encodes every payload byte as two lowercase hex digits
 * (`<raw_len>e<2*raw_len hex>`), so distinct raw spellings always render
 * distinct identifiers. This test constructs BOTH colliding pairs and
 * proves the emitted typedef names differ and carry the exact hex. */

static const char TUP_COLLIDE_SRC[] =
    "#[repr(C)]\n"
    "pub struct Foo_ { pub x: u32 }\n"
    "#[repr(C)]\n"
    "pub struct Foo { pub x: u32 }\n"
    "#[repr(C)]\n"
    "pub struct Bar { pub x: u32 }\n"
    "#[repr(C)]\n"
    "pub struct _Bar { pub x: u32 }\n"
    "#[repr(C)]\n"
    "pub struct Foo__ { pub x: u32 }\n"
    "#[no_mangle]\n"
    "pub extern \"C\" fn make_a() -> (Foo_, Bar) {\n"
    "    (Foo_ { x: 1 }, Bar { x: 2 })\n"
    "}\n"
    "#[no_mangle]\n"
    "pub extern \"C\" fn make_b() -> (Foo, _Bar) {\n"
    "    (Foo { x: 3 }, _Bar { x: 4 })\n"
    "}\n"
    "#[no_mangle]\n"
    "pub extern \"C\" fn make_c(p: *mut Foo) -> (*mut Foo, u32) {\n"
    "    (p, 5u32)\n"
    "}\n"
    "#[no_mangle]\n"
    "pub extern \"C\" fn make_d(q: Foo__) -> (Foo__, u32) {\n"
    "    (q, 6u32)\n"
    "}\n"
    "#[no_mangle]\n"
    "pub extern \"C\" fn make_c1(p: *mut Foo) -> u32 {\n"
    "    let (q, v) = make_c(p);\n"
    "    v\n"
    "}\n"
    "#[no_mangle]\n"
    "pub extern \"C\" fn make_d1(q: Foo__) -> u32 {\n"
    "    let (w, v) = make_d(q);\n"
    "    v\n"
    "}\n";

static int32_t test_tuple_collision(void) {
    void *backing = NULL;
    pm_util_mem_arena_t *arena = NULL;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    int32_t rc;
    char names[4][96];
    size_t nn = 0;
    const char *t;

    backing = malloc(1u << 24);
    if (!backing) return 210;
    arena = pm_util_mem_arena_create(backing, 1u << 24);
    if (!arena) { free(backing); return 211; }
    memset(err, 0, sizeof(err));
    rc = pm_metal_jit_rsx_compile(arena, TUP_COLLIDE_SRC,
                                  strlen(TUP_COLLIDE_SRC),
                                  &c_out, &c_out_len, err, sizeof(err));
    if (rc != 0) {
        fprintf(stderr, "tuple collision: %s\n", err);
        pm_util_mem_arena_destroy(arena); free(backing); return 212;
    }
    if (c_out == NULL || c_out_len < 200) {
        pm_util_mem_arena_destroy(arena); free(backing); return 213;
    }
    /* collect every tuple typedef name: `rsx_tuple_<N>_<len>e...` */
    t = c_out;
    while ((t = strstr(t, "rsx_tuple_")) != NULL) {
        size_t l = 0;
        if (nn >= 4) break;
        while (t[l] != 0 && t[l] != ';' && t[l] != ' ' && l < sizeof(names[0]) - 1) {
            names[nn][l] = t[l]; l++;
        }
        names[nn][l] = 0;
        nn++;
        t++;
    }
    if (nn != 4) {
        fprintf(stderr, "tuple collision: expected 4 names, saw %zu\n", nn);
        pm_util_mem_arena_destroy(arena); free(backing); return 214;
    }
    /* the four signatures must render pairwise DISTINCT identifiers */
    {
        size_t i, j;
        for (i = 0; i < nn; i++) {
            for (j = i + 1; j < nn; j++) {
                if (strcmp(names[i], names[j]) == 0) {
                    fprintf(stderr, "tuple collision: %s == %s\n", names[i], names[j]);
                    pm_util_mem_arena_destroy(arena); free(backing); return 215;
                }
            }
        }
    }
    /* exact canonical names (hex of the raw C-type bytes):
     * Foo_  = 46 6f 6f 5f        Bar  = 42 61 72
     * Foo   = 46 6f 6f           _Bar = 5f 42 61 72
     * Foo * = 46 6f 6f 20 2a     u32->uint32_t = 75 69 6e 74 33 32 5f 74
     * Foo__ = 46 6f 6f 5f 5f */
    {
        static const char *want[4] = {
            "rsx_tuple_2_4e466f6f5f_3e426172",
            "rsx_tuple_2_3e466f6f_4e5f426172",
            "rsx_tuple_2_5e466f6f202a_8e75696e7433325f74",
            "rsx_tuple_2_5e466f6f5f5f_8e75696e7433325f74",
        };
        size_t k;
        int found[4] = {0, 0, 0, 0};
        size_t i;
        for (i = 0; i < nn; i++) {
            for (k = 0; k < 4; k++) {
                if (!found[k] && strcmp(names[i], want[k]) == 0) {
                    found[k] = 1;
                }
            }
        }
        for (k = 0; k < 4; k++) {
            if (!found[k]) {
                fprintf(stderr, "tuple collision: missing canonical name %s\n", want[k]);
                pm_util_mem_arena_destroy(arena); free(backing); return 216;
            }
        }
        /* the historical sanitization collision is specifically broken:
         * `Foo *` (466f6f202a) and `Foo__` (466f6f5f5f) must differ */
        if (strcmp(names[2], names[3]) == 0) {
            pm_util_mem_arena_destroy(arena); free(backing); return 217;
        }
    }
#if defined(PM_METAL_BUILD_HAS_ELF) && PM_HAS_TCC && !defined(TCC_TARGET_WASM32)
    /* in-kernel TCC object compile + ELF link + call: both historically
     * colliding signatures (make_c `(*mut Foo, u32)` and make_d
     * `(Foo__, u32)`) must coexist in ONE linked image — distinct
     * typedefs, no duplicate-definition conflict. */
    {
        void *obacking = malloc(1u << 24);
        pm_util_mem_arena_t *oarena;
        char oerr[256];
        uint8_t *obj = NULL;
        size_t obj_len = 0;
        pm_metal_build_unit_t unit;
        pm_metal_build_artifact_t art;
        uint8_t *objs[1];
        size_t lens[1];
        uint32_t (*mk_c)(void *);
        uint32_t (*mk_d)(void *);
        uint32_t r;

        if (!obacking) {
            pm_util_mem_arena_destroy(arena); free(backing); return 218;
        }
        oarena = pm_util_mem_arena_create(obacking, 1u << 24);
        if (!oarena) {
            pm_util_mem_arena_destroy(arena); free(backing);
            free(obacking);
            return 219;
        }
        memset(oerr, 0, sizeof(oerr));
        rc = pm_metal_jit_c_object_compile(oarena, c_out, c_out_len,
                                           &obj, &obj_len, oerr, sizeof(oerr));
        if (rc != 0) {
            int skip = oerr[0] != 0 && strstr(oerr, "no native object output on this seat") != NULL;
            pm_util_mem_arena_destroy(arena); free(backing);
            pm_util_mem_arena_destroy(oarena); free(obacking);
            if (skip) return 0; /* polite seat refusal — skip */
            fprintf(stderr, "tuple collision: tcc: %s\n", oerr);
            return 233;
        }
        memset(&unit, 0, sizeof(unit));
        snprintf(unit.fqn, sizeof(unit.fqn), "%s", "rsx.tupcollide");
        objs[0] = obj;
        lens[0] = obj_len;
        memset(oerr, 0, sizeof(oerr));
        rc = pm_metal_build_link(oarena, &unit, objs, lens, 1, &art,
                                 oerr, sizeof(oerr));
        if (rc != PM_METAL_BUILD_OK) {
            int skip = oerr[0] != 0 && strstr(oerr, "no ELF loader on this seat") != NULL;
            pm_util_mem_arena_destroy(arena); free(backing);
            pm_util_mem_arena_destroy(oarena); free(obacking);
            if (skip) return 0; /* polite seat refusal — skip */
            fprintf(stderr, "tuple collision: link: %s\n", oerr);
            return 234;
        }
        mk_c = (uint32_t (*)(void *))
            pm_metal_build_artifact_lookup(&art, "make_c1");
        mk_d = (uint32_t (*)(void *))
            pm_metal_build_artifact_lookup(&art, "make_d1");
        if (mk_c == NULL || mk_d == NULL) {
            pm_metal_build_artifact_destroy(&art);
            pm_util_mem_arena_destroy(arena); free(backing);
            pm_util_mem_arena_destroy(oarena); free(obacking); return 235;
        }
        r = mk_c(0); /* (*mut Foo, u32) via accessor: _1 carries 5 */
        if (r != 5u) {
            pm_metal_build_artifact_destroy(&art);
            pm_util_mem_arena_destroy(arena); free(backing);
            pm_util_mem_arena_destroy(oarena); free(obacking); return 236;
        }
        r = mk_d(0); /* (Foo__, u32) via accessor: _1 carries 6 */
        if (r != 6u) {
            pm_metal_build_artifact_destroy(&art);
            pm_util_mem_arena_destroy(arena); free(backing);
            pm_util_mem_arena_destroy(oarena); free(obacking); return 237;
        }
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(oarena);
        free(obacking);
    }
#endif /* PM_METAL_BUILD_HAS_ELF && TCC && !WASM32 */
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* --- tuple let-else ordering: runtime prove --------------------------------
 * Audit item 4: in the generated C, the `._has` test must run BEFORE any
 * `._v` payload read for tuple-of-Option let-else. Two things proven
 * here: (a) the codegen order — the `if (... ._has ...)` block precedes
 * the payload copies in the emitted C; (b) generated + TCC-compiled +
 * ELF-linked execution: the Some path binds both payloads, each None
 * path runs the diverging else-block (never the binds). */

static const char LET_ELSE_RT_SRC[] =
    "#[no_mangle]\n"
    "pub extern \"C\" fn le_pick(which: u32) -> (Option<u32>, Option<u32>) {\n"
    "    if which == 0 { return (None, None); }\n"
    "    if which == 1 { return (Some(0xAAA), None); }\n"
    "    if which == 2 { return (None, Some(0xBBB)); }\n"
    "    (Some(0xAAA), Some(0xBBB))\n"
    "}\n"
    "#[no_mangle]\n"
    "pub extern \"C\" fn le_run(which: u32) -> u32 {\n"
    "    let (Some(a), Some(b)) = le_pick(which) else { return 0xDEAD0000 + which; };\n"
    "    if a != 0xAAA { return 0xDEAD1000; }\n"
    "    if b != 0xBBB { return 0xDEAD2000; }\n"
    "    0x600D600D\n"
    "}\n";

/* --- let-chains (Rust 2024 `if a && let Some(x) = e`) -------------------
 * Positive: parse + lower a chain, generated-C shape checks, linked
 * runtime execution of all four short-circuit paths. Negative: malformed
 * chains refuse with the precise message, never a silent miscompile. */

static const char LET_CHAIN_RT_SRC[] =
    "#[no_mangle]\n"
    "pub extern \"C\" fn lc_pick(which: u32) -> (bool, Option<i32>, Option<i32>) {\n"
    "    if which == 0 { return (false, Some(30), Some(12)); }\n"
    "    if which == 1 { return (true, None, Some(12)); }\n"
    "    if which == 2 { return (true, Some(30), None); }\n"
    "    (true, Some(30), Some(12))\n"
    "}\n"
    "#[no_mangle]\n"
    "pub extern \"C\" fn lc_run(which: u32) -> i32 {\n"
    "    let (flag, a, b) = lc_pick(which);\n"
    "    if flag && let Some(x) = a && let Some(y) = b {\n"
    "        x + y\n"
    "    } else {\n"
    "        -1\n"
    "    }\n"
    "}\n";

static int32_t test_let_chain_parse_and_lower(void) {
    void *backing = malloc(1u << 24);
    pm_util_mem_arena_t *arena;
    char *c = NULL;
    size_t c_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];

    if (backing == NULL) return 250;
    arena = pm_util_mem_arena_create(backing, 1u << 24);
    if (arena == NULL) { free(backing); return 251; }
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, LET_CHAIN_RT_SRC,
                                 strlen(LET_CHAIN_RT_SRC),
                                 &c, &c_len, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 252;
    }
    if (c == NULL || c_len == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 253;
    }
    /* the outer expr segment folds to `if (flag)` — a plain C `&&`
     * binary op must NOT appear (the chain is nested ifs, not one test) */
    if (!rsx_strstr(c, "if (flag)")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 254;
    }
    if (rsx_strstr(c, "flag) &&")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 255;
    }
    /* the two let segments fold to nested option tests with distinct
     * scrutinee temps — both arms present, values bound */
    if (!rsx_strstr(c, "._has /* Some(path) */")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 256;
    }
    /* the chain's value: the innermost then-tail binds x+y through the
     * match-value temp; the else is the shared -1 */
    if (!rsx_strstr(c, "= (x + y);")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 257;
    }
    if (!rsx_strstr(c, "= (-1);")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 258;
    }
    /* two let segments = two nested option tests (scrutinee temps), not
     * one — count the Some-arm comments */
    {
        const char *p = c;
        int arms = 0;
        while ((p = strstr(p, "._has /* Some(path) */")) != NULL) {
            arms++;
            p++;
        }
        if (arms != 2) {
            pm_util_mem_arena_destroy(arena); free(backing); return 259;
        }
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

static int32_t test_let_chain_refuses(void) {
    void *backing = malloc(1u << 22);
    pm_util_mem_arena_t *arena;
    char *c = NULL;
    size_t c_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    static const char no_eq[] =
        "pub fn f(o: Option<i32>) -> i32 {\n"
        "    if let Some(x) { 0 } else { 1 }\n"
        "}\n";
    static const char bare_let[] =
        "pub fn f(a: bool) -> i32 {\n"
        "    if a && let { 0 } else { 1 }\n"
        "}\n";

    if (backing == NULL) return 260;
    arena = pm_util_mem_arena_create(backing, 1u << 22);
    if (arena == NULL) { free(backing); return 261; }

    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, no_eq, strlen(no_eq),
                                 &c, &c_len, err, sizeof(err)) == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 262;
    }
    if (strstr(err, "expected '=' in if-let") == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 263;
    }

    memset(err, 0, sizeof(err));
    c = NULL;
    if (pm_metal_jit_rsx_compile(arena, bare_let, strlen(bare_let),
                                 &c, &c_len, err, sizeof(err)) == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 264;
    }
    /* a `let` with no pattern must refuse — a loud parse error, never a
     * silent fall-through */
    if (err[0] == '\0') {
        pm_util_mem_arena_destroy(arena); free(backing); return 265;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

static int32_t test_let_chain_linked(void) {
#if defined(PM_METAL_BUILD_HAS_ELF) && PM_HAS_TCC && !defined(TCC_TARGET_WASM32)
    void *backing = NULL, *obacking = NULL;
    pm_util_mem_arena_t *arena = NULL, *oarena = NULL;
    char *c = NULL;
    size_t c_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    char oerr[256];
    int32_t rc;
    uint8_t *obj = NULL;
    size_t obj_len = 0;
    pm_metal_build_unit_t unit;
    pm_metal_build_artifact_t art;
    uint8_t *objs[1];
    size_t lens[1];
    int32_t (*l_run)(uint32_t);
    /* one selector per chain path: short-circuit, first-let fail,
     * second-let fail, all pass */
    static const int32_t want[4] = { -1, -1, -1, 42 };
    int i;

    backing = malloc(1u << 24);
    if (!backing) return 270;
    arena = pm_util_mem_arena_create(backing, 1u << 24);
    if (!arena) { free(backing); return 271; }
    memset(err, 0, sizeof(err));
    rc = pm_metal_jit_rsx_compile(arena, LET_CHAIN_RT_SRC,
                                  strlen(LET_CHAIN_RT_SRC),
                                  &c, &c_len, err, sizeof(err));
    if (rc != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 272;
    }

    /* the chain src above binds x+y from Pair fields; for the linked run
     * the object exports lc_run(Pair) — build Pairs per case through a
     * tiny second export that assembles one from raw args */
    obacking = malloc(1u << 24);
    if (!obacking) {
        pm_util_mem_arena_destroy(arena); free(backing); return 273;
    }
    oarena = pm_util_mem_arena_create(obacking, 1u << 24);
    if (!oarena) {
        pm_util_mem_arena_destroy(arena); free(backing); free(obacking);
        return 274;
    }
    memset(oerr, 0, sizeof(oerr));
    rc = pm_metal_jit_c_object_compile(oarena, c, c_len, &obj, &obj_len,
                                       oerr, sizeof(oerr));
    if (rc != 0) {
        if (strstr(oerr, "no native object output on this seat") != NULL) {
            pm_util_mem_arena_destroy(arena); free(backing);
            pm_util_mem_arena_destroy(oarena); free(obacking);
            return 0; /* polite seat refusal — skip */
        }
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking); return 275;
    }
    memset(&unit, 0, sizeof(unit));
    snprintf(unit.fqn, sizeof(unit.fqn), "%s", "rsx.letchain.rt");
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
        pm_util_mem_arena_destroy(oarena); free(obacking); return 276;
    }
    l_run = (int32_t (*)(uint32_t))
        pm_metal_build_artifact_lookup(&art, "lc_run");
    if (l_run == NULL) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking); return 277;
    }
    for (i = 0; i < 4; i++) {
        int32_t r = l_run((uint32_t)i);
        if (r != want[i]) {
            pm_metal_build_artifact_destroy(&art);
            pm_util_mem_arena_destroy(arena); free(backing);
            pm_util_mem_arena_destroy(oarena); free(obacking);
            return 278 + (int32_t)i;
        }
    }
    pm_metal_build_artifact_destroy(&art);
    pm_util_mem_arena_destroy(arena);
    pm_util_mem_arena_destroy(oarena);
    free(backing);
    free(obacking);
    return 0;
#else
    /* No native TCC object output / no ELF loader on this seat — skip */
    return 0;
#endif
}

static int32_t test_let_else_order_linked(void) {
#if defined(PM_METAL_BUILD_HAS_ELF) && PM_HAS_TCC && !defined(TCC_TARGET_WASM32)
    void *backing = NULL, *obacking = NULL;
    pm_util_mem_arena_t *arena = NULL, *oarena = NULL;
    char *c = NULL;
    size_t c_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    char oerr[256];
    int32_t rc;
    uint8_t *obj = NULL;
    size_t obj_len = 0;
    pm_metal_build_unit_t unit;
    pm_metal_build_artifact_t art;
    uint8_t *objs[1];
    size_t lens[1];
    uint32_t (*l_run)(uint32_t);
    uint32_t r;

    backing = malloc(1u << 24);
    if (!backing) return 220;
    arena = pm_util_mem_arena_create(backing, 1u << 24);
    if (!arena) { free(backing); return 221; }
    memset(err, 0, sizeof(err));
    rc = pm_metal_jit_rsx_compile(arena, LET_ELSE_RT_SRC,
                                  strlen(LET_ELSE_RT_SRC),
                                  &c, &c_len, err, sizeof(err));
    if (rc != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 222;
    }
    /* (a) codegen order: the else-block (which contains the `return`)
     * must appear BEFORE the payload copies in the emitted C. The
     * let-else emits: temp decl, if-test + else-block, then binds. */
    {
        const char *tmp_decl = strstr(c, "rsx_tuple_");
        const char *test;
        const char *ret_else;
        const char *bind_a;
        if (tmp_decl == NULL) {
            pm_util_mem_arena_destroy(arena); free(backing); return 223;
        }
        /* the if-test line: `if (!__rsx_tupN._0._has || ... */
        test = strstr(c, "._has");
        if (test == NULL) {
            pm_util_mem_arena_destroy(arena); free(backing); return 224;
        }
        /* the diverging else return inside the if-block */
        ret_else = strstr(c, "0xDEAD0000");
        if (ret_else == NULL) {
            pm_util_mem_arena_destroy(arena); free(backing); return 225;
        }
        /* the first payload copy: `= __rsx_tupN._0._v;` */
        bind_a = strstr(c, "._v;");
        if (bind_a == NULL) {
            pm_util_mem_arena_destroy(arena); free(backing); return 226;
        }
        /* ORDER: test < ret_else < bind_a — no payload read before the
         * ._has checks gate the else-block */
        if (!(test < ret_else && ret_else < bind_a)) {
            pm_util_mem_arena_destroy(arena); free(backing); return 227;
        }
    }
    /* (b) linked execution: all four paths */
    obacking = malloc(1u << 24);
    if (!obacking) {
        pm_util_mem_arena_destroy(arena); free(backing); return 228;
    }
    oarena = pm_util_mem_arena_create(obacking, 1u << 24);
    if (!oarena) {
        pm_util_mem_arena_destroy(arena); free(backing); free(obacking);
        return 229;
    }
    memset(oerr, 0, sizeof(oerr));
    rc = pm_metal_jit_c_object_compile(oarena, c, c_len, &obj, &obj_len,
                                       oerr, sizeof(oerr));
    if (rc != 0) {
        if (strstr(oerr, "no native object output on this seat") != NULL) {
            pm_util_mem_arena_destroy(arena); free(backing);
            pm_util_mem_arena_destroy(oarena); free(obacking);
            return 0; /* polite seat refusal — skip */
        }
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking); return 230;
    }
    memset(&unit, 0, sizeof(unit));
    snprintf(unit.fqn, sizeof(unit.fqn), "%s", "rsx.letelse.rt");
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
        pm_util_mem_arena_destroy(oarena); free(obacking); return 231;
    }
    l_run = (uint32_t (*)(uint32_t))
        pm_metal_build_artifact_lookup(&art, "le_run");
    if (l_run == NULL) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking); return 232;
    }
    /* Some path: both payloads bound, checks pass */
    r = l_run(3);
    if (r != 0x600D600D) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking); return 233;
    }
    /* None paths: the diverging else ran (never the binds) */
    r = l_run(0);
    if (r != 0xDEAD0000) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking); return 234;
    }
    r = l_run(1);
    if (r != 0xDEAD0001) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking); return 235;
    }
    r = l_run(2);
    if (r != 0xDEAD0002) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking); return 236;
    }
    pm_metal_build_artifact_destroy(&art);
    pm_util_mem_arena_destroy(arena);
    pm_util_mem_arena_destroy(oarena);
    free(backing);
    free(obacking);
    return 0;
#else
    /* No native TCC object output / no ELF loader on this seat — skip */
    return 0;
#endif
}

/* --- #[cfg] stripping: feature predicate evaluation at parse time ------
 * The parser evaluates `#[cfg(feature = "x")]` / `#[cfg(not(...))]` /
 * `#[cfg(all(...))]` / `#[cfg(any(...))]` / `#[cfg(test)]` against the
 * active feature set (the compile entry point passes feats = NULL for
 * the kernel seat: no features on, so feature-gated items strip and
 * their not() complements stay). Proven here: the generated C contains
 * exactly one definition per cfg-false/cfg-true item pair, statement
 * gates strip, and unknown predicates never silently keep code. */

static const char CFG_STRIP_SRC[] =
    "#[cfg(feature = \"on\")]\n"
    "pub fn gated() -> i32 { 1 }\n"
    "#[cfg(not(feature = \"on\"))]\n"
    "pub fn gated() -> i32 { 2 }\n"
    "#[cfg(all(feature = \"on\", feature = \"other\"))]\n"
    "pub fn all_gate() -> i32 { 3 }\n"
    "#[cfg(any(feature = \"on\", not(feature = \"on\")))]\n"
    "pub fn any_gate() -> i32 { 4 }\n"
    "pub extern \"C\" fn stmt_gate(v: i32) -> i32 {\n"
    "    #[cfg(feature = \"on\")]\n"
    "    let extra = 10;\n"
    "    v + 1\n"
    "}\n"
    "#[cfg(test)]\n"
    "pub fn test_only() -> i32 { 5 }\n";

static int32_t test_cfg_strip_parse_and_lower(void) {
    void *backing = malloc(1u << 24);
    pm_util_mem_arena_t *arena;
    char *c = NULL;
    size_t c_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    int gated_bodies;

    if (backing == NULL) return 280;
    arena = pm_util_mem_arena_create(backing, 1u << 24);
    if (arena == NULL) { free(backing); return 281; }
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, CFG_STRIP_SRC,
                                 strlen(CFG_STRIP_SRC),
                                 &c, &c_len, err, sizeof(err)) != 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 282;
    }
    if (c == NULL || c_len == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 283;
    }
    /* exactly one `gated` body: the feature is off (feats == NULL), so
     * the gated definition strips and its not() complement stays. */
    gated_bodies = 0;
    {
        const char *p = c;
        while ((p = strstr(p, "gated")) != NULL) {
            gated_bodies++;
            p++;
        }
    }
    /* one prototype/decl + one body — count must be exactly 2 (a single
     * definition survived; the duplicate stripped) */
    if (gated_bodies != 2) {
        pm_util_mem_arena_destroy(arena); free(backing); return 284;
    }
    /* all(on, other) with no features on is false -> stripped */
    if (rsx_strstr(c, "all_gate")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 285;
    }
    /* any(on, not(on)) is true -> kept */
    if (!rsx_strstr(c, "any_gate")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 286;
    }
    /* the cfg-false statement (let extra = 10) stripped */
    if (rsx_strstr(c, "extra")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 287;
    }
    /* cfg(test) is false on the kernel seat -> stripped */
    if (rsx_strstr(c, "test_only")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 288;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* --- Option/Tuple typedef codec: injective + reversible names -----------
 * The typedef name carries the payload's EXACT canonical C-type bytes as
 * lowercase hex (`rsx_opt_<raw_len>e<2*raw_len hex>`), so:
 *   - distinct payload spellings always render distinct identifiers
 *     (the historical `a-b`/`a_b` sanitization collision is impossible)
 *   - the name decodes back to the exact payload bytes (never the encoded
 *     spelling, never a prefix skip)
 * Proven here by compiling sources whose Option payloads spell out the
 * required cases and asserting the exact names in the generated C. */

static const char OPT_CODEC_SRC[] =
    "#[repr(C)]\n"
    "pub struct Foo__ { pub x: u32 }\n"
    "#[repr(C)]\n"
    "pub struct Row { pub a: u32 }\n"
    "#[no_mangle]\n"
    "pub extern \"C\" fn take_sz(v: Option<usize>) -> usize {\n"
    "    match v { Some(x) => x, None => 0usize }\n"
    "}\n"
    "#[no_mangle]\n"
    "pub extern \"C\" fn take_u32(v: Option<u32>) -> u32 {\n"
    "    match v { Some(x) => x, None => 0u32 }\n"
    "}\n"
    "#[no_mangle]\n"
    "pub extern \"C\" fn take_foo(v: Option<Foo__>) -> u32 {\n"
    "    match v { Some(x) => x.x, None => 0u32 }\n"
    "}\n"
    "#[no_mangle]\n"
    "pub extern \"C\" fn take_arr(v: Option<[u8; 16]>) -> u8 {\n"
    "    match v { Some(x) => x[0], None => 0u8 }\n"
    "}\n"
    "#[no_mangle]\n"
    "pub extern \"C\" fn take_arrptr(v: Option<*mut [Row; 4]>) -> usize {\n"
    "    match v { Some(x) => unsafe { (*x)[0].a as usize }, None => 0usize }\n"
    "}\n";

static int32_t test_opt_codec_names(void) {
    void *backing = malloc(1u << 26);
    pm_util_mem_arena_t *arena;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    /* expected canonical names: raw payload bytes -> lowercase hex
     * size_t       73 69 7a 65 5f 74                      (6)
     * uint32_t     75 69 6e 74 33 32 5f 74               (8)
     * Foo__        46 6f 6f 5f 5f                         (5)
     * uint8_t [16] 75 69 6e 74 38 5f 74 20 5b 31 36 5d  (12)
     * Row (*) [4]  52 6f 77 20 28 2a 29 20 5b 34 5d     (11) */
    static const char *want[5] = {
        "rsx_opt_6e73697a655f74",
        "rsx_opt_8e75696e7433325f74",
        "rsx_opt_5e466f6f5f5f",
        "rsx_opt_12e75696e74385f74205b31365d",
        "rsx_opt_11e526f7720282a29205b345d",
    };
    size_t i;
    size_t n_names = 0;

    if (backing == NULL) return 240;
    arena = pm_util_mem_arena_create(backing, 1u << 26);
    if (arena == NULL) { free(backing); return 241; }

    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, OPT_CODEC_SRC, strlen(OPT_CODEC_SRC),
                                 &c_out, &c_out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "opt codec: %s\n", err);
        pm_util_mem_arena_destroy(arena); free(backing); return 242;
    }
    if (c_out == NULL || c_out_len == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 243;
    }
    /* every expected name appears exactly once (the typedef emit) */
    for (i = 0; i < 5; i++) {
        const char *p = c_out;
        int hits = 0;
        while ((p = strstr(p, want[i])) != NULL) {
            hits++;
            p++;
        }
        if (hits == 0) {
            fprintf(stderr, "opt codec: missing %s\n", want[i]);
            pm_util_mem_arena_destroy(arena); free(backing); return 244;
        }
        n_names += (size_t)hits;
    }
    /* the payload C types appear in the typedef bodies (round trip: the
     * name's bytes and the declared member are the same type) */
    if (!rsx_strstr(c_out, "typedef struct { size_t _v; bool _has; } rsx_opt_6e73697a655f74;")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 245;
    }
    if (!rsx_strstr(c_out, "typedef struct { uint32_t _v; bool _has; } rsx_opt_8e75696e7433325f74;")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 246;
    }
    if (!rsx_strstr(c_out, "typedef struct { Foo__ _v; bool _has; } rsx_opt_5e466f6f5f5f;")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 247;
    }
    if (!rsx_strstr(c_out, "typedef struct { uint8_t [16] _v; bool _has; } rsx_opt_12e75696e74385f74205b31365d;")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 248;
    }
    if (!rsx_strstr(c_out, "typedef struct { Row (*) [4] _v; bool _has; } rsx_opt_11e526f7720282a29205b345d;")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 249;
    }
    /* no legacy sanitized names remain (the old scheme is gone) */
    if (strstr(c_out, "rsx_opt_6esize_t") != NULL
        || strstr(c_out, "rsx_opt_8euint32_t") != NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 250;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* --- Option<size_t> + `?`: the name decodes back to the payload ----------
 * `?` on a struct-Option must recover the exact payload C type from the
 * hex name: `rsx_opt_6e73697a655f74` -> `size_t` (never the encoded
 * spelling `73697a655f74`, never a fixed byte skip). Proven by the let
 * declaring its bind with the decoded type and the `. _v` read. */

static const char OPT_TRY_SRC[] =
    "#[no_mangle]\n"
    "pub extern \"C\" fn pick(v: Option<usize>) -> Option<usize> {\n"
    "    match v { Some(x) => return Some(x), None => return None }\n"
    "}\n"
    "#[no_mangle]\n"
    "pub extern \"C\" fn run(v: Option<usize>) -> Option<usize> {\n"
    "    let n = pick(v)?;\n"
    "    Some(n)\n"
    "}\n";

static int32_t test_opt_try_decodes_payload(void) {
    void *backing = malloc(1u << 26);
    pm_util_mem_arena_t *arena;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];

    if (backing == NULL) return 251;
    arena = pm_util_mem_arena_create(backing, 1u << 26);
    if (arena == NULL) { free(backing); return 252; }

    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, OPT_TRY_SRC, strlen(OPT_TRY_SRC),
                                 &c_out, &c_out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "opt try: %s\n", err);
        pm_util_mem_arena_destroy(arena); free(backing); return 253;
    }
    if (c_out == NULL || c_out_len == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return 254;
    }
    /* the fn's own return type is the canonical name */
    if (!rsx_strstr(c_out, "rsx_opt_6e73697a655f74 run(")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 255;
    }
    /* the `?` unwrap declares its bind as the DECODED payload type */
    if (!rsx_strstr(c_out, "size_t n = ")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 256;
    }
    /* and reads the payload through _v of the canonical Option name */
    if (!rsx_strstr(c_out, "rsx_opt_6e73697a655f74 __rsx_try")) {
        pm_util_mem_arena_destroy(arena); free(backing); return 257;
    }
    /* never the encoded spelling as a type */
    if (strstr(c_out, "73697a655f74 n") != NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 258;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* --- malformed Option names: the decoder refuses cleanly -----------------
 * A `rsx_opt_*` type name that is not a well-formed canonical encoding
 * must be refused with a specific error, never partially decoded. Every
 * malformed shape from the codec review rides an extern-block static
 * whose declared type is the malformed name; the let-else/`?` consumer
 * runs the strict decoder on it. */

static int32_t opt_bad_name_case(const char *bad_type, int32_t code) {
    void *backing = malloc(1u << 24);
    pm_util_mem_arena_t *arena;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    /* extern static with the malformed type + a let-else consumer (the
     * decoder entry) — one source, one refusal expected */
    char src[512];
    int n;

    if (backing == NULL) return code;
    arena = pm_util_mem_arena_create(backing, 1u << 24);
    if (arena == NULL) { free(backing); return code + 1; }
    n = snprintf(src, sizeof(src),
        "extern \"C\" { static BAD: %s; }\n"
        "pub fn probe() -> u32 {\n"
        "    let Some(v) = unsafe { BAD } else { return 0u32; };\n"
        "    v\n"
        "}\n", bad_type);
    if (n <= 0 || (size_t)n >= sizeof(src)) {
        pm_util_mem_arena_destroy(arena); free(backing); return code + 2;
    }
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, src, (size_t)n,
                                 &c_out, &c_out_len, err, sizeof(err)) == 0) {
        /* compiled: the malformed name was accepted — WRONG */
        fprintf(stderr, "opt bad %s: accepted\n", bad_type);
        pm_util_mem_arena_destroy(arena); free(backing); return code + 3;
    }
    /* refusal must carry a message (never a silent empty fail) */
    if (err[0] == 0) {
        pm_util_mem_arena_destroy(arena); free(backing); return code + 4;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

static int32_t test_opt_bad_names_refuse(void) {
    /* missing length: no digits between prefix and 'e' */
    if (opt_bad_name_case("rsx_opt_e466f6f", 260) != 0) return 260;
    /* missing 'e' separator */
    if (opt_bad_name_case("rsx_opt_3466f6f", 261) != 0) return 261;
    /* odd number of hex digits */
    if (opt_bad_name_case("rsx_opt_3e466f6", 262) != 0) return 262;
    /* invalid hex digit (g) */
    if (opt_bad_name_case("rsx_opt_3e466f6g", 263) != 0) return 263;
    /* uppercase hex digit — lowercase is canonical-only */
    if (opt_bad_name_case("rsx_opt_3e466F6f", 264) != 0) return 264;
    /* declared length smaller than payload (4 hex digits for len 3) */
    if (opt_bad_name_case("rsx_opt_2e466f6f", 265) != 0) return 265;
    /* declared length larger than payload (2 hex digits for len 3) */
    if (opt_bad_name_case("rsx_opt_3e466f", 266) != 0) return 266;
    /* decimal length overflow (huge declared length) */
    if (opt_bad_name_case("rsx_opt_99999999999999999999e466f6f", 267) != 0) return 267;
    /* zero-length payload */
    if (opt_bad_name_case("rsx_opt_0e", 268) != 0) return 268;
    /* truncated: length says 3 but nothing follows the 'e' */
    if (opt_bad_name_case("rsx_opt_3e", 269) != 0) return 269;
    return 0;
}

/* --- arena OOM: table spans refuse with a specific error ------------------
 * The FnTab/LocalTab arena spans and the exact-size name_tmp allocations
 * must refuse IMMEDIATELY with a message naming the arena — never compile
 * on with unknown types (which would emit untyped call sites / locals).
 * A tiny arena (large enough to lex+parse, too small for the tables)
 * forces the refusal path. */

static const char OOM_SRC[] =
    "#[repr(C)]\n"
    "pub struct P { pub a: u32, pub b: u32 }\n"
    "#[no_mangle]\n"
    "pub extern \"C\" fn f(p: P) -> u32 { p.a + p.b }\n"
    "#[no_mangle]\n"
    "pub extern \"C\" fn g(x: u32) -> u32 {\n"
    "    let y = x + 1u32;\n"
    "    let y2 = y + 1u32;\n"
    "    let y3 = y2 + 1u32;\n"
    "    y3\n"
    "}\n";

static int32_t oom_arena_case(size_t span, const char *what, int32_t code) {
    void *backing = malloc(span);
    pm_util_mem_arena_t *arena;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];

    if (backing == NULL) return code;
    arena = pm_util_mem_arena_create(backing, span);
    if (arena == NULL) { free(backing); return code + 1; }
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, OOM_SRC, strlen(OOM_SRC),
                                 &c_out, &c_out_len, err, sizeof(err)) != 0) {
        /* refusal is correct — but it must carry a reason */
        if (err[0] == 0) {
            fprintf(stderr, "oom %s (%zu): silent refusal\n", what, span);
            pm_util_mem_arena_destroy(arena); free(backing); return code + 2;
        }
        pm_util_mem_arena_destroy(arena); free(backing);
        return 0;
    }
    /* compiled: with a span this small the tables cannot have fit — the
     * compile lied (compiled on past a refused span). */
    fprintf(stderr, "oom %s (%zu): compiled anyway\n", what, span);
    pm_util_mem_arena_destroy(arena); free(backing);
    return code + 3;
}

static int32_t test_arena_oom_refuses(void) {
    /* spans that hold the source + tokens + AST but starve the tables:
     * every refusal must be specific, none silent. */
    size_t spans[] = { 64u * 1024u, 128u * 1024u, 256u * 1024u, 512u * 1024u };
    size_t i;
    int refused = 0;

    for (i = 0; i < sizeof(spans) / sizeof(spans[0]); i++) {
        int32_t rc = oom_arena_case(spans[i], "table-span", 300 + (int32_t)i * 10);
        if (rc != 0) return rc;
        /* rc == 0 means either refused-cleanly OR compiled — distinguish
         * by re-running and checking which happened. */
        {
            void *backing = malloc(spans[i]);
            pm_util_mem_arena_t *arena;
            char *c_out = NULL;
            size_t c_out_len = 0;
            char err[PM_METAL_JIT_RSX_ERR_MAX];

            if (backing == NULL) return 348;
            arena = pm_util_mem_arena_create(backing, spans[i]);
            if (arena == NULL) { free(backing); return 349; }
            memset(err, 0, sizeof(err));
            if (pm_metal_jit_rsx_compile(arena, OOM_SRC, strlen(OOM_SRC),
                                         &c_out, &c_out_len, err,
                                         sizeof(err)) != 0) {
                refused++;
                if (err[0] == 0) {
                    pm_util_mem_arena_destroy(arena); free(backing); return 350;
                }
            }
            pm_util_mem_arena_destroy(arena);
            free(backing);
        }
    }
    /* at least the two smallest spans must refuse (the SymTab block alone
     * is ~1 MiB); if every span compiled, the OOM path was never exercised */
    if (refused < 2) {
        fprintf(stderr, "arena oom: %d/%zu spans refused — path not exercised\n",
                refused, sizeof(spans) / sizeof(spans[0]));
        return 351;
    }
    return 0;
}

/* --- LocalTab rollback: no arena growth from speculative re-registers ------
 * A local rebound N times in one scope (loop reassignment is ONE entry,
 * but nested if/else re-binding the same name with the same type must not
 * grow the table: the reuse guard returns before allocating). Proven by
 * compiling a source with many shadowed re-binds: the compile must succeed
 * with a modest arena AND the emitted C must reuse the declaration (no
 * duplicate `uint32_t y` lines). */

static const char ROLLBACK_SRC[] =
    "#[no_mangle]\n"
    "pub extern \"C\" fn rb(x: u32) -> u32 {\n"
    "    let y = x + 1u32;\n"
    "    let y = y + 1u32;\n"
    "    let y = y + 1u32;\n"
    "    let y = y + 1u32;\n"
    "    let y = y + 1u32;\n"
    "    let y = y + 1u32;\n"
    "    let y = y + 1u32;\n"
    "    let y = y + 1u32;\n"
    "    y\n"
    "}\n";

static int32_t test_localtab_reuse_no_growth(void) {
    void *backing = malloc(1u << 24);
    pm_util_mem_arena_t *arena;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    const char *p;
    int decls = 0;

    if (backing == NULL) return 360;
    arena = pm_util_mem_arena_create(backing, 1u << 24);
    if (arena == NULL) { free(backing); return 361; }
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, ROLLBACK_SRC, strlen(ROLLBACK_SRC),
                                 &c_out, &c_out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "rollback: %s\n", err);
        pm_util_mem_arena_destroy(arena); free(backing); return 362;
    }
    /* the same-scope same-type re-binds reuse: exactly ONE `uint32_t y`
     * declaration, the rest are plain assignments. */
    p = c_out;
    while ((p = strstr(p, "uint32_t y")) != NULL) {
        decls++;
        p++;
    }
    if (decls != 1) {
        fprintf(stderr, "rollback: %d declarations of y (want 1)\n", decls);
        pm_util_mem_arena_destroy(arena); free(backing); return 363;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* --- name_tmp capacity boundary -------------------------------------------
 * A tuple signature whose encoded name is LONGER than the 160-byte
 * arena_tmp scratch must still compile (the exact-size name_tmp
 * allocation), and one whose name exceeds the 1024-byte ceiling must
 * refuse with a message (never truncate). TUP_MAXF caps fields at 4, so
 * the length is driven by long element spellings; 63-byte element types
 * are the per-field cap (elem lens are [u8;64]). */

static const char LONG_TUPLE_SRC[] =
    "#[repr(C)]\n"
    "pub struct Row { pub a: u32, pub b: u32, pub c: u32, pub d: u32 }\n"
    "#[repr(C)]\n"
    "pub struct RowA { pub a: Row, pub b: Row, pub c: Row, pub d: Row }\n"
    "#[repr(C)]\n"
    "pub struct RowB { pub a: RowA, pub b: RowA, pub c: RowA, pub d: RowA }\n"
    "#[no_mangle]\n"
    "pub extern \"C\" fn lt(p: *mut RowB) -> (RowB, RowB, RowB, RowB) {\n"
    "    ((*p), (*p), (*p), (*p))\n"
    "}\n"
    "#[no_mangle]\n"
    "pub extern \"C\" fn lt2(p: *mut RowB, q: u32) -> u32 { q }\n";

static int32_t test_name_tmp_capacity(void) {
    void *backing = malloc(1u << 24);
    pm_util_mem_arena_t *arena;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    int found_long = 0;
    const char *p;

    if (backing == NULL) return 370;
    arena = pm_util_mem_arena_create(backing, 1u << 24);
    if (arena == NULL) { free(backing); return 371; }
    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, LONG_TUPLE_SRC, strlen(LONG_TUPLE_SRC),
                                 &c_out, &c_out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "name_tmp: %s\n", err);
        pm_util_mem_arena_destroy(arena); free(backing); return 372;
    }
    /* the 4x RowB tuple name is ~4*(4+1+2*4)= way past 160 bytes: it must
     * still be present whole (exact-size allocation, not truncation). */
    p = c_out;
    while ((p = strstr(p, "rsx_tuple_4_")) != NULL) {
        found_long = 1;
        /* the name must be terminated cleanly (a full identifier, then the
         * struct body follows) */
        if (strstr(p, ";") == NULL) {
            pm_util_mem_arena_destroy(arena); free(backing); return 373;
        }
        break;
    }
    if (!found_long) {
        fprintf(stderr, "name_tmp: no 4-field tuple name emitted\n");
        pm_util_mem_arena_destroy(arena); free(backing); return 374;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}

/* --- registration ------------------------------------------------------ */

/* --- stack independence: many bodies + deep nesting -----------------------
 * Phase 1: Lower (26 KiB), FnTab (84 KiB) and every per-body LocalTab
 * (23 KiB) are arena-resident — a compile's native frames are the small
 * emit/parse locals only. A unit with MANY fn bodies (each body a fresh
 * arena LocalTab) and DEEPLY nested expressions (the recursive parse and
 * emit path) must compile in a normal-size thread stack. The old by-value
 * Lower parked 110+ KiB on the stack at this exact entry; a tight thread
 * (default 8 MiB here, but the shape matters for firmware stacks) would
 * have burned most of it on one compile. */
static int32_t test_stack_independence(void) {
    /* 40 fns, each with nested if/else and a chain of lets — the body
     * count multiplies LocalTab allocations, the nesting multiplies
     * recursion depth. The body is authored once; each f<i> renders the
     * name into the template by hand (no format string crossing — GCC
     * checks every snprintf against its literal). */
    static const char HEAD[] =
        "#[repr(C)]\n"
        "pub struct Ctx { pub a: u32, pub b: u32, pub c: u32 }\n";
    static const char FN[] =
        "#[no_mangle]\n"
        "pub extern \"C\" fn f0000(x: u32) -> u32 {\n"
        "    let a = x + 1u32;\n"
        "    let b = a + 2u32;\n"
        "    let c = if (b % 2u32) == 0u32 {\n"
        "        if (b % 3u32) == 0u32 { b + 4u32 } else { b + 5u32 }\n"
        "    } else {\n"
        "        if (b % 5u32) == 0u32 { b + 6u32 } else { b + 7u32 }\n"
        "    };\n"
        "    let d = (c, c + 1u32, c + 2u32, c + 3u32);\n"
        "    d.0 + d.1 + d.2 + d.3\n"
        "}\n";
    char src[16384];
    size_t at = 0;
    unsigned i;
    void *backing = malloc(1u << 25);
    pm_util_mem_arena_t *arena;
    char *c_out = NULL;
    size_t c_out_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    const char *p;

    if (backing == NULL) return 380;
    arena = pm_util_mem_arena_create(backing, 1u << 25);
    if (arena == NULL) { free(backing); return 381; }

    memcpy(src + at, HEAD, sizeof(HEAD) - 1);
    at += sizeof(HEAD) - 1;
    for (i = 0; i < 40; i++) {
        /* render fNNNN into a fixed 5-byte name slot */
        size_t fn_len = sizeof(FN) - 1;
        if (at + fn_len + 1 >= sizeof(src)) {
            pm_util_mem_arena_destroy(arena); free(backing); return 382;
        }
        memcpy(src + at, FN, fn_len);
        src[at + 32] = (char)('0' + ((i / 1000) % 10));
        src[at + 33] = (char)('0' + ((i / 100) % 10));
        src[at + 34] = (char)('0' + ((i / 10) % 10));
        src[at + 35] = (char)('0' + (i % 10));
        at += fn_len;
    }

    memset(err, 0, sizeof(err));
    if (pm_metal_jit_rsx_compile(arena, src, at,
                                 &c_out, &c_out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "stack-independence: %s\n", err);
        pm_util_mem_arena_destroy(arena); free(backing); return 383;
    }
    /* every body lowered: 40 distinct fNNNN definitions present */
    for (i = 0; i < 40; i++) {
        char name[8];
        snprintf(name, sizeof(name), "f%04u", i);
        if (strstr(c_out, name) == NULL) {
            fprintf(stderr, "stack-independence: %s missing\n", name);
            pm_util_mem_arena_destroy(arena); free(backing); return 384;
        }
    }
    /* tuple typedef emitted (the bodies construct 4-tuples) */
    p = strstr(c_out, "rsx_tuple_");
    if (p == NULL) {
        fprintf(stderr, "stack-independence: no tuple typedef\n");
        pm_util_mem_arena_destroy(arena); free(backing); return 385;
    }
    pm_util_mem_arena_destroy(arena);
    free(backing);
    return 0;
}


/* --- trait / dyn plane: parse, generated-C shape, linked dispatch --- */

static const char TRAIT_RT_SRC[] =
    "pub struct Counter { pub n: u32 }\n"
    "pub trait Sink { fn put(&mut self, x: u32); fn get(&self) -> u32; }\n"
    "impl Sink for Counter {\n"
    "    fn put(&mut self, x: u32) { self.n += x; }\n"
    "    fn get(&self) -> u32 { self.n }\n"
    "}\n"
    "pub fn drive(s: &mut dyn Sink, v: u32) { s.put(v); }\n"
    "#[no_mangle]\n"
    "pub fn rsx_trait_rt_main() -> u32 {\n"
    "    let mut c = Counter { n: 0 };\n"
    "    drive(&mut c, 7);\n"
    "    c.n\n"
    "}\n";

static int32_t test_trait_runtime_linked(void) {
#if defined(PM_METAL_BUILD_HAS_ELF) && PM_HAS_TCC && !defined(TCC_TARGET_WASM32)
    void *backing = NULL, *obacking = NULL;
    pm_util_mem_arena_t *arena = NULL, *oarena = NULL;
    char *c = NULL;
    size_t c_len = 0;
    char err[PM_METAL_JIT_RSX_ERR_MAX];
    char oerr[256];
    int32_t rc;
    uint8_t *obj = NULL;
    size_t obj_len = 0;
    pm_metal_build_unit_t unit;
    pm_metal_build_artifact_t art;
    uint8_t *objs[1];
    size_t lens[1];
    uint32_t (*l_main)(void);
    uint32_t r;

    backing = malloc(1u << 24);
    if (!backing) return 240;
    arena = pm_util_mem_arena_create(backing, 1u << 24);
    if (!arena) { free(backing); return 241; }
    memset(err, 0, sizeof(err));
    c = NULL; c_len = 0;
    rc = pm_metal_jit_rsx_compile(arena, TRAIT_RT_SRC,
                                  strlen(TRAIT_RT_SRC),
                                  &c, &c_len, err, sizeof(err));
    if (rc != 0) {
        fprintf(stderr, "trait_rt: compile failed: %s\n", err);
        pm_util_mem_arena_destroy(arena); free(backing); return 242;
    }
    /* (1) the object typedef carries the vtable shape: a `_self` data
     * field plus one fn-ptr per trait method. */
    if (strstr(c, "void *_self;") == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 243;
    }
    if (strstr(c, "void (*put)(void *_self, uint32_t x);") == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 244;
    }
    if (strstr(c, "uint32_t (*get)(void *_self);") == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 245;
    }
    /* (2) the impl methods mangle Type_Trait_method with the SELF-typed
     * receiver (the pre-dyn-plane inversion bug: Trait_Type) */
    if (strstr(c, "Counter_Sink_put(Counter * self") == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 246;
    }
    /* (3) the callsite dispatches through the object: `s->put(s->_self, ..)` */
    if (strstr(c, "s->put(s->_self, v)") == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 247;
    }
    /* (4) the coercion materializes the object with the impl's fns */
    if (strstr(c, "._self = (&c)") == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 248;
    }
    /* (4b) the fn-ptr cast carries the slot's full fn-ptr type (params
     * named, name spliced out): `ret (*)(void *_self, T p)`. */
    if (strstr(c, ".put = (void (*)(void *_self, uint32_t x))Counter_Sink_put") == NULL) {
        pm_util_mem_arena_destroy(arena); free(backing); return 249;
    }

    /* linked execution: the dispatch must actually run through the
     * vtable slot and land in the impl method */
    obacking = malloc(1u << 24);
    if (!obacking) {
        pm_util_mem_arena_destroy(arena); free(backing); return 250;
    }
    oarena = pm_util_mem_arena_create(obacking, 1u << 24);
    if (!oarena) {
        pm_util_mem_arena_destroy(arena); free(backing); free(obacking);
        return 251;
    }
    memset(oerr, 0, sizeof(oerr));
    rc = pm_metal_jit_c_object_compile(oarena, c, c_len, &obj, &obj_len,
                                       oerr, sizeof(oerr));
    if (rc != 0) {
        if (strstr(oerr, "no native object output on this seat") != NULL) {
            pm_util_mem_arena_destroy(arena); free(backing);
            pm_util_mem_arena_destroy(oarena); free(obacking);
            return 0; /* polite seat refusal — skip */
        }
        fprintf(stderr, "trait_rt: object compile failed: %s\n", oerr);
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking); return 252;
    }
    memset(&unit, 0, sizeof(unit));
    snprintf(unit.fqn, sizeof(unit.fqn), "%s", "rsx.trait.rt");
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
        pm_util_mem_arena_destroy(oarena); free(obacking); return 253;
    }
    l_main = (uint32_t (*)(void))
        pm_metal_build_artifact_lookup(&art, "rsx_trait_rt_main");
    if (l_main == NULL) {
        pm_metal_build_artifact_destroy(&art);
        pm_util_mem_arena_destroy(arena); free(backing);
        pm_util_mem_arena_destroy(oarena); free(obacking); return 254;
    }
    r = l_main();
    pm_metal_build_artifact_destroy(&art);
    pm_util_mem_arena_destroy(arena); free(backing);
    pm_util_mem_arena_destroy(oarena); free(obacking);
    if (r != 7) {
        fprintf(stderr, "trait_rt: expected 7, got %u\n", r);
        return 255;
    }
    return 0;
#else
    return 0; /* seats without the in-process TCC/ELF fills prove the
               * parser and shape assertions elsewhere (rsx_dump); the
               * linked loop is the host seat's fill */
#endif
}

/* RSX_TEST_VERBOSE=1 prints one line per subtest with its rc, so a FAIL from
 * the registry entry can be attributed without re-running under a debugger. */
static int32_t rsx_run_named(const char *name, int32_t (*fn)(void)) {
    int32_t rc = fn();
    if (getenv("RSX_TEST_VERBOSE") != NULL) {
        fprintf(stderr, "rsx subtest %-28s rc=%d\n", name, rc);
    }
    return rc;
}

static int32_t pm_metal_jit_rsx_tests(void) {
    int32_t rc;
    rc = rsx_run_named("lex_minimal", test_lex_minimal);           if (rc) return rc;
    rc = rsx_run_named("lex_macro", test_lex_macro);               if (rc) return rc;
    rc = rsx_run_named("lex_errors", test_lex_errors);             if (rc) return rc;
    rc = rsx_run_named("parse_minimal", test_parse_minimal);       if (rc) return rc;
    rc = rsx_run_named("parse_struct", test_parse_struct);         if (rc) return rc;
    rc = rsx_run_named("parse_fn", test_parse_fn);                 if (rc) return rc;
    rc = rsx_run_named("parse_unsupported", test_parse_unsupported); if (rc) return rc;
    rc = rsx_run_named("compile_mod_item", test_compile_mod_item); if (rc) return rc;
    rc = rsx_run_named("compile_match_patterns", test_compile_match_patterns); if (rc) return rc;
    rc = rsx_run_named("compile_try_and_range_index", test_compile_try_and_range_index); if (rc) return rc;
    rc = rsx_run_named("compile_labeled_loops", test_compile_labeled_loops); if (rc) return rc;
    rc = rsx_run_named("compile_union_item", test_compile_union_item); if (rc) return rc;
    rc = rsx_run_named("parse_nested_generics", test_parse_nested_generics); if (rc) return rc;
    rc = rsx_run_named("compile_unsafe_impl_marker", test_compile_unsafe_impl_marker); if (rc) return rc;
    rc = rsx_run_named("compile_minimal_fn", test_compile_minimal_fn); if (rc) return rc;
    rc = rsx_run_named("compile_struct", test_compile_struct);     if (rc) return rc;
    rc = rsx_run_named("compile_zero_static_elision", test_compile_zero_static_elision); if (rc) return rc;
    rc = rsx_run_named("compile_nonzero_static_and_iflet_return", test_compile_nonzero_static_and_iflet_return); if (rc) return rc;
    rc = rsx_run_named("compile_fn_body", test_compile_fn_body);   if (rc) return rc;
    rc = rsx_run_named("compile_provenance", test_compile_provenance); if (rc) return rc;
    rc = rsx_run_named("ast_dump", test_ast_dump);                 if (rc) return rc;
    rc = rsx_run_named("self_host", test_self_host);               if (rc) return rc;
    rc = rsx_run_named("self_host_object", test_self_host_object); if (rc) return rc;
    rc = rsx_run_named("self_host_link", test_self_host_link);    if (rc) return rc;
    rc = rsx_run_named("atomic_runtime_linked", test_atomic_runtime_linked); if (rc) return rc;
    rc = rsx_run_named("tuple_collision", test_tuple_collision);  if (rc) return rc;
    rc = rsx_run_named("opt_codec_names", test_opt_codec_names); if (rc) return rc;
    rc = rsx_run_named("opt_try_decodes_payload", test_opt_try_decodes_payload); if (rc) return rc;
    rc = rsx_run_named("opt_bad_names_refuse", test_opt_bad_names_refuse); if (rc) return rc;
    rc = rsx_run_named("arena_oom_refuses", test_arena_oom_refuses); if (rc) return rc;
    rc = rsx_run_named("localtab_reuse_no_growth", test_localtab_reuse_no_growth); if (rc) return rc;
    rc = rsx_run_named("name_tmp_capacity", test_name_tmp_capacity); if (rc) return rc;
    rc = rsx_run_named("stack_independence", test_stack_independence); if (rc) return rc;
    rc = rsx_run_named("let_else_order_linked", test_let_else_order_linked); if (rc) return rc;
    rc = rsx_run_named("let_chain_parse_and_lower", test_let_chain_parse_and_lower); if (rc) return rc;
    rc = rsx_run_named("let_chain_refuses", test_let_chain_refuses); if (rc) return rc;
    rc = rsx_run_named("let_chain_linked", test_let_chain_linked); if (rc) return rc;
    rc = rsx_run_named("trait_runtime_linked", test_trait_runtime_linked); if (rc) return rc;
    rc = rsx_run_named("cfg_strip_parse_and_lower", test_cfg_strip_parse_and_lower); if (rc) return rc;
    rc = rsx_run_named("introspection", test_introspection);      if (rc) return rc;
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.jit.rs.compiler, tests, pm_metal_jit_rsx_tests);
