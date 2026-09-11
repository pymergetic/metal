/*
 *  WASM32 code generator for TCC
 *
 *  Emits a complete WebAssembly module (MVP + bulk memory) for one
 *  translation unit.
 *
 *  This file is included twice by tcc.h:
 *    - First with TARGET_DEFS_ONLY for arch parameters
 *    - Second without it for the code generator implementation
 *
 *  THE TWO THINGS THAT MAKE WASM DIFFERENT FROM EVERY OTHER TCC BACKEND
 *
 *  1. There is no jump. A wasm branch names an enclosing block by nesting
 *     depth, and TCC hands the backend labels as code offsets it patches
 *     later (gjmp/gsym_addr). So the function is emitted straight-line into
 *     a scratch buffer, every jump site is recorded instead of encoded, and
 *     at gfunc_epilog the scratch is cut into basic blocks at the jump and
 *     label offsets and re-emitted as a `br_table` dispatch loop over a $pc
 *     local. Falling out of block i lands on block i+1 for free, so straight
 *     runs stay straight and only real jumps pay for the dispatch. A
 *     function whose only jumps leave the function skips the loop entirely
 *     and uses a single wrapping block.
 *
 *     This works because values live in locals, never on the operand stack
 *     across a statement, so the stack is empty at every block boundary —
 *     which is exactly what wasm validation requires.
 *
 *  2. There is no flags register. Comparisons therefore do not emit
 *     anything: gen_opi/gen_opf record the two operand registers in
 *     vtop->cmp_r and set VT_CMP, and whoever consumes it (gjmp_cond for a
 *     branch, load() for a 0/1 value) emits the compare there. This is the
 *     riscv64/arm64 pattern.
 *
 *  ABI (this backend defines it; the loader calls exports through WAMR)
 *    - Each C function is one wasm function whose params and result mirror
 *      the C signature: i32 for int/pointer/char/short, i64 for long long,
 *      f32/f64 for float/double. long double is double (LDOUBLE_SIZE 8).
 *    - A struct parameter is passed as a pointer; the callee copies it into
 *      its own frame, so the callee owns its copy as C requires.
 *    - A struct return is a hidden leading i32 pointer parameter and a void
 *      result (gfunc_sret returns 0, func_vc holds the slot).
 *    - A variadic function takes one extra trailing i32: a pointer to the
 *      caller-built argument area that va_start/va_arg walk.
 *    - Locals and parameters live in linear memory, addressed from an $fp
 *      local, so &local, arrays, structs and long long halves all work. The
 *      frame is carved off a mutable $sp global, so recursion is fine.
 *    - Function pointers are table indices, offset by one so that a null
 *      pointer is not a valid function.
 *
 *  What is not here: computed goto (`goto *p`), and long double as anything
 *  wider than double. Both refuse by name rather than emit something wrong.
 */

#ifdef TARGET_DEFS_ONLY

/* --- Arch parameters (TARGET_DEFS_ONLY) --- */

/* 8 integer + 4 float "registers". They are wasm locals, so the count is a
 * comfort choice for tccgen's allocator, not a hardware limit. */
#define NB_REGS            12
#define NB_ASM_REGS        0

#define RC_INT     0x0001
#define RC_FLOAT   0x0002

#define RC_R0      0x0004
#define RC_R1      0x0008
#define RC_R2      0x0010
#define RC_R3      0x0020
#define RC_R4      0x0040
#define RC_R5      0x0080
#define RC_R6      0x0100
#define RC_R7      0x0200
#define RC_F0      0x0400
#define RC_F1      0x0800
#define RC_F2      0x1000
#define RC_F3      0x2000

#define RC_IRET    RC_R0
#define RC_IRE2    RC_R1
#define RC_FRET    RC_F0

enum {
    TREG_R0 = 0,
    TREG_R1,
    TREG_R2,
    TREG_R3,
    TREG_R4,
    TREG_R5,
    TREG_R6,
    TREG_R7,
    TREG_F0,
    TREG_F1,
    TREG_F2,
    TREG_F3,
};

#define REG_VALUE(reg) (reg)

#define REG_IRET TREG_R0
#define REG_IRE2 TREG_R1
#define REG_FRET TREG_F0

#define PTR_SIZE 4
#define LDOUBLE_SIZE 8
#define LDOUBLE_ALIGN 8
#define MAX_ALIGN 8

#else /* !TARGET_DEFS_ONLY */
/******************************************************/
#define USING_GLOBALS
#include "tcc.h"
#include <string.h>
#include <stdlib.h>

/* ----- WASM opcodes ----- */
enum {
    WOP_UNREACHABLE   = 0x00,
    WOP_NOP           = 0x01,
    WOP_BLOCK         = 0x02,
    WOP_LOOP          = 0x03,
    WOP_IF            = 0x04,
    WOP_ELSE          = 0x05,
    WOP_END           = 0x0b,
    WOP_BR            = 0x0c,
    WOP_BR_IF         = 0x0d,
    WOP_BR_TABLE      = 0x0e,
    WOP_RETURN        = 0x0f,
    WOP_CALL          = 0x10,
    WOP_CALL_INDIRECT = 0x11,
    WOP_DROP          = 0x1a,
    WOP_SELECT        = 0x1b,
    WOP_LOCAL_GET     = 0x20,
    WOP_LOCAL_SET     = 0x21,
    WOP_LOCAL_TEE     = 0x22,
    WOP_GLOBAL_GET    = 0x23,
    WOP_GLOBAL_SET    = 0x24,
    WOP_I32_LOAD      = 0x28,
    WOP_I64_LOAD      = 0x29,
    WOP_F32_LOAD      = 0x2a,
    WOP_F64_LOAD      = 0x2b,
    WOP_I32_LOAD8_S   = 0x2c,
    WOP_I32_LOAD8_U   = 0x2d,
    WOP_I32_LOAD16_S  = 0x2e,
    WOP_I32_LOAD16_U  = 0x2f,
    WOP_I32_STORE     = 0x36,
    WOP_I64_STORE     = 0x37,
    WOP_F32_STORE     = 0x38,
    WOP_F64_STORE     = 0x39,
    WOP_I32_STORE8    = 0x3a,
    WOP_I32_STORE16   = 0x3b,
    WOP_MEMORY_SIZE   = 0x3f,
    WOP_MEMORY_GROW   = 0x40,
    WOP_I32_CONST     = 0x41,
    WOP_I64_CONST     = 0x42,
    WOP_F32_CONST     = 0x43,
    WOP_F64_CONST     = 0x44,
    WOP_I32_EQZ       = 0x45,
    WOP_I32_EQ        = 0x46,
    WOP_I32_NE        = 0x47,
    WOP_I32_LT_S      = 0x48,
    WOP_I32_LT_U      = 0x49,
    WOP_I32_GT_S      = 0x4a,
    WOP_I32_GT_U      = 0x4b,
    WOP_I32_LE_S      = 0x4c,
    WOP_I32_LE_U      = 0x4d,
    WOP_I32_GE_S      = 0x4e,
    WOP_I32_GE_U      = 0x4f,
    WOP_F32_EQ        = 0x5b,
    WOP_F32_NE        = 0x5c,
    WOP_F32_LT        = 0x5d,
    WOP_F32_GT        = 0x5e,
    WOP_F32_LE        = 0x5f,
    WOP_F32_GE        = 0x60,
    WOP_F64_EQ        = 0x61,
    WOP_F64_NE        = 0x62,
    WOP_F64_LT        = 0x63,
    WOP_F64_GT        = 0x64,
    WOP_F64_LE        = 0x65,
    WOP_F64_GE        = 0x66,
    WOP_I32_ADD       = 0x6a,
    WOP_I32_SUB       = 0x6b,
    WOP_I32_MUL       = 0x6c,
    WOP_I32_DIV_S     = 0x6d,
    WOP_I32_DIV_U     = 0x6e,
    WOP_I32_REM_S     = 0x6f,
    WOP_I32_REM_U     = 0x70,
    WOP_I32_AND       = 0x71,
    WOP_I32_OR        = 0x72,
    WOP_I32_XOR       = 0x73,
    WOP_I32_SHL       = 0x74,
    WOP_I32_SHR_S     = 0x75,
    WOP_I32_SHR_U     = 0x76,
    WOP_I32_ROTL      = 0x77,
    WOP_I32_ROTR      = 0x78,
    WOP_I64_ADD       = 0x7c,
    WOP_I64_SUB       = 0x7d,
    WOP_I64_MUL       = 0x7e,
    WOP_I64_DIV_S     = 0x7f,
    WOP_I64_DIV_U     = 0x80,
    WOP_I64_REM_S     = 0x81,
    WOP_I64_REM_U     = 0x82,
    WOP_I64_AND       = 0x83,
    WOP_I64_OR        = 0x84,
    WOP_I64_XOR       = 0x85,
    WOP_I64_SHL       = 0x86,
    WOP_I64_SHR_S     = 0x87,
    WOP_I64_SHR_U     = 0x88,
    WOP_F32_NEG       = 0x8c,
    WOP_F32_ADD       = 0x92,
    WOP_F32_SUB       = 0x93,
    WOP_F32_MUL       = 0x94,
    WOP_F32_DIV       = 0x95,
    WOP_F64_NEG       = 0x9a,
    WOP_F64_ADD       = 0xa0,
    WOP_F64_SUB       = 0xa1,
    WOP_F64_MUL       = 0xa2,
    WOP_F64_DIV       = 0xa3,
    WOP_I32_WRAP_I64      = 0xa7,
    WOP_I32_TRUNC_F32_S   = 0xa8,
    WOP_I32_TRUNC_F32_U   = 0xa9,
    WOP_I32_TRUNC_F64_S   = 0xaa,
    WOP_I32_TRUNC_F64_U   = 0xab,
    WOP_I64_EXTEND_I32_S  = 0xac,
    WOP_I64_EXTEND_I32_U  = 0xad,
    WOP_I64_TRUNC_F32_S   = 0xae,
    WOP_I64_TRUNC_F32_U   = 0xaf,
    WOP_I64_TRUNC_F64_S   = 0xb0,
    WOP_I64_TRUNC_F64_U   = 0xb1,
    WOP_F32_CONVERT_I32_S = 0xb2,
    WOP_F32_CONVERT_I32_U = 0xb3,
    WOP_F32_CONVERT_I64_S = 0xb4,
    WOP_F32_CONVERT_I64_U = 0xb5,
    WOP_F32_DEMOTE_F64    = 0xb6,
    WOP_F64_CONVERT_I32_S = 0xb7,
    WOP_F64_CONVERT_I32_U = 0xb8,
    WOP_F64_CONVERT_I64_S = 0xb9,
    WOP_F64_CONVERT_I64_U = 0xba,
    WOP_F64_PROMOTE_F32   = 0xbb,
    WOP_PREFIX_FC         = 0xfc,   /* memory.copy / memory.fill, trunc_sat */
    WOP_FC_I32_TRUNC_SAT_F32_S = 0x00,
    WOP_FC_I32_TRUNC_SAT_F32_U = 0x01,
    WOP_FC_I32_TRUNC_SAT_F64_S = 0x02,
    WOP_FC_I32_TRUNC_SAT_F64_U = 0x03,
    WOP_FC_I64_TRUNC_SAT_F32_S = 0x04,
    WOP_FC_I64_TRUNC_SAT_F32_U = 0x05,
    WOP_FC_I64_TRUNC_SAT_F64_S = 0x06,
    WOP_FC_I64_TRUNC_SAT_F64_U = 0x07,
    WOP_FC_MEMORY_COPY    = 0x0a,
    WOP_FC_MEMORY_FILL    = 0x0b,
};

enum {
    VALTYPE_I32  = 0x7f,
    VALTYPE_I64  = 0x7e,
    VALTYPE_F32  = 0x7d,
    VALTYPE_F64  = 0x7c,
    VALTYPE_NONE = 0x40,
};

/* ----- Target machine defines ----- */

ST_DATA const char * const target_machine_defs =
    "__wasm__\0"
    "__wasm32__\0"
    ;

ST_DATA const int reg_classes[NB_REGS] = {
    RC_INT | RC_R0, RC_INT | RC_R1,
    RC_INT | RC_R2, RC_INT | RC_R3,
    RC_INT | RC_R4, RC_INT | RC_R5,
    RC_INT | RC_R6, RC_INT | RC_R7,
    RC_FLOAT | RC_F0, RC_FLOAT | RC_F1,
    RC_FLOAT | RC_F2, RC_FLOAT | RC_F3,
};

/* ----- linear memory layout -----
 * 0                  : one guard word, so a null dereference hits nothing
 *                      anyone owns (wasm itself does not trap on address 0)
 * WASM_DATA_BASE ..  : the TU's data/rodata/bss sections, placed here
 * .. stack_top       : the call stack, growing down from stack_top ($sp) */
#define WASM_DATA_BASE   1024
#define WASM_STACK_SIZE  (256 * 1024)
#define WASM_PAGE        65536

/* ----- LEB128 helpers ----- */

