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
    /* rename-shadow counter: each type-changing `let` in one scope picks
     * the next name__N spelling; per-fn (reset in lower_fn) — the C
     * collision domain is the fn body. */
    shadow_ctr: usize,
    /* the fn body currently being lowered (NULL outside a body) — the
     * Vec::new() let-inference scans it for the binding's first .push()
     * to type an empty container from its uses. */
    cur_body: *const pm_jit_rsx_ast_t,
    /* the body pre-scan's scratch pool (body_intern_types): a fresh
     * arena_tmp per TYPE node — and per nested generic arm inside one
     * ctype render — burns arena bytes linear in the unit's
     * ascription/cast count, and the compile shares its arena with
     * everything else (ksweep's one backing for the whole tree). While
     * pre_mode is on, arena_tmp carves 160-byte slices from this pool
     * first (a ctype render holds at most a few outstanding tmps — the
     * container arm's inner+typedef pair, the INDEX arm's base — so
     * four slots cover; a deeper render falls back to the arena and
     * stays correct, just not free). The pool resets per TYPE node. */
    pre_mode: bool,
    pre_pool: [u8; 640],
    pre_pool_at: usize,
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
    /* The in-progress match's scrutinee C type (emit_match sets it;
     * emit_pat_test's string-literal arms compare views, not scalars).
     * len 0 = no match in progress. */
    cur_scrut: [u8; 128],
    cur_scrut_len: usize,
    /* rsx_strpair_t (the (&str,&str) split_once payload row) is used this
     * unit — its typedef flushes with the str_ref preamble. */
    strpair_used: bool,
    /* Struct-shaped Option payload spellings seen this unit (each renders
     * as the named typedef rsx_opt_<elem>, emitted once in the preamble —
     * an inline `struct { T _v; bool _has; }` at each use site would be a
     * fresh anonymous type per site, incompatible across declarations). */
    opt_elems: [[u8; 128]; OPT_CAP],
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
    /* Trait-object plane (dyn dispatch): declared traits, their method
     * slots and rendered fn-ptr sigs. All-zero is a valid empty table,
     * so the arena-zeroed construction needs no init. */
    traits: TraitTab,
    /* trait impl pairs seen this unit: (self type, trait name) — the
     * vtable initializer of `impl T for S` and the coercion of `&mut S`
     * to `TraitT *` both consult it. */
    ti_self: [[u8; 48]; TI_CAP],
    ti_self_lens: [usize; TI_CAP],
    ti_trait: [[u8; 48]; TI_CAP],
    ti_trait_lens: [usize; TI_CAP],
    ti_n: usize,
    /* Vec container plane: interned element spellings -> named C types,
     * emitted once in the preamble. All-zero is a valid empty table. */
    vecs: VecTab,
    /* &[T] slice-ref rows (rsx_arr_<row>) — see ArrTab in 30-tables. */
    arrs: ArrTab,
    /* Mutex<T>/SpinLock<T> plane: interned payload spellings -> named C
     * rows { pm_util_lock_t raw; T value; }, same emission contract. */
    locks: LockTab,
    /* &str fat-reference plane: `&str` renders as the by-value struct
     * rsx_str_ref_t { const uint8_t *p; size_t n; } — ABI-shaped like
     * cargo's own (ptr,len) pair, so .len()/.as_ptr() lower to field
     * selects instead of refusing. The typedef emits once per unit. */
    str_ref_used: bool,
    str_ref_done: bool,
    /* owned-String plane: `String` renders as the by-value struct
     * rsx_str_t { char *p; size_t n, cap; } (p NULL, n 0 when empty —
     * the empty ctor is the {0} compound literal). Methods lower to
     * calls into the unit-static ops block str_own_emit_rest emits
     * (push_str/append/clone/eq/…), the same contract as the Vec rows.
     * One shared type: String is monomorphic, no row interning. */
    str_own_used: bool,
    str_own_done: bool,
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
            (*p).arrs = ArrTab::new();
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
                    if i + 1 < self.errcap {
                        unsafe { *self.errbuf.add(i) = b'\'' };
                        i += 1;
                        if i + 3 < self.errcap {
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
                    }
                    /* the buffer ALWAYS ends NUL-terminated, even when the
                     * message filled it — an unterminated errbuf reads
                     * past errcap in every downstream printer */
                    if i < self.errcap {
                        unsafe { *self.errbuf.add(i) = 0 };
                    } else {
                        unsafe { *self.errbuf.add(self.errcap - 1) = 0 };
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

    /* Emit a return value expression: when the fn's return type is the
     * fat `&str` (rsx_str_ref_t) and the value is a string literal, the
     * literal becomes the compound (rsx_str_ref_t){ ptr, len } — a C
     * char[] never converts to the struct implicitly. All other shapes
     * emit unchanged. */
    unsafe fn emit_ret_value(&mut self, v: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        if !v.is_null()
            && unsafe { (*v).kind } == pm_jit_rsx_ast_kind::LITERAL
            && self.cur_ret_len == 13
            && unsafe { z_eq(self.cur_ret.as_ptr(), 13, b"rsx_str_ref_t\0".as_ptr()) }
        {
            let t = unsafe { (*v).text };
            let tl = unsafe { (*v).text_len };
            if tl > 0 && !t.is_null() && (unsafe { *t } == b'"' || (unsafe { *t } == b'b' && tl > 1 && unsafe { *t.add(1) } == b'"')) {
                self.out.puts(b"(rsx_str_ref_t){ (const uint8_t *)\0".as_ptr());
                self.out.put(t, tl);
                self.out.puts(b", sizeof(\0".as_ptr());
                self.out.put(t, tl);
                self.out.puts(b") - 1 }\0".as_ptr());
                return;
            }
        }
        unsafe { self.emit_expr(v, locals) };
    }

    /* ---- vec! macro expansion ----
     *
     * `vec![E; N]` / `vec![a, b, ..]` (any `alloc::`/`::alloc::` prefix)
     * parse as one MACRO node carrying the raw invocation text. The elem
     * type + count are inside that text, so the expansion sub-lexes and
     * sub-parses the body — a real parse, the same grammar, never a
     * hand-rolled text scan. Results:
     *   out_row: the interned VecTab row for the element type
     *   out_count: the repeat form's count AST (repeat), or the list
     *              form's element ASTs in elems[0..out_list_n]
     *   out_list_n: >0 for the list form, 0 for the repeat form
     * Returns true on a recognized `vec!` (even if its body then refuses),
     * false when the macro is something else. */
    unsafe fn vec_macro_scan(
        &mut self,
        text: *const u8,
        tlen: usize,
        locals: *mut LocalTab,
        out_row: *mut usize,
        out_count: *mut *mut pm_jit_rsx_ast_t,
        out_elems: *mut *mut pm_jit_rsx_ast_t,
        out_list_n: *mut usize,
    ) -> bool {
        /* strip `::alloc::` / `alloc::` before `vec!` */
        let mut sp = text;
        let mut sl = tlen;
        if sl >= 9
            && unsafe { *sp == b':' }
            && unsafe { *sp.add(1) == b':' }
            && unsafe { z_eq(sp.add(2), 6, b"alloc:\0".as_ptr()) }
        {
            sp = unsafe { sp.add(9) };
            sl -= 9;
        } else if sl >= 7 && unsafe { z_eq(sp, 6, b"alloc:\0".as_ptr()) } {
            sp = unsafe { sp.add(7) };
            sl -= 7;
        }
        /* `vec![` — raw byte compare on the invocation text */
        if sl < 6 {
            return false;
        }
        if unsafe { *sp } != b'v'
            || unsafe { *sp.add(1) } != b'e'
            || unsafe { *sp.add(2) } != b'c'
            || unsafe { *sp.add(3) } != b'!'
            || unsafe { *sp.add(4) } != b'['
        {
            return false;
        }
        /* body: [sp+5, sl-1) — `vec![` is 5 bytes; the invocation's LAST
         * byte is the closing `]` (the lexer emits the whole invocation
         * as one balanced token, so nothing can trail it) */
        if unsafe { *sp.add(sl - 1) } != b']' {
            return false;
        }
        let body = unsafe { sp.add(5) };
        let blen = sl - 6;
        /* sub-lex the body */
        let mut toks: pm_jit_rsx_toklist_t = pm_jit_rsx_toklist_t {
            toks: core::ptr::null_mut(),
            n_toks: 0,
        };
        if unsafe {
            pm_metal_jit_rsx_lex(
                self.arena,
                body,
                blen,
                &mut toks,
                self.errbuf,
                self.errcap,
            )
        } != 0
        {
            return true;
        }
        if toks.n_toks == 0 {
            unsafe {
                self.err(b"unsupported: empty vec!\0".as_ptr(), 0);
            }
            return true;
        }
        let mut p = Parser {
            arena: self.arena,
            toks: toks.toks,
            n_toks: toks.n_toks,
            at: 0,
            nd: Node {
                arena: self.arena,
                errbuf: self.errbuf,
                errcap: self.errcap,
                errline: 0,
                ok: true,
            },
            ok: true,
            cond_ctx: false,
            chain_ctx: false,
            shr_closes: 0,
            feats: core::ptr::null(),
        };
        /* element expression first */
        let elem = unsafe { p.parse_expr() };
        if !p.ok || elem.is_null() {
            unsafe {
                self.err(b"unsupported: vec! element\0".as_ptr(), 0);
            }
            return true;
        }
        /* repeat form: `; count` after the element */
        if unsafe { p.is_punct(p.at, b';') } {
            p.at += 1;
            let count = unsafe { p.parse_expr() };
            if !p.ok || count.is_null() {
                unsafe {
                    self.err(b"unsupported: vec! count\0".as_ptr(), 0);
                }
                return true;
            }
            /* intern the row on the element's C type */
            let eb = self.arena_tmp();
            let en = unsafe { self.expr_ctype(elem, eb, 128, locals) };
            if en == 0 {
                unsafe {
                    self.err(b"unsupported: vec! element type\0".as_ptr(), 0);
                }
                return true;
            }
            let row = unsafe { self.vecs.intern(eb, en) };
            unsafe {
                *out_row = row;
                *out_count = count;
                *out_list_n = 0;
                /* the repeat form needs E's AST too — vec![7u8; n] fills,
                 * not zeroes (calloc only covers E == 0) */
                *out_elems = elem;
            }
            return true;
        }
        /* list form: `, expr`* — elements typed individually (all must
         * agree; the first wins and mismatches surface as C errors). A
         * trailing comma is idiomatic Rust — the list ends at the final
         * `,` when nothing follows (the sub-parse sits on the END
         * sentinel by then). */
        let mut list: [*mut pm_jit_rsx_ast_t; 16] = [core::ptr::null_mut(); 16];
        let mut list_n = 0usize;
        let first = elem;
        let mut any = first;
        loop {
            if list_n < 16 {
                list[list_n] = any;
                list_n += 1;
            } else {
                unsafe {
                    self.err(b"unsupported: vec! list too long\0".as_ptr(), 0);
                }
                return true;
            }
            if !unsafe { p.is_punct(p.at, b',') } {
                break;
            }
            p.at += 1;
            /* trailing comma: `,` then END — the list is closed */
            if unsafe { p.kind(p.at) } == pm_jit_rsx_tok_kind::END {
                break;
            }
            any = unsafe { p.parse_expr() };
            if !p.ok || any.is_null() {
                unsafe {
                    self.err(b"unsupported: vec! element list\0".as_ptr(), 0);
                }
                return true;
            }
        }
        /* the lexer pushes a trailing END sentinel and counts it in
         * n_toks; "fully consumed" means the cursor sits on END (the
         * parser clamps out-of-range to that same sentinel) */
        if unsafe { p.kind(p.at) } != pm_jit_rsx_tok_kind::END {
            unsafe {
                self.err(b"unsupported: vec! trailing tokens\0".as_ptr(), 0);
            }
            return true;
        }
        let eb = self.arena_tmp();
        let en = unsafe { self.expr_ctype(first, eb, 128, locals) };
        if en == 0 {
            unsafe {
                self.err(b"unsupported: vec! element type\0".as_ptr(), 0);
            }
            return true;
        }
        let row = unsafe { self.vecs.intern(eb, en) };
        unsafe {
            let mut k = 0usize;
            while k < list_n {
                *out_elems.add(k) = list[k];
                k += 1;
            }
            *out_row = row;
            *out_count = core::ptr::null_mut();
            *out_list_n = list_n;
        }
        true
    }

    /* `format!` — the string-interpolation macro. The invocation arrives
     * as one MACRO node carrying the raw text; the body sub-parses with
     * the same grammar as any expression (never a hand-rolled scan of
     * the argument expressions). Results:
     *   out_fmt: inner bytes of the format literal (quotes stripped)
     *   out_args: positional argument ASTs (`{}` holes, in order)
     *   out_caps: implicit-capture ASTs (`{ident}` holes, in order)
     * Emission walks out_fmt: literal bytes pass through, `{ident}`
     * pulls the next cap, `{}` the next arg, `{{`/`}}` are escapes.
     * Returns true on a recognized format! (even if its body refuses),
     * false when the macro is something else. */
    unsafe fn format_macro_scan(
        &mut self,
        text: *const u8,
        tlen: usize,
        locals: *mut LocalTab,
        out_fmt: *mut *const u8,
        out_fmt_len: *mut usize,
        out_args: *mut *mut pm_jit_rsx_ast_t,
        out_args_n: *mut usize,
        out_caps: *mut *mut pm_jit_rsx_ast_t,
        out_caps_n: *mut usize,
    ) -> bool {
        /* strip `::alloc::` / `alloc::` before `format!` */
        let mut sp = text;
        let mut sl = tlen;
        if sl >= 9
            && unsafe { *sp == b':' }
            && unsafe { *sp.add(1) == b':' }
            && unsafe { z_eq(sp.add(2), 6, b"alloc:\0".as_ptr()) }
        {
            sp = unsafe { sp.add(9) };
            sl -= 9;
        } else if sl >= 7 && unsafe { z_eq(sp, 6, b"alloc:\0".as_ptr()) } {
            sp = unsafe { sp.add(7) };
            sl -= 7;
        }
        /* `format!(` — the invocation's LAST byte is `)` */
        if sl < 9 {
            return false;
        }
        if unsafe { *sp } != b'f'
            || unsafe { *sp.add(1) } != b'o'
            || unsafe { *sp.add(2) } != b'r'
            || unsafe { *sp.add(3) } != b'm'
            || unsafe { *sp.add(4) } != b'a'
            || unsafe { *sp.add(5) } != b't'
            || unsafe { *sp.add(6) } != b'!'
            || unsafe { *sp.add(7) } != b'('
        {
            return false;
        }
        if unsafe { *sp.add(sl - 1) } != b')' {
            return false;
        }
        /* body: [sp+8, sl-1) — `format!(` is 8 bytes */
        let body = unsafe { sp.add(8) };
        let blen = sl - 9;
        /* sub-lex the body */
        let mut toks: pm_jit_rsx_toklist_t = pm_jit_rsx_toklist_t {
            toks: core::ptr::null_mut(),
            n_toks: 0,
        };
        if unsafe {
            pm_metal_jit_rsx_lex(
                self.arena,
                body,
                blen,
                &mut toks,
                self.errbuf,
                self.errcap,
            )
        } != 0
        {
            return true;
        }
        let mut p = Parser {
            arena: self.arena,
            toks: toks.toks,
            n_toks: toks.n_toks,
            at: 0,
            nd: Node {
                arena: self.arena,
                errbuf: self.errbuf,
                errcap: self.errcap,
                errline: 0,
                ok: true,
            },
            ok: true,
            cond_ctx: false,
            chain_ctx: false,
            shr_closes: 0,
            feats: core::ptr::null(),
        };
        if unsafe { p.kind(p.at) } != pm_jit_rsx_tok_kind::STRING_LITERAL {
            unsafe {
                self.err(b"unsupported: format! needs a literal\0".as_ptr(), 0);
            }
            return true;
        }
        let lit_tok = unsafe { p.tok(p.at) };
        let ltext = unsafe { (*lit_tok).text };
        let llen = unsafe { (*lit_tok).text_len };
        if llen < 2 || unsafe { *ltext } != b'"' {
            unsafe {
                self.err(b"unsupported: format! needs a string literal\0".as_ptr(), 0);
            }
            return true;
        }
        p.at += 1;
        /* positional args: `, expr`* until END */
        let mut args: [*mut pm_jit_rsx_ast_t; 8] = [core::ptr::null_mut(); 8];
        let mut args_n = 0usize;
        if unsafe { p.is_punct(p.at, b',') } {
            p.at += 1;
            while unsafe { p.kind(p.at) } != pm_jit_rsx_tok_kind::END {
                let a = unsafe { p.parse_expr() };
                if !p.ok || a.is_null() {
                    unsafe {
                        self.err(b"unsupported: format! argument\0".as_ptr(), 0);
                    }
                    return true;
                }
                if args_n < 8 {
                    args[args_n] = a;
                    args_n += 1;
                }
                if unsafe { p.is_punct(p.at, b',') } {
                    p.at += 1;
                    continue;
                }
                break;
            }
        }
        if unsafe { p.kind(p.at) } != pm_jit_rsx_tok_kind::END {
            unsafe {
                self.err(b"unsupported: format! trailing tokens\0".as_ptr(), 0);
            }
            return true;
        }
        /* implicit captures: walk the literal's inner bytes for
         * `{ident}` holes and sub-parse each ident as a PATH expr.
         * `{{` / `}}` escapes and `{}` positional holes are the
         * emission's job — only captures need ASTs at scan time. */
        let inner = unsafe { ltext.add(1) };
        let inner_len = llen - 2;
        let mut caps: [*mut pm_jit_rsx_ast_t; 16] = [core::ptr::null_mut(); 16];
        let mut caps_n = 0usize;
        let mut i = 0usize;
        while i < inner_len {
            if unsafe { *inner.add(i) } != b'{' {
                i += 1;
                continue;
            }
            /* `{{` is an escaped brace, not a hole */
            if i + 1 < inner_len && unsafe { *inner.add(i + 1) } == b'{' {
                i += 2;
                continue;
            }
            /* find the closing `}` */
            let mut j = i + 1;
            let mut ok_ident = false;
            while j < inner_len {
                let c = unsafe { *inner.add(j) };
                if c == b'}' {
                    ok_ident = j > i + 1;
                    break;
                }
                /* RFC 3086 capture names: [a-zA-Z_][a-zA-Z0-9_]* */
                if !(c.is_ascii_alphanumeric() || c == b'_') {
                    break;
                }
                j += 1;
            }
            if !ok_ident {
                /* `{}` positional or malformed — emission consumes it */
                i = j + 1;
                continue;
            }
            /* sub-parse the ident bytes as a PATH expr */
            let ip = unsafe { inner.add(i + 1) };
            let il = j - i - 1;
            let mut itoks: pm_jit_rsx_toklist_t = pm_jit_rsx_toklist_t {
                toks: core::ptr::null_mut(),
                n_toks: 0,
            };
            if unsafe {
                pm_metal_jit_rsx_lex(
                    self.arena,
                    ip,
                    il,
                    &mut itoks,
                    self.errbuf,
                    self.errcap,
                )
            } != 0
            {
                return true;
            }
            let mut ip2 = Parser {
                arena: self.arena,
                toks: itoks.toks,
                n_toks: itoks.n_toks,
                at: 0,
                nd: Node {
                    arena: self.arena,
                    errbuf: self.errbuf,
                    errcap: self.errcap,
                    errline: 0,
                    ok: true,
                },
                ok: true,
                cond_ctx: false,
                chain_ctx: false,
                shr_closes: 0,
                feats: core::ptr::null(),
            };
            let cap = unsafe { ip2.parse_expr() };
            if !ip2.ok || cap.is_null() {
                unsafe {
                    self.err(b"unsupported: format! capture\0".as_ptr(), 0);
                }
                return true;
            }
            if caps_n < 16 {
                caps[caps_n] = cap;
                caps_n += 1;
            }
            i = j + 1;
        }
        unsafe {
            *out_fmt = inner;
            *out_fmt_len = inner_len;
            let mut k = 0usize;
            while k < args_n {
                *out_args.add(k) = args[k];
                k += 1;
            }
            *out_args_n = args_n;
            let mut k = 0usize;
            while k < caps_n {
                *out_caps.add(k) = caps[k];
                k += 1;
            }
            *out_caps_n = caps_n;
        }
        true
    }

    /* Is a MACRO node one of the plane's expression macros (vec!,
     * format!)? Statement macros keep the emit_stmt skip semantics. */
    unsafe fn macro_is_value(&mut self, e: *const pm_jit_rsx_ast_t) -> bool {
        let mut ffmt: *const u8 = core::ptr::null();
        let mut ffmt_len: usize = 0;
        let mut fargs: [*mut pm_jit_rsx_ast_t; 8] = [core::ptr::null_mut(); 8];
        let mut fargs_n: usize = 0;
        let mut fcaps: [*mut pm_jit_rsx_ast_t; 16] = [core::ptr::null_mut(); 16];
        let mut fcaps_n: usize = 0;
        let save_ok = self.ok;
        let save_nerrs = self.nerrs;
        self.ok = true;
        let is_fmt = unsafe {
            self.format_macro_scan(
                (*e).text,
                (*e).text_len,
                core::ptr::null_mut(),
                &mut ffmt,
                &mut ffmt_len,
                fargs.as_mut_ptr(),
                &mut fargs_n,
                fcaps.as_mut_ptr(),
                &mut fcaps_n,
            )
        };
        self.ok = save_ok;
        self.nerrs = save_nerrs;
        if is_fmt {
            return true;
        }
        let mut row: usize = 0;
        let mut count: *mut pm_jit_rsx_ast_t = core::ptr::null_mut();
        let mut elems: [*mut pm_jit_rsx_ast_t; 16] = [core::ptr::null_mut(); 16];
        let mut list_n: usize = 0;
        self.ok = true;
        let is_vec = unsafe {
            self.vec_macro_scan(
                (*e).text,
                (*e).text_len,
                core::ptr::null_mut(),
                &mut row,
                &mut count,
                elems.as_mut_ptr(),
                &mut list_n,
            )
        };
        self.ok = save_ok;
        self.nerrs = save_nerrs;
        is_vec
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
                    /* &str -> the fat rsx_str_ref_t (ptr+len struct, the
                     * same shape cargo uses): .len()/.as_ptr() become
                     * field selects, a str param carries its length.
                     * `str` parses as a path type (leaf `str`, no generic
                     * args) — test the leaf, not the rendered spelling. */
                    let mut inner_is_str = false;
                    if unsafe { (*inner).kind } == pm_jit_rsx_ast_kind::TYPE {
                        let it = unsafe { (*inner).text };
                        let itl = unsafe { (*inner).text_len };
                        if unsafe { z_eq(it, itl, b"path\0".as_ptr()) }
                            || unsafe { z_eq(it, itl, b"gpath\0".as_ptr()) }
                        {
                            let ik = unsafe { (*inner).kids };
                            let ikn = unsafe { (*inner).n_kids } as usize;
                            if ikn == 1 {
                                let leaf = unsafe { *ik.add(0) };
                                let lt = unsafe { (*leaf).text };
                                let ltl = unsafe { (*leaf).text_len };
                                if ltl == 3 && unsafe { z_eq(lt, 3, b"str\0".as_ptr()) } {
                                    inner_is_str = true;
                                }
                            }
                        } else if itl == 3 && unsafe { z_eq(it, 3, b"str\0".as_ptr()) } {
                            inner_is_str = true;
                        }
                    }
                    /* &[T] -> the fat slice ref rsx_arr_<row> ({ const T *p;
                     * size_t n; }): a slice param/local carries its length,
                     * so .iter()/.len()/closure builtins/for-in all walk
                     * .p[0..n) exactly like the Vec rows. &mut [T] lowers
                     * the same fat pair (C const discipline aside, the
                     * slice is a read view in this subset). */
                    if unsafe { (*inner).kind } == pm_jit_rsx_ast_kind::TYPE {
                        let it = unsafe { (*inner).text };
                        let itl = unsafe { (*inner).text_len };
                        if itl == 2 && unsafe { z_eq(it, 2, b"[]\0".as_ptr()) } {
                            let ik = unsafe { (*inner).kids };
                            let ikn = unsafe { (*inner).n_kids } as usize;
                            if ikn >= 1 {
                                let elem_ty = unsafe { *ik.add(0) };
                                let eb = self.arena_tmp();
                                let en = unsafe { self.ctype(elem_ty, eb, 128) };
                                if en > 0 && en < ARR_SIG {
                                    let row = unsafe { self.arrs.intern(eb, en) };
                                    if row < ARR_CAP {
                                        let nb = self.arena_tmp();
                                        let nn = unsafe { ArrTab::name_for(row, nb, 96) };
                                        if nn > 0 {
                                            at = unsafe { zput(out, cap, at, nb) };
                                            unsafe {
                                                *out.add(at) = 0;
                                            }
                                            return at;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    let inner_buf = self.arena_tmp();
                    let n = unsafe { self.ctype(inner, inner_buf, 128) };
                    if n == 0 && !inner_is_str {
                        return 0;
                    }
                    if inner_is_str {
                        self.str_ref_used = true;
                        at = unsafe { zput(out, cap, at, b"rsx_str_ref_t\0".as_ptr()) };
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
        if self.pre_mode && self.pre_pool_at + 160 <= 640 {
            let p = self.pre_pool.as_mut_ptr().add(self.pre_pool_at);
            self.pre_pool_at += 160;
            return p;
        }
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
        /* The generic HEAD: for a gpath the parser records the plain-
         * segment count in int_val — the head is the LAST plain segment
         * (`crate::util::lock::Mutex<T>`'s head is Mutex, not `crate`;
         * the generic args are kids[nsegs..]). Legacy gpaths minted
         * before the marker (int_val == 0) and single-segment paths
         * keep head == kids[0]. A malformed marker (> nk) falls back
         * to kids[0] and refuses downstream, never misrenders. */
        let nsegs = unsafe { (*ty).int_val } as usize;
        let head_i = if nsegs >= 1 && nsegs <= nk { nsegs - 1 } else { 0 };
        let garg = if nsegs >= 1 && nsegs <= nk { nsegs } else { 1 };
        let first = unsafe { *kids.add(head_i) };
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
            let inner = unsafe { *kids.add(garg) };
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
            let inner = unsafe { *kids.add(garg) };
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
            let inner = unsafe { *kids.add(garg) };
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
        /* Vec<T>: the container plane — intern the element's C type,
         * mint the rsx_vec_<elem> typedef name (preamble emits the
         * struct + ops once), render the name. */
        if nk >= 2 && unsafe { z_eq(fname, flen, b"Vec\0".as_ptr()) } {
            let inner = unsafe { *kids.add(garg) };
            let inner_buf = self.arena_tmp();
            let n = unsafe { self.ctype(inner, inner_buf, 128) };
            if n == 0 {
                return 0;
            }
            let slot = unsafe { self.vecs.intern(inner_buf, n) };
            if slot == VEC_CAP {
                unsafe {
                    self.err(b"vec type table overflow\0".as_ptr(), unsafe { (*ty).line });
                }
                return 0;
            }
            let tdn = self.arena_tmp();
            let tdn_len = unsafe { VecTab::name_for(slot, tdn, 96) };
            if tdn_len == 0 {
                unsafe {
                    self.err(b"internal: vec typedef name too long\0".as_ptr(), unsafe { (*ty).line });
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
        /* Mutex<T>/SpinLock<T> (any qualification depth — the glue alias
         * `crate::util::lock::Mutex` resolves by its head): the lock
         * plane. Intern the payload's C type, mint rsx_lock_<row> (the
         * preamble emits { pm_util_lock_t raw; T value; } + the extern
         * acquire/release prototypes once). */
        if nk >= 2
            && (unsafe { z_eq(fname, flen, b"Mutex\0".as_ptr()) }
                || unsafe { z_eq(fname, flen, b"SpinLock\0".as_ptr()) })
        {
            let inner = unsafe { *kids.add(garg) };
            let inner_buf = self.arena_tmp();
            let n = unsafe { self.ctype(inner, inner_buf, 128) };
            if n == 0 {
                return 0;
            }
            let slot = unsafe { self.locks.intern(inner_buf, n) };
            if slot == LOCK_CAP {
                unsafe {
                    self.err(b"lock type table overflow\0".as_ptr(), unsafe { (*ty).line });
                }
                return 0;
            }
            let tdn = self.arena_tmp();
            let tdn_len = unsafe { LockTab::name_for(slot, tdn, 96) };
            if tdn_len == 0 {
                unsafe {
                    self.err(b"internal: lock typedef name too long\0".as_ptr(), unsafe { (*ty).line });
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
                /* String-plane pre-scan: pass 0a lowers CONSTs BEFORE any
                 * typedef flush, so a `pub const X: &str`/`String` static
                 * must set str_ref_used/str_own_used HERE — ctype() does
                 * the marking when it renders the declared type, and this
                 * is the last pass that runs before that declaration.
                 * The probe's own refusals are DISCARDED (ok/nerrs saved
                 * and restored): a Mut<[T; N]> interior-mut static, for
                 * example, legitimately fails a bare ctype() here while
                 * the real pass lowers it through its own wrapper path —
                 * poisoning collect would refuse the whole unit for a
                 * static that compiles fine. */
                let mut cj = 0usize;
                while cj < ink {
                    let ckk = unsafe { *ikids.add(cj) };
                    if unsafe { (*ckk).kind } == pm_jit_rsx_ast_kind::TYPE {
                        let save_ok = self.ok;
                        let save_nerrs = self.nerrs;
                        self.ok = true;
                        let cb = self.arena_tmp();
                        let _ = unsafe { self.ctype(ckk, cb, 128) };
                        self.ok = save_ok;
                        self.nerrs = save_nerrs;
                        break;
                    }
                    cj += 1;
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
            } else if kind == pm_jit_rsx_ast_kind::TRAIT {
                /* trait decl: intern the object, render each method's
                 * vtable fn-ptr sig (ret (*name)(Name *self, params..)).
                 * The typedef emits once in the type pass. */
                self.collect_trait(item);
            } else if kind == pm_jit_rsx_ast_kind::IMPL {
                /* methods: Type_method with self as first param. */
                self.collect_impl(item);
            }
            i += 1;
        }
    }

    /* Trait declaration: intern the object and its method slots. Each
     * method's vtable fn-ptr sig is rendered here (the sig AST kids do
     * not outlive collect): `ret (*name)(void *_self, T p, ..)` — the
     * receiver is the DATA pointer the object carries, not the object
     * itself (the trait object is `{ void *_self; fnptrs.. }`: a
     * `&mut dyn T` lowers to `T *`, dispatch reads `p->m(p->_self, ..)`,
     * and a coercion from `&mut S` materializes the object with
     * `._self = &s` and the impl's mangled fns — the object carries both
     * halves of Rust's fat pointer, C-style. A by-value self in a trait
     * is refused: the vtable slot cannot copy a value the caller has
     * no name for.) */
    unsafe fn collect_trait(&mut self, item: *const pm_jit_rsx_ast_t) {
        let tname = unsafe { (*item).text };
        let tlen = unsafe { (*item).text_len };
        let t = unsafe { self.traits.intern(tname, tlen) };
        if t == TRAIT_CAP {
            unsafe {
                self.err(b"trait table overflow\0".as_ptr(), unsafe { (*item).line });
            }
            return;
        }
        let kids = unsafe { (*item).kids };
        let nk = unsafe { (*item).n_kids } as usize;
        let mut i = 0usize;
        while i < nk {
            let k = unsafe { *kids.add(i) };
            if unsafe { (*k).kind } != pm_jit_rsx_ast_kind::FN {
                i += 1;
                continue;
            }
            /* receiver form: the first PARAM kid, if it is self-ish */
            let fk = unsafe { (*k).kids };
            let fnk = unsafe { (*k).n_kids } as usize;
            let mut ref_recv = true;
            let mut saw_recv = false;
            let mut j = 0usize;
            while j < fnk {
                let p = unsafe { *fk.add(j) };
                if unsafe { (*p).kind } == pm_jit_rsx_ast_kind::PARAM {
                    let pt = unsafe { (*p).text };
                    let ptl = unsafe { (*p).text_len };
                    if ptl == 4 && !pt.is_null() && unsafe { z_eq(pt, ptl, b"self\0".as_ptr()) } {
                        ref_recv = false;
                        saw_recv = true;
                    } else if !pt.is_null()
                        && (unsafe { z_eq(pt, ptl, b"&self\0".as_ptr()) }
                            || unsafe { z_eq(pt, ptl, b"&mut self\0".as_ptr()) })
                    {
                        ref_recv = true;
                        saw_recv = true;
                    }
                    break;
                }
                j += 1;
            }
            if saw_recv && !ref_recv {
                unsafe {
                    self.err(
                        b"unsupported: by-value self in trait\0".as_ptr(),
                        unsafe { (*k).line },
                    );
                }
                return;
            }
            /* render the fn-ptr sig body */
            let sig = self.arena_tmp();
            let mut at = 0usize;
            /* ret: the last TYPE kid that is not a qual */
            let mut ret = b"void\0".as_ptr();
            let mut retlen = 4usize;
            let mut j2 = 0usize;
            while j2 < fnk {
                let kk = unsafe { *fk.add(j2) };
                if unsafe { (*kk).kind } == pm_jit_rsx_ast_kind::TYPE {
                    let t2 = unsafe { (*kk).text };
                    let t2l = unsafe { (*kk).text_len };
                    let is_qual = t2l == 0
                        || t2.is_null()
                        || unsafe { z_eq(t2, t2l, b"unsafe\0".as_ptr()) }
                        || unsafe { z_eq(t2, t2l, b"extern\0".as_ptr()) }
                        || (unsafe { *t2 } == b'"');
                    if !is_qual {
                        let ct = self.arena_tmp();
                        let n = unsafe { self.ctype(kk, ct, 128) };
                        if n > 0 {
                            ret = ct;
                            retlen = n;
                        }
                    }
                }
                j2 += 1;
            }
            at = unsafe { bput(sig, TRAIT_SIG, at, ret, retlen) };
            at = unsafe { bput(sig, TRAIT_SIG, at, b" (*\0".as_ptr(), 3) };
            at = unsafe { bput(sig, TRAIT_SIG, at, unsafe { (*k).text }, unsafe { (*k).text_len }) };
            at = unsafe { bput(sig, TRAIT_SIG, at, b")(void *_self\0".as_ptr(), 13) };
            /* params: every PARAM kid after the receiver — each after the
             * `void *_self` receiver, so EVERY one takes a leading comma */
            let mut first = false;
            let mut j3 = 0usize;
            while j3 < fnk {
                let p = unsafe { *fk.add(j3) };
                if unsafe { (*p).kind } != pm_jit_rsx_ast_kind::PARAM {
                    j3 += 1;
                    continue;
                }
                let pt = unsafe { (*p).text };
                let ptl = unsafe { (*p).text_len };
                if !pt.is_null()
                    && (unsafe { z_eq(pt, ptl, b"self\0".as_ptr()) }
                        || unsafe { z_eq(pt, ptl, b"&self\0".as_ptr()) }
                        || unsafe { z_eq(pt, ptl, b"&mut self\0".as_ptr()) })
                {
                    j3 += 1;
                    continue;
                }
                if !first {
                    at = unsafe { bput(sig, TRAIT_SIG, at, b", \0".as_ptr(), 2) };
                }
                first = false;
                if unsafe { (*p).n_kids } >= 1 {
                    let pty = unsafe { *(*p).kids.add(0) };
                    let ct = self.arena_tmp();
                    let n = unsafe { self.ctype(pty, ct, 128) };
                    if n > 0 {
                        at = unsafe { bput(sig, TRAIT_SIG, at, ct, n) };
                    }
                }
                at = unsafe { bput(sig, TRAIT_SIG, at, b" \0".as_ptr(), 1) };
                at = unsafe { bput(sig, TRAIT_SIG, at, pt, ptl) };
                j3 += 1;
            }
            at = unsafe { bput(sig, TRAIT_SIG, at, b")\0".as_ptr(), 1) };
            unsafe {
                *sig.add(at) = 0;
            }
            let ok_add = unsafe {
                self.traits.add_method(
                    t,
                    unsafe { (*k).text },
                    unsafe { (*k).text_len },
                    true,
                    sig,
                    at,
                )
            };
            if !ok_add {
                unsafe {
                    self.err(b"trait method table overflow\0".as_ptr(), unsafe { (*k).line });
                }
                return;
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
        /* trait impl? TYPE kid with text "trait" wraps the trait path */
        let mut trait_nm: *const u8 = b"\0".as_ptr();
        let mut trait_nl = 0usize;
        let mut j = 0usize;
        while j < nk {
            let k = unsafe { *kids.add(j) };
            if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::TYPE {
                let t = unsafe { (*k).text };
                let tl = unsafe { (*k).text_len };
                if tl == 5 && !t.is_null() && unsafe { z_eq(t, tl, b"trait\0".as_ptr()) } {
                    let tk = unsafe { (*k).kids };
                    let tkn = unsafe { (*k).n_kids } as usize;
                    if tkn >= 1 {
                        let tpath = unsafe { *tk.add(0) };
                        let pk = unsafe { (*tpath).kids };
                        let pkn = unsafe { (*tpath).n_kids } as usize;
                        if pkn >= 1 {
                            let leaf = unsafe { *pk.add(pkn - 1) };
                            trait_nm = unsafe { (*leaf).text };
                            trait_nl = unsafe { (*leaf).text_len };
                        }
                    }
                } else {
                    /* leaf of the self-type path */
                    let kk = unsafe { (*k).kids };
                    let kn = unsafe { (*k).n_kids } as usize;
                    if kn > 0 {
                        let leaf = unsafe { *kk.add(kn - 1) };
                        self_ty = unsafe { (*leaf).text };
                        self_ty_len = unsafe { (*leaf).text_len };
                    }
                }
            }
            j += 1;
        }
        /* record the (self type, trait) pair for vtable init + coercion */
        if trait_nl > 0 && self_ty_len > 0 {
            if self.ti_n < TI_CAP {
                let mut a = 0usize;
                while a < self_ty_len && a < 48 {
                    self.ti_self[self.ti_n][a] = unsafe { *self_ty.add(a) };
                    a += 1;
                }
                self.ti_self_lens[self.ti_n] = a;
                let mut b2 = 0usize;
                while b2 < trait_nl && b2 < 48 {
                    self.ti_trait[self.ti_n][b2] = unsafe { *trait_nm.add(b2) };
                    b2 += 1;
                }
                self.ti_trait_lens[self.ti_n] = b2;
                self.ti_n += 1;
            } else {
                unsafe {
                    self.err(b"trait impl table overflow\0".as_ptr(), unsafe { (*item).line });
                }
                return;
            }
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
        if kind == pm_jit_rsx_ast_kind::MACRO {
            /* vec![..] — the container macro; format!(..) — the
             * interpolation macro. Both re-parse their body with the
             * real grammar; anything else refuses (loudly, in emit). */
            let mut row: usize = 0;
            let mut count: *mut pm_jit_rsx_ast_t = core::ptr::null_mut();
            let mut elems: [*mut pm_jit_rsx_ast_t; 16] = [core::ptr::null_mut(); 16];
            let mut list_n: usize = 0;
            if unsafe {
                self.vec_macro_scan(
                    (*e).text,
                    (*e).text_len,
                    locals,
                    &mut row,
                    &mut count,
                    elems.as_mut_ptr(),
                    &mut list_n,
                )
            } {
                if !self.ok {
                    return 0;
                }
                let tdn = self.arena_tmp();
                let tdn_len = unsafe { VecTab::name_for(row, tdn, 96) };
                if tdn_len == 0 {
                    unsafe {
                        self.err(b"internal: vec typedef name too long\0".as_ptr(), 0);
                    }
                    return 0;
                }
                /* name_for fills raw bytes; bput + NUL into out */
                let at = unsafe { bput(out, cap, 0, tdn, tdn_len) };
                if at >= cap {
                    return 0;
                }
                unsafe {
                    *out.add(at) = 0;
                }
                return at;
            }
            /* format!(..) — the interpolation macro types as the
             * owned String; the scan registers the plane so the
             * preamble emits the typedef + ops before any use. */
            let mut ffmt: *const u8 = core::ptr::null();
            let mut ffmt_len: usize = 0;
            let mut fargs: [*mut pm_jit_rsx_ast_t; 8] = [core::ptr::null_mut(); 8];
            let mut fargs_n: usize = 0;
            let mut fcaps: [*mut pm_jit_rsx_ast_t; 16] = [core::ptr::null_mut(); 16];
            let mut fcaps_n: usize = 0;
            if unsafe {
                self.format_macro_scan(
                    (*e).text,
                    (*e).text_len,
                    locals,
                    &mut ffmt,
                    &mut ffmt_len,
                    fargs.as_mut_ptr(),
                    &mut fargs_n,
                    fcaps.as_mut_ptr(),
                    &mut fcaps_n,
                )
            } {
                if !self.ok {
                    return 0;
                }
                self.str_own_used = true;
                let n2 = unsafe { zput(out, cap, 0, b"rsx_str_t\0".as_ptr()) };
                return if n2 >= cap { 0 } else { n2 };
            }
            return 0;
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
                let n2 = unsafe { self.expr_ctype(inner, out, cap, locals) };
                if n2 > 0 {
                    return n2;
                }
            } else if tk == pm_jit_rsx_ast_kind::BLOCK {
                let n2 = unsafe { self.expr_ctype(tail, out, cap, locals) };
                if n2 > 0 {
                    return n2;
                }
            } else if tk != pm_jit_rsx_ast_kind::STMT {
                let n2 = unsafe { self.expr_ctype(tail, out, cap, locals) };
                if n2 > 0 {
                    return n2;
                }
            }
            /* Multi-statement block: the tail references the block's OWN
             * lets (`let a = ..; let b = ..; (a, b)`), which the parent's
             * flat tab never sees. Walk the lets into the tab itself and
             * rewind after: typing only, emission keeps its own scope
             * machinery, and no speculative row survives to mis-type a
             * later expression. (A forked tab worked too but burned one
             * ~37 KiB LocalTab per typed block — the self-compile's draw
             * went from ~63 MiB to ~77 MiB and the 64 MiB self_host gate
             * refused with arena exhaustion.) */
            if !locals.is_null() {
                let n0 = unsafe { (*locals).n };
                let mut bi = 0usize;
                while bi < nk - 1 {
                    let bs = unsafe { *kids.add(bi) };
                    if unsafe { (*bs).kind } == pm_jit_rsx_ast_kind::LET {
                        let lk = unsafe { (*bs).kids };
                        let ln = unsafe { (*bs).n_kids } as usize;
                        let mut lname: *const u8 = core::ptr::null();
                        let mut lname_len = 0usize;
                        let mut linit: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
                        let mut lty: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
                        let mut li = 0usize;
                        while li < ln {
                            let lkk = unsafe { *lk.add(li) };
                            let k2 = unsafe { (*lkk).kind };
                            if k2 == pm_jit_rsx_ast_kind::PATH && lname.is_null() {
                                lname = unsafe { (*lkk).text };
                                lname_len = unsafe { (*lkk).text_len };
                            } else if k2 == pm_jit_rsx_ast_kind::TYPE {
                                lty = lkk;
                            } else if k2 != pm_jit_rsx_ast_kind::ATTR {
                                linit = lkk;
                            }
                            li += 1;
                        }
                        if !lname.is_null() {
                            let lct = self.arena_tmp();
                            let mut lct_len = 0usize;
                            if !lty.is_null() {
                                lct_len = unsafe { self.ctype(lty, lct, 128) };
                            } else if !linit.is_null() {
                                lct_len = unsafe { self.expr_ctype(linit, lct, 128, locals) };
                            }
                            if lct_len > 0 {
                                unsafe {
                                    (*locals).add(lname, lname_len, lct, lct_len, 1);
                                }
                            }
                        }
                    }
                    bi += 1;
                }
                let n3 = unsafe { self.expr_ctype(tail, out, cap, locals) };
                /* rewind: the speculative rows die with the block; the
                 * arena spans they allocated are unreachable garbage the
                 * arena reclaims at unit end (bounded by the typing walk,
                 * not monotonic in re-typing). */
                unsafe {
                    (*locals).n = n0;
                }
                return n3;
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
                    /* `&v` where v: Vec<T> — the borrow coerces to &[T]:
                     * type it as the element's fat slice row (the call-
                     * arg coercion below emits the compound literal). */
                    if bn > 8 && unsafe { z_eq(b_buf, 8, b"rsx_vec_\0".as_ptr()) } {
                        let vs = unsafe { self.vecs.find_by_name(b_buf, bn) };
                        if vs < VEC_CAP {
                            let el = self.vecs.elem_lens[vs];
                            if el > 0 && el < ARR_SIG {
                                let row = unsafe {
                                    self.arrs.intern(self.vecs.elems[vs].as_ptr(), el)
                                };
                                if row < ARR_CAP {
                                    let nb = self.arena_tmp();
                                    let nn = unsafe { ArrTab::name_for(row, nb, 96) };
                                    if nn > 0 {
                                        let w = unsafe { zput(out, cap, 0, nb) };
                                        return if w >= cap { 0 } else { w };
                                    }
                                }
                            }
                        }
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
                /* Call through ANY fn-pointer expression (not only a local
                 * bind): `(hook.f)(..)` types the callee expr — a fn-ptr
                 * spelling `ret (*)(..)` yields ret; an alias (a type
                 * alias used as the field's type) resolves via st_find.
                 * The general callee-side twin of the PATH branch below. */
                if unsafe { (*callee).kind } != pm_jit_rsx_ast_kind::PATH {
                    let cb = self.arena_tmp();
                    let cn = unsafe { self.expr_ctype(callee, cb, 128, locals) };
                    if cn > 0 && cn < 128 {
                        let mut sp = cb;
                        let mut spl = cn;
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
                            /* An alias (a fn-ptr type alias used as the
                             * field's type) registers its RESOLVED RET in
                             * the static table — the call's type IS that
                             * ret. A raw fn-ptr spelling would carry `(`;
                             * the alias never does, so a resolved hit
                             * with no `(` is the answer itself. */
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
                                if !has_paren && spl < cap {
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
                        if has_paren && ri > 0 && ri < cap {
                            let mut j = 0usize;
                            while j < ri {
                                unsafe {
                                    *out.add(j) = *sp.add(j);
                                }
                                j += 1;
                            }
                            /* strip one trailing space (ret " ("…) */
                            let mut rl = ri;
                            while rl > 0 && unsafe { *out.add(rl - 1) } == b' ' {
                                rl -= 1;
                            }
                            unsafe {
                                if rl < cap {
                                    *out.add(rl) = 0;
                                } else if cap > 0 {
                                    *out.add(cap - 1) = 0;
                                }
                            }
                            return if rl >= cap { 0 } else { rl };
                        }
                    }
                }
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
                            /* `String::from(..)` / `String::from_utf8_lossy(..)` —
                             * the owned-String ctors: monomorphic rsx_str_t
                             * regardless of the argument shape (&str, &str
                             * view of bytes, char, another String). */
                            if cn >= 2 {
                                let head2 = unsafe { *ck.add(cn - 2) };
                                if unsafe { z_eq(unsafe { (*head2).text }, unsafe { (*head2).text_len }, b"String\0".as_ptr()) }
                                    && (unsafe { z_eq(name, nl, b"from\0".as_ptr()) }
                                        || unsafe { z_eq(name, nl, b"from_utf8_lossy\0".as_ptr()) })
                                {
                                    self.str_own_used = true;
                                    return unsafe { zput(out, cap, 0, b"rsx_str_t\0".as_ptr()) };
                                }
                            }
                            /* `Vec::new()` / `String::new()` — an empty
                             * container has no element type of its own;
                             * the binding's uses reveal it. The honest
                             * bounded context: when the enclosing fn's
                             * return is that container (the let feeds the
                             * tail), the row is cur_ret's. A wrong guess
                             * surfaces as a loud C error at the first
                             * mismatched push. */
                            let an2: usize = if unsafe { (*e).n_kids } >= 2 {
                                (unsafe { (*(*kids.add(1))).n_kids }) as usize
                            } else {
                                0
                            };
                            if an2 == 0
                                && unsafe { z_eq(name, nl, b"new\0".as_ptr()) }
                                && cn >= 2
                                && self.cur_ret_len > 0
                                && self.cur_ret_len < cap
                            {
                                let head = unsafe { *ck.add(cn - 2) };
                                let hname = unsafe { (*head).text };
                                let hlen = unsafe { (*head).text_len };
                                let is_vec = unsafe { z_eq(hname, hlen, b"Vec\0".as_ptr()) };
                                let is_str = unsafe { z_eq(hname, hlen, b"String\0".as_ptr()) };
                                if is_vec
                                    && self.cur_ret_len >= 8
                                    && unsafe { z_eq(self.cur_ret.as_ptr(), 8, b"rsx_vec_\0".as_ptr()) }
                                {
                                    let mut j = 0usize;
                                    while j < self.cur_ret_len {
                                        unsafe {
                                            *out.add(j) = self.cur_ret[j];
                                        }
                                        j += 1;
                                    }
                                    unsafe {
                                        *out.add(j) = 0;
                                    }
                                    return j;
                                }
                                /* String::new — the owned row spelled
                                 * exactly: rsx_str_t (NOT rsx_str_ref_t,
                                 * the fat &str, which shares the
                                 * 8-byte prefix). */
                                if is_str
                                    && self.cur_ret_len == 9
                                    && unsafe { z_eq(self.cur_ret.as_ptr(), 9, b"rsx_str_t\0".as_ptr()) }
                                {
                                    self.str_own_used = true;
                                    let mut j = 0usize;
                                    while j < self.cur_ret_len {
                                        unsafe {
                                            *out.add(j) = self.cur_ret[j];
                                        }
                                        j += 1;
                                    }
                                    unsafe {
                                        *out.add(j) = 0;
                                    }
                                    return j;
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
            /* Vec base: the element type is the interned spelling the
             * rsx_vec_<hex-of-elem> name encodes — reverse lookup. */
            if bn > 8 && unsafe { z_eq(b_buf, 8, b"rsx_vec_\0".as_ptr()) } {
                let vs = unsafe { self.vecs.find_by_name(b_buf, bn) };
                if vs < VEC_CAP {
                    let el = self.vecs.elem_lens[vs];
                    if el >= cap {
                        return 0;
                    }
                    let mut i = 0usize;
                    while i < el {
                        unsafe { *out.add(i) = self.vecs.elems[vs][i] };
                        i += 1;
                    }
                    unsafe { *out.add(el) = 0 };
                    return el;
                }
            }
            /* &[T] slice base: the ArrTab row's element spelling. */
            if bn > 8 && unsafe { z_eq(b_buf, 8, b"rsx_arr_\0".as_ptr()) } {
                let rs = unsafe { self.arrs.find_by_name(b_buf, bn) };
                if rs < ARR_CAP {
                    let el = self.arrs.elem_lens[rs];
                    if el >= cap {
                        return 0;
                    }
                    let mut i = 0usize;
                    while i < el {
                        unsafe { *out.add(i) = self.arrs.elems[rs][i] };
                        i += 1;
                    }
                    unsafe { *out.add(el) = 0 };
                    return el;
                }
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

                    /* plain binding arms on a fat scrutinee (&str, &[T],
                     * String, Vec rows): register the bind with the
                     * scrutinee's type before anything returns — arm bodies
                     * (and downstream lets) type against the bind. */
                    {
                        let fat_is = (scn == 13
                            && unsafe { z_eq(sct, 13, b"rsx_str_ref_t\0".as_ptr()) })
                            || (scn == 9
                                && unsafe { z_eq(sct, 9, b"rsx_str_t\0".as_ptr()) })
                            || (scn > 8
                                && scn < 128
                                && (unsafe { z_eq(sct, 8, b"rsx_arr_\0".as_ptr()) }
                                    || unsafe { z_eq(sct, 8, b"rsx_vec_\0".as_ptr()) }));
                        if fat_is {
                            let mut i2 = 1usize;
                            while i2 < nk {
                                let arm2 = unsafe { *kids.add(i2) };
                                if unsafe { (*arm2).kind } == pm_jit_rsx_ast_kind::MATCH_ARM
                                    && unsafe { (*arm2).n_kids } >= 2
                                {
                                    let ak2 = unsafe { (*arm2).kids };
                                    let pat2 = unsafe { *ak2.add(0) };
                                    if unsafe { (*pat2).kind } == pm_jit_rsx_ast_kind::PATH {
                                        let mut bnode = pat2;
                                        if unsafe { (*pat2).n_kids } as usize == 1
                                            && unsafe { z_eq(unsafe { (*pat2).text }, unsafe { (*pat2).text_len }, b"path\0".as_ptr()) }
                                        {
                                            bnode = unsafe { *(*pat2).kids.add(0) };
                                        }
                                        if unsafe { (*bnode).kind } == pm_jit_rsx_ast_kind::PATH
                                            && unsafe { (*bnode).n_kids } as usize == 0
                                        {
                                            let pt2 = unsafe { (*bnode).text };
                                            let ptl2 = unsafe { (*bnode).text_len };
                                            if ptl2 > 0
                                                && !pt2.is_null()
                                                && !(ptl2 == 1 && unsafe { z_eq(pt2, 1, b"_\0".as_ptr()) })
                                            {
                                                unsafe {
                                                    (*locals).add(pt2, ptl2, sct, scn, self.depth + 1);
                                                }
                                            }
                                        }
                                    }
                                }
                                i2 += 1;
                            }
                        }
                    }
                    /* &str-scrutinee match: the arms are str views (literals
                     * or the bound `other`) — the value is the fat ref
                     * regardless of which arm's body types first (a bare
                     * literal would type as const char *, the wrong shape
                     * for String::from/format! consumers). */
                    if scn == 13 && unsafe { z_eq(sct, 13, b"rsx_str_ref_t\0".as_ptr()) } {
                        self.str_ref_used = true;
                        return unsafe { zput(out, cap, 0, b"rsx_str_ref_t\0".as_ptr()) };
                    }
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
                /* `.next()` on an `x.rsplit(SEP)` receiver — the one
                 * iterator combinator the str plane lowers natively:
                 * Option<&str> (the segment AFTER the last separator,
                 * None when the separator never occurs or the tail is
                 * empty). Types as the struct-shaped Option row with
                 * the fat-ref payload; the emission is the statement
                 * expr that scans for the last sep byte. */
                if mlen == 4 && unsafe { z_eq(mname, mlen, b"next\0".as_ptr()) } {
                    let recv = unsafe { *kids.add(0) };
                    if unsafe { (*recv).kind } == pm_jit_rsx_ast_kind::METHOD_CALL
                        && unsafe { (*recv).n_kids } as usize >= 3
                    {
                        let rname = unsafe { *(*recv).kids.add(1) };
                        if unsafe { (*rname).text_len } == 6
                            && unsafe { z_eq(unsafe { (*rname).text }, 6, b"rsplit\0".as_ptr()) }
                        {
                            let base = unsafe { *(*recv).kids.add(0) };
                            let sb = self.arena_tmp();
                            let sl = unsafe { self.expr_ctype(base, sb, 128, locals) };
                            let ok_shape = sl == 13
                                && unsafe { z_eq(sb, 13, b"rsx_str_ref_t\0".as_ptr()) };
                            if ok_shape {
                                self.str_ref_used = true;
                                let slot = unsafe { self.opt_add(b"rsx_str_ref_t\0".as_ptr(), 13) };
                                if slot < OPT_CAP {
                                    /* the ENCODED typedef name (raw_len'e'
                                     * + 2*raw_len hex) — the same spelling
                                     * opt_typedef_name gives every Option
                                     * row, so opt_typedef_elem can read
                                     * the payload back out at the match
                                     * arm's Some-bind. */
                                    let tdn = self.arena_tmp();
                                    let tdn_len = unsafe {
                                        Lower::opt_typedef_name(
                                            b"rsx_str_ref_t\0".as_ptr(),
                                            13,
                                            tdn,
                                            160,
                                        )
                                    };
                                    if tdn_len > 0 && tdn_len < cap {
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
                                }
                            }
                        }
                    }
                }
                /* `X.iter().map(|x| body).collect()` — the map-collect
                 * chain: types as rsx_vec_<body's type>. The closure's
                 * one param binds the input element (Vec row, &[T] row,
                 * or fixed array), then the body types with that bind
                 * registered; the body's own type IS the output element.
                 * (The parser skips ::<Vec<_>> turbofish — inference is
                 * from the body, never the annotation.) */
                if mlen == 7 && unsafe { z_eq(mname, mlen, b"collect\0".as_ptr()) } {
                    let recv = unsafe { *kids.add(0) };
                    if unsafe { (*recv).kind } == pm_jit_rsx_ast_kind::METHOD_CALL
                        && (unsafe { (*recv).n_kids } as usize) >= 3
                    {
                        let rk = unsafe { (*recv).kids };
                        let rname = unsafe { *rk.add(1) };
                        if unsafe { (*rname).text_len } == 3
                            && unsafe { z_eq(unsafe { (*rname).text }, 3, b"map\0".as_ptr()) }
                        {
                            let map_recv = unsafe { *rk.add(0) };
                            let map_args = unsafe { *rk.add(2) };
                            let mak = unsafe { (*map_args).kids };
                            let man = unsafe { (*map_args).n_kids } as usize;
                            /* `String::as_str` fn-arg: the borrow combinator —
                             * output is the Vec<rsx_str_ref_t> view row. */
                            if man == 1 {
                                let ma0 = unsafe { *mak.add(0) };
                                if unsafe { (*ma0).kind } == pm_jit_rsx_ast_kind::PATH {
                                    let mkp = unsafe { (*ma0).kids };
                                    let mkn = unsafe { (*ma0).n_kids } as usize;
                                    if mkn >= 2 {
                                        let sl = unsafe { *mkp.add(mkn - 1) };
                                        let sp = unsafe { *mkp.add(mkn - 2) };
                                        if unsafe { (*sl).kind } == pm_jit_rsx_ast_kind::PATH
                                            && unsafe { (*sp).kind } == pm_jit_rsx_ast_kind::PATH
                                            && unsafe { z_eq(unsafe { (*sl).text }, unsafe { (*sl).text_len }, b"as_str\0".as_ptr()) }
                                            && unsafe { z_eq(unsafe { (*sp).text }, unsafe { (*sp).text_len }, b"String\0".as_ptr()) }
                                        {
                                            /* input must be Vec<String>/&[String]:
                                             * the output is Vec<&str>. */
                                            let bt2 = self.arena_tmp();
                                            let mut base2 = map_recv;
                                            if unsafe { (*map_recv).kind } == pm_jit_rsx_ast_kind::METHOD_CALL
                                                && (unsafe { (*map_recv).n_kids } as usize) >= 3
                                            {
                                                let mrk = unsafe { (*map_recv).kids };
                                                let mrn = unsafe { *mrk.add(1) };
                                                let mra = unsafe { *mrk.add(2) };
                                                if unsafe { (*mrn).text_len } == 4
                                                    && unsafe { z_eq(unsafe { (*mrn).text }, 4, b"iter\0".as_ptr()) }
                                                    && (unsafe { (*mra).n_kids } as usize) == 0
                                                {
                                                    base2 = unsafe { *mrk.add(0) };
                                                }
                                            }
                                            let btn2 = unsafe { self.expr_ctype(base2, bt2, 128, locals) };
                                            let mut in_ok = false;
                                            if btn2 > 8 && unsafe { z_eq(bt2, 8, b"rsx_vec_\0".as_ptr()) } {
                                                let vs = unsafe { self.vecs.find_by_name(bt2, btn2) };
                                                if vs < VEC_CAP
                                                    && self.vecs.elem_lens[vs] == 9
                                                    && unsafe { z_eq(self.vecs.elems[vs].as_ptr(), 9, b"rsx_str_t\0".as_ptr()) }
                                                {
                                                    in_ok = true;
                                                }
                                            }
                                            if !in_ok && btn2 > 8 && unsafe { z_eq(bt2, 8, b"rsx_arr_\0".as_ptr()) } {
                                                let rs = unsafe { self.arrs.find_by_name(bt2, btn2) };
                                                if rs < ARR_CAP
                                                    && self.arrs.elem_lens[rs] == 9
                                                    && unsafe { z_eq(self.arrs.elems[rs].as_ptr(), 9, b"rsx_str_t\0".as_ptr()) }
                                                {
                                                    in_ok = true;
                                                }
                                            }
                                            if in_ok {
                                                self.str_ref_used = true;
                                                self.str_own_used = true;
                                                let row = unsafe { self.vecs.intern(b"rsx_str_ref_t\0".as_ptr(), 13) };
                                                if row < VEC_CAP {
                                                    let nb = self.arena_tmp();
                                                    let nn = unsafe { VecTab::name_for(row, nb, 96) };
                                                    if nn > 0 && nn < cap {
                                                        let at = unsafe { bput(out, cap, 0, nb, nn) };
                                                        unsafe {
                                                            if at < cap {
                                                                *out.add(at) = 0;
                                                            } else if cap > 0 {
                                                                *out.add(cap - 1) = 0;
                                                            }
                                                        }
                                                        return if at >= cap { 0 } else { at };
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            if man == 1 && unsafe { (**mak.add(0)).kind } == pm_jit_rsx_ast_kind::CLOSURE {
                                let clo = unsafe { *mak.add(0) };
                                /* unwrap .iter()/.into_iter() on the map receiver */
                                let mut base = map_recv;
                                if unsafe { (*map_recv).kind } == pm_jit_rsx_ast_kind::METHOD_CALL
                                    && (unsafe { (*map_recv).n_kids } as usize) >= 3
                                {
                                    let mrk = unsafe { (*map_recv).kids };
                                    let mrn = unsafe { *mrk.add(1) };
                                    let mra = unsafe { *mrk.add(2) };
                                    let mrn_len = unsafe { (*mrn).text_len };
                                    let is_iter = mrn_len == 4
                                        && unsafe { z_eq(unsafe { (*mrn).text }, 4, b"iter\0".as_ptr()) };
                                    let is_into = mrn_len == 9
                                        && unsafe { z_eq(unsafe { (*mrn).text }, 9, b"into_iter\0".as_ptr()) };
                                    if (is_iter || is_into) && (unsafe { (*mra).n_kids } as usize) == 0
                                    {
                                        base = unsafe { *mrk.add(0) };
                                    }
                                }
                                let bt = self.arena_tmp();
                                let btn = unsafe { self.expr_ctype(base, bt, 128, locals) };
                                let mut eb: *const u8 = b"\0".as_ptr();
                                let mut el = 0usize;
                                if btn > 8 && unsafe { z_eq(bt, 8, b"rsx_vec_\0".as_ptr()) } {
                                    let vs = unsafe { self.vecs.find_by_name(bt, btn) };
                                    if vs < VEC_CAP {
                                        eb = self.vecs.elems[vs].as_ptr();
                                        el = self.vecs.elem_lens[vs];
                                    }
                                } else if btn > 8 && unsafe { z_eq(bt, 8, b"rsx_arr_\0".as_ptr()) } {
                                    let rs = unsafe { self.arrs.find_by_name(bt, btn) };
                                    if rs < ARR_CAP {
                                        eb = self.arrs.elems[rs].as_ptr();
                                        el = self.arrs.elem_lens[rs];
                                    }
                                } else {
                                    /* fixed array `T [N]` */
                                    let mut i = 0usize;
                                    while i < btn {
                                        if unsafe { *bt.add(i) } == b'[' {
                                            el = i;
                                            break;
                                        }
                                        i += 1;
                                    }
                                    if i < btn {
                                        eb = bt;
                                    }
                                }
                                if el > 0 && el < 128 && !locals.is_null() {
                                    /* register the param bind, type the body */
                                    let ck = unsafe { (*clo).kids };
                                    let cn = unsafe { (*clo).n_kids } as usize;
                                    if cn >= 2 {
                                        let param = unsafe { *ck.add(0) };
                                        let body = unsafe { *ck.add(cn - 1) };
                                        if unsafe { (*param).kind } == pm_jit_rsx_ast_kind::PARAM {
                                            let pn2 = unsafe { (*param).text };
                                            let pl2 = unsafe { (*param).text_len };
                                            /* tuple param `|(a, b)|`: register
                                             * the element binds from the
                                             * tuple row's signature. */
                                            let is_tup = pl2 == 3
                                                && unsafe { z_eq(pn2, 3, b"tup\0".as_ptr()) };
                                            if is_tup {
                                                if el >= 10
                                                    && unsafe { z_eq(eb, 10, b"rsx_tuple_\0".as_ptr()) }
                                                {
                                                    let ts = unsafe { self.tup_find(eb, el) };
                                                    let tpk = unsafe { (*param).kids };
                                                    let tpn = unsafe { (*param).n_kids } as usize;
                                                    if ts < TUP_CAP
                                                        && tpn > 0
                                                        && tpn <= TUP_MAXF
                                                        && self.tup_counts[ts] == tpn
                                                    {
                                                        let mut f4 = 0usize;
                                                        while f4 < tpn {
                                                            let sub4 = unsafe { *tpk.add(f4) };
                                                            if unsafe { (*sub4).kind } == pm_jit_rsx_ast_kind::PARAM {
                                                                let sn4 = unsafe { (*sub4).text };
                                                                let sl4 = unsafe { (*sub4).text_len };
                                                                let a4 = ts * TUP_MAXF + f4;
                                                                unsafe {
                                                                    (*locals).add(
                                                                        sn4,
                                                                        sl4,
                                                                        self.tup_elems[a4].as_ptr(),
                                                                        self.tup_lens[a4],
                                                                        0,
                                                                    );
                                                                }
                                                            }
                                                            f4 += 1;
                                                        }
                                                    }
                                                }
                                            } else {
                                                unsafe {
                                                    (*locals).add(pn2, pl2, eb, el, 0);
                                                }
                                            }
                                            let ob = self.arena_tmp();
                                            let saved_pre = self.pre_pool_at;
                                            let on = unsafe { self.expr_ctype(body, ob, 128, locals) };
                                            self.pre_pool_at = saved_pre;
                                            if on > 0 && on < 128 {
                                                let row = unsafe { self.vecs.intern(ob, on) };
                                                if row < VEC_CAP {
                                                    let nb = self.arena_tmp();
                                                    let nn = unsafe { VecTab::name_for(row, nb, 96) };
                                                    if nn > 0 && nn < cap {
                                                        let at = unsafe { bput(out, cap, 0, nb, nn) };
                                                        unsafe {
                                                            if at < cap {
                                                                *out.add(at) = 0;
                                                            } else if cap > 0 {
                                                                *out.add(cap - 1) = 0;
                                                            }
                                                        }
                                                        return if at >= cap { 0 } else { at };
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                /* split_whitespace().collect::<Vec<_>>().join(sep) — the
                 * whitespace-normalize chain: words joined by the one
                 * separator, one rsx_str_t. Recognized as a whole chain
                 * (the intermediate Vec<&&str> materializes only inside
                 * the emission's stmt-expr, never as a C name). */
                if mlen == 4
                    && unsafe { z_eq(mname, mlen, b"join\0".as_ptr()) }
                    && (unsafe { (*e).n_kids } as usize) >= 3
                {
                    let recv = unsafe { *kids.add(0) };
                    /* recv = collect::<Vec<_>>( split_whitespace(c) ) */
                    if unsafe { (*recv).kind } == pm_jit_rsx_ast_kind::METHOD_CALL
                        && (unsafe { (*recv).n_kids } as usize) >= 2
                    {
                        let rk = unsafe { (*recv).kids };
                        let rname = unsafe { *rk.add(1) };
                        if unsafe { (*rname).text_len } == 7
                            && unsafe { z_eq(unsafe { (*rname).text }, 7, b"collect\0".as_ptr()) }
                        {
                            let inner_recv = unsafe { *rk.add(0) };
                            /* inner_recv = split_whitespace(c) */
                            if unsafe { (*inner_recv).kind } == pm_jit_rsx_ast_kind::METHOD_CALL
                                && (unsafe { (*inner_recv).n_kids } as usize) >= 2
                            {
                                let ik = unsafe { (*inner_recv).kids };
                                let iname = unsafe { *ik.add(1) };
                                if unsafe { (*iname).text_len } == 16
                                    && unsafe {
                                        z_eq(unsafe { (*iname).text }, 16, b"split_whitespace\0".as_ptr())
                                    }
                                {
                                    let base = unsafe { *ik.add(0) };
                                    let sb = self.arena_tmp();
                                    let sl = unsafe { self.expr_ctype(base, sb, 128, locals) };
                                    if sl == 13
                                        && unsafe { z_eq(sb, 13, b"rsx_str_ref_t\0".as_ptr()) }
                                    {
                                        self.str_ref_used = true;
                                        self.str_own_used = true;
                                        let n2 = unsafe { zput(out, cap, 0, b"rsx_str_t\0".as_ptr()) };
                                        return if n2 >= cap { 0 } else { n2 };
                                    }
                                }
                            }
                        }
                    }
                }
                /* ---- &str view methods (receiver rsx_str_ref_t) ----
                 * trim/trim_end/trim_start stay the fat-ref shape (a
                 * tighter {p,n} window over the same bytes); the
                 * predicate/finder methods below return bool or the
                 * struct-shaped Option rows the emission fills. */
                if (unsafe { (*e).n_kids } as usize) >= 3
                    && ((mlen == 4 && unsafe { z_eq(mname, mlen, b"trim\0".as_ptr()) })
                        || (mlen == 8 && unsafe { z_eq(mname, mlen, b"trim_end\0".as_ptr()) })
                        || (mlen == 10
                            && unsafe { z_eq(mname, mlen, b"trim_start\0".as_ptr()) }))
                {
                    let recv = unsafe { *kids.add(0) };
                    let sb = self.arena_tmp();
                    let sl = unsafe { self.expr_ctype(recv, sb, 128, locals) };
                    /* the view methods also borrow an owned String: the
                     * rsx_str_t fields are the same p/n pair, so the
                     * emission's window math applies unchanged. */
                    if (sl == 13 && unsafe { z_eq(sb, 13, b"rsx_str_ref_t\0".as_ptr()) })
                        || (sl == 9 && unsafe { z_eq(sb, 9, b"rsx_str_t\0".as_ptr()) })
                    {
                        self.str_ref_used = true;
                        self.str_own_used = true;
                        let n2 = unsafe { zput(out, cap, 0, b"rsx_str_ref_t\0".as_ptr()) };
                        return if n2 >= cap { 0 } else { n2 };
                    }
                }
                if (unsafe { (*e).n_kids } as usize) >= 3
                    && ((mlen == 11 && unsafe { z_eq(mname, mlen, b"starts_with\0".as_ptr()) })
                        || (mlen == 9 && unsafe { z_eq(mname, mlen, b"ends_with\0".as_ptr()) }))
                {
                    let recv = unsafe { *kids.add(0) };
                    let sb = self.arena_tmp();
                    let sl = unsafe { self.expr_ctype(recv, sb, 128, locals) };
                    /* &str view or owned String — an owned receiver borrows
                     * to the same {p,n} pair for the memcmp. */
                    if (sl == 13 && unsafe { z_eq(sb, 13, b"rsx_str_ref_t\0".as_ptr()) })
                        || (sl == 9 && unsafe { z_eq(sb, 9, b"rsx_str_t\0".as_ptr()) })
                    {
                        self.str_ref_used = true;
                        self.str_own_used = true;
                        let n2 = unsafe { zput(out, cap, 0, b"bool\0".as_ptr()) };
                        return if n2 >= cap { 0 } else { n2 };
                    }
                }
                /* find(char) -> Option<usize>: byte index of the first
                 * occurrence, None when absent. struct-shaped row
                 * (size_t payload), the encoded Option typedef name. */
                if (unsafe { (*e).n_kids } as usize) >= 3
                    && ((mlen == 4 && unsafe { z_eq(mname, mlen, b"find\0".as_ptr()) })
                        || (mlen == 5 && unsafe { z_eq(mname, mlen, b"rfind\0".as_ptr()) }))
                {
                    let recv = unsafe { *kids.add(0) };
                    let sb = self.arena_tmp();
                    let sl = unsafe { self.expr_ctype(recv, sb, 128, locals) };
                    /* rfind borrows an owned String the same way — the
                     * scan runs over the same p/n pair. */
                    if (sl == 13 && unsafe { z_eq(sb, 13, b"rsx_str_ref_t\0".as_ptr()) })
                        || (mlen == 5
                            && sl == 9
                            && unsafe { z_eq(sb, 9, b"rsx_str_t\0".as_ptr()) })
                    {
                        let argk = unsafe { *kids.add(2) };
                        let ak = unsafe { (*argk).kids };
                        let an = unsafe { (*argk).n_kids } as usize;
                        if an == 1 {
                            let a0 = unsafe { *ak.add(0) };
                            if unsafe { (*a0).kind } == pm_jit_rsx_ast_kind::LITERAL
                                && unsafe { *(*a0).text } == b'\''
                            {
                                self.str_ref_used = true;
                                let slot = unsafe { self.opt_add(b"size_t\0".as_ptr(), 6) };
                                if slot < OPT_CAP {
                                    let tdn = self.arena_tmp();
                                    let tdn_len = unsafe {
                                        Lower::opt_typedef_name(
                                            b"size_t\0".as_ptr(),
                                            6,
                                            tdn,
                                            160,
                                        )
                                    };
                                    if tdn_len > 0 && tdn_len < cap {
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
                                }
                            }
                        }
                    }
                }
                /* split_once(sep)/rsplit_once(sep) -> Option<(&str,&str)>:
                 * the (before, after) pair around the first/last sep byte.
                 * The payload is the dedicated rsx_strpair_t row (a short
                 * stable name — the encoded tuple name would overflow the
                 * Option table's rows); the match/if-let arms destructure
                 * via emit_some_binds, which maps rsx_strpair_t's fields
                 * _0/_1 itself. Sep: a char literal. */
                if (unsafe { (*e).n_kids } as usize) >= 3
                    && ((mlen == 10 && unsafe { z_eq(mname, mlen, b"split_once\0".as_ptr()) })
                        || (mlen == 11 && unsafe { z_eq(mname, mlen, b"rsplit_once\0".as_ptr()) }))
                {
                    let recv = unsafe { *kids.add(0) };
                    let sb = self.arena_tmp();
                    let sl = unsafe { self.expr_ctype(recv, sb, 128, locals) };
                    /* &str or owned String receiver — the halves are &str
                     * views of the same bytes either way. */
                    let is_str = (sl == 13
                        && unsafe { z_eq(sb, 13, b"rsx_str_ref_t\0".as_ptr()) })
                        || (sl == 9 && unsafe { z_eq(sb, 9, b"rsx_str_t\0".as_ptr()) });
                    if is_str {
                        let argk = unsafe { *kids.add(2) };
                        let ak = unsafe { (*argk).kids };
                        let an = unsafe { (*argk).n_kids } as usize;
                        if an == 1 {
                            let a0 = unsafe { *ak.add(0) };
                            if unsafe { (*a0).kind } == pm_jit_rsx_ast_kind::LITERAL
                                && unsafe { *(*a0).text } == b'\''
                            {
                                self.str_ref_used = true;
                                self.strpair_used = true;
                                let slot = unsafe { self.opt_add(b"rsx_strpair_t\0".as_ptr(), 13) };
                                if slot < OPT_CAP {
                                    let on2 = self.arena_tmp();
                                    let on2_len = unsafe {
                                        Lower::opt_typedef_name(b"rsx_strpair_t\0".as_ptr(), 13, on2, 160)
                                    };
                                    if on2_len > 0 && on2_len < cap {
                                        let at = unsafe { bput(out, cap, 0, on2, on2_len) };
                                        unsafe {
                                            if at < cap {
                                                *out.add(at) = 0;
                                            } else if cap > 0 {
                                                *out.add(cap - 1) = 0;
                                            }
                                        }
                                        return if at >= cap { 0 } else { at };
                                    }
                                }
                            }
                        }
                    }
                }
                /* strip_prefix(lit)/strip_suffix(lit) -> Option<&str>:
                 * the remainder past the stripped literal, None when the
                 * literal is not a prefix/suffix. */
                if (unsafe { (*e).n_kids } as usize) >= 3
                    && ((mlen == 12 && unsafe { z_eq(mname, mlen, b"strip_prefix\0".as_ptr()) })
                        || (mlen == 12 && unsafe { z_eq(mname, mlen, b"strip_suffix\0".as_ptr()) }))
                {
                    let recv = unsafe { *kids.add(0) };
                    let sb = self.arena_tmp();
                    let sl = unsafe { self.expr_ctype(recv, sb, 128, locals) };
                    /* &str and String receivers alike: the remainder is an
                     * &str view either way (a String suffix strip yields
                     * the same Option<&str> contract). */
                    let is_str = (sl == 13
                        && unsafe { z_eq(sb, 13, b"rsx_str_ref_t\0".as_ptr()) })
                        || (sl == 9 && unsafe { z_eq(sb, 9, b"rsx_str_t\0".as_ptr()) });
                    if is_str {
                        self.str_ref_used = true;
                        let slot = unsafe { self.opt_add(b"rsx_str_ref_t\0".as_ptr(), 13) };
                        if slot < OPT_CAP {
                            let tdn = self.arena_tmp();
                            let tdn_len = unsafe {
                                Lower::opt_typedef_name(
                                    b"rsx_str_ref_t\0".as_ptr(),
                                    13,
                                    tdn,
                                    160,
                                )
                            };
                            if tdn_len > 0 && tdn_len < cap {
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
                        }
                    }
                }
                /* `.replace(from, to)` — the one substitution method the
                 * str plane lowers (from: char, to: literal). Both
                 * &str and String receivers yield a fresh owned String;
                 * the C helper takes the (p, n) view. */
                if (unsafe { (*e).n_kids } as usize) >= 3
                    && mlen == 7
                    && unsafe { z_eq(mname, mlen, b"replace\0".as_ptr()) }
                {
                    let argk = unsafe { *kids.add(2) };
                    let ak = unsafe { (*argk).kids };
                    let an = unsafe { (*argk).n_kids } as usize;
                    if an == 2 {
                        let recv = unsafe { *kids.add(0) };
                        let sb = self.arena_tmp();
                        let sl = unsafe { self.expr_ctype(recv, sb, 128, locals) };
                        let is_str = (sl == 13
                            && unsafe { z_eq(sb, 13, b"rsx_str_ref_t\0".as_ptr()) })
                            || (sl == 9 && unsafe { z_eq(sb, 9, b"rsx_str_t\0".as_ptr()) });
                        if is_str {
                            self.str_ref_used = true;
                            self.str_own_used = true;
                            let n2 = unsafe { zput(out, cap, 0, b"rsx_str_t\0".as_ptr()) };
                            return if n2 >= cap { 0 } else { n2 };
                        }
                    }
                }
                /* char predicates — `c.is_ascii_alphanumeric()` /
                 * `c.is_ascii_digit()` etc. on the uint32_t char scalar.
                 * Bool out; the emission folds the range tests. */
                if (unsafe { (*e).n_kids } as usize) >= 3
                    && ((mlen == 20
                        && unsafe { z_eq(mname, mlen, b"is_ascii_alphanumeric\0".as_ptr()) })
                        || (mlen == 14
                            && unsafe { z_eq(mname, mlen, b"is_ascii_digit\0".as_ptr()) })
                        || (mlen == 19
                            && unsafe { z_eq(mname, mlen, b"is_ascii_alphabetic\0".as_ptr()) })
                        || (mlen == 14
                            && unsafe { z_eq(mname, mlen, b"is_ascii_upper\0".as_ptr()) })
                        || (mlen == 13
                            && unsafe { z_eq(mname, mlen, b"is_ascii_lower\0".as_ptr()) }))
                {
                    let recv = unsafe { *kids.add(0) };
                    let argk = unsafe { *kids.add(2) };
                    if unsafe { (*argk).n_kids } as usize == 0 {
                        let sb = self.arena_tmp();
                        let sl = unsafe { self.expr_ctype(recv, sb, 128, locals) };
                        let is_char = sl == 9
                            && unsafe { z_eq(sb, 9, b"uint32_t\0".as_ptr()) };
                        if is_char {
                            let n2 = unsafe { zput(out, cap, 0, b"bool\0".as_ptr()) };
                            return if n2 >= cap { 0 } else { n2 };
                        }
                    }
                }
                /* Lock plane: `.lock()` on an rsx_lock_<row> receiver
                 * types as the payload pointer (the guard IS &value —
                 * deref/deref-assign go through it). try_lock types the
                 * same (NULL when not acquired). */
                if (mlen == 4 || mlen == 9)
                    && (unsafe { z_eq(mname, mlen, b"lock\0".as_ptr()) }
                        || unsafe { z_eq(mname, mlen, b"try_lock\0".as_ptr()) })
                {
                    let rbuf = self.arena_tmp();
                    let rl = unsafe { self.expr_ctype(*kids.add(0), rbuf, 128, locals) };
                    if rl > 9 && rl < 128 && unsafe { z_eq(rbuf, 9, b"rsx_lock_\0".as_ptr()) } {
                        let row = unsafe { self.locks.find_by_name(rbuf, rl) };
                        if row < LOCK_CAP {
                            let el = self.locks.elem_lens[row];
                            let at = unsafe {
                                bput(out, cap, 0, self.locks.elems[row].as_ptr(), el)
                            };
                            if at < cap {
                                let at2 = unsafe { bput(out, cap, at, b" *\0".as_ptr(), 2) };
                                unsafe {
                                    if at2 < cap {
                                        *out.add(at2) = 0;
                                    } else if cap > 0 {
                                        *out.add(cap - 1) = 0;
                                    }
                                }
                                return if at2 >= cap { 0 } else { at2 };
                            }
                            unsafe {
                                if cap > 0 {
                                    *out.add(cap - 1) = 0;
                                }
                            }
                            return if at >= cap { 0 } else { at };
                        }
                    }
                }
                /* `.as_ref()` on a struct-shaped Option (rsx_opt_<..>), by
                 * value or behind a pointer: the payload pointer (Some ->
                 * &._v, None -> NULL). A pointer-shaped Option already
                 * types through the plain PATH arm. */
                if mlen == 6 && unsafe { z_eq(mname, mlen, b"as_ref\0".as_ptr()) } {
                    let rbuf = self.arena_tmp();
                    let rl = unsafe { self.expr_ctype(*kids.add(0), rbuf, 128, locals) };
                    if rl > 0 && rl < 128 {
                        let mut sp = rbuf;
                        let mut sl = rl;
                        if sl >= 6 && unsafe { z_eq(sp, 6, b"const \0".as_ptr()) } {
                            sp = unsafe { sp.add(6) };
                            sl -= 6;
                        }
                        /* a receiver that IS a pointer to the struct-Option
                         * (the lock guard): strip the trailing ` *` — the
                         * payload spelling is the pointee's element. */
                        let mut behind_ptr = false;
                        if sl > 2 && unsafe { *sp.add(sl - 1) } == b'*' {
                            behind_ptr = true;
                            sl -= 1;
                            while sl > 0 && unsafe { *sp.add(sl - 1) } == b' ' {
                                sl -= 1;
                            }
                        }
                        if sl > 8 && unsafe { z_eq(sp, 8, b"rsx_opt_\0".as_ptr()) } {
                            let el = unsafe { Lower::opt_typedef_elem(sp, sl, out, cap) };
                            if el > 0 && el + 2 < cap {
                                let at = unsafe { bput(out, cap, el, b" *\0".as_ptr(), 2) };
                                unsafe {
                                    if at < cap {
                                        *out.add(at) = 0;
                                    } else if cap > 0 {
                                        *out.add(cap - 1) = 0;
                                    }
                                }
                                return if at >= cap { 0 } else { at };
                            }
                            return 0;
                        }
                    }
                }
                /* `.into_iter()` — consuming identity on the subset's
                 * containers: Vec<T>, &[T], String/&str. The iterator IS
                 * the container (index/cursor loops), so the type is the
                 * receiver's own. */
                if mlen == 9 && unsafe { z_eq(mname, mlen, b"into_iter\0".as_ptr()) } {
                    let rbuf = self.arena_tmp();
                    let rl = unsafe { self.expr_ctype(*kids.add(0), rbuf, 128, locals) };
                    if rl > 0 && rl < 128 {
                        let keep = (rl > 8 && unsafe { z_eq(rbuf, 8, b"rsx_vec_\0".as_ptr()) })
                            || (rl > 8 && unsafe { z_eq(rbuf, 8, b"rsx_arr_\0".as_ptr()) })
                            || (rl == 9 && unsafe { z_eq(rbuf, 9, b"rsx_str_t\0".as_ptr()) })
                            || (rl == 13
                                && unsafe { z_eq(rbuf, 13, b"rsx_str_ref_t\0".as_ptr()) });
                        if keep {
                            let at = unsafe { bput(out, cap, 0, rbuf, rl) };
                            unsafe {
                                if at < cap {
                                    *out.add(at) = 0;
                                } else if cap > 0 {
                                    *out.add(cap - 1) = 0;
                                }
                            }
                            return if at >= cap { 0 } else { at };
                        }
                    }
                }
                /* &str plane: `.len()` on a fat rsx_str_ref_t types as
                 * size_t; `.as_ptr()`/`.as_bytes()` as const uint8_t *;
                 * `.is_empty()` as uint8_t (the kernel's bool). */
                if mlen == 3 && unsafe { z_eq(mname, mlen, b"len\0".as_ptr()) } {
                    let rbuf = self.arena_tmp();
                    let rl = unsafe { self.expr_ctype(*kids.add(0), rbuf, 128, locals) };
                    if rl == 13 && rl < 128 && unsafe { z_eq(rbuf, 13, b"rsx_str_ref_t\0".as_ptr()) } {
                        let at = unsafe { bput(out, cap, 0, b"size_t\0".as_ptr(), 6) };
                        unsafe {
                            if at < cap {
                                *out.add(at) = 0;
                            } else if cap > 0 {
                                *out.add(cap - 1) = 0;
                            }
                        }
                        return if at >= cap { 0 } else { at };
                    }
                    /* &[T] rows: .len() is the fat pair's n — size_t. */
                    if rl > 8 && rl < 128 && unsafe { z_eq(rbuf, 8, b"rsx_arr_\0".as_ptr()) } {
                        let at = unsafe { bput(out, cap, 0, b"size_t\0".as_ptr(), 6) };
                        unsafe {
                            if at < cap {
                                *out.add(at) = 0;
                            } else if cap > 0 {
                                *out.add(cap - 1) = 0;
                            }
                        }
                        return if at >= cap { 0 } else { at };
                    }
                }
                if (mlen == 6 || mlen == 9)
                    && (unsafe { z_eq(mname, mlen, b"as_ptr\0".as_ptr()) }
                        || unsafe { z_eq(mname, mlen, b"as_bytes\0".as_ptr()) })
                {
                    let rbuf = self.arena_tmp();
                    let rl = unsafe { self.expr_ctype(*kids.add(0), rbuf, 128, locals) };
                    if rl == 13 && rl < 128 && unsafe { z_eq(rbuf, 13, b"rsx_str_ref_t\0".as_ptr()) } {
                        let at = unsafe { bput(out, cap, 0, b"const uint8_t *\0".as_ptr(), 16) };
                        unsafe {
                            if at < cap {
                                *out.add(at) = 0;
                            } else if cap > 0 {
                                *out.add(cap - 1) = 0;
                            }
                        }
                        return if at >= cap { 0 } else { at };
                    }
                    /* &[T] rows: .as_ptr()/.as_bytes() is `const T *` —
                     * the element spelling off the interned row. */
                    if rl > 8 && rl < 128 && unsafe { z_eq(rbuf, 8, b"rsx_arr_\0".as_ptr()) } {
                        let row = unsafe { self.arrs.find_by_name(rbuf, rl) };
                        if row < ARR_CAP {
                            let el = self.arrs.elems[row].as_ptr();
                            let eln = self.arrs.elem_lens[row];
                            let at = unsafe { bput(out, cap, 0, b"const \0".as_ptr(), 6) };
                            let at2 = unsafe { bput(out, cap, at, el, eln) };
                            let at3 = unsafe { bput(out, cap, at2, b" *\0".as_ptr(), 2) };
                            unsafe {
                                if at3 < cap {
                                    *out.add(at3) = 0;
                                } else if cap > 0 {
                                    *out.add(cap - 1) = 0;
                                }
                            }
                            return if at3 >= cap { 0 } else { at3 };
                        }
                    }
                }
                if mlen == 9 && unsafe { z_eq(mname, mlen, b"is_empty\0".as_ptr()) } {
                    let rbuf = self.arena_tmp();
                    let rl = unsafe { self.expr_ctype(*kids.add(0), rbuf, 128, locals) };
                    /* `&Vec<T>` param (a `const rsx_vec_<row> *` C
                     * spelling) auto-derefs to the row. */
                    let mut vb6 = 0usize;
                    if rl > 6 && unsafe { z_eq(rbuf, 6, b"const \0".as_ptr()) } {
                        vb6 = 6;
                    }
                    let vc6 = rl - vb6;
                    let ref_vec = vc6 > 10
                        && unsafe { *rbuf.add(rl - 1) } == b'*'
                        && unsafe { z_eq(rbuf.add(vb6), 8, b"rsx_vec_\0".as_ptr()) };
                    if (rl == 13 && rl < 128 && unsafe { z_eq(rbuf, 13, b"rsx_str_ref_t\0".as_ptr()) })
                        || ref_vec
                        || (vc6 > 8 && unsafe { z_eq(rbuf.add(vb6), 8, b"rsx_arr_\0".as_ptr()) })
                        || (rl == 9 && unsafe { z_eq(rbuf, 9, b"rsx_str_t\0".as_ptr()) })
                    {
                        let at = unsafe { bput(out, cap, 0, b"uint8_t\0".as_ptr(), 7) };
                        unsafe {
                            if at < cap {
                                *out.add(at) = 0;
                            } else if cap > 0 {
                                *out.add(cap - 1) = 0;
                            }
                        }
                        return if at >= cap { 0 } else { at };
                    }
                }
                /* ---- owned-String plane typing ----
                 *
                 * Every String method whose C shape is knowable up front
                 * types without consulting the receiver (the plane is
                 * monomorphic — one rsx_str_t, no per-instance rows). */
                /* `.as_str()` on an owned String: a borrow of its bytes —
                 * the fat rsx_str_ref_t view. */
                if mlen == 6 && unsafe { z_eq(mname, mlen, b"as_str\0".as_ptr()) } {
                    let rbuf = self.arena_tmp();
                    let rl = unsafe { self.expr_ctype(*kids.add(0), rbuf, 128, locals) };
                    if rl == 9 && unsafe { z_eq(rbuf, 9, b"rsx_str_t\0".as_ptr()) } {
                        self.str_ref_used = true;
                        return unsafe { zput(out, cap, 0, b"rsx_str_ref_t\0".as_ptr()) };
                    }
                }
                if mlen == 9 && unsafe { z_eq(mname, mlen, b"to_string\0".as_ptr()) } {
                    self.str_own_used = true;
                    return unsafe { zput(out, cap, 0, b"rsx_str_t\0".as_ptr()) };
                }
                if mlen == 8 && unsafe { z_eq(mname, mlen, b"to_owned\0".as_ptr()) } {
                    self.str_own_used = true;
                    return unsafe { zput(out, cap, 0, b"rsx_str_t\0".as_ptr()) };
                }
                if mlen == 10 && unsafe { z_eq(mname, mlen, b"into_owned\0".as_ptr()) } {
                    self.str_own_used = true;
                    return unsafe { zput(out, cap, 0, b"rsx_str_t\0".as_ptr()) };
                }
                if mlen == 5 && unsafe { z_eq(mname, mlen, b"clone\0".as_ptr()) } {
                    /* clone on a String receiver — the owned plane's
                     * deep copy (the Vec rows' .clone() keeps its row). */
                    let rbuf = self.arena_tmp();
                    let rl = unsafe { self.expr_ctype(*kids.add(0), rbuf, 128, locals) };
                    if rl == 9 && unsafe { z_eq(rbuf, 9, b"rsx_str_t\0".as_ptr()) } {
                        self.str_own_used = true;
                        return unsafe { zput(out, cap, 0, b"rsx_str_t\0".as_ptr()) };
                    }
                }
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
                /* .is_empty() fallback: any fat/value container receiver
                 * (&str, &[T], &Vec<T>, String, Vec<T>) — uint8_t. The
                 * receiver-probing block above sits inside the n_kids>=3
                 * gate; this one catches every shape the emission's
                 * is_empty arm accepts. */
                if !user_method && unsafe { z_eq(mname, mlen, b"is_empty\0".as_ptr()) } {
                    let rbuf = self.arena_tmp();
                    let rl = unsafe { self.expr_ctype(*kids.add(0), rbuf, 128, locals) };
                    let mut vb7 = 0usize;
                    if rl > 6 && unsafe { z_eq(rbuf, 6, b"const \0".as_ptr()) } {
                        vb7 = 6;
                    }
                    let vc7 = rl - vb7;
                    let ok7 = rl > 0
                        && ((rl == 13 && unsafe { z_eq(rbuf, 13, b"rsx_str_ref_t\0".as_ptr()) })
                            || (rl == 9 && unsafe { z_eq(rbuf, 9, b"rsx_str_t\0".as_ptr()) })
                            || (vc7 > 8 && unsafe { z_eq(rbuf.add(vb7), 8, b"rsx_vec_\0".as_ptr()) })
                            || (vc7 > 8 && unsafe { z_eq(rbuf.add(vb7), 8, b"rsx_arr_\0".as_ptr()) }));
                    if ok7 {
                        let at = unsafe { bput(out, cap, 0, b"uint8_t\0".as_ptr(), 7) };
                        unsafe {
                            if at < cap {
                                *out.add(at) = 0;
                            } else if cap > 0 {
                                *out.add(cap - 1) = 0;
                            }
                        }
                        return if at >= cap { 0 } else { at };
                    }
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
                 * integer type (position's unwrap_or is size_t either way).
                 * An Option receiver's unwrap_or is the PAYLOAD — decode
                 * the rsx_opt_<elem> name back to the elem spelling. */
                if !user_method
                    && (unsafe { z_eq(mname, mlen, b"div_ceil\0".as_ptr()) }
                        || unsafe { z_eq(mname, mlen, b"unwrap_or\0".as_ptr()) })
                {
                    let rbuf = self.arena_tmp();
                    let rl = unsafe { self.expr_ctype(*kids.add(0), rbuf, 128, locals) };
                    if rl > 8
                        && rl < 128
                        && unsafe { z_eq(rbuf, 8, b"rsx_opt_\0".as_ptr()) }
                    {
                        let el = unsafe {
                            Lower::opt_typedef_elem(rbuf, rl, out, cap)
                        };
                        if el > 0 {
                            return el;
                        }
                    }
                    let mut i5 = 0usize;
                    while i5 < rl && i5 < cap {
                        unsafe {
                            *out.add(i5) = *rbuf.add(i5);
                        }
                        i5 += 1;
                    }
                    if rl < cap {
                        unsafe {
                            *out.add(rl) = 0;
                        }
                    }
                    return if rl >= cap { 0 } else { rl };
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
        /* String -> the owned-string plane: rsx_str_t { char *p; size_t
         * n, cap; } — the same one-row-for-all-instances contract as the
         * &str fat reference (rsx_str_ref_t), monomorphic because
         * String has no type parameters. Marked used here so
         * str_own_emit_rest emits the typedef before any reference; the
         * owned plane always emits <stdlib.h> (realloc/free) for its
         * ops, so no other include decision can dangle. */
        if unsafe { z_eq(s, n, b"String\0".as_ptr()) } {
            self.str_own_used = true;
            return unsafe { zput(out, cap, 0, b"rsx_str_t\0".as_ptr()) };
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
    /* Lock guards: a local whose init was `.lock()` holds the receiver's
     * rendered lvalue text (arena span, NUL'd) — the block epilogue emits
     * pm_util_lock_release(&<addr>.raw) when the scope dies. NULL = not
     * a guard. Reuse rows keep their guard note only in the same scope:
     * drop_scope rewinds n, so stale notes die with their entries. */
    guard_addrs: [*const u8; LOCAL_CAP],
    guard_lens: [usize; LOCAL_CAP],
    /* Rust type-changing shadow (`let t = t.trim()`): C cannot redeclare
     * a name in one scope, so the later binding declares a fresh C
     * spelling (name__N) and every later source reference to the name
     * emits the C spelling. Empty (len 0) = the C name is the source
     * name itself — the overwhelmingly common case, zero emission
     * cost. */
    cnames: [*const u8; LOCAL_CAP],
    cname_lens: [usize; LOCAL_CAP],
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
                    /* same binding re-registered (scope entry): its guard
                     * note, if any, was already consumed or belongs to
                     * this very binding — keep it (a re-execution of the
                     * same let re-locks; the scope-exit release still
                     * pairs with the epilogue's acquire). */
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

    /* Type-changing shadow registration: the row keeps the SOURCE name
     * (lookups by spelling) but carries a distinct C spelling (name__N)
     * the declaration and later references emit. */
    unsafe fn add_renamed(
        &mut self,
        name: *const u8,
        nlen: usize,
        cname: *const u8,
        clen: usize,
        ct: *const u8,
        ctlen: usize,
        depth: usize,
    ) {
        if self.n >= LOCAL_CAP || nlen >= 48 || clen >= 56 || ct.is_null() || ctlen == 0 {
            return;
        }
        let p = unsafe { pm_util_mem_alloc(self.arena, ctlen + 1) };
        if p.is_null() {
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
        self.names[s][nlen] = 0;
        self.name_lens[s] = nlen;
        self.ctypes[s] = sp;
        self.ctype_lens[s] = ctlen;
        /* the C spelling is an arena span (NUL'd) — the rename is rare,
         * so no fixed per-row slot burns table bytes on every local. */
        let cp = unsafe { pm_util_mem_alloc(self.arena, clen + 1) };
        if cp.is_null() {
            self.oom = true;
            return;
        }
        unsafe {
            core::ptr::copy_nonoverlapping(cname, cp, clen);
            *cp.add(clen) = 0;
        }
        self.cnames[s] = cp;
        self.cname_lens[s] = clen;
        self.depths[s] = depth;
        self.epochs[s] = self.epoch;
        self.n += 1;
    }

    /* The C type of a same-scope prior binding of this spelling, 0 if
     * none — the type-change test for rename-shadow. */
    unsafe fn same_scope_prior_type(
        &self,
        name: *const u8,
        nlen: usize,
        depth: usize,
        _epoch: usize,
        out: *mut u8,
    ) -> usize {
        let mut floor = 0usize;
        if self.nmarks > 0 {
            floor = self.marks[self.nmarks - 1];
        }
        let mut i = self.n;
        while i > floor {
            i -= 1;
            if self.name_lens[i] == nlen && self.depths[i] == depth && unsafe { self.span_eq(i, name, nlen) } {
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

    /* Tag the most recent binding as a lock guard holding <addr> (the
     * receiver's rendered lvalue). The block epilogue releases it when
     * the binding's scope dies. Arena span; a failed span drops the
     * note (the guard still binds — the release is skipped, and the
     * epilogue never dereferences a stale pointer). */
    unsafe fn mark_guard(&mut self, addr: *const u8, alen: usize) {
        if self.n == 0 || addr.is_null() || alen == 0 {
            return;
        }
        let p = unsafe { pm_util_mem_alloc(self.arena, alen + 1) };
        if p.is_null() {
            return;
        }
        unsafe {
            core::ptr::copy_nonoverlapping(addr, p, alen);
            *p.add(alen) = 0;
        }
        let s = self.n - 1;
        self.guard_addrs[s] = p;
        self.guard_lens[s] = alen;
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

    /* The C spelling a source name currently emits as (rename-shadow
     * rows carry name__N; every other row the name itself). Returns 0
     * when the name is not a local — the caller emits the raw name. */
    unsafe fn lookup_cname(&self, name: *const u8, nlen: usize, out: *mut u8) -> usize {
        let mut i = self.n;
        while i > 0 {
            i -= 1;
            if self.name_lens[i] == nlen && unsafe { self.span_eq(i, name, nlen) } {
                let cl = self.cname_lens[i];
                if cl == 0 {
                    return 0;
                }
                let cs = self.cnames[i];
                let mut j = 0usize;
                while j < cl {
                    unsafe {
                        *out.add(j) = *cs.add(j);
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

