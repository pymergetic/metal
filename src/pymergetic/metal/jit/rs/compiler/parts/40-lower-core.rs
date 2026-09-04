/* One lowering context. */
struct Lower {
    arena: *mut pm_util_mem_arena_t,
    out: Out,
    errbuf: *mut u8,
    errcap: usize,
    errline: u32,
    ok: bool,
    syms: *mut SymTab,
    /* arena-resident (an ~84 KiB by-value field would park the whole table
     * on the native stack whenever a Lower temp existed — same discipline
     * as syms above it) */
    fns: *mut FnTab,
    consts: ConstTab,
    enums: EnumTab,
    depth: usize,
    /* the struct type currently being lowered (for method resolution) */
    cur_impl: [*const u8; 16],
    cur_impl_lens: [usize; 16],
    cur_impl_n: usize,
    /* method receiver: C type text of `self` while lowering a method body;
     * empty when lowering a free fn. */
    recv_type: [u8; 64],
    recv_len: usize,
    /* C type text of the fn currently being lowered's return — `?` lowers
     * to an early `return 0` (Option-of-pointer: None == 0), which is only
     * sound when the fn itself returns a pointer. Empty outside a fn body. */
    cur_ret: [u8; 128],
    cur_ret_len: usize,
    /* last-resort scratch when the arena is exhausted: every arena_tmp
     * failure also sets ok=false, so lowering aborts before the reused
     * bytes can matter — this only keeps the NULL deref off the OOM path. */
    oom_buf: [u8; 160],
    /* refusals seen so far this pass — batching several into errbuf saves
     * the developer a rebuild per error; ok=false still stops the cascade
     * of follow-on diagnostics from a single fault. */
    nerrs: u32,
    /* Transparent newtypes: single-field tuple structs with one generic
     * param (`Mut<T>(UnsafeCell<T>)`). Their C type IS the inner type —
     * the name never appears in C, constructors unwrap, `.0`/`.get()`
     * are identity. Registered by lower_struct during the struct pass. */
    nt_names: [[u8; 48]; NT_CAP],
    nt_lens: [usize; NT_CAP],
    nt_n: usize,
    /* While lowering a match whose scrutinee is a struct-shaped Option
     * (integer payload): the payload type text, len 0 = not struct-shaped.
     * emit_pat_test and the Some-bind declaration read it. */
    cur_opt_elem: [u8; 96],
    cur_opt_elem_len: usize,
    /* Struct-shaped Option payload spellings seen this unit (each renders
     * as the named typedef rsx_opt_<elem>, emitted once in the preamble —
     * an inline `struct { T _v; bool _has; }` at each use site would be a
     * fresh anonymous type per site, incompatible across declarations). */
    opt_elems: [[u8; 64]; OPT_CAP],
    opt_lens: [usize; OPT_CAP],
    opt_done: [bool; OPT_CAP],
    opt_n: usize,
    /* True once the struct/alias passes have run: every Option payload
     * that names a unit type is then either already flushed (matched the
     * naming type in opt_emit_for) or safe to flush (the naming type's
     * typedef is emitted). Before this point a pending Option may name a
     * type the type passes have not emitted yet, so flushing from
     * lower_static (pass 0a consts run first) would emit it ahead of its
     * own payload typedef — a C parse error. */
    types_done: bool,
    /* Tuple signatures seen this unit (each renders as the named struct
     * rsx_tuple_<elem0>_<elem1>.., fields _0.._n, emitted once in the
     * preamble — same one-C-type-per-signature rule as rsx_opt_<elem>).
     * Flattened like SymTab's field table: signature s, element f lives
     * at tup_elems[s * TUP_MAXF + f][0..tup_lens[s * TUP_MAXF + f]],
     * for f in 0..tup_counts[s]. */
    tup_elems: [[u8; 64]; TUP_CAP * TUP_MAXF],
    tup_lens: [usize; TUP_CAP * TUP_MAXF],
    tup_counts: [usize; TUP_CAP],
    tup_done: [bool; TUP_CAP],
    tup_n: usize,
    /* per-unit counter for tuple-destructure temp names (two tuple lets
     * in one C scope would redeclare `__rsx_tup` — each gets its own). */
    tup_tmp_n: usize,
    /* per-unit counter for atomic-op temp names (same redeclaration
     * concern as tup_tmp_n, for `__rsx_at<N>` inside one C scope). */
    atom_tmp_n: usize,
    /* opaque extern type names seen in signatures/statics (`pm_util_lock_t`):
     * the generated C is self-contained, so each gets a hoisted
     * `typedef struct X X;` — the real definition lives in the linked lib. */
    opq_names: [[u8; 48]; 24],
    opq_lens: [usize; 24],
    opq_n: usize,
    /* names already hoisted by an opq_emit flush — the emit is incremental */
    opq_flushed: usize,
    /* Top-level static/const types, filled by the declare-only pass before
     * any fn body lowers (file order is not relied on). expr_ctype consults
     * it for bare PATH names after locals — without it, a static's use
     * sites cannot infer (SymTab is a struct/field table, not variables). */
    st_names: [[u8; 64]; ST_CAP],
    st_name_lens: [usize; ST_CAP],
    st_cts: [[u8; 128]; ST_CAP],
    st_ct_lens: [usize; ST_CAP],
    st_n: usize,
    /* Types already emitted by the dependency-ordered struct pass (pass A).
     * A name here must not re-emit — the C typedef would redefine. Also
     * the recursion guard: a cyclic pair (A names B, B names A by value)
     * cannot be ordered and is refused, not looped on. */
    tydone_names: [[u8; 48]; TYD_CAP],
    tydone_lens: [usize; TYD_CAP],
    tydone_n: usize,
    /* recursion depth of emit_struct_ordered — the cycle refusal needs to
     * know the walk is still inside, not a fresh top-level call */
    tyorder_depth: usize,
}

impl Lower {
    /* Arena-resident construction: the whole struct (its nested FnTab,
     * ConstTab, EnumTab and every fixed array) lives in one arena block,
     * zeroed then armed. No by-value Lower temporary can exist anywhere. */
    unsafe fn new(
        arena: *mut pm_util_mem_arena_t,
        errbuf: *mut u8,
        errcap: usize,
    ) -> *mut Lower {
        let p = unsafe { pm_util_mem_alloc(arena, core::mem::size_of::<Lower>()) } as *mut Lower;
        if p.is_null() {
            return p;
        }
        unsafe {
            let zb = p as *mut u8;
            let n = core::mem::size_of::<Lower>();
            let mut i = 0usize;
            while i < n {
                *zb.add(i) = 0;
                i += 1;
            }
            (*p).arena = arena;
            (*p).out = Out::new(arena);
            (*p).errbuf = errbuf;
            (*p).errcap = errcap;
            (*p).ok = true;
            (*p).syms = SymTab::new(arena);
            (*p).fns = FnTab::new(arena);
            (*p).consts = ConstTab::new();
            (*p).enums = EnumTab::new();
        }
        p
    }

    unsafe fn err(&mut self, msg: *const u8, line: u32) {
        unsafe {
            if self.ok && self.nerrs == 0 {
                err_set(self.errbuf, self.errcap, msg, line);
            } else if self.nerrs < 8 && !self.errbuf.is_null() && self.errcap > 2 {
                /* batch: append "; msg" so one pass reports several gaps
                 * (a truncated tail still reads fine — errbuf bounds hold).
                 * Runs on !ok too: the pass loops re-arm ok between items. */
                let mut i = 0usize;
                while i + 1 < self.errcap && unsafe { *self.errbuf.add(i) } != 0 {
                    i += 1;
                }
                i = unsafe { zput(self.errbuf, self.errcap, i, b"; \0".as_ptr()) };
                i = unsafe { zput(self.errbuf, self.errcap, i, msg) };
                if line != 0 {
                    i = unsafe { zput(self.errbuf, self.errcap, i, b" at line \0".as_ptr()) };
                    i += unsafe { zput_num(self.errbuf.add(i), self.errcap - i, line) };
                }
                unsafe {
                    *self.errbuf.add(i) = 0;
                }
            }
        }
        self.nerrs += 1;
        self.ok = false;
    }

    /* err with the offending binding's name appended — a `let x = ...` that
     * cannot be typed should say which binding. */
    unsafe fn err_let_name(&mut self, msg: *const u8, line: u32, name: *const u8, name_len: usize) {
        unsafe {
            self.err_let_name2(msg, line, name, name_len, b"'\0".as_ptr());
        }
    }

    /* err_let_name plus the failing initializer's AST kind in brackets. */
    unsafe fn err_let_name2(&mut self, msg: *const u8, line: u32, name: *const u8, name_len: usize, kind: *const u8) {
        unsafe {
            if self.ok && self.nerrs == 0 {
                err_set(self.errbuf, self.errcap, msg, line);
            } else if self.nerrs < 8 && !self.errbuf.is_null() && self.errcap > 2 {
                /* batch: same append contract as err() — see there */
                let mut i = 0usize;
                while i + 1 < self.errcap && unsafe { *self.errbuf.add(i) } != 0 {
                    i += 1;
                }
                i = unsafe { zput(self.errbuf, self.errcap, i, b"; \0".as_ptr()) };
                i = unsafe { zput(self.errbuf, self.errcap, i, msg) };
                if line != 0 {
                    i = unsafe { zput(self.errbuf, self.errcap, i, b" at line \0".as_ptr()) };
                    i += unsafe { zput_num(self.errbuf.add(i), self.errcap - i, line) };
                }
                if i < self.errcap {
                    unsafe { *self.errbuf.add(i) = 0 };
                }
            }
                if self.errcap > 2 {
                    let mut i = 0usize;
                    while i < self.errcap && unsafe { *self.errbuf.add(i) } != 0 {
                        i += 1;
                    }
                    let pre = b" for '\0";
                    let pp = pre.as_ptr();
                    let mut j = 0usize;
                    while unsafe { *pp.add(j) } != 0 && i + 1 < self.errcap {
                        unsafe { *self.errbuf.add(i) = *pp.add(j) };
                        i += 1;
                        j += 1;
                    }
                    j = 0;
                    while j < name_len && i + 1 < self.errcap {
                        unsafe { *self.errbuf.add(i) = *name.add(j) };
                        i += 1;
                        j += 1;
                    }
                    unsafe { *self.errbuf.add(i) = b'\'' };
                    i += 1;
                    if i + 2 < self.errcap {
                        unsafe { *self.errbuf.add(i) = b' ' };
                        i += 1;
                        unsafe { *self.errbuf.add(i) = b'[' };
                        i += 1;
                        j = 0;
                        while unsafe { *kind.add(j) } != 0 && i + 2 < self.errcap {
                            unsafe { *self.errbuf.add(i) = *kind.add(j) };
                            i += 1;
                            j += 1;
                        }
                        unsafe { *self.errbuf.add(i) = b']' };
                        i += 1;
                    }
                    if i < self.errcap {
                        unsafe { *self.errbuf.add(i) = 0 };
                    }
                }
            }
            self.nerrs += 1;
            self.ok = false;
    }

    /* append ` on <a>.<b>` to the current error message (diagnostics) */
    unsafe fn err_parts(&mut self, a: *const u8, alen: usize, b: *const u8, blen: usize) {
        if self.errcap > 4 {
            unsafe {
                let mut i = 0usize;
                while i < self.errcap && unsafe { *self.errbuf.add(i) } != 0 {
                    i += 1;
                }
                if i + 3 < self.errcap {
                    unsafe { *self.errbuf.add(i) = b' ' };
                    i += 1;
                    unsafe { *self.errbuf.add(i) = b'o' };
                    i += 1;
                    unsafe { *self.errbuf.add(i) = b'n' };
                    i += 1;
                    unsafe { *self.errbuf.add(i) = b' ' };
                    i += 1;
                }
                let mut j = 0usize;
                while j < alen && i + 1 < self.errcap {
                    unsafe { *self.errbuf.add(i) = *a.add(j) };
                    i += 1;
                    j += 1;
                }
                if i + 1 < self.errcap {
                    unsafe { *self.errbuf.add(i) = b'.' };
                    i += 1;
                }
                j = 0;
                while j < blen && i + 1 < self.errcap {
                    unsafe { *self.errbuf.add(i) = *b.add(j) };
                    i += 1;
                    j += 1;
                }
                if i < self.errcap {
                    unsafe { *self.errbuf.add(i) = 0 };
                }
            }
        }
    }

    unsafe fn oom(&mut self, line: u32) {
        unsafe {
            self.err(b"arena exhausted\0".as_ptr(), line);
        }
    }

    unsafe fn indent(&mut self) {
        let mut i = 0usize;
        while i < self.depth {
            unsafe {
                self.out.puts(b"    \0".as_ptr());
            }
            i += 1;
        }
    }

    /* Split a rendered C type at its trailing array dims: returns the index
     * where the `[...]` groups start (the base type ends there), or n when
     * the render has none. Pointer-to-array declarators need this:
     * `T [N] *` is not C, `T (*)[N]` is. */
    unsafe fn arr_dims_split(&mut self, ct: *const u8, n: usize) -> usize {
        let mut e = n;
        loop {
            let mut s = e;
            while s > 0 && unsafe { *ct.add(s - 1) } == b' ' {
                s -= 1;
            }
            if s == 0 || unsafe { *ct.add(s - 1) } != b']' {
                break;
            }
            let mut d = s - 1;
            while d > 0 && unsafe { *ct.add(d) } != b'[' {
                d -= 1;
            }
            if unsafe { *ct.add(d) } != b'[' {
                break;
            }
            e = d;
        }
        if e == n {
            return n;
        }
        /* base may end with spaces before the first dim group */
        let mut be = e;
        while be > 0 && unsafe { *ct.add(be - 1) } == b' ' {
            be -= 1;
        }
        be
    }

    /* Locate a pointer-to-array declarator ` (*)` in a rendered C type —
     * the byte index of its '(' or usize::MAX when the type is not a
     * pointer-to-array. `T (*)[N]` is how `*const [T; N]` / `&[T; N]`
     * render; the strip sites below need its position to take apart. */
    unsafe fn parr_declarator(&mut self, ct: *const u8, n: usize) -> usize {
        if n < 3 {
            return usize::MAX;
        }
        let mut i = 0usize;
        while i + 3 <= n {
            if unsafe { *ct.add(i) } == b'('
                && unsafe { *ct.add(i + 1) } == b'*'
                && unsafe { *ct.add(i + 2) } == b')'
            {
                return i;
            }
            i += 1;
        }
        usize::MAX
    }