static void leb_u32(uint8_t **pp, uint32_t v) {
    do {
        uint8_t b = (uint8_t)(v & 0x7fu);
        v >>= 7;
        if (v) b |= 0x80u;
        *(*pp)++ = b;
    } while (v);
}

static void leb_i32(uint8_t **pp, int32_t v) {
    int more = 1;
    while (more) {
        uint8_t b = (uint8_t)(v & 0x7f);
        v >>= 7;
        if ((v == 0 && !(b & 0x40)) || (v == -1 && (b & 0x40)))
            more = 0;
        else
            b |= 0x80;
        *(*pp)++ = b;
    }
}

/* A slot that gets patched after the fact (a symbol's address, a function
 * index, a type index) has to keep its width, so it is written as a
 * five-byte LEB with the continuation bits set. Still a legal encoding, and
 * five bytes covers the whole 32-bit range. */
static void leb_pad5(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)((v & 0x7f) | 0x80);
    p[1] = (uint8_t)(((v >> 7) & 0x7f) | 0x80);
    p[2] = (uint8_t)(((v >> 14) & 0x7f) | 0x80);
    p[3] = (uint8_t)(((v >> 21) & 0x7f) | 0x80);
    p[4] = (uint8_t)((v >> 28) & 0x7f);
}

/* ----- growable buffers -----
 * Everything grows through tcc_realloc so the backend follows whatever
 * allocator the embedder installed (jit.c routes TCC allocations through the
 * caller's arena), and no compile is capped by a fixed array. */

#define BUF_FIRST (64 * 1024)

typedef struct WBuf {
    uint8_t *p;
    size_t len;
    size_t cap;
} WBuf;

static int wbuf_reserve(WBuf *b, size_t n) {
    size_t want;
    uint8_t *nb;
    if (b->len + n <= b->cap)
        return 0;
    want = b->cap ? b->cap : BUF_FIRST;
    while (b->len + n > want)
        want *= 2;
    nb = (uint8_t *)tcc_realloc(b->p, want);
    if (nb == NULL)
        return -1;
    b->p = nb;
    b->cap = want;
    return 0;
}

static void wbuf_free(WBuf *b) {
    if (b->p != NULL)
        tcc_free(b->p);
    b->p = NULL;
    b->len = 0;
    b->cap = 0;
}

/* scratch: the current function's straight-line code, cut into basic blocks
 * at gfunc_epilog. `ind` is always its length. */
static WBuf sbuf;
/* bodies: finished function bodies, ready to be copied into the code
 * section (locals declaration included, body size not). */
static WBuf bbuf;
/* get_tok_str hands back a buffer the next call overwrites, so a name that
 * has to outlive the call is copied here */
static WBuf namepool;

/* ----- signatures -----
 * Interned as byte strings: [result valtype][param count][param valtypes].
 * A call site and the callee's definition build the same bytes from the same
 * C type, so they agree without either having to see the other. */
static WBuf sigpool;
static int *sig_off;
static int n_sigs, sig_cap;

static int sig_intern(const uint8_t *bytes, int len) {
    int i;
    for (i = 0; i < n_sigs; i++) {
        const uint8_t *q = sigpool.p + sig_off[i];
        int qlen = 2 + q[1];
        if (qlen == len && memcmp(q, bytes, (size_t)len) == 0)
            return i;
    }
    if (n_sigs >= sig_cap) {
        int want = sig_cap ? sig_cap * 2 : 32;
        int *nb = (int *)tcc_realloc(sig_off, (size_t)want * sizeof(int));
        if (nb == NULL)
            tcc_error("wasm32: out of memory (signatures)");
        sig_off = nb;
        sig_cap = want;
    }
    if (wbuf_reserve(&sigpool, (size_t)len) != 0)
        tcc_error("wasm32: out of memory (signatures)");
    sig_off[n_sigs] = (int)sigpool.len;
    memcpy(sigpool.p + sigpool.len, bytes, (size_t)len);
    sigpool.len += (size_t)len;
    return n_sigs++;
}

static const uint8_t *sig_bytes(int s) { return sigpool.p + sig_off[s]; }

/* ----- relocations -----
 * The backend cannot know a symbol's linear-memory address or a function's
 * index while it emits: imports come first in the index space and the data
 * layout is decided once every section's size is known. So each of those
 * operands is a five-byte slot plus a record here, resolved in
 * wasm_build_module. */
enum {
    WR_DATA_ADDR,   /* i32.const <address of sym + addend> */
    WR_FUNC_IDX,    /* call <function index of sym> */
    WR_FUNC_ADDR,   /* i32.const <table index of sym> (a function pointer) */
    WR_TYPE_IDX,    /* call_indirect <type index of sig> */
};

typedef struct WReloc {
    int off;        /* offset of the five-byte slot (in sbuf, then bbuf) */
    int kind;
    int fn;         /* function row this reloc belongs to (bbuf phase) */
    int sig;        /* WR_FUNC_IDX: the call's signature; WR_TYPE_IDX: same */
    int addend;
    int esym;       /* index into the ELF symbol table, not a Sym* */
} WReloc;

static WReloc *srel;            /* current function, sbuf offsets */
static int n_srel, srel_cap;
static WReloc *mrel;            /* whole module, bbuf offsets */
static int n_mrel, mrel_cap;

static void reloc_add(WReloc **arr, int *n, int *cap, WReloc r) {
    if (*n >= *cap) {
        int want = *cap ? *cap * 2 : 64;
        WReloc *nb = (WReloc *)tcc_realloc(*arr, (size_t)want * sizeof(WReloc));
        if (nb == NULL)
            tcc_error("wasm32: out of memory (relocations)");
        *arr = nb;
        *cap = want;
    }
    (*arr)[(*n)++] = r;
}

/* ----- jump sites -----
 * Recorded, not encoded: a wasm branch needs a nesting depth, which only
 * exists once the function is cut into blocks. `chain` threads TCC's jump
 * chains (a chain handle is index+1, so 0 stays "empty" as TCC expects).
 *
 * Each one still takes a byte of scratch, even though nothing is encoded
 * into it. TCC hands out labels as code offsets, and a label placed before
 * a jump and one placed after it are different labels — with a zero-length
 * jump they would be the same offset and every loop nested in another loop
 * would exit to the wrong place. The byte keeps them apart; it is never
 * copied into the body. */
typedef struct WJump {
    int off;        /* sbuf offset of this jump's placeholder byte */
    int cond;       /* 1: the block's last bytes leave the condition on the stack */
    int target;     /* sbuf offset to land on, -1 while unresolved */
    int chain;      /* next jump in TCC's chain, 0 = end */
} WJump;

static WJump *jmps;
static int n_jmps, jmps_cap;

/* ----- function rows ----- */
typedef struct WParam {
    int wlocal;     /* which wasm param local carries it */
    int addr;       /* frame offset it is copied to */
    int valtype;
    int size;       /* struct params: bytes to copy, else 0 */
} WParam;

typedef struct WFunc {
    char name[160];
    int body_off, body_len;     /* into bbuf */
    int sig;
    int nwparams;
} WFunc;

static WFunc *funcs;
static int n_funcs, funcs_cap;

/* ----- current function state ----- */
static int cur_sig;
static int cur_nwparams;        /* wasm params: register locals sit above them */
static int cur_ret_valtype;
static int cur_param_bytes;     /* size of the incoming parameter area */
static int cur_variadic;
static WParam *cur_params;
static int cur_nparams, cur_params_cap;
static char cur_name[160];

/* locals, relative to the wasm params:
 *   $fp $ap $pc $t32 $carry, r0..r7 (i32), $t64 (i64), f0..f3 (f32), f0..f3 (f64)
 *
 * $fp is the frame: locals sit below it at the negative offsets TCC hands
 * out. $ap is the incoming argument area: parameters sit above it at
 * positive offsets. They are the same address for an ordinary function,
 * which copies its wasm parameters into its own frame — and they differ for
 * a variadic one, whose $ap points into the caller's frame so that
 * `&last_named_param + sizeof(it)` is the first variadic argument. That is
 * exactly what this target's stdarg.h computes, so va_start/va_arg need no
 * backend support at all. */
enum {
    L_FP = 0,
    L_AP,
    L_PC,
    L_T32,
    L_CARRY,
    L_INT0,                     /* 8 integer registers */
    L_T64 = L_INT0 + 8,
    L_F32_0,                    /* 4 float registers, f32 flavour */
    L_F64_0 = L_F32_0 + 4,      /* the same 4, f64 flavour */
    L_COUNT = L_F64_0 + 4,
};

static int lidx(int k) { return cur_nwparams + k; }
static int reg_i32(int r) { return lidx(L_INT0 + r); }
static int reg_flt(int r, int is32) {
    return lidx((is32 ? L_F32_0 : L_F64_0) + (r - TREG_F0));
}

/* ----- emission into the current function's scratch ----- */

static void we(uint8_t b) {
    if (wbuf_reserve(&sbuf, 1) != 0)
        tcc_error("wasm32: out of code memory");
    sbuf.p[sbuf.len++] = b;
    ind = (int)sbuf.len;
}

static void we_bytes(const uint8_t *p, int n) {
    if (wbuf_reserve(&sbuf, (size_t)n) != 0)
        tcc_error("wasm32: out of code memory");
    memcpy(sbuf.p + sbuf.len, p, (size_t)n);
    sbuf.len += (size_t)n;
    ind = (int)sbuf.len;
}

static void we_u32(uint32_t v) {
    uint8_t t[8], *p = t;
    leb_u32(&p, v);
    we_bytes(t, (int)(p - t));
}

static void we_i32(int32_t v) {
    uint8_t t[8], *p = t;
    leb_i32(&p, v);
    we_bytes(t, (int)(p - t));
}

/* The compiler frees its symbol table and its identifier strings the moment
 * the unit is done, and the module is serialized after that. So a
 * relocation cannot hold a Sym*: it holds the ELF symbol index, which lives
 * in a section that outlives the compile and carries both the name and the
 * placement. Registering the symbol here is what TCC's own greloc does. */
static int esym_index(Sym *sym)
{
    if (sym == NULL)
        return 0;
    if (!sym->c)
        put_extern_sym2(sym, SHN_UNDEF, 0, 0, 1);
    return sym->c;
}

/* a five-byte slot plus its relocation record */
static void we_slot(int kind, int esym, int addend, int sig, uint32_t guess) {
    WReloc r;
    uint8_t t[5];
    r.off = (int)sbuf.len;
    r.kind = kind;
    r.fn = -1;
    r.sig = sig;
    r.addend = addend;
    r.esym = esym;
    reloc_add(&srel, &n_srel, &srel_cap, r);
    leb_pad5(t, guess);
    we_bytes(t, 5);
}

static void we_op_u32(uint8_t op, uint32_t v) { we(op); we_u32(v); }
static void we_get(int local) { we_op_u32(WOP_LOCAL_GET, (uint32_t)local); }
static void we_set(int local) { we_op_u32(WOP_LOCAL_SET, (uint32_t)local); }
static void we_tee(int local) { we_op_u32(WOP_LOCAL_TEE, (uint32_t)local); }
static void push_i32(int32_t v) { we(WOP_I32_CONST); we_i32(v); }
static void push_i64(int64_t v) {
    uint8_t t[12], *p = t;
    int more = 1;
    we(WOP_I64_CONST);
    while (more) {
        uint8_t b = (uint8_t)(v & 0x7f);
        v >>= 7;
        if ((v == 0 && !(b & 0x40)) || (v == -1 && (b & 0x40)))
            more = 0;
        else
            b |= 0x80;
        *p++ = b;
    }
    we_bytes(t, (int)(p - t));
}

/* memory access immediates: alignment 0 (byte) is always legal and never
 * traps on a misaligned struct field; offset 0 because the address is
 * already on the stack. */
static void we_mem(uint8_t op) { we(op); we_u32(0); we_u32(0); }

static void we_memory_copy(void) {
    we(WOP_PREFIX_FC); we_u32(WOP_FC_MEMORY_COPY); we(0x00); we(0x00);
}

/* float -> int, saturating.
 *
 * The plain conversions (i32.trunc_f64_s and friends) TRAP when the value
 * does not fit, and C says the result of an out-of-range conversion is
 * undefined — so a cast that x86 answers with wrapped nonsense would kill
 * the whole module here, in the middle of kernel code. The saturating forms
 * clamp instead: in-range values convert identically, out-of-range ones
 * come back as the nearest representable integer (NaN as 0). This is what
 * clang emits for wasm32 with nontrapping-fptoint, and it is the only
 * behaviour that keeps undefined input from taking the seat down. */
