/*
 *  WASM32 code generator for TCC
 *
 *  Emits WebAssembly 1.0 (MVP) bytecode.
 *
 *  This file is included twice by tcc.h:
 *    - First with TARGET_DEFS_ONLY for arch parameters
 *    - Second without it for the code generator implementation
 */

#ifdef TARGET_DEFS_ONLY

/* --- Arch parameters (TARGET_DEFS_ONLY) --- */

#define NB_REGS            8
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

#define RC_IRET    RC_R0
#define RC_IRE2    RC_R1
#define RC_FRET    RC_R0

enum {
    TREG_R0 = 0,
    TREG_R1,
    TREG_R2,
    TREG_R3,
    TREG_R4,
    TREG_R5,
    TREG_R6,
    TREG_R7,
};

#define REG_VALUE(reg) ((reg) & 7)

#define REG_IRET TREG_R0
#define REG_IRE2 TREG_R1
#define REG_FRET TREG_R0

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

/* ----- WASM opcodes (MVP subset) ----- */
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
    WOP_I32_LOAD8_S   = 0x2c,
    WOP_I32_LOAD8_U   = 0x2d,
    WOP_I32_LOAD16_S  = 0x2e,
    WOP_I32_LOAD16_U  = 0x2f,
    WOP_I64_LOAD      = 0x29,
    WOP_F32_LOAD      = 0x2a,
    WOP_F64_LOAD      = 0x2b,
    WOP_I32_STORE     = 0x36,
    WOP_I32_STORE8    = 0x3a,
    WOP_I32_STORE16   = 0x3b,
    WOP_I64_STORE     = 0x37,
    WOP_F32_STORE     = 0x38,
    WOP_F64_STORE     = 0x39,
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
    WOP_F32_EQ        = 0x50,
    WOP_F32_NE        = 0x51,
    WOP_F32_LT        = 0x52,
    WOP_F32_GT        = 0x53,
    WOP_F32_LE        = 0x54,
    WOP_F32_GE        = 0x55,
    WOP_F64_EQ        = 0x56,
    WOP_F64_NE        = 0x57,
    WOP_F64_LT        = 0x58,
    WOP_F64_GT        = 0x59,
    WOP_F64_LE        = 0x5a,
    WOP_F64_GE        = 0x5b,
    WOP_I32_CLZ       = 0x67,
    WOP_I32_CTZ       = 0x68,
    WOP_I32_POPCNT    = 0x69,
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
    WOP_F32_ABS       = 0x8b,
    WOP_F32_NEG       = 0x8c,
    WOP_F32_SQRT      = 0x91,
    WOP_F32_ADD       = 0x92,
    WOP_F32_SUB       = 0x93,
    WOP_F32_MUL       = 0x94,
    WOP_F32_DIV       = 0x95,
    WOP_F64_ABS       = 0x99,
    WOP_F64_NEG       = 0x9a,
    WOP_F64_SQRT      = 0x9f,
    WOP_F64_ADD       = 0xa0,
    WOP_F64_SUB       = 0xa1,
    WOP_F64_MUL       = 0xa2,
    WOP_F64_DIV       = 0xa3,
    WOP_I32_WRAP_I64     = 0xa7,
    WOP_I32_TRUNC_F32_S  = 0xa8,
    WOP_I32_TRUNC_F64_S  = 0xaa,
    WOP_F32_CONVERT_I32_S = 0xb2,
    WOP_F64_CONVERT_I32_S = 0xb7,
    WOP_F32_DEMOTE_F64   = 0xb6,
    WOP_F64_PROMOTE_F32  = 0xbb,
    WOP_I64_EXTEND_I32_S = 0xac,
    WOP_I64_EXTEND_I32_U = 0xad,
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
};

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

/* ----- Code emission context ----- */

/* Growable emission buffers: capacity starts at CODE_BUF_FIRST and doubles
 * on demand through tcc_realloc, so the backend follows whatever allocator
 * the embedder installed (jit.c routes TCC allocations through the caller's
 * arena). Everything funnels through we()/we_u32()/we_i32(), so the grow
 * check lives in one place. */
