/* WASM32 backend prove: the emitter's own gate.
 *
 * History, because it is the reason this file is shaped the way it is. The
 * first version of this prove compiled three straight-line int functions
 * and read their export names — which is exactly the subset the backend
 * implemented, so everything past it emitted plausible bytes with the wrong
 * meaning. A `while` loop emitted a `br` whose label depth was a byte
 * offset (invalid, no engine loads it), and `if`/`&&`/`switch`/static data
 * emitted modules that DID load and answered wrong. The prove then grew a
 * refusal table: each construct the backend could not lower had to come
 * back as a compile error naming itself, because a refusal is a fact the
 * caller can act on and bytes that lie are not.
 *
 * The backend now lowers the language (control flow through a dispatch
 * loop, calls, symbol addresses, floats, long long, structs, varargs — see
 * the header of wasm32-gen.c), so the refusal table is gone and the cases
 * in it must compile. What this binary can check is the shape of what comes
 * out: it holds libtcc and no engine, so it validates the module the way a
 * loader's first pass does — the section sizes must tile the file exactly,
 * the indices must be in range, every body must end where it says. That is
 * the class of bug this backend actually produced (a section size computed
 * from the wrong buffer, a label that was a byte offset), and it is cheap.
 *
 * That the emitted code computes the RIGHT ANSWERS is proved where an
 * engine exists: jit/c's own tests compile for the wasm32 lane, load the
 * module through the wasmmod loader and call it (test_wasm32_runs_real_c),
 * and the µPy seat harness runs the same cases through WAMR. */
#include "tcc.h"
#include "libtcc.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "wasm32_cases.inc.h"
extern int wasm_build_module(uint8_t **out_buf, int *out_len);

/* ---- a reader over the module bytes; every step is bounds-checked ---- */
typedef struct {
    const uint8_t *p;
    int len;
    int at;
    const char *err;
} rd_t;

static int rd_byte(rd_t *r) {
    if (r->err) return 0;
    if (r->at >= r->len) { r->err = "ran off the end"; return 0; }
    return r->p[r->at++];
}

static unsigned rd_u32(rd_t *r) {
    unsigned v = 0;
    int shift = 0;
    for (;;) {
        int b = rd_byte(r);
        if (r->err) return 0;
        v |= (unsigned)(b & 0x7f) << shift;
        if (!(b & 0x80)) break;
        shift += 7;
        if (shift > 28) { r->err = "LEB128 longer than 5 bytes"; return 0; }
    }
    return v;
}

static void rd_skip(rd_t *r, int n) {
    if (r->err) return;
    if (n < 0 || r->at + n > r->len) { r->err = "skip ran off the end"; return; }
    r->at += n;
}

/* a name: length-prefixed bytes. Copies at most cap-1 of them out. */
static void rd_name(rd_t *r, char *out, int cap) {
    unsigned n = rd_u32(r);
    unsigned i;
    if (r->err) return;
    if (r->at + (int)n > r->len) { r->err = "name ran off the end"; return; }
    for (i = 0; i < n; i++) {
        if ((int)i < cap - 1) out[i] = (char)r->p[r->at + i];
    }
    if (cap > 0) out[(int)n < cap - 1 ? (int)n : cap - 1] = '\0';
    r->at += (int)n;
}

/* skip a result-type vector (type section) */
static void rd_types_vec(rd_t *r) {
    unsigned n = rd_u32(r);
    rd_skip(r, (int)n);
}

typedef struct {
    int n_types;
    int n_imported_funcs;
    int n_funcs;            /* defined, i.e. the function section's count */
    int n_code;
    int has_memory;
    int has_table;
    int table_min;
    int found_export;       /* the name the caller asked about */
} shape_t;

/* Validate the module the way a loader's first pass does, and report the
 * shape. Returns NULL on success or the first thing that is wrong. */