static void we_trunc_sat(int to64, int from32, int unsig) {
    int op;
    if (to64) {
        op = from32 ? (unsig ? WOP_FC_I64_TRUNC_SAT_F32_U
                             : WOP_FC_I64_TRUNC_SAT_F32_S)
                    : (unsig ? WOP_FC_I64_TRUNC_SAT_F64_U
                             : WOP_FC_I64_TRUNC_SAT_F64_S);
    } else {
        op = from32 ? (unsig ? WOP_FC_I32_TRUNC_SAT_F32_U
                             : WOP_FC_I32_TRUNC_SAT_F32_S)
                    : (unsig ? WOP_FC_I32_TRUNC_SAT_F64_U
                             : WOP_FC_I32_TRUNC_SAT_F64_S);
    }
    we(WOP_PREFIX_FC);
    we_u32(op);
}

/* ----- types ----- */

static int valtype_of(int t) {
    switch (t & VT_BTYPE) {
    case VT_LLONG:  return VALTYPE_I64;
    case VT_FLOAT:  return VALTYPE_F32;
    case VT_DOUBLE: case VT_LDOUBLE: return VALTYPE_F64;
    case VT_VOID:   return VALTYPE_NONE;
    default:        return VALTYPE_I32;   /* int, ptr, char, short, struct ptr */
    }
}

static int is_i64_type(int t) { return (t & VT_BTYPE) == VT_LLONG; }

/* ----- module-wide state ----- */

/* wasm_build_module runs after the compile, when tcc_state has already been
 * cleared, but it needs the unit's sections and symbol table — and with
 * USING_GLOBALS every one of those names (text_section, symtab_section,
 * elfsym) reads through tcc_state. tccgen_init calls wasm_reset while the
 * state is still current, so keep it here and put it back for the
 * serializer. */
static TCCState *wasm_s1;

void wasm_reset(void)
{
    wasm_s1 = tcc_state;
    sbuf.len = 0;
    bbuf.len = 0;
    sigpool.len = 0;
    n_sigs = 0;
    n_srel = 0;
    n_mrel = 0;
    n_jmps = 0;
    n_funcs = 0;
    cur_nwparams = 0;
    cur_nparams = 0;
}

/* the module-shaping tables (imports, section placement), declared with the
 * serializer further down but released here with everything else */
static void wasm_release_module_state(void);

/* Free the serializer's buffers under the allocator that owns them (the
 * still-active arena window — jit.c calls this before restoring the
 * reallocator). NULLing them makes the next compile re-seed through its own
 * arena; reusing them would emit into a destroyed arena. */
void wasm_release_buffers(void)
{
    wbuf_free(&sbuf);
    wbuf_free(&bbuf);
    wbuf_free(&sigpool);
    if (sig_off) { tcc_free(sig_off); sig_off = NULL; }
    sig_cap = n_sigs = 0;
    if (srel) { tcc_free(srel); srel = NULL; }
    srel_cap = n_srel = 0;
    if (mrel) { tcc_free(mrel); mrel = NULL; }
    mrel_cap = n_mrel = 0;
    wbuf_free(&namepool);
    if (jmps) { tcc_free(jmps); jmps = NULL; }
    jmps_cap = n_jmps = 0;
    if (funcs) { tcc_free(funcs); funcs = NULL; }
    funcs_cap = n_funcs = 0;
    if (cur_params) { tcc_free(cur_params); cur_params = NULL; }
    cur_params_cap = cur_nparams = 0;
    wasm_release_module_state();
}

/* ----- Basic code gen interface ----- */

ST_FUNC void g(int c) { we((uint8_t)c); }

ST_FUNC void o(unsigned int c) {
    we((uint8_t)(c & 0xFF));
    we((uint8_t)((c >> 8) & 0xFF));
    we((uint8_t)((c >> 16) & 0xFF));
    we((uint8_t)((c >> 24) & 0xFF));
}

ST_FUNC void gen_le16(int v) {
    we((uint8_t)(v & 0xFF));
    we((uint8_t)((v >> 8) & 0xFF));
}

ST_FUNC void gen_le32(int c) {
    we((uint8_t)(c & 0xFF));
    we((uint8_t)((c >> 8) & 0xFF));
    we((uint8_t)((c >> 16) & 0xFF));
    we((uint8_t)((c >> 24) & 0xFF));
}

ST_FUNC void gen_le64(int64_t c) {
    gen_le32((int)c);
    gen_le32((int)(c >> 32));
}

/* ----- symbol operands ----- */

static int sym_is_func(Sym *sym) {
    return sym != NULL && (sym->type.t & VT_BTYPE) == VT_FUNC;
}

/* Put a symbol's value on the stack: a function's table index, or a data
 * address. Both are five-byte slots resolved at serialize time. */
static void push_sym_value(Sym *sym, int addend)
{
    we(WOP_I32_CONST);
    we_slot(sym_is_func(sym) ? WR_FUNC_ADDR : WR_DATA_ADDR,
            esym_index(sym), addend, -1, 0);
}

/* the base a frame offset is measured from: parameters live above $ap,
 * locals below $fp (see the locals enum) */
static int frame_base(int off) { return lidx(off >= 0 ? L_AP : L_FP); }

static void push_frame_addr(int off)
{
    we_get(frame_base(off));
    if (off) { push_i32(off); we(WOP_I32_ADD); }
}

/* address of an lvalue, on the stack */
static void push_lval_addr(int fr, Sym *sym, int fc)
{
    int v = fr & VT_VALMASK;
    if (v == VT_LOCAL) {
        push_frame_addr(fc);
    } else if (v == VT_LLOCAL) {
        /* the pointer itself is in the frame slot */
        push_frame_addr(fc);
        we_mem(WOP_I32_LOAD);
    } else if (v == VT_CONST) {
        if (fr & VT_SYM)
            push_sym_value(sym, fc);
        else
            push_i32(fc);
    } else {
        /* A register base already holds the whole address. TCC leaves the
         * offset it folded in sitting in c.i, so adding it here would count
         * it twice — i386 and arm ignore it for the same reason. */
        we_get(reg_i32(v));
    }
}

/* ----- comparisons -----
 * vtop->cmp_r packs both operand registers. Registers TREG_F0.. are the
 * float ones, so the register number says int or float; bit 13 says whether
 * a float compare is f32 or f64, which the register alone cannot tell (a
 * float register has both flavours). */
#define CMPR_VALID  0x8000
#define CMPR_PACK(a, b, f32) \
    ((unsigned short)(CMPR_VALID | (a) | ((b) << 8) | ((f32) ? 0x2000 : 0)))
#define CMPR_A(x)   ((x) & 0x1f)
#define CMPR_B(x)   (((x) >> 8) & 0x1f)
#define CMPR_F32(x) (((x) & 0x2000) != 0)

/* Comparing two long longs, tccgen compares the high halves and then asks a
 * second question about the *same* comparison (`vset_VT_CMP(TOK_NE)` with no
 * new operands): on a flags machine the flags are still standing. Here they
 * are two locals, so remember which ones the last comparison used. Nothing
 * between the two tests writes a register — only branches are emitted. */
static unsigned short last_cmp_r;

static void emit_cmp(int op, unsigned short cmp_r)
{
    int a, b;

    if (cmp_r & CMPR_VALID)
        last_cmp_r = cmp_r;
    else
        cmp_r = last_cmp_r;
    a = CMPR_A(cmp_r);
    b = CMPR_B(cmp_r);

    if (op == 0 || op == 1) {
        /* a comparison tccgen already folded to a constant */
        push_i32(op);
        return;
    }

    if (a >= TREG_F0) {
        int f32 = CMPR_F32(cmp_r);
        we_get(reg_flt(a, f32));
        we_get(reg_flt(b, f32));
        switch (op) {
        case TOK_EQ:  we(f32 ? WOP_F32_EQ : WOP_F64_EQ); break;
        case TOK_NE:  we(f32 ? WOP_F32_NE : WOP_F64_NE); break;
        case TOK_LT: case TOK_ULT: we(f32 ? WOP_F32_LT : WOP_F64_LT); break;
        case TOK_GT: case TOK_UGT: we(f32 ? WOP_F32_GT : WOP_F64_GT); break;
        case TOK_LE: case TOK_ULE: we(f32 ? WOP_F32_LE : WOP_F64_LE); break;
        case TOK_GE: case TOK_UGE: we(f32 ? WOP_F32_GE : WOP_F64_GE); break;
        default: tcc_error("wasm32: float compare op %d", op);
        }
        return;
    }
    we_get(reg_i32(a));
    we_get(reg_i32(b));
    switch (op) {
    case TOK_EQ:  we(WOP_I32_EQ); break;
    case TOK_NE:  we(WOP_I32_NE); break;
    case TOK_LT:  we(WOP_I32_LT_S); break;
    case TOK_GT:  we(WOP_I32_GT_S); break;
    case TOK_LE:  we(WOP_I32_LE_S); break;
    case TOK_GE:  we(WOP_I32_GE_S); break;
    case TOK_ULT: we(WOP_I32_LT_U); break;
    case TOK_UGT: we(WOP_I32_GT_U); break;
    case TOK_ULE: we(WOP_I32_LE_U); break;
    case TOK_UGE: we(WOP_I32_GE_U); break;
    default: tcc_error("wasm32: compare op %d", op);
    }
}

/* ----- control flow: record, do not encode ----- */

static int jump_new(int off, int cond, int target, int chain)
{
    if (n_jmps >= jmps_cap) {
        int want = jmps_cap ? jmps_cap * 2 : 64;
        WJump *nb = (WJump *)tcc_realloc(jmps, (size_t)want * sizeof(WJump));
        if (nb == NULL)
            tcc_error("wasm32: out of memory (jumps)");
        jmps = nb;
        jmps_cap = want;
    }
    jmps[n_jmps].off = off;
    jmps[n_jmps].cond = cond;
    jmps[n_jmps].target = target;
    jmps[n_jmps].chain = chain;
    we(WOP_NOP);                /* the placeholder: see the note above */
    return ++n_jmps;            /* handle is index+1: 0 stays "no chain" */
}

ST_FUNC int gjmp(int t)
{
    return jump_new((int)sbuf.len, 0, -1, t);
}

ST_FUNC void gjmp_addr(int a)
{
    jump_new((int)sbuf.len, 0, a, 0);
}

ST_FUNC int gjmp_cond(int op, int t)
{
    emit_cmp(op, vtop->cmp_r);
    return jump_new((int)sbuf.len, 1, -1, t);
}

ST_FUNC int gjmp_append(int n, int t)
{
    int p;
    if (n == 0)
        return t;
    if (t == 0)
        return n;
    /* walk n's chain to its end and hook t on */
    p = n;
    while (jmps[p - 1].chain != 0)
        p = jmps[p - 1].chain;
    jmps[p - 1].chain = t;
    return n;
}

ST_FUNC void gsym_addr(int t, int a)
{
    while (t != 0) {
        int next = jmps[t - 1].chain;
        jmps[t - 1].target = a;
        jmps[t - 1].chain = 0;
        t = next;
    }
}

/* ----- load: bring SValue sv into register r ----- */

static void emit_typed_load(int ft)
{
    switch (ft & VT_BTYPE) {
    case VT_BYTE: case VT_BOOL:
        we_mem((ft & VT_UNSIGNED) ? WOP_I32_LOAD8_U : WOP_I32_LOAD8_S); break;
    case VT_SHORT:
        we_mem((ft & VT_UNSIGNED) ? WOP_I32_LOAD16_U : WOP_I32_LOAD16_S); break;
    case VT_LLONG:  we_mem(WOP_I64_LOAD); break;
    case VT_FLOAT:  we_mem(WOP_F32_LOAD); break;
    case VT_DOUBLE: case VT_LDOUBLE: we_mem(WOP_F64_LOAD); break;
    default:        we_mem(WOP_I32_LOAD); break;
    }
}

static void set_reg_of_type(int r, int ft)
{
    switch (ft & VT_BTYPE) {
    case VT_LLONG:
        /* tccgen splits a long long into two int registers before it ever
         * asks for a load, so one arriving here means the value model and
         * this backend disagree; guessing a half would be silent nonsense */
        tcc_error("wasm32: 64-bit value asked for a single register");
        break;
    case VT_FLOAT:  we_set(reg_flt(r, 1)); break;
    case VT_DOUBLE: case VT_LDOUBLE: we_set(reg_flt(r, 0)); break;
    default:        we_set(reg_i32(r)); break;
    }
}

