#include "pymergetic/metal/jit/cpp/__types__.h"
#include "pymergetic/metal/jit/cpp/__exports__.h"
#include "pymergetic/util/mem.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define PM_JIT_CPP_ERR_MAX 256u
#define PM_JIT_CPP_MAX_TOKS 65536u

static int is_decl_keyword(const char *s, size_t n);
static int tok_text_needs_space(pm_jit_cpp_tok_kind k);

static const char *pm_jit_cpp_tok_name(pm_jit_cpp_tok_kind k) {
    switch (k) {
#define PM_JIT_CPP_TOK_NAME(name) \
    case PM_JIT_CPP_TOK_##name: return #name;
    PM_JIT_CPP_TOKENS(PM_JIT_CPP_TOK_NAME)
#undef PM_JIT_CPP_TOK_NAME
    }
    return "?";
}

/*------------------ Lexer ------------------*/

typedef struct {
    const char *src;
    size_t len;
    size_t pos;
    uint32_t line;
} pm_jit_cpp_lex_t;

typedef struct {
    pm_jit_cpp_token_t *toks;
    uint32_t n;
    uint32_t cap;
    char *strtab;
    size_t strtab_len;
    size_t strtab_cap;
    int failed;
} pm_jit_cpp_out_t;

static int pm_jit_cpp_err(char *errbuf, size_t errbuf_len, const char *msg,
    uint32_t line) {
    if (errbuf != NULL && errbuf_len > 0) {
        snprintf(errbuf, errbuf_len, "cppx: %s at line %u", msg, line);
    }
    return -1;
}

static char *pm_jit_cpp_strtab_intern(pm_jit_cpp_out_t *out,
    pm_util_mem_arena_t *arena, const char *s, size_t n) {
    char *p;
    if (out->strtab_len + n + 1 > out->strtab_cap) {
        size_t ncap = out->strtab_cap == 0 ? 4096 : out->strtab_cap * 2;
        char *nbuf;
        while (ncap < out->strtab_len + n + 1) ncap *= 2;
        nbuf = (char *)pm_util_mem_alloc(arena, ncap);
        if (nbuf == NULL) { out->failed = 1; return NULL; }
        if (out->strtab_len > 0) memcpy(nbuf, out->strtab, out->strtab_len);
        out->strtab = nbuf;
        out->strtab_cap = ncap;
    }
    p = out->strtab + out->strtab_len;
    memcpy(p, s, n);
    p[n] = '\0';
    out->strtab_len += n + 1;
    return p;
}

static void pm_jit_cpp_push(pm_jit_cpp_out_t *out, pm_util_mem_arena_t *arena,
    pm_jit_cpp_tok_kind kind, uint32_t line, const char *text, size_t text_len) {
    pm_jit_cpp_token_t *t;
    if (out->n >= PM_JIT_CPP_MAX_TOKS) { out->failed = 1; return; }
    if (out->n >= out->cap) {
        uint32_t ncap = out->cap == 0 ? 256 : out->cap * 2;
        pm_jit_cpp_token_t *nb = (pm_jit_cpp_token_t *)pm_util_mem_alloc(
            arena, (size_t)ncap * sizeof(pm_jit_cpp_token_t));
        if (nb == NULL) { out->failed = 1; return; }
        if (out->n > 0) memcpy(nb, out->toks, (size_t)out->n * sizeof(*nb));
        out->toks = nb;
        out->cap = ncap;
    }
    t = &out->toks[out->n++];
    t->kind = kind;
    t->line = line;
    t->text = pm_jit_cpp_strtab_intern(out, arena, text, text_len);
    t->text_len = text_len;
    if (t->text == NULL) { out->failed = 1; }
}

/* C++14 keyword set (minus operator/alternative spellings kept as punctuation
 * for simplicity: and/or/not/xor handled as identifiers is a known limit). */
static const char *const pm_jit_cpp_keywords[] = {
    "alignas", "alignof", "asm", "auto", "bool", "break", "case", "catch",
    "char", "char16_t", "char32_t", "class", "const", "constexpr", "const_cast",
    "continue", "decltype", "default", "delete", "do", "double", "dynamic_cast",
    "else", "enum", "explicit", "export", "extern", "false", "float", "for",
    "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace",
    "new", "noexcept", "nullptr", "operator", "private", "protected", "public",
    "register", "reinterpret_cast", "return", "short", "signed", "sizeof",
    "static", "static_assert", "static_cast", "struct", "switch", "template",
    "this", "thread_local", "throw", "true", "try", "typedef", "typeid",
    "typename", "union", "unsigned", "using", "virtual", "void", "volatile",
    "wchar_t", "while",
    NULL
};

static int pm_jit_cpp_is_keyword(const char *s, size_t n) {
    const char *const *k;
    for (k = pm_jit_cpp_keywords; *k != NULL; k++) {
        if (strlen(*k) == n && memcmp(*k, s, n) == 0) return 1;
    }
    return 0;
}

/* Longest-match operator table (3/2-char then 2-char then 1-char). */
static const struct { const char *txt; pm_jit_cpp_tok_kind kind; }
pm_jit_cpp_ops3[] = {
    { "<<=", PM_JIT_CPP_TOK_SHIFT_LEFT_ASSIGN },
    { ">>=", PM_JIT_CPP_TOK_SHIFT_RIGHT_ASSIGN },
    { "->*", PM_JIT_CPP_TOK_ARROW_STAR },
    { NULL, PM_JIT_CPP_TOK_ERROR }
};

static const struct { const char *txt; pm_jit_cpp_tok_kind kind; }
pm_jit_cpp_ops2[] = {
    { "->", PM_JIT_CPP_TOK_PTR_OP },
    { "++", PM_JIT_CPP_TOK_INC_OP },
    { "--", PM_JIT_CPP_TOK_DEC_OP },
    { "<<", PM_JIT_CPP_TOK_LEFT_OP },
    { ">>", PM_JIT_CPP_TOK_RIGHT_OP },
    { "<=", PM_JIT_CPP_TOK_LE_OP },
    { ">=", PM_JIT_CPP_TOK_GE_OP },
    { "==", PM_JIT_CPP_TOK_EQ_OP },
    { "!=", PM_JIT_CPP_TOK_NE_OP },
    { "&&", PM_JIT_CPP_TOK_AND_OP },
    { "||", PM_JIT_CPP_TOK_OR_OP },
    { "+=", PM_JIT_CPP_TOK_ADD_ASSIGN },
    { "-=", PM_JIT_CPP_TOK_SUB_ASSIGN },
    { "*=", PM_JIT_CPP_TOK_MUL_ASSIGN },
    { "/=", PM_JIT_CPP_TOK_DIV_ASSIGN },
    { "%=", PM_JIT_CPP_TOK_MOD_ASSIGN },
    { "^=", PM_JIT_CPP_TOK_XOR_ASSIGN },
    { "&=", PM_JIT_CPP_TOK_AND_ASSIGN },
    { "|=", PM_JIT_CPP_TOK_OR_ASSIGN },
    { "::", PM_JIT_CPP_TOK_DOUBLE_COLON },
    { ".*", PM_JIT_CPP_TOK_DOT_STAR },
    { NULL, PM_JIT_CPP_TOK_ERROR }
};

static int pm_jit_cpp_is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
}

static int pm_jit_cpp_is_ident_char(char c) {
    return pm_jit_cpp_is_ident_start(c) || (c >= '0' && c <= '9');
}

static int pm_jit_cpp_is_digit(char c) { return c >= '0' && c <= '9'; }

static int pm_jit_cpp_is_hex_digit(char c) {
    return pm_jit_cpp_is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* The lexer is line-oriented by design: it tracks the CURRENT line as it
 * scans, so error positions are exact without a second pass. */

static void pm_jit_cpp_skip_ws_comments(pm_jit_cpp_lex_t *lx,
    char *errbuf, size_t errbuf_len) {
    for (;;) {
        if (lx->pos >= lx->len) return;
        char c = lx->src[lx->pos];
        if (c == '\n') { lx->line++; lx->pos++; continue; }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f') {
            lx->pos++;
            continue;
        }
        if (c == '/' && lx->pos + 1 < lx->len) {
            if (lx->src[lx->pos + 1] == '/') {
                while (lx->pos < lx->len && lx->src[lx->pos] != '\n') lx->pos++;
                continue;
            }
            if (lx->src[lx->pos + 1] == '*') {
                uint32_t start = lx->line;
                lx->pos += 2;
                for (;;) {
                    if (lx->pos + 1 >= lx->len) {
                        pm_jit_cpp_err(errbuf, errbuf_len,
                            "unterminated block comment", start);
                        lx->pos = lx->len;
                        return;
                    }
                    if (lx->src[lx->pos] == '\n') lx->line++;
                    if (lx->src[lx->pos] == '*'
                        && lx->src[lx->pos + 1] == '/') {
                        lx->pos += 2;
                        break;
                    }
                    lx->pos++;
                }
                continue;
            }
        }
        return;
    }
}

static void pm_jit_cpp_lex_number(pm_jit_cpp_lex_t *lx, pm_jit_cpp_out_t *out,
    pm_util_mem_arena_t *arena) {
    size_t start = lx->pos;
    pm_jit_cpp_tok_kind kind = PM_JIT_CPP_TOK_INT_LITERAL;
    int is_float = 0;
    if (lx->pos + 1 < lx->len && lx->src[lx->pos] == '0'
        && (lx->src[lx->pos + 1] == 'x' || lx->src[lx->pos + 1] == 'X')) {
        lx->pos += 2;
        while (lx->pos < lx->len && (pm_jit_cpp_is_hex_digit(lx->src[lx->pos])
            || lx->src[lx->pos] == '\'')) lx->pos++;
        /* hex float: fraction or exponent */
        if (lx->pos < lx->len && lx->src[lx->pos] == '.') {
            is_float = 1;
            lx->pos++;
            while (lx->pos < lx->len && (pm_jit_cpp_is_hex_digit(lx->src[lx->pos])
                || lx->src[lx->pos] == '\'')) lx->pos++;
        }
        if (lx->pos < lx->len && (lx->src[lx->pos] == 'p' || lx->src[lx->pos] == 'P')) {
            is_float = 1;
            lx->pos++;
            if (lx->pos < lx->len && (lx->src[lx->pos] == '+' || lx->src[lx->pos] == '-')) {
                lx->pos++;
            }
            while (lx->pos < lx->len && pm_jit_cpp_is_digit(lx->src[lx->pos])) lx->pos++;
        }
    } else {
        while (lx->pos < lx->len && (pm_jit_cpp_is_digit(lx->src[lx->pos])
            || lx->src[lx->pos] == '\'')) lx->pos++;
        if (lx->pos < lx->len && lx->src[lx->pos] == '.'
            && !(lx->pos + 1 < lx->len
                && pm_jit_cpp_is_ident_start(lx->src[lx->pos + 1]))) {
            is_float = 1;
            lx->pos++;
            while (lx->pos < lx->len && (pm_jit_cpp_is_digit(lx->src[lx->pos])
                || lx->src[lx->pos] == '\'')) lx->pos++;
        }
        if (lx->pos < lx->len && (lx->src[lx->pos] == 'e' || lx->src[lx->pos] == 'E')) {
            size_t save = lx->pos;
            lx->pos++;
            if (lx->pos < lx->len && (lx->src[lx->pos] == '+' || lx->src[lx->pos] == '-')) {
                lx->pos++;
            }
            if (lx->pos < lx->len && pm_jit_cpp_is_digit(lx->src[lx->pos])) {
                is_float = 1;
                while (lx->pos < lx->len && pm_jit_cpp_is_digit(lx->src[lx->pos])) lx->pos++;
            } else {
                lx->pos = save;
            }
        }
    }
    /* integer suffixes */
    while (lx->pos < lx->len && (lx->src[lx->pos] == 'u' || lx->src[lx->pos] == 'U'
        || lx->src[lx->pos] == 'l' || lx->src[lx->pos] == 'L')) {
        lx->pos++;
    }
    /* float suffixes */
    if (is_float) {
        while (lx->pos < lx->len && (lx->src[lx->pos] == 'f' || lx->src[lx->pos] == 'F'
            || lx->src[lx->pos] == 'l' || lx->src[lx->pos] == 'L')) {
            lx->pos++;
        }
        kind = PM_JIT_CPP_TOK_FLOAT_LITERAL;
    }
    pm_jit_cpp_push(out, arena, kind, lx->line, lx->src + start, lx->pos - start);
}

static int pm_jit_cpp_lex_escape(pm_jit_cpp_lex_t *lx, char *errbuf,
    size_t errbuf_len) {
    if (lx->pos >= lx->len || lx->src[lx->pos] != '\\') {
        return 0;
    }
    lx->pos++;
    if (lx->pos >= lx->len) {
        pm_jit_cpp_err(errbuf, errbuf_len, "dangling escape", lx->line);
        return -1;
    }
    {
        char c = lx->src[lx->pos];
        if (c == 'x' || c == 'u' || c == 'U') {
            int n = c == 'x' ? -1 : (c == 'u' ? 4 : 8);
            lx->pos++;
            if (n < 0) {
                while (lx->pos < lx->len && pm_jit_cpp_is_hex_digit(lx->src[lx->pos])) {
                    lx->pos++;
                }
            } else {
                int i;
                for (i = 0; i < n; i++) {
                    if (lx->pos >= lx->len || !pm_jit_cpp_is_hex_digit(lx->src[lx->pos])) {
                        pm_jit_cpp_err(errbuf, errbuf_len,
                            "short universal character escape", lx->line);
                        return -1;
                    }
                    lx->pos++;
                }
            }
            return 0;
        }
        /* simple escape char or octal */
        if (c >= '0' && c <= '7') {
            int i;
            for (i = 0; i < 3 && lx->pos < lx->len
                && lx->src[lx->pos] >= '0' && lx->src[lx->pos] <= '7'; i++) {
                lx->pos++;
            }
            return 0;
        }
        lx->pos++;
        return 0;
    }
}

static void pm_jit_cpp_lex_string(pm_jit_cpp_lex_t *lx, pm_jit_cpp_out_t *out,
    pm_util_mem_arena_t *arena, char *errbuf, size_t errbuf_len) {
    /* prefix (L, u8, u, U, R) + quote; raw strings are unsupported */
    size_t start = lx->pos;
    char q = lx->src[lx->pos];
    uint32_t line = lx->line;
    lx->pos++;
    for (;;) {
        if (lx->pos >= lx->len) {
            pm_jit_cpp_err(errbuf, errbuf_len, "unterminated string", line);
            return;
        }
        if (lx->src[lx->pos] == '\n') {
            pm_jit_cpp_err(errbuf, errbuf_len, "newline in string", line);
            return;
        }
        if (lx->src[lx->pos] == '\\') {
            if (pm_jit_cpp_lex_escape(lx, errbuf, errbuf_len) != 0) return;
            continue;
        }
        if (lx->src[lx->pos] == q) { lx->pos++; break; }
        lx->pos++;
    }
    pm_jit_cpp_push(out, arena, PM_JIT_CPP_TOK_STRING_LITERAL, line,
        lx->src + start, lx->pos - start);
}

static void pm_jit_cpp_lex_char(pm_jit_cpp_lex_t *lx, pm_jit_cpp_out_t *out,
    pm_util_mem_arena_t *arena, char *errbuf, size_t errbuf_len) {
    size_t start = lx->pos;
    uint32_t line = lx->line;
    lx->pos++; /* ' */
    if (lx->pos >= lx->len) {
        pm_jit_cpp_err(errbuf, errbuf_len, "unterminated char literal", line);
        return;
    }
    if (lx->src[lx->pos] == '\\') {
        if (pm_jit_cpp_lex_escape(lx, errbuf, errbuf_len) != 0) return;
    } else if (lx->src[lx->pos] == '\'') {
        pm_jit_cpp_err(errbuf, errbuf_len, "empty char literal", line);
        return;
    } else {
        lx->pos++;
    }
    if (lx->pos >= lx->len || lx->src[lx->pos] != '\'') {
        pm_jit_cpp_err(errbuf, errbuf_len, "unterminated char literal", line);
        return;
    }
    lx->pos++;
    pm_jit_cpp_push(out, arena, PM_JIT_CPP_TOK_CHAR_LITERAL, line,
        lx->src + start, lx->pos - start);
}

int32_t pm_metal_jit_cpp_lex(pm_util_mem_arena_t *arena,
    const char *src, size_t src_len,
    pm_jit_cpp_toklist_t *toklist, char *errbuf, size_t errbuf_len) {
    pm_jit_cpp_lex_t lx;
    pm_jit_cpp_out_t out;

    if (arena == NULL || src == NULL || toklist == NULL) {
        return pm_jit_cpp_err(errbuf, errbuf_len, "lex: bad args", 0);
    }
    memset(&lx, 0, sizeof(lx));
    memset(&out, 0, sizeof(out));
    lx.src = src;
    lx.len = src_len;
    lx.line = 1;

    for (;;) {
        char c;
        pm_jit_cpp_skip_ws_comments(&lx, errbuf, errbuf_len);
        if (errbuf != NULL && errbuf[0] != '\0') return -1;
        if (out.failed) return pm_jit_cpp_err(errbuf, errbuf_len,
            "lex: arena exhausted", lx.line);
        if (lx.pos >= lx.len) break;
        c = lx.src[lx.pos];

        if (pm_jit_cpp_is_ident_start(c)) {
            /* string/char prefixes: L"..." u8"..." u'...' U"..." R"..." (R unsupported) */
            size_t start = lx.pos;
            size_t save = lx.pos;
            int prefixed_string = 0;
            while (lx.pos < lx.len && pm_jit_cpp_is_ident_char(lx.src[lx.pos])) lx.pos++;
            if (lx.pos < lx.len && lx.src[lx.pos] == '"'
                && lx.pos - start <= 3) {
                size_t k;
                int ok = 1;
                for (k = start; k < lx.pos; k++) {
                    char pc = lx.src[k];
                    if (pc != 'L' && pc != 'u' && pc != 'U' && pc != '8') { ok = 0; break; }
                    if (pc == '8' && (k != start + 1 || lx.src[start] != 'u')) { ok = 0; break; }
                }
                if (ok && (lx.pos == start
                    || lx.src[start] == 'L' || lx.src[start] == 'u'
                    || lx.src[start] == 'U')) {
                    prefixed_string = 1;
                }
            }
            if (prefixed_string) {
                lx.pos = save;
                pm_jit_cpp_lex_string(&lx, &out, arena, errbuf, errbuf_len);
                if (errbuf != NULL && errbuf[0] != '\0') return -1;
                continue;
            }
            if (lx.pos < lx.len && lx.src[lx.pos] == '\'' && lx.pos - start == 1
                && (c == 'L' || c == 'u' || c == 'U')) {
                lx.pos = save;
                pm_jit_cpp_lex_char(&lx, &out, arena, errbuf, errbuf_len);
                if (errbuf != NULL && errbuf[0] != '\0') return -1;
                continue;
            }
            if (lx.pos < lx.len && lx.src[lx.pos] == '"'
                && lx.pos - start == 1 && c == 'R') {
                return pm_jit_cpp_err(errbuf, errbuf_len,
                    "unsupported: raw string literal", lx.line);
            }
            {
                pm_jit_cpp_tok_kind kind = pm_jit_cpp_is_keyword(
                    lx.src + start, lx.pos - start)
                    ? PM_JIT_CPP_TOK_KEYWORD : PM_JIT_CPP_TOK_IDENT;
                /* constructs the card will never accept are rejected here so
                 * they cannot be silently tokenized-and-misparsed later */
                if (kind == PM_JIT_CPP_TOK_KEYWORD
                    && lx.pos - start == 9
                    && memcmp(lx.src + start, "namespace", 9) == 0) {
                    return pm_jit_cpp_err(errbuf, errbuf_len,
                        "unsupported: namespace", lx.line);
                }
                pm_jit_cpp_push(&out, arena, kind, lx.line, lx.src + start,
                    lx.pos - start);
            }
            continue;
        }

        if (pm_jit_cpp_is_digit(c)
            || (c == '.' && lx.pos + 1 < lx.len
                && pm_jit_cpp_is_digit(lx.src[lx.pos + 1]))) {
            pm_jit_cpp_lex_number(&lx, &out, arena);
            continue;
        }

        if (c == '"') {
            pm_jit_cpp_lex_string(&lx, &out, arena, errbuf, errbuf_len);
            if (errbuf != NULL && errbuf[0] != '\0') return -1;
            continue;
        }
        if (c == '\'') {
            pm_jit_cpp_lex_char(&lx, &out, arena, errbuf, errbuf_len);
            if (errbuf != NULL && errbuf[0] != '\0') return -1;
            continue;
        }

        /* operators: 3-char, 2-char, then 1-char */
        {
            size_t k;
            int matched = 0;
            for (k = 0; pm_jit_cpp_ops3[k].txt != NULL; k++) {
                if (lx.pos + 3 <= lx.len
                    && memcmp(lx.src + lx.pos, pm_jit_cpp_ops3[k].txt, 3) == 0) {
                    pm_jit_cpp_push(&out, arena, pm_jit_cpp_ops3[k].kind,
                        lx.line, pm_jit_cpp_ops3[k].txt, 3);
                    lx.pos += 3;
                    matched = 1;
                    break;
                }
            }
            if (matched) continue;
            for (k = 0; pm_jit_cpp_ops2[k].txt != NULL; k++) {
                if (lx.pos + 2 <= lx.len
                    && memcmp(lx.src + lx.pos, pm_jit_cpp_ops2[k].txt, 2) == 0) {
                    pm_jit_cpp_push(&out, arena, pm_jit_cpp_ops2[k].kind,
                        lx.line, pm_jit_cpp_ops2[k].txt, 2);
                    lx.pos += 2;
                    matched = 1;
                    break;
                }
            }
            if (matched) continue;
            if (lx.pos + 3 <= lx.len && memcmp(lx.src + lx.pos, "...", 3) == 0) {
                pm_jit_cpp_push(&out, arena, PM_JIT_CPP_TOK_ELLIPSIS,
                    lx.line, "...", 3);
                lx.pos += 3;
                continue;
            }
            /* digraphs and trigraphs are unsupported — say so, never misparse */
            if ((c == '%' && lx.pos + 1 < lx.len
                    && (lx.src[lx.pos + 1] == '>' || lx.src[lx.pos + 1] == ':'))
                || (c == '<' && lx.pos + 1 < lx.len && lx.src[lx.pos + 1] == '%')
                || (c == '?' && lx.pos + 1 < lx.len && lx.src[lx.pos + 1] == '?')) {
                return pm_jit_cpp_err(errbuf, errbuf_len,
                    "unsupported: digraph/trigraph", lx.line);
            }
            if (c == '#') {
                /* A '#' is a preprocessor directive only when it is the
                 * first non-blank character of its line. Mid-line '#' is
                 * macro-body material (token pasting ##, stringize #x) —
                 * those live inside #define text but lex fine as PUNCT so
                 * the parser can re-emit them for TCC's preprocessor. */
                size_t back = lx.pos;
                int at_line_start = 1;
                while (back > 0) {
                    char b = lx.src[back - 1];
                    if (b == ' ' || b == '\t') { back--; continue; }
                    if (b == '\n') break; /* only blanks since line start */
                    at_line_start = 0;
                    break;
                }
                if (!at_line_start) {
                    if (lx.pos + 1 < lx.len && lx.src[lx.pos + 1] == '#') {
                        pm_jit_cpp_push(&out, arena, PM_JIT_CPP_TOK_PUNCT,
                            lx.line, lx.src + lx.pos, 2);
                        lx.pos += 2;
                        continue;
                    }
                    pm_jit_cpp_push(&out, arena, PM_JIT_CPP_TOK_PUNCT,
                        lx.line, lx.src + lx.pos, 1);
                    lx.pos++;
                    continue;
                }
                /* directive: from '#' to end of line, with backslash-newline
                 * continuation (multi-line #defines). Verbatim text — the
                 * lowerer re-emits it and TCC's own preprocessor expands it.
                 * No expansion here: cppx's job is the language, not the
                 * preprocessor. */
                {
                    size_t start = lx.pos;
                    while (lx.pos < lx.len) {
                        char d = lx.src[lx.pos];
                        if (d == '\\'
                            && lx.pos + 1 < lx.len
                            && (lx.src[lx.pos + 1] == '\n'
                                || (lx.pos + 2 < lx.len
                                    && lx.src[lx.pos + 1] == '\r'
                                    && lx.src[lx.pos + 2] == '\n'))) {
                            if (lx.src[lx.pos + 1] == '\n') {
                                lx.pos += 2;
                            } else {
                                lx.pos += 3;
                            }
                            lx.line++;
                            continue;
                        }
                        if (d == '\n') break;
                        if (d == '\r') break;
                        lx.pos++;
                    }
                    pm_jit_cpp_push(&out, arena, PM_JIT_CPP_TOK_PP_DIRECTIVE,
                        lx.line, lx.src + start, lx.pos - start);
                }
                continue;
            }
            if (strchr("+-*/%<>=!&|^~?.:,;()[]{}<>", c) != NULL) {
                pm_jit_cpp_push(&out, arena, PM_JIT_CPP_TOK_PUNCT,
                    lx.line, lx.src + lx.pos, 1);
                lx.pos++;
                continue;
            }
            return pm_jit_cpp_err(errbuf, errbuf_len,
                "unsupported: character in input", lx.line);
        }
    }

    /* intern a final END token so the parser never runs off the array */
    pm_jit_cpp_push(&out, arena, PM_JIT_CPP_TOK_END, lx.line, "", 0);
    if (out.failed) return pm_jit_cpp_err(errbuf, errbuf_len,
        "lex: arena exhausted", lx.line);

    /* The tokens' text pointers were interned into a strtab that push() may
     * have reallocated mid-lex. Re-home every text into one arena block so
     * the pointers are stable for the parser. */
    {
        pm_jit_cpp_token_t *toks = out.toks;
        char *blob;
        size_t blob_len = 0;
        size_t off = 0;
        uint32_t j;
        for (j = 0; j < out.n; j++) {
            blob_len += toks[j].text_len + 1;
        }
        blob = (char *)pm_util_mem_alloc(arena, blob_len);
        if (blob == NULL) {
            return pm_jit_cpp_err(errbuf, errbuf_len, "lex: arena exhausted", lx.line);
        }
        for (j = 0; j < out.n; j++) {
            memcpy(blob + off, toks[j].text, toks[j].text_len);
            blob[off + toks[j].text_len] = '\0';
            off += toks[j].text_len + 1;
        }
        for (j = 0; j < out.n; j++) {
            toks[j].text = blob;
            blob += toks[j].text_len + 1;
        }
    }
    toklist->toks = out.toks;
    toklist->n_toks = out.n;
    return 0;
}

/*------------------ Parser ------------------*/

typedef struct {
    pm_jit_cpp_token_t *toks;
    uint32_t n;
    uint32_t pos;
    char *errbuf;
    size_t errbuf_len;
    int failed;
    pm_util_mem_arena_t *arena;
    /* template-parameter stash: while parse_template_decl walks the <...>
     * list the TEMPLATE_DECL node does not exist yet, so the recorded
     * parameter NAME nodes park here and move onto the decl once built. */
    pm_jit_cpp_ast_t *tpl_params[8];
    uint32_t n_tpl_params;
} pm_jit_cpp_parser_t;

static pm_jit_cpp_ast_t *pm_jit_cpp_parse_template_decl(pm_jit_cpp_parser_t *p);
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_class_decl(pm_jit_cpp_parser_t *p);
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_using_decl(pm_jit_cpp_parser_t *p);
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_function_or_var(pm_jit_cpp_parser_t *p);
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_member_or_error(pm_jit_cpp_parser_t *p);
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_postfix(pm_jit_cpp_parser_t *p);

static const pm_jit_cpp_token_t *pm_jit_cpp_cur(pm_jit_cpp_parser_t *p) {
    return p->pos < p->n ? &p->toks[p->pos] : &p->toks[p->n - 1];
}

static const pm_jit_cpp_token_t *pm_jit_cpp_peek(pm_jit_cpp_parser_t *p,
    uint32_t ahead) {
    uint32_t i = p->pos + ahead;
    return i < p->n ? &p->toks[i] : &p->toks[p->n - 1];
}

static void pm_jit_cpp_advance(pm_jit_cpp_parser_t *p) {
    if (p->pos < p->n - 1) p->pos++;
}

static int pm_jit_cpp_perr(pm_jit_cpp_parser_t *p, const char *msg) {
    if (p->errbuf != NULL && p->errbuf_len > 0 && p->errbuf[0] == '\0') {
        snprintf(p->errbuf, p->errbuf_len, "cppx: %s at line %u", msg,
            pm_jit_cpp_cur(p)->line);
    }
    p->failed = 1;
    return -1;
}

/* "expected X, got KIND 'text'" — names the offending token, never silent */
static int pm_jit_cpp_perr_got(pm_jit_cpp_parser_t *p, const char *msg) {
    const pm_jit_cpp_token_t *t = pm_jit_cpp_cur(p);
    char msg2[160];
    snprintf(msg2, sizeof(msg2), "%s, got %s '%.*s'", msg,
        pm_jit_cpp_tok_name(t->kind),
        (int)(t->text_len < 40 ? t->text_len : 40), t->text);
    return pm_jit_cpp_perr(p, msg2);
}

static int pm_jit_cpp_is_punct(const pm_jit_cpp_token_t *t, char c) {
    return t->kind == PM_JIT_CPP_TOK_PUNCT && t->text_len == 1 && t->text[0] == c;
}

static int pm_jit_cpp_is_kw(const pm_jit_cpp_token_t *t, const char *kw) {
    return t->kind == PM_JIT_CPP_TOK_KEYWORD
        && strlen(kw) == t->text_len && memcmp(t->text, kw, t->text_len) == 0;
}

static int pm_jit_cpp_eat_punct(pm_jit_cpp_parser_t *p, char c) {
    if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), c)) {
        pm_jit_cpp_advance(p);
        return 1;
    }
    return 0;
}

static int pm_jit_cpp_expect_punct(pm_jit_cpp_parser_t *p, char c) {
    if (!pm_jit_cpp_eat_punct(p, c)) {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected '%c'", c);
        return pm_jit_cpp_perr_got(p, msg);
    }
    return 0;
}

static pm_jit_cpp_ast_t *pm_jit_cpp_node(pm_jit_cpp_parser_t *p,
    pm_jit_cpp_ast_kind kind, uint32_t line) {
    pm_jit_cpp_ast_t *n = (pm_jit_cpp_ast_t *)pm_util_mem_alloc(
        p->arena, sizeof(*n));
    if (n == NULL) { pm_jit_cpp_perr(p, "arena exhausted"); return NULL; }
    memset(n, 0, sizeof(*n));
    n->kind = kind;
    n->line = line;
    return n;
}

static const char *pm_jit_cpp_intern(pm_jit_cpp_parser_t *p,
    const char *s, size_t n) {
    char *dst = (char *)pm_util_mem_alloc(p->arena, n + 1);
    if (dst == NULL) { pm_jit_cpp_perr(p, "arena exhausted"); return NULL; }
    memcpy(dst, s, n);
    dst[n] = '\0';
    return dst;
}

static int pm_jit_cpp_add_kid(pm_jit_cpp_parser_t *p, pm_jit_cpp_ast_t *parent,
    pm_jit_cpp_ast_t *kid) {
    pm_jit_cpp_ast_t **nk;
    if (parent == NULL || kid == NULL) return 0;
    nk = (pm_jit_cpp_ast_t **)pm_util_mem_alloc(p->arena,
        (size_t)(parent->n_kids + 1) * sizeof(pm_jit_cpp_ast_t *));
    if (nk == NULL) { pm_jit_cpp_perr(p, "arena exhausted"); return -1; }
    if (parent->n_kids > 0) {
        memcpy(nk, parent->kids, (size_t)parent->n_kids * sizeof(pm_jit_cpp_ast_t *));
    }
    nk[parent->n_kids++] = kid;
    parent->kids = nk;
    return 0;
}

/* Join the token range [from, to) with single spaces into arena text.
 * Used for passthrough constructs (typedef) where exact source layout does
 * not matter, only that the tokens re-emit as valid C. String literals are
 * preserved verbatim; other tokens get separating spaces so adjacent idents
 * or numbers never fuse. */
static const char *pm_jit_cpp_raw_text(pm_util_mem_arena_t *arena,
    const pm_jit_cpp_token_t *toks, uint32_t from, uint32_t to,
    size_t *out_len) {
    size_t cap = 64;
    size_t len = 0;
    char *buf;
    uint32_t i;
    buf = (char *)pm_util_mem_alloc(arena, cap);
    if (buf == NULL) return NULL;
    for (i = from; i < to; i++) {
        const pm_jit_cpp_token_t *t = &toks[i];
        size_t need = len + t->text_len + 2;
        if (need > cap) {
            size_t ncap = cap * 2;
            char *nb;
            while (need > ncap) ncap *= 2;
            nb = (char *)pm_util_mem_alloc(arena, ncap);
            if (nb == NULL) return NULL;
            memcpy(nb, buf, len);
            buf = nb;
            cap = ncap;
        }
        if (len > 0) buf[len++] = ' ';
        memcpy(buf + len, t->text, t->text_len);
        len += t->text_len;
    }
    buf[len] = '\0';
    *out_len = len;
    return buf;
}

/* forward decls */
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_expr(pm_jit_cpp_parser_t *p);
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_stmt(pm_jit_cpp_parser_t *p);
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_type(pm_jit_cpp_parser_t *p);
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_block(pm_jit_cpp_parser_t *p);
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_declaration_or_error(
    pm_jit_cpp_parser_t *p);