#define CODE_BUF_FIRST (64 * 1024)
static uint8_t *code_buf = NULL;
static size_t code_cap = 0;
static uint8_t *code_ptr = NULL;
/* Per-function build state, one row per compiled function. Grown through
 * tcc_realloc like the code buffer, so the tables follow the embedder's
 * allocator (jit.c routes TCC through the caller's arena) and no compile is
 * capped by a static 256-function array. */
typedef struct WasmFuncRow {
    int start_offs;
    int body_len;
    int local_decl_count;
    int param_count;        /* source params (all i32) */
    int ret_i32;           /* 1 when the fn returns a value */
    char name[128];
} WasmFuncRow;

#define FUNC_ROWS_FIRST 64
static WasmFuncRow *func_rows = NULL;
static int func_rows_cap = 0;
static int cur_ret_i32;             /* the fn being generated */
static int cur_param_count;         /* its param count (register-local base) */
static int func_count;
static int label_depth;

/* Ensure room for one more function row. Returns 0 on success. */
static int func_row_reserve(void) {
    if (func_count < func_rows_cap) {
        return 0;
    }
    int want = func_rows_cap ? func_rows_cap * 2 : FUNC_ROWS_FIRST;
    WasmFuncRow *nb = (WasmFuncRow *)tcc_realloc(func_rows,
        (unsigned long)want * sizeof(WasmFuncRow));
    if (nb == NULL) {
        return -1;
    }
    memset(nb + func_rows_cap, 0,
        (size_t)(want - func_rows_cap) * sizeof(WasmFuncRow));
    func_rows = nb;
    func_rows_cap = want;
    return 0;
}

/* Grow so code_ptr has room for n more bytes. Returns 0 on success. */
static int code_reserve(size_t n) {
    size_t used = (size_t)(code_ptr - code_buf);
    if (used + n <= code_cap) {
        return 0;
    }
    size_t want = code_cap ? code_cap : CODE_BUF_FIRST;
    while (used + n > want) {
        want *= 2;
    }
    uint8_t *nb = (uint8_t *)tcc_realloc(code_buf, want);
    if (nb == NULL) {
        return -1;
    }
    code_buf = nb;
    code_cap = want;
    code_ptr = code_buf + used;
    return 0;
}

/* Frame model: TCC addresses locals by negative offsets from 0 (loc counts
 * down). Map offset a -> linear-memory FRAME_BASE + a so every frame slot
 * lands in the module's own memory (FRAME_BASE keeps them positive). */
#define WASM_FRAME_BASE 8192
/* Param model: each source param is one i32 wasm local (locals 0..n-1, the
 * functype's own params). gfunc_set_param assigns param i the sentinel
 * address PARAM_ADDR(i); load/store recognize it and use the local. */
#define PARAM_ADDR(i) (-65536 - 4 * (i))
static int param_local_of(int addr) {
    if (addr <= -65536) {
        int idx = (-addr - 65536) / 4;
        return idx;
    }
    return -1;
}
static int frame_addr(int addr) { return WASM_FRAME_BASE + addr; }

void wasm_reset(void) {
    /* first use lazily seeds the buffers through the active TCC allocator
     * (jit.c routes that through the caller's arena) */
    if (code_reserve(1) != 0) {
        return;
    }
    code_ptr = code_buf;
    func_count = 0;
    label_depth = 0;
}

/* Free the serializer's static buffers under the allocator that owns them
 * (the still-active arena window — jit.c calls this before restoring the
 * reallocator). NULLs them so the next compile re-seeds through its own
 * arena: reusing a previous compile's buffers would emit into the destroyed
 * arena's memory. */
void wasm_release_buffers(void) {
    if (code_buf != NULL) {
        tcc_free(code_buf);
        code_buf = NULL;
        code_ptr = NULL;
        code_cap = 0;
    }
    if (func_rows != NULL) {
        tcc_free(func_rows);
        func_rows = NULL;
        func_rows_cap = 0;
    }
}

/* leb emitters must also pass through the reserve check: leb_u32/leb_i32
 * take a **pp and advance it, so give each its own checked wrapper. */