ST_FUNC void load(int r, SValue *sv)
{
    int fr = sv->r;
    int ft = sv->type.t & ~VT_DEFSIGN;
    int fc = sv->c.i;
    int v = fr & VT_VALMASK;
    ft &= ~(VT_VOLATILE | VT_CONSTANT);

    if (fr & VT_LVAL) {
        if ((ft & VT_BTYPE) == VT_STRUCT)
            tcc_error("wasm32: struct rvalue in a register");
        push_lval_addr(fr, sv->sym, fc);
        emit_typed_load(ft);
        set_reg_of_type(r, ft);
        return;
    }

    switch (v) {
    case VT_CONST:
        if (fr & VT_SYM) {
            push_sym_value(sv->sym, fc);
            we_set(reg_i32(r));
        } else if (is_i64_type(ft)) {
            /* The low word, not the whole value. gv() loads a 64-bit
             * constant in two steps and leaves the type 64-bit on the
             * first one (see tccgen's gv: "load constant"), then pushes
             * `ll >> 32` as a plain int for the second. Every 32-bit
             * backend reads this as the low half — i386 emits a movl of
             * the truncated c.i — and putting the whole i64 in the 64-bit
             * scratch instead left the pair's low register untouched:
             * `v % 1000` divided by whatever that register happened to
             * hold, which for a fresh local is zero. */
            push_i32((int32_t)sv->c.i);
            we_set(reg_i32(r));
        } else if ((ft & VT_BTYPE) == VT_FLOAT) {
            union { float f; uint32_t u; } cv;
            cv.f = (float)sv->c.ld;
            we(WOP_F32_CONST);
            gen_le32((int)cv.u);
            we_set(reg_flt(r, 1));
        } else if ((ft & VT_BTYPE) == VT_DOUBLE || (ft & VT_BTYPE) == VT_LDOUBLE) {
            union { double d; uint64_t u; } cv;
            cv.d = (double)sv->c.ld;
            we(WOP_F64_CONST);
            gen_le64((int64_t)cv.u);
            we_set(reg_flt(r, 0));
        } else {
            push_i32((int32_t)fc);
            we_set(reg_i32(r));
        }
        break;
    case VT_LOCAL:
        /* the address of a frame slot */
        we_get(lidx(L_FP));
        if (fc) { push_i32(fc); we(WOP_I32_ADD); }
        we_set(reg_i32(r));
        break;
    case VT_CMP:
        /* materialise the pending comparison as 0/1 */
        emit_cmp(sv->cmp_op, sv->cmp_r);
        we_set(reg_i32(r));
        break;
    case VT_JMP:
    case VT_JMPI: {
        /* the value is "did we arrive here by the jump chain or not": set
         * one constant, jump over the other, land the chain in between. */
        int t = v & 1;
        int over;
        push_i32(t);
        we_set(reg_i32(r));
        over = gjmp(0);
        gsym(fc);
        push_i32(t ^ 1);
        we_set(reg_i32(r));
        gsym(over);
        break;
    }
    default:
        if (is_float(ft)) {
            int f32 = (ft & VT_BTYPE) == VT_FLOAT;
            if (v != r) {
                we_get(reg_flt(v, f32));
                we_set(reg_flt(r, f32));
            }
        } else if (v != r) {
            we_get(reg_i32(v));
            we_set(reg_i32(r));
        }
        break;
    }
}

/* ----- store: write register r to the lvalue v ----- */

ST_FUNC void store(int r, SValue *v)
{
    int fr = v->r;
    int bt = v->type.t & VT_BTYPE;
    int fc = v->c.i;

    push_lval_addr(fr, v->sym, fc);

    switch (bt) {
    case VT_FLOAT:
        we_get(reg_flt(r, 1));
        we_mem(WOP_F32_STORE);
        break;
    case VT_DOUBLE: case VT_LDOUBLE:
        we_get(reg_flt(r, 0));
        we_mem(WOP_F64_STORE);
        break;
    case VT_LLONG:
        /* One half, four bytes — the same meaning i386 gives it. A
         * 64-bit type reaches store() when tccgen spills a long long
         * (save_reg_upstack): it calls store() twice with the type left
         * at VT_LLONG, once per register, the second time four bytes
         * further on. Writing eight bytes from the 64-bit scratch on
         * each of those calls wrote a value nobody had put there. */
        we_get(reg_i32(r));
        we_mem(WOP_I32_STORE);
        break;
    case VT_STRUCT:
        tcc_error("wasm32: struct store through a register");
        break;
    case VT_BYTE: case VT_BOOL:
        we_get(reg_i32(r));
        we_mem(WOP_I32_STORE8);
        break;
    case VT_SHORT:
        we_get(reg_i32(r));
        we_mem(WOP_I32_STORE16);
        break;
    default:
        we_get(reg_i32(r));
        we_mem(WOP_I32_STORE);
        break;
    }
}

/* ----- signatures from C types ----- */

static void sig_push(uint8_t *buf, int *n, int valtype) {
    if (*n >= 2 + 64)
        tcc_error("wasm32: more than 64 parameters");
    buf[(*n)++] = (uint8_t)valtype;
}

/* The signature of a definition.
 *
 * An ordinary function's parameters are wasm parameters, one each, with a
 * hidden leading pointer when it returns a struct. A variadic function takes
 * a single pointer instead: its whole argument list, hidden pointer
 * included, is a contiguous area the caller built, which is the only shape
 * in which `&last_named_param` can be followed by the variadic ones. */
static int sig_of_def(Sym *func_sym, int *out_nwparams)
{
    uint8_t buf[2 + 64];
    int n = 2;
    Sym *ps;
    CType *rt = &func_sym->type.ref->type;
    int struct_ret = (rt->t & VT_BTYPE) == VT_STRUCT;

    buf[0] = (uint8_t)(struct_ret ? VALTYPE_NONE : valtype_of(rt->t));
    if (func_sym->type.ref->f.func_type == FUNC_ELLIPSIS) {
        sig_push(buf, &n, VALTYPE_I32);
    } else {
        if (struct_ret)
            sig_push(buf, &n, VALTYPE_I32);
        for (ps = func_sym->type.ref->next; ps != NULL; ps = ps->next)
            sig_push(buf, &n, valtype_of(ps->type.t));
    }
    buf[1] = (uint8_t)(n - 2);
    *out_nwparams = n - 2;
    return sig_intern(buf, n);
}

/* ----- function prologue / epilogue ----- */

ST_FUNC int gfunc_sret(CType *vt, int variadic, CType *ret, int *ret_align, int *regsize)
{
    (void)vt; (void)variadic; (void)ret;
    /* every struct comes back through the hidden pointer: one rule, and it
     * keeps the wasm signature independent of the struct's shape */
    *ret_align = 1;
    *regsize = 4;
    return 0;
}

static void cur_param_add(int wlocal, int addr, int valtype, int size)
{
    if (cur_nparams >= cur_params_cap) {
        int want = cur_params_cap ? cur_params_cap * 2 : 32;
        WParam *nb = (WParam *)tcc_realloc(cur_params, (size_t)want * sizeof(WParam));
        if (nb == NULL)
            tcc_error("wasm32: out of memory (parameters)");
        cur_params = nb;
        cur_params_cap = want;
    }
    cur_params[cur_nparams].wlocal = wlocal;
    cur_params[cur_nparams].addr = addr;
    cur_params[cur_nparams].valtype = valtype;
    cur_params[cur_nparams].size = size;
    cur_nparams++;
}

ST_FUNC void gfunc_prolog(Sym *func_sym)
{
    Sym *sym = func_sym->type.ref;
    CType *rt = &sym->type;
    int struct_ret = (rt->t & VT_BTYPE) == VT_STRUCT;
    int addr = 0;
    int wlocal = 0;
    Sym *ps;
    const char *nm;
    int variadic;

    /* one function at a time in the scratch */
    sbuf.len = 0;
    n_srel = 0;
    n_jmps = 0;
    cur_nparams = 0;
    ind = 0;
    loc = 0;
    func_vc = 0;

    nm = get_tok_str(func_sym->v, NULL);
    if (nm != NULL) {
        size_t i;
        for (i = 0; i + 1 < sizeof(cur_name) && nm[i] != '\0'; i++)
            cur_name[i] = nm[i];
        cur_name[i] = '\0';
    } else {
        cur_name[0] = '\0';
    }

    cur_sig = sig_of_def(func_sym, &cur_nwparams);
    cur_ret_valtype = sig_bytes(cur_sig)[0];
    variadic = sym->f.func_type == FUNC_ELLIPSIS;

    /* Parameters are addressed in memory, not through the wasm locals that
     * carry them: &param, a long long's two halves and a struct's fields all
     * need an address. An ordinary function copies its incoming locals into
     * its own frame (bw_prolog); a variadic one reads them where the caller
     * put them, so nothing is copied and $ap is the passed pointer. */
    if (struct_ret) {
        if (!variadic)
            cur_param_add(wlocal++, addr, VALTYPE_I32, 0);
        func_vc = addr;
        addr += 4;
    }
    for (ps = sym->next; ps != NULL; ps = ps->next) {
        int align, size = type_size(&ps->type, &align);
        int is_struct = (ps->type.t & VT_BTYPE) == VT_STRUCT;
        (void)align;
        /* four-byte granularity, the layout this target's stdarg.h walks */
        if (!variadic)
            cur_param_add(wlocal++, addr, valtype_of(ps->type.t),
                          is_struct ? size : 0);
        gfunc_set_param(ps, addr, 0);
        addr += (size + 3) & -4;
    }
    cur_param_bytes = variadic ? 0 : ((addr + 7) & -8);
    cur_variadic = variadic;
}

/* --- assembling the body at epilogue time --- */

static void bw(uint8_t b) {
    if (wbuf_reserve(&bbuf, 1) != 0)
        tcc_error("wasm32: out of code memory");
    bbuf.p[bbuf.len++] = b;
}
static void bw_bytes(const uint8_t *p, int n) {
    if (wbuf_reserve(&bbuf, (size_t)n) != 0)
        tcc_error("wasm32: out of code memory");
    memcpy(bbuf.p + bbuf.len, p, (size_t)n);
    bbuf.len += (size_t)n;
}
static void bw_u32(uint32_t v) {
    uint8_t t[8], *p = t;
    leb_u32(&p, v);
    bw_bytes(t, (int)(p - t));
}
static void bw_i32(int32_t v) {
    uint8_t t[8], *p = t;
    leb_i32(&p, v);
    bw_bytes(t, (int)(p - t));
}
static void bw_op_u32(uint8_t op, uint32_t v) { bw(op); bw_u32(v); }
static void bw_get(int l) { bw_op_u32(WOP_LOCAL_GET, (uint32_t)l); }
static void bw_set(int l) { bw_op_u32(WOP_LOCAL_SET, (uint32_t)l); }
static void bw_const(int32_t v) { bw(WOP_I32_CONST); bw_i32(v); }

/* the locals declaration: runs of same-typed locals */
static void bw_locals_decl(void)
{
    bw_u32(4);
    bw_u32(L_INT0 + 8); bw(VALTYPE_I32);   /* $fp $pc $t32 $carry + r0..r7 */
    bw_u32(1);          bw(VALTYPE_I64);   /* $t64 */
    bw_u32(4);          bw(VALTYPE_F32);   /* float regs, f32 flavour */
    bw_u32(4);          bw(VALTYPE_F64);   /* float regs, f64 flavour */
}

static void bw_prolog(int locals_size)
{
    int i;
    /* fp = sp - param_bytes; sp = fp - locals_size */
    bw_op_u32(WOP_GLOBAL_GET, 0);
    if (cur_param_bytes) {
        bw_const(cur_param_bytes);
        bw(WOP_I32_SUB);
    }
    bw_op_u32(WOP_LOCAL_TEE, (uint32_t)lidx(L_FP));
    bw_const(locals_size);
    bw(WOP_I32_SUB);
    bw_op_u32(WOP_GLOBAL_SET, 0);

    if (cur_variadic) {
        /* the arguments stayed in the caller's frame */
        bw_get(0);
        bw_set(lidx(L_AP));
    } else {
        bw_get(lidx(L_FP));
        bw_set(lidx(L_AP));
    }

    for (i = 0; i < cur_nparams; i++) {
        WParam *p = &cur_params[i];
        bw_get(lidx(L_AP));
        if (p->addr) { bw_const(p->addr); bw(WOP_I32_ADD); }
        if (p->size) {
            /* a struct parameter arrives as a pointer: take our own copy */
            bw_get(p->wlocal);
            bw_const(p->size);
            bw(WOP_PREFIX_FC); bw_u32(WOP_FC_MEMORY_COPY); bw(0x00); bw(0x00);
            continue;
        }
        bw_get(p->wlocal);
        switch (p->valtype) {
        case VALTYPE_I64: bw(WOP_I64_STORE); bw_u32(0); bw_u32(0); break;
        case VALTYPE_F32: bw(WOP_F32_STORE); bw_u32(0); bw_u32(0); break;
        case VALTYPE_F64: bw(WOP_F64_STORE); bw_u32(0); bw_u32(0); break;
        default:          bw(WOP_I32_STORE); bw_u32(0); bw_u32(0); break;
        }
    }
}

static void bw_epilog_tail(void)
{
    /* give the frame back, then produce the result from its register */
    bw_get(lidx(L_FP));
    if (cur_param_bytes) {
        bw_const(cur_param_bytes);
        bw(WOP_I32_ADD);
    }
    bw_op_u32(WOP_GLOBAL_SET, 0);

    switch (cur_ret_valtype) {
    case VALTYPE_I32: bw_get(reg_i32(REG_IRET)); break;
    case VALTYPE_I64:
        /* tccgen returns a long long in the register pair: put the halves
         * back together for the one i64 result wasm declares */
        bw_get(reg_i32(REG_IRET));
        bw(WOP_I64_EXTEND_I32_U);
        bw_get(reg_i32(REG_IRE2));
        bw(WOP_I64_EXTEND_I32_U);
        bw(WOP_I64_CONST); bw(32);
        bw(WOP_I64_SHL);
        bw(WOP_I64_OR);
        break;
    case VALTYPE_F32: bw_get(reg_flt(REG_FRET, 1)); break;
    case VALTYPE_F64: bw_get(reg_flt(REG_FRET, 0)); break;
    default: break;
    }
}