/* type := [const|volatile]* (builtin | template-id | qualified-name)
 *        ('*' | '&' | '&&' | const | '[' n ']')* */
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_type(pm_jit_cpp_parser_t *p) {
    const pm_jit_cpp_token_t *t = pm_jit_cpp_cur(p);
    pm_jit_cpp_ast_t *ty;
    char buf[256];
    size_t len = 0;

    buf[0] = '\0';
    while (pm_jit_cpp_is_kw(t, "const") || pm_jit_cpp_is_kw(t, "volatile")
        || pm_jit_cpp_is_kw(t, "unsigned") || pm_jit_cpp_is_kw(t, "signed")
        || pm_jit_cpp_is_kw(t, "long") || pm_jit_cpp_is_kw(t, "short")
        || pm_jit_cpp_is_kw(t, "static") || pm_jit_cpp_is_kw(t, "extern")
        || pm_jit_cpp_is_kw(t, "register") || pm_jit_cpp_is_kw(t, "inline")
        || pm_jit_cpp_is_kw(t, "constexpr") || pm_jit_cpp_is_kw(t, "mutable")
        || pm_jit_cpp_is_kw(t, "thread_local")) {
        if (len + t->text_len + 2 >= sizeof(buf)) {
            pm_jit_cpp_perr(p, "type name too long");
            return NULL;
        }
        memcpy(buf + len, t->text, t->text_len);
        len += t->text_len;
        buf[len++] = ' ';
        pm_jit_cpp_advance(p);
        t = pm_jit_cpp_cur(p);
    }
    buf[len] = '\0';

    if (t->kind != PM_JIT_CPP_TOK_IDENT && t->kind != PM_JIT_CPP_TOK_KEYWORD) {
        pm_jit_cpp_perr_got(p, "expected type name");
        return NULL;
    }
    /* builtin type keywords arrive as KEYWORD tokens; accept the common set */
    {
        static const char *const builtins[] = {
            "void", "bool", "char", "int", "float", "double", "auto",
            "wchar_t", "char16_t", "char32_t", "long", "short", "signed",
            "unsigned", "const", "volatile", NULL
        };
        int is_builtin = 0;
        const char *const *b;
        for (b = builtins; *b != NULL; b++) {
            if (t->kind == PM_JIT_CPP_TOK_KEYWORD && t->text_len == strlen(*b)
                && memcmp(t->text, *b, t->text_len) == 0) {
                is_builtin = 1;
                break;
            }
        }
        if (!is_builtin && t->kind != PM_JIT_CPP_TOK_IDENT) {
            pm_jit_cpp_perr(p, "expected type name");
            return NULL;
        }
    }
    if (len + t->text_len >= sizeof(buf)) {
        pm_jit_cpp_perr(p, "type name too long");
        return NULL;
    }
    memcpy(buf + len, t->text, t->text_len);
    len += t->text_len;
    pm_jit_cpp_advance(p);

    /* declarators: *, &, &&, const, ::, <args>, [] */
    for (;;) {
        t = pm_jit_cpp_cur(p);
        if (pm_jit_cpp_is_punct(t, '*') || pm_jit_cpp_is_punct(t, '&')
            || (t->kind == PM_JIT_CPP_TOK_DOUBLE_COLON)) {
            if (len + 4 >= sizeof(buf)) { pm_jit_cpp_perr(p, "type name too long"); return NULL; }
            if (t->kind == PM_JIT_CPP_TOK_DOUBLE_COLON) {
                memcpy(buf + len, "::", 2); len += 2;
                pm_jit_cpp_advance(p);
                /* qualified-name tail: :: must be followed by an identifier
                 * (std::vector). Without this the tail name would be taken
                 * as the declarator and the template args misparsed. */
                t = pm_jit_cpp_cur(p);
                if (t->kind != PM_JIT_CPP_TOK_IDENT
                    && t->kind != PM_JIT_CPP_TOK_KEYWORD) {
                    pm_jit_cpp_perr_got(p, "expected name after '::'");
                    return NULL;
                }
                if (len + t->text_len + 1 >= sizeof(buf)) {
                    pm_jit_cpp_perr(p, "type name too long");
                    return NULL;
                }
                memcpy(buf + len, t->text, t->text_len);
                len += t->text_len;
                pm_jit_cpp_advance(p);
                continue;
            }
            if (pm_jit_cpp_is_punct(t, '*')) {
                if (len + 2 >= sizeof(buf)) {
                    pm_jit_cpp_perr(p, "type name too long");
                    return NULL;
                }
                buf[len++] = '*';
            } else if (pm_jit_cpp_peek(p, 1)->kind == PM_JIT_CPP_TOK_PUNCT
                && pm_jit_cpp_peek(p, 1)->text_len == 1
                && pm_jit_cpp_peek(p, 1)->text[0] == '&') {
                buf[len++] = '&'; buf[len++] = '&';
                pm_jit_cpp_advance(p);
            } else {
                buf[len++] = '&';
            }
            pm_jit_cpp_advance(p);
            continue;
        }
        if (pm_jit_cpp_is_kw(t, "const") || pm_jit_cpp_is_kw(t, "volatile")) {
            if (len + 7 >= sizeof(buf)) { pm_jit_cpp_perr(p, "type name too long"); return NULL; }
            memcpy(buf + len, " const", 6); len += 6;
            pm_jit_cpp_advance(p);
            continue;
        }
        /* template args on the type: Foo<int> — parse and record in the text */
        if (pm_jit_cpp_is_punct(t, '<')) {
            /* balanced angle scan; nested <> and expressions like Foo<A<B>> */
            uint32_t depth = 0;
            uint32_t start_pos = p->pos;
            do {
                t = pm_jit_cpp_cur(p);
                if (t->kind == PM_JIT_CPP_TOK_END) {
                    p->pos = start_pos;
                    goto type_done;
                }
                if (pm_jit_cpp_is_punct(t, '<')) depth++;
                if (pm_jit_cpp_is_punct(t, '>')) depth--;
                pm_jit_cpp_advance(p);
            } while (depth > 0);
            /* append the <...> text */
            {
                uint32_t k;
                for (k = start_pos; k < p->pos; k++) {
                    if (len + 2 >= sizeof(buf)) { pm_jit_cpp_perr(p, "type name too long"); return NULL; }
                    if (tok_text_needs_space(p->toks[k].kind)) buf[len++] = ' ';
                    memcpy(buf + len, p->toks[k].text, p->toks[k].text_len);
                    len += p->toks[k].text_len;
                }
            }
            continue;
        }
        /* array suffix [N]: keep the size text — the declarator must carry
         * it (char buf[256] emits as char[256] buf). */
        if (pm_jit_cpp_is_punct(t, '[')) {
            uint32_t start = p->pos;
            uint32_t depth = 0;
            do {
                t = pm_jit_cpp_cur(p);
                if (t->kind == PM_JIT_CPP_TOK_END) {
                    pm_jit_cpp_perr(p, "unterminated array declarator");
                    return NULL;
                }
                if (pm_jit_cpp_is_punct(t, '[')) depth++;
                if (pm_jit_cpp_is_punct(t, ']')) depth--;
                pm_jit_cpp_advance(p);
            } while (depth > 0);
            {
                uint32_t k;
                for (k = start; k < p->pos; k++) {
                    if (len + 2 >= sizeof(buf)) { pm_jit_cpp_perr(p, "type name too long"); return NULL; }
                    if (tok_text_needs_space(p->toks[k].kind)) buf[len++] = ' ';
                    memcpy(buf + len, p->toks[k].text, p->toks[k].text_len);
                    len += p->toks[k].text_len;
                }
            }
            continue;
        }
        break;
    }
type_done:
    buf[len] = '\0';
    ty = pm_jit_cpp_node(p, PM_JIT_CPP_AST_TYPE, pm_jit_cpp_cur(p)->line);
    if (ty == NULL) return NULL;
    ty->text = pm_jit_cpp_intern(p, buf, len);
    if (ty->text == NULL) return NULL;
    ty->text_len = len;
    return ty;
}

/* integer literal value — local parser, no strtoull (freestanding firmware
 * stdlib has no C99 int64 conversion) */
static int64_t pm_jit_cpp_parse_int(const char *s, size_t n) {
    uint64_t v = 0;
    int base = 10;
    size_t i = 0;
    if (n > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        i = 2;
    } else if (n > 1 && s[0] == '0') {
        base = 8;
        i = 1;
    }
    for (; i < n; i++) {
        int d;
        char c = s[i];
        if (c == 'u' || c == 'U' || c == 'l' || c == 'L') break;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        v = v * (uint64_t)base + (uint64_t)d;
    }
    return (int64_t)v;
}

/* primary := literal | name | this | ( expr ) | new-expr | call-suffix chains */
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_primary(pm_jit_cpp_parser_t *p) {
    const pm_jit_cpp_token_t *t = pm_jit_cpp_cur(p);
    pm_jit_cpp_ast_t *e;

    if (t->kind == PM_JIT_CPP_TOK_INT_LITERAL
        || t->kind == PM_JIT_CPP_TOK_FLOAT_LITERAL
        || t->kind == PM_JIT_CPP_TOK_CHAR_LITERAL
        || t->kind == PM_JIT_CPP_TOK_STRING_LITERAL) {
        e = pm_jit_cpp_node(p, PM_JIT_CPP_AST_LITERAL, t->line);
        if (e == NULL) return NULL;
        e->text = pm_jit_cpp_intern(p, t->text, t->text_len);
        if (e->text == NULL) return NULL;
        e->text_len = t->text_len;
        if (t->kind == PM_JIT_CPP_TOK_INT_LITERAL) {
            e->int_val = pm_jit_cpp_parse_int(t->text, t->text_len);
        }
        pm_jit_cpp_advance(p);
        return e;
    }
    if (t->kind == PM_JIT_CPP_TOK_KEYWORD) {
        if (pm_jit_cpp_is_kw(t, "true") || pm_jit_cpp_is_kw(t, "false")) {
            e = pm_jit_cpp_node(p, PM_JIT_CPP_AST_LITERAL, t->line);
            if (e == NULL) return NULL;
            e->text = pm_jit_cpp_intern(p, t->text, t->text_len);
            if (e->text == NULL) return NULL;
            e->text_len = t->text_len;
            e->int_val = pm_jit_cpp_is_kw(t, "true") ? 1 : 0;
            pm_jit_cpp_advance(p);
            return e;
        }
        if (pm_jit_cpp_is_kw(t, "nullptr")) {
            e = pm_jit_cpp_node(p, PM_JIT_CPP_AST_LITERAL, t->line);
            if (e == NULL) return NULL;
            e->text = pm_jit_cpp_intern(p, "nullptr", 7);
            if (e->text == NULL) return NULL;
            e->text_len = 7;
            pm_jit_cpp_advance(p);
            return e;
        }
        if (pm_jit_cpp_is_kw(t, "this")) {
            e = pm_jit_cpp_node(p, PM_JIT_CPP_AST_NAME, t->line);
            if (e == NULL) return NULL;
            e->text = pm_jit_cpp_intern(p, "this", 4);
            if (e->text == NULL) return NULL;
            e->text_len = 4;
            pm_jit_cpp_advance(p);
            return e;
        }
        if (pm_jit_cpp_is_kw(t, "new")) {
            pm_jit_cpp_advance(p);
            {
                pm_jit_cpp_ast_t *ty = pm_jit_cpp_parse_type(p);
                pm_jit_cpp_ast_t *ne;
                if (ty == NULL) return NULL;
                ne = pm_jit_cpp_node(p, PM_JIT_CPP_AST_NEW_EXPR, t->line);
                if (ne == NULL) return NULL;
                ne->text = ty->text;
                ne->text_len = ty->text_len;
                /* initializer (parens): real exprs, recorded as kids so the
                 * lowerer can replay the ctor call. Braces and new[] are
                 * consumed raw — the lowerer refuses them. */
                if (pm_jit_cpp_eat_punct(p, '(')) {
                    if (!pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ')')) {
                        for (;;) {
                            pm_jit_cpp_ast_t *arg = pm_jit_cpp_parse_expr(p);
                            if (arg == NULL) return NULL;
                            if (pm_jit_cpp_add_kid(p, ne, arg) != 0) return NULL;
                            if (pm_jit_cpp_eat_punct(p, ',')) continue;
                            break;
                        }
                    }
                    if (pm_jit_cpp_expect_punct(p, ')') != 0) return NULL;
                } else if (pm_jit_cpp_eat_punct(p, '[')) {
                    uint32_t depth = 1;
                    while (depth > 0) {
                        const pm_jit_cpp_token_t *u = pm_jit_cpp_cur(p);
                        if (u->kind == PM_JIT_CPP_TOK_END) {
                            return pm_jit_cpp_perr(p, "unterminated new[] size") < 0
                                ? NULL : NULL;
                        }
                        if (pm_jit_cpp_is_punct(u, '[')) depth++;
                        if (pm_jit_cpp_is_punct(u, ']')) depth--;
                        pm_jit_cpp_advance(p);
                    }
                }
                return ne;
            }
        }
        if (pm_jit_cpp_is_kw(t, "delete")) {
            pm_jit_cpp_advance(p);
            pm_jit_cpp_eat_punct(p, '[');
            pm_jit_cpp_eat_punct(p, ']');
            {
                pm_jit_cpp_ast_t *operand = pm_jit_cpp_parse_expr(p);
                pm_jit_cpp_ast_t *de;
                if (operand == NULL) return NULL;
                de = pm_jit_cpp_node(p, PM_JIT_CPP_AST_DELETE_EXPR, t->line);
                if (de == NULL) return NULL;
                if (pm_jit_cpp_add_kid(p, de, operand) != 0) return NULL;
                return de;
            }
        }
        if (pm_jit_cpp_is_kw(t, "sizeof") || pm_jit_cpp_is_kw(t, "alignof")
            || pm_jit_cpp_is_kw(t, "decltype")) {
            /* sizeof(expr) / sizeof(type) — try type first (a single IDENT
             * or IDENT * ... spans), fall back to expression. */
            uint32_t save = p->pos;
            pm_jit_cpp_advance(p);
            if (!pm_jit_cpp_eat_punct(p, '(')) {
                pm_jit_cpp_perr(p, "expected '(' after sizeof/alignof/decltype");
                return NULL;
            }
            {
                pm_jit_cpp_ast_t *operand = NULL;
                pm_jit_cpp_ast_t *un;
                const pm_jit_cpp_token_t *inner = pm_jit_cpp_cur(p);
                int as_type = 0;
                if (inner->kind == PM_JIT_CPP_TOK_IDENT
                    && pm_jit_cpp_is_punct(pm_jit_cpp_peek(p, 1), ')')) {
                    as_type = 1; /* sizeof(Name) */
                } else if (inner->kind == PM_JIT_CPP_TOK_KEYWORD
                    && (pm_jit_cpp_is_kw(inner, "int")
                        || pm_jit_cpp_is_kw(inner, "char")
                        || pm_jit_cpp_is_kw(inner, "long")
                        || pm_jit_cpp_is_kw(inner, "short")
                        || pm_jit_cpp_is_kw(inner, "float")
                        || pm_jit_cpp_is_kw(inner, "double")
                        || pm_jit_cpp_is_kw(inner, "void")
                        || pm_jit_cpp_is_kw(inner, "unsigned")
                        || pm_jit_cpp_is_kw(inner, "signed")
                        || pm_jit_cpp_is_kw(inner, "bool"))) {
                    as_type = 1; /* sizeof(int) */
                } else if (inner->kind == PM_JIT_CPP_TOK_IDENT
                    && pm_jit_cpp_is_punct(pm_jit_cpp_peek(p, 1), '*')
                    && pm_jit_cpp_is_punct(pm_jit_cpp_peek(p, 2), ')')) {
                    as_type = 1; /* sizeof(T *) */
                }
                if (as_type) {
                    pm_jit_cpp_ast_t *ty = pm_jit_cpp_parse_type(p);
                    if (ty != NULL
                        && pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ')')) {
                        operand = ty;
                    }
                }
                if (operand == NULL) {
                    p->pos = save;
                    pm_jit_cpp_advance(p); /* sizeof */
                    if (!pm_jit_cpp_eat_punct(p, '(')) {
                        pm_jit_cpp_perr(p,
                            "expected '(' after sizeof/alignof/decltype");
                        return NULL;
                    }
                    operand = pm_jit_cpp_parse_expr(p);
                }
                if (operand == NULL) return NULL;
                if (pm_jit_cpp_expect_punct(p, ')') != 0) return NULL;
                un = pm_jit_cpp_node(p, PM_JIT_CPP_AST_UNARY, t->line);
                if (un == NULL) return NULL;
                un->text = pm_jit_cpp_intern(p, t->text, t->text_len);
                if (un->text == NULL) return NULL;
                un->text_len = t->text_len;
                un->op_kind = t->kind;
                if (pm_jit_cpp_add_kid(p, un, operand) != 0) return NULL;
                return un;
            }
        }
        pm_jit_cpp_perr(p, "unsupported: keyword in expression");
        return NULL;
    }
    if (t->kind == PM_JIT_CPP_TOK_IDENT) {
        /* qualified name and/or template-id: std::cout, identity<int>(x).
         * Both extend the name; a TEMPLATE_REF node records the <args>. */
        char buf[256];
        size_t len = t->text_len;
        int is_template = 0;
        if (len >= sizeof(buf)) {
            pm_jit_cpp_perr(p, "name too long");
            return NULL;
        }
        memcpy(buf, t->text, t->text_len);
        pm_jit_cpp_advance(p);
        for (;;) {
            const pm_jit_cpp_token_t *u = pm_jit_cpp_cur(p);
            if (u->kind == PM_JIT_CPP_TOK_DOUBLE_COLON) {
                const pm_jit_cpp_token_t *nx = pm_jit_cpp_peek(p, 1);
                if (nx->kind != PM_JIT_CPP_TOK_IDENT) break;
                if (len + 2 + nx->text_len + 1 >= sizeof(buf)) {
                    pm_jit_cpp_perr(p, "name too long");
                    return NULL;
                }
                buf[len++] = ':'; buf[len++] = ':';
                memcpy(buf + len, nx->text, nx->text_len);
                len += nx->text_len;
                pm_jit_cpp_advance(p);
                pm_jit_cpp_advance(p);
                continue;
            }
            if (pm_jit_cpp_is_punct(u, '<')) {
                /* balanced angle bracket scan — raw text, like parse_type.
                 * '<' is ambiguous (comparison vs template-args); the scan
                 * aborts (pos restored) when the brackets never close or the
                 * bracketed span crosses a statement boundary (';', '{',
                 * '}'), which comparisons like i < 10; i++) do. */
                uint32_t depth = 0;
                uint32_t start_pos = p->pos;
                uint32_t k;
                do {
                    u = pm_jit_cpp_cur(p);
                    if (u->kind == PM_JIT_CPP_TOK_END
                        || pm_jit_cpp_is_punct(u, ';')
                        || pm_jit_cpp_is_punct(u, '{')
                        || pm_jit_cpp_is_punct(u, '}')
                        || pm_jit_cpp_is_punct(u, '?')
                        /* operators that cannot appear inside template
                         * argument lists: a < b && b > 3 is a comparison */
                        || u->kind == PM_JIT_CPP_TOK_AND_OP
                        || u->kind == PM_JIT_CPP_TOK_OR_OP
                        || u->kind == PM_JIT_CPP_TOK_EQ_OP
                        || u->kind == PM_JIT_CPP_TOK_NE_OP
                        || u->kind == PM_JIT_CPP_TOK_LE_OP
                        || u->kind == PM_JIT_CPP_TOK_GE_OP
                        || u->kind == PM_JIT_CPP_TOK_LEFT_OP
                        || u->kind == PM_JIT_CPP_TOK_RIGHT_OP) {
                        p->pos = start_pos;
                        goto name_done;
                    }
                    if (pm_jit_cpp_is_punct(u, '<')) depth++;
                    if (pm_jit_cpp_is_punct(u, '>')) depth--;
                    pm_jit_cpp_advance(p);
                } while (depth > 0);
                if (len + 2 >= sizeof(buf)) {
                    pm_jit_cpp_perr(p, "name too long");
                    return NULL;
                }
                for (k = start_pos; k < p->pos; k++) {
                    if (len + 2 >= sizeof(buf)) {
                        pm_jit_cpp_perr(p, "name too long");
                        return NULL;
                    }
                    if (tok_text_needs_space(p->toks[k].kind)) buf[len++] = ' ';
                    memcpy(buf + len, p->toks[k].text, p->toks[k].text_len);
                    len += p->toks[k].text_len;
                }
                is_template = 1;
                continue;
            }
            break;
        }
name_done:
        e = pm_jit_cpp_node(p, is_template ? PM_JIT_CPP_AST_TEMPLATE_REF
            : PM_JIT_CPP_AST_NAME, t->line);
        if (e == NULL) return NULL;
        e->text = pm_jit_cpp_intern(p, buf, len);
        if (e->text == NULL) return NULL;
        e->text_len = len;
        return e;
    }
    if (pm_jit_cpp_is_punct(t, '(')) {
        pm_jit_cpp_advance(p);
        e = pm_jit_cpp_parse_expr(p);
        if (e == NULL) return NULL;
        if (pm_jit_cpp_expect_punct(p, ')') != 0) return NULL;
        return e;
    }
    pm_jit_cpp_perr_got(p, "expected expression");
    return NULL;
}

/* unary: (! - + * & ++ -- cast) unary | postfix */
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_unary(pm_jit_cpp_parser_t *p) {
    const pm_jit_cpp_token_t *t = pm_jit_cpp_cur(p);
    if (t->kind == PM_JIT_CPP_TOK_PUNCT && t->text_len == 1
        && strchr("!-+*&", t->text[0]) != NULL) {
        pm_jit_cpp_ast_t *operand;
        pm_jit_cpp_ast_t *un;
        pm_jit_cpp_advance(p);
        operand = pm_jit_cpp_parse_unary(p);
        if (operand == NULL) return NULL;
        un = pm_jit_cpp_node(p, PM_JIT_CPP_AST_UNARY, t->line);
        if (un == NULL) return NULL;
        un->text = pm_jit_cpp_intern(p, t->text, t->text_len);
        if (un->text == NULL) return NULL;
        un->text_len = t->text_len;
        un->op_kind = t->kind;
        if (pm_jit_cpp_add_kid(p, un, operand) != 0) return NULL;
        return un;
    }
    if (t->kind == PM_JIT_CPP_TOK_INC_OP || t->kind == PM_JIT_CPP_TOK_DEC_OP) {
        pm_jit_cpp_ast_t *operand;
        pm_jit_cpp_ast_t *un;
        pm_jit_cpp_advance(p);
        operand = pm_jit_cpp_parse_unary(p);
        if (operand == NULL) return NULL;
        un = pm_jit_cpp_node(p, PM_JIT_CPP_AST_UNARY, t->line);
        if (un == NULL) return NULL;
        un->text = pm_jit_cpp_intern(p, t->text, t->text_len);
        if (un->text == NULL) return NULL;
        un->text_len = t->text_len;
        un->op_kind = t->kind;
        if (pm_jit_cpp_add_kid(p, un, operand) != 0) return NULL;
        return un;
    }
    /* C-style cast: (type)expr. Disambiguated from a parenthesized
     * expression by the token after '(': a type looks like IDENT or a
     * builtin-type keyword, optionally with stars, ampersands or const. */
    if (pm_jit_cpp_is_punct(t, '(')) {
        const pm_jit_cpp_token_t *nx = pm_jit_cpp_peek(p, 1);
        int looks_like_type;
        if (nx->kind == PM_JIT_CPP_TOK_IDENT) {
            looks_like_type = 1;
        } else if (nx->kind == PM_JIT_CPP_TOK_KEYWORD) {
            static const char *const tys[] = {
                "void", "bool", "char", "int", "float", "double", "long",
                "short", "unsigned", "signed", "const", "size_t", NULL
            };
            const char *const *ty;
            looks_like_type = 0;
            for (ty = tys; *ty != NULL; ty++) {
                if (nx->text_len == strlen(*ty)
                    && memcmp(nx->text, *ty, nx->text_len) == 0) {
                    looks_like_type = 1;
                    break;
                }
            }
        } else {
            looks_like_type = 0;
        }
        if (looks_like_type) {
            /* scan the balanced parens; verify the span is a plausible type:
             * tokens all in {IDENT, KEYWORD(builtin), *, &, const, [N]} and
             * ends with ')' directly after a type-ish token. */
            uint32_t save = p->pos;
            uint32_t k;
            int plaus = 1;
            pm_jit_cpp_advance(p); /* ( */
            k = p->pos;
            while (pm_jit_cpp_cur(p)->kind != PM_JIT_CPP_TOK_END
                && !pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ')')) {
                const pm_jit_cpp_token_t *u = pm_jit_cpp_cur(p);
                if (u->kind == PM_JIT_CPP_TOK_IDENT
                    || u->kind == PM_JIT_CPP_TOK_KEYWORD
                    || pm_jit_cpp_is_punct(u, '*')
                    || pm_jit_cpp_is_punct(u, '[')) {
                    /* skip balanced [N] */
                    if (pm_jit_cpp_is_punct(u, '[')) {
                        uint32_t d = 0;
                        do {
                            if (pm_jit_cpp_cur(p)->kind == PM_JIT_CPP_TOK_END) {
                                plaus = 0; break;
                            }
                            if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '[')) d++;
                            if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ']')) d--;
                            pm_jit_cpp_advance(p);
                        } while (d > 0);
                        continue;
                    }
                    pm_jit_cpp_advance(p);
                    continue;
                }
                plaus = 0;
                break;
            }
            if (plaus && pm_jit_cpp_cur(p)->kind != PM_JIT_CPP_TOK_END) {
                /* we are at ')': it is a cast iff the next token can start an
                 * expression (IDENT, literal, '(', unary op). */
                const pm_jit_cpp_token_t *after = pm_jit_cpp_peek(p, 1);
                int expr_follows = 0;
                if (after->kind == PM_JIT_CPP_TOK_IDENT
                    || after->kind == PM_JIT_CPP_TOK_INT_LITERAL
                    || after->kind == PM_JIT_CPP_TOK_FLOAT_LITERAL
                    || after->kind == PM_JIT_CPP_TOK_CHAR_LITERAL
                    || after->kind == PM_JIT_CPP_TOK_STRING_LITERAL
                    || pm_jit_cpp_is_punct(after, '(')) {
                    expr_follows = 1;
                }
                if (after->kind == PM_JIT_CPP_TOK_PUNCT
                    && after->text_len == 1
                    && strchr("!-+*&", after->text[0]) != NULL) {
                    expr_follows = 1;
                }
                if (after->kind == PM_JIT_CPP_TOK_INC_OP
                    || after->kind == PM_JIT_CPP_TOK_DEC_OP) {
                    expr_follows = 1;
                }
                if (after->kind == PM_JIT_CPP_TOK_KEYWORD
                    && (pm_jit_cpp_is_kw(after, "sizeof")
                        || pm_jit_cpp_is_kw(after, "this"))) {
                    expr_follows = 1;
                }
                if (expr_follows) {
                    pm_jit_cpp_ast_t *cast;
                    pm_jit_cpp_ast_t *operand;
                    /* rebuild the type text from the span */
                    cast = pm_jit_cpp_node(p, PM_JIT_CPP_AST_CAST, t->line);
                    if (cast == NULL) return NULL;
                    cast->text = pm_jit_cpp_raw_text(p->arena, p->toks,
                        k, p->pos, &cast->text_len);
                    if (cast->text == NULL) return NULL;
                    pm_jit_cpp_advance(p); /* ) */
                    operand = pm_jit_cpp_parse_unary(p);
                    if (operand == NULL) return NULL;
                    if (pm_jit_cpp_add_kid(p, cast, operand) != 0) return NULL;
                    return cast;
                }
            }
            /* not a cast — restore and re-parse as parenthesized expr */
            p->pos = save;
        }
    }
    return pm_jit_cpp_parse_postfix(p);
}

/* postfix: primary followed by ., ->, (), [], ++, -- */
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_postfix(pm_jit_cpp_parser_t *p) {
    pm_jit_cpp_ast_t *e = pm_jit_cpp_parse_primary(p);
    if (e == NULL) return NULL;
    for (;;) {
        const pm_jit_cpp_token_t *t = pm_jit_cpp_cur(p);
        if (pm_jit_cpp_is_punct(t, '.')) {
            pm_jit_cpp_advance(p);
            {
                const pm_jit_cpp_token_t *nm = pm_jit_cpp_cur(p);
                pm_jit_cpp_ast_t *m;
                if (nm->kind != PM_JIT_CPP_TOK_IDENT) {
                    pm_jit_cpp_perr(p, "expected member name after '.'");
                    return NULL;
                }
                m = pm_jit_cpp_node(p, PM_JIT_CPP_AST_MEMBER, t->line);
                if (m == NULL) return NULL;
                m->text = pm_jit_cpp_intern(p, nm->text, nm->text_len);
                if (m->text == NULL) return NULL;
                m->text_len = nm->text_len;
                if (pm_jit_cpp_add_kid(p, m, e) != 0) return NULL;
                pm_jit_cpp_advance(p);
                e = m;
                continue;
            }
        }
        if (t->kind == PM_JIT_CPP_TOK_PTR_OP) {
            pm_jit_cpp_advance(p);
            {
                const pm_jit_cpp_token_t *nm = pm_jit_cpp_cur(p);
                pm_jit_cpp_ast_t *m;
                if (nm->kind != PM_JIT_CPP_TOK_IDENT) {
                    pm_jit_cpp_perr(p, "expected member name after '->'");
                    return NULL;
                }
                m = pm_jit_cpp_node(p, PM_JIT_CPP_AST_MEMBER, t->line);
                if (m == NULL) return NULL;
                m->text = pm_jit_cpp_intern(p, nm->text, nm->text_len);
                if (m->text == NULL) return NULL;
                m->text_len = nm->text_len;
                m->int_val = 1; /* '->' access, not '.' */
                if (pm_jit_cpp_add_kid(p, m, e) != 0) return NULL;
                pm_jit_cpp_advance(p);
                e = m;
                continue;
            }
        }
        if (pm_jit_cpp_is_punct(t, '(')) {
            pm_jit_cpp_advance(p);
            {
                pm_jit_cpp_ast_t *call = pm_jit_cpp_node(p, PM_JIT_CPP_AST_CALL,
                    t->line);
                if (call == NULL) return NULL;
                if (pm_jit_cpp_add_kid(p, call, e) != 0) return NULL;
                if (!pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ')')) {
                    for (;;) {
                        pm_jit_cpp_ast_t *arg = pm_jit_cpp_parse_expr(p);
                        if (arg == NULL) return NULL;
                        if (pm_jit_cpp_add_kid(p, call, arg) != 0) return NULL;
                        if (pm_jit_cpp_eat_punct(p, ',')) continue;
                        break;
                    }
                }
                if (pm_jit_cpp_expect_punct(p, ')') != 0) return NULL;
                e = call;
                continue;
            }
        }
        if (pm_jit_cpp_is_punct(t, '[')) {
            pm_jit_cpp_advance(p);
            {
                pm_jit_cpp_ast_t *idx = pm_jit_cpp_parse_expr(p);
                pm_jit_cpp_ast_t *bin;
                if (idx == NULL) return NULL;
                if (pm_jit_cpp_expect_punct(p, ']') != 0) return NULL;
                bin = pm_jit_cpp_node(p, PM_JIT_CPP_AST_BINARY, t->line);
                if (bin == NULL) return NULL;
                bin->text = "[]";
                bin->text_len = 2;
                if (pm_jit_cpp_add_kid(p, bin, e) != 0) return NULL;
                if (pm_jit_cpp_add_kid(p, bin, idx) != 0) return NULL;
                e = bin;
                continue;
            }
        }
        if (t->kind == PM_JIT_CPP_TOK_INC_OP || t->kind == PM_JIT_CPP_TOK_DEC_OP) {
            pm_jit_cpp_advance(p);
            {
                pm_jit_cpp_ast_t *un = pm_jit_cpp_node(p, PM_JIT_CPP_AST_UNARY,
                    t->line);
                if (un == NULL) return NULL;
                un->text = pm_jit_cpp_intern(p, t->text, t->text_len);
                if (un->text == NULL) return NULL;
                un->text_len = t->text_len;
                un->op_kind = t->kind;
                un->int_val = 1; /* postfix marker: op rides after the operand */
                if (pm_jit_cpp_add_kid(p, un, e) != 0) return NULL;
                e = un;
                continue;
            }
        }
        break;
    }
    return e;
}

/* precedence climbing over the binary operator tokens; higher binds tighter */
static int pm_jit_cpp_binop_prec(const pm_jit_cpp_token_t *t) {
    if (t->kind == PM_JIT_CPP_TOK_PUNCT && t->text_len == 1) {
        switch (t->text[0]) {
        case '|': return 3;
        case '^': return 4;
        case '&': return 5;
        case '<': case '>': return 6;
        case '+': case '-': return 8;
        case '*': case '/': case '%': return 9;
        default: return -1;
        }
    }
    switch (t->kind) {
    case PM_JIT_CPP_TOK_OR_OP: return 1;
    case PM_JIT_CPP_TOK_AND_OP: return 2;
    case PM_JIT_CPP_TOK_EQ_OP:
    case PM_JIT_CPP_TOK_NE_OP: return 7;
    case PM_JIT_CPP_TOK_LE_OP:
    case PM_JIT_CPP_TOK_GE_OP: return 6;
    case PM_JIT_CPP_TOK_LEFT_OP:
    case PM_JIT_CPP_TOK_RIGHT_OP: return 7;
    default: return -1;
    }
}

static pm_jit_cpp_ast_t *pm_jit_cpp_parse_binary(pm_jit_cpp_parser_t *p,
    int min_prec) {
    pm_jit_cpp_ast_t *lhs = pm_jit_cpp_parse_unary(p);
    if (lhs == NULL) return NULL;
    for (;;) {
        const pm_jit_cpp_token_t *t = pm_jit_cpp_cur(p);
        int prec = pm_jit_cpp_binop_prec(t);
        if (prec < 0 || prec < min_prec) break;
        pm_jit_cpp_advance(p);
        {
            pm_jit_cpp_ast_t *rhs = pm_jit_cpp_parse_binary(p, prec + 1);
            pm_jit_cpp_ast_t *bin;
            if (rhs == NULL) return NULL;
            bin = pm_jit_cpp_node(p, PM_JIT_CPP_AST_BINARY, t->line);
            if (bin == NULL) return NULL;
            bin->text = pm_jit_cpp_intern(p, t->text, t->text_len);
            if (bin->text == NULL) return NULL;
            bin->text_len = t->text_len;
            bin->op_kind = t->kind;
            if (pm_jit_cpp_add_kid(p, bin, lhs) != 0) return NULL;
            if (pm_jit_cpp_add_kid(p, bin, rhs) != 0) return NULL;
            lhs = bin;
        }
    }
    return lhs;
}

/* assignment: right-assoc, lowest precedence; ternary sits just above it */
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_assign(pm_jit_cpp_parser_t *p) {
    pm_jit_cpp_ast_t *lhs = pm_jit_cpp_parse_binary(p, 1);
    if (lhs == NULL) return NULL;
    if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '?')) {
        pm_jit_cpp_ast_t *tern = pm_jit_cpp_node(p, PM_JIT_CPP_AST_BINARY,
            pm_jit_cpp_cur(p)->line);
        pm_jit_cpp_ast_t *mid;
        pm_jit_cpp_ast_t *alt;
        if (tern == NULL) return NULL;
        tern->text = "?:";
        tern->text_len = 2;
        tern->op_kind = PM_JIT_CPP_TOK_PUNCT;
        pm_jit_cpp_advance(p);
        mid = pm_jit_cpp_parse_assign(p);
        if (mid == NULL) return NULL;
        if (pm_jit_cpp_expect_punct(p, ':') != 0) return NULL;
        alt = pm_jit_cpp_parse_assign(p);
        if (alt == NULL) return NULL;
        if (pm_jit_cpp_add_kid(p, tern, lhs) != 0) return NULL;
        if (pm_jit_cpp_add_kid(p, tern, mid) != 0) return NULL;
        if (pm_jit_cpp_add_kid(p, tern, alt) != 0) return NULL;
        lhs = tern;
    }
    {
        const pm_jit_cpp_token_t *t = pm_jit_cpp_cur(p);
        int is_assign = 0;
        if (pm_jit_cpp_is_punct(t, '=')) is_assign = 1;
        if (t->kind >= PM_JIT_CPP_TOK_ADD_ASSIGN
            && t->kind <= PM_JIT_CPP_TOK_SHIFT_RIGHT_ASSIGN) is_assign = 1;
        if (is_assign) {
            pm_jit_cpp_ast_t *rhs;
            pm_jit_cpp_ast_t *bin;
            pm_jit_cpp_advance(p);
            rhs = pm_jit_cpp_parse_assign(p);
            if (rhs == NULL) return NULL;
            bin = pm_jit_cpp_node(p, PM_JIT_CPP_AST_BINARY, t->line);
            if (bin == NULL) return NULL;
            bin->text = pm_jit_cpp_intern(p, t->text, t->text_len);
            if (bin->text == NULL) return NULL;
            bin->text_len = t->text_len;
            bin->op_kind = t->kind;
            if (pm_jit_cpp_add_kid(p, bin, lhs) != 0) return NULL;
            if (pm_jit_cpp_add_kid(p, bin, rhs) != 0) return NULL;
            return bin;
        }
    }
    return lhs;
}