    /* Rust type node -> C type into out (NUL-terminated); byte length or 0 on
     * refusal (err already set). */
    unsafe fn ctype(&mut self, ty: *const pm_jit_rsx_ast_t, out: *mut u8, cap: usize) -> usize {
        let mut at = 0usize;
        if ty.is_null() {
            unsafe {
                self.err(b"missing type\0".as_ptr(), 0);
            }
            return 0;
        }
        let kind = unsafe { (*ty).kind };
        let text = unsafe { (*ty).text };
        let text_len = unsafe { (*ty).text_len };
        if kind == pm_jit_rsx_ast_kind::TYPE {
            /* leaf TYPE nodes carry the primitive/spelling in text, or a
             * wrapper ("*" / "&" / "&mut" / "[]" / "[;]" / "fnptr" / "path")
             * with kids. */
            if unsafe { z_eq(text, text_len, b"u8\0".as_ptr()) } {
                at = unsafe { zput(out, cap, at, b"uint8_t\0".as_ptr()) };
                return at;
            }
            if unsafe { z_eq(text, text_len, b"u16\0".as_ptr()) } {
                at = unsafe { zput(out, cap, at, b"uint16_t\0".as_ptr()) };
                return at;
            }
            if unsafe { z_eq(text, text_len, b"u32\0".as_ptr()) } {
                at = unsafe { zput(out, cap, at, b"uint32_t\0".as_ptr()) };
                return at;
            }
            /* AtomicU32 -> _Atomic uint32_t: the field is a C11 atomic;
             * loads/stores/swaps go through the __atomic_* builtins
             * (see emit_method_call), never plain access. */
            if unsafe { z_eq(text, text_len, b"AtomicU32\0".as_ptr()) } {
                at = unsafe { zput(out, cap, at, b"_Atomic uint32_t\0".as_ptr()) };
                return at;
            }
            if unsafe { z_eq(text, text_len, b"u64\0".as_ptr()) } {
                at = unsafe { zput(out, cap, at, b"uint64_t\0".as_ptr()) };
                return at;
            }
            if unsafe { z_eq(text, text_len, b"i8\0".as_ptr()) } {
                at = unsafe { zput(out, cap, at, b"int8_t\0".as_ptr()) };
                return at;
            }
            if unsafe { z_eq(text, text_len, b"i16\0".as_ptr()) } {
                at = unsafe { zput(out, cap, at, b"int16_t\0".as_ptr()) };
                return at;
            }
            if unsafe { z_eq(text, text_len, b"i32\0".as_ptr()) } {
                at = unsafe { zput(out, cap, at, b"int32_t\0".as_ptr()) };
                return at;
            }
            if unsafe { z_eq(text, text_len, b"i64\0".as_ptr()) } {
                at = unsafe { zput(out, cap, at, b"int64_t\0".as_ptr()) };
                return at;
            }
            if unsafe { z_eq(text, text_len, b"usize\0".as_ptr()) } {
                at = unsafe { zput(out, cap, at, b"size_t\0".as_ptr()) };
                return at;
            }
            if unsafe { z_eq(text, text_len, b"isize\0".as_ptr()) } {
                at = unsafe { zput(out, cap, at, b"intptr_t\0".as_ptr()) };
                return at;
            }
            if unsafe { z_eq(text, text_len, b"f32\0".as_ptr()) } {
                at = unsafe { zput(out, cap, at, b"float\0".as_ptr()) };
                return at;
            }
            if unsafe { z_eq(text, text_len, b"f64\0".as_ptr()) } {
                at = unsafe { zput(out, cap, at, b"double\0".as_ptr()) };
                return at;
            }
            if unsafe { z_eq(text, text_len, b"bool\0".as_ptr()) } {
                at = unsafe { zput(out, cap, at, b"bool\0".as_ptr()) };
                return at;
            }
            if unsafe { z_eq(text, text_len, b"char\0".as_ptr()) } {
                at = unsafe { zput(out, cap, at, b"uint32_t\0".as_ptr()) };
                return at;
            }
            if unsafe { z_eq(text, text_len, b"()\0".as_ptr()) }
                || unsafe { z_eq(text, text_len, b"void\0".as_ptr()) }
            {
                at = unsafe { zput(out, cap, at, b"void\0".as_ptr()) };
                return at;
            }
            if unsafe { z_eq(text, text_len, b"str\0".as_ptr()) } {
                at = unsafe { zput(out, cap, at, b"char\0".as_ptr()) };
                return at;
            }
            /* tuple type: kids are the element TYPEs. Registers the
             * signature (idempotent) and renders the one shared typedef
             * name — emitted once in the preamble as
             * `struct { A _0; B _1; } rsx_tuple_A_B;`. */
            if text_len == 5 && unsafe { z_eq(text, text_len, b"tuple\0".as_ptr()) } {
                let kids = unsafe { (*ty).kids };
                let nk = unsafe { (*ty).n_kids } as usize;
                if nk == 0 {
                    return 0;
                }
                let mut el_bufs: [[u8; 64]; TUP_MAXF] = [[0; 64]; TUP_MAXF];
                let mut el_lens: [usize; TUP_MAXF] = [0; TUP_MAXF];
                if nk > TUP_MAXF {
                    unsafe {
                        self.err(b"unsupported: tuple with more than 4 elements\0".as_ptr(), unsafe { (*ty).line });
                    }
                    return 0;
                }
                let mut f = 0usize;
                while f < nk {
                    let e = unsafe { *kids.add(f) };
                    let n = unsafe { self.ctype(e, el_bufs[f].as_mut_ptr(), 64) };
                    if n == 0 || n >= 64 {
                        return 0;
                    }
                    el_lens[f] = n;
                    f += 1;
                }
                let slot = unsafe { self.tup_add(el_bufs.as_ptr(), el_lens.as_ptr(), nk) };
                if slot >= TUP_CAP {
                    unsafe {
                        self.err(b"internal: too many tuple types\0".as_ptr(), unsafe { (*ty).line });
                    }
                    return 0;
                }
                let base = self.tup_elems.as_ptr().add(slot * TUP_MAXF);
                let blens = self.tup_lens.as_ptr().add(slot * TUP_MAXF);
                let need = unsafe { Lower::tup_name_need(blens, nk) };
                let tdn = if need == 0 {
                    core::ptr::null_mut()
                } else {
                    unsafe { self.name_tmp(need) }
                };
                if tdn.is_null() {
                    unsafe {
                        self.err(b"internal: tuple typedef name too long\0".as_ptr(), unsafe { (*ty).line });
                    }
                    return 0;
                }
                let tdn_len = unsafe { Lower::tup_typedef_name(base, blens, nk, tdn, need) };
                if tdn_len == 0 {
                    unsafe {
                        self.err(b"internal: tuple typedef name too long\0".as_ptr(), unsafe { (*ty).line });
                    }
                    return 0;
                }
                at = unsafe { bput(out, cap, at, tdn, tdn_len) };
                unsafe {
                    if at < cap {
                        *out.add(at) = 0;
                    } else if cap > 0 {
                        *out.add(cap - 1) = 0;
                    }
                }
                return if at >= cap { 0 } else { at };
            }
            /* wrapper forms */
            if unsafe { z_eq(text, text_len, b"*\0".as_ptr()) } {
                /* kids: const|mut, inner */
                let kids = unsafe { (*ty).kids };
                let nk = unsafe { (*ty).n_kids } as usize;
                if nk >= 2 {
                    let q = unsafe { *kids.add(0) };
                    let inner = unsafe { *kids.add(1) };
                    let qk = unsafe { (*q).kind };
                    let _ = qk;
                    let qt = unsafe { (*q).text };
                    let qtl = unsafe { (*q).text_len };
                    let inner_buf = self.arena_tmp();
                    let n = unsafe { self.ctype(inner, inner_buf, 128) };
                    if n == 0 {
                        return 0;
                    }
                    if unsafe { z_eq(qt, qtl, b"const\0".as_ptr()) } {
                        at = unsafe { zput(out, cap, at, b"const \0".as_ptr()) };
                    }
                    /* pointer-to-array (`*mut [T; N]`): C wants the
                     * declarator form `T (*)[N]`, not `T [N] *` — the
                     * trailing dims come from the inner array render */
                    let dsplit = unsafe { self.arr_dims_split(inner_buf, n) };
                    if dsplit < n {
                        let mut j = 0usize;
                        while j < dsplit {
                            unsafe {
                                *out.add(at) = *inner_buf.add(j);
                            }
                            at += 1;
                            j += 1;
                        }
                        at = unsafe { zput(out, cap, at, b" (*)\0".as_ptr()) };
                        let mut j2 = dsplit;
                        while j2 < n {
                            unsafe {
                                *out.add(at) = *inner_buf.add(j2);
                            }
                            at += 1;
                            j2 += 1;
                        }
                    } else {
                        at = unsafe { zput(out, cap, at, inner_buf) };
                        at = unsafe { zput(out, cap, at, b" *\0".as_ptr()) };
                    }
                    unsafe {
                        *out.add(at) = 0;
                    }
                    return at;
                }
                unsafe {
                    self.err(b"bad pointer type\0".as_ptr(), unsafe { (*ty).line });
                }
                return 0;
            }
            if unsafe { z_eq(text, text_len, b"&\0".as_ptr()) }
                || unsafe { z_eq(text, text_len, b"&mut\0".as_ptr()) }
            {
                let kids = unsafe { (*ty).kids };
                let nk = unsafe { (*ty).n_kids } as usize;
                if nk >= 1 {
                    let inner = unsafe { *kids.add(0) };
                    let inner_buf = self.arena_tmp();
                    let n = unsafe { self.ctype(inner, inner_buf, 128) };
                    if n == 0 {
                        return 0;
                    }
                    /* &str -> const char* */
                    if unsafe { z_eq(inner_buf, n, b"char\0".as_ptr()) } {
                        at = unsafe { zput(out, cap, at, b"const char *\0".as_ptr()) };
                        unsafe {
                            *out.add(at) = 0;
                        }
                        return at;
                    }
                    at = unsafe { zput(out, cap, at, b"const \0".as_ptr()) };
                    if unsafe { z_eq(text, text_len, b"&mut\0".as_ptr()) } {
                        /* drop the "const " we just wrote for &mut */
                        at = 0;
                    }
                    /* reference-to-array (`&mut [T; N]`): `T (*)[N]` */
                    let dsplit = unsafe { self.arr_dims_split(inner_buf, n) };
                    if dsplit < n {
                        let mut j = 0usize;
                        while j < dsplit {
                            unsafe {
                                *out.add(at) = *inner_buf.add(j);
                            }
                            at += 1;
                            j += 1;
                        }
                        at = unsafe { zput(out, cap, at, b" (*)\0".as_ptr()) };
                        let mut j2 = dsplit;
                        while j2 < n {
                            unsafe {
                                *out.add(at) = *inner_buf.add(j2);
                            }
                            at += 1;
                            j2 += 1;
                        }
                    } else {
                        at = unsafe { zput(out, cap, at, inner_buf) };
                        at = unsafe { zput(out, cap, at, b" *\0".as_ptr()) };
                    }
                    unsafe {
                        *out.add(at) = 0;
                    }
                    return at;
                }
                unsafe {
                    self.err(b"bad reference type\0".as_ptr(), unsafe { (*ty).line });
                }
                return 0;
            }
            if unsafe { z_eq(text, text_len, b"[]\0".as_ptr()) }
                || unsafe { z_eq(text, text_len, b"[;]\0".as_ptr()) }
            {
                /* [T] / [T; N] -> C array on the inner type */
                let kids = unsafe { (*ty).kids };
                let nk = unsafe { (*ty).n_kids } as usize;
                if nk >= 1 {
                    let inner = unsafe { *kids.add(0) };
                    let inner_buf = self.arena_tmp();
                    let n = unsafe { self.ctype(inner, inner_buf, 128) };
                    if n == 0 {
                        return 0;
                    }
                    at = unsafe { zput(out, cap, at, inner_buf) };
                    if unsafe { z_eq(text, text_len, b"[;]\0".as_ptr()) } && nk >= 2 {
                        let size = unsafe { *kids.add(1) };
                        at = unsafe { zput(out, cap, at, b" \0".as_ptr()) };
                        at = unsafe { zput(out, cap, at, b"[\0".as_ptr()) };
                        at = unsafe { zput(out, cap, at, unsafe { (*size).text }) };
                        at = unsafe { zput(out, cap, at, b"]\0".as_ptr()) };
                    }
                    unsafe {
                        *out.add(at) = 0;
                    }
                    return at;
                }
                unsafe {
                    self.err(b"bad array type\0".as_ptr(), unsafe { (*ty).line });
                }
                return 0;
            }
            if unsafe { z_eq(text, text_len, b"fnptr\0".as_ptr()) } {
                /* kids: quals.., param types..., ret (last) */
                let kids = unsafe { (*ty).kids };
                let nk = unsafe { (*ty).n_kids } as usize;
                let ret = if nk > 0 { unsafe { *kids.add(nk - 1) } } else { core::ptr::null_mut() };
                let ret_buf = self.arena_tmp();
                let ret_len = if !ret.is_null() {
                    unsafe { self.ctype(ret, ret_buf, 128) }
                } else {
                    unsafe { zput(ret_buf, 128, 0, b"void\0".as_ptr()) }
                };
                if ret_len == 0 {
                    return 0;
                }
                /* C fn-pointer: `RET (*)(params)` */
                at = unsafe { zput(out, cap, at, ret_buf) };
                at = unsafe { zput(out, cap, at, b" (*)(\0".as_ptr()) };
                let mut i = 0usize;
                let mut first = true;
                while i + 1 < nk {
                    let pty = unsafe { *kids.add(i) };
                    if unsafe { (*pty).kind } == pm_jit_rsx_ast_kind::TYPE {
                        let pt = unsafe { (*pty).text };
                        let ptl = unsafe { (*pty).text_len };
                        if unsafe { z_eq(pt, ptl, b"unsafe\0".as_ptr()) }
                            || unsafe { z_eq(pt, ptl, b"extern\0".as_ptr()) }
                        {
                            /* qualifier — skip */
                            i += 1;
                            continue;
                        }
                        if ptl > 0 && unsafe { *pt } == b'"' {
                            /* ABI string ("C") — skip */
                            i += 1;
                            continue;
                        }
                    }
                    if !first {
                        at = unsafe { zput(out, cap, at, b", \0".as_ptr()) };
                    }
                    let p_buf = self.arena_tmp();
                    let pn = unsafe { self.ctype(pty, p_buf, 128) };
                    if pn == 0 {
                        return 0;
                    }
                    at = unsafe { zput(out, cap, at, p_buf) };
                    first = false;
                    i += 1;
                }
                if first {
                    at = unsafe { zput(out, cap, at, b"void\0".as_ptr()) };
                }
                at = unsafe { zput(out, cap, at, b")\0".as_ptr()) };
                unsafe {
                    *out.add(at) = 0;
                }
                return at;
            }
            /* a path type with one segment: user type name, or Option<...> */
            if unsafe { z_eq(text, text_len, b"path\0".as_ptr()) }
                || unsafe { z_eq(text, text_len, b"gpath\0".as_ptr()) }
            {
                return unsafe { self.ctype_path(ty, out, cap) };
            }
            /* named segment (also arrives for generic-less paths) */
            let prim = unsafe { self.prim_ctype(text, text_len, out, cap) };
            if prim > 0 {
                return prim;
            }
            /* same opaque-extern recording as ctype_path's single-segment
             * branch — bare names reach here too (pointer pointees) */
            unsafe { self.opq_note(text, text_len) };
            at = unsafe { zput(out, cap, at, text) };
            unsafe {
                *out.add(at) = 0;
            }
            return at;
        }
        unsafe {
            self.err(b"bad type node\0".as_ptr(), unsafe { (*ty).line });
        }
        0
    }

    /* scratch buffer for nested ctype renders (arena, reused). On OOM it
     * yields the shared oom_buf — lowering is already condemned (ok=false),
     * so no output built from it can ship. */
    unsafe fn arena_tmp(&mut self) -> *mut u8 {
        let p = unsafe { pm_util_mem_alloc(self.arena, 160) };
        if !p.is_null() {
            return p;
        }
        self.ok = false;
        self.oom_buf.as_mut_ptr()
    }

