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
            if (c == '#' ) {
                return pm_jit_cpp_err(errbuf, errbuf_len,
                    "unsupported: preprocessor directive", lx.line);
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
        || pm_jit_cpp_is_kw(t, "long") || pm_jit_cpp_is_kw(t, "short")) {
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
        /* array suffix [N] */
        if (pm_jit_cpp_is_punct(t, '[')) {
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
            if (len + 4 >= sizeof(buf)) { pm_jit_cpp_perr(p, "type name too long"); return NULL; }
            buf[len++] = '['; buf[len++] = ']';
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
                /* optional initializer (parens or braces) consumed as kids */
                if (pm_jit_cpp_eat_punct(p, '(')) {
                    uint32_t depth = 1;
                    while (depth > 0) {
                        const pm_jit_cpp_token_t *u = pm_jit_cpp_cur(p);
                        if (u->kind == PM_JIT_CPP_TOK_END) {
                            return pm_jit_cpp_perr(p, "unterminated new initializer") < 0
                                ? NULL : NULL;
                        }
                        if (pm_jit_cpp_is_punct(u, '(')) depth++;
                        if (pm_jit_cpp_is_punct(u, ')')) depth--;
                        pm_jit_cpp_advance(p);
                    }
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
            /* sizeof(expr) / sizeof(type) — take the balanced parens as one
             * operand node */
            pm_jit_cpp_advance(p);
            if (!pm_jit_cpp_eat_punct(p, '(')) {
                pm_jit_cpp_perr(p, "expected '(' after sizeof/alignof/decltype");
                return NULL;
            }
            {
                pm_jit_cpp_ast_t *operand = pm_jit_cpp_parse_expr(p);
                pm_jit_cpp_ast_t *un;
                if (operand == NULL) return NULL;
                if (pm_jit_cpp_expect_punct(p, ')') != 0) return NULL;
                un = pm_jit_cpp_node(p, PM_JIT_CPP_AST_UNARY, t->line);
                if (un == NULL) return NULL;
                un->text = pm_jit_cpp_intern(p, t->text, t->text_len);
                if (un->text == NULL) return NULL;
                un->text_len = t->text_len;
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
    /* C-style cast: (type)expr — the '(' is already consumed by primary, so
     * casts are handled here when a type-looking token follows '(' … we keep
     * it simple: casts are unsupported (they need type disambiguation). */
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
        pm_jit_cpp_advance(p);
        if (!pm_jit_cpp_eat_punct(p, '(')) {
            pm_jit_cpp_perr(p, "expected '(' after for");
            return NULL;
        }
        /* init: declaration (consumes its own ';') or expression + ';' */
        if (!pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ';')) {
            const pm_jit_cpp_token_t *d0 = pm_jit_cpp_cur(p);
            if (d0->kind == PM_JIT_CPP_TOK_KEYWORD && is_decl_keyword(d0->text, d0->text_len)) {
                pm_jit_cpp_ast_t *init = pm_jit_cpp_parse_declaration_or_error(p);
                if (init == NULL) return NULL;
                if (pm_jit_cpp_add_kid(p, n, init) != 0) return NULL;
            } else {
                pm_jit_cpp_ast_t *init = pm_jit_cpp_parse_expr(p);
                if (init == NULL) return NULL;
                if (pm_jit_cpp_add_kid(p, n, init) != 0) return NULL;
                if (pm_jit_cpp_expect_punct(p, ';') != 0) return NULL;
            }
        } else {
            pm_jit_cpp_advance(p);
        }
        if (!pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ';')) {
            pm_jit_cpp_ast_t *cond = pm_jit_cpp_parse_expr(p);
            if (cond == NULL) return NULL;
            if (pm_jit_cpp_add_kid(p, n, cond) != 0) return NULL;
        }
        if (pm_jit_cpp_expect_punct(p, ';') != 0) return NULL;
        if (!pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ')')) {
            pm_jit_cpp_ast_t *step = pm_jit_cpp_parse_expr(p);
            if (step == NULL) return NULL;
            if (pm_jit_cpp_add_kid(p, n, step) != 0) return NULL;
        }
        if (pm_jit_cpp_expect_punct(p, ')') != 0) return NULL;
        {
            pm_jit_cpp_ast_t *body = pm_jit_cpp_parse_stmt(p);
            if (body == NULL) return NULL;
            if (pm_jit_cpp_add_kid(p, n, body) != 0) return NULL;
        }
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
    if (pm_jit_cpp_is_kw(t, "switch")) {
        pm_jit_cpp_perr(p, "unsupported: switch statement");
        return NULL;
    }
    if (pm_jit_cpp_is_kw(t, "try") || pm_jit_cpp_is_kw(t, "throw")
        || pm_jit_cpp_is_kw(t, "goto")) {
        pm_jit_cpp_perr(p, "unsupported: exception/goto statement");
        return NULL;
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
            if (pm_jit_cpp_expect_punct(p, ';') != 0) return NULL;
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
    /* must be followed by a name (possibly behind * / & declarators:
     * int *p, T&& r, std::vector<int> v) to be a declaration */
    {
        uint32_t k = 1;
        const pm_jit_cpp_token_t *nxt = pm_jit_cpp_peek(p, k);
        while (pm_jit_cpp_is_punct(nxt, '*')
            || pm_jit_cpp_is_punct(nxt, '&')) {
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
    /* initializer */
    if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '=')) {
        pm_jit_cpp_advance(p);
        {
            pm_jit_cpp_ast_t *init = pm_jit_cpp_parse_assign(p);
            if (init == NULL) return NULL;
            if (pm_jit_cpp_add_kid(p, decl, init) != 0) return NULL;
        }
    } else if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '(')) {
        /* constructor-style init: balanced parens consumed as expression(s);
         * the initializer may itself contain parens (p(new LoudBox(8))) */
        uint32_t depth = 1;
        pm_jit_cpp_advance(p);
        while (depth > 0) {
            const pm_jit_cpp_token_t *u = pm_jit_cpp_cur(p);
            if (u->kind == PM_JIT_CPP_TOK_END) {
                pm_jit_cpp_perr(p, "unterminated initializer");
                return NULL;
            }
            if (pm_jit_cpp_is_punct(u, '(')) depth++;
            if (pm_jit_cpp_is_punct(u, ')')) depth--;
            pm_jit_cpp_advance(p);
        }
    } else if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '{')) {
        /* brace init: balanced braces consumed raw (aggregate init) */
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
    }
    /* multiple declarators: int a, b; — consume the rest */
    while (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ',')) {
        pm_jit_cpp_advance(p);
        if (pm_jit_cpp_cur(p)->kind != PM_JIT_CPP_TOK_IDENT) {
            pm_jit_cpp_perr(p, "expected declarator name");
            return NULL;
        }
        pm_jit_cpp_advance(p);
        if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '=')) {
            pm_jit_cpp_advance(p);
            {
                pm_jit_cpp_ast_t *init = pm_jit_cpp_parse_assign(p);
                if (init == NULL) return NULL;
            }
        }
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

        if (pm_jit_cpp_is_kw(t, "template")) {
            decl = pm_jit_cpp_parse_template_decl(&p);
        } else if (pm_jit_cpp_is_kw(t, "class") || pm_jit_cpp_is_kw(t, "struct")) {
            decl = pm_jit_cpp_parse_class_decl(&p);
        } else if (pm_jit_cpp_is_kw(t, "using")) {
            decl = pm_jit_cpp_parse_using_decl(&p);
        } else if (pm_jit_cpp_is_kw(t, "namespace")) {
            pm_jit_cpp_perr(&p, "unsupported: namespace definition");
            return -1;
        } else {
            /* function or variable declaration */
            decl = pm_jit_cpp_parse_function_or_var(&p);
        }
        if (decl == NULL) return -1;
        if (pm_jit_cpp_add_kid(&p, unit, decl) != 0) return -1;
    }    *unit_out = unit;
    return 0;
}