static pm_jit_cpp_ast_t *pm_jit_cpp_parse_expr(pm_jit_cpp_parser_t *p) {
    return pm_jit_cpp_parse_assign(p);
}

/* compound statement */
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_block(pm_jit_cpp_parser_t *p) {
    const pm_jit_cpp_token_t *t = pm_jit_cpp_cur(p);
    pm_jit_cpp_ast_t *blk;
    if (!pm_jit_cpp_is_punct(t, '{')) {
        pm_jit_cpp_perr(p, "expected '{'");
        return NULL;
    }
    blk = pm_jit_cpp_node(p, PM_JIT_CPP_AST_COMPOUND, t->line);
    if (blk == NULL) return NULL;
    pm_jit_cpp_advance(p);
    while (!pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '}')) {
        pm_jit_cpp_ast_t *stmt = pm_jit_cpp_parse_stmt(p);
        if (stmt == NULL) return NULL;
        if (pm_jit_cpp_add_kid(p, blk, stmt) != 0) return NULL;
    }
    pm_jit_cpp_advance(p); /* } */
    return blk;
}

/* statement: block | if | for | while | return | declaration | expr; */
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_stmt(pm_jit_cpp_parser_t *p) {
    const pm_jit_cpp_token_t *t = pm_jit_cpp_cur(p);

    if (t->kind == PM_JIT_CPP_TOK_END) {
        pm_jit_cpp_perr(p, "unexpected end of input");
        return NULL;
    }
    if (pm_jit_cpp_is_punct(t, '{')) {
        return pm_jit_cpp_parse_block(p);
    }
    if (pm_jit_cpp_is_punct(t, ';')) {
        pm_jit_cpp_advance(p);
        {
            pm_jit_cpp_ast_t *n = pm_jit_cpp_node(p, PM_JIT_CPP_AST_EXPR_STMT, t->line);
            return n;
        }
    }
    if (pm_jit_cpp_is_kw(t, "if")) {
        pm_jit_cpp_ast_t *n = pm_jit_cpp_node(p, PM_JIT_CPP_AST_IF, t->line);
        pm_jit_cpp_advance(p);
        if (!pm_jit_cpp_eat_punct(p, '(')) {
            pm_jit_cpp_perr(p, "expected '(' after if");
            return NULL;
        }
        {
            pm_jit_cpp_ast_t *cond = pm_jit_cpp_parse_expr(p);
            if (cond == NULL) return NULL;
            if (pm_jit_cpp_add_kid(p, n, cond) != 0) return NULL;
            if (pm_jit_cpp_expect_punct(p, ')') != 0) return NULL;
        }
        {
            pm_jit_cpp_ast_t *then = pm_jit_cpp_parse_stmt(p);
            if (then == NULL) return NULL;
            if (pm_jit_cpp_add_kid(p, n, then) != 0) return NULL;
        }
        if (pm_jit_cpp_is_kw(pm_jit_cpp_cur(p), "else")) {
            pm_jit_cpp_advance(p);
            {
                pm_jit_cpp_ast_t *els = pm_jit_cpp_parse_stmt(p);
                if (els == NULL) return NULL;
                if (pm_jit_cpp_add_kid(p, n, els) != 0) return NULL;
            }
        }
        return n;
    }
    if (pm_jit_cpp_is_kw(t, "while")) {
        pm_jit_cpp_ast_t *n = pm_jit_cpp_node(p, PM_JIT_CPP_AST_WHILE, t->line);
        pm_jit_cpp_advance(p);
        if (!pm_jit_cpp_eat_punct(p, '(')) {
            pm_jit_cpp_perr(p, "expected '(' after while");
            return NULL;
        }
        {
            pm_jit_cpp_ast_t *cond = pm_jit_cpp_parse_expr(p);
            if (cond == NULL) return NULL;
            if (pm_jit_cpp_add_kid(p, n, cond) != 0) return NULL;
            if (pm_jit_cpp_expect_punct(p, ')') != 0) return NULL;
        }
        {
            pm_jit_cpp_ast_t *body = pm_jit_cpp_parse_stmt(p);
            if (body == NULL) return NULL;
            if (pm_jit_cpp_add_kid(p, n, body) != 0) return NULL;
        }
        return n;
    }
    if (pm_jit_cpp_is_kw(t, "for")) {
        pm_jit_cpp_ast_t *n = pm_jit_cpp_node(p, PM_JIT_CPP_AST_FOR, t->line);
        pm_jit_cpp_ast_t *init = NULL;
        pm_jit_cpp_ast_t *cond = NULL;
        pm_jit_cpp_ast_t *step = NULL;
        pm_jit_cpp_ast_t *body;
        pm_jit_cpp_advance(p);
        if (!pm_jit_cpp_eat_punct(p, '(')) {
            pm_jit_cpp_perr(p, "expected '(' after for");
            return NULL;
        }
        /* init: declaration (consumes its own ';') or expression + ';' */
        if (!pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ';')) {
            const pm_jit_cpp_token_t *d0 = pm_jit_cpp_cur(p);
            if (d0->kind == PM_JIT_CPP_TOK_KEYWORD && is_decl_keyword(d0->text, d0->text_len)) {
                init = pm_jit_cpp_parse_declaration_or_error(p);
                if (init == NULL) return NULL;
            } else {
                init = pm_jit_cpp_parse_expr(p);
                if (init == NULL) return NULL;
                if (pm_jit_cpp_expect_punct(p, ';') != 0) return NULL;
            }
        } else {
            pm_jit_cpp_advance(p);
        }
        if (!pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ';')) {
            cond = pm_jit_cpp_parse_expr(p);
            if (cond == NULL) return NULL;
        }
        if (pm_jit_cpp_expect_punct(p, ';') != 0) return NULL;
        if (!pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ')')) {
            step = pm_jit_cpp_parse_expr(p);
            if (step == NULL) return NULL;
        }
        if (pm_jit_cpp_expect_punct(p, ')') != 0) return NULL;
        body = pm_jit_cpp_parse_stmt(p);
        if (body == NULL) return NULL;
        /* fixed 4-kid shape: init/cond/step/body, NULL for an empty slot —
         * add_kid cannot store NULL, so empty slots ride as LITERAL nodes
         * with zero-length text. */
        if (init == NULL) {
            init = pm_jit_cpp_node(p, PM_JIT_CPP_AST_LITERAL, t->line);
            if (init == NULL) return NULL;
            init->text = "";
            init->text_len = 0;
        }
        if (cond == NULL) {
            cond = pm_jit_cpp_node(p, PM_JIT_CPP_AST_LITERAL, t->line);
            if (cond == NULL) return NULL;
            cond->text = "";
            cond->text_len = 0;
        }
        if (step == NULL) {
            step = pm_jit_cpp_node(p, PM_JIT_CPP_AST_LITERAL, t->line);
            if (step == NULL) return NULL;
            step->text = "";
            step->text_len = 0;
        }
        if (pm_jit_cpp_add_kid(p, n, init) != 0) return NULL;
        if (pm_jit_cpp_add_kid(p, n, cond) != 0) return NULL;
        if (pm_jit_cpp_add_kid(p, n, step) != 0) return NULL;
        if (pm_jit_cpp_add_kid(p, n, body) != 0) return NULL;
        return n;
    }
    if (pm_jit_cpp_is_kw(t, "return")) {
        pm_jit_cpp_ast_t *n = pm_jit_cpp_node(p, PM_JIT_CPP_AST_RETURN_STMT, t->line);
        pm_jit_cpp_advance(p);
        if (!pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ';')) {
            pm_jit_cpp_ast_t *v = pm_jit_cpp_parse_expr(p);
            if (v == NULL) return NULL;
            if (pm_jit_cpp_add_kid(p, n, v) != 0) return NULL;
        }
        if (pm_jit_cpp_expect_punct(p, ';') != 0) return NULL;
        return n;
    }
    if (pm_jit_cpp_is_kw(t, "break") || pm_jit_cpp_is_kw(t, "continue")) {
        pm_jit_cpp_ast_t *n = pm_jit_cpp_node(p, PM_JIT_CPP_AST_EXPR_STMT, t->line);
        pm_jit_cpp_advance(p);
        if (pm_jit_cpp_expect_punct(p, ';') != 0) return NULL;
        n->text = pm_jit_cpp_intern(p, t->text, t->text_len);
        if (n->text == NULL) return NULL;
        n->text_len = t->text_len;
        return n;
    }
    if (t->kind == PM_JIT_CPP_TOK_PP_DIRECTIVE) {
        /* directive inside a function body (local #define/#undef): verbatim
         * PP node, re-emitted in place by the lowerer. */
        pm_jit_cpp_ast_t *n = pm_jit_cpp_node(p, PM_JIT_CPP_AST_PP, t->line);
        if (n == NULL) return NULL;
        n->text = pm_jit_cpp_intern(p, t->text, t->text_len);
        if (n->text == NULL) return NULL;
        n->text_len = t->text_len;
        pm_jit_cpp_advance(p);
        return n;
    }
    if (pm_jit_cpp_is_kw(t, "do")) {
        pm_jit_cpp_ast_t *n = pm_jit_cpp_node(p, PM_JIT_CPP_AST_WHILE, t->line);
        pm_jit_cpp_ast_t *body;
        pm_jit_cpp_ast_t *cond;
        n->int_val = 1; /* do-while marker: body precedes the condition */
        pm_jit_cpp_advance(p);
        body = pm_jit_cpp_parse_stmt(p);
        if (body == NULL) return NULL;
        if (pm_jit_cpp_add_kid(p, n, body) != 0) return NULL;
        if (!pm_jit_cpp_is_kw(pm_jit_cpp_cur(p), "while")) {
            pm_jit_cpp_perr(p, "expected 'while' after do body");
            return NULL;
        }
        pm_jit_cpp_advance(p);
        if (!pm_jit_cpp_eat_punct(p, '(')) {
            pm_jit_cpp_perr(p, "expected '(' after while");
            return NULL;
        }
        cond = pm_jit_cpp_parse_expr(p);
        if (cond == NULL) return NULL;
        if (pm_jit_cpp_add_kid(p, n, cond) != 0) return NULL;
        if (pm_jit_cpp_expect_punct(p, ')') != 0) return NULL;
        if (pm_jit_cpp_expect_punct(p, ';') != 0) return NULL;
        return n;
    }
    if (pm_jit_cpp_is_kw(t, "switch")) {
        pm_jit_cpp_ast_t *n = pm_jit_cpp_node(p, PM_JIT_CPP_AST_SWITCH, t->line);
        pm_jit_cpp_advance(p);
        if (!pm_jit_cpp_eat_punct(p, '(')) {
            pm_jit_cpp_perr(p, "expected '(' after switch");
            return NULL;
        }
        {
            pm_jit_cpp_ast_t *disc = pm_jit_cpp_parse_expr(p);
            if (disc == NULL) return NULL;
            if (pm_jit_cpp_add_kid(p, n, disc) != 0) return NULL;
            if (pm_jit_cpp_expect_punct(p, ')') != 0) return NULL;
        }
        {
            pm_jit_cpp_ast_t *body = pm_jit_cpp_parse_block(p);
            if (body == NULL) return NULL;
            if (pm_jit_cpp_add_kid(p, n, body) != 0) return NULL;
        }
        return n;
    }
    if (pm_jit_cpp_is_kw(t, "case") || pm_jit_cpp_is_kw(t, "default")) {
        /* case/default may appear at the top level of a switch body block,
         * but parse_block calls parse_stmt per statement — the labels ride
         * here and the lowerer re-flattens them into the block. */
        pm_jit_cpp_ast_t *n = pm_jit_cpp_node(p,
            pm_jit_cpp_is_kw(t, "case") ? PM_JIT_CPP_AST_CASE
                : PM_JIT_CPP_AST_DEFAULT,
            t->line);
        pm_jit_cpp_advance(p);
        if (n->kind == PM_JIT_CPP_AST_CASE) {
            pm_jit_cpp_ast_t *v = pm_jit_cpp_parse_assign(p);
            if (v == NULL) return NULL;
            if (pm_jit_cpp_add_kid(p, n, v) != 0) return NULL;
        }
        if (pm_jit_cpp_expect_punct(p, ':') != 0) return NULL;
        return n;
    }
    if (pm_jit_cpp_is_kw(t, "goto")) {
        pm_jit_cpp_ast_t *n = pm_jit_cpp_node(p, PM_JIT_CPP_AST_GOTO, t->line);
        const pm_jit_cpp_token_t *lab;
        pm_jit_cpp_advance(p);
        lab = pm_jit_cpp_cur(p);
        if (lab->kind != PM_JIT_CPP_TOK_IDENT) {
            pm_jit_cpp_perr(p, "expected label after goto");
            return NULL;
        }
        n->text = pm_jit_cpp_intern(p, lab->text, lab->text_len);
        if (n->text == NULL) return NULL;
        n->text_len = lab->text_len;
        pm_jit_cpp_advance(p);
        if (pm_jit_cpp_expect_punct(p, ';') != 0) return NULL;
        return n;
    }
    if (pm_jit_cpp_is_kw(t, "try") || pm_jit_cpp_is_kw(t, "throw")) {
        pm_jit_cpp_perr(p, "unsupported: exception statement");
        return NULL;
    }
    /* label: IDENT ':' — must be tried before declaration parsing so the
     * label name is not eaten as a declaration name. */
    if (t->kind == PM_JIT_CPP_TOK_IDENT
        && pm_jit_cpp_peek(p, 1)->kind == PM_JIT_CPP_TOK_PUNCT
        && pm_jit_cpp_peek(p, 1)->text_len == 1
        && pm_jit_cpp_peek(p, 1)->text[0] == ':'
        && !(pm_jit_cpp_peek(p, 2)->kind == PM_JIT_CPP_TOK_PUNCT
            && pm_jit_cpp_peek(p, 2)->text_len == 1
            && pm_jit_cpp_peek(p, 2)->text[0] == ':')) {
        pm_jit_cpp_ast_t *n = pm_jit_cpp_node(p, PM_JIT_CPP_AST_LABEL, t->line);
        pm_jit_cpp_ast_t *stmt;
        n->text = pm_jit_cpp_intern(p, t->text, t->text_len);
        if (n->text == NULL) return NULL;
        n->text_len = t->text_len;
        pm_jit_cpp_advance(p); /* name */
        pm_jit_cpp_advance(p); /* : */
        stmt = pm_jit_cpp_parse_stmt(p);
        if (stmt == NULL) return NULL;
        if (pm_jit_cpp_add_kid(p, n, stmt) != 0) return NULL;
        return n;
    }
    /* declaration or expression statement */
    {
        pm_jit_cpp_ast_t *d = pm_jit_cpp_parse_declaration_or_error(p);
        if (d != NULL) return d;
        if (p->failed) return NULL;
        /* not a declaration → expression statement */
        {
            pm_jit_cpp_ast_t *e = pm_jit_cpp_parse_expr(p);
            if (e == NULL) return NULL;
            /* ';' normally follows. A missing ';' is tolerated ONLY when
             * the next token is a preprocessor directive or block end: the
             * source relies on macro expansion to supply the ';'
             * (X-macro invocations like TOKENS(NAME) with the ';' living in
             * the macro body). The lowerer re-emits a ';' so the generated
             * C is complete for TCC. */
            if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ';')) {
                pm_jit_cpp_advance(p);
            } else if (pm_jit_cpp_cur(p)->kind != PM_JIT_CPP_TOK_PP_DIRECTIVE
                && !pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '}')) {
                if (pm_jit_cpp_expect_punct(p, ';') != 0) return NULL;
            }
            {
                pm_jit_cpp_ast_t *n = pm_jit_cpp_node(p, PM_JIT_CPP_AST_EXPR_STMT,
                    e->line);
                if (n == NULL) return NULL;
                if (pm_jit_cpp_add_kid(p, n, e) != 0) return NULL;
                return n;
            }
        }
    }
}

/* declaration := type name ('=' expr | '(' args ')' | braces)? (',' name ...)? ';'
 * returns NULL with failed=0 when the tokens are not a declaration */
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_declaration_or_error(
    pm_jit_cpp_parser_t *p) {
    const pm_jit_cpp_token_t *t = pm_jit_cpp_cur(p);
    pm_jit_cpp_ast_t *ty;
    pm_jit_cpp_ast_t *decl;
    const pm_jit_cpp_token_t *name;

    if (!(t->kind == PM_JIT_CPP_TOK_IDENT
        || (t->kind == PM_JIT_CPP_TOK_KEYWORD && is_decl_keyword(t->text, t->text_len)))) {
        return NULL;
    }
    /* must be followed by a name (possibly behind * / & / const declarators
     * and further type keywords: int *p, const char *const *k, unsigned
     * long v) to be a declaration */
    {
        uint32_t k = 1;
        const pm_jit_cpp_token_t *nxt = pm_jit_cpp_peek(p, k);
        while (pm_jit_cpp_is_punct(nxt, '*')
            || pm_jit_cpp_is_punct(nxt, '&')
            || (nxt->kind == PM_JIT_CPP_TOK_KEYWORD
                && is_decl_keyword(nxt->text, nxt->text_len))) {
            k++;
            nxt = pm_jit_cpp_peek(p, k);
        }
        if (!(nxt->kind == PM_JIT_CPP_TOK_IDENT
            || nxt->kind == PM_JIT_CPP_TOK_DOUBLE_COLON)) {
            return NULL;
        }
    }
    ty = pm_jit_cpp_parse_type(p);
    if (ty == NULL) return NULL;
    name = pm_jit_cpp_cur(p);
    /* parse_type may have eaten the declarator name as the type's final
     * identifier (storage specifiers ahead of a builtin base: "static
     * const char *const builtins" parses type="static const char * const
     * builtins"). When no declarator follows, split the trailing identifier
     * off the type text and use it as the name. */
    if (name->kind != PM_JIT_CPP_TOK_IDENT
        && ty->text_len > 0) {
        size_t tl = ty->text_len;
        size_t end = tl;
        size_t start;
        while (end > 0 && (ty->text[end - 1] == ' ' || ty->text[end - 1] == '*'
            || ty->text[end - 1] == '\t')) end--;
        start = end;
        while (start > 0) {
            char ch = ty->text[start - 1];
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                || (ch >= '0' && ch <= '9') || ch == '_') {
                start--;
                continue;
            }
            break;
        }
        if (start < end) {
            /* candidate name [start,end); the char before must be a
             * separator (space/star), and it must not be a keyword. */
            size_t nlen = end - start;
            char nb2[128];
            const pm_jit_cpp_token_t *split;
            if (nlen < sizeof(nb2)
                && (start == 0 || ty->text[start - 1] == ' '
                    || ty->text[start - 1] == '*')) {
                memcpy(nb2, ty->text + start, nlen);
                nb2[nlen] = '\0';
                if (!is_decl_keyword(nb2, nlen)) {
                    /* find the token to rewind to: the name must be a real
                     * token — rewind pos so cur() is the name again. */
                    uint32_t back = p->pos;
                    while (back > 0) {
                        const pm_jit_cpp_token_t *bt = &p->toks[back - 1];
                        if (bt->kind == PM_JIT_CPP_TOK_IDENT
                            && bt->text_len == nlen
                            && memcmp(bt->text, nb2, nlen) == 0) {
                            p->pos = back - 1;
                            break;
                        }
                        back--;
                    }
                    split = pm_jit_cpp_cur(p);
                    if (split->kind == PM_JIT_CPP_TOK_IDENT
                        && split->text_len == nlen
                        && memcmp(split->text, nb2, nlen) == 0) {
                        /* truncate the type text before the name */
                        {
                            char tb[512];
                            size_t keep = start;
                            if (keep >= sizeof(tb)) keep = sizeof(tb) - 1;
                            memcpy(tb, ty->text, keep);
                            while (keep > 0 && (tb[keep - 1] == ' '
                                || tb[keep - 1] == '*')) keep--;
                            tb[keep] = '\0';
                            ty->text = pm_jit_cpp_intern(p, tb, keep);
                            if (ty->text == NULL) return NULL;
                            ty->text_len = keep;
                        }
                        name = split;
                    }
                }
            }
        }
    }
    if (name->kind != PM_JIT_CPP_TOK_IDENT) {
        pm_jit_cpp_perr(p, "expected declarator name");
        return NULL;
    }
    decl = pm_jit_cpp_node(p, PM_JIT_CPP_AST_DECL_STMT, name->line);
    if (decl == NULL) return NULL;
    decl->text = pm_jit_cpp_intern(p, name->text, name->text_len);
    if (decl->text == NULL) return NULL;
    decl->text_len = name->text_len;
    if (pm_jit_cpp_add_kid(p, decl, ty) != 0) return NULL;
    pm_jit_cpp_advance(p);
    /* array declarator after the name: Type name[N] — fold [N] into the
     * type text (locals: char buf[256]). */
    if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '[')) {
        char nbuf[512];
        size_t nlen = ty->text_len;
        uint32_t start = p->pos;
        uint32_t depth = 0;
        uint32_t k;
        do {
            const pm_jit_cpp_token_t *u = pm_jit_cpp_cur(p);
            if (u->kind == PM_JIT_CPP_TOK_END) {
                pm_jit_cpp_perr(p, "unterminated array declarator");
                return NULL;
            }
            if (pm_jit_cpp_is_punct(u, '[')) depth++;
            if (pm_jit_cpp_is_punct(u, ']')) depth--;
            pm_jit_cpp_advance(p);
        } while (depth > 0);
        if (nlen >= sizeof(nbuf)) {
            pm_jit_cpp_perr(p, "type name too long");
            return NULL;
        }
        memcpy(nbuf, ty->text, nlen);
        for (k = start; k < p->pos; k++) {
            if (nlen + p->toks[k].text_len + 2 >= sizeof(nbuf)) {
                pm_jit_cpp_perr(p, "type name too long");
                return NULL;
            }
            if (tok_text_needs_space(p->toks[k].kind)) nbuf[nlen++] = ' ';
            memcpy(nbuf + nlen, p->toks[k].text, p->toks[k].text_len);
            nlen += p->toks[k].text_len;
        }
        nbuf[nlen] = '\0';
        ty->text = pm_jit_cpp_intern(p, nbuf, nlen);
        if (ty->text == NULL) return NULL;
        ty->text_len = nlen;
    }
    /* initializer */
    if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '=')) {
        pm_jit_cpp_advance(p);
        if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '{')) {
            /* brace initializer after '=': aggregate init — capture the
             * balanced braces as raw text so the lowerer re-emits it. */
            uint32_t start = p->pos;
            uint32_t depth = 0;
            pm_jit_cpp_ast_t *init;
            do {
                const pm_jit_cpp_token_t *u = pm_jit_cpp_cur(p);
                if (u->kind == PM_JIT_CPP_TOK_END) {
                    pm_jit_cpp_perr(p, "unterminated initializer");
                    return NULL;
                }
                if (pm_jit_cpp_is_punct(u, '{')) depth++;
                if (pm_jit_cpp_is_punct(u, '}')) depth--;
                pm_jit_cpp_advance(p);
            } while (depth > 0);
            init = pm_jit_cpp_node(p, PM_JIT_CPP_AST_LITERAL, t->line);
            if (init == NULL) return NULL;
            init->text = pm_jit_cpp_raw_text(p->arena, p->toks, start, p->pos,
                &init->text_len);
            if (init->text == NULL) return NULL;
            if (pm_jit_cpp_add_kid(p, decl, init) != 0) return NULL;
        } else {
            pm_jit_cpp_ast_t *init = pm_jit_cpp_parse_assign(p);
            if (init == NULL) return NULL;
            if (pm_jit_cpp_add_kid(p, decl, init) != 0) return NULL;
        }
    } else if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '(')) {
        /* constructor-style init: T name(args). Only for plain class types —
         * a template type (std::unique_ptr<LoudBox>) is never a ctor call;
         * its paren init is a value init, so it lowers as an assignment of
         * the single arg. The args are real exprs — the initializer may
         * itself contain parens (p(new LoudBox(8))). */
        int is_plain_type = 1;
        {
            uint32_t k;
            for (k = 0; k < ty->text_len; k++) {
                char ch = ty->text[k];
                if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                    || (ch >= '0' && ch <= '9') || ch == '_'
                    || ch == ':' || ch == ' ' || ch == '*')) {
                    is_plain_type = 0;
                    break;
                }
            }
        }
        if (is_plain_type) {
            pm_jit_cpp_ast_t *call = pm_jit_cpp_node(p, PM_JIT_CPP_AST_CALL,
                pm_jit_cpp_cur(p)->line);
            pm_jit_cpp_ast_t *callee;
            if (call == NULL) return NULL;
            callee = pm_jit_cpp_node(p, PM_JIT_CPP_AST_NAME, t->line);
            if (callee == NULL) return NULL;
            callee->text = pm_jit_cpp_intern(p, ty->text, ty->text_len);
            if (callee->text == NULL) return NULL;
            callee->text_len = ty->text_len;
            if (pm_jit_cpp_add_kid(p, call, callee) != 0) return NULL;
            pm_jit_cpp_advance(p); /* ( */
            if (!pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ')')) {
                for (;;) {
                    pm_jit_cpp_ast_t *arg = pm_jit_cpp_parse_expr(p);
                    if (arg == NULL) return NULL;
                    if (pm_jit_cpp_add_kid(p, call, arg) != 0) return NULL;
                    if (pm_jit_cpp_eat_punct(p, ',')) continue;
                    break;
                }
            }
            if (pm_jit_cpp_expect_punct(p, ')') != 0) return NULL;
            if (pm_jit_cpp_add_kid(p, decl, call) != 0) return NULL;
        } else {
            /* value init for template handles: take the single arg as the
             * initializer expression (unique_ptr<T> p(new T(...)) -> p = T_new(...)) */
            pm_jit_cpp_ast_t *arg = NULL;
            pm_jit_cpp_advance(p); /* ( */
            if (!pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ')')) {
                arg = pm_jit_cpp_parse_expr(p);
                if (arg == NULL) return NULL;
            }
            if (pm_jit_cpp_expect_punct(p, ')') != 0) return NULL;
            if (arg != NULL) {
                if (pm_jit_cpp_add_kid(p, decl, arg) != 0) return NULL;
            }
        }
    } else if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '{')) {
        /* brace init: aggregate init — capture the balanced braces as raw
         * text so the lowerer re-emits it (= { ... }). */
        uint32_t start = p->pos;
        uint32_t depth = 0;
        do {
            const pm_jit_cpp_token_t *u = pm_jit_cpp_cur(p);
            if (u->kind == PM_JIT_CPP_TOK_END) {
                pm_jit_cpp_perr(p, "unterminated initializer");
                return NULL;
            }
            if (pm_jit_cpp_is_punct(u, '{')) depth++;
            if (pm_jit_cpp_is_punct(u, '}')) depth--;
            pm_jit_cpp_advance(p);
        } while (depth > 0);
        {
            pm_jit_cpp_ast_t *init = pm_jit_cpp_node(p, PM_JIT_CPP_AST_LITERAL,
                t->line);
            if (init == NULL) return NULL;
            init->text = pm_jit_cpp_raw_text(p->arena, p->toks, start, p->pos,
                &init->text_len);
            if (init->text == NULL) return NULL;
            if (pm_jit_cpp_add_kid(p, decl, init) != 0) return NULL;
        }
    }
    /* multiple declarators: int a, b; — each gets its own DECL_STMT; they
     * ride in a DECL_GROUP so lowering emits them inline, no new scope. */
    if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ',')) {
        pm_jit_cpp_ast_t *grp = pm_jit_cpp_node(p, PM_JIT_CPP_AST_DECL_GROUP,
            decl->line);
        if (grp == NULL) return NULL;
        if (pm_jit_cpp_add_kid(p, grp, decl) != 0) return NULL;
        while (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ',')) {
            const pm_jit_cpp_token_t *n2;
            pm_jit_cpp_ast_t *d2;
            pm_jit_cpp_advance(p);
            n2 = pm_jit_cpp_cur(p);
            if (n2->kind != PM_JIT_CPP_TOK_IDENT) {
                pm_jit_cpp_perr(p, "expected declarator name");
                return NULL;
            }
            d2 = pm_jit_cpp_node(p, PM_JIT_CPP_AST_DECL_STMT, n2->line);
            if (d2 == NULL) return NULL;
            d2->text = pm_jit_cpp_intern(p, n2->text, n2->text_len);
            if (d2->text == NULL) return NULL;
            d2->text_len = n2->text_len;
            {
                /* fresh TYPE node per declarator: the array fold below
                 * rewrites the type text and must not touch the shared ty */
                pm_jit_cpp_ast_t *ty2 = pm_jit_cpp_node(p,
                    PM_JIT_CPP_AST_TYPE, n2->line);
                if (ty2 == NULL) return NULL;
                ty2->text = ty->text;
                ty2->text_len = ty->text_len;
                if (pm_jit_cpp_add_kid(p, d2, ty2) != 0) return NULL;
            }
            pm_jit_cpp_advance(p);
            /* array suffix on the later declarator: fold into its type copy */
            if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '[')) {
                char nbuf[512];
                size_t nlen = ty->text_len;
                uint32_t start = p->pos;
                uint32_t depth = 0;
                uint32_t k;
                do {
                    const pm_jit_cpp_token_t *u = pm_jit_cpp_cur(p);
                    if (u->kind == PM_JIT_CPP_TOK_END) {
                        pm_jit_cpp_perr(p, "unterminated array declarator");
                        return NULL;
                    }
                    if (pm_jit_cpp_is_punct(u, '[')) depth++;
                    if (pm_jit_cpp_is_punct(u, ']')) depth--;
                    pm_jit_cpp_advance(p);
                } while (depth > 0);
                if (nlen >= sizeof(nbuf)) {
                    pm_jit_cpp_perr(p, "type name too long");
                    return NULL;
                }
                memcpy(nbuf, ty->text, nlen);
                for (k = start; k < p->pos; k++) {
                    if (nlen + p->toks[k].text_len + 2 >= sizeof(nbuf)) {
                        pm_jit_cpp_perr(p, "type name too long");
                        return NULL;
                    }
                    if (tok_text_needs_space(p->toks[k].kind)) nbuf[nlen++] = ' ';
                    memcpy(nbuf + nlen, p->toks[k].text, p->toks[k].text_len);
                    nlen += p->toks[k].text_len;
                }
                nbuf[nlen] = '\0';
                d2->kids[0]->text = pm_jit_cpp_intern(p, nbuf, nlen);
                if (d2->kids[0]->text == NULL) return NULL;
                d2->kids[0]->text_len = nlen;
            }
            if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '=')) {
                pm_jit_cpp_advance(p);
                if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '{')) {
                    uint32_t start = p->pos;
                    uint32_t depth = 0;
                    pm_jit_cpp_ast_t *init;
                    do {
                        const pm_jit_cpp_token_t *u = pm_jit_cpp_cur(p);
                        if (u->kind == PM_JIT_CPP_TOK_END) {
                            pm_jit_cpp_perr(p, "unterminated initializer");
                            return NULL;
                        }
                        if (pm_jit_cpp_is_punct(u, '{')) depth++;
                        if (pm_jit_cpp_is_punct(u, '}')) depth--;
                        pm_jit_cpp_advance(p);
                    } while (depth > 0);
                    init = pm_jit_cpp_node(p, PM_JIT_CPP_AST_LITERAL, n2->line);
                    if (init == NULL) return NULL;
                    init->text = pm_jit_cpp_raw_text(p->arena, p->toks, start,
                        p->pos, &init->text_len);
                    if (init->text == NULL) return NULL;
                    if (pm_jit_cpp_add_kid(p, d2, init) != 0) return NULL;
                } else {
                    pm_jit_cpp_ast_t *init = pm_jit_cpp_parse_assign(p);
                    if (init == NULL) return NULL;
                    if (pm_jit_cpp_add_kid(p, d2, init) != 0) return NULL;
                }
            }
            if (pm_jit_cpp_add_kid(p, grp, d2) != 0) return NULL;
        }
        if (pm_jit_cpp_expect_punct(p, ';') != 0) return NULL;
        return grp;
    }
    if (pm_jit_cpp_expect_punct(p, ';') != 0) return NULL;
    return decl;
}

static int is_decl_keyword(const char *s, size_t n) {
    static const char *const kws[] = {
        "const", "unsigned", "signed", "long", "short", "void", "bool", "char",
        "int", "float", "double", "auto", "wchar_t", "char16_t", "char32_t",
        "struct", "class", "enum", "typename", "static", "constexpr", "extern",
        "inline", "virtual", "explicit", "friend", "typedef", "mutable",
        "volatile", "register", "thread_local", NULL
    };
    const char *const *k;
    for (k = kws; *k != NULL; k++) {
        if (strlen(*k) == n && memcmp(*k, s, n) == 0) return 1;
    }
    return 0;
}

/* storage/qualifier keywords that may precede struct/union/enum in a C
 * aggregate declarator (static const struct { ... } x;) */
static int pm_jit_cpp_cur_is_specifier(pm_jit_cpp_parser_t *p) {
    const pm_jit_cpp_token_t *t = pm_jit_cpp_cur(p);
    return t->kind == PM_JIT_CPP_TOK_KEYWORD
        && (pm_jit_cpp_is_kw(t, "static") || pm_jit_cpp_is_kw(t, "extern")
            || pm_jit_cpp_is_kw(t, "const") || pm_jit_cpp_is_kw(t, "volatile")
            || pm_jit_cpp_is_kw(t, "register")
            || pm_jit_cpp_is_kw(t, "thread_local")
            || pm_jit_cpp_is_kw(t, "inline"));
}

static int tok_text_needs_space(pm_jit_cpp_tok_kind k) {
    return k == PM_JIT_CPP_TOK_KEYWORD || k == PM_JIT_CPP_TOK_IDENT;
}