static void we(uint8_t b) {
    if (code_reserve(1) != 0) {
        tcc_error("wasm32: out of code memory");
    }
    *code_ptr++ = b;
}
static void we_u32(uint32_t v) {
    if (code_reserve(5) != 0) {
        tcc_error("wasm32: out of code memory");
    }
    leb_u32(&code_ptr, v);
}
static void we_i32(int32_t v) {
    if (code_reserve(5) != 0) {
        tcc_error("wasm32: out of code memory");
    }
    leb_i32(&code_ptr, v);
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
    gen_le32((uint32_t)c);
    gen_le32((uint32_t)(c >> 32));
}

static void we_op_local(uint8_t op, int idx) { we(op); we_u32((uint32_t)idx); }
static void push_i32(int32_t v) { we(WOP_I32_CONST); we_i32(v); }

/* register r's wasm local index: the body's declared locals start AFTER the
 * functype's params, so every synthetic register local is shifted by the
 * current fn's param count. */
static int reg_local(int r) { return cur_param_count + r; }

/* consume the stack top into local idx: locals ARE the registers, the wasm
 * stack is only ever transient scratch between one producer and its
 * consumer, so the operand stack is balanced at every statement boundary
 * (a requirement for validation). */
static void we_op_setlocal(uint8_t op, int idx) { we(op); we_u32((uint32_t)idx); }

/* ----- load: bring SValue sv into register r ----- */
ST_FUNC void load(int r, SValue *sv)
{
    int fr = sv->r;
    int ft = sv->type.t & ~VT_DEFSIGN;
    int fc = sv->c.i;
    int v = fr & VT_VALMASK;
    int pl;
    ft &= ~(VT_VOLATILE | VT_CONSTANT);

    if (fr & VT_LVAL) {
        if (v == VT_LOCAL && (pl = param_local_of(fc)) >= 0) {
            /* a param: its value IS the local, no memory deref. The
             * destination register is a declared local, so it lives past
             * the params — reg_local, not the raw register number (a raw
             * r < n_params would clobber a param slot). */
            we_op_local(WOP_LOCAL_GET, pl);
            we_op_setlocal(WOP_LOCAL_SET, reg_local(r));
            return;
        }
        if (v == VT_LOCAL) {
            push_i32(frame_addr(fc));
        } else if (v == VT_LLOCAL) {
            push_i32(frame_addr(fc));
            we_op_local(WOP_LOCAL_GET, reg_local(NB_REGS + 0));
            we(WOP_I32_ADD);
        } else if ((fr & VT_SYM) && sv->sym) {
            push_i32(fc);
        } else {
            we_op_local(WOP_LOCAL_GET, reg_local(v));
            if (fc) { push_i32(fc); we(WOP_I32_ADD); }
        }
        switch (ft & VT_BTYPE) {
        case VT_BYTE: case VT_BOOL:
            we(ft & VT_UNSIGNED ? WOP_I32_LOAD8_U : WOP_I32_LOAD8_S);
            we_u32(0); we_u32(0); break;
        case VT_SHORT:
            we(ft & VT_UNSIGNED ? WOP_I32_LOAD16_U : WOP_I32_LOAD16_S);
            we_u32(1); we_u32(0); break;
        case VT_LLONG: we(WOP_I64_LOAD); we_u32(3); we_u32(0); break;
        case VT_FLOAT: we(WOP_F32_LOAD); we_u32(2); we_u32(0); break;
        case VT_DOUBLE: case VT_LDOUBLE: we(WOP_F64_LOAD); we_u32(3); we_u32(0); break;
        case VT_INT: case VT_PTR:
        default: we(WOP_I32_LOAD); we_u32(2); we_u32(0); break;
        }
    } else {
        if (fr & VT_SYM) { push_i32(fc); }
        else if (v == VT_CONST) { push_i32(fc); }
        else if (v == VT_CMP || v == VT_JMP || v == VT_JMPI) {
            /* cmp results and jump values live in the vtop register as 0/1 */
            we_op_local(WOP_LOCAL_GET, reg_local(TREG_R0));
        }
        else { we_op_local(WOP_LOCAL_GET, reg_local(v)); }
    }
    we_op_setlocal(WOP_LOCAL_SET, reg_local(r));
}