    /* Exact-size scratch for one encoded typedef name. The 160-byte
     * arena_tmp covers every name the current corpus produces; a longer
     * (still legal) tuple signature gets an allocation sized to its exact
     * encoded length rather than a refusal. Bounded by the same
     * elem/caps rules as the encoders: need is computed by the caller from
     * the signature, never from untrusted input. Refuses (NULL) on OOM —
     * the caller records the specific error. */
    unsafe fn name_tmp(&mut self, need: usize) -> *mut u8 {
        if need <= 160 {
            return unsafe { self.arena_tmp() };
        }
        if need > 1024 {
            /* hard ceiling: names beyond this are a runaway signature
             * (nested tuples of Options), refused — not a buffer to grow. */
            return core::ptr::null_mut();
        }
        let p = unsafe { pm_util_mem_alloc(self.arena, need) };
        if p.is_null() {
            self.ok = false;
            return core::ptr::null_mut();
        }
        p
    }

    /* Exact encoded length of a tuple typedef name for signature
     * (elems,lens,n): `rsx_tuple_` + count digits + per element
     * ('_' + len digits + 'e' + 2*elen). Mirrors tup_typedef_name's layout
     * byte for byte; 0 when any element would overflow the sum. */
    unsafe fn tup_name_need(lens: *const usize, n: usize) -> usize {
        let mut need = 10usize; /* "rsx_tuple_" */
        let mut cnt = n;
        let mut cd = 0usize;
        while cnt > 0 {
            cd += 1;
            cnt /= 10;
        }
        if cd == 0 {
            cd = 1;
        }
        need += cd;
        let mut f = 0usize;
        while f < n {
            let elen = unsafe { *lens.add(f) };
            if elen > (usize::MAX - need) / 2 {
                return 0;
            }
            let mut ld = 1usize;
            let mut l = elen;
            while l >= 10 {
                ld += 1;
                l /= 10;
            }
            need += 1 + ld + 1 + 2 * elen;
            if need > 1024 {
                return 0;
            }
            f += 1;
        }
        need + 1 /* NUL */
    }

    unsafe fn ctype_path(&mut self, ty: *const pm_jit_rsx_ast_t, out: *mut u8, cap: usize) -> usize {
        let kids = unsafe { (*ty).kids };
        let nk = unsafe { (*ty).n_kids } as usize;
        let mut at = 0usize;
        if nk == 0 {
            unsafe {
                self.err(b"empty type path\0".as_ptr(), unsafe { (*ty).line });
            }
            return 0;
        }
        let first = unsafe { *kids.add(0) };
        let fname = unsafe { (*first).text };
        let flen = unsafe { (*first).text_len };
        /* Option<T> */
        if unsafe { z_eq(fname, flen, b"Option\0".as_ptr()) } {
            if nk < 2 {
                unsafe {
                    self.err(b"bad Option type\0".as_ptr(), unsafe { (*ty).line });
                }
                return 0;
            }
            let inner = unsafe { *kids.add(1) };
            /* Option<ptr/fn-ptr> lowers to the inner pointer type; the
             * payload type decides (checked by the caller's sym table for
             * user types — here we accept any pointer-ish inner). */
            let inner_buf = self.arena_tmp();
            let n = unsafe { self.ctype(inner, inner_buf, 128) };
            if n == 0 {
                return 0;
            }
            /* Integer payloads cannot collapse to the inner type: Some(0)
             * must not equal None. A named typedef per payload spelling —
             * `rsx_opt_<elem>` = struct { T _v; bool _has; }, emitted once
             * in the preamble — keeps every use site one C type (an inline
             * anonymous struct per site would be mutually incompatible).
             * Pointer payloads keep the collapse (None == NULL is sound). */
            let mut is_ptr = false;
            {
                let mut j = n;
                while j > 0 {
                    let c = unsafe { *inner_buf.add(j - 1) };
                    if c == b' ' {
                        j -= 1;
                        continue;
                    }
                    if c == b'*' {
                        is_ptr = true;
                    }
                    break;
                }
            }
            if !is_ptr {
                if n >= 48 {
                    unsafe {
                        self.err(b"unsupported: Option payload type too long\0".as_ptr(), unsafe { (*ty).line });
                    }
                    return 0;
                }
                if unsafe { self.opt_add(inner_buf, n) } >= OPT_CAP {
                    unsafe {
                        self.err(b"internal: too many Option payload types\0".as_ptr(), unsafe { (*ty).line });
                    }
                    return 0;
                }
                let tdn = self.arena_tmp();
                let tdn_len = unsafe { Lower::opt_typedef_name(inner_buf, n, tdn, 160) };
                if tdn_len == 0 {
                    unsafe {
                        self.err(b"internal: Option typedef name too long\0".as_ptr(), unsafe { (*ty).line });
                    }
                    return 0;
                }
                at = unsafe { bput(out, cap, at, tdn, tdn_len) };
                unsafe {
                    if at < cap {
                        *out.add(at) = 0;
                    } else if cap > 0 {
                        *out.add(cap - 1) = 0;
                    }
                }
                return if at >= cap { 0 } else { at };
            }
            at = unsafe { zput(out, cap, at, inner_buf) };
            unsafe {
                *out.add(at) = 0;
            }
            return at;
        }
        /* UnsafeCell<T> / Cell<T>: a transparent wrapper — the inner type is
         * the C type (a `Mut<T>(UnsafeCell<T>)` static is exactly `T` in C;
         * C has no interior mutability rules to enforce). */
        if nk >= 2
            && (unsafe { z_eq(fname, flen, b"UnsafeCell\0".as_ptr()) }
                || unsafe { z_eq(fname, flen, b"Cell\0".as_ptr()) })
        {
            let inner = unsafe { *kids.add(1) };
            let inner_buf = self.arena_tmp();
            let n = unsafe { self.ctype(inner, inner_buf, 128) };
            if n == 0 {
                return 0;
            }
            at = unsafe { zput(out, cap, at, inner_buf) };
            unsafe {
                *out.add(at) = 0;
            }
            return at;
        }
        /* Registered transparent newtype used with its generic arg
         * (`Mut<T>`, `Mut<[Conn; N]>`) — the C type is the inner arg's. */
        if unsafe { z_eq(unsafe { (*ty).text }, unsafe { (*ty).text_len }, b"gpath\0".as_ptr()) }
            && nk >= 2
            && unsafe { self.nt_find(fname, flen) }
        {
            let inner = unsafe { *kids.add(1) };
            let inner_buf = self.arena_tmp();
            let n = unsafe { self.ctype(inner, inner_buf, 128) };
            if n == 0 {
                return 0;
            }
            at = unsafe { zput(out, cap, at, inner_buf) };
            unsafe {
                *out.add(at) = 0;
            }
            return at;
        }
        /* Any other generic path (`Vec<T>`, `Box<T>`, `HashMap<K, V>`, …)
         * must refuse, not render its leaf: the old fall-through silently
         * took the last generic ARG as the type (`Vec<Export>` -> `Export`),
         * a miscompile. `gpath` text marks a parsed generic list; plain
         * multi-segment paths (`core::ffi::c_void`) stay on the leaf render. */
        if unsafe { z_eq(unsafe { (*ty).text }, unsafe { (*ty).text_len }, b"gpath\0".as_ptr()) }
            && nk >= 2
        {
            unsafe {
                self.err(b"unsupported: generic type outside Option/UnsafeCell\0".as_ptr(), unsafe { (*ty).line });
            }
            return 0;
        }
        if unsafe { z_eq(fname, flen, b"u128\0".as_ptr()) }
            || unsafe { z_eq(fname, flen, b"i128\0".as_ptr()) }
        {
            unsafe {
                self.err(b"unsupported: 128-bit integer type\0".as_ptr(), unsafe { (*ty).line });
            }
            return 0;
        }
        /* single-segment: primitive spelling maps to its C type; anything
         * else is a user type name, used verbatim (typedefs carry it). */
        if nk == 1 {
            let prim = unsafe { self.prim_ctype(fname, flen, out, cap) };
            if prim > 0 {
                return prim;
            }
            /* unknown name: either a type this unit declares (checked when
             * the emit passes run) or an opaque extern type — record it so
             * the preamble can hoist `typedef struct X X;` for the latter. */
            unsafe { self.opq_note(fname, flen) };
            at = unsafe { zput(out, cap, at, fname) };
            unsafe {
                *out.add(at) = 0;
            }
            return at;
        }
        /* Multi-segment path: last segment is the type (core::…, etc. map
         * by their leaf). */
        let leaf = unsafe { *kids.add(nk - 1) };
        let lname = unsafe { (*leaf).text };
        let llen = unsafe { (*leaf).text_len };
        let prim = unsafe { self.prim_ctype(lname, llen, out, cap) };
        if prim > 0 {
            return prim;
        }
        at = unsafe { zput(out, cap, at, lname) };
        unsafe {
            *out.add(at) = 0;
        }
        at
    }