static const char *module_shape(const uint8_t *w, int len,
    const char *want_export, shape_t *sh)
{
    rd_t r;
    int last_id = 0;
    memset(sh, 0, sizeof(*sh));
    if (len < 8) return "shorter than a header";
    if (!(w[0] == 0x00 && w[1] == 0x61 && w[2] == 0x73 && w[3] == 0x6d))
        return "not a wasm module (magic)";
    if (!(w[4] == 0x01 && w[5] == 0x00 && w[6] == 0x00 && w[7] == 0x00))
        return "not wasm version 1";

    r.p = w; r.len = len; r.at = 8; r.err = NULL;
    while (r.at < len && !r.err) {
        int id = rd_byte(&r);
        unsigned size = rd_u32(&r);
        int body, end;
        if (r.err) break;
        body = r.at;
        if (body + (int)size > len) return "section size runs past the end";
        end = body + (int)size;
        if (id != 0) {
            if (id <= last_id) return "sections out of order";
            last_id = id;
        }
        switch (id) {
        case 1: {                       /* type */
            unsigned n = rd_u32(&r), i;
            sh->n_types = (int)n;
            for (i = 0; i < n && !r.err; i++) {
                if (rd_byte(&r) != 0x60) { r.err = "type is not a functype"; break; }
                rd_types_vec(&r);       /* params */
                rd_types_vec(&r);       /* results */
            }
            break;
        }
        case 2: {                       /* import */
            unsigned n = rd_u32(&r), i;
            for (i = 0; i < n && !r.err; i++) {
                char nm[8];
                rd_name(&r, nm, sizeof(nm));    /* module */
                rd_name(&r, nm, sizeof(nm));    /* field */
                switch (rd_byte(&r)) {
                case 0x00: {                    /* func: typeidx */
                    unsigned t = rd_u32(&r);
                    if (!r.err && (int)t >= sh->n_types)
                        r.err = "imported function's type index is out of range";
                    sh->n_imported_funcs++;
                    break;
                }
                case 0x01: rd_byte(&r); rd_u32(&r); break;   /* table */
                case 0x02: {                                 /* memory */
                    int fl = rd_byte(&r);
                    rd_u32(&r);
                    if (fl & 1) rd_u32(&r);
                    break;
                }
                case 0x03: rd_byte(&r); rd_byte(&r); break;  /* global */
                default: r.err = "unknown import kind"; break;
                }
            }
            break;
        }
        case 3: {                       /* function */
            unsigned n = rd_u32(&r), i;
            sh->n_funcs = (int)n;
            for (i = 0; i < n && !r.err; i++) {
                unsigned t = rd_u32(&r);
                if (!r.err && (int)t >= sh->n_types)
                    r.err = "function's type index is out of range";
            }
            break;
        }
        case 4: {                       /* table */
            unsigned n = rd_u32(&r), i;
            for (i = 0; i < n && !r.err; i++) {
                int fl;
                rd_byte(&r);            /* reftype */
                fl = rd_byte(&r);
                sh->table_min = (int)rd_u32(&r);
                if (fl & 1) rd_u32(&r);
                sh->has_table = 1;
            }
            break;
        }
        case 5: {                       /* memory */
            unsigned n = rd_u32(&r), i;
            for (i = 0; i < n && !r.err; i++) {
                int fl = rd_byte(&r);
                rd_u32(&r);
                if (fl & 1) rd_u32(&r);
                sh->has_memory = 1;
            }
            break;
        }
        case 6: {                       /* global */
            unsigned n = rd_u32(&r), i;
            for (i = 0; i < n && !r.err; i++) {
                rd_byte(&r);            /* valtype */
                rd_byte(&r);            /* mut */
                /* one const expr, then 0x0B */
                rd_byte(&r);
                rd_u32(&r);
                if (rd_byte(&r) != 0x0B) r.err = "global init does not end";
            }
            break;
        }
        case 7: {                       /* export */
            unsigned n = rd_u32(&r), i;
            for (i = 0; i < n && !r.err; i++) {
                char nm[128];
                unsigned idx;
                int kind;
                rd_name(&r, nm, sizeof(nm));
                kind = rd_byte(&r);
                idx = rd_u32(&r);
                if (r.err) break;
                if (kind == 0x00
                    && (int)idx >= sh->n_imported_funcs + sh->n_funcs) {
                    r.err = "exported function index is out of range";
                    break;
                }
                if (want_export != NULL && strcmp(nm, want_export) == 0)
                    sh->found_export = 1;
            }
            break;
        }
        case 9: {                       /* element */
            unsigned n = rd_u32(&r), i;
            for (i = 0; i < n && !r.err; i++) {
                unsigned cnt, k;
                rd_u32(&r);             /* table index / flags */
                rd_byte(&r);            /* i32.const */
                rd_u32(&r);
                if (rd_byte(&r) != 0x0B) { r.err = "element offset does not end"; break; }
                cnt = rd_u32(&r);
                for (k = 0; k < cnt && !r.err; k++) {
                    unsigned f = rd_u32(&r);
                    if ((int)f >= sh->n_imported_funcs + sh->n_funcs)
                        r.err = "element's function index is out of range";
                }
            }
            break;
        }
        case 10: {                      /* code */
            unsigned n = rd_u32(&r), i;
            sh->n_code = (int)n;
            for (i = 0; i < n && !r.err; i++) {
                unsigned bsz = rd_u32(&r);
                int bend;
                if (r.err) break;
                bend = r.at + (int)bsz;
                if (bend > end) { r.err = "body runs past the code section"; break; }
                if (bsz == 0) { r.err = "empty function body"; break; }
                if (w[bend - 1] != 0x0B) { r.err = "body does not end with `end`"; break; }
                r.at = bend;
            }
            break;
        }
        case 11: {                      /* data */
            unsigned n = rd_u32(&r), i;
            for (i = 0; i < n && !r.err; i++) {
                unsigned dsz;
                rd_u32(&r);             /* memory index / flags */
                rd_byte(&r);            /* i32.const */
                rd_u32(&r);
                if (rd_byte(&r) != 0x0B) { r.err = "data offset does not end"; break; }
                dsz = rd_u32(&r);
                rd_skip(&r, (int)dsz);
            }
            break;
        }
        default:
            r.at = end;                 /* custom or anything we do not read */
            break;
        }
        if (r.err) break;
        if (r.at != end) return "section body does not fill its declared size";
    }
    if (r.err) return r.err;
    if (r.at != len) return "sections do not tile the file";
    if (sh->n_code != sh->n_funcs) return "code bodies do not match declared functions";
    if (want_export != NULL && !sh->found_export) return "the entry is not exported";
    return NULL;
}