/* ----- store: write register r to SValue v (lvalue) ----- */
ST_FUNC void store(int r, SValue *v)
{
    int fr = v->r;
    int bt = v->type.t & VT_BTYPE;
    int fc = v->c.i;
    int vi = fr & VT_VALMASK;
    int pl;

    if (vi == VT_LOCAL && (pl = param_local_of(fc)) >= 0) {
        /* store to a param: it IS a local */
        we_op_local(WOP_LOCAL_GET, reg_local(r));
        we_op_setlocal(WOP_LOCAL_SET, pl);
        return;
    }
    if (vi == VT_LOCAL) { push_i32(frame_addr(fc)); }
    else if (vi == VT_LLOCAL) { push_i32(frame_addr(fc)); we_op_local(WOP_LOCAL_GET, reg_local(NB_REGS + 0)); we(WOP_I32_ADD); }
    else if ((fr & VT_SYM) && v->sym) { push_i32(fc); }
    else { we_op_local(WOP_LOCAL_GET, reg_local(vi)); if (fc) { push_i32(fc); we(WOP_I32_ADD); } }

    we_op_local(WOP_LOCAL_GET, reg_local(r));

    switch (bt) {
    case VT_BYTE: case VT_BOOL: we(WOP_I32_STORE8); we_u32(0); we_u32(0); break;
    case VT_SHORT: we(WOP_I32_STORE16); we_u32(1); we_u32(0); break;
    case VT_LLONG: we(WOP_I64_STORE); we_u32(3); we_u32(0); break;
    case VT_FLOAT: we(WOP_F32_STORE); we_u32(2); we_u32(0); break;
    case VT_DOUBLE: case VT_LDOUBLE: we(WOP_F64_STORE); we_u32(3); we_u32(0); break;
    case VT_INT: case VT_PTR:
    default: we(WOP_I32_STORE); we_u32(2); we_u32(0); break;
    }
    /* the store consumed both scratch operands: stack is clean */
}

/* ----- Function prologue / epilogue ----- */

ST_FUNC int gfunc_sret(CType *vt, int variadic, CType *ret, int *ret_align, int *regsize)
{
    (void)vt; (void)variadic; (void)ret;
    *ret_align = 4; *regsize = 4;
    return 0;
}

ST_FUNC void gfunc_prolog(Sym *func_sym)
{
    int ret_i32 = 1;
    int n_params = 0;
    Sym *ps;
    WasmFuncRow *row;
    if (func_row_reserve() != 0) {
        tcc_error("wasm32: out of function table memory");
    }
    row = &func_rows[func_count];
    row->start_offs = (int)(code_ptr - code_buf);
    /* capture the function's source name: the wasm module exports it, which
     * is what the registry-linked build path addresses faces by. */
    {
        const char *nm = get_tok_str(func_sym->v, NULL);
        if (nm != NULL) {
            size_t i;
            for (i = 0; i + 1 < sizeof(row->name) && nm[i] != '\0'; i++) {
                row->name[i] = nm[i];
            }
            row->name[i] = '\0';
        } else {
            row->name[0] = '\0';
        }
    }
    /* signature: every source param is one i32 wasm local (the functype's
     * own params). gfunc_set_param binds each param Sym to its sentinel
     * address; load/store recognize it and use the local directly. */
    if (func_sym != NULL && (func_sym->type.t & VT_FUNC)) {
        ret_i32 = !((func_sym->type.ref->type.t & VT_BTYPE) == VT_VOID);
        for (ps = func_sym->type.ref->next; ps != NULL; ps = ps->next) {
            if (gfunc_set_param(ps, PARAM_ADDR(n_params), 0) != NULL) {
                n_params++;
            }
        }
    }
    row->param_count = n_params;
    row->ret_i32 = ret_i32;
    cur_ret_i32 = ret_i32;
    cur_param_count = n_params;
    we_u32(0); /* body size placeholder */
    int total_locals = NB_REGS + 2;
    we_u32((uint32_t)total_locals);
    for (int i = 0; i < total_locals; i++) { we_u32(1); we(VALTYPE_I32); }
    row->local_decl_count = total_locals;
    /* One block wraps the body and is the single exit label every gjmp's
     * br 0 targets, typed by the fn: (result i32) for value fns. Values
     * live in locals (locals are the registers); a br to the exit reads
     * the return register (TREG_R0 = REG_IRET) onto the stack, and the
     * fallthrough path does the same at the epilog. */
    we(WOP_BLOCK); we(ret_i32 ? VALTYPE_I32 : VALTYPE_NONE);
    label_depth = 1;
    loc = 0; ind = 0; func_vc = 0;
}

