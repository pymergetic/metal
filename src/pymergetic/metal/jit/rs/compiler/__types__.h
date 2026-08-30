/* pymergetic.metal.jit.rs.compiler — micro-rustc, the Rust→C pipeline in Rust.
 *
 * Phase 7 of the self-hosting plan. The vendored mrustc (jit.rs card) is a
 * C++ program linked whole; this card is the same pipeline — lex, parse,
 * lower — written in the kernel's own Rust subset so the Rust loop closes
 * without C++ in the tree.
 *
 * Scope is the kernel's own Rust surface (see __pmm__.toml notes): no_std
 * cards with extern "C" blocks, #[repr(C)] structs, enums, statics, const
 * fns, slices, Option<extern "C" fn ..>, and the PM_MOD_EXPORT_RS! ctor
 * tail. Anything outside that subset is refused with "rsx: unsupported:
 * <construct> at line N" — never a silent miscompile.
 *
 * The card is one defining lang (impl = rs): __impl__.rs is the muscle, the
 * C faces below are the ABI the registry sees.
 */
#ifndef PYMERGETIC_METAL_JIT_RS_COMPILER_TYPES_H
#define PYMERGETIC_METAL_JIT_RS_COMPILER_TYPES_H

#include "pymergetic/util/mem/__types__.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Diagnostics carry a line number and a message; the message never moves
 * (arena-owned) so callers can hold it across steps of an async compile. */
#define PM_METAL_JIT_RSX_ERR_MAX 256u

/* ---- lexer ---- */

/* X-macro token table — one definition, every consumer includes it.
 * Kind names mirror the cppx card's table shape so tooling reads both. */
#define PM_JIT_RSX_TOKENS(X) \
    X(END) \
    X(IDENT) \
    X(INT_LITERAL) \
    X(FLOAT_LITERAL) \
    X(CHAR_LITERAL) \
    X(STRING_LITERAL) \
    X(BYTE_STR_LITERAL)  /* b"..."/b'c' */ \
    X(LIFETIME)          /* 'a */ \
    X(ARROW)             /* -> */ \
    X(FAT_ARROW)         /* => */ \
    X(DOUBLE_COLON)      /* :: */ \
    X(DOT)               /* . (incl. 1. . 2; float formed in lexer) */ \
    X(RANGE)             /* .. / ..= */ \
    X(SHL)               /* << */ \
    X(SHR)               /* >> */ \
    X(LE)                /* <= */ \
    X(GE)                /* >= */ \
    X(EQ)                /* == */ \
    X(NE)                /* != */ \
    X(ANDAND)            /* && */ \
    X(OROR)              /* || */ \
    X(PLUSEQ)            /* += */ \
    X(MINUSEQ)           /* -= */ \
    X(STAREQ)            /* *= */ \
    X(SLASHEQ)           /* /= */ \
    X(PERCENTEQ)         /* %= */ \
    X(CARETEQ)           /* ^= */ \
    X(AMPEQ)             /* &= */ \
    X(OREQ)              /* |= */ \
    X(SHLEQ)             /* <<= */ \
    X(SHREQ)             /* >>= */ \
    X(MACRO_INVOC)       /* name!(...) — captures whole invocation text */ \
    X(PUNCT)             /* single-char punctuation */ \
    X(ERROR)

typedef enum {
#define PM_JIT_RSX_TOK_ENUM(name) PM_JIT_RSX_TOK_##name,
    PM_JIT_RSX_TOKENS(PM_JIT_RSX_TOK_ENUM)
#undef PM_JIT_RSX_TOK_ENUM
} pm_jit_rsx_tok_kind;

typedef struct pm_jit_rsx_token {
    pm_jit_rsx_tok_kind kind;
    uint32_t line;        /* 1-based source line */
    const char *text;     /* arena-owned */
    size_t text_len;
} pm_jit_rsx_token_t;

typedef struct pm_jit_rsx_toklist {
    pm_jit_rsx_token_t *toks;
    uint32_t n_toks;
} pm_jit_rsx_toklist_t;

/* Lex one Rust translation unit. Tokens/text live in arena.
 * :return: 0 with toklist filled; -1 with errbuf set.
 * :example:
 * pm_jit_rsx_toklist_t toks;
 * if (pm_metal_jit_rsx_lex(arena, src, len, &toks, err, sizeof(err)) != 0)
 *     return -1;
 */
int32_t pm_metal_jit_rsx_lex(pm_util_mem_arena_t *arena,
    const char *src, size_t src_len,
    pm_jit_rsx_toklist_t *toklist, char *errbuf, size_t errbuf_len);

/* ---- parser (AST) ---- */

/* Arena-owned tree; no destructor — free the arena and the tree is gone.
 * A generic kid-slab node mirrors cppx's shape: tooling that walks one
 * walks the other. */