    /* Collect struct/fn/enum info before emission. */
    unsafe fn collect(&mut self, file: *const pm_jit_rsx_ast_t) {
        let kids = unsafe { (*file).kids };
        let nk = unsafe { (*file).n_kids } as usize;
        let mut i = 0usize;
        while i < nk {
            let item = unsafe { *kids.add(i) };
            if item.is_null() {
                i += 1;
                continue;
            }
            let kind = unsafe { (*item).kind };
            if kind == pm_jit_rsx_ast_kind::STRUCT {
                /* Transparent newtypes register here (collect runs before
                 * every emission pass, so pass-0 statics can already spell
                 * them) and stay out of syms — they are not structs in C. */
                if unsafe { self.has_generic_marker(item) } {
                    let ikids = unsafe { (*item).kids };
                    let inkn = unsafe { (*item).n_kids } as usize;
                    let mut nfields = 0usize;
                    let mut single: *const pm_jit_rsx_ast_t = core::ptr::null();
                    let mut j = 0usize;
                    while j < inkn {
                        let f = unsafe { *ikids.add(j) };
                        if unsafe { (*f).kind } == pm_jit_rsx_ast_kind::STRUCT_FIELD {
                            nfields += 1;
                            single = f;
                        }
                        j += 1;
                    }
                    if nfields == 1 {
                        let ftxt = unsafe { (*single).text };
                        let flen = unsafe { (*single).text_len };
                        if flen == 5 && !ftxt.is_null() && unsafe { z_eq(ftxt, flen, b"tuple\0".as_ptr()) } {
                            unsafe { self.nt_add(unsafe { (*item).text }, unsafe { (*item).text_len }) };
                        }
                    }
                    i += 1;
                    continue;
                }
                let s = unsafe { (*self.syms).add(unsafe { (*item).text }, unsafe { (*item).text_len }) };
                let mut j = 0usize;
                while j < unsafe { (*item).n_kids } as usize {
                    let f = unsafe { *(*item).kids.add(j) };
                    if unsafe { (*f).kind } == pm_jit_rsx_ast_kind::STRUCT_FIELD {
                        let fname = unsafe { (*f).text };
                        let flen = unsafe { (*f).text_len };
                        if unsafe { (*f).n_kids } >= 1 && !fname.is_null() {
                            let fty = unsafe { *(*f).kids.add(0) };
                            let ct = self.arena_tmp();
                            let n = unsafe { self.ctype(fty, ct, 128) };
                            if n > 0 {
                                unsafe {
                                    (*self.syms).add_field(self.arena, s, fname, flen, ct, n);
                                }
                            }
                        }
                    }
                    j += 1;
                }
            } else if kind == pm_jit_rsx_ast_kind::ENUM {
                /* enum type name registered (no fields) so `E::V` paths can
                 * resolve their type; variants go into the enum table so
                 * the all-zero static-initializer elision can prove which
                 * variants are zero (same numbering lower_enum assigns). */
                let _ = unsafe { (*self.syms).add(unsafe { (*item).text }, unsafe { (*item).text_len }) };
                let ename = unsafe { (*item).text };
                let elen = unsafe { (*item).text_len };
                let ekids = unsafe { (*item).kids };
                let ekn = unsafe { (*item).n_kids } as usize;
                let mut next: u64 = 0;
                let mut j = 0usize;
                while j < ekn {
                    let k = unsafe { *ekids.add(j) };
                    if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::ENUM_VARIANT {
                        let vt = unsafe { (*k).text };
                        let vl = unsafe { (*k).text_len };
                        let mut val = next;
                        let vkn = unsafe { (*k).n_kids } as usize;
                        let mut m = 0usize;
                        while m < vkn {
                            let d = unsafe { *(*k).kids.add(m) };
                            if unsafe { (*d).kind } == pm_jit_rsx_ast_kind::LITERAL {
                                let dt = unsafe { (*d).text };
                                let dl = unsafe { (*d).text_len };
                                if dl > 0 && !dt.is_null() {
                                    let mut v: u64 = 0;
                                    let mut okv = true;
                                    let mut at = 0usize;
                                    let hex = dl > 2 && unsafe { *dt } == b'0'
                                        && (unsafe { *dt.add(1) } == b'x' || unsafe { *dt.add(1) } == b'X');
                                    if hex {
                                        at = 2;
                                    }
                                    while at < dl {
                                        let c = unsafe { *dt.add(at) };
                                        if c == b'u' || c == b'U' || c == b'i' || c == b'I' {
                                            break;
                                        }
                                        if c == b'_' {
                                            at += 1;
                                            continue;
                                        }
                                        if hex {
                                            let d2 = if c >= b'0' && c <= b'9' {
                                                (c - b'0') as u64
                                            } else if c >= b'a' && c <= b'f' {
                                                (c - b'a' + 10) as u64
                                            } else if c >= b'A' && c <= b'F' {
                                                (c - b'A' + 10) as u64
                                            } else {
                                                okv = false;
                                                break;
                                            };
                                            v = v.wrapping_mul(16).wrapping_add(d2);
                                        } else {
                                            if c < b'0' || c > b'9' {
                                                okv = false;
                                                break;
                                            }
                                            v = v.wrapping_mul(10).wrapping_add((c - b'0') as u64);
                                        }
                                        at += 1;
                                    }
                                    if okv {
                                        val = v;
                                    }
                                }
                                break;
                            }
                            m += 1;
                        }
                        /* C member spelling: Enum_Variant (matches the
                         * joined emission of `E::V` paths). */
                        let mut joined = self.arena_tmp();
                        let mut at2 = 0usize;
                        at2 = unsafe { bput(joined, 64, at2, ename, elen) };
                        at2 = unsafe { bput(joined, 64, at2, b"_\0".as_ptr(), 1) };
                        at2 = unsafe { bput(joined, 64, at2, vt, vl) };
                        unsafe {
                            *joined.add(at2) = 0;
                        }
                        unsafe { self.enums.add(joined, at2, val) };
                        next = val.wrapping_add(1);
                    }
                    j += 1;
                }
            } else if kind == pm_jit_rsx_ast_kind::STATIC || kind == pm_jit_rsx_ast_kind::CONST {
                /* const NAME: T = <int literal> — remember the numeric value
                 * so `[e; NAME]` repeat counts and friends resolve. */
                let ikids = unsafe { (*item).kids };
                let ink = unsafe { (*item).n_kids } as usize;
                let mut vit: *const u8 = core::ptr::null();
                let mut vil: usize = 0;
                let mut j = 0usize;
                while j < ink {
                    let k = unsafe { *ikids.add(j) };
                    if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::LITERAL {
                        vit = unsafe { (*k).text };
                        vil = unsafe { (*k).text_len };
                        break;
                    }
                    j += 1;
                }
                if !vit.is_null() && vil > 0 {
                    let mut v: u64 = 0;
                    let mut okv = true;
                    let mut at = 0usize;
                    let hex = vil > 2 && unsafe { *vit } == b'0'
                        && (unsafe { *vit.add(1) } == b'x' || unsafe { *vit.add(1) } == b'X');
                    if hex {
                        at = 2;
                    }
                    while at < vil {
                        let c = unsafe { *vit.add(at) };
                        /* type suffix (usize, u64, i32, …) ends the digits */
                        if c == b'u' || c == b'U' || c == b'i' || c == b'I' {
                            break;
                        }
                        if c == b'_' {
                            at += 1;
                            continue;
                        }
                        if hex {
                            let d = if c >= b'0' && c <= b'9' {
                                (c - b'0') as u64
                            } else if c >= b'a' && c <= b'f' {
                                (c - b'a' + 10) as u64
                            } else if c >= b'A' && c <= b'F' {
                                (c - b'A' + 10) as u64
                            } else {
                                okv = false;
                                break;
                            };
                            v = v.wrapping_mul(16).wrapping_add(d);
                        } else {
                            if c < b'0' || c > b'9' {
                                okv = false;
                                break;
                            }
                            v = v.wrapping_mul(10).wrapping_add((c - b'0') as u64);
                        }
                        at += 1;
                    }
                    /* trailing type suffixes (usize etc.) are idents — stop
                     * at the first non-digit without failing. */
                    if okv {
                        unsafe {
                            self.consts.add(unsafe { (*item).text }, unsafe { (*item).text_len }, v);
                        }
                    }
                }
            } else if kind == pm_jit_rsx_ast_kind::EXTERN_BLOCK {
                /* extern fns: register name + return type + param types for
                 * call inference — a `None`/`Some(x)` argument at a call
                 * site reads the param's Option shape from the FnTab */
                let mut j = 0usize;
                while j < unsafe { (*item).n_kids } as usize {
                    let k = unsafe { *(*item).kids.add(j) };
                    if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::FN {
                        let ename = unsafe { (*k).text };
                        let elen = unsafe { (*k).text_len };
                        let mut ret = b"void\0".as_ptr();
                        let mut retlen = 4usize;
                        let mut nparams = 0u32;
                        let mut ptypes: [*const u8; FN_MAXP] = [b"\0".as_ptr(); FN_MAXP];
                        let mut plens: [usize; FN_MAXP] = [0; FN_MAXP];
                        let mut j2 = 0usize;
                        while j2 < unsafe { (*k).n_kids } as usize {
                            let kk = unsafe { *(*k).kids.add(j2) };
                            if unsafe { (*kk).kind } == pm_jit_rsx_ast_kind::PARAM {
                                if (nparams as usize) < FN_MAXP {
                                    let pk = unsafe { (*kk).kids };
                                    if unsafe { (*kk).n_kids } as usize >= 1 {
                                        let pty = unsafe { *pk.add(0) };
                                        let ct = self.arena_tmp();
                                        let n = unsafe { self.ctype(pty, ct, 128) };
                                        /* FnTab param types are arena
                                         * spans — no fixed-slot cap; the
                                         * 128 render buffer is the bound */
                                        if n > 0 && n < 128 {
                                            ptypes[nparams as usize] = ct;
                                            plens[nparams as usize] = n;
                                        }
                                    }
                                }
                                nparams += 1;
                            } else if unsafe { (*kk).kind } == pm_jit_rsx_ast_kind::TYPE {
                                let t = unsafe { (*kk).text };
                                let tl = unsafe { (*kk).text_len };
                                if tl == 0 || t.is_null() {
                                    j2 += 1;
                                    continue;
                                }
                                if unsafe { z_eq(t, tl, b"unsafe\0".as_ptr()) }
                                    || unsafe { z_eq(t, tl, b"extern\0".as_ptr()) }
                                    || (unsafe { *t } == b'"')
                                {
                                    j2 += 1;
                                    continue;
                                }
                                let ct = self.arena_tmp();
                                let n = unsafe { self.ctype(kk, ct, 128) };
                                if n > 0 {
                                    ret = ct;
                                    retlen = n;
                                }
                            }
                            j2 += 1;
                        }
                        let fs = unsafe { (*self.fns).add(ename, elen, ret, retlen) };
                        unsafe {
                            (*self.fns).set_n_params(fs, nparams);
                        }
                        if fs < SYM_CAP {
                            let mut p = 0usize;
                            while p < FN_MAXP && p < nparams as usize {
                                if plens[p] > 0 {
                                    unsafe {
                                        (*self.fns).add_param(fs, p, ptypes[p], plens[p]);
                                    }
                                }
                                p += 1;
                            }
                        }
                    }
                    j += 1;
                }
            } else if kind == pm_jit_rsx_ast_kind::FN {
                let name = unsafe { (*item).text };
                let nlen = unsafe { (*item).text_len };
                let n_k = unsafe { (*item).n_kids } as usize;
                let mut ret = b"void\0".as_ptr();
                let mut retlen = 4usize;
                let mut nparams = 0u32;
                /* param slots in order — rendered ctypes land in the FnTab
                 * so a `None` argument at a call site reads the param's
                 * Option shape (NULL vs the rsx_opt_ zero literal) */
                let mut ptypes: [*const u8; FN_MAXP] = [b"\0".as_ptr(); FN_MAXP];
                let mut plens: [usize; FN_MAXP] = [0; FN_MAXP];
                let mut j = 0usize;
                while j < n_k {
                    let k = unsafe { *(*item).kids.add(j) };
                    let kk = unsafe { (*k).kind };
                    if kk == pm_jit_rsx_ast_kind::PARAM {
                        if (nparams as usize) < FN_MAXP {
                            let pk = unsafe { (*k).kids };
                            if unsafe { (*k).n_kids } as usize >= 1 {
                                let pty = unsafe { *pk.add(0) };
                                let ct = self.arena_tmp();
                                let n = unsafe { self.ctype(pty, ct, 128) };
                                /* FnTab param types are arena spans — no
                                 * fixed-slot cap; the render buffer is */
                                if n > 0 && n < 128 {
                                    ptypes[nparams as usize] = ct;
                                    plens[nparams as usize] = n;
                                }
                            }
                        }
                        nparams += 1;
                    } else if kk == pm_jit_rsx_ast_kind::TYPE {
                        /* return type is the last TYPE kid; quals arrive as
                         * TYPE kids too (unsafe/extern/"C") — skip them like
                         * lower_fn does, they are not the ret */
                        let t = unsafe { (*k).text };
                        let tl = unsafe { (*k).text_len };
                        if tl > 0 && !t.is_null() {
                            let is_qual = unsafe { z_eq(t, tl, b"unsafe\0".as_ptr()) }
                                || unsafe { z_eq(t, tl, b"extern\0".as_ptr()) }
                                || unsafe { *t } == b'"';
                            if !is_qual {
                                let ct = self.arena_tmp();
                                let n = unsafe { self.ctype(k, ct, 128) };
                                if n > 0 {
                                    ret = ct;
                                    retlen = n;
                                }
                            }
                        }
                        let ct = self.arena_tmp();
                        let n = unsafe { self.ctype(k, ct, 128) };
                        if n > 0 {
                            ret = ct;
                            retlen = n;
                        }
                    }
                    j += 1;
                }
                let fs = unsafe { (*self.fns).add(name, nlen, ret, retlen) };
                unsafe {
                    (*self.fns).set_n_params(fs, nparams);
                }
                if fs < SYM_CAP {
                    let mut p = 0usize;
                    while p < FN_MAXP && p < nparams as usize {
                        if plens[p] > 0 {
                            unsafe {
                                (*self.fns).add_param(fs, p, ptypes[p], plens[p]);
                            }
                        }
                        p += 1;
                    }
                }
            } else if kind == pm_jit_rsx_ast_kind::IMPL {
                /* methods: Type_method with self as first param. */
                self.collect_impl(item);
            }
            i += 1;
        }
    }

    unsafe fn collect_impl(&mut self, item: *const pm_jit_rsx_ast_t) {
        /* kids: attrs..., self_ty, [trait], methods... */
        let kids = unsafe { (*item).kids };
        let nk = unsafe { (*item).n_kids } as usize;
        /* self type: first non-ATTR kid (a path TYPE node) */
        let mut self_ty: *const u8 = b"\0".as_ptr();
        let mut self_ty_len = 0usize;
        let mut j = 0usize;
        while j < nk {
            let k = unsafe { *kids.add(j) };
            if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::TYPE {
                /* leaf of the path */
                let kk = unsafe { (*k).kids };
                let kn = unsafe { (*k).n_kids } as usize;
                if kn > 0 {
                    let leaf = unsafe { *kk.add(kn - 1) };
                    self_ty = unsafe { (*leaf).text };
                    self_ty_len = unsafe { (*leaf).text_len };
                }
                break;
            }
            j += 1;
        }
        let mut methods_start = 0usize;
        while methods_start < nk {
            let k = unsafe { *kids.add(methods_start) };
            if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::FN {
                break;
            }
            methods_start += 1;
        }
        j = methods_start;
        while j < nk {
            let k = unsafe { *kids.add(j) };
            if unsafe { (*k).kind } != pm_jit_rsx_ast_kind::FN {
                j += 1;
                continue;
            }
            /* mangled name Type_method */
            let mut name_buf = self.arena_tmp();
            let mut at = 0usize;
            at = unsafe { bput(name_buf, 128, at, self_ty, self_ty_len) };
            at = unsafe { bput(name_buf, 128, at, b"_\0".as_ptr(), 1) };
            at = unsafe { bput(name_buf, 128, at, unsafe { (*k).text }, unsafe { (*k).text_len }) };
            unsafe {
                *name_buf.add(at) = 0;
            }
            /* return type */
            let mut ret = b"void\0".as_ptr();
            let mut retlen = 4usize;
            let mut j2 = 0usize;
            while j2 < unsafe { (*k).n_kids } as usize {
                let kk = unsafe { *(*k).kids.add(j2) };
                if unsafe { (*kk).kind } == pm_jit_rsx_ast_kind::TYPE {
                    let ct = self.arena_tmp();
                    let n = unsafe { self.ctype(kk, ct, 128) };
                    if n > 0 {
                        ret = ct;
                        retlen = n;
                    }
                }
                j2 += 1;
            }
            let fs = unsafe { (*self.fns).add(name_buf, at, ret, retlen) };
            unsafe {
                (*self.fns).set_n_params(fs, 1);
            }
            j += 1;
        }
    }

    /* Innermost tail node of a block (unwrapping BLOCK/STMT/EXPR_STMT). */
    unsafe fn block_tail_node(&mut self, b: *const pm_jit_rsx_ast_t) -> *const pm_jit_rsx_ast_t {
        let mut b2 = b;
        let mut guard = 0usize;
        loop {
            guard += 1;
            if guard > 64 {
                return b2;
            }
            let k = unsafe { (*b2).kind };
            let n = unsafe { (*b2).n_kids } as usize;
            if n == 0 {
                return b2;
            }
            if k == pm_jit_rsx_ast_kind::BLOCK || k == pm_jit_rsx_ast_kind::STMT {
                b2 = unsafe { *(*b2).kids.add(n - 1) };
                continue;
            }
            if k == pm_jit_rsx_ast_kind::EXPR_STMT {
                b2 = unsafe { *(*b2).kids.add(0) };
                continue;
            }
            return b2;
        }
    }