ST_FUNC void gfunc_epilog(void)
{
    /* fallthrough: the fn result rides TREG_R0 (gv(RC_RET) set it there);
     * void fns end on the empty stack their void block expects. */
    if (cur_ret_i32) {
        we_op_local(WOP_LOCAL_GET, reg_local(TREG_R0));
    }
    we(WOP_END); label_depth = 0; we(WOP_END);
    {
        WasmFuncRow *row = &func_rows[func_count];
        int body_start   = row->start_offs + 1;
        int body_end     = (int)(code_ptr - code_buf);
        int body_content_len = body_end - body_start;   /* WASM body_size (excludes the size field itself) */
        int full_body_len    = body_end - row->start_offs; /* total bytes to copy (includes size field) */
        uint8_t *patch = code_buf + row->start_offs;
        uint8_t *save  = code_ptr;
        /* the placeholder reserved one byte; a longer LEB rewrites the body
         * from the patch point. No growth can trigger here (patch+5 <=
         * body_end), so `save` stays valid across the we_u32. */
        code_ptr = patch; we_u32((uint32_t)body_content_len); code_ptr = save;
        row->body_len = full_body_len;
    }
    func_count++;
}

/* ----- Function call ----- */

ST_FUNC void gfunc_call(int nb_args)
{
    /* args were gv'd to registers by tccgen (vtop-nb_args..vtop-1); push
     * them in order, call, and park the i32 result in the caller's reg 0
     * (REG_IRET — where gv(RC_RET) for the call's result expects it). */
    int func_idx = (nb_args > 0) ? (vtop - nb_args)->c.i : 0;
    for (int i = nb_args; i > 0; i--) {
        we_op_local(WOP_LOCAL_GET, reg_local((vtop - i)->r & VT_VALMASK));
    }
    we(WOP_CALL); we_u32((uint32_t)func_idx);
    we_op_setlocal(WOP_LOCAL_SET, reg_local(TREG_R0));
    vtop -= nb_args;
}

/* ----- Control flow ----- */

ST_FUNC int gjmp(int t) {
    /* a br to the exit label (depth 0 = the body block) must carry the fn
     * result when the fn returns a value: it rides TREG_R0 like the
     * fallthrough path. Non-exit depths are forward labels this backend
     * does not support (gsym_addr is a stub) — still emit a legal br. */
    if (label_depth == 1 && t == 0 && cur_ret_i32) {
        we_op_local(WOP_LOCAL_GET, reg_local(TREG_R0));
    }
    we(WOP_BR); we_u32((uint32_t)(label_depth - 1 - t)); return t;
}
ST_FUNC void gjmp_addr(int a) { we(WOP_BR); we_u32((uint32_t)a); }

ST_FUNC int gjmp_cond(int op, int t)
{
    /* the condition is already a 0/1 in the vtop register (gen_opi parked
     * it there and vset_VT_CMP kept the register); br_if tests it. */
    (void)op;
    we_op_local(WOP_LOCAL_GET, reg_local(vtop->r & VT_VALMASK));
    we(WOP_BR_IF); we_u32((uint32_t)(label_depth - 1 - t));
    return t;
}

ST_FUNC int gjmp_append(int n, int t) { (void)n; return t; }

/* ----- Integer operations ----- */