typedef enum {
    PM_JIT_RSX_AST_FILE,
    PM_JIT_RSX_AST_USE,
    PM_JIT_RSX_AST_FN,
    PM_JIT_RSX_AST_STRUCT,
    PM_JIT_RSX_AST_ENUM,
    PM_JIT_RSX_AST_IMPL,
    PM_JIT_RSX_AST_EXTERN_BLOCK,
    PM_JIT_RSX_AST_STATIC,
    PM_JIT_RSX_AST_CONST,
    PM_JIT_RSX_AST_TYPE_ALIAS,
    PM_JIT_RSX_AST_TRAIT,
    PM_JIT_RSX_AST_MODULE,
    PM_JIT_RSX_AST_ATTR,          /* #[...] — attached to next item */
    PM_JIT_RSX_AST_BLOCK,
    PM_JIT_RSX_AST_STMT,
    PM_JIT_RSX_AST_LET,
    PM_JIT_RSX_AST_IF,
    PM_JIT_RSX_AST_MATCH,
    PM_JIT_RSX_AST_MATCH_ARM,
    PM_JIT_RSX_AST_LOOP,
    PM_JIT_RSX_AST_WHILE,
    PM_JIT_RSX_AST_FOR,
    PM_JIT_RSX_AST_RETURN,
    PM_JIT_RSX_AST_BREAK,
    PM_JIT_RSX_AST_CONTINUE,
    PM_JIT_RSX_AST_EXPR_STMT,
    PM_JIT_RSX_AST_ASSIGN,
    PM_JIT_RSX_AST_BINARY,
    PM_JIT_RSX_AST_UNARY,
    PM_JIT_RSX_AST_CALL,
    PM_JIT_RSX_AST_METHOD_CALL,
    PM_JIT_RSX_AST_FIELD,
    PM_JIT_RSX_AST_PATH,
    PM_JIT_RSX_AST_LITERAL,
    PM_JIT_RSX_AST_TUPLE,
    PM_JIT_RSX_AST_STRUCT_LIT,
    PM_JIT_RSX_AST_CLOSURE,
    PM_JIT_RSX_AST_INDEX,
    PM_JIT_RSX_AST_CAST,
    PM_JIT_RSX_AST_MACRO,
    PM_JIT_RSX_AST_PAREN,
    PM_JIT_RSX_AST_TYPE,
    PM_JIT_RSX_AST_PARAM,
    PM_JIT_RSX_AST_STRUCT_FIELD,
    PM_JIT_RSX_AST_ENUM_VARIANT,
    PM_JIT_RSX_AST_GENERIC,
    PM_JIT_RSX_AST_WHERE,
    PM_JIT_RSX_AST_ARRAY,
} pm_jit_rsx_ast_kind;

typedef struct pm_jit_rsx_ast pm_jit_rsx_ast_t;

struct pm_jit_rsx_ast {
    pm_jit_rsx_ast_kind kind;
    uint32_t line;
    const char *text;      /* name / literal / path text (arena) */
    size_t text_len;
    pm_jit_rsx_ast_t **kids;
    uint32_t n_kids;
    /* leaf payloads */
    int64_t int_val;
    pm_jit_rsx_tok_kind op_kind;
};

/* Parse tokens to an AST. Refuses out-of-subset constructs with
 * "rsx: unsupported: <construct> at line N".
 *
 * :return: 0 with *unit_out set; -1 with errbuf set.
 */
int32_t pm_metal_jit_rsx_parse(pm_util_mem_arena_t *arena,
    const pm_jit_rsx_toklist_t *toks,
    pm_jit_rsx_ast_t **unit_out, char *errbuf, size_t errbuf_len);

/* Render the AST as indented text (debug/inspect face).
 * :return: bytes written, or -1 with errbuf set when the buffer is short.
 */
int32_t pm_metal_jit_rsx_ast_dump(const pm_jit_rsx_ast_t *ast,
    char *out, size_t out_cap, char *errbuf, size_t errbuf_len);

/* ---- lowering (AST -> C) ---- */

/* Lower an AST to C. Output is NUL-terminated, arena-owned via *c_out.
 * Every generated line that maps to authored source carries a #line
 * directive (the provenance chain: /src/<fqn> pane stays primary).
 *
 * :param unit: tree from pm_metal_jit_rsx_parse
 * :param c_out: out, arena pointer to generated C
 * :param c_out_len: out, byte length (excluding NUL)
 * :return: 0 on success, -1 with errbuf set on unsupported construct
 */
int32_t pm_metal_jit_rsx_lower(pm_util_mem_arena_t *arena,
    const pm_jit_rsx_ast_t *unit,
    char **c_out, size_t *c_out_len, char *errbuf, size_t errbuf_len);

/* One-shot: Rust source to generated C (lex+parse+lower).
 * The prove path — also the face the build card's Rust unit compiles with.
 *
 * :param source: Rust source bytes (NUL not required)
 * :param source_len: byte length
 * :param c_out: out, arena pointer to generated C
 * :param c_out_len: out, byte length (excluding NUL)
 * :return: 0 on success, -1 with errbuf set
 * :example:
 * char *c; size_t c_len;
 * if (pm_metal_jit_rsx_compile(arena, src, len, &c, &c_len, err, sizeof(err)) != 0)
 *     return -1;
 */
int32_t pm_metal_jit_rsx_compile(pm_util_mem_arena_t *arena,
    const char *source, size_t source_len,
    char **c_out, size_t *c_out_len, char *errbuf, size_t errbuf_len);

/* Number of supported tokens/AST kinds (introspection for the prove). */
uint32_t pm_metal_jit_rsx_token_kind_count(void);
uint32_t pm_metal_jit_rsx_ast_kind_count(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_JIT_RS_COMPILER_TYPES_H */