    /* expr's C type into out; 0 = unknown. Only what the lowering needs:
     * literals, casts, field types via the sym table, calls via the fn
     * table, locals via the let-types seen in this function. */
    unsafe fn expr_ctype(&mut self, e: *const pm_jit_rsx_ast_t, out: *mut u8, cap: usize, locals: *mut LocalTab) -> usize {
        if e.is_null() {
            return 0;
        }
        let kind = unsafe { (*e).kind };
        if kind == pm_jit_rsx_ast_kind::LITERAL {
            let t = unsafe { (*e).text };
            let tl = unsafe { (*e).text_len };
            if tl == 0 {
                return 0;
            }
            let c = unsafe { *t };
            if tl >= 3 && c == b'b' && unsafe { *t.add(1) } == b'\'' {
                /* byte char b'x' — an integer, not a pointer */
                return unsafe { zput(out, cap, 0, b"uint8_t\0".as_ptr()) };
            }
            if c == b'"' || c == b'b' || c == b'r' {
                return unsafe { zput(out, cap, 0, b"const char *\0".as_ptr()) };
            }
            if c == b'\'' {
                return unsafe { zput(out, cap, 0, b"uint32_t\0".as_ptr()) };
            }
            /* numeric: check suffix */
            let mut i = 0usize;
            while i < tl {
                let ch = unsafe { *t.add(i) };
                if ch == b'u' || ch == b'i' {
                    break;
                }
                i += 1;
            }
            if i < tl {
                return unsafe { self.suffix_ctype(t.add(i), tl - i, out, cap) };
            }
            /* no suffix: int */
            return unsafe { zput(out, cap, 0, b"int32_t\0".as_ptr()) };
        }
        if kind == pm_jit_rsx_ast_kind::CAST {
            /* kids: expr, type */
            let kids = unsafe { (*e).kids };
            if unsafe { (*e).n_kids } >= 2 {
                let ty = unsafe { *kids.add(1) };
                return unsafe { self.ctype(ty, out, cap) };
            }
            return 0;
        }
        if kind == pm_jit_rsx_ast_kind::TUPLE {
            /* tuple expression: register the signature from the element
             * types and render the shared typedef name — same path the
             * tuple TYPE takes, so both agree on one C type. */
            let kids = unsafe { (*e).kids };
            let nk = unsafe { (*e).n_kids } as usize;
            if nk == 0 || nk > TUP_MAXF {
                return 0;
            }
            let mut el_bufs: [[u8; 64]; TUP_MAXF] = [[0; 64]; TUP_MAXF];
            let mut el_lens: [usize; TUP_MAXF] = [0; TUP_MAXF];
            let mut f = 0usize;
            while f < nk {
                let kv = unsafe { *kids.add(f) };
                let n = unsafe { self.expr_ctype(kv, el_bufs[f].as_mut_ptr(), 64, locals) };
                if n == 0 || n >= 64 {
                    return 0;
                }
                el_lens[f] = n;
                f += 1;
            }
            let slot = unsafe { self.tup_add(el_bufs.as_ptr(), el_lens.as_ptr(), nk) };
            if slot >= TUP_CAP {
                return 0;
            }
            let base = self.tup_elems.as_ptr().add(slot * TUP_MAXF);
            let blens = self.tup_lens.as_ptr().add(slot * TUP_MAXF);
            let need = unsafe { Lower::tup_name_need(blens, nk) };
            let tdn = if need == 0 {
                core::ptr::null_mut()
            } else {
                unsafe { self.name_tmp(need) }
            };
            if tdn.is_null() {
                return 0;
            }
            let tdn_len = unsafe { Lower::tup_typedef_name(base, blens, nk, tdn, need) };
            if tdn_len == 0 {
                return 0;
            }
            let at = unsafe { bput(out, cap, 0, tdn, tdn_len) };
            unsafe {
                if at < cap {
                    *out.add(at) = 0;
                } else if cap > 0 {
                    *out.add(cap - 1) = 0;
                }
            }
            return if at >= cap { 0 } else { at };
        }
        if kind == pm_jit_rsx_ast_kind::BLOCK {
            /* value-position block (incl. `unsafe { .. }` wrappers): type of
             * its tail expr, unwrapping nested statement wrappers. */
            let kids = unsafe { (*e).kids };
            let nk = unsafe { (*e).n_kids } as usize;
            if nk == 0 {
                return 0;
            }
            let tail = unsafe { *kids.add(nk - 1) };
            let tk = unsafe { (*tail).kind };
            if tk == pm_jit_rsx_ast_kind::EXPR_STMT && unsafe { (*tail).n_kids } >= 1 {
                let inner = unsafe { *(*tail).kids.add(0) };
                return unsafe { self.expr_ctype(inner, out, cap, locals) };
            }
            if tk == pm_jit_rsx_ast_kind::BLOCK {
                return unsafe { self.expr_ctype(tail, out, cap, locals) };
            }
            if tk != pm_jit_rsx_ast_kind::STMT {
                return unsafe { self.expr_ctype(tail, out, cap, locals) };
            }
            return 0;
        }
        if kind == pm_jit_rsx_ast_kind::UNARY {
            /* `*p` — pointee; `&e`/`&mut e` — pointer to; `!e` — bool;
             * `-e` — the operand's type. */
            let op = unsafe { (*e).text };
            let op_len = unsafe { (*e).text_len };
            let kids = unsafe { (*e).kids };
            if unsafe { (*e).n_kids } >= 1 {
                let b_buf = self.arena_tmp();
                let bn = unsafe { self.expr_ctype(*kids.add(0), b_buf, 128, locals) };
                if unsafe { z_eq(op, op_len, b"*\0".as_ptr()) } {
                    if bn == 0 {
                        return 0;
                    }
                    /* strip one trailing '*' (and spaces) from e.g. `const u8 *`.
                     * A non-pointer operand is identity: Rust `*x.get()` on a
                     * transparent-newtype static derefs a place whose C type
                     * is already the value (`Mut<usize>` IS a size_t lvalue). */
                    let mut j = bn;
                    while j > 0 && unsafe { *b_buf.add(j - 1) } == b' ' {
                        j -= 1;
                    }
                    if j == 0 || unsafe { *b_buf.add(j - 1) } != b'*' {
                        /* pointer-to-array operand (`T (*)[N]`): the deref is
                         * the array lvalue `T [N]` — drop the ` (*)` group,
                         * never the trailing-star identity (the type ends
                         * with `]`, which the strip above would misread). */
                        let pp = unsafe { self.parr_declarator(b_buf, bn) };
                        if pp != usize::MAX {
                            if bn < cap {
                                let mut w = 0usize;
                                let mut r = 0usize;
                                while r < bn {
                                    if r < pp || r >= pp + 3 {
                                        unsafe {
                                            *out.add(w) = *b_buf.add(r);
                                        }
                                        w += 1;
                                    }
                                    r += 1;
                                }
                                unsafe {
                                    *out.add(w) = 0;
                                }
                                return w;
                            }
                            return 0;
                        }
                        return bn;
                    }
                    j -= 1;
                    while j > 0 && unsafe { *b_buf.add(j - 1) } == b' ' {
                        j -= 1;
                    }
                    if j == 0 {
                        return 0;
                    }
                    unsafe {
                        core::ptr::copy_nonoverlapping(b_buf, out, j);
                        *out.add(j) = 0;
                    }
                    return j;
                }
                if unsafe { z_eq(op, op_len, b"&\0".as_ptr()) }
                    || unsafe { z_eq(op, op_len, b"&mut\0".as_ptr()) }
                {
                    if bn == 0 {
                        return 0;
                    }
                    /* `&slice[lo..hi]` is the sub-slice pointer itself —
                     * no extra indirection over the index's own type. */
                    let inner = unsafe { *kids.add(0) };
                    let is_range_idx = unsafe { rsx_idx_is_range(inner) };
                    if is_range_idx {
                        unsafe {
                            core::ptr::copy_nonoverlapping(b_buf, out, bn);
                            *out.add(bn) = 0;
                        }
                        return bn;
                    }
                    /* reference-to-array (`&mut arr` where arr's ctype ends
                     * in dims): pointer-to-array declarator `T (*)[N]` */
                    let dsplit = unsafe { self.arr_dims_split(b_buf, bn) };
                    if dsplit < bn {
                        let mut j = 0usize;
                        let mut at2 = 0usize;
                        while j < dsplit {
                            unsafe {
                                *out.add(at2) = *b_buf.add(j);
                            }
                            at2 += 1;
                            j += 1;
                        }
                        let n2b = unsafe { zput(out, cap, at2, b" (*)\0".as_ptr()) };
                        if n2b >= cap {
                            return 0;
                        }
                        let mut j2 = dsplit;
                        let mut at3 = n2b;
                        while j2 < bn {
                            unsafe {
                                *out.add(at3) = *b_buf.add(j2);
                            }
                            at3 += 1;
                            j2 += 1;
                        }
                        unsafe {
                            *out.add(at3) = 0;
                        }
                        return at3;
                    }
                    let n2 = unsafe { zput(out, cap, 0, b_buf) };
                    if n2 >= cap {
                        return 0;
                    }
                    let n3 = unsafe { zput(out, cap, n2, b" *\0".as_ptr()) };
                    return if n3 >= cap { 0 } else { n3 };
                }
                if unsafe { z_eq(op, op_len, b"-\0".as_ptr()) } {
                    if bn == 0 {
                        return 0;
                    }
                    unsafe {
                        core::ptr::copy_nonoverlapping(b_buf, out, bn);
                        *out.add(bn) = 0;
                    }
                    return bn;
                }
                if unsafe { z_eq(op, op_len, b"?\0".as_ptr()) } {
                    /* `expr?` yields the *payload*, not the Option. A
                     * pointer-Option operand is already the inner pointer
                     * spelling; a struct-Option operand (`rsx_opt_<len>e<elem>`)
                     * must unwrap: the try expression's value is
                     * `__rsx_try._v`. opt_typedef_elem parses the `<len>e`
                     * prefix (and validates the payload length) — never a
                     * hardcoded byte skip. */
                    if bn == 0 {
                        return 0;
                    }
                    if bn >= 8 && unsafe { z_eq(b_buf, 8, b"rsx_opt_\0".as_ptr()) } {
                        let pl = unsafe { Lower::opt_typedef_elem(b_buf, bn, out, cap) };
                        if pl > 0 {
                            return pl;
                        }
                        return 0;
                    }
                    unsafe {
                        core::ptr::copy_nonoverlapping(b_buf, out, bn);
                        *out.add(bn) = 0;
                    }
                    return bn;
                }
            }
            if unsafe { z_eq(op, op_len, b"!\0".as_ptr()) } {
                let n2 = unsafe { zput(out, cap, 0, b"bool\0".as_ptr()) };
                return if n2 >= cap { 0 } else { n2 };
            }
            return 0;
        }
        if kind == pm_jit_rsx_ast_kind::PATH {
            /* local first, then struct literal type, then plain name.
             * Childless PATH nodes (`self`) use their own text. */
            let kids = unsafe { (*e).kids };
            let nk = unsafe { (*e).n_kids } as usize;
            if nk == 0 {
                let name = unsafe { (*e).text };
                let nl = unsafe { (*e).text_len };
                if nl == 0 {
                    return 0;
                }
                let n = unsafe { (*locals).lookup(name, nl, out) };
                if n > 0 {
                    return n;
                }
                let n2 = unsafe { self.st_find(name, nl, out, cap) };
                if n2 > 0 {
                    return n2;
                }
                return 0;
            }
            let leaf = unsafe { *kids.add(nk - 1) };
            let name = unsafe { (*leaf).text };
            let nl = unsafe { (*leaf).text_len };
            let n = unsafe { (*locals).lookup(name, nl, out) };
            if n > 0 {
                return n;
            }
            /* single-segment paths may name a top-level static/const */
            if nk == 1 {
                let n2 = unsafe { self.st_find(name, nl, out, cap) };
                if n2 > 0 {
                    return n2;
                }
            }
            /* bool literals */
            if unsafe { z_eq(name, nl, b"true\0".as_ptr()) }
                || unsafe { z_eq(name, nl, b"false\0".as_ptr()) }
            {
                let n2 = unsafe { zput(out, cap, 0, b"bool\0".as_ptr()) };
                return if n2 >= cap { 0 } else { n2 };
            }
            /* struct literal type: S { .. } / m::S { .. } -> S. The
             * trailing STRUCT_LIT node's own text is the marker
             * "struct-lit" — the type name is the last path segment
             * before it. */
            if unsafe { (*leaf).kind } == pm_jit_rsx_ast_kind::STRUCT_LIT {
                let mut i2 = nk;
                while i2 > 0 {
                    i2 -= 1;
                    let seg = unsafe { *kids.add(i2) };
                    let sk = unsafe { (*seg).kind };
                    if sk != pm_jit_rsx_ast_kind::PATH && sk != pm_jit_rsx_ast_kind::STRUCT_LIT {
                        continue;
                    }
                    if sk == pm_jit_rsx_ast_kind::STRUCT_LIT {
                        continue;
                    }
                    let sname = unsafe { (*seg).text };
                    let slen = unsafe { (*seg).text_len };
                    if slen > 0 {
                        let n2 = unsafe { zput(out, cap, 0, sname) };
                        return if n2 >= cap { 0 } else { n2 };
                    }
                }
                return 0;
            }
            if nk >= 2 {
                /* `E::V` / `Ty::CONST` — the head segment names the type. */
                let head = unsafe { *kids.add(0) };
                let hname = unsafe { (*head).text };
                let hlen = unsafe { (*head).text_len };
                if hlen > 0 && unsafe { (*self.syms).find(hname, hlen) } < SYM_CAP {
                    let n2 = unsafe { zput(out, cap, 0, hname) };
                    return if n2 >= cap { 0 } else { n2 };
                }
            }
            if nk == 1 {
                /* a struct name as a value is not a value; but `S`
                 * alone (enum) — unknown */
                return 0;
            }
            return 0;
        }
        if kind == pm_jit_rsx_ast_kind::CALL {
            /* kids: callee(path), args */
            let kids = unsafe { (*e).kids };
            if unsafe { (*e).n_kids } >= 1 {
                let callee = unsafe { *kids.add(0) };
                if unsafe { (*callee).kind } == pm_jit_rsx_ast_kind::PATH {
                    /* call through a local fn-pointer bind (`if let Some(h)
                     * = r.handler`): the callee names a local whose type is
                     * the fn-ptr spelling — either a raw render `ret (*)(..)`
                     * (extract the ret before the first `(`) or a type alias
                     * (st_find resolves it to the ret directly, registered
                     * at the alias's typedef). */
                    {
                        let ck0 = unsafe { (*callee).kids };
                        let cn0 = unsafe { (*callee).n_kids } as usize;
                        if cn0 >= 1 {
                            let leaf0 = unsafe { *ck0.add(cn0 - 1) };
                            if unsafe { (*leaf0).kind } == pm_jit_rsx_ast_kind::PATH {
                                let ln0 = unsafe { (*leaf0).text };
                                let ll0 = unsafe { (*leaf0).text_len };
                                let lb = self.arena_tmp();
                                let ln = unsafe { (*locals).lookup(ln0, ll0, lb) };
                                if ln > 0 {
                                    let mut sp = lb;
                                    let mut spl = ln;
                                    /* alias? resolve one level */
                                    let mut ri = 0usize;
                                    let mut has_paren = false;
                                    while ri < spl {
                                        if unsafe { *sp.add(ri) } == b'(' {
                                            has_paren = true;
                                            break;
                                        }
                                        ri += 1;
                                    }
                                    if !has_paren {
                                        let ab = self.arena_tmp();
                                        let an = unsafe { self.st_find(sp, spl, ab, 128) };
                                        if an > 0 {
                                            sp = ab;
                                            spl = an;
                                            ri = 0;
                                            while ri < spl {
                                                if unsafe { *sp.add(ri) } == b'(' {
                                                    has_paren = true;
                                                    break;
                                                }
                                                ri += 1;
                                            }
                                        }
                                    }
                                    if has_paren && ri > 0 && ri < cap {
                                        let mut j = 0usize;
                                        while j < ri {
                                            unsafe {
                                                *out.add(j) = *sp.add(j);
                                            }
                                            j += 1;
                                        }
                                        unsafe {
                                            *out.add(ri) = 0;
                                        }
                                        return ri;
                                    }
                                    if !has_paren && spl > 0 && spl < cap {
                                        /* the alias resolved straight to the ret */
                                        let mut j = 0usize;
                                        while j < spl {
                                            unsafe {
                                                *out.add(j) = *sp.add(j);
                                            }
                                            j += 1;
                                        }
                                        unsafe {
                                            *out.add(spl) = 0;
                                        }
                                        return spl;
                                    }
                                }
                            }
                        }
                    }
                    let ck = unsafe { (*callee).kids };
                    let cn = unsafe { (*callee).n_kids } as usize;
                    /* core::ptr::null_mut() -> generic pointer; usable where a
                     * pointer-typed let needs *some* type (branch inference
                     * usually picks the typed branch first). */
                    if cn >= 3 {
                        let s0 = unsafe { *ck.add(0) };
                        let s1 = unsafe { *ck.add(1) };
                        let s2 = unsafe { *ck.add(2) };
                        if unsafe { (*s0).kind } == pm_jit_rsx_ast_kind::PATH
                            && unsafe { (*s1).kind } == pm_jit_rsx_ast_kind::PATH
                            && unsafe { (*s2).kind } == pm_jit_rsx_ast_kind::PATH
                            && unsafe { z_eq((*s0).text, (*s0).text_len, b"core\0".as_ptr()) }
                            && unsafe { z_eq((*s1).text, (*s1).text_len, b"ptr\0".as_ptr()) }
                            && (unsafe { z_eq((*s2).text, (*s2).text_len, b"null_mut\0".as_ptr()) }
                                || unsafe { z_eq((*s2).text, (*s2).text_len, b"null\0".as_ptr()) })
                        {
                            let n2 = unsafe { zput(out, cap, 0, b"void *\0".as_ptr()) };
                            return if n2 >= cap { 0 } else { n2 };
                        }
                    }
                    /* core::mem::size_of::<T>() -> size_t */
                    if cn >= 3 {
                        let s0 = unsafe { *ck.add(0) };
                        let s1 = unsafe { *ck.add(1) };
                        let s2 = unsafe { *ck.add(2) };
                        if unsafe { (*s0).kind } == pm_jit_rsx_ast_kind::PATH
                            && unsafe { (*s1).kind } == pm_jit_rsx_ast_kind::PATH
                            && unsafe { (*s2).kind } == pm_jit_rsx_ast_kind::PATH
                            && unsafe { z_eq((*s0).text, (*s0).text_len, b"core\0".as_ptr()) }
                            && unsafe { z_eq((*s1).text, (*s1).text_len, b"mem\0".as_ptr()) }
                            && unsafe { z_eq((*s2).text, (*s2).text_len, b"size_of\0".as_ptr()) }
                        {
                            let n2 = unsafe { zput(out, cap, 0, b"size_t\0".as_ptr()) };
                            return if n2 >= cap { 0 } else { n2 };
                        }
                    }
                    if cn >= 1 {
                        let leaf = unsafe { *ck.add(cn - 1) };
                        if unsafe { (*leaf).kind } == pm_jit_rsx_ast_kind::PATH {
                            let name = unsafe { (*leaf).text };
                            let nl = unsafe { (*leaf).text_len };
                            /* Newtype constructor / UnsafeCell::new — the
                             * call's type is the argument's type. */
                            if unsafe { (*e).n_kids } >= 2 {
                                let args = unsafe { *kids.add(1) };
                                let an = unsafe { (*args).n_kids } as usize;
                                let akids = unsafe { (*args).kids };
                                if an == 1 {
                                    let mut unwrap = unsafe { self.nt_find(name, nl) };
                                    if !unwrap && cn >= 2 && unsafe { z_eq(name, nl, b"new\0".as_ptr()) } {
                                        /* segment before `new` names the
                                         * wrapper, any qualification depth */
                                        let wrap = unsafe { *ck.add(cn - 2) };
                                        let wname = unsafe { (*wrap).text };
                                        let wlen = unsafe { (*wrap).text_len };
                                        if unsafe { z_eq(wname, wlen, b"UnsafeCell\0".as_ptr()) }
                                            || unsafe { z_eq(wname, wlen, b"Cell\0".as_ptr()) }
                                        {
                                            unwrap = true;
                                        }
                                    }
                                    if unwrap {
                                        return unsafe { self.expr_ctype(*akids.add(0), out, cap, locals) };
                                    }
                                }
                            }
                            let n = unsafe { (*self.fns).ret_ctype(name, nl, out) };
                            if n > 0 {
                                return n;
                            }
                            /* `Type::fn(..)` — associated fn return type */
                            if cn >= 2 {
                                let head = unsafe { *ck.add(0) };
                                let hname = unsafe { (*head).text };
                                let hlen = unsafe { (*head).text_len };
                                if hlen > 0
                                    && unsafe { (*self.syms).find(hname, hlen) } < SYM_CAP
                                {
                                    let mbuf = self.arena_tmp();
                                    let mn = unsafe { bput(mbuf, 160, 0, hname, hlen) };
                                    let mn2 = unsafe { bput(mbuf, 160, mn, b"_\0".as_ptr(), 1) };
                                    let mn3 =
                                        unsafe { bput(mbuf, 160, mn2, name, nl) };
                                    unsafe {
                                        *mbuf.add(mn3) = 0;
                                    }
                                    let rn = unsafe { (*self.fns).ret_ctype(mbuf, mn3, out) };
                                    if rn > 0 {
                                        return rn;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            return 0;
        }
        if kind == pm_jit_rsx_ast_kind::FIELD {
            /* kids: base, name — need base's type */
            let kids = unsafe { (*e).kids };
            if unsafe { (*e).n_kids } >= 2 {
                let base = unsafe { *kids.add(0) };
                let fname = unsafe { *kids.add(1) };
                /* numeric `.N` on a transparent newtype: the unwrap keeps
                 * the base's (already-inner) C type. On a real tuple
                 * (rsx_tuple_*) it is the designated element: look the
                 * signature back up by typedef name. */
                {
                    let ftxt = unsafe { (*fname).text };
                    let flen = unsafe { (*fname).text_len };
                    if flen > 0 && !ftxt.is_null() && unsafe { *ftxt } >= b'0' && unsafe { *ftxt } <= b'9' {
                        let bb = self.arena_tmp();
                        let bl_raw = unsafe { self.expr_ctype(base, bb, 128, locals) };
                        /* `*const (..)` derefs register the pointee WITH the
                         * `const ` qualifier — skip it so the tuple prefix
                         * test sees rsx_tuple_, never `const rsx_tuple_`
                         * (which would fall through and type the field as
                         * the whole tuple). */
                        let mut bb2 = bb;
                        let mut bl = bl_raw;
                        if bl >= 6 && unsafe { z_eq(bb, 6, b"const \0".as_ptr()) } {
                            bb2 = unsafe { bb.add(6) };
                            bl -= 6;
                        }
                        if bl >= 10 && unsafe { z_eq(bb2, 10, b"rsx_tuple_\0".as_ptr()) } {
                            let fi = (unsafe { *ftxt }) - b'0';
                            let s = unsafe { self.tup_find(bb2, bl) };
                            if s >= TUP_CAP || (fi as usize) >= self.tup_counts[s] {
                                return 0;
                            }
                            let a = s * TUP_MAXF + fi as usize;
                            let el = unsafe { bput(out, cap, 0, self.tup_elems[a].as_ptr(), self.tup_lens[a]) };
                            unsafe {
                                if el < cap {
                                    *out.add(el) = 0;
                                } else if cap > 0 {
                                    *out.add(cap - 1) = 0;
                                }
                            }
                            return if el >= cap { 0 } else { el };
                        }
                        return unsafe { self.expr_ctype(base, out, cap, locals) };
                    }
                }
                let b_buf = self.arena_tmp();
                let bn = unsafe { self.expr_ctype(base, b_buf, 128, locals) };
                if bn == 0 {
                    return 0;
                }
                /* strip a trailing ' *' when present (pointer base);
                 * non-pointer bases are the struct itself (`(*e).f`). */
                let mut tn: *const u8 = b"\0".as_ptr();
                let mut tn_len = 0usize;
                unsafe {
                    let mut j = bn;
                    while j > 0 {
                        if *b_buf.add(j - 1) == b'*' {
                            j -= 1;
                            break;
                        }
                        j -= 1;
                    }
                    while j > 0 && *b_buf.add(j - 1) == b' ' {
                        j -= 1;
                    }
                    if j == 0 {
                        j = bn;
                    }
                    if j > 0 {
                        tn = b_buf;
                        tn_len = j;
                    }
                }
                if tn_len == 0 {
                    return 0;
                }
                /* strip a leading `const ` (&T receivers render as `const T *`) */
                let mut tb = tn;
                let mut tl = tn_len;
                if tl >= 6
                    && unsafe { *tb.add(0) } == b'c'
                    && unsafe { *tb.add(1) } == b'o'
                    && unsafe { *tb.add(2) } == b'n'
                    && unsafe { *tb.add(3) } == b's'
                    && unsafe { *tb.add(4) } == b't'
                    && unsafe { *tb.add(5) } == b' '
                {
                    tb = unsafe { tb.add(6) };
                    tl -= 6;
                }
                let s = unsafe { (*self.syms).find(tb, tl) };
                if s >= SYM_CAP {
                    unsafe {
                        self.err(b"unsupported: field base type - ascribe it\0".as_ptr(), unsafe { (*e).line });
                        self.err_parts(tb, tl, unsafe { (*fname).text }, unsafe { (*fname).text_len });
                    }
                    return 0;
                }
                let n2 = unsafe {
                    (*self.syms).field_ctype(
                        s,
                        unsafe { (*fname).text },
                        unsafe { (*fname).text_len },
                        out,
                    )
                };
                if n2 == 0 {
                    let st = unsafe { (*self.syms).names[s] };
                    let stl = unsafe { (*self.syms).name_lens[s] };
                    let sc = unsafe { (*self.syms).field_counts[s] };
                    let _ = sc;
                    unsafe {
                        self.err(b"unsupported: unknown field of base type\0".as_ptr(), unsafe { (*e).line });
                        self.err_parts(tb, tl, unsafe { (*fname).text }, unsafe { (*fname).text_len });
                        self.err_parts(st, stl, b"(struct slot)\0".as_ptr(), 14);
                    }
                }
                return n2;
            }
            return 0;
        }
        if kind == pm_jit_rsx_ast_kind::INDEX {
            /* `base[idx]` — element type: strip the trailing `[N]` / `[]`
             * from the base's type. */
            let kids = unsafe { (*e).kids };
            if unsafe { (*e).n_kids } < 1 {
                return 0;
            }
            let base = unsafe { *kids.add(0) };
            /* range index `a[lo..hi]`: still a slice pointer of the same
             * element type — the base type carries over unchanged. */
            if unsafe { (*e).n_kids } >= 2 {
                let is_range = unsafe { rsx_idx_is_range(e) };
                if is_range {
                    return unsafe { self.expr_ctype(base, out, cap, locals) };
                }
            }
            let b_buf = self.arena_tmp();
            let bn = unsafe { self.expr_ctype(base, b_buf, 128, locals) };
            if bn == 0 {
                return 0;
            }
            /* pointer-to-array base (`Defer (*)[MAX_DEFER]` — `defers()[i]`):
             * the element type is what precedes the `(*)` declarator. */
            {
                let mut i = 0usize;
                while i + 3 <= bn {
                    if unsafe { *b_buf.add(i) } == b'('
                        && unsafe { *b_buf.add(i + 1) } == b'*'
                        && unsafe { *b_buf.add(i + 2) } == b')'
                    {
                        let mut last = i;
                        while last > 0 && unsafe { *b_buf.add(last - 1) } == b' ' {
                            last -= 1;
                        }
                        if last == 0 {
                            return 0;
                        }
                        if last > cap - 1 {
                            last = cap - 1;
                        }
                        let mut j = 0usize;
                        while j < last {
                            unsafe { *out.add(j) = *b_buf.add(j) };
                            j += 1;
                        }
                        unsafe { *out.add(j) = 0 };
                        return j;
                    }
                    i += 1;
                }
            }
            /* last '[' at or after the last non-space byte; find last '[' */
            let mut last = bn;
            let mut open: i64 = -1;
            let mut i = bn as i64 - 1;
            while i >= 0 {
                if unsafe { *b_buf.add(i as usize) } == b'[' {
                    open = i;
                    break;
                }
                i -= 1;
            }
            if open < 0 {
                /* pointer indexing (`slice[k]` where the slice param
                 * lowered to `const T *`): element = base minus one '*'. */
                let mut j = bn;
                while j > 0 && unsafe { *b_buf.add(j - 1) } == b' ' {
                    j -= 1;
                }
                if j == 0 || unsafe { *b_buf.add(j - 1) } != b'*' {
                    return 0;
                }
                j -= 1;
                while j > 0 && unsafe { *b_buf.add(j - 1) } == b' ' {
                    j -= 1;
                }
                if j == 0 || j >= cap {
                    return 0;
                }
                unsafe {
                    core::ptr::copy_nonoverlapping(b_buf, out, j);
                    *out.add(j) = 0;
                }
                return j;
            }
            last = open as usize;
            while last > 0 && unsafe { *b_buf.add(last - 1) } == b' ' {
                last -= 1;
            }
            if last == 0 {
                return 0;
            }
            if last > cap - 1 {
                last = cap - 1;
            }
            let mut j = 0usize;
            while j < last {
                unsafe { *out.add(j) = *b_buf.add(j) };
                j += 1;
            }
            unsafe { *out.add(j) = 0 };
            return j;
        }
        if kind == pm_jit_rsx_ast_kind::ARRAY {
            /* `[a, b, ..]` / `[e; n]` — element type from the first element,
             * sized for repeats only when the count is a plain literal. */
            let kids = unsafe { (*e).kids };
            let nk = unsafe { (*e).n_kids } as usize;
            if nk == 0 {
                return 0;
            }
            let is_repeat = unsafe { z_eq((*e).text, (*e).text_len, b"[;]\0".as_ptr()) };
            let elem = unsafe { *kids.add(0) };
            let e_buf = self.arena_tmp();
            let en = unsafe { self.expr_ctype(elem, e_buf, 128, locals) };
            if en == 0 {
                return 0;
            }
            if is_repeat && nk >= 2 {
                let cnt = unsafe { (*(*kids.add(1))).text };
                let cl = unsafe { (*(*kids.add(1))).text_len };
                let mut i = 0usize;
                let mut num: u64 = 0;
                let mut oknum = true;
                while i < cl {
                    let ch = unsafe { *cnt.add(i) };
                    if ch < b'0' || ch > b'9' {
                        oknum = false;
                        break;
                    }
                    num = num * 10 + (ch - b'0') as u64;
                    i += 1;
                }
                if oknum && num > 0 && num < 65536 {
                    /* `T[12]` — copy elem type, then bracket the count. */
                    let mut w = 0usize;
                    while w < en {
                        unsafe {
                            *out.add(w) = *e_buf.add(w);
                        }
                        w += 1;
                    }
                    let mut n2 = unsafe { zput(out, cap, w, b"[\0".as_ptr()) };
                    let digs = self.arena_tmp();
                    let mut dtmp = num;
                    let mut di = 0usize;
                    while dtmp > 0 && di < 32 {
                        unsafe {
                            *digs.add(di) = b'0' + (dtmp % 10) as u8;
                        }
                        dtmp /= 10;
                        di += 1;
                    }
                    if di == 0 {
                        unsafe {
                            *digs.add(0) = b'0';
                        }
                        di = 1;
                    }
                    let mut dj = di;
                    while dj > 0 {
                        dj -= 1;
                        unsafe {
                            *out.add(n2) = *digs.add(dj);
                        }
                        n2 += 1;
                    }
                    unsafe {
                        *out.add(n2) = b']';
                        *out.add(n2 + 1) = 0;
                    }
                    return if n2 + 1 >= cap { 0 } else { n2 + 1 };
                }
            }
            /* unsized: `T[]` */
            let mut w = 0usize;
            while w < en {
                unsafe {
                    *out.add(w) = *e_buf.add(w);
                }
                w += 1;
            }
            let e3 = unsafe { zput(out, cap, w, b"[]\0".as_ptr()) };
            return if e3 >= cap { 0 } else { e3 };
        }
        if kind == pm_jit_rsx_ast_kind::EXPR_STMT {
            /* statement wrapper in value position — the inner expression. */
            let kids = unsafe { (*e).kids };
            if unsafe { (*e).n_kids } >= 1 {
                return unsafe { self.expr_ctype(*kids.add(0), out, cap, locals) };
            }
            return 0;
        }
        if kind == pm_jit_rsx_ast_kind::PAREN {
            /* `(expr)` — the inner expression's type. */
            let kids = unsafe { (*e).kids };
            if unsafe { (*e).n_kids } >= 1 {
                return unsafe { self.expr_ctype(*kids.add(0), out, cap, locals) };
            }
            return 0;
        }
        if kind == pm_jit_rsx_ast_kind::MATCH {
            /* match-expression: first arm's body type (unwrapping
             * EXPR_STMT/BLOCK layers like the IF case). A struct-Option
             * scrutinee is a FALLBACK for arms whose body type is not yet
             * computable (`Some(v) => v` — the bind is declared only at
             * emission): every such arm returns v (payload) and `None`
             * arms diverge. Arms whose body IS typed (`Some(s) => s.heap`
             * — a field read) must win: the match's value is the body's
             * type, not the payload's. */
            {
                let kids = unsafe { (*e).kids };
                let nk = unsafe { (*e).n_kids } as usize;
                if nk >= 1 {
                    /* payload type of a struct-Option scrutinee: pre-registers
                     * the Some-bind's type so arm bodies can resolve it */
                    let scrut = unsafe { *kids.add(0) };
                    let sct = self.arena_tmp();
                    let scn = unsafe { self.expr_ctype(scrut, sct, 128, locals) };
                    let mut elem_buf: [u8; 96] = [0; 96];
                    let mut elem_len = 0usize;
                    if scn > 0 {
                        elem_len = unsafe { Lower::opt_typedef_elem(sct, scn, elem_buf.as_mut_ptr(), 96) };
                    }
                    let mut i = 1usize;
                    while i < nk {
                        let arm = unsafe { *kids.add(i) };
                        if unsafe { (*arm).kind } == pm_jit_rsx_ast_kind::MATCH_ARM
                            && unsafe { (*arm).n_kids } >= 2
                        {
                            let ak = unsafe { (*arm).kids };
                            let pat = unsafe { *ak.add(0) };
                            /* arm kids: [pat, body] or [pat, guard, body] */
                            let mut br = unsafe { *ak.add(unsafe { (*arm).n_kids } as usize - 1) };
                            let mut bk = unsafe { (*br).kind };
                            while bk == pm_jit_rsx_ast_kind::EXPR_STMT
                                && unsafe { (*br).n_kids } >= 1
                            {
                                br = unsafe { *(*br).kids.add(0) };
                                bk = unsafe { (*br).kind };
                            }
                            if bk == pm_jit_rsx_ast_kind::BLOCK {
                                let b2 = unsafe { self.block_tail_node(br) };
                                if !b2.is_null() {
                                    br = b2 as *mut pm_jit_rsx_ast_t;
                                }
                            }
                            if bk == pm_jit_rsx_ast_kind::RETURN
                                || bk == pm_jit_rsx_ast_kind::BREAK
                                || bk == pm_jit_rsx_ast_kind::CONTINUE
                            {
                                i += 1;
                                continue;
                            }
                            /* Some(bind): register the bind with the payload
                             * type for the body walk (emission declares it
                             * the same way — see emit_some_binds) */
                            let saved_n = unsafe { (*locals).n };
                            if elem_len > 0
                                && unsafe { (*pat).kind } == pm_jit_rsx_ast_kind::PATH
                            {
                                let pk = unsafe { (*pat).kids };
                                let pnk = unsafe { (*pat).n_kids } as usize;
                                if pnk >= 2 {
                                    let head = unsafe { *pk.add(0) };
                                    let bind = unsafe { *pk.add(1) };
                                    if unsafe { (*head).kind } == pm_jit_rsx_ast_kind::PATH
                                        && unsafe { z_eq(unsafe { (*head).text }, unsafe { (*head).text_len }, b"Some\0".as_ptr()) }
                                        && unsafe { (*bind).kind } == pm_jit_rsx_ast_kind::PATH
                                        && unsafe { (*bind).n_kids } >= 1
                                    {
                                        let bleaf = unsafe { *(*bind).kids.add(0) };
                                        unsafe {
                                            (*locals).add(
                                                unsafe { (*bleaf).text },
                                                unsafe { (*bleaf).text_len },
                                                elem_buf.as_ptr(),
                                                elem_len,
                                                self.depth + 1,
                                            );
                                        }
                                    }
                                }
                            }
                            let n = unsafe { self.expr_ctype(br, out, cap, locals) };
                            unsafe { (*locals).n = saved_n };
                            if n > 0 {
                                return n;
                            }
                        }
                        i += 1;
                    }
                }
            }
            {
                let kids0 = unsafe { (*e).kids };
                if unsafe { (*e).n_kids } as usize >= 1 {
                    let scrut = unsafe { *kids0.add(0) };
                    let sct = self.arena_tmp();
                    let scn = unsafe { self.expr_ctype(scrut, sct, 128, locals) };
                    if scn > 0 {
                        let eb = self.arena_tmp();
                        let eln = unsafe { Lower::opt_typedef_elem(sct, scn, eb, 160) };
                        if eln > 0 {
                            unsafe {
                                core::ptr::copy_nonoverlapping(eb, out, eln);
                                *out.add(eln) = 0;
                            }
                            return eln;
                        }
                    }
                }
            }
            return 0;
        }
        if kind == pm_jit_rsx_ast_kind::BINARY {
            /* comparisons yield bool; other operators keep the operand type
             * (first operand that has a known one wins). */
            let op = unsafe { (*e).text };
            let op_len = unsafe { (*e).text_len };
            let is_cmp = unsafe { z_eq(op, op_len, b"==\0".as_ptr()) }
                || unsafe { z_eq(op, op_len, b"!=\0".as_ptr()) }
                || unsafe { z_eq(op, op_len, b"<\0".as_ptr()) }
                || unsafe { z_eq(op, op_len, b">\0".as_ptr()) }
                || unsafe { z_eq(op, op_len, b"<=\0".as_ptr()) }
                || unsafe { z_eq(op, op_len, b">=\0".as_ptr()) };
            if is_cmp {
                let n2 = unsafe { zput(out, cap, 0, b"bool\0".as_ptr()) };
                return if n2 >= cap { 0 } else { n2 };
            }
            let kids = unsafe { (*e).kids };
            let nk = unsafe { (*e).n_kids } as usize;
            let mut i = 0usize;
            while i < nk && i < 2 {
                let tn = unsafe { self.expr_ctype(*kids.add(i), out, cap, locals) };
                if tn > 0 {
                    return tn;
                }
                i += 1;
            }
            return 0;
        }
        if kind == pm_jit_rsx_ast_kind::IF {
            /* if-expression: try each branch's value expression in order —
             * then-branch first, else-branch as fallback. A `void *`
             * branch (core::ptr::null_mut() rendered generically) is only a
             * LAST resort: the other arm of `cond ? null_mut() : typed`
             * carries the real pointee, and the let must be typed by it or
             * every later deref is a void operation. */
            let kids = unsafe { (*e).kids };
            let nk = unsafe { (*e).n_kids } as usize;
            if nk < 2 {
                return 0;
            }
            let mut void_at = 0usize;
            let mut void_len = 0usize;
            let mut ti = 1usize;
            while ti < nk && ti < 3 {
                let br = self.block_tail_node(unsafe { *kids.add(ti) });
                let n = unsafe { self.expr_ctype(br, out, cap, locals) };
                if n > 0 {
                    if n == 6 && unsafe { z_eq(out, n, b"void *\0".as_ptr()) } {
                        if void_len == 0 {
                            void_len = n;
                            void_at = ti;
                        }
                        ti += 1;
                        continue;
                    }
                    return n;
                }
                ti += 1;
            }
            if void_len > 0 {
                let n2 = unsafe { zput(out, cap, 0, b"void *\0".as_ptr()) };
                return if n2 >= cap { 0 } else { n2 };
            }
            return 0;
        }
        if kind == pm_jit_rsx_ast_kind::METHOD_CALL {
            /* `.add(k)` / `.sub(k)` keep the receiver's pointer type. */
            let kids = unsafe { (*e).kids };
            if unsafe { (*e).n_kids } >= 3 {
                let name = unsafe { *kids.add(1) };
                let mname = unsafe { (*name).text };
                let mlen = unsafe { (*name).text_len };
                let mut user_method = false;
                if unsafe { z_eq(mname, mlen, b"add\0".as_ptr()) }
                    || unsafe { z_eq(mname, mlen, b"sub\0".as_ptr()) }
                    || unsafe { z_eq(mname, mlen, b"wrapping_mul\0".as_ptr()) }
                    || unsafe { z_eq(mname, mlen, b"wrapping_add\0".as_ptr()) }
                    || unsafe { z_eq(mname, mlen, b"wrapping_sub\0".as_ptr()) }
                    || unsafe { z_eq(mname, mlen, b"wrapping_shl\0".as_ptr()) }
                    || unsafe { z_eq(mname, mlen, b"wrapping_shr\0".as_ptr()) }
                {
                    /* pointer/integer arithmetic — but a method of the
                     * same name on a known struct type wins (SymTab::add).
                     * A POINTER receiver (rendered type ends `*`) is always
                     * pointer arithmetic: C has no methods on a pointer
                     * value, and ptr-to-struct receivers are common in the
                     * kernel discipline. */
                    let rbuf = self.arena_tmp();
                    let rn = unsafe { self.expr_ctype(*kids.add(0), rbuf, 128, locals) };
                    let mut is_ptr = false;
                    if rn > 0 && unsafe { *rbuf.add(rn - 1) } == b'*' {
                        is_ptr = true;
                    }
                    if rn > 0 && !is_ptr {
                        let mut j = rn;
                        while j > 0 && unsafe { *rbuf.add(j - 1) } == b' ' {
                            j -= 1;
                        }
                        let mut b0 = 0usize;
                        if j >= 6
                            && unsafe { *rbuf.add(0) } == b'c'
                            && unsafe { *rbuf.add(1) } == b'o'
                            && unsafe { *rbuf.add(2) } == b'n'
                            && unsafe { *rbuf.add(3) } == b's'
                            && unsafe { *rbuf.add(4) } == b't'
                            && unsafe { *rbuf.add(5) } == b' '
                        {
                            b0 = 6;
                        }
                        if j > b0
                            && unsafe { (*self.syms).find(rbuf.add(b0), j - b0) } < SYM_CAP
                        {
                            /* the receiver names a struct with methods —
                             * fall through to the user-method return lookup */
                            user_method = true;
                        }
                    }
                    if !user_method {
                        return unsafe { self.expr_ctype(*kids.add(0), out, cap, locals) };
                    }
                }
                if !user_method && unsafe { z_eq(mname, mlen, b"len\0".as_ptr()) } {
                    let n2 = unsafe { zput(out, cap, 0, b"size_t\0".as_ptr()) };
                    return if n2 >= cap { 0 } else { n2 };
                }
                /* `.get()` on a transparent newtype/UnsafeCell — Rust's
                 * `UnsafeCell::get()` returns `*mut T`; in C the receiver IS
                 * the value, so the result type is a pointer to it. */
                if !user_method
                    && unsafe { z_eq(mname, mlen, b"get\0".as_ptr()) }
                    && unsafe { (*(*kids.add(2))).n_kids } as usize == 0
                {
                    let n2 = unsafe { self.expr_ctype(*kids.add(0), out, cap - 2, locals) };
                    if n2 > 0 && n2 + 2 < cap {
                        unsafe {
                            *out.add(n2) = b' ';
                            *out.add(n2 + 1) = b'*';
                            *out.add(n2 + 2) = 0;
                        }
                        return n2 + 2;
                    }
                    return 0;
                }
                /* closure-loop builtins and range predicates — fixed types */
                if !user_method
                    && (unsafe { z_eq(mname, mlen, b"all\0".as_ptr()) }
                        || unsafe { z_eq(mname, mlen, b"any\0".as_ptr()) }
                        || unsafe { z_eq(mname, mlen, b"contains\0".as_ptr()) })
                {
                    let n2 = unsafe { zput(out, cap, 0, b"bool\0".as_ptr()) };
                    return if n2 >= cap { 0 } else { n2 };
                }
                if !user_method && unsafe { z_eq(mname, mlen, b"position\0".as_ptr()) } {
                    let n2 = unsafe { zput(out, cap, 0, b"size_t\0".as_ptr()) };
                    return if n2 >= cap { 0 } else { n2 };
                }
                /* `.div_ceil(d)` / `.unwrap_or(x)` keep the receiver's
                 * integer type (position's unwrap_or is size_t either way). */
                if !user_method
                    && (unsafe { z_eq(mname, mlen, b"div_ceil\0".as_ptr()) }
                        || unsafe { z_eq(mname, mlen, b"unwrap_or\0".as_ptr()) })
                {
                    return unsafe { self.expr_ctype(*kids.add(0), out, cap, locals) };
                }
                /* `.saturating_mul(d)` / `.saturating_sub(d)` / `.min(d)` —
                 * receiver-type integers (see emit_method_call). */
                if !user_method
                    && (unsafe { z_eq(mname, mlen, b"saturating_mul\0".as_ptr()) }
                        || unsafe { z_eq(mname, mlen, b"saturating_sub\0".as_ptr()) }
                        || unsafe { z_eq(mname, mlen, b"min\0".as_ptr()) }
                        || unsafe { z_eq(mname, mlen, b"max\0".as_ptr()) })
                {
                    return unsafe { self.expr_ctype(*kids.add(0), out, cap, locals) };
                }
                /* `.cast::<T>()` — the turbofish type (4th kid, TYPE node):
                 * a pointer to it. */
                if !user_method && unsafe { z_eq(mname, mlen, b"cast\0".as_ptr()) } {
                    if unsafe { (*e).n_kids } as usize >= 4 {
                        let gty = unsafe { *kids.add(3) };
                        let gbuf = self.arena_tmp();
                        let gn = unsafe { self.ctype(gty, gbuf, 128) };
                        if gn > 0 {
                            let mut i3 = 0usize;
                            while i3 < gn {
                                unsafe {
                                    *out.add(i3) = *gbuf.add(i3);
                                }
                                i3 += 1;
                            }
                            unsafe {
                                *out.add(i3) = b' ';
                                *out.add(i3 + 1) = b'*';
                            }
                            return if i3 + 2 >= cap { 0 } else { i3 + 2 };
                        }
                    }
                    return 0;
                }
                /* `.as_ptr()` / `.as_mut_ptr()` — pointer to the first
                 * element: `T[N]` -> `T *`, non-arrays pass through. */
                if unsafe { z_eq(mname, mlen, b"as_ptr\0".as_ptr()) }
                    || unsafe { z_eq(mname, mlen, b"as_mut_ptr\0".as_ptr()) }
                {
                    let rbuf = self.arena_tmp();
                    let rn = unsafe { self.expr_ctype(*kids.add(0), rbuf, 128, locals) };
                    if rn == 0 {
                        return 0;
                    }
                    /* `T [N]` -> the element spelling before the bracket,
                     * then a pointer to it. */
                    let mut j = rn;
                    while j > 0 && unsafe { *rbuf.add(j - 1) } != b'[' {
                        j -= 1;
                    }
                    if j == 0 {
                        /* pointer-to-array receiver (`T (*)[N]`): `.as_ptr()`
                         * is the first element — the spelling before the
                         * ` (*)` group plus a star. The generic scan below
                         * would split at the wrong bracket and emit garbage. */
                        let pp = unsafe { self.parr_declarator(rbuf, rn) };
                        if pp != usize::MAX {
                            let mut last = pp;
                            while last > 0 && unsafe { *rbuf.add(last - 1) } == b' ' {
                                last -= 1;
                            }
                            if last == 0 || last + 2 >= cap {
                                return 0;
                            }
                            let mut i2 = 0usize;
                            while i2 < last {
                                unsafe {
                                    *out.add(i2) = *rbuf.add(i2);
                                }
                                i2 += 1;
                            }
                            unsafe {
                                *out.add(last) = b' ';
                                *out.add(last + 1) = b'*';
                                *out.add(last + 2) = 0;
                            }
                            return last + 2;
                        }
                        /* not an array — already a pointer (or a pointer-y
                         * value): re-starring it would double the star. */
                        let mut i3 = 0usize;
                        while i3 < rn {
                            unsafe {
                                *out.add(i3) = *rbuf.add(i3);
                            }
                            i3 += 1;
                        }
                        if rn < cap {
                            unsafe { *out.add(rn) = 0 };
                        }
                        return rn;
                    }
                    let mut w = j - 1;
                    {
                        let mut w2 = j - 1;
                        while w2 > 0 && unsafe { *rbuf.add(w2 - 1) } == b' ' {
                            w2 -= 1;
                        }
                        w = w2;
                    }
                    if w == 0 {
                        return 0;
                    }
                    /* multi-dimensional receiver (`T [D1] [D2]` — e.g. the
                     * tuple table `[[u8;64]; N]`): the element is the inner
                     * array, so the pointer is `T (*)[D1]` — strip the
                     * outer group, keep the inner ones behind a `(*)`.
                     * The generic one-dim path below would emit
                     * `T [D1] *`, which is not a C declarator at all. */
                    {
                        let mut g1 = w;
                        let mut has_inner = false;
                        while g1 > 0 {
                            if unsafe { *rbuf.add(g1 - 1) } == b']' {
                                has_inner = true;
                                break;
                            }
                            g1 -= 1;
                        }
                        if has_inner {
                            /* k lands one PAST the inner group's `[` (the scan
                             * tests k-1) — the group span is [k-1, g1). */
                            let mut k = g1;
                            while k > 0 && unsafe { *rbuf.add(k - 1) } != b'[' {
                                k -= 1;
                            }
                            if k == 0 {
                                return 0;
                            }
                            k -= 1;
                            let mut e2 = k;
                            while e2 > 0 && unsafe { *rbuf.add(e2 - 1) } == b' ' {
                                e2 -= 1;
                            }
                            if e2 == 0 || e2 + 5 >= cap {
                                return 0;
                            }
                            let mut i4 = 0usize;
                            while i4 < e2 {
                                unsafe {
                                    *out.add(i4) = *rbuf.add(i4);
                                }
                                i4 += 1;
                            }
                            unsafe {
                                *out.add(e2) = b' ';
                                *out.add(e2 + 1) = b'(';
                                *out.add(e2 + 2) = b'*';
                                *out.add(e2 + 3) = b')';
                            }
                            let mut w3 = e2 + 4;
                            let mut r2 = k;
                            while r2 < g1 && w3 < cap - 1 {
                                unsafe {
                                    *out.add(w3) = *rbuf.add(r2);
                                }
                                w3 += 1;
                                r2 += 1;
                            }
                            unsafe {
                                *out.add(w3) = 0;
                            }
                            return w3;
                        }
                    }
                    let mut i2 = 0usize;
                    while i2 < w {
                        unsafe {
                            *out.add(i2) = *rbuf.add(i2);
                        }
                        i2 += 1;
                    }
                    unsafe {
                        *out.add(w) = b' ';
                        *out.add(w + 1) = b'*';
                        *out.add(w + 2) = 0;
                    }
                    w += 2;
                    return w;
                }
                /* `.is_null()` / `.is_some()` / `.is_none()` — bool */
                if unsafe { z_eq(mname, mlen, b"is_null\0".as_ptr()) }
                    || unsafe { z_eq(mname, mlen, b"is_some\0".as_ptr()) }
                    || unsafe { z_eq(mname, mlen, b"is_none\0".as_ptr()) }
                {
                    let n2 = unsafe { zput(out, cap, 0, b"bool\0".as_ptr()) };
                    return if n2 >= cap { 0 } else { n2 };
                }
                /* user-defined method: `Type_m`'s registered return type */
                let tbuf = self.arena_tmp();
                let tn = unsafe { self.expr_ctype(*kids.add(0), tbuf, 128, locals) };
                if tn > 0 {
                    let mut j = tn;
                    while j > 0 && unsafe { *tbuf.add(j - 1) } == b' ' {
                        j -= 1;
                    }
                    if j > 0 && unsafe { *tbuf.add(j - 1) } == b'*' {
                        j -= 1;
                        while j > 0 && unsafe { *tbuf.add(j - 1) } == b' ' {
                            j -= 1;
                        }
                    }
                    let mut b0 = 0usize;
                    if j >= 6
                        && unsafe { *tbuf.add(0) } == b'c'
                        && unsafe { *tbuf.add(1) } == b'o'
                        && unsafe { *tbuf.add(2) } == b'n'
                        && unsafe { *tbuf.add(3) } == b's'
                        && unsafe { *tbuf.add(4) } == b't'
                        && unsafe { *tbuf.add(5) } == b' '
                    {
                        b0 = 6;
                    }
                    if j > b0 && unsafe { (*self.syms).find(tbuf.add(b0), j - b0) } < SYM_CAP {
                        let mbuf = self.arena_tmp();
                        let mn = unsafe { bput(mbuf, 160, 0, tbuf.add(b0), j - b0) };
                        let mn2 = unsafe { bput(mbuf, 160, mn, b"_\0".as_ptr(), 1) };
                        let mn3 = unsafe { bput(mbuf, 160, mn2, mname, mlen) };
                        unsafe {
                            *mbuf.add(mn3) = 0;
                        }
                        let rn = unsafe { (*self.fns).ret_ctype(mbuf, mn3, out) };
                        if rn > 0 {
                            return rn;
                        }
                    }
                }
            }
            return 0;
        }
        0
    }

    /* Primitive Rust spelling -> C type; 0 when not a primitive. */
    unsafe fn prim_ctype(&mut self, s: *const u8, n: usize, out: *mut u8, cap: usize) -> usize {
        if unsafe { z_eq(s, n, b"c_void\0".as_ptr()) } {
            /* core::ffi::c_void (the leaf of the path) and a bare
             * `c_void` after `use core::ffi::c_void` — same void. */
            return unsafe { zput(out, cap, 0, b"void\0".as_ptr()) };
        }
        if unsafe { z_eq(s, n, b"f32\0".as_ptr()) } {
            return unsafe { zput(out, cap, 0, b"float\0".as_ptr()) };
        }
        if unsafe { z_eq(s, n, b"f64\0".as_ptr()) } {
            return unsafe { zput(out, cap, 0, b"double\0".as_ptr()) };
        }
        if unsafe { z_eq(s, n, b"bool\0".as_ptr()) } {
            return unsafe { zput(out, cap, 0, b"bool\0".as_ptr()) };
        }
        if unsafe { z_eq(s, n, b"char\0".as_ptr()) } {
            return unsafe { zput(out, cap, 0, b"uint32_t\0".as_ptr()) };
        }
        /* AtomicU32 -> _Atomic uint32_t: the field is a C11 atomic;
         * loads/stores/swaps go through the __atomic_* builtins
         * (see emit_method_call), never plain access. */
        if unsafe { z_eq(s, n, b"AtomicU32\0".as_ptr()) } {
            return unsafe { zput(out, cap, 0, b"_Atomic uint32_t\0".as_ptr()) };
        }
        unsafe { self.suffix_ctype(s, n, out, cap) }
    }

    unsafe fn suffix_ctype(&mut self, s: *const u8, n: usize, out: *mut u8, cap: usize) -> usize {
        if unsafe { z_eq(s, n, b"u8\0".as_ptr()) } {
            return unsafe { zput(out, cap, 0, b"uint8_t\0".as_ptr()) };
        }
        if unsafe { z_eq(s, n, b"u16\0".as_ptr()) } {
            return unsafe { zput(out, cap, 0, b"uint16_t\0".as_ptr()) };
        }
        if unsafe { z_eq(s, n, b"u32\0".as_ptr()) } {
            return unsafe { zput(out, cap, 0, b"uint32_t\0".as_ptr()) };
        }
        if unsafe { z_eq(s, n, b"u64\0".as_ptr()) } {
            return unsafe { zput(out, cap, 0, b"uint64_t\0".as_ptr()) };
        }
        if unsafe { z_eq(s, n, b"usize\0".as_ptr()) } {
            return unsafe { zput(out, cap, 0, b"size_t\0".as_ptr()) };
        }
        if unsafe { z_eq(s, n, b"i8\0".as_ptr()) } {
            return unsafe { zput(out, cap, 0, b"int8_t\0".as_ptr()) };
        }
        if unsafe { z_eq(s, n, b"i16\0".as_ptr()) } {
            return unsafe { zput(out, cap, 0, b"int16_t\0".as_ptr()) };
        }
        if unsafe { z_eq(s, n, b"i32\0".as_ptr()) } {
            return unsafe { zput(out, cap, 0, b"int32_t\0".as_ptr()) };
        }
        if unsafe { z_eq(s, n, b"i64\0".as_ptr()) } {
            return unsafe { zput(out, cap, 0, b"int64_t\0".as_ptr()) };
        }
        if unsafe { z_eq(s, n, b"isize\0".as_ptr()) } {
            return unsafe { zput(out, cap, 0, b"intptr_t\0".as_ptr()) };
        }
        0
    }
}

/* Fixed-size local variable table for one function body. */
const LOCAL_CAP: usize = 256;

struct LocalTab {
    names: [[u8; 48]; LOCAL_CAP],
    name_lens: [usize; LOCAL_CAP],
    /* Local C types are arena spans (NUL-terminated copies), not inline
     * arrays: tuple/Option typedef names are unbounded by any fixed slot
     * — a span grows to the name, exactly like SymTab's field ctypes. */
    ctypes: [*const u8; LOCAL_CAP],
    ctype_lens: [usize; LOCAL_CAP],
    depths: [usize; LOCAL_CAP],
    epochs: [usize; LOCAL_CAP],
    marks: [usize; 64],
    nmarks: usize,
    n: usize,
    epoch: usize,
    /* arena for the span copies (LocalTab is arena-resident). */
    arena: *mut pm_util_mem_arena_t,
    /* set the moment a span allocation refuses — the owning Lower reads
     * it after each fn body so an OOM is a refusal, never a silent
     * unknown-type local. */
    oom: bool,
}

impl LocalTab {
    /* Arena-resident: one table per fn body (and one per static
     * initializer) is ~23 KiB — by-value construction parked that whole
     * struct on the native stack for the duration of the body. The block
     * is zeroed so every slot starts empty; the arena owns the lifetime
     * (the body's span), so no free — scoped reuse is the marks/epochs
     * machinery above. NULL = arena exhausted; callers must refuse. */
    unsafe fn new(arena: *mut pm_util_mem_arena_t) -> *mut LocalTab {
        let p = unsafe { pm_util_mem_alloc(arena, core::mem::size_of::<LocalTab>()) } as *mut LocalTab;
        if p.is_null() {
            return p;
        }
        unsafe {
            let zb = p as *mut u8;
            let n = core::mem::size_of::<LocalTab>();
            let mut i = 0usize;
            while i < n {
                *zb.add(i) = 0;
                i += 1;
            }
            (*p).arena = arena;
        }
        p
    }

    /* Each C block that opens ({) is a fresh scope — reuse of a shadowed
     * spelling is only legal inside the very block that declared it.
     * A scope pushes its entry count on entry and truncates back to it on
     * exit, so sibling blocks (then/else, match arms, `*`/`&` branches)
     * never alias each other's declarations. */
    unsafe fn note_scope(&mut self) {
        self.epoch += 1;
        if self.nmarks < 64 {
            self.marks[self.nmarks] = self.n;
            self.nmarks += 1;
        }
    }

    /* Leave the innermost scope: its locals die with its closing brace. */
    unsafe fn drop_scope(&mut self) {
        if self.nmarks > 0 {
            self.nmarks -= 1;
            self.n = self.marks[self.nmarks];
        }
    }

    unsafe fn add(
        &mut self,
        name: *const u8,
        nlen: usize,
        ct: *const u8,
        ctlen: usize,
        depth: usize,
    ) {
        if self.n >= LOCAL_CAP || nlen >= 48 || ct.is_null() || ctlen == 0 {
            return;
        }
        /* Reuse guard BEFORE the span allocation: a same-scope shadowing
         * entry with the identical C type reuses the existing row — the
         * arena span of a dropped speculative registration is never freed,
         * so re-registering every reassignment of one local would grow the
         * arena monotonically. Only genuinely new bindings allocate. */
        let mut s = self.n;
        while s > 0 {
            s -= 1;
            if self.name_lens[s] == nlen
                && self.depths[s] == depth
                && self.epochs[s] == self.epoch
                && unsafe { self.span_eq(s, name, nlen) }
                && self.ctype_lens[s] == ctlen
            {
                let sp = self.ctypes[s];
                let mut j = 0usize;
                let mut eq = true;
                while j < ctlen {
                    if unsafe { *sp.add(j) } != unsafe { *ct.add(j) } {
                        eq = false;
                        break;
                    }
                    j += 1;
                }
                if eq {
                    return;
                }
            }
        }
        /* span copy: no fixed ctype slot bound (a tuple/Option typedef name
         * can exceed any inline cap); NULL span drops the entry, lookup
         * then reports unknown — the caller's ascription refusal. */
        let p = unsafe { pm_util_mem_alloc(self.arena, ctlen + 1) };
        if p.is_null() {
            /* immediate, specific: the compile is dead, not degraded —
             * callers that ignore lookup's 0 would emit untyped locals. */
            self.oom = true;
            return;
        }
        unsafe {
            core::ptr::copy_nonoverlapping(ct, p, ctlen);
            *p.add(ctlen) = 0;
        }
        let sp = p;
        let s = self.n;
        let mut i = 0usize;
        while i < nlen {
            self.names[s][i] = unsafe { *name.add(i) };
            i += 1;
        }
        /* NUL-terminate: callers compare the stored spelling as a
         * C string (z_eq's z side reads until the terminator). */
        self.names[s][nlen] = 0;
        self.name_lens[s] = nlen;
        self.ctypes[s] = sp;
        self.ctype_lens[s] = ctlen;
        self.depths[s] = depth;
        self.epochs[s] = self.epoch;
        self.n += 1;
    }

    unsafe fn lookup(&self, name: *const u8, nlen: usize, out: *mut u8) -> usize {
        /* Names shadow: sibling blocks stack lets with the same spelling in
         * one flat tab, so the LAST entry is the innermost binding. */
        let mut i = self.n;
        while i > 0 {
            i -= 1;
            if self.name_lens[i] == nlen && unsafe { self.span_eq(i, name, nlen) } {
                let cl = self.ctype_lens[i];
                let src = self.ctypes[i];
                let mut j = 0usize;
                while j < cl {
                    unsafe {
                        *out.add(j) = *src.add(j);
                    }
                    j += 1;
                }
                unsafe {
                    *out.add(cl) = 0;
                }
                return cl;
            }
        }
        0
    }

    /* Byte compare of a stored spelling against a source span — both are
     * length-known, neither side is a C string. */
    unsafe fn span_eq(&self, s: usize, p: *const u8, n: usize) -> bool {
        let mut j = 0usize;
        while j < n {
            if self.names[s][j] != unsafe { *p.add(j) } {
                return false;
            }
            j += 1;
        }
        true
    }

    /* Shadowing with a different type needs a fresh C variable; the same
     * spelling can only be reused when the type is identical AND the prior
     * binding lives in the innermost open scope (the marks stack top). */
    unsafe fn same_type_same_scope(
        &self,
        name: *const u8,
        nlen: usize,
        ct: *const u8,
        ctlen: usize,
        depth: usize,
        _epoch: usize,
    ) -> bool {
        /* only entries above the innermost scope's mark can be reused —
         * siblings that came and went are already truncated away, and the
         * mark wall keeps this search inside the live block. */
        let mut floor = 0usize;
        if self.nmarks > 0 {
            floor = self.marks[self.nmarks - 1];
        }
        let mut i = self.n;
        while i > floor {
            i -= 1;
            if self.name_lens[i] == nlen
                && self.ctype_lens[i] == ctlen
                && self.depths[i] == depth
                && unsafe { self.span_eq(i, name, nlen) }
            {
                let mut j = 0usize;
                let sp = self.ctypes[i];
                while j < ctlen {
                    if unsafe { *sp.add(j) } != unsafe { *ct.add(j) } {
                        return false;
                    }
                    j += 1;
                }
                return true;
            }
        }
        false
    }
}