int32_t pm_metal_jit_cpp_parse(pm_util_mem_arena_t *arena,
    const pm_jit_cpp_toklist_t *toks,
    pm_jit_cpp_ast_t **unit_out, char *errbuf, size_t errbuf_len) {
    pm_jit_cpp_parser_t p;
    pm_jit_cpp_ast_t *unit;

    if (arena == NULL || toks == NULL || unit_out == NULL) {
        return pm_jit_cpp_err(errbuf, errbuf_len, "parse: bad args", 0);
    }
    memset(&p, 0, sizeof(p));
    p.toks = toks->toks;
    p.n = toks->n_toks;
    p.pos = 0;
    p.errbuf = errbuf;
    p.errbuf_len = errbuf_len;
    p.arena = arena;
    if (errbuf != NULL && errbuf_len > 0) errbuf[0] = '\0';

    unit = pm_jit_cpp_node(&p, PM_JIT_CPP_AST_TRANSLATION_UNIT, 1);
    if (unit == NULL) return -1;

    while (pm_jit_cpp_cur(&p)->kind != PM_JIT_CPP_TOK_END) {
        const pm_jit_cpp_token_t *t = pm_jit_cpp_cur(&p);
        pm_jit_cpp_ast_t *decl = NULL;

        if (t->kind == PM_JIT_CPP_TOK_PP_DIRECTIVE) {
            /* verbatim preprocessor line — rides as a PP node; the lowerer
             * re-emits it at the same position for TCC's cpp. */
            pm_jit_cpp_ast_t *pp = pm_jit_cpp_node(&p, PM_JIT_CPP_AST_PP,
                t->line);
            if (pp == NULL) return -1;
            pp->text = pm_jit_cpp_intern(&p, t->text, t->text_len);
            if (pp->text == NULL) return -1;
            pp->text_len = t->text_len;
            pm_jit_cpp_advance(&p);
            if (pm_jit_cpp_add_kid(&p, unit, pp) != 0) return -1;
            continue;
        }
        /* macro-invocation statement at TU scope: IDENT ( ... ) ; — the
         * registration macros (PM_MOD_EXPORT_C and friends). Captured raw
         * and re-emitted verbatim; TCC's cpp expands them. */
        if (t->kind == PM_JIT_CPP_TOK_IDENT
            && pm_jit_cpp_is_punct(pm_jit_cpp_peek(&p, 1), '(')) {
            uint32_t start = p.pos;
            uint32_t depth = 0;
            pm_jit_cpp_ast_t *raw;
            int closed = 0;
            while (pm_jit_cpp_cur(&p)->kind != PM_JIT_CPP_TOK_END) {
                if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(&p), '(')) {
                    depth++;
                } else if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(&p), ')')) {
                    depth--;
                    if (depth == 0) {
                        pm_jit_cpp_advance(&p);
                        closed = 1;
                        break;
                    }
                }
                pm_jit_cpp_advance(&p);
            }
            if (!closed) {
                pm_jit_cpp_perr(&p, "unterminated macro invocation");
                return -1;
            }
            /* optional ; */
            if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(&p), ';')) {
                pm_jit_cpp_advance(&p);
            }
            raw = pm_jit_cpp_node(&p, PM_JIT_CPP_AST_PP, t->line);
            if (raw == NULL) return -1;
            raw->text = pm_jit_cpp_raw_text(p.arena, p.toks, start, p.pos,
                &raw->text_len);
            if (raw->text == NULL) return -1;
            if (pm_jit_cpp_add_kid(&p, unit, raw) != 0) return -1;
            continue;
        }
        if (pm_jit_cpp_is_kw(t, "template")) {
            decl = pm_jit_cpp_parse_template_decl(&p);
        } else if (pm_jit_cpp_is_kw(t, "class")) {
            decl = pm_jit_cpp_parse_class_decl(&p);
        } else if (pm_jit_cpp_is_kw(t, "struct") || pm_jit_cpp_is_kw(t, "union")
            || pm_jit_cpp_is_kw(t, "enum")) {
            /* plain class def: struct Name { ... } — a name directly before
             * '{' (or ':'). Anything else (anonymous struct with a
             * declarator, enum with trailing comma list) is C data and
             * rides through parse_function_or_var's raw capture. */
            const pm_jit_cpp_token_t *nx = pm_jit_cpp_peek(&p, 1);
            int is_class_def = 0;
            if (pm_jit_cpp_is_punct(nx, '{')) {
                is_class_def = 0; /* anonymous aggregate declarator */
            } else if (nx->kind == PM_JIT_CPP_TOK_IDENT) {
                const pm_jit_cpp_token_t *nx2 = pm_jit_cpp_peek(&p, 2);
                if (pm_jit_cpp_is_punct(nx2, '{') || pm_jit_cpp_is_punct(nx2, ':')) {
                    is_class_def = 1;
                }
            }
            if (pm_jit_cpp_is_kw(t, "enum")) {
                /* enum Name { ... } — C data (constant table), not a class:
                 * always capture raw via parse_function_or_var, which also
                 * handles plain "enum X {...};" typedef-style trailing. */
                is_class_def = 0;
            }
            if (is_class_def) {
                decl = pm_jit_cpp_parse_class_decl(&p);
            } else {
                decl = pm_jit_cpp_parse_function_or_var(&p);
            }
        } else if (pm_jit_cpp_is_kw(t, "using")) {
            decl = pm_jit_cpp_parse_using_decl(&p);
        } else if (pm_jit_cpp_is_kw(t, "typedef")) {
            /* C typedef — passthrough. Captured as balanced raw text so the
             * lowerer re-emits it verbatim (TCC understands it fully). The
             * scan tracks brace depth: struct/union/enum bodies contain ';'
             * that must not terminate the typedef. */
            uint32_t start = p.pos;
            uint32_t depth = 0;
            pm_jit_cpp_ast_t *td;
            while (pm_jit_cpp_cur(&p)->kind != PM_JIT_CPP_TOK_END) {
                if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(&p), '{')) depth++;
                if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(&p), '}')) {
                    if (depth == 0) break;
                    depth--;
                }
                pm_jit_cpp_advance(&p);
                if (depth == 0 && pm_jit_cpp_is_punct(pm_jit_cpp_cur(&p), ';')) {
                    break;
                }
            }
            if (pm_jit_cpp_cur(&p)->kind == PM_JIT_CPP_TOK_END
                || depth != 0) {
                pm_jit_cpp_perr(&p, "unterminated typedef");
                return -1;
            }
            pm_jit_cpp_advance(&p); /* ; */
            td = pm_jit_cpp_node(&p, PM_JIT_CPP_AST_TYPEDEF, t->line);
            if (td == NULL) return -1;
            td->text = pm_jit_cpp_raw_text(p.arena, p.toks, start, p.pos - 1,
                &td->text_len);
            if (td->text == NULL) return -1;
            decl = td;
        } else if (pm_jit_cpp_is_kw(t, "namespace")) {
            pm_jit_cpp_perr(&p, "unsupported: namespace definition");
            return -1;
        } else {
            /* function or variable declaration */
            decl = pm_jit_cpp_parse_function_or_var(&p);
        }
        if (decl == NULL) return -1;
        if (pm_jit_cpp_add_kid(&p, unit, decl) != 0) return -1;
    }
    *unit_out = unit;
    return 0;
}

/* top-level: template<params> decl. The parameter names ride as leading
 * NAME kids (one per template parameter); the templated entity is the LAST
 * kid. Substitution during lowering pairs the names with the use-site args. */
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_template_decl(pm_jit_cpp_parser_t *p) {
    const pm_jit_cpp_token_t *t = pm_jit_cpp_cur(p); /* template */
    pm_jit_cpp_ast_t *decl;
    pm_jit_cpp_advance(p);
    if (!pm_jit_cpp_eat_punct(p, '<')) {
        pm_jit_cpp_perr(p, "expected '<' after template");
        return NULL;
    }
    p->n_tpl_params = 0;
    {
        uint32_t depth = 1;
        while (depth > 0) {
            const pm_jit_cpp_token_t *u = pm_jit_cpp_cur(p);
            if (u->kind == PM_JIT_CPP_TOK_END) {
                pm_jit_cpp_perr(p, "unterminated template parameter list");
                return NULL;
            }
            /* record the name of a type parameter: typename T / class T.
             * Only at depth 1 (not inside a nested arg) and only the IDENT
             * right after the kind keyword — non-type params (int N) and
             * defaults (= U) are not template-substitutable here. */
            if (depth == 1 && (pm_jit_cpp_is_kw(u, "typename")
                || pm_jit_cpp_is_kw(u, "class"))
                && pm_jit_cpp_peek(p, 1)->kind == PM_JIT_CPP_TOK_IDENT
                && p->n_tpl_params < 8) {
                const pm_jit_cpp_token_t *nx = pm_jit_cpp_peek(p, 1);
                pm_jit_cpp_ast_t *pn = pm_jit_cpp_node(p,
                    PM_JIT_CPP_AST_NAME, u->line);
                if (pn == NULL) return NULL;
                pn->text = pm_jit_cpp_intern(p, nx->text, nx->text_len);
                if (pn->text == NULL) return NULL;
                pn->text_len = nx->text_len;
                p->tpl_params[p->n_tpl_params++] = pn;
            }
            if (pm_jit_cpp_is_punct(u, '<')) depth++;
            if (pm_jit_cpp_is_punct(u, '>')) depth--;
            pm_jit_cpp_advance(p);
        }
    }
    /* the templated entity */
    {
        const pm_jit_cpp_token_t *u = pm_jit_cpp_cur(p);
        pm_jit_cpp_ast_t *inner;
        if (pm_jit_cpp_is_kw(u, "class") || pm_jit_cpp_is_kw(u, "struct")) {
            inner = pm_jit_cpp_parse_class_decl(p);
        } else {
            inner = pm_jit_cpp_parse_function_or_var(p);
        }
        if (inner == NULL) return NULL;
        decl = pm_jit_cpp_node(p, PM_JIT_CPP_AST_TEMPLATE_DECL, t->line);
        if (decl == NULL) return NULL;
        /* params first (from the stash), then the entity last */
        {
            uint32_t k;
            for (k = 0; k < p->n_tpl_params; k++) {
                if (pm_jit_cpp_add_kid(p, decl, p->tpl_params[k]) != 0) {
                    return NULL;
                }
            }
        }
        if (pm_jit_cpp_add_kid(p, decl, inner) != 0) return NULL;
        return decl;
    }
}

/* class/struct Name [: bases] { members } ; */
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_class_decl(pm_jit_cpp_parser_t *p) {
    const pm_jit_cpp_token_t *t = pm_jit_cpp_cur(p); /* class|struct */
    pm_jit_cpp_ast_t *cls;
    const pm_jit_cpp_token_t *name;
    const char *kwd;

    kwd = pm_jit_cpp_is_kw(t, "class") ? "class" : "struct";
    pm_jit_cpp_advance(p);
    /* skip attributes/final */
    while (pm_jit_cpp_cur(p)->kind == PM_JIT_CPP_TOK_IDENT
        && (strncmp(pm_jit_cpp_cur(p)->text, "final", 5) == 0)) {
        pm_jit_cpp_advance(p);
    }
    name = pm_jit_cpp_cur(p);
    if (name->kind != PM_JIT_CPP_TOK_IDENT) {
        pm_jit_cpp_perr(p, "expected class name");
        return NULL;
    }
    cls = pm_jit_cpp_node(p, PM_JIT_CPP_AST_CLASS, name->line);
    if (cls == NULL) return NULL;
    cls->text = pm_jit_cpp_intern(p, name->text, name->text_len);
    if (cls->text == NULL) return NULL;
    cls->text_len = name->text_len;
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s", kwd);
        /* store keyword in a kid-less way: reuse int_val as a class/struct tag */
        cls->int_val = pm_jit_cpp_is_kw(t, "class") ? 1 : 0;
    }
    pm_jit_cpp_advance(p);
    /* base clause */
    if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ':')) {
        pm_jit_cpp_advance(p);
        for (;;) {
            while (pm_jit_cpp_is_kw(pm_jit_cpp_cur(p), "public")
                || pm_jit_cpp_is_kw(pm_jit_cpp_cur(p), "private")
                || pm_jit_cpp_is_kw(pm_jit_cpp_cur(p), "protected")
                || pm_jit_cpp_is_kw(pm_jit_cpp_cur(p), "virtual")) {
                pm_jit_cpp_advance(p);
            }
            {
                const pm_jit_cpp_token_t *base = pm_jit_cpp_cur(p);
                pm_jit_cpp_ast_t *bt;
                char bbuf[160];
                size_t blen;
                if (base->kind != PM_JIT_CPP_TOK_IDENT
                    && base->kind != PM_JIT_CPP_TOK_KEYWORD) {
                    pm_jit_cpp_perr(p, "expected base class name");
                    return NULL;
                }
                blen = base->text_len;
                if (blen >= sizeof(bbuf)) {
                    pm_jit_cpp_perr(p, "base class name too long");
                    return NULL;
                }
                memcpy(bbuf, base->text, blen);
                bt = pm_jit_cpp_node(p, PM_JIT_CPP_AST_TYPE, base->line);
                if (bt == NULL) return NULL;
                pm_jit_cpp_advance(p);
                /* template base Foo<int> — the <args> ride in the TYPE text
                 * so lowering can map Foo<int> -> Foo_int */
                if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '<')) {
                    uint32_t depth = 0;
                    do {
                        const pm_jit_cpp_token_t *u = pm_jit_cpp_cur(p);
                        if (u->kind == PM_JIT_CPP_TOK_END) {
                            pm_jit_cpp_perr(p, "unterminated base template args");
                            return NULL;
                        }
                        if (blen + u->text_len + 1 >= sizeof(bbuf)) {
                            pm_jit_cpp_perr(p, "base class name too long");
                            return NULL;
                        }
                        if (tok_text_needs_space(u->kind)) bbuf[blen++] = ' ';
                        memcpy(bbuf + blen, u->text, u->text_len);
                        blen += u->text_len;
                        if (pm_jit_cpp_is_punct(u, '<')) depth++;
                        if (pm_jit_cpp_is_punct(u, '>')) depth--;
                        pm_jit_cpp_advance(p);
                    } while (depth > 0);
                }
                bbuf[blen] = '\0';
                bt->text = pm_jit_cpp_intern(p, bbuf, blen);
                if (bt->text == NULL) return NULL;
                bt->text_len = blen;
                if (pm_jit_cpp_add_kid(p, cls, bt) != 0) return NULL;
            }
            if (pm_jit_cpp_eat_punct(p, ',')) continue;
            break;
        }
    }
    if (!pm_jit_cpp_eat_punct(p, '{')) {
        pm_jit_cpp_perr(p, "expected '{' to open class body");
        return NULL;
    }
    /* member declarations */
    while (!pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '}')) {
        const pm_jit_cpp_token_t *u = pm_jit_cpp_cur(p);
        if (u->kind == PM_JIT_CPP_TOK_END) {
            pm_jit_cpp_perr(p, "unterminated class body");
            return NULL;
        }
        if (pm_jit_cpp_is_kw(u, "public") || pm_jit_cpp_is_kw(u, "private")
            || pm_jit_cpp_is_kw(u, "protected")) {
            pm_jit_cpp_ast_t *acc = pm_jit_cpp_node(p,
                PM_JIT_CPP_AST_ACCESS_SPEC, u->line);
            if (acc == NULL) return NULL;
            acc->text = pm_jit_cpp_intern(p, u->text, u->text_len);
            if (acc->text == NULL) return NULL;
            acc->text_len = u->text_len;
            if (pm_jit_cpp_add_kid(p, cls, acc) != 0) return NULL;
            pm_jit_cpp_advance(p);
            if (pm_jit_cpp_expect_punct(p, ':') != 0) return NULL;
            continue;
        }
        if (pm_jit_cpp_is_kw(u, "template")) {
            pm_jit_cpp_ast_t *m = pm_jit_cpp_parse_template_decl(p);
            if (m == NULL) return NULL;
            if (pm_jit_cpp_add_kid(p, cls, m) != 0) return NULL;
            continue;
        }
        if (pm_jit_cpp_is_kw(u, "friend")) {
            pm_jit_cpp_perr(p, "unsupported: friend declaration");
            return NULL;
        }
        if (pm_jit_cpp_is_kw(u, "using")) {
            pm_jit_cpp_ast_t *m = pm_jit_cpp_parse_using_decl(p);
            if (m == NULL) return NULL;
            if (pm_jit_cpp_add_kid(p, cls, m) != 0) return NULL;
            continue;
        }
        if (pm_jit_cpp_is_kw(u, "class") || pm_jit_cpp_is_kw(u, "struct")) {
            pm_jit_cpp_ast_t *nested = pm_jit_cpp_parse_class_decl(p);
            if (nested == NULL) return NULL;
            if (pm_jit_cpp_add_kid(p, cls, nested) != 0) return NULL;
            continue;
        }
        {
            pm_jit_cpp_ast_t *m = pm_jit_cpp_parse_member_or_error(p);
            if (m == NULL) return p->failed ? NULL : NULL;
            if (pm_jit_cpp_add_kid(p, cls, m) != 0) return NULL;
        }
    }
    pm_jit_cpp_advance(p); /* } */
    if (pm_jit_cpp_expect_punct(p, ';') != 0) return NULL;
    return cls;
}

/* using namespace x; / using T = U; / using std::vector; */
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_using_decl(pm_jit_cpp_parser_t *p) {
    const pm_jit_cpp_token_t *t = pm_jit_cpp_cur(p); /* using */
    pm_jit_cpp_ast_t *u;
    pm_jit_cpp_advance(p);
    u = pm_jit_cpp_node(p, PM_JIT_CPP_AST_USING, t->line);
    if (u == NULL) return NULL;
    if (pm_jit_cpp_is_kw(pm_jit_cpp_cur(p), "namespace")) {
        pm_jit_cpp_advance(p);
        u->int_val = 1; /* using-namespace */
    }
    /* consume tokens up to ';' building the name text; the name is a run of
     * ident / '::' / template-arg tokens (using Box<int>::get;) */
    {
        char buf[256];
        size_t len = 0;
        while (!pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ';')) {
            const pm_jit_cpp_token_t *k = pm_jit_cpp_cur(p);
            if (k->kind == PM_JIT_CPP_TOK_END) {
                pm_jit_cpp_perr(p, "unterminated using declaration");
                return NULL;
            }
            if (k->kind != PM_JIT_CPP_TOK_IDENT
                && k->kind != PM_JIT_CPP_TOK_KEYWORD
                && k->kind != PM_JIT_CPP_TOK_DOUBLE_COLON
                && !pm_jit_cpp_is_punct(k, '<')
                && !pm_jit_cpp_is_punct(k, '>')
                && !pm_jit_cpp_is_punct(k, ',')
                && !pm_jit_cpp_is_punct(k, '*')
                && !pm_jit_cpp_is_punct(k, '&')) {
                pm_jit_cpp_perr_got(p, "unsupported token in using name");
                return NULL;
            }
            if (len + k->text_len + 1 >= sizeof(buf)) {
                pm_jit_cpp_perr(p, "using name too long");
                return NULL;
            }
            memcpy(buf + len, k->text, k->text_len);
            len += k->text_len;
            pm_jit_cpp_advance(p);
        }
        buf[len] = '\0';
        u->text = pm_jit_cpp_intern(p, buf, len);
        if (u->text == NULL) return NULL;
        u->text_len = len;
    }
    if (pm_jit_cpp_expect_punct(p, ';') != 0) return NULL;
    return u;
}

/* function or variable at translation-unit scope */
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_function_or_var(
    pm_jit_cpp_parser_t *p) {
    const pm_jit_cpp_token_t *t = pm_jit_cpp_cur(p);
    pm_jit_cpp_ast_t *ty;
    const pm_jit_cpp_token_t *name;

    /* skip leading specifiers we can re-emit verbatim */
    /* C aggregate declarator: struct/union/enum { body } name...; —
     * captured as balanced raw text and re-emitted verbatim (anonymous
     * struct variable with aggregate initializer, TCC handles it).
     * Storage/qualifier keywords may precede the struct/union/enum. */
    {
        uint32_t scan = p->pos;
        while (pm_jit_cpp_cur_is_specifier(p)) {
            pm_jit_cpp_advance(p);
            scan = p->pos;
        }
        if (pm_jit_cpp_is_kw(pm_jit_cpp_cur(p), "struct")
            || pm_jit_cpp_is_kw(pm_jit_cpp_cur(p), "union")
            || pm_jit_cpp_is_kw(pm_jit_cpp_cur(p), "enum")) {
            const pm_jit_cpp_token_t *nx = pm_jit_cpp_peek(p, 1);
            /* named aggregate: struct/union/enum NAME { ... } — also C data
             * (enum tables, struct defs with a declarator). Capture raw. */
            if (nx->kind == PM_JIT_CPP_TOK_IDENT) {
                const pm_jit_cpp_token_t *nx2 = pm_jit_cpp_peek(p, 2);
                if (pm_jit_cpp_is_punct(nx2, '{')) {
                    nx = nx2; /* fall into the brace capture below */
                }
            }
            if (pm_jit_cpp_is_punct(nx, '{')) {
                uint32_t start = p->pos;
                uint32_t depth = 0;
                pm_jit_cpp_ast_t *raw;
                while (pm_jit_cpp_cur(p)->kind != PM_JIT_CPP_TOK_END) {
                    if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '{')) depth++;
                    if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '}')) {
                        depth--;
                        if (depth == 0) {
                            pm_jit_cpp_advance(p);
                            break;
                        }
                    }
                    pm_jit_cpp_advance(p);
                }
                /* declarators + initializer until ';' at depth 0 — the raw
                 * span stops BEFORE the ';' so the emitter supplies it. */
                depth = 0;
                while (pm_jit_cpp_cur(p)->kind != PM_JIT_CPP_TOK_END) {
                    if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '{')) depth++;
                    if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '}')) depth--;
                    if (depth == 0
                        && pm_jit_cpp_is_punct(pm_jit_cpp_peek(p, 1), ';')) {
                        pm_jit_cpp_advance(p);
                        break;
                    }
                    pm_jit_cpp_advance(p);
                }
                if (pm_jit_cpp_cur(p)->kind != PM_JIT_CPP_TOK_END
                    && !pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ';')) {
                    pm_jit_cpp_perr(p, "unterminated aggregate declarator");
                    return NULL;
                }
                {
                    uint32_t span_end = p->pos;
                    pm_jit_cpp_advance(p); /* ; */
                    raw = pm_jit_cpp_node(p, PM_JIT_CPP_AST_TYPEDEF, t->line);
                    if (raw == NULL) return NULL;
                    raw->text = pm_jit_cpp_raw_text(p->arena, p->toks, start,
                        span_end, &raw->text_len);
                    if (raw->text == NULL) return NULL;
                    return raw;
                }
            }
        }
        p->pos = scan;
    }
    ty = pm_jit_cpp_parse_type(p);
    if (ty == NULL) return NULL;
    name = pm_jit_cpp_cur(p);
    if (name->kind != PM_JIT_CPP_TOK_IDENT) {
        pm_jit_cpp_perr(p, "expected declaration name");
        return NULL;
    }
    pm_jit_cpp_advance(p);
    /* array declarator after the name: Type name[N] — fold [N] into the
     * type text so the emitter writes Type[N] name. */
    if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '[')) {
        char nbuf[512];
        size_t nlen = ty->text_len;
        uint32_t start = p->pos;
        uint32_t depth = 0;
        uint32_t k;
        do {
            const pm_jit_cpp_token_t *u = pm_jit_cpp_cur(p);
            if (u->kind == PM_JIT_CPP_TOK_END) {
                pm_jit_cpp_perr(p, "unterminated array declarator");
                return NULL;
            }
            if (pm_jit_cpp_is_punct(u, '[')) depth++;
            if (pm_jit_cpp_is_punct(u, ']')) depth--;
            pm_jit_cpp_advance(p);
        } while (depth > 0);
        if (nlen >= sizeof(nbuf)) {
            pm_jit_cpp_perr(p, "type name too long");
            return NULL;
        }
        memcpy(nbuf, ty->text, nlen);
        for (k = start; k < p->pos; k++) {
            if (nlen + p->toks[k].text_len + 2 >= sizeof(nbuf)) {
                pm_jit_cpp_perr(p, "type name too long");
                return NULL;
            }
            if (tok_text_needs_space(p->toks[k].kind)) nbuf[nlen++] = ' ';
            memcpy(nbuf + nlen, p->toks[k].text, p->toks[k].text_len);
            nlen += p->toks[k].text_len;
        }
        nbuf[nlen] = '\0';
        ty->text = pm_jit_cpp_intern(p, nbuf, nlen);
        if (ty->text == NULL) return NULL;
        ty->text_len = nlen;
    }
    if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '(')) {
        /* function */
        pm_jit_cpp_ast_t *fn = pm_jit_cpp_node(p, PM_JIT_CPP_AST_FUNCTION,
            name->line);
        if (fn == NULL) return NULL;
        fn->text = pm_jit_cpp_intern(p, name->text, name->text_len);
        if (fn->text == NULL) return NULL;
        fn->text_len = name->text_len;
        if (pm_jit_cpp_add_kid(p, fn, ty) != 0) return NULL;
        /* parameters */
        pm_jit_cpp_advance(p); /* ( */
        while (!pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ')')) {
            const pm_jit_cpp_token_t *u = pm_jit_cpp_cur(p);
            pm_jit_cpp_ast_t *param_ty;
            const pm_jit_cpp_token_t *param_name;
            pm_jit_cpp_ast_t *param;
            if (u->kind == PM_JIT_CPP_TOK_END) {
                pm_jit_cpp_perr(p, "unterminated parameter list");
                return NULL;
            }
            if (pm_jit_cpp_is_kw(u, "virtual")) { pm_jit_cpp_advance(p); continue; }
            if (pm_jit_cpp_is_punct(u, ',') ) { pm_jit_cpp_advance(p); continue; }
            param_ty = pm_jit_cpp_parse_type(p);
            if (param_ty == NULL) return NULL;
            param_name = pm_jit_cpp_cur(p);
            param = pm_jit_cpp_node(p, PM_JIT_CPP_AST_PARAM, u->line);
            if (param == NULL) return NULL;
            if (param_name->kind == PM_JIT_CPP_TOK_IDENT) {
                param->text = pm_jit_cpp_intern(p, param_name->text,
                    param_name->text_len);
                if (param->text == NULL) return NULL;
                param->text_len = param_name->text_len;
                pm_jit_cpp_advance(p);
                /* default arg */
                if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '=')) {
                    pm_jit_cpp_advance(p);
                    {
                        pm_jit_cpp_ast_t *dv = pm_jit_cpp_parse_assign(p);
                        if (dv == NULL) return NULL;
                        if (pm_jit_cpp_add_kid(p, param, dv) != 0) return NULL;
                    }
                }
            } else {
                param->text = "";
                param->text_len = 0;
            }
            if (pm_jit_cpp_add_kid(p, param, param_ty) != 0) return NULL;
            if (pm_jit_cpp_add_kid(p, fn, param) != 0) return NULL;
        }
        pm_jit_cpp_advance(p); /* ) */
        /* trailing specifiers: const, noexcept, override, final */
        while (pm_jit_cpp_is_kw(pm_jit_cpp_cur(p), "const")
            || pm_jit_cpp_is_kw(pm_jit_cpp_cur(p), "noexcept")
            || pm_jit_cpp_is_kw(pm_jit_cpp_cur(p), "override")
            || pm_jit_cpp_is_kw(pm_jit_cpp_cur(p), "final")
            || (pm_jit_cpp_cur(p)->kind == PM_JIT_CPP_TOK_IDENT
                && strncmp(pm_jit_cpp_cur(p)->text, "override", 8) == 0)) {
            pm_jit_cpp_advance(p);
        }
        /* pure virtual: = 0 */
        if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '=')) {
            const pm_jit_cpp_token_t *pv = pm_jit_cpp_peek(p, 1);
            if (pv->kind == PM_JIT_CPP_TOK_INT_LITERAL && pv->text_len == 1
                && pv->text[0] == '0') {
                fn->int_val = 1; /* pure virtual marker */
                pm_jit_cpp_advance(p);
                pm_jit_cpp_advance(p);
            }
        }
        /* body: ; (pure decl) or { ... } */
        if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ';')) {
            pm_jit_cpp_advance(p);
            return fn;
        }
        {
            pm_jit_cpp_ast_t *body = pm_jit_cpp_parse_block(p);
            if (body == NULL) return NULL;
            if (pm_jit_cpp_add_kid(p, fn, body) != 0) return NULL;
        }
        return fn;
    }
    /* variable */
    {
        pm_jit_cpp_ast_t *v = pm_jit_cpp_node(p, PM_JIT_CPP_AST_VAR, name->line);
        if (v == NULL) return NULL;
        v->text = pm_jit_cpp_intern(p, name->text, name->text_len);
        if (v->text == NULL) return NULL;
        v->text_len = name->text_len;
        if (pm_jit_cpp_add_kid(p, v, ty) != 0) return NULL;
        if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '=')) {
            pm_jit_cpp_advance(p);
            if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '{')) {
                /* brace initializer: aggregate init (arrays, structs) —
                 * captured as balanced raw text and re-emitted verbatim. */
                uint32_t start = p->pos;
                uint32_t depth = 0;
                pm_jit_cpp_ast_t *init;
                while (pm_jit_cpp_cur(p)->kind != PM_JIT_CPP_TOK_END) {
                    if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '{')) depth++;
                    if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '}')) depth--;
                    pm_jit_cpp_advance(p);
                    if (depth == 0) break;
                }
                if (depth != 0) {
                    pm_jit_cpp_perr(p, "unterminated brace initializer");
                    return NULL;
                }
                init = pm_jit_cpp_node(p, PM_JIT_CPP_AST_LITERAL, name->line);
                if (init == NULL) return NULL;
                init->text = pm_jit_cpp_raw_text(p->arena, p->toks, start,
                    p->pos, &init->text_len);
                if (init->text == NULL) return NULL;
                if (pm_jit_cpp_add_kid(p, v, init) != 0) return NULL;
            } else {
                pm_jit_cpp_ast_t *init = pm_jit_cpp_parse_assign(p);
                if (init == NULL) return NULL;
                if (pm_jit_cpp_add_kid(p, v, init) != 0) return NULL;
            }
        }
        if (pm_jit_cpp_expect_punct(p, ';') != 0) return NULL;
        return v;
    }
}

/* class member: same as function_or_var but allows ctor/dtor and virtual */
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_member_or_error(
    pm_jit_cpp_parser_t *p) {
    /* ctor/dtor: ClassName(...) — the type token IS the name.  Leading
     * specifiers (virtual ~Box()) must be skipped before either check. */
    const pm_jit_cpp_token_t *t;
    while (pm_jit_cpp_is_kw(pm_jit_cpp_cur(p), "static")
        || pm_jit_cpp_is_kw(pm_jit_cpp_cur(p), "inline")
        || pm_jit_cpp_is_kw(pm_jit_cpp_cur(p), "virtual")
        || pm_jit_cpp_is_kw(pm_jit_cpp_cur(p), "explicit")) {
        pm_jit_cpp_advance(p);
    }
    t = pm_jit_cpp_cur(p);
    if (t->kind == PM_JIT_CPP_TOK_IDENT
        && pm_jit_cpp_peek(p, 1)->kind == PM_JIT_CPP_TOK_PUNCT
        && pm_jit_cpp_is_punct(pm_jit_cpp_peek(p, 1), '(')) {
        pm_jit_cpp_ast_t *fn = pm_jit_cpp_node(p, PM_JIT_CPP_AST_FUNCTION, t->line);
        if (fn == NULL) return NULL;
        fn->text = pm_jit_cpp_intern(p, t->text, t->text_len);
        if (fn->text == NULL) return NULL;
        fn->text_len = t->text_len;
        fn->int_val = 2; /* ctor marker: name == class name, no return type */
        pm_jit_cpp_advance(p); /* past name → cur is the opening '(' */
        pm_jit_cpp_advance(p); /* past '(' → depth counts only inner parens */
        if (!pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ')')) {
            for (;;) {
                const pm_jit_cpp_token_t *u = pm_jit_cpp_cur(p);
                pm_jit_cpp_ast_t *param;
                pm_jit_cpp_ast_t *pty;
                const pm_jit_cpp_token_t *pn;
                if (u->kind == PM_JIT_CPP_TOK_END) {
                    pm_jit_cpp_perr(p, "unterminated ctor parameter list");
                    return NULL;
                }
                if (pm_jit_cpp_is_punct(u, ',')) { pm_jit_cpp_advance(p); continue; }
                if (pm_jit_cpp_is_punct(u, ')')) break;
                /* ctor params are the subset: [const] type [&] name. Record
                 * the type text plus the trailing name; default args are
                 * accepted and recorded so the lowering can replay them. */
                pty = pm_jit_cpp_parse_type(p);
                if (pty == NULL) return NULL;
                pn = pm_jit_cpp_cur(p);
                param = pm_jit_cpp_node(p, PM_JIT_CPP_AST_PARAM, u->line);
                if (param == NULL) return NULL;
                if (pn->kind == PM_JIT_CPP_TOK_IDENT) {
                    param->text = pm_jit_cpp_intern(p, pn->text, pn->text_len);
                    if (param->text == NULL) return NULL;
                    param->text_len = pn->text_len;
                    pm_jit_cpp_advance(p);
                    if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '=')) {
                        pm_jit_cpp_advance(p);
                        {
                            pm_jit_cpp_ast_t *dv = pm_jit_cpp_parse_assign(p);
                            if (dv == NULL) return NULL;
                            if (pm_jit_cpp_add_kid(p, param, dv) != 0) return NULL;
                        }
                    }
                } else {
                    param->text = "";
                    param->text_len = 0;
                }
                if (pm_jit_cpp_add_kid(p, param, pty) != 0) return NULL;
                if (pm_jit_cpp_add_kid(p, fn, param) != 0) return NULL;
                if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ',')) {
                    pm_jit_cpp_advance(p);
                    continue;
                }
                break;
            }
        }
        if (!pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ')')) {
            /* the balanced scan above stopped somewhere unexpected */
            {
                uint32_t depth = 1;
                while (depth > 0) {
                    const pm_jit_cpp_token_t *u = pm_jit_cpp_cur(p);
                    if (u->kind == PM_JIT_CPP_TOK_END) {
                        pm_jit_cpp_perr(p, "unterminated ctor parameter list");
                        return NULL;
                    }
                    if (pm_jit_cpp_is_punct(u, '(')) depth++;
                    if (pm_jit_cpp_is_punct(u, ')')) depth--;
                    pm_jit_cpp_advance(p);
                }
            }
        } else {
            pm_jit_cpp_advance(p);
        }
        /* ctor init list : name(args), name{args}, ... — each entry is an
         * INIT_DECL kid: text = member/base name, kids = the arg exprs. */
        if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ':')) {
            pm_jit_cpp_advance(p);
            for (;;) {
                const pm_jit_cpp_token_t *mn = pm_jit_cpp_cur(p);
                pm_jit_cpp_ast_t *ini;
                if (mn->kind != PM_JIT_CPP_TOK_IDENT) {
                    pm_jit_cpp_perr(p, "expected member name in ctor init list");
                    return NULL;
                }
                ini = pm_jit_cpp_node(p, PM_JIT_CPP_AST_INIT_DECL, mn->line);
                if (ini == NULL) return NULL;
                ini->text = pm_jit_cpp_intern(p, mn->text, mn->text_len);
                if (ini->text == NULL) return NULL;
                ini->text_len = mn->text_len;
                pm_jit_cpp_advance(p);
                /* template base name Box<int>(...) — the <...> rides in the
                 * name text, matching how parse_type spells template ids. */
                if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '<')) {
                    char buf[128];
                    size_t len = ini->text_len;
                    uint32_t depth = 0;
                    memcpy(buf, ini->text, len);
                    do {
                        const pm_jit_cpp_token_t *u = pm_jit_cpp_cur(p);
                        if (u->kind == PM_JIT_CPP_TOK_END) {
                            pm_jit_cpp_perr(p, "unterminated base template args");
                            return NULL;
                        }
                        if (len + u->text_len + 1 >= sizeof(buf)) {
                            pm_jit_cpp_perr(p, "ctor init name too long");
                            return NULL;
                        }
                        if (tok_text_needs_space(u->kind)) buf[len++] = ' ';
                        memcpy(buf + len, u->text, u->text_len);
                        len += u->text_len;
                        if (pm_jit_cpp_is_punct(u, '<')) depth++;
                        if (pm_jit_cpp_is_punct(u, '>')) depth--;
                        pm_jit_cpp_advance(p);
                    } while (depth > 0);
                    buf[len] = '\0';
                    ini->text = pm_jit_cpp_intern(p, buf, len);
                    if (ini->text == NULL) return NULL;
                    ini->text_len = len;
                }
                if (!pm_jit_cpp_eat_punct(p, '(')) {
                    pm_jit_cpp_perr(p, "expected '(' in ctor init entry");
                    return NULL;
                }
                if (!pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ')')) {
                    for (;;) {
                        pm_jit_cpp_ast_t *arg = pm_jit_cpp_parse_expr(p);
                        if (arg == NULL) return NULL;
                        if (pm_jit_cpp_add_kid(p, ini, arg) != 0) return NULL;
                        if (pm_jit_cpp_eat_punct(p, ',')) continue;
                        break;
                    }
                }
                if (pm_jit_cpp_expect_punct(p, ')') != 0) return NULL;
                if (pm_jit_cpp_add_kid(p, fn, ini) != 0) return NULL;
                if (pm_jit_cpp_eat_punct(p, ',')) continue;
                break;
            }
        }
        if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '{')) {
            pm_jit_cpp_ast_t *body = pm_jit_cpp_parse_block(p);
            if (body == NULL) return NULL;
            if (pm_jit_cpp_add_kid(p, fn, body) != 0) return NULL;
        } else if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ';')) {
            pm_jit_cpp_advance(p);
        }
        return fn;
    }
    /* dtor: ~Name() */
    if (pm_jit_cpp_is_punct(t, '~')
        && pm_jit_cpp_peek(p, 1)->kind == PM_JIT_CPP_TOK_IDENT
        && pm_jit_cpp_is_punct(pm_jit_cpp_peek(p, 2), '(')) {
        pm_jit_cpp_ast_t *fn;
        const pm_jit_cpp_token_t *nm = pm_jit_cpp_peek(p, 1);
        pm_jit_cpp_advance(p);
        pm_jit_cpp_advance(p);
        fn = pm_jit_cpp_node(p, PM_JIT_CPP_AST_FUNCTION, nm->line);
        if (fn == NULL) return NULL;
        {
            char buf[128];
            snprintf(buf, sizeof(buf), "~%.*s", (int)nm->text_len, nm->text);
            fn->text = pm_jit_cpp_intern(p, buf, strlen(buf));
            if (fn->text == NULL) return NULL;
            fn->text_len = strlen(fn->text);
        }
        /* ( ) body */
        if (pm_jit_cpp_eat_punct(p, '(')) {
            while (!pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ')')) {
                if (pm_jit_cpp_cur(p)->kind == PM_JIT_CPP_TOK_END) {
                    pm_jit_cpp_perr(p, "unterminated dtor parameter list");
                    return NULL;
                }
                pm_jit_cpp_advance(p);
            }
            pm_jit_cpp_advance(p);
        }
        while (pm_jit_cpp_is_kw(pm_jit_cpp_cur(p), "noexcept")
            || pm_jit_cpp_is_kw(pm_jit_cpp_cur(p), "override")
            || pm_jit_cpp_is_kw(pm_jit_cpp_cur(p), "final")) {
            pm_jit_cpp_advance(p);
        }
        if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '{')) {
            pm_jit_cpp_ast_t *body = pm_jit_cpp_parse_block(p);
            if (body == NULL) return NULL;
            if (pm_jit_cpp_add_kid(p, fn, body) != 0) return NULL;
        } else if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ';')) {
            pm_jit_cpp_advance(p);
        }
        return fn;
    }
    /* operator overload: type operator+ ( ...) */
    if (pm_jit_cpp_is_kw(pm_jit_cpp_peek(p, 0), "operator")) {
        pm_jit_cpp_perr(p, "unsupported: operator overload");
        return NULL;
    }
    return pm_jit_cpp_parse_function_or_var(p);
}