/* compile one source for wasm32 and validate what comes out */
static int emit_and_check(const char *what, const char *src,
    const char *want_export, shape_t *sh_out)
{
    TCCState *s = tcc_new();
    uint8_t *w = NULL;
    int len = 0;
    shape_t sh;
    const char *bad;

    if (s == NULL) { fprintf(stderr, "FAIL: %s: tcc_new\n", what); return 1; }
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
    tcc_set_lib_path(s, PM_METAL_TCC_LIB_DIR);
    tcc_add_sysinclude_path(s, PM_METAL_TCC_LIB_DIR "/include");
    if (tcc_compile_string(s, src) != 0) {
        fprintf(stderr, "FAIL: %s: refused a construct it must lower\n", what);
        tcc_delete(s);
        return 1;
    }
    if (wasm_build_module(&w, &len) != 0 || w == NULL || len <= 0) {
        fprintf(stderr, "FAIL: %s: serializer produced nothing\n", what);
        tcc_delete(s);
        return 1;
    }
    bad = module_shape(w, len, want_export, &sh);
    if (bad != NULL) {
        fprintf(stderr, "FAIL: %s: %s (%d bytes)\n", what, bad, len);
        tcc_free(w);
        tcc_delete(s);
        return 1;
    }
    if (sh_out != NULL) *sh_out = sh;
    tcc_free(w);
    tcc_delete(s);
    return 0;
}

