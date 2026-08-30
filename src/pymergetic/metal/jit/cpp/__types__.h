/* pymergetic.metal.jit.cpp — C++14 front-end card: lexer + parser.
 *
 * Phase 6 scope is the syntactic layer only: pm_metal_jit_cpp_lex tokenizes
 * C++14, pm_metal_jit_cpp_parse builds an AST, pm_metal_jit_cpp_ast_dump
 * renders it. Semantic analysis and C lowering are NOT here — the parser
 * accepts what the grammar accepts and rejects what it cannot parse with
 * "cppx: unsupported: <construct> at line N", never a silent misparse.
 */
#ifndef PYMERGETIC_METAL_JIT_CPP_TYPES_H
#define PYMERGETIC_METAL_JIT_CPP_TYPES_H

#include "pymergetic/metal/async/__types__.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* X-macro token kind table — one definition, every consumer (enum, dumper
 * names, keyword table) includes it. Mirrors mrustc's eTokenType.enum.h. */
#define PM_JIT_CPP_TOKENS(X) \
    X(END) \
    X(IDENT) \
    X(KEYWORD) \
    X(INT_LITERAL) \
    X(FLOAT_LITERAL) \
    X(CHAR_LITERAL) \
    X(STRING_LITERAL) \
    X(BOOL_LITERAL) \
    X(PTR_OP)          /* -> */ \
    X(INC_OP)          /* ++ */ \
    X(DEC_OP)          /* -- */ \
    X(LEFT_OP)         /* << */ \
    X(RIGHT_OP)        /* >> */ \
    X(LE_OP)           /* <= */ \
    X(GE_OP)           /* >= */ \
    X(EQ_OP)           /* == */ \
    X(NE_OP)           /* != */ \
    X(AND_OP)          /* && */ \
    X(OR_OP)           /* || */ \
    X(ADD_ASSIGN)      /* += */ \
    X(SUB_ASSIGN)      /* -= */ \
    X(MUL_ASSIGN)      /* *= */ \
    X(DIV_ASSIGN)      /* /= */ \
    X(MOD_ASSIGN)      /* %= */ \
    X(XOR_ASSIGN)      /* ^= */ \
    X(AND_ASSIGN)      /* &= */ \
    X(OR_ASSIGN)       /* |= */ \
    X(SHIFT_LEFT_ASSIGN)  /* <<= */ \
    X(SHIFT_RIGHT_ASSIGN) /* >>= */ \
    X(DOUBLE_COLON)    /* :: */ \
    X(DOT_STAR)        /* .* */ \
    X(ARROW_STAR)      /* ->* */ \
    X(ELLIPSIS)        /* ... */ \
    X(SCOPE)           /* (kept for symmetry; :: is DOUBLE_COLON) */ \
    X(PUNCT)           /* single-char punctuation */ \
    X(PP_DIRECTIVE)    /* preprocessor line, verbatim (starts with '#') */ \
    X(ERROR)

typedef enum {
#define PM_JIT_CPP_TOK_ENUM(name) PM_JIT_CPP_TOK_##name,
    PM_JIT_CPP_TOKENS(PM_JIT_CPP_TOK_ENUM)
#undef PM_JIT_CPP_TOK_ENUM
} pm_jit_cpp_tok_kind;

typedef struct pm_jit_cpp_token {
    pm_jit_cpp_tok_kind kind;
    uint32_t line;        /* 1-based source line */
    const char *text;     /* arena-owned; literal text or identifier */
    size_t text_len;
} pm_jit_cpp_token_t;

typedef struct pm_jit_cpp_toklist {
    pm_jit_cpp_token_t *toks;
    uint32_t n_toks;
} pm_jit_cpp_toklist_t;

/* Lex one C++14 translation unit. Tokens and their text live in arena.
 * Returns 0 with toklist filled; -1 with errbuf set on error. */
int32_t pm_metal_jit_cpp_lex(pm_util_mem_arena_t *arena,
    const char *src, size_t src_len,
    pm_jit_cpp_toklist_t *toklist, char *errbuf, size_t errbuf_len);

