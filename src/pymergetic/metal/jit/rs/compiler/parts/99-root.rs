/* ================= C ABI entry points ================= */

/// Tokenize `src`. On success fills `*toklist` (arena-owned, END-terminated)
/// and returns 0; on failure returns -1 with `rsx: …` in errbuf.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_jit_rsx_lex(
    arena: *mut pm_util_mem_arena_t,
    src: *const u8,
    src_len: usize,
    toklist: *mut pm_jit_rsx_toklist_t,
    errbuf: *mut u8,
    errbuf_len: usize,
) -> i32 {
    let mut lx = Lexer {
        arena,
        src,
        src_len,
        pos: 0,
        line: 1,
        toks: Toks::new(arena),
        errbuf,
        errcap: errbuf_len,
        ok: true,
    };
    unsafe { lx.run() };
    if !lx.ok || !lx.toks.ok {
        return -1;
    }
    unsafe {
        (*toklist).toks = lx.toks.p;
        (*toklist).n_toks = lx.toks.n as u32;
    }
    0
}

/// Parse a token list into an AST. Returns 0 and sets `*unit_out`, or -1
/// with `rsx: …` in errbuf.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_jit_rsx_parse(
    arena: *mut pm_util_mem_arena_t,
    toks: *const pm_jit_rsx_toklist_t,
    unit_out: *mut *mut pm_jit_rsx_ast_t,
    errbuf: *mut u8,
    errbuf_len: usize,
) -> i32 {
    let n_toks = if toks.is_null() { 0 } else { unsafe { (*toks).n_toks } };
    if n_toks == 0 {
        unsafe {
            err_set(errbuf, errbuf_len, b"empty token list\0".as_ptr(), 0);
        }
        return -1;
    }
    let mut p = Parser {
        arena,
        toks: if toks.is_null() { core::ptr::null() } else { unsafe { (*toks).toks } },
        n_toks,
        at: 0,
        nd: Node {
            arena,
            errbuf,
            errcap: errbuf_len,
            errline: 0,
            ok: true,
        },
        ok: true,
        cond_ctx: false,
        chain_ctx: false,
        shr_closes: 0,
        feats: core::ptr::null(),
    };
    let file = unsafe { p.parse_file() };
    if !p.ok || !p.nd.ok || file.is_null() {
        return -1;
    }
    unsafe {
        *unit_out = file;
    }
    0
}

/// Render an AST as indented text into the caller's buffer.
/// Returns bytes written, or -1 when out_cap is too short.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_jit_rsx_ast_dump(
    ast: *const pm_jit_rsx_ast_t,
    out: *mut u8,
    out_cap: usize,
    errbuf: *mut u8,
    errbuf_len: usize,
) -> i32 {
    if out.is_null() || out_cap == 0 {
        unsafe {
            err_set(errbuf, errbuf_len, b"no dump buffer\0".as_ptr(), 0);
        }
        return -1;
    }
    let at = unsafe { dump_node(out, out_cap, 0, ast, 0) };
    if at + 1 >= out_cap {
        unsafe {
            err_set(errbuf, errbuf_len, b"dump buffer too small\0".as_ptr(), 0);
        }
        return -1;
    }
    unsafe {
        *out.add(at) = 0;
    }
    at as i32
}