/*------------------ AST dump ------------------*/

static const char *pm_jit_cpp_ast_name(pm_jit_cpp_ast_kind k) {
    switch (k) {
    case PM_JIT_CPP_AST_TRANSLATION_UNIT: return "translation-unit";
    case PM_JIT_CPP_AST_FUNCTION: return "function";
    case PM_JIT_CPP_AST_CLASS: return "class";
    case PM_JIT_CPP_AST_VAR: return "var";
    case PM_JIT_CPP_AST_USING: return "using";
    case PM_JIT_CPP_AST_TYPE: return "type";
    case PM_JIT_CPP_AST_PARAM: return "param";
    case PM_JIT_CPP_AST_RETURN: return "return";
    case PM_JIT_CPP_AST_IF: return "if";
    case PM_JIT_CPP_AST_FOR: return "for";
    case PM_JIT_CPP_AST_WHILE: return "while";
    case PM_JIT_CPP_AST_RETURN_STMT: return "return-stmt";
    case PM_JIT_CPP_AST_EXPR_STMT: return "expr-stmt";
    case PM_JIT_CPP_AST_DECL_STMT: return "decl-stmt";
    case PM_JIT_CPP_AST_COMPOUND: return "compound";
    case PM_JIT_CPP_AST_CALL: return "call";
    case PM_JIT_CPP_AST_BINARY: return "binary";
    case PM_JIT_CPP_AST_UNARY: return "unary";
    case PM_JIT_CPP_AST_LITERAL: return "literal";
    case PM_JIT_CPP_AST_NAME: return "name";
    case PM_JIT_CPP_AST_INIT_DECL: return "init-decl";
    case PM_JIT_CPP_AST_MEMBER: return "member";
    case PM_JIT_CPP_AST_TEMPLATE_DECL: return "template-decl";
    case PM_JIT_CPP_AST_TEMPLATE_REF: return "template-ref";
    case PM_JIT_CPP_AST_VIRTUAL_METHOD: return "virtual-method";
    case PM_JIT_CPP_AST_ACCESS_SPEC: return "access-spec";
    case PM_JIT_CPP_AST_NEW_EXPR: return "new";
    case PM_JIT_CPP_AST_DELETE_EXPR: return "delete";
    case PM_JIT_CPP_AST_REF_QUALIFIER: return "ref-qualifier";
    case PM_JIT_CPP_AST_SWITCH: return "switch";
    case PM_JIT_CPP_AST_CASE: return "case";
    case PM_JIT_CPP_AST_DEFAULT: return "default";
    case PM_JIT_CPP_AST_GOTO: return "goto";
    case PM_JIT_CPP_AST_LABEL: return "label";
    case PM_JIT_CPP_AST_TYPEDEF: return "typedef";
    case PM_JIT_CPP_AST_PP: return "pp";
    case PM_JIT_CPP_AST_CAST: return "cast";
    case PM_JIT_CPP_AST_COMMA: return "comma";
    case PM_JIT_CPP_AST_DECL_GROUP: return "decl-group";
    }
    return "?";
}

typedef struct {
    char *out;
    size_t cap;
    size_t len;
    int overflow;
    char *errbuf;
    size_t errbuf_len;
} pm_jit_cpp_dump_t;

static void pm_jit_cpp_emit(pm_jit_cpp_dump_t *d, const char *s) {
    size_t n = strlen(s);
    if (d->len + n + 1 > d->cap) { d->overflow = 1; return; }
    memcpy(d->out + d->len, s, n);
    d->len += n;
}

static void pm_jit_cpp_emitn(pm_jit_cpp_dump_t *d, const char *s, size_t n) {
    if (d->len + n + 1 > d->cap) { d->overflow = 1; return; }
    memcpy(d->out + d->len, s, n);
    d->len += n;
}

static void pm_jit_cpp_dump_rec(pm_jit_cpp_dump_t *d,
    const pm_jit_cpp_ast_t *n, uint32_t depth) {
    uint32_t i;
    if (n == NULL || d->overflow) return;
    for (i = 0; i < depth; i++) pm_jit_cpp_emit(d, "  ");
    pm_jit_cpp_emit(d, "(");
    pm_jit_cpp_emit(d, pm_jit_cpp_ast_name(n->kind));
    if (n->text != NULL && n->text_len > 0) {
        pm_jit_cpp_emit(d, " ");
        pm_jit_cpp_emitn(d, n->text, n->text_len);
    }
    if (n->kind == PM_JIT_CPP_AST_CLASS) {
        pm_jit_cpp_emit(d, n->int_val == 1 ? " class" : " struct");
    }
    if (n->kind == PM_JIT_CPP_AST_FUNCTION && n->int_val == 1) {
        pm_jit_cpp_emit(d, " pure-virtual");
    }
    pm_jit_cpp_emit(d, ")");
    pm_jit_cpp_emit(d, "\n");
    for (i = 0; i < n->n_kids; i++) {
        pm_jit_cpp_dump_rec(d, n->kids[i], depth + 1);
    }
}

int32_t pm_metal_jit_cpp_ast_dump(const pm_jit_cpp_ast_t *ast,
    char *out, size_t out_cap, char *errbuf, size_t errbuf_len) {
    pm_jit_cpp_dump_t d;
    if (ast == NULL || out == NULL || out_cap == 0) {
        return pm_jit_cpp_err(errbuf, errbuf_len, "ast_dump: bad args", 0);
    }
    d.out = out;
    d.cap = out_cap;
    d.len = 0;
    d.overflow = 0;
    d.errbuf = errbuf;
    d.errbuf_len = errbuf_len;
    pm_jit_cpp_dump_rec(&d, ast, 0);
    if (d.overflow) {
        return pm_jit_cpp_err(errbuf, errbuf_len, "ast_dump: buffer short", 0);
    }
    out[d.len] = '\0';
    return (int32_t)d.len;
}

/*------------------ Lower: C++ AST -> C source ------------------*/

/*------------------ Lower: class table ------------------
 *
 * A translation unit lowers through a class table: every class (and every
 * template-class instantiation) is registered with its fields, methods,
 * ctors, dtor and virtual order before any body lowers, so method-call
 * lowering can resolve the receiver's class and virtual dispatch can pick
 * the vtable slot. The table is arena-owned; ordering is source order.
 *
 * Mangling (stable, readable, collision-checked):
 *   class C            -> struct C, methods C_m, ctor C_ctor, dtor C_dtor
 *   template Box<int>  -> struct Box_int, methods Box_int_m
 *   virtual            -> struct C_vt (vtable type), C_vtable (the instance)
 */

#define PM_CPPX_MAX_CLASSES 64
#define PM_CPPX_MAX_MEMBERS 64
#define PM_CPPX_MAX_VT 32
#define PM_CPPX_MAX_INSTS 128
#define PM_CPPX_NAME_MAX 160
#define PM_CPPX_MAX_PARAMS 8
#define PM_CPPX_MAX_LOCALS 64

typedef struct {
    const char *name;     /* source name ("Box") */
    const char *cname;    /* C name ("Box" or "Box_int") */
    const pm_jit_cpp_ast_t *node;    /* CLASS node (post-substitution clone
                                      * for template instantiations) */
    const char *base_cname;          /* base class C name or NULL */
    const char *base_field;          /* struct field name for the base */
    uint32_t n_virtual;              /* number of virtual slots (own+base) */
    const char *vt_names[PM_CPPX_MAX_VT]; /* method C names, vtable order */
    const char *vt_methods[PM_CPPX_MAX_VT]; /* plain method names (slot ids) */
    const char *vt_rets[PM_CPPX_MAX_VT];   /* slot return types */
    int vt_pure[PM_CPPX_MAX_VT];     /* slot is an unimplemented pure virtual */
    int has_dtor;
} pm_cppx_class_t;

/* template instantiation registry: Box<int> -> Box_int */
typedef struct {
    const char *tpl_name;    /* "Box" (NULL = template-function instantiation) */
    const char *args;        /* "int" */
    const char *cname;       /* "Box_int" */
} pm_cppx_inst_t;

typedef struct {
    pm_util_mem_arena_t *arena;
    pm_cppx_class_t classes[PM_CPPX_MAX_CLASSES];
    uint32_t n_classes;
    pm_cppx_inst_t insts[PM_CPPX_MAX_INSTS];
    uint32_t n_insts;
    /* template decls first, then instantiated template-function clones
     * (kind FUNCTION) parked at the tail for the free-function pass */
    const pm_jit_cpp_ast_t *templates[PM_CPPX_MAX_INSTS];
    uint32_t n_templates;
    /* active method's class (for this->x and virtual dispatch) */
    const pm_cppx_class_t *cur_class;
    /* local variable type map: name -> class cname (NULL entry = unknown) */
    struct {
        const char *var;
        const char *cls;
        int is_pointer; /* declared as Class* p */
    } locals[PM_CPPX_MAX_LOCALS];
    uint32_t n_locals;
} pm_cppx_table_t;

/* Growth-doubling output buffer, arena-backed (mirrors the rsx lowerer's
 * Out). Rendering never fails on capacity — only on arena exhaustion,
 * which surfaces as a NULL put and stops the walk. */
typedef struct {
    pm_util_mem_arena_t *arena;
    char *p;
    size_t len;
    size_t cap;
    int dead;              /* arena exhausted: stop emitting, fail at end */
    pm_cppx_table_t *tbl;  /* class/instance registry for this unit */
} pm_jit_cpp_low_t;

static int pm_jit_cpp_low_reserve(pm_jit_cpp_low_t *l, size_t extra) {
    if (l->dead) return 0;
    if (l->len + extra + 1 > l->cap) {
        size_t ncap = l->cap == 0 ? 8192 : l->cap * 2;
        char *nb;
        while (ncap < l->len + extra + 1) ncap *= 2;
        nb = (char *)pm_util_mem_alloc(l->arena, ncap);
        if (nb == NULL) { l->dead = 1; return 0; }
        if (l->len > 0) memcpy(nb, l->p, l->len);
        l->p = nb;
        l->cap = ncap;
    }
    return 1;
}

static void pm_jit_cpp_low_puts(pm_jit_cpp_low_t *l, const char *s) {
    size_t n = strlen(s);
    if (!pm_jit_cpp_low_reserve(l, n)) return;
    memcpy(l->p + l->len, s, n);
    l->len += n;
}

static void pm_jit_cpp_low_putn(pm_jit_cpp_low_t *l, const char *s, size_t n) {
    if (!pm_jit_cpp_low_reserve(l, n)) return;
    memcpy(l->p + l->len, s, n);
    l->len += n;
}

static void pm_jit_cpp_low_indent(pm_jit_cpp_low_t *l, uint32_t depth) {
    uint32_t i;
    for (i = 0; i < depth; i++) pm_jit_cpp_low_puts(l, "    ");
}

/* Emit "type name" for a declaration, moving trailing array suffixes
 * ([...] groups folded into the type text) after the name — C requires
 * "char buf[4]", not "char[4] buf". */
static void pm_jit_cpp_low_decl_type_name(pm_jit_cpp_low_t *l,
    const char *ty, size_t ty_len, const char *name, size_t name_len) {
    size_t base_len = ty_len;
    size_t i;
    /* find where the trailing [..][..] run starts */
    while (base_len > 0 && (ty[base_len - 1] == ' ' || ty[base_len - 1] == '\t')) {
        base_len--;
    }
    for (i = base_len; i > 0; i--) {
        if (ty[i - 1] == ']') {
            /* scan back to the matching '[': nested ']' increments, the
             * '[' that zeroes the depth is the match */
            size_t j = i - 1;
            size_t depth = 0;
            while (j > 0) {
                if (ty[j] == ']') depth++;
                if (ty[j] == '[') {
                    depth--;
                    if (depth == 0) break;
                }
                j--;
            }
            if (j == 0 || depth != 0) break; /* not a bracket group */
            i = j + 1; /* for's i-- then examines ty[j-1] */
        } else if (ty[i - 1] == ' ' || ty[i - 1] == '\t') {
            continue;
        } else {
            break;
        }
    }
    base_len = i;
    while (base_len > 0 && (ty[base_len - 1] == ' ' || ty[base_len - 1] == '\t')) {
        base_len--;
    }
    pm_jit_cpp_low_putn(l, ty, base_len);
    pm_jit_cpp_low_puts(l, " ");
    pm_jit_cpp_low_putn(l, name, name_len);
    if (base_len < ty_len) {
        pm_jit_cpp_low_putn(l, ty + base_len, ty_len - base_len);
    }
}

/* Error helper for the lower path — msg becomes "cppx: <msg> at line N". */
static int pm_jit_cpp_lerr(pm_util_mem_arena_t *arena,
    char *errbuf, size_t errbuf_len, const char *msg, uint32_t line) {
    char buf[PM_JIT_CPP_ERR_MAX];
    snprintf(buf, sizeof(buf), "cppx: %s at line %u", msg, line);
    if (errbuf != NULL && errbuf_len > 0) {
        snprintf(errbuf, errbuf_len, "%s", buf);
    }
    (void)arena;
    return -1;
}

/* ---- class table helpers ---- */

static void pm_cppx_table_init(pm_cppx_table_t *t, pm_util_mem_arena_t *arena) {
    memset(t, 0, sizeof(*t));
    t->arena = arena;
}

static const char *pm_cppx_intern(pm_cppx_table_t *t, const char *s, size_t n) {    char *dst = (char *)pm_util_mem_alloc(t->arena, n + 1);
    if (dst == NULL) return NULL;
    memcpy(dst, s, n);
    dst[n] = '\0';
    return dst;
}

static pm_cppx_class_t *pm_cppx_class_find(pm_cppx_table_t *t,
    const char *cname) {
    uint32_t i;
    if (cname == NULL) return NULL;
    for (i = 0; i < t->n_classes; i++) {
        if (strcmp(t->classes[i].cname, cname) == 0) return &t->classes[i];
    }
    return NULL;
}

/* by SOURCE name — template decls register under their unmangled name */
static pm_cppx_class_t *pm_cppx_class_find_src(pm_cppx_table_t *t,
    const char *name) {
    uint32_t i;
    if (name == NULL) return NULL;
    for (i = 0; i < t->n_classes; i++) {
        if (strcmp(t->classes[i].name, name) == 0) return &t->classes[i];
    }
    return NULL;
}

static pm_cppx_class_t *pm_cppx_class_add(pm_cppx_table_t *t, const char *name,
    const char *cname, const pm_jit_cpp_ast_t *node) {
    pm_cppx_class_t *c;
    if (t->n_classes >= PM_CPPX_MAX_CLASSES) return NULL;
    c = &t->classes[t->n_classes++];
    memset(c, 0, sizeof(*c));
    c->name = name;
    c->cname = cname;
    c->node = node;
    return c;
}

/* type name cleanup for the C side: "Box<int>" is never a C name — the
 * instantiation mapping must resolve it first. Returns NULL when unknown.
 * Input may carry token spacing ("Box < int >") — normalized here. */
static void pm_cppx_norm_type(char *ty) {
    size_t r = 0;   /* read */
    size_t w = 0;   /* write */
    while (ty[r] != '\0') {
        char ch = ty[r];
        if (ch == ' ') { r++; continue; }
        ty[w++] = ch;
        r++;
    }
    ty[w] = '\0';
}

static const char *pm_cppx_map_type(pm_cppx_table_t *t, const char *ty) {
    uint32_t i;
    if (strchr(ty, '<') != NULL) {
        /* name<args> — split at '<', strip trailing '>' */
        const char *lt = strchr(ty, '<');
        size_t nlen = (size_t)(lt - ty);
        size_t alen = strlen(ty) - nlen - 2; /* < ... > */
        char nb[PM_CPPX_NAME_MAX];
        char ab[PM_CPPX_NAME_MAX];
        if (nlen >= sizeof(nb) || alen >= sizeof(ab)) return NULL;
        memcpy(nb, ty, nlen); nb[nlen] = '\0';
        memcpy(ab, lt + 1, alen); ab[alen] = '\0';
        pm_cppx_norm_type(nb);
        pm_cppx_norm_type(ab);
        /* std smart pointers are the one std template with a fixed C shape:
         * unique_ptr<T>/shared_ptr<T> own a T* (the fixture uses them as
         * owning handles; dtor tracking is out of the subset). */
        if (strcmp(nb, "std::unique_ptr") == 0
            || strcmp(nb, "unique_ptr") == 0
            || strcmp(nb, "std::shared_ptr") == 0
            || strcmp(nb, "shared_ptr") == 0) {
            const char *inner = pm_cppx_map_type(t, ab);
            char pb[PM_CPPX_NAME_MAX + 2];
            const char *out;
            if (inner == NULL) return NULL;
            if (strlen(inner) + 1 >= sizeof(pb)) return NULL;
            snprintf(pb, sizeof(pb), "%s *", inner);
            out = pm_cppx_intern(t, pb, strlen(pb));
            return out;
        }
        for (i = 0; i < t->n_insts; i++) {
            if (strcmp(t->insts[i].tpl_name, nb) == 0
                && strcmp(t->insts[i].args, ab) == 0) {
                return t->insts[i].cname;
            }
        }
        return NULL;
    }
    return ty;
}

static const pm_jit_cpp_ast_t *pm_cppx_find_template(pm_cppx_table_t *t,
    const char *name) {
    uint32_t i;
    for (i = 0; i < t->n_templates; i++) {
        const pm_jit_cpp_ast_t *td = t->templates[i];
        const pm_jit_cpp_ast_t *inner =
            td->n_kids > 0 ? td->kids[td->n_kids - 1] : NULL;
        if (inner != NULL
            && (inner->kind == PM_JIT_CPP_AST_CLASS
                || inner->kind == PM_JIT_CPP_AST_FUNCTION)
            && inner->text_len == strlen(name)
            && memcmp(inner->text, name, strlen(name)) == 0) {
            return td;
        }
    }
    /* template FUNCTION decls park their clones at the tail; a FUNCTION
     * node there is an instantiation, never a decl — nothing to find. */
    return NULL;
}

/* locals map (function-local scope, one frame: params then body) */
static void pm_cppx_local_add(pm_cppx_table_t *t, const char *var,
    const char *cls, int is_pointer) {
    uint32_t i;
    for (i = 0; i < t->n_locals; i++) {
        if (strcmp(t->locals[i].var, var) == 0) {
            t->locals[i].cls = cls;
            t->locals[i].is_pointer = is_pointer;
            return;
        }
    }
    if (t->n_locals >= PM_CPPX_MAX_LOCALS) return;
    t->locals[t->n_locals].var = var;
    t->locals[t->n_locals].cls = cls;
    t->locals[t->n_locals].is_pointer = is_pointer;
    t->n_locals++;
}

static void pm_cppx_locals_clear(pm_cppx_table_t *t) {
    t->n_locals = 0;
}

static const char *pm_cppx_local_class(pm_cppx_table_t *t, const char *var,
    int *is_pointer) {
    uint32_t i;
    for (i = 0; i < t->n_locals; i++) {
        if (strcmp(t->locals[i].var, var) == 0) {
            if (is_pointer != NULL) *is_pointer = t->locals[i].is_pointer;
            return t->locals[i].cls;
        }
    }
    if (is_pointer != NULL) *is_pointer = 0;
    return NULL;
}

/* template function instantiation lookup by mangled name (identity_int) */
static int pm_cppx_inst_fn_exists(pm_cppx_table_t *t, const char *cname) {
    uint32_t i;
    for (i = 0; i < t->n_insts; i++) {
        if (t->insts[i].tpl_name == NULL
            && strcmp(t->insts[i].cname, cname) == 0) {
            return 1;
        }
    }
    return 0;
}

/* template substitution: clone a node tree replacing a type-param name with
 * the argument type in every TYPE text. Only the shapes the parser produces
 * are cloned. */
static pm_jit_cpp_ast_t *pm_cppx_subst_node(pm_cppx_table_t *t,
    const pm_jit_cpp_ast_t *n, const char *param, const char *arg) {
    pm_jit_cpp_ast_t *out;
    uint32_t i;
    if (n == NULL) return NULL;
    out = (pm_jit_cpp_ast_t *)pm_util_mem_alloc(t->arena, sizeof(*out));
    if (out == NULL) return NULL;
    *out = *n;
    out->n_kids = 0;
    out->kids = NULL;
    if (n->kind == PM_JIT_CPP_AST_TYPE && n->text != NULL) {
        const char *s = n->text;
        size_t plen = strlen(param);
        char buf[PM_CPPX_NAME_MAX];
        size_t len = 0;
        while (*s != '\0' && len + 1 < sizeof(buf)) {
            if (strncmp(s, param, plen) == 0
                && (s[plen] == '\0' || s[plen] == ' ' || s[plen] == '*'
                    || s[plen] == '&' || s[plen] == '<' || s[plen] == ',')) {
                if (len + strlen(arg) >= sizeof(buf)) return NULL;
                memcpy(buf + len, arg, strlen(arg));
                len += strlen(arg);
                s += plen;
                continue;
            }
            buf[len++] = *s++;
        }
        buf[len] = '\0';
        out->text = pm_cppx_intern(t, buf, len);
        if (out->text == NULL) return NULL;
        out->text_len = len;
    }
    for (i = 0; i < n->n_kids; i++) {
        pm_jit_cpp_ast_t *kid = pm_cppx_subst_node(t, n->kids[i], param, arg);
        if (kid == NULL) return NULL;
        {
            pm_jit_cpp_ast_t **nk = (pm_jit_cpp_ast_t **)pm_util_mem_alloc(
                t->arena, (size_t)(out->n_kids + 1) * sizeof(pm_jit_cpp_ast_t *));
            if (nk == NULL) return NULL;
            if (out->n_kids > 0) {
                memcpy(nk, out->kids,
                    (size_t)out->n_kids * sizeof(pm_jit_cpp_ast_t *));
            }
            nk[out->n_kids++] = kid;
            out->kids = nk;
        }
    }
    return out;
}

static pm_jit_cpp_ast_t *pm_cppx_subst_class(pm_cppx_table_t *t,
    const pm_jit_cpp_ast_t *cls, const char *param, const char *arg,
    const char *new_name) {
    pm_jit_cpp_ast_t *out = pm_cppx_subst_node(t, cls, param, arg);
    if (out == NULL) return NULL;
    out->text = new_name;
    out->text_len = strlen(new_name);
    return out;
}

/* C++-only token kinds in BINARY/UNARY operator slots that C shares nothing
 * with. The C-shared set lowers verbatim; everything else refuses. */
static const char *pm_jit_cpp_bin_op_text(pm_jit_cpp_tok_kind k) {
    switch (k) {
    case PM_JIT_CPP_TOK_ADD_ASSIGN: return "+=";
    case PM_JIT_CPP_TOK_SUB_ASSIGN: return "-=";
    case PM_JIT_CPP_TOK_MUL_ASSIGN: return "*=";
    case PM_JIT_CPP_TOK_DIV_ASSIGN: return "/=";
    case PM_JIT_CPP_TOK_MOD_ASSIGN: return "%=";
    case PM_JIT_CPP_TOK_XOR_ASSIGN: return "^=";
    case PM_JIT_CPP_TOK_AND_ASSIGN: return "&=";
    case PM_JIT_CPP_TOK_OR_ASSIGN: return "|=";
    case PM_JIT_CPP_TOK_SHIFT_LEFT_ASSIGN: return "<<=";
    case PM_JIT_CPP_TOK_SHIFT_RIGHT_ASSIGN: return ">>=";
    case PM_JIT_CPP_TOK_LEFT_OP: return "<<";
    case PM_JIT_CPP_TOK_RIGHT_OP: return ">>";
    case PM_JIT_CPP_TOK_LE_OP: return "<=";
    case PM_JIT_CPP_TOK_GE_OP: return ">=";
    case PM_JIT_CPP_TOK_EQ_OP: return "==";
    case PM_JIT_CPP_TOK_NE_OP: return "!=";
    case PM_JIT_CPP_TOK_AND_OP: return "&&";
    case PM_JIT_CPP_TOK_OR_OP: return "||";
    default: return NULL; /* single-char punct ops use their own text */
    }
}

static int pm_jit_cpp_is_c_shared_binop(const pm_jit_cpp_ast_t *e) {
    if (e->op_kind == PM_JIT_CPP_TOK_PUNCT) return 1;
    return pm_jit_cpp_bin_op_text(e->op_kind) != NULL;
}

/* stmt/expr forward decls */
static int pm_jit_cpp_low_stmt(pm_jit_cpp_low_t *l, const pm_jit_cpp_ast_t *n,
    uint32_t depth, char *errbuf, size_t errbuf_len);
static int pm_jit_cpp_low_expr(pm_jit_cpp_low_t *l, const pm_jit_cpp_ast_t *e,
    char *errbuf, size_t errbuf_len);

/* ---- expressions ---- */

/* receiver class resolution: NAME (local or param), MEMBER chains through
 * tracked locals (p->box, this->inner), or a NEW_EXPR of a known class. */
static const pm_cppx_class_t *pm_cppx_recv_class(pm_jit_cpp_low_t *l,
    const pm_jit_cpp_ast_t *e, int *is_pointer) {
    const char *cls;
    if (is_pointer != NULL) *is_pointer = 0;
    if (e == NULL) return NULL;
    if (e->kind == PM_JIT_CPP_AST_NAME) {
        char nb[PM_CPPX_NAME_MAX];
        size_t len = e->text_len;
        int ptr = 0;
        if (len >= sizeof(nb)) return NULL;
        memcpy(nb, e->text, len); nb[len] = '\0';
        /* this -> the current method's class, always a pointer */
        if (strcmp(nb, "this") == 0 && l->tbl->cur_class != NULL) {
            if (is_pointer != NULL) *is_pointer = 1;
            return l->tbl->cur_class;
        }
        cls = pm_cppx_local_class(l->tbl, nb, &ptr);
        if (cls == NULL) return NULL;
        if (is_pointer != NULL) *is_pointer = ptr;
        return pm_cppx_class_find(l->tbl, cls);
    }
    if (e->kind == PM_JIT_CPP_AST_MEMBER) {
        /* p->field.method() / obj.field.method() — the receiver of the
         * method is the chain minus the last hop; resolve the chain root. */
        return pm_cppx_recv_class(l, e->kids[0], is_pointer);
    }
    if (e->kind == PM_JIT_CPP_AST_NEW_EXPR) {
        char nb[PM_CPPX_NAME_MAX];
        size_t len = e->text_len;
        const char *mapped;
        if (len >= sizeof(nb)) return NULL;
        memcpy(nb, e->text, len); nb[len] = '\0';
        mapped = pm_cppx_map_type(l->tbl, nb);
        if (mapped == NULL) return NULL;
        if (is_pointer != NULL) *is_pointer = 0; /* new returns a value here */
        return pm_cppx_class_find(l->tbl, mapped);
    }
    return NULL;
}

/* is <name> a field of cls (or a base)? Methods' bare field refs resolve
 * against the current class chain. */
static int pm_cppx_class_has_field(const pm_cppx_table_t *t,
    const pm_cppx_class_t *cls, const char *name) {
    const pm_cppx_class_t *c = cls;
    while (c != NULL) {
        uint32_t i;
        for (i = 0; i < c->node->n_kids; i++) {
            const pm_jit_cpp_ast_t *k = c->node->kids[i];
            if (k->kind == PM_JIT_CPP_AST_VAR
                && k->text_len == strlen(name)
                && memcmp(k->text, name, strlen(name)) == 0) {
                return 1;
            }
        }
        c = c->base_cname != NULL ? pm_cppx_class_find(
            (pm_cppx_table_t *)t, c->base_cname) : NULL;
    }
    return 0;
}

/* emit a method call. Virtual methods dispatch through the vtable:
 *   recv->vptr->slot((Base*)recv, args)  for a pointer receiver
 *   recv.vptr->slot((Base*)&recv, args)  for a value receiver
 * Non-virtual: Class_method(recv, args). */
static int pm_cppx_emit_method_call(pm_jit_cpp_low_t *l,
    const pm_cppx_class_t *cls, const char *method,
    const pm_jit_cpp_ast_t *recv, int is_arrow, uint32_t line,
    const pm_jit_cpp_ast_t *args, uint32_t n_args,
    char *errbuf, size_t errbuf_len) {
    uint32_t i;
    char cname[PM_CPPX_NAME_MAX];
    const pm_cppx_class_t *owner = cls;
    int vslot = -1;

    /* search the class chain for the method; the vtable order is base slots
     * first, then own slots — matching how the vtable struct is emitted. */
    {
        const pm_cppx_class_t *c = cls;
        uint32_t base_total = 0;
        while (c != NULL) {
            uint32_t k;
            for (k = 0; k < c->n_virtual; k++) {
                const char *vn = c->vt_names[k];
                size_t vnl = strlen(vn);
                size_t ml = strlen(method);
                if (vnl > ml + 1 && vn[vnl - ml - 1] == '_'
                    && strcmp(vn + vnl - ml, method) == 0) {
                    vslot = (int)(base_total + k);
                    owner = c;
                    break;
                }
            }
            if (vslot >= 0) break;
            if (c->base_cname != NULL) {
                const pm_cppx_class_t *b =
                    pm_cppx_class_find(l->tbl, c->base_cname);
                if (b == NULL) break;
                /* the base's own slots number base_total + its position in
                 * ITS chain; recompute the running total by walking down. */
                base_total = 0;
                {
                    const pm_cppx_class_t *bb = b;
                    while (bb != NULL) {
                        base_total += bb->n_virtual;
                        bb = bb->base_cname != NULL
                            ? pm_cppx_class_find(l->tbl, bb->base_cname) : NULL;
                    }
                }
                c = b;
                continue;
            }
            break;
        }
    }

    if (vslot >= 0) {
        /* virtual dispatch via the receiver's vptr */
        char slot[PM_CPPX_NAME_MAX];
        if ((size_t)snprintf(slot, sizeof(slot), "%s", method)
            >= sizeof(slot)) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: virtual method name too long", line);
        }
        if (is_arrow) {
            /* recv->vptr->slot((Base*)recv, ...) */
            if (pm_jit_cpp_low_expr(l, recv, errbuf, errbuf_len) != 0) {
                return -1;
            }
            pm_jit_cpp_low_puts(l, "->vptr->");
            pm_jit_cpp_low_putn(l, slot, strlen(slot));
            pm_jit_cpp_low_puts(l, "((");
            pm_jit_cpp_low_putn(l, owner->cname, strlen(owner->cname));
            pm_jit_cpp_low_puts(l, " *)");
            if (pm_jit_cpp_low_expr(l, recv, errbuf, errbuf_len) != 0) {
                return -1;
            }
        } else {
            if (pm_jit_cpp_low_expr(l, recv, errbuf, errbuf_len) != 0) {
                return -1;
            }
            pm_jit_cpp_low_puts(l, ".vptr->");
            pm_jit_cpp_low_putn(l, slot, strlen(slot));
            pm_jit_cpp_low_puts(l, "((");
            pm_jit_cpp_low_putn(l, owner->cname, strlen(owner->cname));
            pm_jit_cpp_low_puts(l, " *)&");
            if (pm_jit_cpp_low_expr(l, recv, errbuf, errbuf_len) != 0) {
                return -1;
            }
        }
    } else {
        /* non-virtual: Class_method(recv, ...) */
        if ((size_t)snprintf(cname, sizeof(cname), "%s_%s", cls->cname,
            method) >= sizeof(cname)) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: method name too long", line);
        }
        pm_jit_cpp_low_puts(l, cname);
        pm_jit_cpp_low_puts(l, "(");
        if (is_arrow) {
            if (pm_jit_cpp_low_expr(l, recv, errbuf, errbuf_len) != 0) {
                return -1;
            }
        } else {
            pm_jit_cpp_low_puts(l, "&");
            if (pm_jit_cpp_low_expr(l, recv, errbuf, errbuf_len) != 0) {
                return -1;
            }
        }
    }
    /* args (kids[1..]) */
    for (i = 1; i < n_args; i++) {
        pm_jit_cpp_low_puts(l, ", ");
        if (pm_jit_cpp_low_expr(l, args->kids[i], errbuf, errbuf_len) != 0) {
            return -1;
        }
    }
    pm_jit_cpp_low_puts(l, ")");
    (void)line;
    return l->dead ? -1 : 0;
}