/* copy scratch[from,to) into the body, moving this range's relocations with
 * it (their offsets shift by the copy) */
static void bw_code(int from, int to, int fn_index)
{
    int i;
    int delta = (int)bbuf.len - from;
    for (i = 0; i < n_srel; i++) {
        if (srel[i].off >= from && srel[i].off < to) {
            WReloc r = srel[i];
            r.off += delta;
            r.fn = fn_index;
            reloc_add(&mrel, &n_mrel, &mrel_cap, r);
        }
    }
    bw_bytes(sbuf.p + from, to - from);
}

static int bnd_find(const int *bnd, int n, int off)
{
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (bnd[mid] == off) return mid;
        if (bnd[mid] < off) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

ST_FUNC void gfunc_epilog(void)
{
    int code_end = (int)sbuf.len;
    int locals_size = (-loc + 7) & -8;
    int fn_index = n_funcs;
    int only_exits = 1;
    int i, j;
    int *bnd = NULL;
    int nb = 0;
    WFunc *fn;

    if (n_funcs >= funcs_cap) {
        int want = funcs_cap ? funcs_cap * 2 : 64;
        WFunc *nb2 = (WFunc *)tcc_realloc(funcs, (size_t)want * sizeof(WFunc));
        if (nb2 == NULL)
            tcc_error("wasm32: out of memory (function table)");
        funcs = nb2;
        funcs_cap = want;
    }
    fn = &funcs[fn_index];
    memset(fn, 0, sizeof(*fn));
    memcpy(fn->name, cur_name, sizeof(fn->name));
    fn->sig = cur_sig;
    fn->nwparams = cur_nwparams;
    fn->body_off = (int)bbuf.len;

    /* An unresolved jump means a label TCC never placed; landing on the
     * function exit is what falling off the end would do anyway. */
    for (i = 0; i < n_jmps; i++) {
        if (jmps[i].target < 0)
            jmps[i].target = code_end;
        if (jmps[i].target != code_end)
            only_exits = 0;
    }

    bw_locals_decl();
    bw_prolog(locals_size);

    if (only_exits) {
        /* No branch goes anywhere but out: one wrapping block is enough and
         * the code stays in source order. */
        int prev = 0;
        bw(WOP_BLOCK); bw(VALTYPE_NONE);
        for (i = 0; i < n_jmps; i++) {
            int off = jmps[i].off;
            /* jumps are created in offset order */
            bw_code(prev, off, fn_index);
            prev = off + 1;
            if (jmps[i].cond) {
                bw(WOP_IF); bw(VALTYPE_NONE);
                bw_op_u32(WOP_BR, 1);
                bw(WOP_END);
            } else {
                bw_op_u32(WOP_BR, 0);
            }
        }
        bw_code(prev, code_end, fn_index);
        bw(WOP_END);
    } else {
        /* General case: cut into basic blocks and dispatch on $pc. */
        bnd = (int *)tcc_malloc((size_t)(2 * n_jmps + 2) * sizeof(int));
        if (bnd == NULL)
            tcc_error("wasm32: out of memory (blocks)");
        /* a block starts at the function's entry, after every jump, and at
         * every label */
        bnd[nb++] = 0;
        for (i = 0; i < n_jmps; i++) {
            if (jmps[i].off + 1 < code_end)
                bnd[nb++] = jmps[i].off + 1;
            if (jmps[i].target > 0 && jmps[i].target < code_end)
                bnd[nb++] = jmps[i].target;
        }
        /* sort and unique (insertion sort: nb is small and nearly ordered) */
        for (i = 1; i < nb; i++) {
            int v = bnd[i];
            for (j = i - 1; j >= 0 && bnd[j] > v; j--)
                bnd[j + 1] = bnd[j];
            bnd[j + 1] = v;
        }
        for (i = 1, j = 1; i < nb; i++) {
            if (bnd[i] != bnd[j - 1])
                bnd[j++] = bnd[i];
        }
        nb = j;

        /* block $exit, loop $L, then nb blocks with a br_table inside */
        bw(WOP_BLOCK); bw(VALTYPE_NONE);
        bw(WOP_LOOP);  bw(VALTYPE_NONE);
        for (i = nb - 1; i >= 0; i--) { bw(WOP_BLOCK); bw(VALTYPE_NONE); }
        bw_get(lidx(L_PC));
        bw(WOP_BR_TABLE);
        bw_u32((uint32_t)nb);
        for (i = 0; i < nb; i++)
            bw_u32((uint32_t)i);        /* pc == i leaves block $B_i */
        bw_u32((uint32_t)(nb + 1));     /* default: out of the function */

        for (i = 0; i < nb; i++) {
            int start = bnd[i];
            int end = (i + 1 < nb) ? bnd[i + 1] : code_end;
            /* depth from inside block i's code: nb-1-i enclosing blocks,
             * then the loop, then the exit block */
            int d_loop = nb - 1 - i;
            int d_exit = nb - i;

            bw(WOP_END);                /* close $B_i: its code follows */

            /* the block's own code stops at its jump, if it has one; the
             * next block starts on the far side of that placeholder byte */
            for (j = 0; j < n_jmps; j++) {
                if (jmps[j].off >= start && jmps[j].off < end)
                    break;
            }
            if (j == n_jmps) {
                /* no jump: fall through to block i+1, which is simply the
                 * next thing in the byte stream */
                bw_code(start, end, fn_index);
                continue;
            }
            bw_code(start, jmps[j].off, fn_index);
            {
                int tgt = jmps[j].target;
                int tb = (tgt >= code_end) ? -1 : bnd_find(bnd, nb, tgt);
                if (tb < 0 && tgt < code_end)
                    tcc_error("wasm32: jump to an offset that is not a block");
                if (jmps[j].cond) {
                    bw(WOP_IF); bw(VALTYPE_NONE);
                    if (tb < 0) {
                        bw_op_u32(WOP_BR, (uint32_t)(d_exit + 1));
                    } else {
                        bw_const(tb);
                        bw_set(lidx(L_PC));
                        bw_op_u32(WOP_BR, (uint32_t)(d_loop + 1));
                    }
                    bw(WOP_END);
                } else if (tb < 0) {
                    bw_op_u32(WOP_BR, (uint32_t)d_exit);
                } else if (tb != i + 1) {
                    bw_const(tb);
                    bw_set(lidx(L_PC));
                    bw_op_u32(WOP_BR, (uint32_t)d_loop);
                }
            }
        }
        bw(WOP_END);                    /* loop */
        bw(WOP_END);                    /* exit block */
        tcc_free(bnd);
    }

    bw_epilog_tail();
    bw(WOP_END);                        /* function body */

    fn->body_len = (int)bbuf.len - fn->body_off;
    n_funcs++;
    sbuf.len = 0;
    n_srel = 0;
    n_jmps = 0;
    ind = 0;
}

/* ----- calls ----- */

/* i64 halves <-> one i64 on the stack */
static void push_pair_as_i64(int lo, int hi)
{
    we_get(reg_i32(lo));
    we(WOP_I64_EXTEND_I32_U);
    we_get(reg_i32(hi));
    we(WOP_I64_EXTEND_I32_U);
    push_i64(32);
    we(WOP_I64_SHL);
    we(WOP_I64_OR);
}

/* the i64 on the stack -> the IRET/IRE2 register pair */
static void split_i64_to_ret(void)
{
    we_set(lidx(L_T64));
    we_get(lidx(L_T64));
    we(WOP_I32_WRAP_I64);
    we_set(reg_i32(REG_IRET));
    we_get(lidx(L_T64));
    push_i64(32);
    we(WOP_I64_SHR_U);
    we(WOP_I32_WRAP_I64);
    we_set(reg_i32(REG_IRE2));
}

/* tccgen lowers 64-bit division, shifts and unsigned 64-bit conversions into
 * calls to libgcc helpers, because a 32-bit machine has no instruction for
 * them. wasm does: i64 is a native type here, only TCC's value model is
 * split in halves. So these calls never become calls — they are lowered in
 * place, which also keeps the module free of imports no loader could
 * resolve. */
static int lower_helper(int tok, int nb_args)
{
    int lo, hi, blo, bhi, is32;

    switch (tok) {
    case TOK___divdi3: case TOK___udivdi3:
    case TOK___moddi3: case TOK___umoddi3:
        if (nb_args != 2) return 0;
        gv2(RC_INT, RC_INT);
        lo = vtop[-1].r & VT_VALMASK; hi = vtop[-1].r2 & VT_VALMASK;
        blo = vtop[0].r & VT_VALMASK; bhi = vtop[0].r2 & VT_VALMASK;
        push_pair_as_i64(lo, hi);
        push_pair_as_i64(blo, bhi);
        we(tok == TOK___divdi3  ? WOP_I64_DIV_S :
           tok == TOK___udivdi3 ? WOP_I64_DIV_U :
           tok == TOK___moddi3  ? WOP_I64_REM_S : WOP_I64_REM_U);
        split_i64_to_ret();
        vtop -= 3;
        return 1;
    case TOK___ashrdi3: case TOK___lshrdi3: case TOK___ashldi3:
        if (nb_args != 2) return 0;
        gv2(RC_INT, RC_INT);
        lo = vtop[-1].r & VT_VALMASK; hi = vtop[-1].r2 & VT_VALMASK;
        blo = vtop[0].r & VT_VALMASK;
        push_pair_as_i64(lo, hi);
        if (is_i64_type(vtop[0].type.t)) {
            bhi = vtop[0].r2 & VT_VALMASK;
            push_pair_as_i64(blo, bhi);
        } else {
            we_get(reg_i32(blo));
            we(WOP_I64_EXTEND_I32_U);
        }
        we(tok == TOK___ashrdi3 ? WOP_I64_SHR_S :
           tok == TOK___lshrdi3 ? WOP_I64_SHR_U : WOP_I64_SHL);
        split_i64_to_ret();
        vtop -= 3;
        return 1;
    case TOK___floatundisf: case TOK___floatundidf:
        if (nb_args != 1) return 0;
        gv(RC_INT);
        lo = vtop[0].r & VT_VALMASK; hi = vtop[0].r2 & VT_VALMASK;
        push_pair_as_i64(lo, hi);
        is32 = (tok == TOK___floatundisf);
        we(is32 ? WOP_F32_CONVERT_I64_U : WOP_F64_CONVERT_I64_U);
        we_set(reg_flt(REG_FRET, is32));
        vtop -= 2;
        return 1;
    case TOK___fixunssfdi: case TOK___fixunsdfdi:
        if (nb_args != 1) return 0;
        gv(RC_FLOAT);
        is32 = (tok == TOK___fixunssfdi);
        we_get(reg_flt(vtop[0].r & VT_VALMASK, is32));
        we_trunc_sat(1, is32, 1);
        split_i64_to_ret();
        vtop -= 2;
        return 1;
    default:
        return 0;
    }
}

/* store the argument on top of the vstack into the outgoing area at `off` */
static void spill_arg(int off, int valtype, int struct_size)
{
    if (struct_size) {
        /* a struct argument is passed as a pointer and the callee takes its
         * own copy, so what goes in the slot is the address */
        push_frame_addr(off);
        push_lval_addr(vtop->r, vtop->sym, vtop->c.i);
        we_mem(WOP_I32_STORE);
        return;
    }
    switch (valtype) {
    case VALTYPE_I64: {
        int lo, hi;
        gv(RC_INT);
        lo = vtop->r & VT_VALMASK;
        hi = vtop->r2 & VT_VALMASK;
        push_frame_addr(off);
        push_pair_as_i64(lo, hi);
        we_mem(WOP_I64_STORE);
        break;
    }
    case VALTYPE_F32: case VALTYPE_F64: {
        int r;
        gv(RC_FLOAT);
        r = vtop->r & VT_VALMASK;
        push_frame_addr(off);
        we_get(reg_flt(r, valtype == VALTYPE_F32));
        we_mem(valtype == VALTYPE_F32 ? WOP_F32_STORE : WOP_F64_STORE);
        break;
    }
    default: {
        int r;
        gv(RC_INT);
        r = vtop->r & VT_VALMASK;
        push_frame_addr(off);
        we_get(reg_i32(r));
        we_mem(WOP_I32_STORE);
        break;
    }
    }
}

/* a variadic call carries structs by value, like every other argument */
static void copy_struct_arg(int off, int size)
{
    push_frame_addr(off);
    push_lval_addr(vtop->r, vtop->sym, vtop->c.i);
    push_i32(size);
    we_memory_copy();
}

static void load_arg(int off, int valtype)
{
    push_frame_addr(off);
    switch (valtype) {
    case VALTYPE_I64: we_mem(WOP_I64_LOAD); break;
    case VALTYPE_F32: we_mem(WOP_F32_LOAD); break;
    case VALTYPE_F64: we_mem(WOP_F64_LOAD); break;
    default:          we_mem(WOP_I32_LOAD); break;
    }
}

ST_FUNC void gfunc_call(int nb_args)
{
    SValue *fsv = &vtop[-nb_args];
    Sym *fs = fsv->type.ref;
    int functype = fs->f.func_type;
    int struct_ret = (fs->type.t & VT_BTYPE) == VT_STRUCT;
    int variadic = (functype == FUNC_ELLIPSIS);
    int direct = (fsv->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == (VT_CONST | VT_SYM);
    int i;
    int area_base = 0, area_size = 0;
    uint8_t sigb[2 + 64];
    int nsig = 2;
    int sig;
    int *aoff;
    int *avt;
    int *asz;

    if (direct && lower_helper(fsv->sym->v, nb_args))
        return;

#ifdef CONFIG_TCC_BCHECK
    if (tcc_state->do_bounds_check)
        gbound_args(nb_args);
#endif

    /* Every live value goes to memory first: a call clobbers the register
     * locals, and the arguments have to sit still while the rest are
     * evaluated. */
    save_regs(nb_args + 1);

    aoff = (int *)tcc_malloc((size_t)(nb_args + 1) * 3 * sizeof(int));
    if (aoff == NULL)
        tcc_error("wasm32: out of memory (call)");
    avt = aoff + nb_args + 1;
    asz = avt + nb_args + 1;

    for (i = 0; i < nb_args; i++) {
        SValue *a = &vtop[1 + i - nb_args];
        int align, size = type_size(&a->type, &align);
        int is_struct = (a->type.t & VT_BTYPE) == VT_STRUCT;
        (void)align;
        asz[i] = is_struct ? size : 0;
        avt[i] = (is_struct && !variadic) ? VALTYPE_I32 : valtype_of(a->type.t);
        if (variadic) {
            /* one contiguous area, four-byte granularity: the layout the
             * callee's parameters and its va_arg walk both assume */
            aoff[i] = area_size;
            area_size += (size + 3) & -4;
        }
    }
    if (variadic) {
        loc = (loc - ((area_size + 15) & -8)) & -8;
        area_base = loc;
        for (i = 0; i < nb_args; i++)
            aoff[i] += area_base;
    } else {
        loc = (loc - 8 * (nb_args + 1)) & -8;
        for (i = 0; i < nb_args; i++)
            aoff[i] = loc + 8 * i;
    }

    /* build the call's signature from the same rules the callee's prologue
     * used, so the two agree without consulting each other */
    sigb[0] = (uint8_t)(struct_ret ? VALTYPE_NONE : valtype_of(fs->type.t));
    if (variadic) {
        sig_push(sigb, &nsig, VALTYPE_I32);
    } else {
        for (i = 0; i < nb_args; i++)
            sig_push(sigb, &nsig, avt[i]);
    }
    sigb[1] = (uint8_t)(nsig - 2);
    sig = sig_intern(sigb, nsig);

    /* evaluate the arguments last to first: vtop is the last one */
    for (i = nb_args - 1; i >= 0; i--) {
        if (asz[i] && variadic)
            copy_struct_arg(aoff[i], asz[i]);
        else
            spill_arg(aoff[i], avt[i], asz[i]);
        vtop--;
    }

    /* the callee itself: an address for an indirect call */
    if (!direct)
        gv(RC_INT);

    if (variadic) {
        push_frame_addr(area_base);
    } else {
        for (i = 0; i < nb_args; i++)
            load_arg(aoff[i], avt[i]);
    }

    if (direct) {
        we(WOP_CALL);
        we_slot(WR_FUNC_IDX, esym_index(fsv->sym), 0, sig, 0);
    } else {
        /* a function pointer is its table index (slot 0 is the null one) */
        we_get(reg_i32(vtop->r & VT_VALMASK));
        we(WOP_CALL_INDIRECT);
        we_slot(WR_TYPE_IDX, 0, 0, sig, 0);
        we_u32(0);                      /* table 0 */
    }

    switch (sigb[0]) {
    case VALTYPE_I32: we_set(reg_i32(REG_IRET)); break;
    case VALTYPE_I64: split_i64_to_ret(); break;
    case VALTYPE_F32: we_set(reg_flt(REG_FRET, 1)); break;
    case VALTYPE_F64: we_set(reg_flt(REG_FRET, 0)); break;
    default: break;                     /* void, or a struct through memory */
    }

    tcc_free(aoff);
    vtop--;                             /* the function */
}

/* ----- integer operations ----- */

ST_FUNC void gen_opi(int op)
{
    int a, b, d;

    if (op >= TOK_ULT && op <= TOK_GT) {
        gv2(RC_INT, RC_INT);
        a = vtop[-1].r & VT_VALMASK;
        b = vtop[0].r & VT_VALMASK;
        vtop--;
        vset_VT_CMP(op);
        vtop->cmp_r = CMPR_PACK(a, b, 0);
        return;
    }

    gv2(RC_INT, RC_INT);
    a = vtop[-1].r & VT_VALMASK;
    b = vtop[0].r & VT_VALMASK;

    if (op == TOK_UMULL) {
        /* 32 x 32 -> 64, the one op that yields a register pair */
        vtop--;
        d = get_reg(RC_INT);
        we_get(reg_i32(a));
        we(WOP_I64_EXTEND_I32_U);
        we_get(reg_i32(b));
        we(WOP_I64_EXTEND_I32_U);
        we(WOP_I64_MUL);
        we_set(lidx(L_T64));
        we_get(lidx(L_T64));
        we(WOP_I32_WRAP_I64);
        we_set(reg_i32(a));
        we_get(lidx(L_T64));
        push_i64(32);
        we(WOP_I64_SHR_U);
        we(WOP_I32_WRAP_I64);
        we_set(reg_i32(d));
        vtop->r = (vtop->r & ~VT_VALMASK) | a;
        vtop->r2 = d;
        return;
    }

    switch (op) {
    /* The carry ops come in pairs from tccgen's long long lowering: the low
     * halves set it, the high halves use it, with nothing in between that
     * touches $carry. wasm has no flags, so it is a local. */
    case TOK_ADDC1:
        we_get(reg_i32(a)); we_get(reg_i32(b)); we(WOP_I32_ADD);
        we_set(lidx(L_T32));
        we_get(lidx(L_T32)); we_get(reg_i32(a)); we(WOP_I32_LT_U);
        we_set(lidx(L_CARRY));
        we_get(lidx(L_T32)); we_set(reg_i32(a));
        break;
    case TOK_ADDC2:
        we_get(reg_i32(a)); we_get(reg_i32(b)); we(WOP_I32_ADD);
        we_get(lidx(L_CARRY)); we(WOP_I32_ADD);
        we_set(reg_i32(a));
        break;
    case TOK_SUBC1:
        we_get(reg_i32(a)); we_get(reg_i32(b)); we(WOP_I32_LT_U);
        we_set(lidx(L_CARRY));
        we_get(reg_i32(a)); we_get(reg_i32(b)); we(WOP_I32_SUB);
        we_set(reg_i32(a));
        break;
    case TOK_SUBC2:
        we_get(reg_i32(a)); we_get(reg_i32(b)); we(WOP_I32_SUB);
        we_get(lidx(L_CARRY)); we(WOP_I32_SUB);
        we_set(reg_i32(a));
        break;
    default: {
        uint8_t opc;
        switch (op) {
        case '+': opc = WOP_I32_ADD; break;
        case '-': opc = WOP_I32_SUB; break;
        case '*': opc = WOP_I32_MUL; break;
        case '/': case TOK_PDIV: opc = WOP_I32_DIV_S; break;
        case '%': opc = WOP_I32_REM_S; break;
        case '&': opc = WOP_I32_AND; break;
        case '|': opc = WOP_I32_OR; break;
        case '^': opc = WOP_I32_XOR; break;
        case TOK_SHL: opc = WOP_I32_SHL; break;
        case TOK_SAR: opc = WOP_I32_SHR_S; break;
        case TOK_SHR: opc = WOP_I32_SHR_U; break;
        case TOK_UDIV: opc = WOP_I32_DIV_U; break;
        case TOK_UMOD: opc = WOP_I32_REM_U; break;
        default:
            tcc_error("wasm32: integer op '%s' is not lowered",
                      get_tok_str(op, NULL));
            return;
        }
        we_get(reg_i32(a));
        we_get(reg_i32(b));
        we(opc);
        we_set(reg_i32(a));
        break;
    }
    }
    vtop--;
    vtop->r = (vtop->r & ~VT_VALMASK) | a;
}

/* ----- float operations ----- */

ST_FUNC void gen_opf(int op)
{
    int a, b, is32;

    gv2(RC_FLOAT, RC_FLOAT);
    is32 = (vtop->type.t & VT_BTYPE) == VT_FLOAT;
    a = vtop[-1].r & VT_VALMASK;
    b = vtop[0].r & VT_VALMASK;

    if (op >= TOK_ULT && op <= TOK_GT) {
        vtop--;
        vset_VT_CMP(op);
        vtop->cmp_r = CMPR_PACK(a, b, is32);
        return;
    }

    we_get(reg_flt(a, is32));
    we_get(reg_flt(b, is32));
    switch (op) {
    case '+': we(is32 ? WOP_F32_ADD : WOP_F64_ADD); break;
    case '-': we(is32 ? WOP_F32_SUB : WOP_F64_SUB); break;
    case '*': we(is32 ? WOP_F32_MUL : WOP_F64_MUL); break;
    case '/': we(is32 ? WOP_F32_DIV : WOP_F64_DIV); break;
    default:
        tcc_error("wasm32: float op '%s' is not lowered", get_tok_str(op, NULL));
        return;
    }
    we_set(reg_flt(a, is32));
    vtop--;
    vtop->r = (vtop->r & ~VT_VALMASK) | a;
}

/* ----- type conversions ----- */

ST_FUNC void gen_cvt_itof(int t)
{
    int lo, hi, d, unsig, ll, to32;

    gv(RC_INT);
    unsig = (vtop->type.t & VT_UNSIGNED) != 0;
    ll = is_i64_type(vtop->type.t);
    lo = vtop->r & VT_VALMASK;
    hi = vtop->r2 & VT_VALMASK;
    to32 = (t & VT_BTYPE) == VT_FLOAT;

    --vtop;
    d = get_reg(RC_FLOAT);
    ++vtop;

    if (ll) {
        push_pair_as_i64(lo, hi);
        we(to32 ? (unsig ? WOP_F32_CONVERT_I64_U : WOP_F32_CONVERT_I64_S)
                : (unsig ? WOP_F64_CONVERT_I64_U : WOP_F64_CONVERT_I64_S));
    } else {
        we_get(reg_i32(lo));
        we(to32 ? (unsig ? WOP_F32_CONVERT_I32_U : WOP_F32_CONVERT_I32_S)
                : (unsig ? WOP_F64_CONVERT_I32_U : WOP_F64_CONVERT_I32_S));
    }
    we_set(reg_flt(d, to32));
    vtop->r = d;
}

ST_FUNC void gen_cvt_ftoi(int t)
{
    int r, d, from32, unsig;

    gv(RC_FLOAT);
    r = vtop->r & VT_VALMASK;
    from32 = (vtop->type.t & VT_BTYPE) == VT_FLOAT;
    unsig = (t & VT_UNSIGNED) != 0;

    if (is_i64_type(t)) {
        /* park the 64-bit result in the frame and let tccgen pick up both
         * halves from there, the way i386 does */
        loc = (loc - 8) & -8;
        we_get(lidx(L_FP));
        push_i32(loc);
        we(WOP_I32_ADD);
        we_get(reg_flt(r, from32));
        we_trunc_sat(1, from32, unsig);
        we_mem(WOP_I64_STORE);
        vtop->r = VT_LOCAL | VT_LVAL;
        vtop->c.i = loc;
        return;
    }

    --vtop;
    d = get_reg(RC_INT);
    ++vtop;
    we_get(reg_flt(r, from32));
    we_trunc_sat(0, from32, unsig);
    we_set(reg_i32(d));
    vtop->r = d;
}

ST_FUNC void gen_cvt_ftof(int t)
{
    int f = vtop->type.t & VT_BTYPE;
    int to32 = (t & VT_BTYPE) == VT_FLOAT;
    int from32 = f == VT_FLOAT;
    int r;

    if (to32 == from32)
        return;                         /* double and long double are one type */
    gv(RC_FLOAT);
    r = vtop->r & VT_VALMASK;
    if (from32) {
        we_get(reg_flt(r, 1));
        we(WOP_F64_PROMOTE_F32);
        we_set(reg_flt(r, 0));
    } else {
        we_get(reg_flt(r, 0));
        we(WOP_F32_DEMOTE_F64);
        we_set(reg_flt(r, 1));
    }
}

ST_FUNC void gen_cvt_sxtw(void) { }     /* only called when PTR_SIZE is 8 */

ST_FUNC void gen_cvt_csti(int t)
{
    int r = gv(RC_INT) & VT_VALMASK;
    int shift = ((t & VT_BTYPE) == VT_SHORT) ? 16 : 24;
    int unsig = (t & VT_UNSIGNED) != 0;

    we_get(reg_i32(r));
    if (unsig) {
        push_i32(shift == 16 ? 0xffff : 0xff);
        we(WOP_I32_AND);
    } else {
        push_i32(shift);
        we(WOP_I32_SHL);
        push_i32(shift);
        we(WOP_I32_SHR_S);
    }
    we_set(reg_i32(r));
}

/* ----- address computation ----- */

ST_FUNC void gen_addr32(int r, Sym *sym, int c)
{
    (void)r;
    if (sym != NULL) {
        push_sym_value(sym, c);
        return;
    }
    push_i32(c);
}

ST_FUNC void gen_addrpc32(int r, Sym *sym, int c) { gen_addr32(r, sym, c); }

/* ----- misc ----- */

ST_FUNC void gen_fill_nops(int bytes) { int i; for (i = 0; i < bytes; i++) we(WOP_NOP); }
ST_FUNC void gen_increment_tcov(SValue *sv) { (void)sv; }
ST_FUNC void gen_clear_cache(void) { }

ST_FUNC void ggoto(void)
{
    /* `goto *p`: the label values would have to be the dispatch loop's block
     * numbers, which are assigned after this expression is long gone. */
    tcc_error("wasm32: computed goto is not supported");
}

/* ----- varargs -----
 * Nothing to do here. This target's stdarg.h (include/tccdefs.h, the branch
 * for machines that pass arguments in memory) defines va_list as char* and
 * va_start as `&last_named_param + sizeof(it)`, which is plain C over the
 * argument area — and gfunc_call lays a variadic call's arguments out as
 * exactly that contiguous area, with $ap pointing into it. So tccgen never
 * asks the backend; these two exist only because tcc.h declares them. */

ST_FUNC void gen_va_start(void)
{
    tcc_error("wasm32: va_start is handled by stdarg.h on this target");
}

ST_FUNC void gen_va_arg(CType *t)
{
    (void)t;
    tcc_error("wasm32: va_arg is handled by stdarg.h on this target");
}

/* ----- variable length arrays: the frame pointer is a real global ----- */

ST_FUNC void gen_vla_sp_save(int addr)
{
    push_frame_addr(addr);
    we_op_u32(WOP_GLOBAL_GET, 0);
    we_mem(WOP_I32_STORE);
}

ST_FUNC void gen_vla_sp_restore(int addr)
{
    push_frame_addr(addr);
    we_mem(WOP_I32_LOAD);
    we_op_u32(WOP_GLOBAL_SET, 0);
}

ST_FUNC void gen_vla_alloc(CType *type, int align)
{
    int r;
    (void)type; (void)align;
    r = gv(RC_INT) & VT_VALMASK;        /* the size */
    we_op_u32(WOP_GLOBAL_GET, 0);
    we_get(reg_i32(r));
    we(WOP_I32_SUB);
    push_i32(-16);
    we(WOP_I32_AND);
    we_op_u32(WOP_GLOBAL_SET, 0);
    vpop();
}

ST_FUNC void gen_vla_result(int addr)
{
    push_frame_addr(addr);
    we_op_u32(WOP_GLOBAL_GET, 0);
    we_mem(WOP_I32_STORE);
}

/* ----- WASM module serializer ----- */
/* one-shot: make this one function non-static, then restore */
#undef ST_FUNC

typedef struct WImport {
    int name;                   /* offset into namepool */
    int sig;
} WImport;

static WImport *imports;
static int n_imports, imports_cap;

static const char *import_name(int i) { return (const char *)namepool.p + imports[i].name; }

static int find_func(const char *name)
{
    int i;
    for (i = 0; i < n_funcs; i++) {
        if (funcs[i].name[0] != '\0' && strcmp(funcs[i].name, name) == 0)
            return i;
    }
    return -1;
}

static int intern_import(const char *name, int sig)
{
    size_t len = strlen(name) + 1;
    int i;
    for (i = 0; i < n_imports; i++) {
        if (strcmp(import_name(i), name) == 0)
            return i;
    }
    if (n_imports >= imports_cap) {
        int want = imports_cap ? imports_cap * 2 : 32;
        WImport *nb = (WImport *)tcc_realloc(imports, (size_t)want * sizeof(WImport));
        if (nb == NULL)
            tcc_error("wasm32: out of memory (imports)");
        imports = nb;
        imports_cap = want;
    }
    if (wbuf_reserve(&namepool, len) != 0)
        tcc_error("wasm32: out of memory (import names)");
    imports[n_imports].name = (int)namepool.len;
    memcpy(namepool.p + namepool.len, name, len);
    namepool.len += len;
    imports[n_imports].sig = sig;
    return n_imports++;
}

/* TCC lowers struct assignment, struct arguments and struct returns into
 * calls to memmove/memcpy/memset. Nothing in a freshly compiled unit
 * defines them, and a module that imports them is a module the loader
 * cannot start on its own — so give them bodies here. wasm has the exact
 * instructions, and memory.copy already handles overlap the way memmove
 * must. A unit that defines its own keeps its own (find_func wins). */
static int mem_helper_kind(const char *name)
{
    if (strcmp(name, "memcpy") == 0 || strcmp(name, "memmove") == 0
        || strcmp(name, "memmove4") == 0 || strcmp(name, "memmove8") == 0)
        return 1;                       /* copy */
    if (strcmp(name, "memset") == 0)
        return 2;                       /* fill */
    return 0;
}

static int synth_mem_helper(const char *name, int kind, int sig);

/* section placement, filled in by wasm_build_module */
static int *sec_base;
static int sec_base_n;
static int common_next;

/* Both tables live only for the length of one serialization, but a refusal
 * in the middle of it leaves them set. Clearing them from one place —
 * wasm_release_buffers, which every caller runs while the window that
 * allocated them is still open — keeps the next compile from reallocating a
 * pointer into a dropped arena. */
static void wasm_release_module_state(void)
{
    if (sec_base != NULL) {
        tcc_free(sec_base);
        sec_base = NULL;
    }
    sec_base_n = 0;
    if (imports != NULL) {
        tcc_free(imports);
        imports = NULL;
    }
    imports_cap = 0;
    n_imports = 0;
}

static ElfSym *esym_at(int idx)
{
    if (idx <= 0)
        return NULL;
    return (ElfSym *)symtab_section->data + idx;
}

static const char *esym_name(int idx)
{
    ElfSym *e = esym_at(idx);
    if (e == NULL)
        return "";
    return (const char *)symtab_section->link->data + e->st_name;
}

/* place a tentative definition the way a linker would, keeping the
 * alignment the symbol asked for */
static int place_common(ElfSym *e)
{
    int align = (int)e->st_value ? (int)e->st_value : 4;
    int at = (common_next + align - 1) & -align;
    common_next = at + (int)e->st_size;
    e->st_shndx = SHN_ABS;
    e->st_value = (addr_t)at;
    return at;
}

/* a data symbol's linear-memory address */
static int sym_data_addr(int idx)
{
    ElfSym *esym = esym_at(idx);
    if (esym != NULL && esym->st_shndx != SHN_UNDEF
        && esym->st_shndx < (unsigned)sec_base_n
        && sec_base[esym->st_shndx] >= 0) {
        return sec_base[esym->st_shndx] + (int)esym->st_value;
    }
    if (esym != NULL && esym->st_shndx == SHN_COMMON)
        return place_common(esym);
    if (esym != NULL && esym->st_shndx == SHN_ABS)
        return (int)esym->st_value;
    if (esym != NULL) {
        /* A datum this unit only declares. The ELF object path leaves a
         * relocation for the final link; a wasm module is already linked, so
         * the address has to exist now. Give it a zeroed cell above the data
         * — the same treatment a tentative definition gets — and say so, the
         * way the linker would say "defaulting to zero". The symbols that
         * reach here are the ones an image supplies (__pm_metal_image_base
         * and friends), and a standalone module has no image. */
        int align = 4;
        int size = (int)esym->st_size ? (int)esym->st_size : 4;
        int at = (common_next + align - 1) & -align;
        common_next = at + size;
        esym->st_shndx = SHN_ABS;
        esym->st_value = (addr_t)at;
        tcc_warning("wasm32: '%s' has no definition in this unit; its address"
                    " is a zeroed %d-byte cell in this module's memory",
                    esym_name(idx), size);
        return at;
    }
    tcc_error("wasm32: relocation against a symbol that is not in the table");
    return 0;
}

static int synth_mem_helper(const char *name, int kind, int sig)
{
    WFunc *fn;
    size_t i;

    if (n_funcs >= funcs_cap) {
        int want = funcs_cap ? funcs_cap * 2 : 64;
        WFunc *nb = (WFunc *)tcc_realloc(funcs, (size_t)want * sizeof(WFunc));
        if (nb == NULL)
            return -1;
        funcs = nb;
        funcs_cap = want;
    }
    fn = &funcs[n_funcs];
    memset(fn, 0, sizeof(*fn));
    for (i = 0; i + 1 < sizeof(fn->name) && name[i] != '\0'; i++)
        fn->name[i] = name[i];
    fn->sig = sig;
    fn->nwparams = 3;
    fn->body_off = (int)bbuf.len;

    bw_u32(0);                          /* no locals */
    bw_get(0);
    bw_get(1);
    bw_get(2);
    bw(WOP_PREFIX_FC);
    bw_u32(kind == 1 ? WOP_FC_MEMORY_COPY : WOP_FC_MEMORY_FILL);
    bw(0x00);
    if (kind == 1)
        bw(0x00);
    bw_get(0);                          /* both return the destination */
    bw(WOP_END);

    fn->body_len = (int)bbuf.len - fn->body_off;
    return n_funcs++;
}

static int wasm_serialize(uint8_t **out_buf, int *out_len)
{
    TCCState *s1 = wasm_s1;
    WBuf out;
    int i, j;
    int data_end, stack_top, pages;
    int n_total_funcs;
    int *fn_type = NULL;
    int rc = -1;

    memset(&out, 0, sizeof(out));
    n_imports = 0;
    namepool.len = 0;

    /* ---- place the data sections in linear memory ---- */
    sec_base_n = s1->nb_sections;
    sec_base = (int *)tcc_malloc((size_t)sec_base_n * sizeof(int));
    if (sec_base == NULL)
        tcc_error("wasm32: out of memory (section placement)");
    for (i = 0; i < sec_base_n; i++)
        sec_base[i] = -1;
    data_end = WASM_DATA_BASE;
    for (i = 1; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];
        int align;
        if (s == NULL || !(s->sh_flags & SHF_ALLOC) || s->data_offset == 0)
            continue;
        if (s == text_section)
            continue;                   /* code does not live in memory here */
        align = s->sh_addralign ? (int)s->sh_addralign : 1;
        if (align > 16)
            align = 16;
        data_end = (data_end + align - 1) & -align;
        sec_base[i] = data_end;
        data_end += (int)s->data_offset;
    }
    common_next = (data_end + 15) & -16;

    /* ---- give the compiler's memory helpers bodies before anything is
     * counted, so they are defined functions rather than imports ---- */
    for (i = 0; i < n_mrel; i++) {
        WReloc *r = &mrel[i];
        const char *nm;
        int kind;
        if (r->kind != WR_FUNC_IDX && r->kind != WR_FUNC_ADDR)
            continue;
        if (r->esym <= 0)
            continue;
        nm = esym_name(r->esym);
        if (find_func(nm) >= 0)
            continue;
        kind = mem_helper_kind(nm);
        if (kind && r->sig >= 0) {
            const uint8_t *sg = sig_bytes(r->sig);
            if (sg[0] == VALTYPE_I32 && sg[1] == 3
                && sg[2] == VALTYPE_I32 && sg[3] == VALTYPE_I32
                && sg[4] == VALTYPE_I32) {
                if (synth_mem_helper(nm, kind, r->sig) < 0)
                    goto done;
            }
        }
    }

    /* ---- resolve relocations ----
     * Two passes: the first discovers imports (which shift every defined
     * function's index), the second writes the numbers. */
    for (i = 0; i < n_mrel; i++) {
        WReloc *r = &mrel[i];
        if (r->kind != WR_FUNC_IDX && r->kind != WR_FUNC_ADDR)
            continue;
        if (r->esym <= 0)
            continue;
        if (find_func(esym_name(r->esym)) >= 0)
            continue;
        if (intern_import(esym_name(r->esym), r->sig) < 0)
            goto done;
    }
    /* data may hold function pointers too */
    for (i = 1; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];
        if (s == NULL || sec_base[i] < 0 || s->reloc == NULL)
            continue;
        for (j = 0; j < (int)(s->reloc->data_offset / sizeof(ElfW_Rel)); j++) {
            ElfW_Rel *rel = (ElfW_Rel *)s->reloc->data + j;
            int symi = (int)ELFW(R_SYM)(rel->r_info);
            ElfSym *esym = (ElfSym *)symtab_section->data + symi;
            const char *nm = (char *)symtab_section->link->data + esym->st_name;
            if (esym->st_shndx == SHN_UNDEF && nm[0] != '\0'
                && find_func(nm) < 0 && ELFW(ST_TYPE)(esym->st_info) == STT_FUNC) {
                if (intern_import(nm, -1) < 0)
                    goto done;
            }
        }
    }

    n_total_funcs = n_imports + n_funcs;

    /* every import needs a signature; one that was only ever addressed (not
     * called) has none, so give it the only shape that can be honest: no
     * arguments, no result. It is a table entry, not a call target. */
    for (i = 0; i < n_imports; i++) {
        if (imports[i].sig < 0) {
            uint8_t sb[2];
            sb[0] = VALTYPE_NONE;
            sb[1] = 0;
            imports[i].sig = sig_intern(sb, 2);
        }
    }

    fn_type = (int *)tcc_malloc((size_t)(n_funcs + 1) * sizeof(int));
    if (fn_type == NULL)
        tcc_error("wasm32: out of memory (function types)");
    for (i = 0; i < n_funcs; i++)
        fn_type[i] = funcs[i].sig;

    stack_top = (common_next + 15 + WASM_STACK_SIZE) & -16;
    pages = (stack_top + WASM_PAGE - 1) / WASM_PAGE;
    if (pages < 2)
        pages = 2;                      /* WAMR's shared heap attach wants real backing */

    /* ---- now the numbers are known: patch the code's five-byte slots ---- */
    for (i = 0; i < n_mrel; i++) {
        WReloc *r = &mrel[i];
        uint8_t *slot = bbuf.p + r->off;
        switch (r->kind) {
        case WR_FUNC_IDX:
        case WR_FUNC_ADDR: {
            const char *nm = esym_name(r->esym);
            int k = find_func(nm);
            int idx;
            if (k >= 0)
                idx = n_imports + k;
            else {
                int im = intern_import(nm, r->sig);
                if (im < 0)
                    goto done;
                idx = im;
            }
            leb_pad5(slot, (uint32_t)(r->kind == WR_FUNC_IDX ? idx : idx + 1));
            break;
        }
        case WR_TYPE_IDX:
            leb_pad5(slot, (uint32_t)r->sig);
            break;
        case WR_DATA_ADDR:
        default:
            leb_pad5(slot, (uint32_t)(sym_data_addr(r->esym) + r->addend));
            break;
        }
    }

    /* ---- apply the data sections' own relocations ---- */
    for (i = 1; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];
        if (s == NULL || sec_base[i] < 0 || s->reloc == NULL || s->data == NULL)
            continue;
        for (j = 0; j < (int)(s->reloc->data_offset / sizeof(ElfW_Rel)); j++) {
            ElfW_Rel *rel = (ElfW_Rel *)s->reloc->data + j;
            int symi = (int)ELFW(R_SYM)(rel->r_info);
            ElfSym *esym = (ElfSym *)symtab_section->data + symi;
            const char *nm = (char *)symtab_section->link->data + esym->st_name;
            unsigned char *at = s->data + rel->r_offset;
            int val;
            int k;
            if (rel->r_offset + 4 > s->data_offset)
                continue;
            k = nm[0] ? find_func(nm) : -1;
            if (k >= 0) {
                val = n_imports + k + 1;        /* a function pointer */
            } else if (esym->st_shndx != SHN_UNDEF
                       && esym->st_shndx < (unsigned)sec_base_n
                       && sec_base[esym->st_shndx] >= 0) {
                val = sec_base[esym->st_shndx] + (int)esym->st_value;
            } else if (esym->st_shndx == SHN_COMMON) {
                val = place_common(esym);
            } else if (nm[0] != '\0') {
                int im = intern_import(nm, -1);
                if (im < 0)
                    goto done;
                val = im + 1;                   /* an imported function */
            } else {
                continue;
            }
            /* Elf32_Rel keeps the addend in place */
            val += (int)read32le(at);
            write32le(at, (uint32_t)val);
        }
    }

    /* ---- serialize ---- */