ST_FUNC void gen_opi(int op)
{
    int r1, r2;

    /* operands may be constants/lvalues: materialize both into registers
     * first (the x86 backend's contract — gv2 leaves vtop-1/vtop in regs). */
    gv2(RC_INT, RC_INT);
    r1 = vtop[-1].r & VT_VALMASK;
    r2 = vtop[0].r & VT_VALMASK;

    we_op_local(WOP_LOCAL_GET, reg_local(r1));
    we_op_local(WOP_LOCAL_GET, reg_local(r2));
    switch (op) {
    case '+': we(WOP_I32_ADD); break;
    case '-': we(WOP_I32_SUB); break;
    case '*': we(WOP_I32_MUL); break;
    case '/': we(WOP_I32_DIV_S); break;
    case '%': we(WOP_I32_REM_S); break;
    case '&': we(WOP_I32_AND); break;
    case '|': we(WOP_I32_OR); break;
    case '^': we(WOP_I32_XOR); break;
    case TOK_SHL: we(WOP_I32_SHL); break;
    case TOK_SAR: we(WOP_I32_SHR_S); break;
    case TOK_SHR: we(WOP_I32_SHR_U); break;
    case TOK_UDIV: we(WOP_I32_DIV_U); break;
    case TOK_UMOD: we(WOP_I32_REM_U); break;
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
    default:
        /* unknown op: leave the first operand as the result */
        we(WOP_DROP);
        break;
    }
    /* the result parks in r1 (the surviving SValue's register) */
    we_op_setlocal(WOP_LOCAL_SET, reg_local(r1));
    vtop--;
    vtop->r = (vtop->r & ~VT_VALMASK) | r1;
}

/* ----- Float operations ----- */

ST_FUNC void gen_opf(int op)
{
    int r1, r2;
    int size = ((vtop->type.t & VT_BTYPE) == VT_FLOAT) ? 4 : 8;

    gv2(RC_FLOAT, RC_FLOAT);
    r1 = vtop[-1].r & VT_VALMASK;
    r2 = vtop[0].r & VT_VALMASK;
    we_op_local(WOP_LOCAL_GET, reg_local(r1));
    we_op_local(WOP_LOCAL_GET, reg_local(r2));
    if (op == '+') we(size == 4 ? WOP_F32_ADD : WOP_F64_ADD);
    else if (op == '-') we(size == 4 ? WOP_F32_SUB : WOP_F64_SUB);
    else if (op == '*') we(size == 4 ? WOP_F32_MUL : WOP_F64_MUL);
    else if (op == '/') we(size == 4 ? WOP_F32_DIV : WOP_F64_DIV);
    else we(WOP_DROP);
    we_op_setlocal(WOP_LOCAL_SET, reg_local(r1));
    vtop--;
    vtop->r = (vtop->r & ~VT_VALMASK) | r1;
}

/* ----- Type conversions ----- */

ST_FUNC void gen_cvt_itof(int t) { (void)t; we(WOP_F64_CONVERT_I32_S); }
ST_FUNC void gen_cvt_ftoi(int t) { (void)t; we(WOP_I32_TRUNC_F64_S); }
ST_FUNC void gen_cvt_ftof(int t) {
    if ((t & VT_BTYPE) == VT_FLOAT) we(WOP_F64_PROMOTE_F32);
    else we(WOP_F32_DEMOTE_F64);
}
ST_FUNC void gen_cvt_sxtw(void) {}
ST_FUNC void gen_cvt_csti(int t) { (void)t; }

/* ----- Address computation ----- */
ST_FUNC void gen_addr32(int r, Sym *sym, int c) { (void)r; (void)sym; push_i32(c); }
ST_FUNC void gen_addrpc32(int r, Sym *sym, int c) { (void)r; (void)sym; push_i32(c); }

/* ----- Misc ----- */
ST_FUNC void gen_fill_nops(int bytes) { for (int i = 0; i < bytes; i++) we(WOP_NOP); }
ST_FUNC void gen_increment_tcov(SValue *sv) { (void)sv; }
ST_FUNC void ggoto(void) { tcc_error("computed goto not supported on wasm target"); }