static int pm_jit_cpp_low_call(pm_jit_cpp_low_t *l, const pm_jit_cpp_ast_t *e,
    char *errbuf, size_t errbuf_len) {
    uint32_t i;
    const pm_jit_cpp_ast_t *callee = e->n_kids > 0 ? e->kids[0] : NULL;

    if (callee == NULL) {
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
            "unsupported: call target is not a plain function name", e->line);
    }
    /* method call: obj.method(...) / ptr->method(...) — the callee is a
     * MEMBER node whose text is the method name. */
    if (callee->kind == PM_JIT_CPP_AST_MEMBER) {
        int is_ptr = callee->int_val ? 1 : 0;
        const pm_cppx_class_t *cls = pm_cppx_recv_class(l, callee->kids[0],
            &is_ptr);
        const char *method = callee->text;
        if (cls == NULL) {
            /* plain C: not a method call — a call through a struct member
             * (function pointer table). Emit the member access as the
             * callee, with the recorded '.'/'->' operator. */
            if (pm_jit_cpp_low_expr(l, callee, errbuf, errbuf_len) != 0) {
                return -1;
            }
            pm_jit_cpp_low_puts(l, "(");
            for (i = 1; i < e->n_kids; i++) {
                if (i > 1) pm_jit_cpp_low_puts(l, ", ");
                if (pm_jit_cpp_low_expr(l, e->kids[i], errbuf, errbuf_len) != 0) {
                    return -1;
                }
            }
            pm_jit_cpp_low_puts(l, ")");
            return l->dead ? -1 : 0;
        }
        return pm_cppx_emit_method_call(l, cls, method, callee->kids[0],
            is_ptr, e->line, e, e->n_kids, errbuf, errbuf_len);
    }
    /* plain template call: identity<int>(x) — the callee is a TEMPLATE_REF
     * whose text is the mangled instantiation name. */
    if (callee->kind == PM_JIT_CPP_AST_TEMPLATE_REF) {
        char cname[PM_CPPX_NAME_MAX];
        size_t len = callee->text_len;
        if (len >= sizeof(cname)) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: template call name too long", e->line);
        }
        memcpy(cname, callee->text, len);
        cname[len] = '\0';
        pm_cppx_norm_type(cname);
        /* only a template FUNCTION with explicit args reaches here; verify
         * the scan instantiated it (mangled <name>_<arg>) before calling */
        {
            const char *lt = strchr(cname, '<');
            size_t nlen;
            char arg[PM_CPPX_NAME_MAX];
            size_t alen;
            char mangled[PM_CPPX_NAME_MAX];
            size_t k;
            if (lt == NULL) {
                return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                    "unsupported: unknown template instantiation", e->line);
            }
            nlen = (size_t)(lt - cname);
            alen = strlen(cname) - nlen - 2;
            if (alen >= sizeof(arg)
                || nlen + 1 + alen >= sizeof(mangled)) {
                return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                    "unsupported: template call name too long", e->line);
            }
            memcpy(arg, lt + 1, alen); arg[alen] = '\0';
            memcpy(mangled, cname, nlen);
            mangled[nlen] = '_';
            memcpy(mangled + nlen + 1, arg, alen + 1);
            for (k = 0; k < nlen + 1 + alen; k++) {
                char ch = mangled[k];
                if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                    || (ch >= '0' && ch <= '9') || ch == '_')) {
                    mangled[k] = '_';
                }
            }
            if (!pm_cppx_inst_fn_exists(l->tbl, mangled)) {
                return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                    "unsupported: unknown template instantiation", e->line);
            }
            pm_jit_cpp_low_puts(l, mangled);
        }
        pm_jit_cpp_low_puts(l, "(");
        for (i = 1; i < e->n_kids; i++) {
            if (i > 1) pm_jit_cpp_low_puts(l, ", ");
            if (pm_jit_cpp_low_expr(l, e->kids[i], errbuf, errbuf_len) != 0) {
                return -1;
            }
        }
        pm_jit_cpp_low_puts(l, ")");
        return l->dead ? -1 : 0;
    }
    if (callee->kind != PM_JIT_CPP_AST_NAME) {
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
            "unsupported: call target is not a plain function name", e->line);
    }
    /* ctor call in an initializer: T name(args) — T is a known class */
    {
        char cname[PM_CPPX_NAME_MAX];
        const pm_cppx_class_t *cls;
        size_t len = callee->text_len;
        if (len < sizeof(cname)) {
            memcpy(cname, callee->text, len);
            cname[len] = '\0';
            cls = pm_cppx_class_find_src(l->tbl, cname);
            if (cls != NULL) {
                /* value ctor call — not a statement form the C backend can
                 * express directly; decl lowering handles it specially. */
                return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                    "unsupported: class value in expression", e->line);
            }
        }
    }
    pm_jit_cpp_low_putn(l, callee->text, callee->text_len);
    pm_jit_cpp_low_puts(l, "(");
    for (i = 1; i < e->n_kids; i++) {
        if (i > 1) pm_jit_cpp_low_puts(l, ", ");
        if (pm_jit_cpp_low_expr(l, e->kids[i], errbuf, errbuf_len) != 0) {
            return -1;
        }
    }
    pm_jit_cpp_low_puts(l, ")");
    return l->dead ? -1 : 0;
}

static int pm_jit_cpp_low_unary(pm_jit_cpp_low_t *l, const pm_jit_cpp_ast_t *e,
    char *errbuf, size_t errbuf_len) {
    const char *op = NULL;
    char one[2];

    if (e->n_kids != 1) {
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
            "unsupported: unary operand shape", e->line);
    }
    switch (e->op_kind) {
    case PM_JIT_CPP_TOK_KEYWORD:
        /* sizeof/alignof/decltype ride as UNARY with keyword text; operand
         * may be an expression or a TYPE node (re-emitted verbatim). */
        if (e->text_len == 6 && memcmp(e->text, "sizeof", 6) == 0) {
            pm_jit_cpp_low_puts(l, "sizeof");
        } else if (e->text_len == 7 && memcmp(e->text, "alignof", 7) == 0) {
            pm_jit_cpp_low_puts(l, "alignof");
        } else if (e->text_len == 8 && memcmp(e->text, "decltype", 8) == 0) {
            pm_jit_cpp_low_puts(l, "sizeof"); /* C-safe approximation */
        } else {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: keyword unary operator", e->line);
        }
        pm_jit_cpp_low_puts(l, "(");
        if (e->kids[0]->kind == PM_JIT_CPP_AST_TYPE) {
            pm_jit_cpp_low_putn(l, e->kids[0]->text, e->kids[0]->text_len);
        } else if (e->kids[0]->kind == PM_JIT_CPP_AST_PP) {
            pm_jit_cpp_low_putn(l, e->kids[0]->text, e->kids[0]->text_len);
        } else {
            if (pm_jit_cpp_low_expr(l, e->kids[0], errbuf, errbuf_len) != 0) {
                return -1;
            }
        }
        pm_jit_cpp_low_puts(l, ")");
        return l->dead ? -1 : 0;
    case PM_JIT_CPP_TOK_INC_OP: op = "++"; break;
    case PM_JIT_CPP_TOK_DEC_OP: op = "--"; break;
    case PM_JIT_CPP_TOK_PUNCT:
        if (e->text == NULL || e->text_len != 1) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: unary operator", e->line);
        }
        one[0] = e->text[0];
        one[1] = '\0';
        op = one;
        break;
    default:
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
            "unsupported: C++-only unary operator", e->line);
    }
    if (e->int_val == 1 && (e->op_kind == PM_JIT_CPP_TOK_INC_OP
            || e->op_kind == PM_JIT_CPP_TOK_DEC_OP)) {
        /* postfix ++/--: the operand rides first, the op after it — the
         * parser's postfix marker, without which "toks[out->n++]" would
         * re-emit as "toks[++out->n]" and write past the array. */
        if (pm_jit_cpp_low_expr(l, e->kids[0], errbuf, errbuf_len) != 0) {
            return -1;
        }
        pm_jit_cpp_low_puts(l, op);
        return l->dead ? -1 : 0;
    }
    pm_jit_cpp_low_puts(l, op);
    /* a binary operand of a prefix op must keep its parens: the parser drops
     * them, but "-(a + b)" would re-parse as "(-a) + b" without them. */
    if (e->kids[0]->kind == PM_JIT_CPP_AST_BINARY && e->kids[0]->n_kids == 2
        && !(e->kids[0]->text != NULL && e->kids[0]->text_len == 2
            && memcmp(e->kids[0]->text, "[]", 2) == 0)
        && !(e->kids[0]->text != NULL && e->kids[0]->text_len == 2
            && memcmp(e->kids[0]->text, "?:", 2) == 0)) {
        pm_jit_cpp_low_puts(l, "(");
        if (pm_jit_cpp_low_expr(l, e->kids[0], errbuf, errbuf_len) != 0) {
            return -1;
        }
        pm_jit_cpp_low_puts(l, ")");
        return l->dead ? -1 : 0;
    }
    return pm_jit_cpp_low_expr(l, e->kids[0], errbuf, errbuf_len);
}

/* C binary-operator precedence (higher binds tighter). Comma 1, assignment 2,
 * || 3, && 4, bitwise | ^ & 5/6/7, equality 8, relational 9, shift 10,
 * additive 11, multiplicative 12. The multi-char ops ride op_kind;
 * single-char puncts ride e->text. Ternary and [] never reach here. */
static int pm_jit_cpp_bin_prec(const pm_jit_cpp_ast_t *e) {
    if (e->op_kind == PM_JIT_CPP_TOK_PUNCT) {
        if (e->text == NULL || e->text_len != 1) return 1;
        switch (e->text[0]) {
        case ',': return 1;
        case '=': return 2;
        case '|': return 5;
        case '^': return 6;
        case '&': return 7;
        case '<': case '>': return 9;
        case '+': case '-': return 11;
        case '*': case '/': case '%': return 12;
        default: return 1;
        }
    }
    switch (e->op_kind) {
    case PM_JIT_CPP_TOK_ADD_ASSIGN: case PM_JIT_CPP_TOK_SUB_ASSIGN:
    case PM_JIT_CPP_TOK_MUL_ASSIGN: case PM_JIT_CPP_TOK_DIV_ASSIGN:
    case PM_JIT_CPP_TOK_MOD_ASSIGN: case PM_JIT_CPP_TOK_XOR_ASSIGN:
    case PM_JIT_CPP_TOK_AND_ASSIGN: case PM_JIT_CPP_TOK_OR_ASSIGN:
    case PM_JIT_CPP_TOK_SHIFT_LEFT_ASSIGN:
    case PM_JIT_CPP_TOK_SHIFT_RIGHT_ASSIGN:
        return 2;
    case PM_JIT_CPP_TOK_OR_OP: return 3;
    case PM_JIT_CPP_TOK_AND_OP: return 4;
    case PM_JIT_CPP_TOK_EQ_OP: case PM_JIT_CPP_TOK_NE_OP: return 8;
    case PM_JIT_CPP_TOK_LE_OP: case PM_JIT_CPP_TOK_GE_OP: return 9;
    case PM_JIT_CPP_TOK_LEFT_OP: case PM_JIT_CPP_TOK_RIGHT_OP: return 10;
    default: return 1;
    }
}

/* May a same-precedence rhs with the SAME operator be flattened (no parens)?
 * Only the commutative/associative-safe set: a op (b op c) == (a op b) op c. */
static int pm_jit_cpp_bin_flat_ok(const pm_jit_cpp_ast_t *e) {
    if (e->op_kind == PM_JIT_CPP_TOK_PUNCT) {
        if (e->text == NULL || e->text_len != 1) return 0;
        switch (e->text[0]) {
        case '+': case '*': case '&': case '|': case '^': return 1;
        default: return 0;
        }
    }
    switch (e->op_kind) {
    case PM_JIT_CPP_TOK_AND_OP: case PM_JIT_CPP_TOK_OR_OP: return 1;
    default: return 0;
    }
}

/* Does operand `kid` of binary `e` need parentheses when emitted inside it?
 * The parser drops "redundant" parens from the AST, so the lowerer must
 * restore any paren the C precedence table demands: lower-precedence child
 * inside a tighter parent, a non-flattenable same-precedence rhs, and any
 * assignment child (its "redundant" parens were load-bearing in the source
 * once nested). */
static int pm_jit_cpp_op_needs_paren(const pm_jit_cpp_ast_t *kid,
    const pm_jit_cpp_ast_t *parent, int is_rhs) {
    int kp, pp;
    if (kid->kind != PM_JIT_CPP_AST_BINARY || kid->n_kids != 2) {
        return 0;
    }
    if (kid->text != NULL && kid->text_len == 2
        && memcmp(kid->text, "?:", 2) == 0) {
        return 0; /* ternary self-parenthesizes */
    }
    if (kid->text != NULL && kid->text_len == 2
        && memcmp(kid->text, "[]", 2) == 0) {
        return 0; /* indexing self-delimits */
    }
    kp = pm_jit_cpp_bin_prec(kid);
    pp = pm_jit_cpp_bin_prec(parent);
    if (kp < pp) {
        return 1;
    }
    if (kp == pp && is_rhs) {
        if (kid->op_kind == parent->op_kind && kid->text != NULL
            && parent->text != NULL && kid->text_len == parent->text_len
            && memcmp(kid->text, parent->text, kid->text_len) == 0) {
            return !pm_jit_cpp_bin_flat_ok(kid);
        }
        return 1;
    }
    return 0;
}

static int pm_jit_cpp_low_binary(pm_jit_cpp_low_t *l, const pm_jit_cpp_ast_t *e,
    char *errbuf, size_t errbuf_len) {
    if (e->n_kids == 3 && e->text_len == 2 && e->text != NULL
        && memcmp(e->text, "?:", 2) == 0) {
        /* ternary — C shares it; parenthesise so it nests cleanly. */
        pm_jit_cpp_low_puts(l, "(");
        if (pm_jit_cpp_low_expr(l, e->kids[0], errbuf, errbuf_len) != 0) {
            return -1;
        }
        pm_jit_cpp_low_puts(l, " ? ");
        if (pm_jit_cpp_low_expr(l, e->kids[1], errbuf, errbuf_len) != 0) {
            return -1;
        }
        pm_jit_cpp_low_puts(l, " : ");
        if (pm_jit_cpp_low_expr(l, e->kids[2], errbuf, errbuf_len) != 0) {
            return -1;
        }
        pm_jit_cpp_low_puts(l, ")");
        return l->dead ? -1 : 0;
    }
    if (e->n_kids == 2 && e->text_len == 2 && e->text != NULL
        && memcmp(e->text, "[]", 2) == 0) {
        /* array indexing — postfix, C shares it. */
        if (pm_jit_cpp_low_expr(l, e->kids[0], errbuf, errbuf_len) != 0) {
            return -1;
        }
        pm_jit_cpp_low_puts(l, "[");
        if (pm_jit_cpp_low_expr(l, e->kids[1], errbuf, errbuf_len) != 0) {
            return -1;
        }
        pm_jit_cpp_low_puts(l, "]");
        return l->dead ? -1 : 0;
    }
    if (e->n_kids != 2) {
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
            "unsupported: binary operand shape", e->line);
    }
    if (!pm_jit_cpp_is_c_shared_binop(e)) {
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
            "unsupported: C++-only binary operator", e->line);
    }
    if (pm_jit_cpp_op_needs_paren(e->kids[0], e, 0)) {
        pm_jit_cpp_low_puts(l, "(");
    }
    if (pm_jit_cpp_low_expr(l, e->kids[0], errbuf, errbuf_len) != 0) {
        return -1;
    }
    if (pm_jit_cpp_op_needs_paren(e->kids[0], e, 0)) {
        pm_jit_cpp_low_puts(l, ")");
    }
    pm_jit_cpp_low_puts(l, " ");
    if (e->op_kind == PM_JIT_CPP_TOK_PUNCT) {
        pm_jit_cpp_low_putn(l, e->text, e->text_len);
    } else {
        pm_jit_cpp_low_puts(l, pm_jit_cpp_bin_op_text(e->op_kind));
    }
    pm_jit_cpp_low_puts(l, " ");
    if (pm_jit_cpp_op_needs_paren(e->kids[1], e, 1)) {
        pm_jit_cpp_low_puts(l, "(");
    }
    if (pm_jit_cpp_low_expr(l, e->kids[1], errbuf, errbuf_len) != 0) {
        return -1;
    }
    if (pm_jit_cpp_op_needs_paren(e->kids[1], e, 1)) {
        pm_jit_cpp_low_puts(l, ")");
    }
    return l->dead ? -1 : 0;
}

static int pm_jit_cpp_low_expr(pm_jit_cpp_low_t *l, const pm_jit_cpp_ast_t *e,
    char *errbuf, size_t errbuf_len) {
    if (e == NULL) {
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len, "null expr", 0);
    }
    switch (e->kind) {
    case PM_JIT_CPP_AST_LITERAL:
        /* nullptr has no C spelling; true/false become 1/0 (C99 <stdbool.h>
         * is not assumed — emit the integer spelling). */
        if (e->text_len == 7 && memcmp(e->text, "nullptr", 7) == 0) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: nullptr", e->line);
        }
        if (e->text_len == 4 && memcmp(e->text, "true", 4) == 0) {
            pm_jit_cpp_low_puts(l, "1");
            return l->dead ? -1 : 0;
        }
        if (e->text_len == 5 && memcmp(e->text, "false", 5) == 0) {
            pm_jit_cpp_low_puts(l, "0");
            return l->dead ? -1 : 0;
        }
        pm_jit_cpp_low_putn(l, e->text, e->text_len);
        return l->dead ? -1 : 0;
    case PM_JIT_CPP_AST_NAME:
        /* inside a method body, bare field refs are this->field */
        if (l->tbl->cur_class != NULL && e->text != NULL) {
            char nb[PM_CPPX_NAME_MAX];
            size_t len = e->text_len;
            if (len < sizeof(nb)) {
                memcpy(nb, e->text, len); nb[len] = '\0';
                if (strcmp(nb, "this") == 0) {
                    pm_jit_cpp_low_puts(l, "self");
                    return l->dead ? -1 : 0;
                }
                if (pm_cppx_class_has_field(l->tbl, l->tbl->cur_class, nb)) {
                    pm_jit_cpp_low_puts(l, "self->");
                }
            }
        }
        pm_jit_cpp_low_putn(l, e->text, e->text_len);
        return l->dead ? -1 : 0;
    case PM_JIT_CPP_AST_CALL:
        return pm_jit_cpp_low_call(l, e, errbuf, errbuf_len);
    case PM_JIT_CPP_AST_CAST:
        /* C cast: (type)expr — re-emit as-is; the type text came from the
         * source tokens so it is already valid C. */
        if (e->n_kids != 1) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: cast shape", e->line);
        }
        pm_jit_cpp_low_puts(l, "(");
        pm_jit_cpp_low_putn(l, e->text, e->text_len);
        pm_jit_cpp_low_puts(l, ")");
        /* non-atomic operands get their own parens: (size_t)(a - b) must
         * not degrade into (size_t)a - b (different semantics in C). */
        if (e->kids[0]->kind == PM_JIT_CPP_AST_BINARY
            || e->kids[0]->kind == PM_JIT_CPP_AST_UNARY
            || e->kids[0]->kind == PM_JIT_CPP_AST_COMMA) {
            pm_jit_cpp_low_puts(l, "(");
            if (pm_jit_cpp_low_expr(l, e->kids[0], errbuf, errbuf_len) != 0) {
                return -1;
            }
            pm_jit_cpp_low_puts(l, ")");
            return l->dead ? -1 : 0;
        }
        return pm_jit_cpp_low_expr(l, e->kids[0], errbuf, errbuf_len);
    case PM_JIT_CPP_AST_COMMA:
        /* comma expression: a, b — array initializer lists and the like */
        if (e->n_kids < 1) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: comma expr shape", e->line);
        }
        {
            uint32_t i;
            for (i = 0; i < e->n_kids; i++) {
                if (i > 0) pm_jit_cpp_low_puts(l, ", ");
                if (pm_jit_cpp_low_expr(l, e->kids[i], errbuf, errbuf_len) != 0) {
                    return -1;
                }
            }
        }
        return l->dead ? -1 : 0;
    case PM_JIT_CPP_AST_BINARY:
        return pm_jit_cpp_low_binary(l, e, errbuf, errbuf_len);
    case PM_JIT_CPP_AST_UNARY:
        return pm_jit_cpp_low_unary(l, e, errbuf, errbuf_len);
    case PM_JIT_CPP_AST_MEMBER: {
        /* obj.field / ptr->field — a data member access on a tracked local
         * or `this`, or any member access in plain C. The '.'/'->'
         * distinction is carried in int_val (1 = '->'). */
        int is_ptr = e->int_val ? 1 : 0;
        const pm_cppx_class_t *cls = pm_cppx_recv_class(l, e->kids[0],
            &is_ptr);
        if (cls == NULL) {
            /* not a class receiver: a plain C struct/union member access —
             * emit it verbatim with the recorded operator. */
            if (pm_jit_cpp_low_expr(l, e->kids[0], errbuf, errbuf_len) != 0) {
                return -1;
            }
            pm_jit_cpp_low_puts(l, e->int_val ? "->" : ".");
            pm_jit_cpp_low_putn(l, e->text, e->text_len);
            return l->dead ? -1 : 0;
        }
        {
            /* field of the class (or its bases)? Emit C member access. */
            const pm_jit_cpp_ast_t *m = NULL;
            const pm_cppx_class_t *c = cls;
            while (c != NULL && m == NULL) {
                uint32_t k;
                for (k = 0; k < c->node->n_kids; k++) {
                    const pm_jit_cpp_ast_t *kid = c->node->kids[k];
                    if (kid->kind == PM_JIT_CPP_AST_VAR
                        && kid->text_len == e->text_len
                        && memcmp(kid->text, e->text, e->text_len) == 0) {
                        m = kid;
                        break;
                    }
                }
                c = c->base_cname != NULL
                    ? pm_cppx_class_find(l->tbl, c->base_cname) : NULL;
            }
            if (m != NULL) {
                if (pm_jit_cpp_low_expr(l, e->kids[0], errbuf, errbuf_len) != 0) {
                    return -1;
                }
                pm_jit_cpp_low_puts(l, (e->int_val || is_ptr) ? "->" : ".");
                pm_jit_cpp_low_putn(l, e->text, e->text_len);
                return l->dead ? -1 : 0;
            }
        }
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
            "unsupported: unknown member in member access", e->line);
    }
    case PM_JIT_CPP_AST_NEW_EXPR: {
        /* new C(args) -> ((C *)malloc(sizeof(C))) + ctor: emitted as a
         * statement-friendly expression requires GNU statement exprs —
         * instead the lowerer emits a helper per class:
         *   C *C_new(args) { C *p = malloc; C_ctor(p, args); return p; }
         * and the expression lowers to C_new(args). new[] and brace-init
         * are refused (no initializer kids recorded for them). */
        char nb[PM_CPPX_NAME_MAX];
        const char *mapped;
        size_t len = e->text_len;
        if (len >= sizeof(nb)) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: new type name too long", e->line);
        }
        memcpy(nb, e->text, len); nb[len] = '\0';
        mapped = pm_cppx_map_type(l->tbl, nb);
        if (mapped == NULL) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: new of unknown class", e->line);
        }
        {
            const pm_cppx_class_t *cls = pm_cppx_class_find(l->tbl, mapped);
            if (cls == NULL) {
                return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                    "unsupported: new of non-class type", e->line);
            }
            pm_jit_cpp_low_putn(l, mapped, strlen(mapped));
            pm_jit_cpp_low_puts(l, "_new(");
            {
                uint32_t i;
                for (i = 0; i < e->n_kids; i++) {
                    if (i > 0) pm_jit_cpp_low_puts(l, ", ");
                    if (pm_jit_cpp_low_expr(l, e->kids[i], errbuf,
                        errbuf_len) != 0) return -1;
                }
            }
            pm_jit_cpp_low_puts(l, ")");
            return l->dead ? -1 : 0;
        }
    }
    case PM_JIT_CPP_AST_DELETE_EXPR: {
        /* delete p -> { C_dtor(p); free(p); } — needs statement context; the
         * parser only produces this inside an expr-stmt, so the expr-stmt
         * path intercepts it. Reaching here means nested use. */
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
            "unsupported: delete outside a statement", e->line);
    }
    case PM_JIT_CPP_AST_TEMPLATE_REF:
        /* as a VALUE (not a call target): identity<int> alone is not a
         * callable C name; only the call path handles it. */
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
            "unsupported: template name outside a call", e->line);
    default:
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
            "unsupported: expression kind", e->line);
    }
}

/* ---- statements ---- */

/* decl-stmt: kid[0]=type, kid[1]=init (optional). Class-typed locals lower
 * to the struct + a ctor call when the initializer is a ctor call; a class
 * with no ctor inits zero (C struct init). Multiple declarators are dropped
 * by the parser (only the first name survives). */
static int pm_jit_cpp_low_decl(pm_jit_cpp_low_t *l, const pm_jit_cpp_ast_t *n,
    uint32_t depth, char *errbuf, size_t errbuf_len) {
    const pm_jit_cpp_ast_t *ty;
    const pm_jit_cpp_ast_t *init;
    char tybuf[PM_CPPX_NAME_MAX];
    const char *cty;
    const pm_cppx_class_t *cls = NULL;
    int is_ptr_decl = 0;
    size_t tlen;

    if (n->n_kids < 1 || n->kids[0]->kind != PM_JIT_CPP_AST_TYPE) {
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
            "unsupported: declaration shape", n->line);
    }
    ty = n->kids[0];
    if (ty->text_len == 4 && memcmp(ty->text, "auto", 4) == 0) {
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
            "unsupported: auto (no type inference in C)", n->line);
    }
    tlen = ty->text_len;
    if (tlen >= sizeof(tybuf)) {
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
            "unsupported: type name too long", n->line);
    }
    memcpy(tybuf, ty->text, tlen); tybuf[tlen] = '\0';
    cty = pm_cppx_map_type(l->tbl, tybuf);
    if (cty == NULL) {
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
            "unsupported: unknown template instantiation in declaration",
            n->line);
    }
    /* pointer-to-class? strip trailing '*' (and spacing) for the local map */
    {
        char base[PM_CPPX_NAME_MAX];
        size_t blen = strlen(cty);
        if (blen >= sizeof(base)) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: type name too long", n->line);
        }
        memcpy(base, cty, blen + 1);
        while (blen > 0 && (base[blen - 1] == '*' || base[blen - 1] == ' '
            || base[blen - 1] == '\t')) {
            if (base[blen - 1] == '*') is_ptr_decl = 1;
            base[--blen] = '\0';
        }
        if (blen > 0) {
            cls = pm_cppx_class_find(l->tbl, base);
            if (cls != NULL) {
                pm_cppx_local_add(l->tbl,
                    pm_cppx_intern(l->tbl, n->text, n->text_len),
                    cls->cname, is_ptr_decl);
            }
        }
    }
    init = n->n_kids > 1 ? n->kids[1] : NULL;
    pm_jit_cpp_low_indent(l, depth);
    pm_jit_cpp_low_decl_type_name(l, cty, strlen(cty), n->text, n->text_len);
    if (init != NULL && init->kind == PM_JIT_CPP_AST_CALL
        && init->n_kids > 0 && init->kids[0]->kind == PM_JIT_CPP_AST_NAME
        && cls != NULL && !is_ptr_decl) {
        /* ctor call initializer on a class VALUE: T name(args) lowers to
         * "T name; T_ctor(&name, args);" — two statements, the ctor call
         * walks the args like any call. */
        pm_jit_cpp_low_puts(l, ";\n");
        pm_jit_cpp_low_indent(l, depth);
        pm_jit_cpp_low_putn(l, cls->cname, strlen(cls->cname));
        pm_jit_cpp_low_puts(l, "_ctor(&");
        pm_jit_cpp_low_putn(l, n->text, n->text_len);
        {
            uint32_t i;
            for (i = 1; i < init->n_kids; i++) {
                pm_jit_cpp_low_puts(l, ", ");
                if (pm_jit_cpp_low_expr(l, init->kids[i], errbuf,
                    errbuf_len) != 0) return -1;
            }
        }
        pm_jit_cpp_low_puts(l, ");\n");
        return l->dead ? -1 : 0;
    }
    if (init != NULL) {
        pm_jit_cpp_low_puts(l, " = ");
        if (pm_jit_cpp_low_expr(l, init, errbuf, errbuf_len) != 0) return -1;
    } else if (cls != NULL && !is_ptr_decl) {
        /* class value with no ctor call: zero it (C99 compound literal not
         * needed — designated zero via = {0}) */
        pm_jit_cpp_low_puts(l, " = {0}");
    }
    pm_jit_cpp_low_puts(l, ";\n");
    return l->dead ? -1 : 0;
}

static int pm_jit_cpp_low_stmt(pm_jit_cpp_low_t *l, const pm_jit_cpp_ast_t *n,
    uint32_t depth, char *errbuf, size_t errbuf_len) {
    uint32_t i;

    if (n == NULL) {
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len, "null stmt", 0);
    }
    switch (n->kind) {
    case PM_JIT_CPP_AST_DECL_GROUP:
        /* int a, b; — emit each declarator inline, no new scope */
        for (i = 0; i < n->n_kids; i++) {
            if (pm_jit_cpp_low_stmt(l, n->kids[i], depth, errbuf,
                errbuf_len) != 0) return -1;
        }
        return l->dead ? -1 : 0;
    case PM_JIT_CPP_AST_COMPOUND:
        pm_jit_cpp_low_indent(l, depth);        pm_jit_cpp_low_puts(l, "{\n");
        for (i = 0; i < n->n_kids; i++) {
            if (pm_jit_cpp_low_stmt(l, n->kids[i], depth + 1, errbuf,
                errbuf_len) != 0) return -1;
        }
        pm_jit_cpp_low_indent(l, depth);
        pm_jit_cpp_low_puts(l, "}\n");
        return l->dead ? -1 : 0;
    case PM_JIT_CPP_AST_PP:
        /* verbatim preprocessor line at statement level */
        pm_jit_cpp_low_indent(l, depth);
        pm_jit_cpp_low_putn(l, n->text, n->text_len);
        pm_jit_cpp_low_puts(l, "\n");
        return l->dead ? -1 : 0;
    case PM_JIT_CPP_AST_SWITCH:
        if (n->n_kids < 2) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: switch shape", n->line);
        }
        pm_jit_cpp_low_indent(l, depth);
        pm_jit_cpp_low_puts(l, "switch (");
        if (pm_jit_cpp_low_expr(l, n->kids[0], errbuf, errbuf_len) != 0) {
            return -1;
        }
        pm_jit_cpp_low_puts(l, ") ");
        /* body is a COMPOUND whose kids are CASE/DEFAULT labels; emit as one
         * braced block. */
        pm_jit_cpp_low_puts(l, "{\n");
        {
            const pm_jit_cpp_ast_t *body = n->kids[1];
            uint32_t i;
            if (body->kind == PM_JIT_CPP_AST_COMPOUND) {
                for (i = 0; i < body->n_kids; i++) {
                    if (pm_jit_cpp_low_stmt(l, body->kids[i], depth + 1,
                        errbuf, errbuf_len) != 0) return -1;
                }
            } else {
                if (pm_jit_cpp_low_stmt(l, body, depth + 1, errbuf,
                    errbuf_len) != 0) return -1;
            }
        }
        pm_jit_cpp_low_indent(l, depth);
        pm_jit_cpp_low_puts(l, "}\n");
        return l->dead ? -1 : 0;
    case PM_JIT_CPP_AST_CASE:
        if (n->n_kids < 1) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: case shape", n->line);
        }
        pm_jit_cpp_low_indent(l, depth);
        pm_jit_cpp_low_puts(l, "case ");
        if (pm_jit_cpp_low_expr(l, n->kids[0], errbuf, errbuf_len) != 0) {
            return -1;
        }
        pm_jit_cpp_low_puts(l, ":\n");
        return l->dead ? -1 : 0;
    case PM_JIT_CPP_AST_DEFAULT:
        pm_jit_cpp_low_indent(l, depth);
        pm_jit_cpp_low_puts(l, "default:\n");
        return l->dead ? -1 : 0;
    case PM_JIT_CPP_AST_GOTO:
        pm_jit_cpp_low_indent(l, depth);
        pm_jit_cpp_low_puts(l, "goto ");
        pm_jit_cpp_low_putn(l, n->text, n->text_len);
        pm_jit_cpp_low_puts(l, ";\n");
        return l->dead ? -1 : 0;
    case PM_JIT_CPP_AST_LABEL:
        pm_jit_cpp_low_indent(l, depth);
        pm_jit_cpp_low_putn(l, n->text, n->text_len);
        pm_jit_cpp_low_puts(l, ":\n");
        if (n->n_kids > 0) {
            return pm_jit_cpp_low_stmt(l, n->kids[0], depth, errbuf,
                errbuf_len);
        }
        return l->dead ? -1 : 0;
    case PM_JIT_CPP_AST_EXPR_STMT:
        /* break/continue ride as EXPR_STMT with the keyword in text */
        if (n->text_len == 5 && memcmp(n->text, "break", 5) == 0) {
            pm_jit_cpp_low_indent(l, depth);
            pm_jit_cpp_low_puts(l, "break;\n");
            return l->dead ? -1 : 0;
        }
        if (n->text_len == 8 && memcmp(n->text, "continue", 8) == 0) {
            pm_jit_cpp_low_indent(l, depth);
            pm_jit_cpp_low_puts(l, "continue;\n");
            return l->dead ? -1 : 0;
        }
        if (n->n_kids == 0) { /* bare ';' */ return l->dead ? -1 : 0; }
        /* delete p; — statement context: dtor + free */
        if (n->kids[0]->kind == PM_JIT_CPP_AST_DELETE_EXPR) {
            const pm_jit_cpp_ast_t *del = n->kids[0];
            int is_ptr = 0;
            const pm_cppx_class_t *cls;
            if (del->n_kids != 1 || del->kids[0]->kind != PM_JIT_CPP_AST_NAME) {
                return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                    "unsupported: delete of non-variable", n->line);
            }
            cls = pm_cppx_recv_class(l, del->kids[0], &is_ptr);
            if (cls == NULL || !is_ptr) {
                return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                    "unsupported: delete of non-class pointer", n->line);
            }
            pm_jit_cpp_low_indent(l, depth);
            pm_jit_cpp_low_putn(l, cls->cname, strlen(cls->cname));
            pm_jit_cpp_low_puts(l, "_dtor(");
            pm_jit_cpp_low_putn(l, del->kids[0]->text, del->kids[0]->text_len);
            pm_jit_cpp_low_puts(l, ");\n");
            pm_jit_cpp_low_indent(l, depth);
            pm_jit_cpp_low_puts(l, "free(");
            pm_jit_cpp_low_putn(l, del->kids[0]->text, del->kids[0]->text_len);
            pm_jit_cpp_low_puts(l, ");\n");
            return l->dead ? -1 : 0;
        }
        pm_jit_cpp_low_indent(l, depth);
        if (pm_jit_cpp_low_expr(l, n->kids[0], errbuf, errbuf_len) != 0) return -1;
        pm_jit_cpp_low_puts(l, ";\n");
        return l->dead ? -1 : 0;
    case PM_JIT_CPP_AST_DECL_STMT:
        return pm_jit_cpp_low_decl(l, n, depth, errbuf, errbuf_len);
    case PM_JIT_CPP_AST_RETURN_STMT:
        pm_jit_cpp_low_indent(l, depth);
        pm_jit_cpp_low_puts(l, "return");
        if (n->n_kids > 0) {
            pm_jit_cpp_low_puts(l, " ");
            if (pm_jit_cpp_low_expr(l, n->kids[0], errbuf, errbuf_len) != 0) {
                return -1;
            }
        }
        pm_jit_cpp_low_puts(l, ";\n");
        return l->dead ? -1 : 0;
    case PM_JIT_CPP_AST_IF:
        if (n->n_kids < 2) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: if shape", n->line);
        }
        pm_jit_cpp_low_indent(l, depth);
        pm_jit_cpp_low_puts(l, "if (");
        if (pm_jit_cpp_low_expr(l, n->kids[0], errbuf, errbuf_len) != 0) return -1;
        pm_jit_cpp_low_puts(l, ") ");
        /* then/else may be a single stmt (no braces) — emit inline */
        if (pm_jit_cpp_low_stmt(l, n->kids[1], depth, errbuf, errbuf_len) != 0) {
            return -1;
        }
        if (n->n_kids > 2) {
            pm_jit_cpp_low_indent(l, depth);
            pm_jit_cpp_low_puts(l, "else ");
            if (pm_jit_cpp_low_stmt(l, n->kids[2], depth, errbuf,
                errbuf_len) != 0) return -1;
        }
        return l->dead ? -1 : 0;
    case PM_JIT_CPP_AST_WHILE:
        if (n->n_kids < 2) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: while shape", n->line);
        }
        if (n->int_val == 1) {
            /* do-while: kids are [body, cond] */
            pm_jit_cpp_low_indent(l, depth);
            pm_jit_cpp_low_puts(l, "do ");
            if (pm_jit_cpp_low_stmt(l, n->kids[0], depth, errbuf,
                errbuf_len) != 0) return -1;
            pm_jit_cpp_low_indent(l, depth);
            pm_jit_cpp_low_puts(l, "while (");
            if (pm_jit_cpp_low_expr(l, n->kids[1], errbuf, errbuf_len) != 0) {
                return -1;
            }
            pm_jit_cpp_low_puts(l, ");\n");
            return l->dead ? -1 : 0;
        }
        pm_jit_cpp_low_indent(l, depth);
        pm_jit_cpp_low_puts(l, "while (");
        if (pm_jit_cpp_low_expr(l, n->kids[0], errbuf, errbuf_len) != 0) return -1;
        pm_jit_cpp_low_puts(l, ") ");
        return pm_jit_cpp_low_stmt(l, n->kids[1], depth, errbuf, errbuf_len);
    case PM_JIT_CPP_AST_FOR:
        if (n->n_kids < 4) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: for shape", n->line);
        }
        pm_jit_cpp_low_indent(l, depth);
        pm_jit_cpp_low_puts(l, "for (");
        /* init: decl-stmt (with its ';') or expr; empty slot = empty
         * LITERAL node rides through as nothing. */
        if (n->kids[0]->kind == PM_JIT_CPP_AST_DECL_STMT) {
            if (pm_jit_cpp_low_decl(l, n->kids[0], 0, errbuf, errbuf_len) != 0) {
                return -1;
            }
            /* low_decl ends with ";\n" — the '\n' is wrong inside the for
             * header; back up one byte. */
            if (l->len > 0 && l->p[l->len - 1] == '\n') l->len--;
        } else if (n->kids[0]->kind == PM_JIT_CPP_AST_LITERAL
            && n->kids[0]->text_len == 0) {
            /* empty init — still needs its ';' */
            pm_jit_cpp_low_puts(l, "; ");
        } else {
            if (pm_jit_cpp_low_expr(l, n->kids[0], errbuf, errbuf_len) != 0) {
                return -1;
            }
            pm_jit_cpp_low_puts(l, "; ");
        }
        pm_jit_cpp_low_puts(l, " ");
        if (!(n->kids[1]->kind == PM_JIT_CPP_AST_LITERAL
            && n->kids[1]->text_len == 0)) {
            if (pm_jit_cpp_low_expr(l, n->kids[1], errbuf, errbuf_len) != 0) return -1;
        }
        pm_jit_cpp_low_puts(l, "; ");
        if (!(n->kids[2]->kind == PM_JIT_CPP_AST_LITERAL
            && n->kids[2]->text_len == 0)) {
            if (pm_jit_cpp_low_expr(l, n->kids[2], errbuf, errbuf_len) != 0) return -1;
        }
        pm_jit_cpp_low_puts(l, ") ");
        return pm_jit_cpp_low_stmt(l, n->kids[3], depth, errbuf, errbuf_len);
    default:
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
            "unsupported: statement kind", n->line);
    }
}