#define OUT_RESERVE(n_) do { if (wbuf_reserve(&out, (size_t)(n_)) != 0) \
        tcc_error("wasm32: out of memory (module is %d bytes so far)", \
            (int)out.len); } while (0)
#define OUT_B(v_) do { OUT_RESERVE(1); out.p[out.len++] = (uint8_t)(v_); } while (0)
#define OUT_BYTES(src_, n_) do { OUT_RESERVE(n_); memcpy(out.p + out.len, (const void *)(src_), (size_t)(n_)); out.len += (size_t)(n_); } while (0)
#define OUT_U32(v_) do { uint8_t tb_[8], *tq_ = tb_; leb_u32(&tq_, (uint32_t)(v_)); OUT_BYTES(tb_, (int)(tq_ - tb_)); } while (0)

    OUT_BYTES("\0asm\x01\0\0\0", 8);

    /* every section is written body-first into a scratch, so its size is
     * exact instead of guessed */
    {
        WBuf b;
        memset(&b, 0, sizeof(b));

#define SEC_RESERVE(n_) do { if (wbuf_reserve(&b, (size_t)(n_)) != 0) { \
        wbuf_free(&b); \
        tcc_error("wasm32: out of memory (section body wants %d more bytes)", \
            (int)(n_)); } } while (0)