int main(void) {
    int bad = 0;

    /* ---- named exports: the registry addresses faces by name ---- */
    {
        shape_t sh;
        const char *src =
            "int main(void) { return 42; }\n"
            "int add_one(int x) { return x + 1; }\n"
            "int scale3(int x) { return x * 3; }\n";
        if (emit_and_check("named exports", src, "main", &sh)) return 1;
        if (sh.n_funcs != 3) {
            fprintf(stderr, "FAIL: named exports: %d functions, wanted 3\n",
                sh.n_funcs);
            return 1;
        }
        printf("PASS: WASM32 exports every defined function by name\n");
    }

    /* ---- the serializer resets: no stale exports from the last module ---- */
    {
        shape_t sh;
        if (emit_and_check("reset", "int main(void) { return 7; }", "main", &sh))
            return 1;
        if (sh.n_funcs != 1) {
            fprintf(stderr, "FAIL: reset: %d functions carried over\n",
                sh.n_funcs);
            return 1;
        }
        printf("PASS: WASM32 serializer resets between modules\n");
    }

    /* ---- the language: every case in wasm32_cases.inc.h must compile
     * into a module that tiles and whose indices are in range. What each
     * one must ANSWER is checked where an engine exists — jit/c's
     * test_wasm32_runs_real_c runs this same list. ---- */
    {
        int k;
        for (k = 0; k < WASM32_CASE_COUNT; k++) {
            if (emit_and_check(wasm32_cases[k].name, wasm32_cases[k].src,
                    "f", NULL)) {
                bad = 1;
            }
        }
        if (bad) return 1;
        printf("PASS: WASM32 lowers all %d cases into modules that tile"
            " and index in range\n", WASM32_CASE_COUNT);
    }

    /* ---- what it still refuses, and must refuse loudly ---- */
    {
        static const struct { const char *what; const char *src; } refuse[] = {
            /* a label's address is a host pointer; wasm branches take a
             * static label depth, so there is nothing to take the address of */
            { "computed goto", "int f(int n){void*p=&&a;goto *p;a:return n;}" },
        };
        size_t n = sizeof(refuse) / sizeof(refuse[0]);
        size_t k;
        for (k = 0; k < n; k++) {
            TCCState *sr = tcc_new();
            assert(sr);
            tcc_set_output_type(sr, TCC_OUTPUT_MEMORY);
            if (tcc_compile_string(sr, refuse[k].src) == 0) {
                fprintf(stderr, "FAIL: %s compiled instead of being refused"
                    " — the object would be wrong\n", refuse[k].what);
                bad = 1;
            }
            tcc_delete(sr);
        }
        if (bad) return 1;
        printf("PASS: WASM32 refuses the %d construct%s it cannot mean\n",
            (int)n, n == 1 ? "" : "s");
    }

    /* ---- a refusal during serialization must come back as a refusal,
     * not as exit(1): the seat compiles in-process ---- */
    {
        TCCState *s = tcc_new();
        uint8_t *w = NULL;
        int len = 0;
        assert(s);
        tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
        /* one declared-never-defined datum: the serializer gives it a
         * zeroed cell and warns, and the module must still be whole */
        if (tcc_compile_string(s, "extern int nowhere;"
                "int f(void){return nowhere;}") != 0) {
            fprintf(stderr, "FAIL: extern datum: compile\n");
            return 1;
        }
        if (wasm_build_module(&w, &len) != 0 || w == NULL) {
            fprintf(stderr, "FAIL: extern datum: no module\n");
            return 1;
        }
        {
            shape_t sh;
            const char *msg = module_shape(w, len, "f", &sh);
            if (msg != NULL) {
                fprintf(stderr, "FAIL: extern datum: %s\n", msg);
                return 1;
            }
            if (!sh.has_memory) {
                fprintf(stderr, "FAIL: extern datum: no memory for the cell\n");
                return 1;
            }
        }
        tcc_free(w);
        tcc_delete(s);
        printf("PASS: WASM32 completes a module whose externals are"
            " only declared\n");
    }
    return 0;
}