/* ---- top level ---- */

/* function: kid[0]=return type, then params, then body. Functions without a
 * body (pure declarations) emit nothing — C callers see the definition only
 * when one exists. */
static int pm_jit_cpp_low_function(pm_jit_cpp_low_t *l,
    const pm_jit_cpp_ast_t *fn, char *errbuf, size_t errbuf_len) {
    uint32_t i;
    const pm_jit_cpp_ast_t *ty = NULL;
    const pm_jit_cpp_ast_t *body = NULL;
    uint32_t n_params;

    if (fn->n_kids < 1 || fn->kids[0]->kind != PM_JIT_CPP_AST_TYPE) {
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
            "unsupported: function shape", fn->line);
    }
    ty = fn->kids[0];
    if (ty->text_len == 4 && memcmp(ty->text, "auto", 4) == 0) {
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
            "unsupported: auto (no type inference in C)", fn->line);
    }
    for (i = 1; i < fn->n_kids; i++) {
        if (fn->kids[i]->kind == PM_JIT_CPP_AST_COMPOUND) body = fn->kids[i];
    }
    if (body == NULL) return 0; /* declaration only */

    pm_jit_cpp_low_putn(l, ty->text, ty->text_len);
    pm_jit_cpp_low_puts(l, " ");
    pm_jit_cpp_low_putn(l, fn->text, fn->text_len);
    pm_jit_cpp_low_puts(l, "(");
    n_params = 0;
    for (i = 1; i < fn->n_kids; i++) {
        const pm_jit_cpp_ast_t *k = fn->kids[i];
        if (k->kind != PM_JIT_CPP_AST_PARAM) continue;
        if (n_params > 0) pm_jit_cpp_low_puts(l, ", ");
        if (k->n_kids < 1 || k->kids[0]->kind != PM_JIT_CPP_AST_TYPE) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: parameter shape", k->line);
        }
        if (k->kids[0]->text_len == 4 && memcmp(k->kids[0]->text, "auto", 4) == 0) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: auto (no type inference in C)", k->line);
        }
        pm_jit_cpp_low_decl_type_name(l, k->kids[0]->text,
            k->kids[0]->text_len, k->text, k->text_len);
        n_params++;
    }
    pm_jit_cpp_low_puts(l, ") ");
    return pm_jit_cpp_low_stmt(l, body, 0, errbuf, errbuf_len);
}

/*------------------ Lower: class emission ------------------
 *
 * Order of emission for one unit:
 *   1. typedef forward decls for every class (and instantiation)
 *   2. struct definitions (fields; base as an embedded first struct)
 *   3. vtable struct typedefs for polymorphic classes
 *   4. method/ctor/dtor/new function FORWARD decls
 *   5. vtable INSTANCES (after the method decls, before bodies)
 *   6. free functions' forward decls
 *   7. all bodies: methods, ctors, dtors, _new, then free functions
 *
 * Emission is driven by the table; the source order of top-level
 * declarations only matters for step 7's interleaving, which is preserved.
 */
static int pm_cppx_register_class(pm_cppx_table_t *t,
    const pm_jit_cpp_ast_t *cls, const char *cname, char *errbuf,
    size_t errbuf_len) {
    pm_cppx_class_t *c;
    uint32_t i;
    const pm_jit_cpp_ast_t *base = NULL;
    char base_cname[PM_CPPX_NAME_MAX];

    if (cls->kind != PM_JIT_CPP_AST_CLASS) return 0;
    if (t->n_classes >= PM_CPPX_MAX_CLASSES) {
        return pm_jit_cpp_lerr(t->arena, errbuf, errbuf_len,
            "too many classes", cls->line);
    }
    /* base clause: kids before the first ACCESS_SPEC are TYPE bases */
    for (i = 0; i < cls->n_kids; i++) {
        if (cls->kids[i]->kind == PM_JIT_CPP_AST_TYPE) {
            base = cls->kids[i];
        } else {
            break;
        }
    }
    c = pm_cppx_class_add(t,
        pm_cppx_intern(t, cls->text, cls->text_len), cname, cls);
    if (c == NULL) {
        return pm_jit_cpp_lerr(t->arena, errbuf, errbuf_len,
            "class table full", cls->line);
    }
    if (base != NULL) {
        const char *mapped;
        char bbuf[PM_CPPX_NAME_MAX];
        size_t blen = base->text_len;
        if (blen >= sizeof(bbuf)) {
            return pm_jit_cpp_lerr(t->arena, errbuf, errbuf_len,
                "base class name too long", cls->line);
        }
        memcpy(bbuf, base->text, blen); bbuf[blen] = '\0';
        pm_cppx_norm_type(bbuf);
        mapped = pm_cppx_map_type(t, bbuf);
        if (mapped == NULL) {
            return pm_jit_cpp_lerr(t->arena, errbuf, errbuf_len,
                "unsupported: unknown base class template", cls->line);
        }
        /* embed the base as the first field: base struct type + field name */
        snprintf(base_cname, sizeof(base_cname), "%s", mapped);
        c->base_cname = pm_cppx_intern(t, base_cname, strlen(base_cname));
        c->base_field = pm_cppx_intern(t, "base", 4);
    }
    /* vtable order: this class's virtuals AFTER the base's (chain order).
     * The chain is fully built only when bases register first, so the vtable
     * slot list is computed lazily by pm_cppx_vtable_build (after all
     * classes register). */
    return 0;
}

/* virtual method detection: a FUNCTION node is virtual when the base class
 * has a same-named method in its vtable, OR when it is the pure-virtual
 * marker (int_val == 1). The parser drops the `virtual` keyword, so this is
 * the only reliable signal. */
static int pm_cppx_method_is_virtual(pm_cppx_table_t *t,
    const pm_cppx_class_t *cls, const pm_jit_cpp_ast_t *fn) {
    const pm_cppx_class_t *b;
    if (fn->int_val == 1) return 1; /* pure virtual */
    b = cls->base_cname != NULL ? pm_cppx_class_find(t, cls->base_cname) : NULL;
    while (b != NULL) {
        uint32_t k;
        for (k = 0; k < b->n_virtual; k++) {
            const char *vn = b->vt_names[k];
            size_t vnl = strlen(vn);
            size_t ml = fn->text_len;
            if (vnl > ml + 1 && vn[vnl - ml - 1] == '_'
                && vn[vnl - ml] == fn->text[0]
                && memcmp(vn + vnl - ml, fn->text, ml) == 0) {
                return 1;
            }
        }
        b = b->base_cname != NULL ? pm_cppx_class_find(t, b->base_cname) : NULL;
    }
    return 0;
}

/* build the vtable order for a class chain (called after all classes
 * register, in dependency order — bases first). */
static int pm_cppx_vtable_build(pm_cppx_table_t *t, pm_cppx_class_t *c,
    char *errbuf, size_t errbuf_len) {
    uint32_t i;
    if (c->n_virtual > 0) return 0; /* already built */
    /* base chain first (recursively) */
    if (c->base_cname != NULL) {
        pm_cppx_class_t *b = pm_cppx_class_find(t, c->base_cname);
        if (b == NULL) {
            return pm_jit_cpp_lerr(t->arena, errbuf, errbuf_len,
                "unknown base class", c->node->line);
        }
        if (pm_cppx_vtable_build(t, b, errbuf, errbuf_len) != 0) return -1;
        /* inherit base slots (already ordered chain-first); overriding
         * same-name slots are replaced when the own virtuals are added
         * below, keyed on the method-name tail. */
        for (i = 0; i < b->n_virtual; i++) {
            if (c->n_virtual >= PM_CPPX_MAX_VT) {
                return pm_jit_cpp_lerr(t->arena, errbuf, errbuf_len,
                    "vtable too large", c->node->line);
            }
            c->vt_names[c->n_virtual] = b->vt_names[i];
            c->vt_methods[c->n_virtual] = b->vt_methods[i];
            c->vt_rets[c->n_virtual] = b->vt_rets[i];
            c->vt_pure[c->n_virtual] = b->vt_pure[i];
            c->n_virtual++;
        }
    }
    /* own virtuals: pure-virtual markers and base-virtual overrides */
    for (i = 0; i < c->node->n_kids; i++) {
        const pm_jit_cpp_ast_t *k = c->node->kids[i];
        char cname[PM_CPPX_NAME_MAX];
        if (k->kind != PM_JIT_CPP_AST_FUNCTION) continue;
        if (!pm_cppx_method_is_virtual(t, c, k)) continue;
        if ((size_t)snprintf(cname, sizeof(cname), "%s_%s", c->cname,
            k->text_len < sizeof(cname) ? k->text : "") >= sizeof(cname)) {
            return pm_jit_cpp_lerr(t->arena, errbuf, errbuf_len,
                "method name too long", k->line);
        }
        {
            size_t ml = k->text_len;
            /* overriding? replace the inherited slot in place */
            uint32_t slot;
            int replaced = 0;
                for (slot = 0; slot < c->n_virtual; slot++) {
                    const char *vn = c->vt_names[slot];
                    size_t vnl = strlen(vn);
                    if (vnl > ml + 1 && vn[vnl - ml - 1] == '_'
                        && memcmp(vn + vnl - ml, k->text, ml) == 0) {
                        c->vt_names[slot] = pm_cppx_intern(t, cname, strlen(cname));
                        c->vt_methods[slot] = pm_cppx_intern(t, k->text, k->text_len);
                        c->vt_rets[slot] = (k->n_kids > 0
                            && k->kids[0]->kind == PM_JIT_CPP_AST_TYPE)
                            ? pm_cppx_intern(t, k->kids[0]->text,
                                k->kids[0]->text_len)
                            : "void";
                        c->vt_pure[slot] = (k->int_val == 1);
                        replaced = 1;
                        break;
                    }
                }
                if (!replaced) {
                    if (c->n_virtual >= PM_CPPX_MAX_VT) {
                        return pm_jit_cpp_lerr(t->arena, errbuf, errbuf_len,
                            "vtable too large", k->line);
                    }
                    c->vt_names[c->n_virtual] =
                        pm_cppx_intern(t, cname, strlen(cname));
                    c->vt_methods[c->n_virtual] =
                        pm_cppx_intern(t, k->text, k->text_len);
                    c->vt_rets[c->n_virtual] = (k->n_kids > 0
                        && k->kids[0]->kind == PM_JIT_CPP_AST_TYPE)
                        ? pm_cppx_intern(t, k->kids[0]->text,
                            k->kids[0]->text_len)
                        : "void";
                    c->vt_pure[c->n_virtual] = (k->int_val == 1);
                    c->n_virtual++;
                }
        }
    }
    return 0;
}

/* emit one method signature: ret Class_method(Class *this, params) —
 * `header` selects "…;" (decl) vs "… " (body). Returns the C name used. */
static int pm_cppx_emit_method_sig(pm_jit_cpp_low_t *l,
    const pm_cppx_class_t *cls, const pm_jit_cpp_ast_t *fn, int header,
    char *cname_out, size_t cname_cap, char *errbuf, size_t errbuf_len) {
    char cname[PM_CPPX_NAME_MAX];
    const pm_jit_cpp_ast_t *ty = NULL;
    uint32_t i;
    uint32_t np = 0;

    if ((size_t)snprintf(cname, sizeof(cname), "%.*s_%.*s",
        (int)strlen(cls->cname), cls->cname,
        (int)(fn->text_len < 120 ? fn->text_len : 119), fn->text)
        >= sizeof(cname)) {
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
            "method name too long", fn->line);
    }
    if (cname_out != NULL) {
        snprintf(cname_out, cname_cap, "%s", cname);
    }
    /* ctor-shaped members (int_val == 2) are handled elsewhere */
    if (fn->int_val == 2) {
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
            "internal: ctor in method sig", fn->line);
    }
    if (fn->n_kids < 1 || fn->kids[0]->kind != PM_JIT_CPP_AST_TYPE) {
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
            "unsupported: method shape (no return type)", fn->line);
    }
    ty = fn->kids[0];
    if (ty->text_len == 4 && memcmp(ty->text, "auto", 4) == 0) {
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
            "unsupported: auto (no type inference in C)", fn->line);
    }
    /* map the return type through the instantiation table */
    {
        char tb[PM_CPPX_NAME_MAX];
        const char *mapped;
        if (ty->text_len >= sizeof(tb)) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: return type too long", fn->line);
        }
        memcpy(tb, ty->text, ty->text_len); tb[ty->text_len] = '\0';
        mapped = pm_cppx_map_type(l->tbl, tb);
        if (mapped == NULL) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: unknown template type in return", fn->line);
        }
        pm_jit_cpp_low_puts(l, mapped);
    }
    pm_jit_cpp_low_puts(l, " ");
    pm_jit_cpp_low_puts(l, cname);
    pm_jit_cpp_low_puts(l, "(");
    pm_jit_cpp_low_putn(l, cls->cname, strlen(cls->cname));
    pm_jit_cpp_low_puts(l, " *self");
    for (i = 1; i < fn->n_kids; i++) {
        const pm_jit_cpp_ast_t *k = fn->kids[i];
        char tb[PM_CPPX_NAME_MAX];
        const char *mapped;
        if (k->kind != PM_JIT_CPP_AST_PARAM) continue;
        if (k->n_kids < 1 || k->kids[0]->kind != PM_JIT_CPP_AST_TYPE) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: parameter shape", k->line);
        }
        if (k->kids[0]->text_len >= sizeof(tb)) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: parameter type too long", k->line);
        }
        memcpy(tb, k->kids[0]->text, k->kids[0]->text_len);
        tb[k->kids[0]->text_len] = '\0';
        mapped = pm_cppx_map_type(l->tbl, tb);
        if (mapped == NULL) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: unknown template type in parameter", k->line);
        }
        /* track the param's class for method calls on it (references to a
         * class lower to a pointer param, so is_pointer = 1) */
        {
            char base[PM_CPPX_NAME_MAX];
            size_t blen = strlen(mapped);
            int is_ptr = 0;
            const pm_cppx_class_t *pc;
            if (blen < sizeof(base)) {
                memcpy(base, mapped, blen + 1);
                while (blen > 0 && base[blen - 1] == '*') {
                    base[--blen] = '\0'; is_ptr = 1;
                }
                pc = pm_cppx_class_find(l->tbl, base);
                if (pc != NULL) {
                    /* & or * both mean pointer receiver in C */
                    is_ptr = 1;
                    pm_cppx_local_add(l->tbl,
                        pm_cppx_intern(l->tbl, k->text, k->text_len),
                        pc->cname, is_ptr);
                }
            }
        }
        if (np > 0 || 1) pm_jit_cpp_low_puts(l, ", ");
        pm_jit_cpp_low_puts(l, mapped);
        if (k->text_len > 0) {
            pm_jit_cpp_low_puts(l, " ");
            pm_jit_cpp_low_putn(l, k->text, k->text_len);
        }
        np++;
    }
    pm_jit_cpp_low_puts(l, ")");
    if (header) pm_jit_cpp_low_puts(l, ";\n");
    else pm_jit_cpp_low_puts(l, " ");
    return 0;
}

/* emit one ctor: void Class_ctor(Class *self, params) with the init list
 * replayed as base ctor + field assignments. */
static int pm_cppx_emit_ctor(pm_jit_cpp_low_t *l, const pm_cppx_class_t *cls,
    const pm_jit_cpp_ast_t *fn, int header, char *errbuf, size_t errbuf_len) {
    char cname[PM_CPPX_NAME_MAX];
    const pm_jit_cpp_ast_t *body = NULL;
    uint32_t i;

    (void)errbuf; (void)errbuf_len;
    snprintf(cname, sizeof(cname), "%s_ctor", cls->cname);
    if (header) {
        pm_jit_cpp_low_puts(l, "void ");
        pm_jit_cpp_low_puts(l, cname);
        pm_jit_cpp_low_puts(l, "(");
        pm_jit_cpp_low_putn(l, cls->cname, strlen(cls->cname));
        pm_jit_cpp_low_puts(l, " *self");
        for (i = 0; i < fn->n_kids; i++) {
            const pm_jit_cpp_ast_t *k = fn->kids[i];
            if (k->kind != PM_JIT_CPP_AST_PARAM) continue;
            {
                char tb[PM_CPPX_NAME_MAX];
                const char *mapped;
                if (k->n_kids < 1 || k->kids[0]->kind != PM_JIT_CPP_AST_TYPE) {
                    return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                        "unsupported: ctor parameter shape", k->line);
                }
                if (k->kids[0]->text_len >= sizeof(tb)) {
                    return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                        "unsupported: parameter type too long", k->line);
                }
                memcpy(tb, k->kids[0]->text, k->kids[0]->text_len);
                tb[k->kids[0]->text_len] = '\0';
                mapped = pm_cppx_map_type(l->tbl, tb);
                if (mapped == NULL) {
                    return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                        "unsupported: unknown template type in ctor param",
                        k->line);
                }
                pm_jit_cpp_low_puts(l, ", ");
                pm_jit_cpp_low_puts(l, mapped);
                if (k->text_len > 0) {
                    pm_jit_cpp_low_puts(l, " ");
                    pm_jit_cpp_low_putn(l, k->text, k->text_len);
                }
            }
        }
        pm_jit_cpp_low_puts(l, ");\n");
        return 0;
    }
    /* body */
    for (i = 0; i < fn->n_kids; i++) {
        if (fn->kids[i]->kind == PM_JIT_CPP_AST_COMPOUND) body = fn->kids[i];
    }
    pm_jit_cpp_low_puts(l, "void ");
    pm_jit_cpp_low_puts(l, cname);
    pm_jit_cpp_low_puts(l, "(");
    pm_jit_cpp_low_putn(l, cls->cname, strlen(cls->cname));
    pm_jit_cpp_low_puts(l, " *self");
    for (i = 0; i < fn->n_kids; i++) {
        const pm_jit_cpp_ast_t *k = fn->kids[i];
        char tb[PM_CPPX_NAME_MAX];
        const char *mapped;
        if (k->kind != PM_JIT_CPP_AST_PARAM) continue;
        if (k->n_kids < 1 || k->kids[0]->kind != PM_JIT_CPP_AST_TYPE) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: ctor parameter shape", k->line);
        }
        if (k->kids[0]->text_len >= sizeof(tb)) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: parameter type too long", k->line);
        }
        memcpy(tb, k->kids[0]->text, k->kids[0]->text_len);
        tb[k->kids[0]->text_len] = '\0';
        mapped = pm_cppx_map_type(l->tbl, tb);
        if (mapped == NULL) {
            return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                "unsupported: unknown template type in ctor param", k->line);
        }
        pm_jit_cpp_low_puts(l, ", ");
        pm_jit_cpp_low_puts(l, mapped);
        if (k->text_len > 0) {
            pm_jit_cpp_low_puts(l, " ");
            pm_jit_cpp_low_putn(l, k->text, k->text_len);
        }
    }
    pm_jit_cpp_low_puts(l, ") {\n");
    /* vtable install (polymorphic classes) */
    if (cls->n_virtual > 0) {
        pm_jit_cpp_low_puts(l, "    self->vptr = &");
        pm_jit_cpp_low_putn(l, cls->cname, strlen(cls->cname));
        pm_jit_cpp_low_puts(l, "_vtable;\n");
    }
    /* init list: base ctor + member assignments */
    for (i = 0; i < fn->n_kids; i++) {
        const pm_jit_cpp_ast_t *ini = fn->kids[i];
        if (ini->kind != PM_JIT_CPP_AST_INIT_DECL) continue;
        {
            /* base init names may be template ids (Box<int>); map them the
             * same way the base clause maps, so the C base name matches */
            char ib[PM_CPPX_NAME_MAX];
            int is_base = 0;
            if (ini->text_len < sizeof(ib)) {
                memcpy(ib, ini->text, ini->text_len);
                ib[ini->text_len] = '\0';
                pm_cppx_norm_type(ib);
                if (cls->base_cname != NULL
                    && strcmp(ib, cls->base_cname) == 0) {
                    is_base = 1;
                } else {
                    const char *m = pm_cppx_map_type(l->tbl, ib);
                    if (m != NULL && cls->base_cname != NULL
                        && strcmp(m, cls->base_cname) == 0) {
                        is_base = 1;
                    }
                }
            }
            if (is_base) {
                /* base init (possibly template-id text Box<int>) — the
                 * base's C name is the mapped base_cname */
                uint32_t a;
                pm_jit_cpp_low_puts(l, "    ");
                pm_jit_cpp_low_putn(l, cls->base_cname,
                    strlen(cls->base_cname));
                pm_jit_cpp_low_puts(l, "_ctor(&self->base");
                for (a = 0; a < ini->n_kids; a++) {
                    pm_jit_cpp_low_puts(l, ", ");
                    if (pm_jit_cpp_low_expr(l, ini->kids[a], errbuf,
                        errbuf_len) != 0) return -1;
                }
                pm_jit_cpp_low_puts(l, ");\n");
                continue;
            }
        }
        {
            /* member field init */
            pm_jit_cpp_low_puts(l, "    self->");
            pm_jit_cpp_low_putn(l, ini->text, ini->text_len);
            pm_jit_cpp_low_puts(l, " = ");
            if (ini->n_kids == 1) {
                if (pm_jit_cpp_low_expr(l, ini->kids[0], errbuf,
                    errbuf_len) != 0) return -1;
            } else {
                pm_jit_cpp_low_puts(l, "0");
            }
            pm_jit_cpp_low_puts(l, ";\n");
        }
    }
    /* vtable re-install AFTER base ctor: the base ctor set the BASE vtable
     * on the base subobject; the derived ctor must restore its own. */
    if (cls->n_virtual > 0) {
        pm_jit_cpp_low_puts(l, "    self->vptr = &");
        pm_jit_cpp_low_putn(l, cls->cname, strlen(cls->cname));
        pm_jit_cpp_low_puts(l, "_vtable;\n");
    }
    if (body != NULL) {
        uint32_t k;
        for (k = 0; k < body->n_kids; k++) {
            if (pm_jit_cpp_low_stmt(l, body->kids[k], 1, errbuf,
                errbuf_len) != 0) return -1;
        }
    }
    pm_jit_cpp_low_puts(l, "}\n\n");
    return l->dead ? -1 : 0;
}

/* emit the dtor: void Class_dtor(Class *self) — base dtor last. */
static int pm_cppx_emit_dtor(pm_jit_cpp_low_t *l, const pm_cppx_class_t *cls,
    const pm_jit_cpp_ast_t *fn, int header, char *errbuf, size_t errbuf_len) {
    char cname[PM_CPPX_NAME_MAX];
    const pm_jit_cpp_ast_t *body = NULL;
    uint32_t i;
    (void)errbuf; (void)errbuf_len;
    snprintf(cname, sizeof(cname), "%s_dtor", cls->cname);
    if (header) {
        pm_jit_cpp_low_puts(l, "void ");
        pm_jit_cpp_low_puts(l, cname);
        pm_jit_cpp_low_puts(l, "(");
        pm_jit_cpp_low_putn(l, cls->cname, strlen(cls->cname));
        pm_jit_cpp_low_puts(l, " *self);\n");
        return 0;
    }
    for (i = 0; i < fn->n_kids; i++) {
        if (fn->kids[i]->kind == PM_JIT_CPP_AST_COMPOUND) body = fn->kids[i];
    }
    pm_jit_cpp_low_puts(l, "void ");
    pm_jit_cpp_low_puts(l, cname);
    pm_jit_cpp_low_puts(l, "(");
    pm_jit_cpp_low_putn(l, cls->cname, strlen(cls->cname));
    pm_jit_cpp_low_puts(l, " *self) {\n");
    if (body != NULL) {
        uint32_t k;
        for (k = 0; k < body->n_kids; k++) {
            if (pm_jit_cpp_low_stmt(l, body->kids[k], 1, errbuf,
                errbuf_len) != 0) return -1;
        }
    }
    if (cls->base_cname != NULL) {
        pm_jit_cpp_low_puts(l, "    ");
        pm_jit_cpp_low_putn(l, cls->base_cname, strlen(cls->base_cname));
        pm_jit_cpp_low_puts(l, "_dtor(&self->base);\n");
    }
    pm_jit_cpp_low_puts(l, "}\n\n");
    return l->dead ? -1 : 0;
}

/* emit the _new helper: Class *Class_new(params) { malloc + ctor } */
static int pm_cppx_emit_new_helper(pm_jit_cpp_low_t *l,
    const pm_cppx_class_t *cls, const pm_jit_cpp_ast_t *ctor, int header,
    char *errbuf, size_t errbuf_len) {
    char cname[PM_CPPX_NAME_MAX];
    uint32_t i;
    (void)errbuf; (void)errbuf_len;
    snprintf(cname, sizeof(cname), "%s_new", cls->cname);
    pm_jit_cpp_low_putn(l, cls->cname, strlen(cls->cname));
    pm_jit_cpp_low_puts(l, " *");
    pm_jit_cpp_low_puts(l, cname);
    pm_jit_cpp_low_puts(l, "(");
    {
        uint32_t np = 0;
        for (i = 0; i < ctor->n_kids; i++) {
            const pm_jit_cpp_ast_t *k = ctor->kids[i];
            char tb[PM_CPPX_NAME_MAX];
            const char *mapped;
            if (k->kind != PM_JIT_CPP_AST_PARAM) continue;
            if (k->n_kids < 1 || k->kids[0]->kind != PM_JIT_CPP_AST_TYPE) {
                return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                    "unsupported: ctor parameter shape", k->line);
            }
            if (k->kids[0]->text_len >= sizeof(tb)) {
                return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                    "unsupported: parameter type too long", k->line);
            }
            memcpy(tb, k->kids[0]->text, k->kids[0]->text_len);
            tb[k->kids[0]->text_len] = '\0';
            mapped = pm_cppx_map_type(l->tbl, tb);
            if (mapped == NULL) {
                return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                    "unsupported: unknown template type in ctor param",
                    k->line);
            }
            if (np > 0) pm_jit_cpp_low_puts(l, ", ");
            pm_jit_cpp_low_puts(l, mapped);
            if (k->text_len > 0) {
                pm_jit_cpp_low_puts(l, " ");
                pm_jit_cpp_low_putn(l, k->text, k->text_len);
            }
            np++;
        }
    }
    if (header) {
        pm_jit_cpp_low_puts(l, ");\n");
        return 0;
    }
    pm_jit_cpp_low_puts(l, ") {\n    ");
    pm_jit_cpp_low_putn(l, cls->cname, strlen(cls->cname));
    pm_jit_cpp_low_puts(l, " *p = (");
    pm_jit_cpp_low_putn(l, cls->cname, strlen(cls->cname));
    pm_jit_cpp_low_puts(l, " *)malloc(sizeof(");
    pm_jit_cpp_low_putn(l, cls->cname, strlen(cls->cname));
    pm_jit_cpp_low_puts(l, "));\n    ");
    pm_jit_cpp_low_putn(l, cls->cname, strlen(cls->cname));
    pm_jit_cpp_low_puts(l, "_ctor(p");
    for (i = 0; i < ctor->n_kids; i++) {
        const pm_jit_cpp_ast_t *k = ctor->kids[i];
        if (k->kind != PM_JIT_CPP_AST_PARAM) continue;
        pm_jit_cpp_low_puts(l, ", ");
        pm_jit_cpp_low_putn(l, k->text, k->text_len);
    }
    pm_jit_cpp_low_puts(l, ");\n    return p;\n}\n\n");
    return l->dead ? -1 : 0;
}

/* find a class's single ctor (parser allows at most the last-declared one
 * to be recorded with a body; decls without bodies are ignored) */
static const pm_jit_cpp_ast_t *pm_cppx_find_ctor(const pm_cppx_class_t *cls) {
    uint32_t i;
    const pm_jit_cpp_ast_t *found = NULL;
    for (i = 0; i < cls->node->n_kids; i++) {
        const pm_jit_cpp_ast_t *k = cls->node->kids[i];
        if (k->kind == PM_JIT_CPP_AST_FUNCTION && k->int_val == 2) {
            found = k; /* last ctor wins; overload subset is single-ctor */
        }
    }
    return found;
}

/* emit struct definitions + vtable typedefs + method forward decls for all
 * registered classes. Called once, after registration + vtable build. */