#define SEC_B(v_) do { SEC_RESERVE(1); b.p[b.len++] = (uint8_t)(v_); } while (0)
#define SEC_BYTES(src_, n_) do { SEC_RESERVE(n_); memcpy(b.p + b.len, (const void *)(src_), (size_t)(n_)); b.len += (size_t)(n_); } while (0)
#define SEC_U32(v_) do { uint8_t tb_[8], *tq_ = tb_; leb_u32(&tq_, (uint32_t)(v_)); SEC_BYTES(tb_, (int)(tq_ - tb_)); } while (0)
#define SEC_I32(v_) do { uint8_t tb_[8], *tq_ = tb_; leb_i32(&tq_, (int32_t)(v_)); SEC_BYTES(tb_, (int)(tq_ - tb_)); } while (0)
#define SEC_FLUSH(id_) do { \
        OUT_B(id_); OUT_U32(b.len); OUT_BYTES(b.p, (int)b.len); b.len = 0; } while (0)

        /* type section (1) */
        SEC_U32(n_sigs);
        for (i = 0; i < n_sigs; i++) {
            const uint8_t *sg = sig_bytes(i);
            SEC_B(0x60);
            SEC_U32(sg[1]);
            for (j = 0; j < sg[1]; j++)
                SEC_B(sg[2 + j]);
            if (sg[0] == VALTYPE_NONE) {
                SEC_U32(0);
            } else {
                SEC_U32(1);
                SEC_B(sg[0]);
            }
        }
        SEC_FLUSH(1);

        /* import section (2): every function this unit calls but does not
         * define, as env.<name> with the signature seen at the call */
        if (n_imports > 0) {
            SEC_U32(n_imports);
            for (i = 0; i < n_imports; i++) {
                const char *inm = import_name(i);
                size_t nl = strlen(inm);
                SEC_U32(3);
                SEC_BYTES("env", 3);
                SEC_U32(nl);
                SEC_BYTES(inm, (int)nl);
                SEC_B(0x00);            /* kind: function */
                SEC_U32(imports[i].sig);
            }
            SEC_FLUSH(2);
        }

        /* function section (3) */
        SEC_U32(n_funcs);
        for (i = 0; i < n_funcs; i++)
            SEC_U32(fn_type[i]);
        SEC_FLUSH(3);

        /* table section (4): one funcref table so function pointers work.
         * Slot 0 stays empty, so a null pointer is not callable. */
        SEC_U32(1);
        SEC_B(0x70);                    /* funcref */
        SEC_B(0x00);                    /* no maximum */
        SEC_U32(n_total_funcs + 1);
        SEC_FLUSH(4);

        /* memory section (5) */
        SEC_U32(1);
        SEC_B(0x00);                    /* no maximum: the loader may grow it */
        SEC_U32(pages);
        SEC_FLUSH(5);

        /* global section (6): the stack pointer */
        SEC_U32(1);
        SEC_B(VALTYPE_I32);
        SEC_B(0x01);                    /* mutable */
        SEC_B(WOP_I32_CONST);
        SEC_I32(stack_top);
        SEC_B(WOP_END);
        SEC_FLUSH(6);

        /* export section (7): the memory and every named function, so the
         * loader can address any face by name */
        {
            int n_exp = 1;
            for (i = 0; i < n_funcs; i++) {
                if (funcs[i].name[0] != '\0' && find_func(funcs[i].name) == i)
                    n_exp++;
            }
            SEC_U32(n_exp);
            SEC_U32(6); SEC_BYTES("memory", 6); SEC_B(0x02); SEC_U32(0);
            for (i = 0; i < n_funcs; i++) {
                size_t nl;
                if (funcs[i].name[0] == '\0' || find_func(funcs[i].name) != i)
                    continue;
                nl = strlen(funcs[i].name);
                SEC_U32(nl);
                SEC_BYTES(funcs[i].name, (int)nl);
                SEC_B(0x00);
                SEC_U32(n_imports + i);
            }
            SEC_FLUSH(7);
        }

        /* element section (9): table[1..] = every function, so a function
         * pointer is just its index plus one */
        if (n_total_funcs > 0) {
            SEC_U32(1);
            SEC_U32(0);                 /* table 0 */
            SEC_B(WOP_I32_CONST); SEC_U32(1); SEC_B(WOP_END);
            SEC_U32(n_total_funcs);
            for (i = 0; i < n_total_funcs; i++)
                SEC_U32(i);
            SEC_FLUSH(9);
        }

        /* code section (10) */
        SEC_U32(n_funcs);
        for (i = 0; i < n_funcs; i++) {
            SEC_U32(funcs[i].body_len);
            SEC_BYTES(bbuf.p + funcs[i].body_off, funcs[i].body_len);
        }
        /* the code's five-byte slots were patched in bbuf before this copy */
        SEC_FLUSH(10);

        /* data section (11) */
        {
            int n_seg = 0;
            for (i = 1; i < s1->nb_sections; i++) {
                Section *s = s1->sections[i];
                if (s == NULL || sec_base[i] < 0 || s->sh_type == SHT_NOBITS
                    || s->data == NULL)
                    continue;
                n_seg++;
            }
            if (n_seg > 0) {
                SEC_U32(n_seg);
                for (i = 1; i < s1->nb_sections; i++) {
                    Section *s = s1->sections[i];
                    if (s == NULL || sec_base[i] < 0 || s->sh_type == SHT_NOBITS
                        || s->data == NULL)
                        continue;
                    SEC_U32(0);         /* memory 0 */
                    SEC_B(WOP_I32_CONST);
                    SEC_I32(sec_base[i]);
                    SEC_B(WOP_END);
                    SEC_U32(s->data_offset);
                    SEC_BYTES(s->data, (int)s->data_offset);
                }
                SEC_FLUSH(11);
            }
        }
        wbuf_free(&b);
    }

    *out_buf = out.p;
    *out_len = (int)out.len;
    out.p = NULL;                       /* handed to the caller */
    rc = 0;