/* ----- Varargs / VLA ----- */
ST_FUNC void gen_va_start(void) {}
ST_FUNC void gen_va_arg(CType *t) { (void)t; }
ST_FUNC void gen_clear_cache(void) {}
ST_FUNC void gen_vla_sp_save(int addr) { (void)addr; }
ST_FUNC void gen_vla_sp_restore(int addr) { (void)addr; }
ST_FUNC void gen_vla_alloc(CType *type, int align) { (void)type; (void)align; }
ST_FUNC void gen_vla_result(int addr) { (void)addr; }

/* gsym_addr — resolve forward jumps. Called by tccgen.c. */
ST_FUNC void gsym_addr(int t, int a)
{
    /* Write the forward jump target address at the jump position. */
    (void)t; (void)a;
}

/* ----- WASM module serializer ----- */
/* one-shot: make this one function non-static, then restore */
#undef ST_FUNC

/* append a u32 LEB into p */
static void wput_leb(uint8_t **pp, uint32_t v) { leb_u32(pp, v); }


/* write v as a 2-byte LEB (padded with continuation bits) — sizes are
 * patched in place after the section body is known, so every size slot
 * must occupy a fixed width; a padded LEB is still valid encoding */
static void wput_leb2(uint8_t **pp, uint32_t v) {
    (*pp)[0] = (uint8_t)((v & 0x7f) | 0x80);
    (*pp)[1] = (uint8_t)((v >> 7) & 0x7f);
    *pp += 2;
}