/* top-level: template<params> decl */
static pm_jit_cpp_ast_t *pm_jit_cpp_parse_template_decl(pm_jit_cpp_parser_t *p) {
    const pm_jit_cpp_token_t *t = pm_jit_cpp_cur(p); /* template */
    pm_jit_cpp_ast_t *decl;
    pm_jit_cpp_advance(p);
    if (!pm_jit_cpp_eat_punct(p, '<')) {
        pm_jit_cpp_perr(p, "expected '<' after template");
        return NULL;
    }
    {
        uint32_t depth = 1;
        while (depth > 0) {
            const pm_jit_cpp_token_t *u = pm_jit_cpp_cur(p);
            if (u->kind == PM_JIT_CPP_TOK_END) {
                pm_jit_cpp_perr(p, "unterminated template parameter list");
                return NULL;
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
                if (base->kind != PM_JIT_CPP_TOK_IDENT
                    && base->kind != PM_JIT_CPP_TOK_KEYWORD) {
                    pm_jit_cpp_perr(p, "expected base class name");
                    return NULL;
                }
                bt = pm_jit_cpp_node(p, PM_JIT_CPP_AST_TYPE, base->line);
                if (bt == NULL) return NULL;
                bt->text = pm_jit_cpp_intern(p, base->text, base->text_len);
                if (bt->text == NULL) return NULL;
                bt->text_len = base->text_len;
                if (pm_jit_cpp_add_kid(p, cls, bt) != 0) return NULL;
                pm_jit_cpp_advance(p);
                /* template base Foo<int> */
                if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '<')) {
                    uint32_t depth = 0;
                    do {
                        const pm_jit_cpp_token_t *u = pm_jit_cpp_cur(p);
                        if (u->kind == PM_JIT_CPP_TOK_END) {
                            pm_jit_cpp_perr(p, "unterminated base template args");
                            return NULL;
                        }
                        if (pm_jit_cpp_is_punct(u, '<')) depth++;
                        if (pm_jit_cpp_is_punct(u, '>')) depth--;
                        pm_jit_cpp_advance(p);
                    } while (depth > 0);
                }
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
    while (pm_jit_cpp_is_kw(t, "static") || pm_jit_cpp_is_kw(t, "inline")
        || pm_jit_cpp_is_kw(t, "extern") || pm_jit_cpp_is_kw(t, "constexpr")
        || pm_jit_cpp_is_kw(t, "virtual") || pm_jit_cpp_is_kw(t, "explicit")) {
        pm_jit_cpp_advance(p);
        t = pm_jit_cpp_cur(p);
    }
    ty = pm_jit_cpp_parse_type(p);
    if (ty == NULL) return NULL;
    name = pm_jit_cpp_cur(p);
    if (name->kind != PM_JIT_CPP_TOK_IDENT) {
        pm_jit_cpp_perr(p, "expected declaration name");
        return NULL;
    }
    pm_jit_cpp_advance(p);
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
            {
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
        pm_jit_cpp_advance(p); /* past name → cur is the opening '(' */
        pm_jit_cpp_advance(p); /* past '(' → depth counts only inner parens */
        if (!pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ')')) {
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
        } else {
            pm_jit_cpp_advance(p);
        }
        /* ctor init list : name(args), ... */
        if (pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), ':')) {
            while (!pm_jit_cpp_is_punct(pm_jit_cpp_cur(p), '{')
                && pm_jit_cpp_cur(p)->kind != PM_JIT_CPP_TOK_END) {
                pm_jit_cpp_advance(p);
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

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.jit.cpp, pm_metal_jit_cpp_lex, pm_metal_jit_cpp_lex,
    int32_t(pm_util_mem_arena_t *, const char *, size_t,
        pm_jit_cpp_toklist_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.jit.cpp, pm_metal_jit_cpp_parse, pm_metal_jit_cpp_parse,
    int32_t(pm_util_mem_arena_t *, const pm_jit_cpp_toklist_t *,
        pm_jit_cpp_ast_t **, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.jit.cpp, pm_metal_jit_cpp_ast_dump, pm_metal_jit_cpp_ast_dump,
    int32_t(const pm_jit_cpp_ast_t *, char *, size_t, char *, size_t));