done:
    if (out.p != NULL)
        wbuf_free(&out);
    if (fn_type != NULL)
        tcc_free(fn_type);
    wasm_release_module_state();
    return rc;
}

/* Serialization runs after tcc_compile has returned, so nothing has armed
 * the error handler any more: a refusal from in here would report through
 * the error func and then exit(1), taking the whole seat down instead of
 * failing one compile. Arm it around the serializer so a refusal comes back
 * as -1 with its message already delivered.
 *
 * The abandoned frame's buffers are not freed on that path; the caller drops
 * the allocator window it gave us (jit/c's arena, the prove's libc heap),
 * which is also what tcc does with its own frames after an error. */
int wasm_build_module(uint8_t **out_buf, int *out_len)
{
    TCCState *s1 = wasm_s1;
    TCCState *saved_state = tcc_state;
    int rc;

    if (s1 == NULL)
        return -1;
    tcc_state = s1;
    /* tcc_error decorates a message with the line it is parsing when the
     * handler is armed, and the parse is over: `file` points at a closed
     * BufferedFile. Say there is no file and the message comes out as
     * "tcc: error: wasm32: ..." instead of a read of freed memory. */
    file = NULL;
    s1->current_filename = NULL;
    if (setjmp(s1->error_jmp_buf) == 0) {
        s1->error_set_jmp_enabled = 1;
        rc = wasm_serialize(out_buf, out_len);
    } else {
        rc = -1;
    }
    s1->error_set_jmp_enabled = 0;
    tcc_state = saved_state;
    return rc;
}
#define ST_FUNC static

#endif /* !TARGET_DEFS_ONLY */