int wasm_build_module(uint8_t **out_buf, int *out_len)
{
    int main_idx = -1;
    int n_exp = 0; /* named function exports (main counts when present) */
    /* Type table: one functype per distinct (n_params, returns_i32). */
    int type_params[64];
    int type_ret[64];
    int n_types = 0;
    /* per-fn type index: heap-backed so a module with more than 256
     * functions cannot blow the stack (func_count itself is growable) */
    int *fn_type = (int *)tcc_malloc((size_t)func_count * sizeof(int));
    if (fn_type == NULL && func_count > 0) {
        return -1;
    }
    for (int i = 0; i < func_count; i++) {
        if (func_rows[i].name[0] == '\0')
            continue;
        if (strcmp(func_rows[i].name, "main") == 0) {
            main_idx = i;
            continue;
        }
        n_exp++;
    }
    /* assign each fn a type index (allocating table entries on demand) */
    for (int i = 0; i < func_count; i++) {
        int k;
        int found = -1;
        for (k = 0; k < n_types; k++) {
            if (type_params[k] == func_rows[i].param_count
                && type_ret[k] == func_rows[i].ret_i32) {
                found = k;
                break;
            }
        }
        if (found < 0 && n_types < 64) {
            type_params[n_types] = func_rows[i].param_count;
            type_ret[n_types] = func_rows[i].ret_i32;
            found = n_types++;
        }
        fn_type[i] = found < 0 ? 0 : found;
    }
    if (main_idx >= 0) {
        n_exp++; /* main rides its true index */
    }
    int exp_name_bytes = 0;
    for (int i = 0; i < func_count; i++) {
        if (func_rows[i].name[0] != '\0')
            exp_name_bytes += (int)strlen(func_rows[i].name);
    }
    /* section sizes are LEB-encoded into a scratch first, then copied, so
     * each section carries an honest size instead of the old 1-byte cap */
    int total = 8 + 7 + (2 + func_count) + (2 + 5)
        + (2 + 1 + n_exp * 2 + exp_name_bytes + 16)
        + (2 + func_count + (int)(code_ptr - code_buf) + 256) + 512;
    *out_buf = (uint8_t *)tcc_malloc((size_t)total);
    if (!*out_buf) {
        tcc_free(fn_type);
        return -1;
    }
    uint8_t *p = *out_buf;

    memcpy(p, "\0asm\x01\0\0\0", 8); p += 8;

    /* Type section (id=1): every distinct functype, params and result all
     * i32 (or empty results for void fns). Scratch-built for honest LEB
     * sizes. */
    {
        uint8_t ts[2048];
        uint8_t *t = ts;
        uint8_t *body;
        size_t used;
        for (int k = 0; k < n_types; k++) {
            *t++ = 0x60;                      /* functype */
            wput_leb(&t, (uint32_t)type_params[k]);
            for (int a = 0; a < type_params[k]; a++) {
                *t++ = VALTYPE_I32;
            }
            if (type_ret[k]) {
                wput_leb(&t, 1);
                *t++ = VALTYPE_I32;
            } else {
                wput_leb(&t, 0);
            }
        }
        used = (size_t)(t - ts);
        *p++ = 1;
        body = p;
        wput_leb2(&p, 0);                     /* fixed 2-byte size slot */
        wput_leb(&p, (uint32_t)n_types);
        memcpy(p, ts, used); p += used;
        wput_leb2(&body, (uint32_t)(p - body - 2));
    }

    /* Function section (id=3): each fn's type index */
    {
        uint8_t *body;
        *p++ = 3;
        body = p;
        wput_leb2(&p, 0);                     /* fixed 2-byte size slot */
        wput_leb(&p, (uint32_t)func_count);
        for (int i = 0; i < func_count; i++) {
            wput_leb(&p, (uint32_t)fn_type[i]);
        }
        wput_leb2(&body, (uint32_t)(p - body - 2));
    }

    /* Memory section (id=5) — 2 pages, no max. wasm_runtime_attach_shared_heap
     * grafts the shared heap onto the module's linear memory, which needs real
     * backing: the working fixture (hello.wasm) declares min=2, and a min=0
     * memory makes attach fail and the loader refuse the module. */
    *p++ = 5; *p++ = 3; *p++ = 1; *p++ = 0x00; *p++ = 0x02;

    /* Export section (id=7): memory + main (at its true index) + every
     * other named function. Nothing is exported that was not defined. */
    {
        uint8_t exp[8192];
        uint8_t *e = exp;
        size_t used;
        int n_entries = 1; /* memory */
        /* memory export */
        *e++ = 6; memcpy(e, "memory", 6); e += 6; *e++ = 0x02; *e++ = 0x00;
        if (main_idx >= 0) {
            *e++ = 4; memcpy(e, "main", 4); e += 4; *e++ = 0x00;
            wput_leb(&e, (uint32_t)main_idx);
            n_entries++;
        }
        for (int i = 0; i < func_count; i++) {
            const char *nm = func_rows[i].name;
            if (nm[0] == '\0' || strcmp(nm, "main") == 0)
                continue;
            {
                size_t nl = strlen(nm);
                if (nl > 120) nl = 120;
                *e++ = (uint8_t)nl;
                memcpy(e, nm, nl); e += nl;
                *e++ = 0x00;             /* kind func */
                wput_leb(&e, (uint32_t)i); /* function index */
                n_entries++;
            }
        }
        used = (size_t)(e - exp);
        {
            /* exact section size: count LEB + entries, measured first */
            uint8_t cnt_scratch[8];
            uint8_t *cs = cnt_scratch;
            size_t cnt_bytes;
            wput_leb(&cs, (uint32_t)n_entries);
            cnt_bytes = (size_t)(cs - cnt_scratch);
            *p++ = 7;
            wput_leb(&p, (uint32_t)(used + cnt_bytes));
            memcpy(p, cnt_scratch, cnt_bytes); p += cnt_bytes;
            memcpy(p, exp, used); p += used;
        }
    }

    /* Code section (id=10) */
    {
        uint8_t *body;
        *p++ = 10;
        body = p;
        wput_leb2(&p, 0);                     /* fixed 2-byte size slot */
        wput_leb(&p, (uint32_t)func_count);
        for (int i = 0; i < func_count; i++) {
            int len = func_rows[i].body_len;
            memcpy(p, code_buf + func_rows[i].start_offs, (size_t)len); p += len;
        }
        wput_leb2(&body, (uint32_t)(p - body - 2));
    }

    *out_len = (int)(p - *out_buf);
    tcc_free(fn_type);
    return 0;
}
#define ST_FUNC static

#endif /* !TARGET_DEFS_ONLY */