static int pm_cppx_emit_decls(pm_jit_cpp_low_t *l, char *errbuf,
    size_t errbuf_len) {
    uint32_t ci, i;
    pm_cppx_table_t *t = l->tbl;

    /* 1. struct forward decls (typedef struct X X;) */
    for (ci = 0; ci < t->n_classes; ci++) {
        pm_jit_cpp_low_puts(l, "typedef struct ");
        pm_jit_cpp_low_putn(l, t->classes[ci].cname,
            strlen(t->classes[ci].cname));
        pm_jit_cpp_low_puts(l, " ");
        pm_jit_cpp_low_putn(l, t->classes[ci].cname,
            strlen(t->classes[ci].cname));
        pm_jit_cpp_low_puts(l, ";\n");
    }
    pm_jit_cpp_low_puts(l, "\n");
    /* 2. struct bodies */
    for (ci = 0; ci < t->n_classes; ci++) {
        const pm_cppx_class_t *c = &t->classes[ci];
        pm_jit_cpp_low_puts(l, "struct ");
        pm_jit_cpp_low_putn(l, c->cname, strlen(c->cname));
        pm_jit_cpp_low_puts(l, " {\n");
        if (c->n_virtual > 0 || (c->base_cname != NULL
            && pm_cppx_class_find(t, c->base_cname)->n_virtual > 0)) {
            /* the vptr type is the ROOT of the chain's vtable */
            const pm_cppx_class_t *root = c;
            while (root->base_cname != NULL) {
                const pm_cppx_class_t *b = pm_cppx_class_find(t, root->base_cname);
                if (b == NULL) break;
                root = b;
            }
            pm_jit_cpp_low_puts(l, "    const struct ");
            pm_jit_cpp_low_putn(l, root->cname, strlen(root->cname));
            pm_jit_cpp_low_puts(l, "_vt *vptr;\n");
        }
        if (c->base_cname != NULL) {
            pm_jit_cpp_low_puts(l, "    struct ");
            pm_jit_cpp_low_putn(l, c->base_cname, strlen(c->base_cname));
            pm_jit_cpp_low_puts(l, " base;\n");
        }
        for (i = 0; i < c->node->n_kids; i++) {
            const pm_jit_cpp_ast_t *k = c->node->kids[i];
            char tb[PM_CPPX_NAME_MAX];
            const char *mapped;
            if (k->kind != PM_JIT_CPP_AST_VAR) continue;
            if (k->n_kids < 1 || k->kids[0]->kind != PM_JIT_CPP_AST_TYPE) {
                return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                    "unsupported: field shape", k->line);
            }
            if (k->kids[0]->text_len >= sizeof(tb)) {
                return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                    "unsupported: field type too long", k->line);
            }
            memcpy(tb, k->kids[0]->text, k->kids[0]->text_len);
            tb[k->kids[0]->text_len] = '\0';
            mapped = pm_cppx_map_type(t, tb);
            if (mapped == NULL) {
                return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                    "unsupported: unknown template type in field", k->line);
            }
            pm_jit_cpp_low_puts(l, "    ");
            pm_jit_cpp_low_puts(l, mapped);
            pm_jit_cpp_low_puts(l, " ");
            pm_jit_cpp_low_putn(l, k->text, k->text_len);
            pm_jit_cpp_low_puts(l, ";\n");
        }
        pm_jit_cpp_low_puts(l, "};\n\n");
    }
    /* 3. vtable typedefs (only polymorphic chains, at the chain ROOT —
     * every class in the chain shares the root's vtable layout) */
    for (ci = 0; ci < t->n_classes; ci++) {
        const pm_cppx_class_t *c = &t->classes[ci];
        const pm_cppx_class_t *root = c;
        uint32_t k;
        if (c->base_cname != NULL) continue; /* only roots define a layout */
        if (c->n_virtual == 0) continue;
        while (root->base_cname != NULL) {
            const pm_cppx_class_t *b = pm_cppx_class_find(t, root->base_cname);
            if (b == NULL) break;
            root = b;
        }
        (void)root;
        pm_jit_cpp_low_puts(l, "struct ");
        pm_jit_cpp_low_putn(l, c->cname, strlen(c->cname));
        pm_jit_cpp_low_puts(l, "_vt {\n");
        for (k = 0; k < c->n_virtual; k++) {
            pm_jit_cpp_low_puts(l, "    ");
            pm_jit_cpp_low_putn(l, c->vt_rets[k], strlen(c->vt_rets[k]));
            pm_jit_cpp_low_puts(l, " (*");
            pm_jit_cpp_low_putn(l, c->vt_methods[k], strlen(c->vt_methods[k]));
            pm_jit_cpp_low_puts(l, ")(void *);\n");
        }
        pm_jit_cpp_low_puts(l, "};\n\n");
    }
    /* 4. method/ctor/dtor/new forward decls (every class) */
    for (ci = 0; ci < t->n_classes; ci++) {
        const pm_cppx_class_t *c = &t->classes[ci];
        for (i = 0; i < c->node->n_kids; i++) {
            const pm_jit_cpp_ast_t *k = c->node->kids[i];
            if (k->kind != PM_JIT_CPP_AST_FUNCTION) continue;
            if (k->int_val == 2) {
                if (pm_cppx_emit_ctor(l, c, k, 1, errbuf, errbuf_len) != 0) {
                    return -1;
                }
            } else if (k->text_len > 0 && k->text[0] == '~') {
                if (pm_cppx_emit_dtor(l, c, k, 1, errbuf, errbuf_len) != 0) {
                    return -1;
                }
            } else if (k->int_val == 1) {
                /* pure virtual: no definition exists — the vtable instance
                 * of a DERIVED class supplies the slot; the abstract base
                 * itself gets no decl (it can never be constructed). */
            } else {
                if (pm_cppx_emit_method_sig(l, c, k, 1, NULL, 0, errbuf,
                    errbuf_len) != 0) return -1;
            }
        }
        if (pm_cppx_find_ctor(c) != NULL) {
            if (pm_cppx_emit_new_helper(l, c, pm_cppx_find_ctor(c), 1,
                errbuf, errbuf_len) != 0) return -1;
        }
    }
    /* 5. vtable instances (function decls exist; bodies come later) */
    for (ci = 0; ci < t->n_classes; ci++) {
        const pm_cppx_class_t *c = &t->classes[ci];
        uint32_t k;
        const pm_cppx_class_t *root = c;
        if (c->n_virtual == 0) continue;
        while (root->base_cname != NULL) {
            const pm_cppx_class_t *b = pm_cppx_class_find(t, root->base_cname);
            if (b == NULL) break;
            root = b;
        }
        /* a class with an unimplemented pure virtual has no complete vtable:
         * refuse (abstract classes must be completed by a derived override;
         * the dtor slot may be satisfied by the base) */
        pm_jit_cpp_low_puts(l, "static const struct ");
        pm_jit_cpp_low_putn(l, root->cname, strlen(root->cname));
        pm_jit_cpp_low_puts(l, "_vt ");
        pm_jit_cpp_low_putn(l, c->cname, strlen(c->cname));
        pm_jit_cpp_low_puts(l, "_vtable = {\n");
        for (k = 0; k < c->n_virtual; k++) {
            if (c->vt_pure[k]) {
                /* unimplemented pure virtual: the slot is a null entry; the
                 * abstract class must never be instantiated directly */
                pm_jit_cpp_low_puts(l, "    0");
            } else {
                pm_jit_cpp_low_puts(l, "    (");
                pm_jit_cpp_low_putn(l, c->vt_rets[k], strlen(c->vt_rets[k]));
                pm_jit_cpp_low_puts(l, " (*)(void *))");
                pm_jit_cpp_low_putn(l, c->vt_names[k], strlen(c->vt_names[k]));
            }
            pm_jit_cpp_low_puts(l, k + 1 < c->n_virtual ? ",\n" : "\n");
        }
        pm_jit_cpp_low_puts(l, "};\n\n");
    }
    return l->dead ? -1 : 0;
}

/* emit all method/ctor/dtor/_new bodies for one class */
static int pm_cppx_emit_bodies(pm_jit_cpp_low_t *l, const pm_cppx_class_t *c,
    char *errbuf, size_t errbuf_len) {
    uint32_t i;
    for (i = 0; i < c->node->n_kids; i++) {
        const pm_jit_cpp_ast_t *k = c->node->kids[i];
        if (k->kind != PM_JIT_CPP_AST_FUNCTION) continue;
        if (k->int_val == 2) {
            if (pm_cppx_emit_ctor(l, c, k, 0, errbuf, errbuf_len) != 0) {
                return -1;
            }
        } else if (k->text_len > 0 && k->text[0] == '~') {
            if (pm_cppx_emit_dtor(l, c, k, 0, errbuf, errbuf_len) != 0) {
                return -1;
            }
        } else if (k->int_val == 1) {
            /* pure virtual: no body */
        } else {
            char mname[PM_CPPX_NAME_MAX];
            if (pm_cppx_emit_method_sig(l, c, k, 0, mname, sizeof(mname),
                errbuf, errbuf_len) != 0) return -1;
            {
                const pm_jit_cpp_ast_t *body = NULL;
                uint32_t j;
                for (j = 1; j < k->n_kids; j++) {
                    if (k->kids[j]->kind == PM_JIT_CPP_AST_COMPOUND) {
                        body = k->kids[j];
                    }
                }
                if (body == NULL) continue;
                /* method body: `this` maps to self */
                l->tbl->cur_class = c;
                pm_cppx_locals_clear(l->tbl);
                pm_cppx_local_add(l->tbl, "this", c->cname, 1);
                /* params */
                for (j = 1; j < k->n_kids; j++) {
                    const pm_jit_cpp_ast_t *pa = k->kids[j];
                    char tb[PM_CPPX_NAME_MAX];
                    const char *mapped;
                    const pm_cppx_class_t *pc;
                    if (pa->kind != PM_JIT_CPP_AST_PARAM) continue;
                    if (pa->n_kids < 1
                        || pa->kids[0]->kind != PM_JIT_CPP_AST_TYPE) continue;
                    if (pa->kids[0]->text_len >= sizeof(tb)) continue;
                    memcpy(tb, pa->kids[0]->text, pa->kids[0]->text_len);
                    tb[pa->kids[0]->text_len] = '\0';
                    mapped = pm_cppx_map_type(l->tbl, tb);
                    if (mapped == NULL) continue;
                    {
                        char base[PM_CPPX_NAME_MAX];
                        size_t blen = strlen(mapped);
                        if (blen >= sizeof(base)) continue;
                        memcpy(base, mapped, blen + 1);
                        while (blen > 0 && (base[blen - 1] == '*'
                            || base[blen - 1] == '&')) base[--blen] = '\0';
                        pc = pm_cppx_class_find(l->tbl, base);
                        if (pc != NULL && pa->text_len > 0) {
                            pm_cppx_local_add(l->tbl,
                                pm_cppx_intern(l->tbl, pa->text, pa->text_len),
                                pc->cname, 1);
                        }
                    }
                }
                pm_jit_cpp_low_puts(l, "{\n");
                {
                    uint32_t q;
                    for (q = 0; q < body->n_kids; q++) {
                        if (pm_jit_cpp_low_stmt(l, body->kids[q], 1, errbuf,
                            errbuf_len) != 0) return -1;
                    }
                }
                pm_jit_cpp_low_puts(l, "}\n\n");
                l->tbl->cur_class = NULL;
                pm_cppx_locals_clear(l->tbl);
            }
        }
    }
    if (pm_cppx_find_ctor(c) != NULL) {
        if (pm_cppx_emit_new_helper(l, c, pm_cppx_find_ctor(c), 0, errbuf,
            errbuf_len) != 0) return -1;
    }
    return l->dead ? -1 : 0;
}

/* Emit forward declarations for every function before the definitions, so
 * call order (call a function defined later in the file) is C-legal. */
/* one function's forward decl: "ret name(params);" — works for a unit child
 * and a parked template-fn clone alike. */
static int pm_jit_cpp_low_fn_decl(pm_jit_cpp_low_t *l,
    const pm_jit_cpp_ast_t *d, char *errbuf, size_t errbuf_len) {
    const pm_jit_cpp_ast_t *ty = NULL;
    uint32_t j;
    int has_body = 0;
    if (d->kind != PM_JIT_CPP_AST_FUNCTION) return 0;
    if (d->n_kids < 1 || d->kids[0]->kind != PM_JIT_CPP_AST_TYPE) return 0;
    ty = d->kids[0];
    if (ty->text_len == 4 && memcmp(ty->text, "auto", 4) == 0) {
        return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
            "unsupported: auto (no type inference in C)", d->line);
    }
    for (j = 1; j < d->n_kids; j++) {
        if (d->kids[j]->kind == PM_JIT_CPP_AST_COMPOUND) has_body = 1;
    }
    if (!has_body) return 0;
    pm_jit_cpp_low_decl_type_name(l, ty->text, ty->text_len, d->text, d->text_len);
    pm_jit_cpp_low_puts(l, "(");
    {
        uint32_t np = 0;
        for (j = 1; j < d->n_kids; j++) {
            const pm_jit_cpp_ast_t *k = d->kids[j];
            if (k->kind != PM_JIT_CPP_AST_PARAM) continue;
            if (np > 0) pm_jit_cpp_low_puts(l, ", ");
            if (k->n_kids < 1 || k->kids[0]->kind != PM_JIT_CPP_AST_TYPE) {
                return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                    "unsupported: parameter shape", k->line);
            }
            if (k->kids[0]->text_len == 4 && memcmp(k->kids[0]->text, "auto", 4) == 0) {
                return pm_jit_cpp_lerr(l->arena, errbuf, errbuf_len,
                    "unsupported: auto (no type inference in C)", k->line);
            }
            pm_jit_cpp_low_decl_type_name(l, k->kids[0]->text,
                k->kids[0]->text_len, k->text, k->text_len);
            np++;
        }
    }
    pm_jit_cpp_low_puts(l, ");\n");
    return l->dead ? -1 : 0;
}

static int pm_jit_cpp_low_forward_decls(pm_jit_cpp_low_t *l,
    const pm_jit_cpp_ast_t *unit, char *errbuf, size_t errbuf_len) {
    uint32_t i;
    for (i = 0; i < unit->n_kids; i++) {
        if (pm_jit_cpp_low_fn_decl(l, unit->kids[i], errbuf,
                errbuf_len) != 0) {
            return -1;
        }
    }
    return l->dead ? -1 : 0;
}

/* template instantiation: Box<int> used at a decl/base/new site. Clones the
 * template CLASS with T->arg substitution and registers it as Box_int. */
static int pm_cppx_instantiate(pm_cppx_table_t *t, const char *tpl_name,
    const char *args, char *errbuf, size_t errbuf_len) {
    const pm_jit_cpp_ast_t *td = pm_cppx_find_template(t, tpl_name);
    const pm_jit_cpp_ast_t *tpl_cls;
    const pm_jit_cpp_ast_t *param;
    char cname[PM_CPPX_NAME_MAX];
    const char *cn;
    uint32_t i;

    if (td == NULL) {
        return pm_jit_cpp_lerr(t->arena, errbuf, errbuf_len,
            "unsupported: unknown template", 0);
    }
    tpl_cls = td->n_kids > 0 ? td->kids[td->n_kids - 1] : NULL;
    if (tpl_cls == NULL || tpl_cls->kind != PM_JIT_CPP_AST_CLASS) {
        return pm_jit_cpp_lerr(t->arena, errbuf, errbuf_len,
            "unsupported: template of non-class", 0);
    }
    /* one type parameter (the parser records NAME kids) */
    if (td->n_kids < 2) {
        return pm_jit_cpp_lerr(t->arena, errbuf, errbuf_len,
            "unsupported: template needs exactly one type parameter", 0);
    }
    param = td->kids[0];
    /* mangled name: Box<int> -> Box_int (chars outside [A-Za-z0-9_] -> '_') */
    {
        size_t nlen = strlen(tpl_name);
        size_t alen = strlen(args);
        size_t k;
        if (nlen + 1 + alen >= sizeof(cname)) {
            return pm_jit_cpp_lerr(t->arena, errbuf, errbuf_len,
                "unsupported: instantiation name too long", 0);
        }
        memcpy(cname, tpl_name, nlen);
        cname[nlen] = '_';
        memcpy(cname + nlen + 1, args, alen);
        cname[nlen + 1 + alen] = '\0';
        for (k = 0; k < nlen + 1 + alen; k++) {
            char ch = cname[k];
            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                || (ch >= '0' && ch <= '9') || ch == '_')) {
                cname[k] = '_';
            }
        }
    }
    /* already instantiated? */
    for (i = 0; i < t->n_insts; i++) {
        if (strcmp(t->insts[i].cname, cname) == 0) return 0;
    }
    /* clone + substitute + rename to the mangled name */
    cn = pm_cppx_intern(t, cname, strlen(cname));
    if (cn == NULL) {
        return pm_jit_cpp_lerr(t->arena, errbuf, errbuf_len,
            "arena exhausted", 0);
    }
    {
        pm_jit_cpp_ast_t *clone = pm_cppx_subst_class(t, tpl_cls,
            param->text, args, cn);
        if (clone == NULL) {
            return pm_jit_cpp_lerr(t->arena, errbuf, errbuf_len,
                "arena exhausted", 0);
        }
        /* register the clone; then record the instantiation */
        if (pm_cppx_register_class(t, clone, cn, errbuf, errbuf_len) != 0) {
            return -1;
        }
        if (t->n_insts >= PM_CPPX_MAX_INSTS) {
            return pm_jit_cpp_lerr(t->arena, errbuf, errbuf_len,
                "too many instantiations", 0);
        }
        t->insts[t->n_insts].tpl_name =
            pm_cppx_intern(t, tpl_name, strlen(tpl_name));
        t->insts[t->n_insts].args = pm_cppx_intern(t, args, strlen(args));
        t->insts[t->n_insts].cname = cn;
        t->n_insts++;
    }
    /* instantiate the base chain of the clone too (Box<int> deriving from
     * Box<int> is tautological; other bases resolve via map_type later) */
    return 0;
}

/* scan a type text for template uses and instantiate them: "Box<int>",
 * "std::unique_ptr<LoudBox>", nested "A<B<int> >". Only the OUTER template
 * is instantiated here (the inner arg is a plain type once mapped). */
static int pm_cppx_instantiate_uses(pm_cppx_table_t *t, const char *ty,
    char *errbuf, size_t errbuf_len) {
    const char *lt = strchr(ty, '<');
    const char *s = ty;
    while ((lt = strchr(s, '<')) != NULL) {
        /* find the matching close for THIS bracket */
        int depth = 0;
        const char *e = lt;
        const char *name_end = lt;
        const char *name_start;
        while (*e != '\0') {
            if (*e == '<') depth++;
            if (*e == '>') {
                depth--;
                if (depth == 0) break;
            }
            e++;
        }
        if (*e != '>') return 0; /* unbalanced — leave to the caller */
        /* the template name: identifier run ending at name_end */
        name_start = name_end;
        while (name_start > s
            && (name_start[-1] == '_' || name_start[-1] == ':'
                || (name_start[-1] >= 'a' && name_start[-1] <= 'z')
                || (name_start[-1] >= 'A' && name_start[-1] <= 'Z')
                || (name_start[-1] >= '0' && name_start[-1] <= '9'))) {
            name_start--;
        }
        /* skip qualified prefixes (std::) — instantiate the LAST segment */
        {
            const char *colon = NULL;
            const char *q;
            for (q = name_start; q < name_end; q++) {
                if (*q == ':') colon = q;
            }
            if (colon != NULL) name_start = colon + 1;
        }
        if (name_end > name_start) {
            char nb[PM_CPPX_NAME_MAX];
            char ab[PM_CPPX_NAME_MAX];
            size_t nlen = (size_t)(name_end - name_start);
            size_t alen = (size_t)(e - lt - 1);
            if (nlen < sizeof(nb) && alen < sizeof(ab)) {
                memcpy(nb, name_start, nlen); nb[nlen] = '\0';
                memcpy(ab, lt + 1, alen); ab[alen] = '\0';
                pm_cppx_norm_type(nb);
                pm_cppx_norm_type(ab);
                /* only instantiate when a template by that name exists and
                 * the arg is a known plain type (not another template) */
                if (pm_cppx_find_template(t, nb) != NULL
                    && strchr(ab, '<') == NULL) {
                    if (pm_cppx_instantiate(t, nb, ab, errbuf, errbuf_len) != 0) {
                        return -1;
                    }
                }
            }
        }
        s = e + 1;
    }
    return 0;
}

/* walk the whole unit instantiating every template used in decls, bases,
 * new-expressions and calls. Template FUNCTIONS get their instantiation
 * recorded by the call path (identity<int>) and emitted like free funcs. */
static int pm_cppx_scan_uses(pm_cppx_table_t *t, const pm_jit_cpp_ast_t *n,
    char *errbuf, size_t errbuf_len) {
    uint32_t i;
    if (n == NULL) return 0;
    if (n->kind == PM_JIT_CPP_AST_TYPE && n->text != NULL) {
        char tb[PM_CPPX_NAME_MAX];
        if (n->text_len < sizeof(tb)) {
            memcpy(tb, n->text, n->text_len); tb[n->text_len] = '\0';
            if (pm_cppx_instantiate_uses(t, tb, errbuf, errbuf_len) != 0) {
                return -1;
            }
        }
    }
    if (n->kind == PM_JIT_CPP_AST_NEW_EXPR && n->text != NULL) {
        char tb[PM_CPPX_NAME_MAX];
        if (n->text_len < sizeof(tb)) {
            memcpy(tb, n->text, n->text_len); tb[n->text_len] = '\0';
            if (pm_cppx_instantiate_uses(t, tb, errbuf, errbuf_len) != 0) {
                return -1;
            }
        }
    }
    for (i = 0; i < n->n_kids; i++) {
        if (pm_cppx_scan_uses(t, n->kids[i], errbuf, errbuf_len) != 0) {
            return -1;
        }
    }
    return 0;
}

/* template FUNCTION instantiation: identity<int> -> identity_int, recorded
 * so the free-function pass emits it. Stored as insts with tpl_name="fn". */
static int pm_cppx_instantiate_fn(pm_cppx_table_t *t, const char *name,
    const char *args, char *errbuf, size_t errbuf_len) {
    const pm_jit_cpp_ast_t *td = pm_cppx_find_template(t, name);
    const pm_jit_cpp_ast_t *fn;
    char cname[PM_CPPX_NAME_MAX];
    size_t nlen = strlen(name);
    size_t alen = strlen(args);
    size_t k;
    uint32_t i;
    if (td == NULL) {
        return pm_jit_cpp_lerr(t->arena, errbuf, errbuf_len,
            "unsupported: unknown template function", 0);
    }
    fn = td->n_kids > 0 ? td->kids[td->n_kids - 1] : NULL;
    if (fn == NULL || fn->kind != PM_JIT_CPP_AST_FUNCTION) {
        return pm_jit_cpp_lerr(t->arena, errbuf, errbuf_len,
            "unsupported: template of non-function", 0);
    }
    if (nlen + 1 + alen >= sizeof(cname)) {
        return pm_jit_cpp_lerr(t->arena, errbuf, errbuf_len,
            "unsupported: instantiation name too long", 0);
    }
    memcpy(cname, name, nlen);
    cname[nlen] = '_';
    memcpy(cname + nlen + 1, args, alen);
    cname[nlen + 1 + alen] = '\0';
    for (k = 0; k < nlen + 1 + alen; k++) {
        char ch = cname[k];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
            || (ch >= '0' && ch <= '9') || ch == '_')) {
            cname[k] = '_';
        }
    }
    for (i = 0; i < t->n_insts; i++) {
        if (strcmp(t->insts[i].cname, cname) == 0) return 0;
    }
    /* clone the function with T->arg substitution and rename */
    {
        const pm_jit_cpp_ast_t *param = td->kids[0];
        pm_jit_cpp_ast_t *clone;
        const char *cn = pm_cppx_intern(t, cname, strlen(cname));
        if (cn == NULL) {
            return pm_jit_cpp_lerr(t->arena, errbuf, errbuf_len,
                "arena exhausted", 0);
        }
        clone = pm_cppx_subst_node(t, fn, param->text, args);
        if (clone == NULL) {
            return pm_jit_cpp_lerr(t->arena, errbuf, errbuf_len,
                "arena exhausted", 0);
        }
        clone->text = cn;
        clone->text_len = strlen(cn);
        /* remember the instantiated function for the free-function pass */
        if (t->n_templates >= PM_CPPX_MAX_INSTS) {
            return pm_jit_cpp_lerr(t->arena, errbuf, errbuf_len,
                "too many instantiations", 0);
        }
        /* park the clone on the insts list too: tpl_name NULL marks fn-inst */
        if (t->n_insts >= PM_CPPX_MAX_INSTS) {
            return pm_jit_cpp_lerr(t->arena, errbuf, errbuf_len,
                "too many instantiations", 0);
        }
        t->insts[t->n_insts].tpl_name = NULL; /* fn instantiation marker */
        t->insts[t->n_insts].args = NULL;
        t->insts[t->n_insts].cname = cn;
        t->n_insts++;
        /* the cloned FUNCTION node needs to reach the emit pass: keep it in
         * the templates array after the decls (append-only scratch) */
        t->templates[t->n_templates++] = clone;
        /* fn clones ride as TEMPLATE_DECL-shaped entries would break the
         * class scan; instead mark via the last-kid trick: wrap in a unit-
         * like array the lower pass reads. Simplest: store on templates[]
         * and the lower pass treats a FUNCTION there as an instantiated fn. */
    }
    return 0;
}

/* walk the unit finding template FUNCTION calls (identity<int>(x)) and
 * instantiate them. TEMPLATE_REF nodes carry the full "name<args>" text. */
static int pm_cppx_scan_fn_calls(pm_cppx_table_t *t,
    const pm_jit_cpp_ast_t *n, char *errbuf, size_t errbuf_len) {
    uint32_t i;
    if (n == NULL) return 0;
    if (n->kind == PM_JIT_CPP_AST_TEMPLATE_REF && n->text != NULL
        && strchr(n->text, '<') != NULL) {
        char tb[PM_CPPX_NAME_MAX];
        if (n->text_len < sizeof(tb)) {
            const char *lt;
            memcpy(tb, n->text, n->text_len); tb[n->text_len] = '\0';
            lt = strchr(tb, '<');
            {
                size_t nlen = (size_t)(lt - tb);
                size_t alen = strlen(tb) - nlen - 2;
                char nb[PM_CPPX_NAME_MAX];
                char ab[PM_CPPX_NAME_MAX];
                if (nlen < sizeof(nb) && alen < sizeof(ab)) {
                    memcpy(nb, tb, nlen); nb[nlen] = '\0';
                    memcpy(ab, lt + 1, alen); ab[alen] = '\0';
                    pm_cppx_norm_type(nb);
                    pm_cppx_norm_type(ab);
                    /* only a template FUNCTION by that name (not a class) */
                    {
                        const pm_jit_cpp_ast_t *td =
                            pm_cppx_find_template(t, nb);
                        const pm_jit_cpp_ast_t *inner = td != NULL
                            && td->n_kids > 0 ? td->kids[td->n_kids - 1]
                            : NULL;
                        if (td != NULL && inner != NULL
                            && inner->kind == PM_JIT_CPP_AST_FUNCTION
                            && strchr(ab, '<') == NULL) {
                            if (pm_cppx_instantiate_fn(t, nb, ab, errbuf,
                                errbuf_len) != 0) {
                                return -1;
                            }
                        }
                    }
                }
            }
        }
    }
    for (i = 0; i < n->n_kids; i++) {
        if (pm_cppx_scan_fn_calls(t, n->kids[i], errbuf, errbuf_len) != 0) {
            return -1;
        }
    }
    return 0;
}

int32_t pm_metal_jit_cpp_lower(pm_util_mem_arena_t *arena,
    const pm_jit_cpp_ast_t *unit, char **c_out, size_t *c_out_len,
    char *errbuf, size_t errbuf_len) {
    pm_jit_cpp_low_t l;
    pm_cppx_table_t tbl;
    uint32_t i;
    int rc;

    if (arena == NULL || unit == NULL || c_out == NULL || c_out_len == NULL) {
        return pm_jit_cpp_err(errbuf, errbuf_len, "lower: bad args", 0);
    }
    if (unit->kind != PM_JIT_CPP_AST_TRANSLATION_UNIT) {
        return pm_jit_cpp_err(errbuf, errbuf_len, "lower: not a unit", 0);
    }
    if (errbuf != NULL && errbuf_len > 0) errbuf[0] = '\0';

    pm_cppx_table_init(&tbl, arena);
    memset(&l, 0, sizeof(l));
    l.arena = arena;
    l.tbl = &tbl;

    /* pass 1: record template decls (classes and functions) */
    for (i = 0; i < unit->n_kids; i++) {
        if (unit->kids[i]->kind == PM_JIT_CPP_AST_TEMPLATE_DECL) {
            if (tbl.n_templates < PM_CPPX_MAX_INSTS) {
                tbl.templates[tbl.n_templates++] = unit->kids[i];
            } else {
                return pm_jit_cpp_lerr(arena, errbuf, errbuf_len,
                    "too many templates", unit->kids[i]->line);
            }
        }
    }
    /* pass 2: instantiate every template used anywhere in the unit */
    if (pm_cppx_scan_uses(&tbl, unit, errbuf, errbuf_len) != 0) return -1;
    /* template function calls: TEMPLATE_REF kids of CALL nodes */
    for (i = 0; i < unit->n_kids; i++) {
        if (pm_cppx_scan_fn_calls(&tbl, unit->kids[i], errbuf,
            errbuf_len) != 0) return -1;
    }
    /* pass 3: register non-template classes */
    for (i = 0; i < unit->n_kids; i++) {
        const pm_jit_cpp_ast_t *d = unit->kids[i];
        if (d->kind != PM_JIT_CPP_AST_CLASS) continue;
        if (pm_cppx_register_class(&tbl, d,
                pm_cppx_intern(&tbl, d->text, d->text_len),
                errbuf, errbuf_len) != 0) {
            return -1;
        }
    }
    /* pass 3b: register template-CLASS instantiations created in pass 2
     * (they appended to classes directly inside pm_cppx_instantiate) */
    /* pass 4: build vtables (base chains first — classes registered in
     * dependency order: bases appear before derived in the source) */
    for (i = 0; i < tbl.n_classes; i++) {
        if (pm_cppx_vtable_build(&tbl, &tbl.classes[i], errbuf,
            errbuf_len) != 0) {
            return -1;
        }
    }
    /* pass 5: forward decls (classes + free functions) */
    pm_jit_cpp_low_puts(&l, "/* generated by pymergetic.metal.jit.cpp lower */\n");
    /* every TU-level preprocessor line first: includes must precede any
     * emitted declaration that uses their types (the source's own headers
     * define the enums/typedefs the forward decls reference). C typedefs
     * from the source ride here too — the forward decls reference those
     * struct types (pm_jit_cpp_out_t etc.) before any function body. */
    for (i = 0; i < unit->n_kids; i++) {
        if (unit->kids[i]->kind == PM_JIT_CPP_AST_PP) {
            pm_jit_cpp_low_putn(&l, unit->kids[i]->text, unit->kids[i]->text_len);
            pm_jit_cpp_low_puts(&l, "\n");
        }
    }
    for (i = 0; i < unit->n_kids; i++) {
        if (unit->kids[i]->kind == PM_JIT_CPP_AST_TYPEDEF) {
            pm_jit_cpp_low_putn(&l, unit->kids[i]->text, unit->kids[i]->text_len);
            pm_jit_cpp_low_puts(&l, ";\n");
        }
    }
    /* the lowerer's own stdlib include only when the source didn't have
     * one — keeps the output idempotent (a re-lower must not add lines) */
    {
        int has_stdlib = 0;
        for (i = 0; i < unit->n_kids; i++) {
            const pm_jit_cpp_ast_t *d = unit->kids[i];
            if (d->kind == PM_JIT_CPP_AST_PP && d->text_len >= 19
                && memcmp(d->text, "#include <stdlib.h>", 19) == 0) {
                has_stdlib = 1;
                break;
            }
        }
        if (!has_stdlib) {
            pm_jit_cpp_low_puts(&l, "#include <stdlib.h>\n");
        }
    }
    if (pm_cppx_emit_decls(&l, errbuf, errbuf_len) != 0) return -1;
    pm_jit_cpp_low_puts(&l, "\n");
    /* free-function forward decls + instantiated template fn decls */
    if (pm_jit_cpp_low_forward_decls(&l, unit, errbuf, errbuf_len) != 0) {
        return -1;
    }
    /* parked template-fn clones need their forward decls too — they emit
     * after every unit function but are callable from them */
    for (i = 0; i < tbl.n_templates; i++) {
        const pm_jit_cpp_ast_t *td = tbl.templates[i];
        if (td->kind == PM_JIT_CPP_AST_FUNCTION) {
            if (pm_jit_cpp_low_fn_decl(&l, td, errbuf, errbuf_len) != 0) {
                return -1;
            }
        }
    }
    /* pass 6: bodies — class members first, then free functions in source
     * order (classes emit theirs via the table) */
    for (i = 0; i < tbl.n_classes; i++) {
        if (pm_cppx_emit_bodies(&l, &tbl.classes[i], errbuf,
            errbuf_len) != 0) {
            return -1;
        }
    }
    for (i = 0; i < unit->n_kids; i++) {
        const pm_jit_cpp_ast_t *d = unit->kids[i];
        switch (d->kind) {
        case PM_JIT_CPP_AST_FUNCTION: {
            /* body-less prototypes emit nothing — not even the separator
             * newline (idempotency: a re-lower must reproduce the input's
             * prototypes exactly once, in the forward-decl pass). */
            uint32_t q;
            int fn_has_body = 0;
            for (q = 1; q < d->n_kids; q++) {
                if (d->kids[q]->kind == PM_JIT_CPP_AST_COMPOUND) fn_has_body = 1;
            }
            rc = pm_jit_cpp_low_function(&l, d, errbuf, errbuf_len);
            if (rc != 0) return -1;
            if (fn_has_body) pm_jit_cpp_low_puts(&l, "\n");
            break;
        }
        case PM_JIT_CPP_AST_VAR: {
            /* top-level variable with constant initializer lowers fine */
            const pm_jit_cpp_ast_t *init =
                d->n_kids > 1 && d->kids[1] != NULL ? d->kids[1] : NULL;
            char tb[PM_CPPX_NAME_MAX];
            const char *mapped;
            if (d->n_kids < 1 || d->kids[0]->kind != PM_JIT_CPP_AST_TYPE) {
                return pm_jit_cpp_lerr(arena, errbuf, errbuf_len,
                    "unsupported: variable shape", d->line);
            }
            if (d->kids[0]->text_len >= sizeof(tb)) {
                return pm_jit_cpp_lerr(arena, errbuf, errbuf_len,
                    "unsupported: type name too long", d->line);
            }
            memcpy(tb, d->kids[0]->text, d->kids[0]->text_len);
            tb[d->kids[0]->text_len] = '\0';
            mapped = pm_cppx_map_type(&tbl, tb);
            if (mapped == NULL) {
                return pm_jit_cpp_lerr(arena, errbuf, errbuf_len,
                    "unsupported: unknown template instantiation", d->line);
            }
            pm_jit_cpp_low_decl_type_name(&l, mapped, strlen(mapped),
                d->text, d->text_len);
            if (init != NULL) {
                pm_jit_cpp_low_puts(&l, " = ");
                if (pm_jit_cpp_low_expr(&l, init, errbuf, errbuf_len) != 0) {
                    return -1;
                }
            }
            pm_jit_cpp_low_puts(&l, ";\n\n");
            break;
        }
        case PM_JIT_CPP_AST_CLASS:
            /* emitted via the class table above */
            break;
        case PM_JIT_CPP_AST_TEMPLATE_DECL:
            /* the template itself is not emitted; instantiations are */
            break;
        case PM_JIT_CPP_AST_PP:
            /* verbatim preprocessor line at top level — already emitted in
             * the includes-first pass above */
            break;
        case PM_JIT_CPP_AST_TYPEDEF:
            /* C typedef — passthrough (TCC understands it fully); already
             * emitted in the types-first pass above */
            break;
        case PM_JIT_CPP_AST_USING:
            /* using Box<int>::get; — inherited names resolve through the
             * struct embedding; nothing to emit */
            break;
        default:
            return pm_jit_cpp_lerr(arena, errbuf, errbuf_len,
                "unsupported: top-level declaration kind", d->line);
        }
    }
    /* instantiated template functions: their cloned FUNCTION nodes are
     * parked at templates[] tail (kind FUNCTION, not TEMPLATE_DECL) */
    for (i = 0; i < tbl.n_templates; i++) {
        const pm_jit_cpp_ast_t *td = tbl.templates[i];
        if (td->kind == PM_JIT_CPP_AST_FUNCTION) {
            rc = pm_jit_cpp_low_function(&l, td, errbuf, errbuf_len);
            if (rc != 0) return -1;
            pm_jit_cpp_low_puts(&l, "\n");
        }
    }
    if (l.dead) {
        return pm_jit_cpp_err(errbuf, errbuf_len,
            "arena exhausted while lowering", 0);
    }
    pm_jit_cpp_low_reserve(&l, 1);
    if (l.dead) {
        return pm_jit_cpp_err(errbuf, errbuf_len,
            "arena exhausted while lowering", 0);
    }
    l.p[l.len] = '\0';
    *c_out = l.p;
    *c_out_len = l.len;
    return 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.jit.cpp, pm_metal_jit_cpp_lex, pm_metal_jit_cpp_lex,
    int32_t(pm_util_mem_arena_t *, const char *, size_t,
        pm_jit_cpp_toklist_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.jit.cpp, pm_metal_jit_cpp_parse, pm_metal_jit_cpp_parse,
    int32_t(pm_util_mem_arena_t *, const pm_jit_cpp_toklist_t *,
        pm_jit_cpp_ast_t **, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.jit.cpp, pm_metal_jit_cpp_ast_dump, pm_metal_jit_cpp_ast_dump,
    int32_t(const pm_jit_cpp_ast_t *, char *, size_t, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.jit.cpp, pm_metal_jit_cpp_lower, pm_metal_jit_cpp_lower,
    int32_t(pm_util_mem_arena_t *, const pm_jit_cpp_ast_t *, char **, size_t *, char *, size_t));