/// Lower a parsed AST to C text. Returns 0 and fills `*c_out`/`*c_out_len`
/// (NUL-terminated, arena-owned), or -1 with `rsx: …` in errbuf.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_jit_rsx_lower(
    arena: *mut pm_util_mem_arena_t,
    unit: *const pm_jit_rsx_ast_t,
    c_out: *mut *mut u8,
    c_out_len: *mut usize,
    errbuf: *mut u8,
    errbuf_len: usize,
) -> i32 {
    if unit.is_null() {
        unsafe {
            err_set(errbuf, errbuf_len, b"null AST\0".as_ptr(), 0);
        }
        return -1;
    }
    /* Lower is ~110 KiB of resident tables — it is arena-resident, built
     * through this pointer: no by-value temporary ever lands on the native
     * stack. The block is zeroed first (tlsf does not zero), then the
     * non-zero fields are set; NULL = the arena refused, and that refusal
     * is the error. */
    let lw = unsafe { Lower::new(arena, errbuf, errbuf_len) };
    if lw.is_null() {
        unsafe {
            err_set(errbuf, errbuf_len, b"arena too small for the lowering tables\0".as_ptr(), 0);
        }
        return -1;
    }
    /* SymTab is arena-backed — a span smaller than its block hands back
     * NULL and every table probe would crash. Refuse instead. Same for
     * FnTab: a NULL table means every call-site lookup would crash. */
    if unsafe { (*lw).syms.is_null() } {
        unsafe {
            err_set(errbuf, errbuf_len, b"arena too small for the symbol table\0".as_ptr(), 0);
        }
        return -1;
    }
    if unsafe { (*lw).fns.is_null() } {
        unsafe {
            err_set(errbuf, errbuf_len, b"arena too small for the fn table\0".as_ptr(), 0);
        }
        return -1;
    }
    let good = unsafe { (*lw).lower_file(unit) };
    /* FnTab span OOM: refuse with the specific error — compiling on with
     * unknown return/param types would emit untyped call sites. */
    if unsafe { (*(*lw).fns).oom } {
        unsafe {
            err_set(errbuf, errbuf_len, b"arena exhausted while lowering\0".as_ptr(), 0);
        }
        return -1;
    }
    if !good || unsafe { !(*lw).ok } || unsafe { !(*lw).out.ok } {
        /* aborted without a specific message (arena exhausted mid-render):
         * leave the caller a reason instead of an empty errbuf */
        unsafe {
            if !(*lw).errbuf.is_null() && (*lw).errcap > 0 && *(*lw).errbuf == 0 {
                err_set((*lw).errbuf, (*lw).errcap, b"arena exhausted while lowering\0".as_ptr(), 0);
            }
        }
        return -1;
    }
    unsafe {
        (*lw).out.putc(0);
        *c_out = (*lw).out.p;
        *c_out_len = (*lw).out.len - 1;
    }
    0
}

/// One-shot: Rust source bytes to generated C (lex + parse + lower).
/// The prove path — also the face the build card's Rust unit compiles with.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_jit_rsx_compile(
    arena: *mut pm_util_mem_arena_t,
    source: *const u8,
    source_len: usize,
    c_out: *mut *mut u8,
    c_out_len: *mut usize,
    errbuf: *mut u8,
    errbuf_len: usize,
) -> i32 {
    let mut toks = pm_jit_rsx_toklist_t {
        toks: core::ptr::null_mut(),
        n_toks: 0,
    };
    let mut unit: *mut pm_jit_rsx_ast_t = core::ptr::null_mut();
    unsafe {
        let r = pm_metal_jit_rsx_lex(arena, source, source_len, &mut toks, errbuf, errbuf_len);
        if r != 0 {
            return r;
        }
        let r = pm_metal_jit_rsx_parse(arena, &toks, &mut unit, errbuf, errbuf_len);
        if r != 0 {
            return r;
        }
        pm_metal_jit_rsx_lower(arena, unit, c_out, c_out_len, errbuf, errbuf_len)
    }
}

/// Number of token kinds in the `__types__.h` X-macro table.
#[unsafe(no_mangle)]
pub extern "C" fn pm_metal_jit_rsx_token_kind_count() -> u32 {
    TOK_KIND_COUNT
}

/// Number of AST kinds in the `__types__.h` X-macro table.
#[unsafe(no_mangle)]
pub extern "C" fn pm_metal_jit_rsx_ast_kind_count() -> u32 {
    AST_KIND_COUNT
}

/* ---- registration (the registry table is built by the real toolchain) ---- */

pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.jit.rs.compiler",
    pm_metal_jit_rsx_lex,
    "int32_t(pm_util_mem_arena_t *, const char *, size_t, pm_jit_rsx_toklist_t *, char *, size_t)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.jit.rs.compiler",
    pm_metal_jit_rsx_parse,
    "int32_t(pm_util_mem_arena_t *, const pm_jit_rsx_toklist_t *, pm_jit_rsx_ast_t **, char *, size_t)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.jit.rs.compiler",
    pm_metal_jit_rsx_ast_dump,
    "int32_t(const pm_jit_rsx_ast_t *, char *, size_t, char *, size_t)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.jit.rs.compiler",
    pm_metal_jit_rsx_lower,
    "int32_t(pm_util_mem_arena_t *, const pm_jit_rsx_ast_t *, char **, size_t *, char *, size_t)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.jit.rs.compiler",
    pm_metal_jit_rsx_compile,
    "int32_t(pm_util_mem_arena_t *, const char *, size_t, char **, size_t *, char *, size_t)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.jit.rs.compiler",
    pm_metal_jit_rsx_token_kind_count,
    "uint32_t(void)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.jit.rs.compiler",
    pm_metal_jit_rsx_ast_kind_count,
    "uint32_t(void)"
);