/* AST — arena-owned, no destructor: free the arena and the tree is gone. */
typedef enum {
    PM_JIT_CPP_AST_TRANSLATION_UNIT,
    PM_JIT_CPP_AST_FUNCTION,
    PM_JIT_CPP_AST_CLASS,
    PM_JIT_CPP_AST_VAR,
    PM_JIT_CPP_AST_USING,
    PM_JIT_CPP_AST_TYPE,          /* named type used in a signature */
    PM_JIT_CPP_AST_PARAM,
    PM_JIT_CPP_AST_RETURN,
    PM_JIT_CPP_AST_IF,
    PM_JIT_CPP_AST_FOR,
    PM_JIT_CPP_AST_WHILE,
    PM_JIT_CPP_AST_RETURN_STMT,
    PM_JIT_CPP_AST_EXPR_STMT,
    PM_JIT_CPP_AST_DECL_STMT,
    PM_JIT_CPP_AST_COMPOUND,
    PM_JIT_CPP_AST_CALL,
    PM_JIT_CPP_AST_BINARY,
    PM_JIT_CPP_AST_UNARY,
    PM_JIT_CPP_AST_LITERAL,
    PM_JIT_CPP_AST_NAME,
    PM_JIT_CPP_AST_INIT_DECL,     /* T name = init; folded into VAR? no: stmt */
    PM_JIT_CPP_AST_MEMBER,        /* expr.name / expr->name */
    PM_JIT_CPP_AST_TEMPLATE_DECL,
    PM_JIT_CPP_AST_TEMPLATE_REF,
    PM_JIT_CPP_AST_VIRTUAL_METHOD,
    PM_JIT_CPP_AST_ACCESS_SPEC,
    PM_JIT_CPP_AST_NEW_EXPR,
    PM_JIT_CPP_AST_DELETE_EXPR,
    PM_JIT_CPP_AST_REF_QUALIFIER,
    PM_JIT_CPP_AST_SWITCH,         /* switch (expr) { case/default ... } */
    PM_JIT_CPP_AST_CASE,           /* case EXPR: stmt-list (label) */
    PM_JIT_CPP_AST_DEFAULT,        /* default: stmt-list (label) */
    PM_JIT_CPP_AST_GOTO,           /* goto label; */
    PM_JIT_CPP_AST_LABEL,          /* label: stmt */
    PM_JIT_CPP_AST_TYPEDEF,        /* typedef struct {...} Name; — verbatim */
    PM_JIT_CPP_AST_PP,             /* preprocessor directive, verbatim */
    PM_JIT_CPP_AST_CAST,           /* (type)expr — C cast */
    PM_JIT_CPP_AST_COMMA,          /* comma-separated exprs (array inits) */
    PM_JIT_CPP_AST_DECL_GROUP,     /* int a, b; — inline decl list, no scope */
} pm_jit_cpp_ast_kind;

typedef struct pm_jit_cpp_ast pm_jit_cpp_ast_t;

struct pm_jit_cpp_ast {
    pm_jit_cpp_ast_kind kind;
    uint32_t line;
    const char *text;      /* name / operator / literal text (arena) */
    size_t text_len;
    pm_jit_cpp_ast_t **kids;
    uint32_t n_kids;
    /* leaf payloads used by specific kinds */
    int64_t int_val;       /* INT_LITERAL / BOOL_LITERAL */
    pm_jit_cpp_tok_kind op_kind; /* BINARY/UNARY: operator token kind */
};

/* Parse one C++14 translation unit (top-level declarations). Semantic
 * analysis is out of scope: constructs the grammar cannot represent parse to
 * an error node or fail with "cppx: unsupported: <construct> at line N". */
int32_t pm_metal_jit_cpp_parse(pm_util_mem_arena_t *arena,
    const pm_jit_cpp_toklist_t *toks,
    pm_jit_cpp_ast_t **unit_out, char *errbuf, size_t errbuf_len);

/* Render the AST as indented text into out (NUL-terminated). Returns the
 * number of bytes written, or -1 with errbuf set when the buffer is short. */
int32_t pm_metal_jit_cpp_ast_dump(const pm_jit_cpp_ast_t *ast,
    char *out, size_t out_cap, char *errbuf, size_t errbuf_len);

/* Lower a parsed translation unit to C source (arena-owned, NUL-terminated
 * but len excludes the terminator). The subset is the C-compatible layer:
 * free functions, params, locals, if/while/for, return/break/continue, the
 * C-shared expression operators, literals. Class/template/using declarations
 * and C++-only expressions are refused with
 * "cppx: unsupported: <construct> at line N" — never a silent miscompile. */
int32_t pm_metal_jit_cpp_lower(pm_util_mem_arena_t *arena,
    const pm_jit_cpp_ast_t *unit, char **c_out, size_t *c_out_len,
    char *errbuf, size_t errbuf_len);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_JIT_CPP_TYPES_H */
