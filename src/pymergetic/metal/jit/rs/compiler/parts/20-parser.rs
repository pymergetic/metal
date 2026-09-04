/* ================= parser ================= */

/* AST node arena. kids arrays grow by doubling; a finished node's kids array
 * is copied to an exact-size arena block so the dump/lower passes can size
 * them without a header. */
struct Node {
    arena: *mut pm_util_mem_arena_t,
    errbuf: *mut u8,
    errcap: usize,
    errline: u32,
    ok: bool,
}

impl Node {
    unsafe fn err(&mut self, msg: *const u8, line: u32) {
        if self.ok {
            unsafe {
                err_set(self.errbuf, self.errcap, msg, line);
            }
            self.ok = false;
        }
    }

    unsafe fn oom(&mut self, line: u32) {
        unsafe {
            self.err(b"arena exhausted\0".as_ptr(), line);
        }
    }

    /* Leaf / small node with inline text (NUL-terminated fixed string or an
     * already-arena-owned span — copied either way so the node owns bytes). */
    unsafe fn mk(
        &mut self,
        kind: pm_jit_rsx_ast_kind,
        line: u32,
        text: *const u8,
        text_len: usize,
    ) -> *mut pm_jit_rsx_ast_t {
        let p: *mut pm_jit_rsx_ast_t;
        let tp: *mut u8;
        if !self.ok {
            return core::ptr::null_mut();
        }
        p = unsafe {
            pm_util_mem_alloc(
                self.arena,
                core::mem::size_of::<pm_jit_rsx_ast_t>(),
            )
        } as *mut pm_jit_rsx_ast_t;
        if p.is_null() {
            unsafe {
                self.oom(line);
            }
            return core::ptr::null_mut();
        }
        unsafe {
            core::ptr::write(
                p,
                pm_jit_rsx_ast_t {
                    kind,
                    line,
                    text: core::ptr::null(),
                    text_len: 0,
                    kids: core::ptr::null_mut(),
                    n_kids: 0,
                    int_val: 0,
                    op_kind: pm_jit_rsx_tok_kind::PUNCT,
                },
            );
        }
        if !text.is_null() && text_len > 0 {
            tp = unsafe { pm_util_mem_alloc(self.arena, text_len + 1) };
            if tp.is_null() {
                unsafe {
                    self.oom(line);
                }
                return core::ptr::null_mut();
            }
            unsafe {
                core::ptr::copy_nonoverlapping(text, tp, text_len);
                *tp.add(text_len) = 0;
                (*p).text = tp;
                (*p).text_len = text_len;
            }
        } else if !text.is_null() {
            unsafe {
                (*p).text = text;
            }
        }
        p
    }

    /* Copy len bytes from src into a fresh arena block (NUL-terminated). */
    unsafe fn span(&mut self, src: *const u8, len: usize, line: u32) -> *const u8 {
        let p = unsafe { pm_util_mem_alloc(self.arena, len + 1) };
        if p.is_null() {
            unsafe {
                self.oom(line);
            }
            return b"\0".as_ptr();
        }
        unsafe {
            core::ptr::copy_nonoverlapping(src, p, len);
            *p.add(len) = 0;
        }
        p
    }

    /* Pack the kids slab into an exact-size arena block on the node. */
    unsafe fn set_kids(
        &mut self,
        n: *mut pm_jit_rsx_ast_t,
        src: *mut *mut pm_jit_rsx_ast_t,
        count: usize,
        line: u32,
    ) {
        let kp: *mut *mut pm_jit_rsx_ast_t;
        if n.is_null() {
            return;
        }
        if count == 0 {
            unsafe {
                (*n).kids = core::ptr::null_mut();
                (*n).n_kids = 0;
            }
            return;
        }
        kp = unsafe {
            pm_util_mem_alloc(
                self.arena,
                count * core::mem::size_of::<*mut pm_jit_rsx_ast_t>(),
            )
        } as *mut *mut pm_jit_rsx_ast_t;
        if kp.is_null() {
            unsafe {
                self.oom(line);
            }
            return;
        }
        unsafe {
            core::ptr::copy_nonoverlapping(src, kp, count);
            (*n).kids = kp;
            (*n).n_kids = count as u32;
        }
    }
}

/* Kid list: fixed 16-slot stack for small nodes, arena spill beyond. */
const KIDS_INLINE: usize = 16;

struct Kids {
    fixed: [*mut pm_jit_rsx_ast_t; KIDS_INLINE],
    spill: *mut *mut pm_jit_rsx_ast_t,
    spill_n: usize,
    n: usize,
}

impl Kids {
    unsafe fn new() -> Kids {
        Kids {
            fixed: [
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                core::ptr::null_mut(),
            ],
            spill: core::ptr::null_mut(),
            spill_n: 0,
            n: 0,
        }
    }

    unsafe fn add(&mut self, k: *mut pm_jit_rsx_ast_t, arena: *mut pm_util_mem_arena_t) {
        if k.is_null() {
            return;
        }
        if self.n < KIDS_INLINE {
            self.fixed[self.n] = k;
            self.n += 1;
            return;
        }
        if self.spill_n == 0 || self.n - KIDS_INLINE >= self.spill_n {
            let ncap = if self.spill_n == 0 { 32 } else { self.spill_n * 2 };
            let nb = unsafe {
                pm_util_mem_alloc(
                    arena,
                    ncap * core::mem::size_of::<*mut pm_jit_rsx_ast_t>(),
                )
            } as *mut *mut pm_jit_rsx_ast_t;
            if nb.is_null() {
                return;
            }
            if self.spill_n > 0 {
                unsafe {
                    core::ptr::copy_nonoverlapping(self.spill, nb, self.spill_n);
                }
            }
            self.spill = nb;
            self.spill_n = ncap;
        }
        unsafe {
            *self.spill.add(self.n - KIDS_INLINE) = k;
        }
        self.n += 1;
    }

    /* Build the packed slab for set_kids (arena, exact size). Element count
     * stays in `self.n` — this subset has no tuple returns. */
    unsafe fn pack(&self, arena: *mut pm_util_mem_arena_t) -> *mut *mut pm_jit_rsx_ast_t {
        let kp: *mut *mut pm_jit_rsx_ast_t;
        if self.n == 0 {
            return core::ptr::null_mut();
        }
        kp = unsafe {
            pm_util_mem_alloc(
                arena,
                self.n * core::mem::size_of::<*mut pm_jit_rsx_ast_t>(),
            )
        } as *mut *mut pm_jit_rsx_ast_t;
        if kp.is_null() {
            return core::ptr::null_mut();
        }
        let i_in = 0usize;
        let mut i = i_in;
        while i < self.n && i < KIDS_INLINE {
            unsafe {
                *kp.add(i) = self.fixed[i];
            }
            i += 1;
        }
        i = i_in;
        while self.n > KIDS_INLINE && i < self.n - KIDS_INLINE {
            unsafe {
                *kp.add(i + KIDS_INLINE) = *self.spill.add(i);
            }
            i += 1;
        }
        kp
    }
}

/* ---- cfg evaluation (`#[cfg(..)]` attrs against the active feature set) ----
 *
 * The ATTR text is the attribute's tokens joined with single spaces, e.g.
 * `# cfg ( feature = "gen" )`. Predicates: feature = "x", not(..), all(..),
 * any(..), test. Unknown predicates evaluate FALSE (never enable code the
 * subset cannot see). */

/* feature "x" active? feats: NUL-terminated comma list (no spaces). */
unsafe fn cfg_feat_active(feats: *const u8, name: *const u8, name_len: usize) -> bool {
    if feats.is_null() || name_len == 0 {
        return false;
    }
    let mut p = feats;
    unsafe {
        while *p != 0 {
            let seg = p;
            while *p != 0 && *p != b',' {
                p = p.add(1);
            }
            let seg_len = p as usize - seg as usize;
            if seg_len == name_len {
                let mut i = 0usize;
                let mut eq = true;
                while i < seg_len {
                    if *seg.add(i) != *name.add(i) {
                        eq = false;
                        break;
                    }
                    i += 1;
                }
                if eq {
                    return true;
                }
            }
            if *p == b',' {
                p = p.add(1);
            }
        }
    }
    false
}

/* Evaluate the cfg predicate at s[..len) — COMPACT form (no spaces):
 * feature="x", not(..), all(..), any(..), test.
 * Returns: 1 true, 0 false, -1 malformed (treated as false by the caller). */
unsafe fn cfg_eval(s: *const u8, len: usize, feats: *const u8) -> i32 {
    unsafe {
        if len >= 8 && z_eq(s, 7, b"feature\0".as_ptr()) && *s.add(7) == b'=' {
            /* feature="x" — the value is the quoted run right after '=' */
            let mut i = 8usize;
            while i < len && *s.add(i) != b'"' {
                i += 1;
            }
            if i >= len {
                return -1;
            }
            i += 1;
            let v = s.add(i);
            while i < len && *s.add(i) != b'"' {
                i += 1;
            }
            if i >= len {
                return -1;
            }
            return cfg_feat_active(feats, v, i - (v as usize - s as usize)) as i32;
        }
        if len >= 4 && z_eq(s, 3, b"not\0".as_ptr()) && *s.add(3) == b'(' {
            /* not(inner) — the inner is s[4..len-1] (caller trims the
             * matching ')' by scanning, so len-1 is that ')' here). */
            let mut depth = 1i32;
            let mut q = 4usize;
            let mut close = len;
            while q < len {
                if *s.add(q) == b'(' {
                    depth += 1;
                } else if *s.add(q) == b')' {
                    depth -= 1;
                    if depth == 0 {
                        close = q;
                        break;
                    }
                }
                q += 1;
            }
            if close <= 4 {
                return -1;
            }
            let v = cfg_eval(s.add(4), close - 4, feats);
            return (v <= 0) as i32;
        }
        /* all(..) / any(..) — segments split at depth-1 `,`; the closing
         * `)` is NOT part of a segment. The last segment ends at the
         * `)` that returns depth to 0 — evaluate it before returning
         * (the pre-fix loop broke on depth==0 without folding the
         * trailing segment, so any(a, b) never saw b). */
        if (len >= 4 && z_eq(s, 3, b"all\0".as_ptr()) && *s.add(3) == b'(')
            || (len >= 4 && z_eq(s, 3, b"any\0".as_ptr()) && *s.add(3) == b'(')
        {
            let is_all = unsafe { z_eq(s, 3, b"all\0".as_ptr()) };
            let mut i = 4usize;
            let mut depth = 1i32;
            let mut acc: i32 = if is_all { 1 } else { 0 };
            while i < len {
                let st = i;
                let mut closed = false;
                while i < len {
                    if *s.add(i) == b'(' {
                        depth += 1;
                    } else if *s.add(i) == b')' {
                        depth -= 1;
                        if depth == 0 {
                            closed = true;
                            break;
                        }
                    } else if *s.add(i) == b',' && depth == 1 {
                        break;
                    }
                    i += 1;
                }
                if i > st {
                    let v = cfg_eval(s.add(st), i - st, feats);
                    if is_all {
                        if v <= 0 {
                            acc = 0;
                        }
                    } else if v > 0 {
                        acc = 1;
                    }
                }
                if closed {
                    return acc;
                }
                if i < len && *s.add(i) == b',' {
                    i += 1;
                } else {
                    return acc;
                }
            }
            return acc;
        }
        if len == 4 && z_eq(s, 4, b"test\0".as_ptr()) {
            return 0;
        }
        -1
    }
}

struct Parser {
    arena: *mut pm_util_mem_arena_t,
    toks: *const pm_jit_rsx_token_t,
    n_toks: u32,
    at: u32,
    nd: Node,
    ok: bool,
    /* true while parsing a `while`/`if`/`for` condition: a `{` after a path
     * is the body block, never a struct literal. */
    cond_ctx: bool,
    /* true while parsing an `if` condition: a top-level `&&` is let-chain
     * glue (Rust 2024 `if a && let Some(x) = e`), not a binary operator.
     * Cleared inside `(` — parentheses bind `&&` as an operator again. */
    chain_ctx: bool,
    /* `>>` (SHR) closes up to two open generic lists. When a generic list
     * eats a SHR token as its own `>`, the second close belongs to the
     * enclosing list: this counter is how the enclosing loop learns. */
    shr_closes: u32,
    /* Active cargo features for cfg evaluation (NUL-terminated comma
     * list, NULL = none): `#[cfg(feature = "x")]` items strip unless
     * "x" is listed. `test` is never active. */
    feats: *const u8,
}

impl Parser {
    /* ---- token access ---- */

    unsafe fn tok(&self, i: u32) -> *const pm_jit_rsx_token_t {
        if i < self.n_toks {
            unsafe { self.toks.add(i as usize) }
        } else {
            unsafe { self.toks.add((self.n_toks - 1) as usize) }
        }
    }

    unsafe fn kind(&self, i: u32) -> pm_jit_rsx_tok_kind {
        unsafe { (*self.tok(i)).kind }
    }

    unsafe fn line(&self, i: u32) -> u32 {
        unsafe { (*self.tok(i)).line }
    }

    unsafe fn text(&self, i: u32) -> *const u8 {
        unsafe { (*self.tok(i)).text }
    }

    unsafe fn text_len(&self, i: u32) -> usize {
        unsafe { (*self.tok(i)).text_len }
    }

    /* Ident text at i, or NULL when i is not the ident `z`. */
    unsafe fn is_kw(&self, i: u32, z: *const u8) -> bool {
        unsafe { self.kind(i) == pm_jit_rsx_tok_kind::IDENT && z_eq(self.text(i), self.text_len(i), z) }
    }

    unsafe fn is_punct(&self, i: u32, c: u8) -> bool {
        unsafe {
            self.kind(i) == pm_jit_rsx_tok_kind::PUNCT
                && self.text_len(i) == 1
                && *self.text(i) == c
        }
    }

    /* Binary operators do not continue a block-like expression (loop/if/
     * match/block) across a newline: `loop {..}` followed by `*ok = ..` on
     * the next line is two statements, never `loop{} * ok`. Plain operands
     * (a < src_len\n && …) and unsafe-block expressions keep the line-free
     * Rust grammar. */
    unsafe fn op_continues(&self, lhs: *mut pm_jit_rsx_ast_t) -> bool {
        if lhs.is_null() {
            return false;
        }
        let k = unsafe { (*lhs).kind };
        let blocky = k == pm_jit_rsx_ast_kind::LOOP
            || k == pm_jit_rsx_ast_kind::IF
            || k == pm_jit_rsx_ast_kind::MATCH
            || k == pm_jit_rsx_ast_kind::BLOCK;
        if !blocky {
            return true;
        }
        /* unsafe-blocks continue as expressions (`unsafe { .. }\n && ..`) */
        let tl = unsafe { (*lhs).text_len };
        let tp = unsafe { (*lhs).text };
        if tl == 6 && unsafe { z_eq(tp, tl, b"unsafe\0".as_ptr()) } {
            return true;
        }
        unsafe { self.line(self.at) == (*lhs).line }
    }

    unsafe fn err(&mut self, msg: *const u8) {
        let line = unsafe { self.line(self.at) };
        if self.ok {
            unsafe {
                self.nd.err(msg, line);
            }
            self.ok = false;
        }
    }

    /* ---- node helpers (wrap Node, always use self.ok) ---- */

    unsafe fn mk(
        &mut self,
        kind: pm_jit_rsx_ast_kind,
        line: u32,
        text: *const u8,
        text_len: usize,
    ) -> *mut pm_jit_rsx_ast_t {
        let p = unsafe { self.nd.mk(kind, line, text, text_len) };
        if !self.nd.ok {
            self.ok = false;
        }
        p
    }

    unsafe fn set_kids(
        &mut self,
        n: *mut pm_jit_rsx_ast_t,
        k: &Kids,
    ) {
        let line = 0u32;
        if n.is_null() {
            return;
        }
        let count = k.n;
        let packed = unsafe { k.pack(self.arena) };
        if packed.is_null() && count > 0 {
            unsafe {
                self.nd.oom((*n).line);
            }
            self.ok = false;
            return;
        }
        unsafe {
            self.nd.set_kids(n, packed, count, line);
        }
        if !self.nd.ok {
            self.ok = false;
        }
    }

    /* ---- outer attributes ---- */

    /* Parse `#[...]` into ATTR kids (attached to the following item). The
     * delimiters are tokens: PUNCT '[', contents, PUNCT ']'. */
    unsafe fn parse_outer_attrs(&mut self, kids: &mut Kids) {
        loop {
            if !unsafe { self.is_punct(self.at, b'#') } {
                return;
            }
            let line = unsafe { self.line(self.at) };
            if !unsafe { self.is_punct(self.at + 1, b'[') } {
                unsafe {
                    self.err(b"expected '[' after '#'\0".as_ptr());
                }
                return;
            }
            /* Copy the attribute text span [at, close) into the node text. */
            let start = self.at;
            let mut i = self.at + 2;
            let mut depth = 1i32;
            while i < self.n_toks && depth > 0 {
                let k = unsafe { self.kind(i) };
                if k == pm_jit_rsx_tok_kind::PUNCT {
                    if unsafe { self.is_punct(i, b'[') } {
                        depth += 1;
                    } else if unsafe { self.is_punct(i, b']') } {
                        depth -= 1;
                        if depth == 0 {
                            break;
                        }
                    }
                }
                i += 1;
            }
            if depth != 0 {
                unsafe {
                    self.err(b"unterminated attribute\0".as_ptr());
                }
                return;
            }
            /* Token-span -> text via the dump-style renderer is overkill;
             * attrs the lowering cares about are name + inner text, so join
             * token texts with single spaces into the ATTR node text. */
            let mut buf = Out::new(self.arena);
            unsafe {
                buf.puts(b"#\0".as_ptr());
                let mut j = start + 1;
                while j <= i {
                    if j > start + 1 {
                        buf.putc(b' ');
                    }
                    buf.put(unsafe { self.text(j) }, unsafe { self.text_len(j) });
                    j += 1;
                }
            }
            if !buf.ok {
                unsafe {
                    self.nd.oom(line);
                }
                self.ok = false;
                return;
            }
            let at_node = unsafe {
                self.nd.mk(
                    pm_jit_rsx_ast_kind::ATTR,
                    line,
                    buf.p,
                    buf.len,
                )
            };
            unsafe {
                kids.add(at_node, self.arena);
            }
            self.at = i + 1;
        }
    }

    /* ---- types ---- */

    unsafe fn parse_type(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        /* `dyn Trait` trait-object sugar: the subset spells the object
         * type by the trait's leaf name (a `&mut dyn GenSink` is the
         * trait-name reference) — skip the `dyn` and parse the path. */
        if unsafe { self.is_kw(self.at, b"dyn\0".as_ptr()) } {
            self.at += 1;
            return unsafe { self.parse_type() };
        }
        let k = unsafe { self.kind(self.at) };
        /* fn-ptr forms first: `fn(...)`, `unsafe extern "C" fn(...)`. */
        if k == pm_jit_rsx_tok_kind::IDENT {
            if unsafe { self.is_kw(self.at, b"fn\0".as_ptr()) }
                || unsafe { self.is_kw(self.at, b"unsafe\0".as_ptr()) }
                || unsafe { self.is_kw(self.at, b"extern\0".as_ptr()) }
            {
                return unsafe { self.parse_fn_ptr_type() };
            }
            return unsafe { self.parse_path_type() };
        }
        if k == pm_jit_rsx_tok_kind::PUNCT {
            if unsafe { self.is_punct(self.at, b'*') } {
                self.at += 1;
                let mut kid: *mut pm_jit_rsx_ast_t = core::ptr::null_mut();
                if unsafe { self.is_kw(self.at, b"const\0".as_ptr()) } {
                    kid = unsafe { self.mk(pm_jit_rsx_ast_kind::TYPE, line, b"const\0".as_ptr(), 5) };
                    self.at += 1;
                } else if unsafe { self.is_kw(self.at, b"mut\0".as_ptr()) } {
                    kid = unsafe { self.mk(pm_jit_rsx_ast_kind::TYPE, line, b"mut\0".as_ptr(), 3) };
                    self.at += 1;
                } else {
                    unsafe {
                        self.err(b"expected const or mut after '*'\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
                let inner = unsafe { self.parse_type() };
                let mut kids = Kids::new();
                unsafe {
                    kids.add(kid, self.arena);
                    kids.add(inner, self.arena);
                }
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::TYPE, line, b"*\0".as_ptr(), 1) };
                unsafe {
                    self.set_kids(n, &kids);
                }
                return n;
            }
            if unsafe { self.is_punct(self.at, b'&') } {
                self.at += 1;
                /* optional lifetime */
                if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::LIFETIME {
                    self.at += 1;
                }
                if unsafe { self.is_kw(self.at, b"mut\0".as_ptr()) } {
                    self.at += 1;
                    let inner = unsafe { self.parse_type() };
                    let mut kids = Kids::new();
                    unsafe {
                        kids.add(inner, self.arena);
                    }
                    let n = unsafe { self.mk(pm_jit_rsx_ast_kind::TYPE, line, b"&mut\0".as_ptr(), 4) };
                    unsafe {
                        self.set_kids(n, &kids);
                    }
                    return n;
                }
                let inner = unsafe { self.parse_type() };
                let mut kids = Kids::new();
                unsafe {
                    kids.add(inner, self.arena);
                }
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::TYPE, line, b"&\0".as_ptr(), 1) };
                unsafe {
                    self.set_kids(n, &kids);
                }
                return n;
            }
            if unsafe { self.is_punct(self.at, b'(') } {
                /* `()` is the unit type; `(A, B, ..)` is a tuple type — a
                 * TYPE node tagged "tuple" with one kid TYPE per element
                 * (the lower registers it as the named struct
                 * rsx_tuple_<sig> so every use site agrees on one C type). */
                if unsafe { self.is_punct(self.at + 1, b')') } {
                    self.at += 2;
                    return unsafe { self.mk(pm_jit_rsx_ast_kind::TYPE, line, b"()\0".as_ptr(), 2) };
                }
                self.at += 1;
                let mut kids = Kids::new();
                loop {
                    let elem = unsafe { self.parse_type() };
                    if !self.ok || elem.is_null() {
                        return core::ptr::null_mut();
                    }
                    unsafe {
                        kids.add(elem, self.arena);
                    }
                    if unsafe { self.is_punct(self.at, b',') } {
                        self.at += 1;
                        if unsafe { self.is_punct(self.at, b')') } {
                            break;
                        }
                        continue;
                    }
                    if unsafe { self.is_punct(self.at, b')') } {
                        break;
                    }
                    unsafe {
                        self.err(b"expected ',' or ')' in tuple type\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
                self.at += 1;
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::TYPE, line, b"tuple\0".as_ptr(), 5) };
                unsafe {
                    self.set_kids(n, &kids);
                }
                return n;
            }
            if unsafe { self.is_punct(self.at, b'[') } {
                self.at += 1;
                let elem = unsafe { self.parse_type() };
                let mut kids = Kids::new();
                unsafe {
                    kids.add(elem, self.arena);
                }
                let mut fixed = false;
                if unsafe { self.is_punct(self.at, b';') } {
                    self.at += 1;
                    let size: *mut pm_jit_rsx_ast_t;
                    if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::INT_LITERAL {
                        size = unsafe { self.mk(pm_jit_rsx_ast_kind::LITERAL, unsafe { self.line(self.at) }, self.text(self.at), self.text_len(self.at)) };
                        self.at += 1;
                    } else if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::IDENT {
                        /* const name or const expr length (e.g. [T; N], [T; N * 8]).
                         * Collect the tokens verbatim into one TYPE node. */
                        let sline = unsafe { self.line(self.at) };
                        let mut buf = Out::new(self.arena);
                        loop {
                            let k2 = unsafe { self.kind(self.at) };
                            if k2 == pm_jit_rsx_tok_kind::IDENT
                                || k2 == pm_jit_rsx_tok_kind::INT_LITERAL
                                || (k2 == pm_jit_rsx_tok_kind::PUNCT
                                    && (unsafe { self.is_punct(self.at, b'*') }
                                        || unsafe { self.is_punct(self.at, b'+') }
                                        || unsafe { self.is_punct(self.at, b'/') }))
                            {
                                unsafe {
                                    buf.put(unsafe { self.text(self.at) }, unsafe { self.text_len(self.at) });
                                }
                                self.at += 1;
                            } else {
                                break;
                            }
                        }
                        if !buf.ok || buf.len == 0 {
                            unsafe {
                                self.err(b"unsupported: non-literal array length\0".as_ptr());
                            }
                            return core::ptr::null_mut();
                        }
                        size = unsafe { self.mk(pm_jit_rsx_ast_kind::TYPE, sline, buf.p, buf.len) };
                    } else {
                        unsafe {
                            self.err(b"unsupported: non-literal array length\0".as_ptr());
                        }
                        return core::ptr::null_mut();
                    }
                    unsafe {
                        kids.add(size, self.arena);
                    }
                    fixed = true;
                }
                if !unsafe { self.is_punct(self.at, b']') } {
                    unsafe {
                        self.err(b"expected ']' in array type\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
                self.at += 1;
                let z = if fixed {
                    b"[;]\0".as_ptr()
                } else {
                    b"[]\0".as_ptr()
                };
                let zl = if fixed { 3 } else { 2 };
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::TYPE, line, z, zl) };
                unsafe {
                    self.set_kids(n, &kids);
                }
                return n;
            }
        }
        unsafe {
            self.err(b"expected type\0".as_ptr());
        }
        core::ptr::null_mut()
    }

    unsafe fn parse_fn_ptr_type(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        let mut kids = Kids::new();
        /* qualifiers */
        if unsafe { self.is_kw(self.at, b"unsafe\0".as_ptr()) } {
            let q = unsafe { self.mk(pm_jit_rsx_ast_kind::TYPE, line, b"unsafe\0".as_ptr(), 6) };
            unsafe {
                kids.add(q, self.arena);
            }
            self.at += 1;
        }
        if unsafe { self.is_kw(self.at, b"extern\0".as_ptr()) } {
            let q = unsafe { self.mk(pm_jit_rsx_ast_kind::TYPE, line, b"extern\0".as_ptr(), 6) };
            unsafe {
                kids.add(q, self.arena);
            }
            self.at += 1;
            if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::STRING_LITERAL {
                let abi = unsafe {
                    self.mk(
                        pm_jit_rsx_ast_kind::TYPE,
                        line,
                        self.text(self.at),
                        self.text_len(self.at),
                    )
                };
                unsafe {
                    kids.add(abi, self.arena);
                }
                self.at += 1;
            }
        }
        if !unsafe { self.is_kw(self.at, b"fn\0".as_ptr()) } {
            unsafe {
                self.err(b"expected 'fn' in fn-pointer type\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        self.at += 1;
        if !unsafe { self.is_punct(self.at, b'(') } {
            unsafe {
                self.err(b"expected '(' in fn-pointer type\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        self.at += 1;
        loop {
            if unsafe { self.is_punct(self.at, b')') } {
                self.at += 1;
                break;
            }
            if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::IDENT
                && unsafe { self.is_punct(self.at + 1, b':') }
            {
                /* named param in a fn-ptr type is legal Rust; skip name */
                self.at += 2;
            }
            let ty = unsafe { self.parse_type() };
            unsafe {
                kids.add(ty, self.arena);
            }
            if unsafe { self.is_punct(self.at, b',') } {
                self.at += 1;
                continue;
            }
        }
        let mut ret: *mut pm_jit_rsx_ast_t = core::ptr::null_mut();
        if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::ARROW {
            self.at += 1;
            ret = unsafe { self.parse_type() };
        }
        if ret.is_null() {
            /* No `-> T`: the fnptr kids always carry a ret node (last), so
             * absence is spelled as an explicit `void` — a synthetic TYPE
             * node, not a new AST kind (the kind table is a stable
             * __types__.h contract). */
            ret = unsafe { self.mk(pm_jit_rsx_ast_kind::TYPE, line, b"void\0".as_ptr(), 4) };
        }
        unsafe {
            kids.add(ret, self.arena);
        }
        let n = unsafe { self.mk(pm_jit_rsx_ast_kind::TYPE, line, b"fnptr\0".as_ptr(), 5) };
        unsafe {
            self.set_kids(n, &kids);
        }
        n
    }

    /* Path type: a::b::c<Args> — one optional generic list. */
    unsafe fn parse_path_type(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        let mut kids = Kids::new();
        loop {
            if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::IDENT {
                unsafe {
                    self.err(b"expected type name\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            let seg = unsafe {
                self.mk(
                    pm_jit_rsx_ast_kind::TYPE,
                    line,
                    self.text(self.at),
                    self.text_len(self.at),
                )
            };
            unsafe {
                kids.add(seg, self.arena);
            }
            self.at += 1;
            if unsafe { self.is_punct(self.at, b'<') } {
                /* `<` opens generics only when not a comparison — in type
                 * position it always does. */
                self.at += 1;
                loop {
                    /* An inner list consumed a SHR token that carried a
                     * second close for THIS list — done, nothing to eat. */
                    if self.shr_closes > 0 {
                        self.shr_closes -= 1;
                        break;
                    }
                    if unsafe { self.is_punct(self.at, b'>') } {
                        self.at += 1;
                        break;
                    }
                    /* `>>` lexes as one SHR: it closes this list and, when
                     * an outer generic list is open, that one too. Eat the
                     * token for this list and post the second close on the
                     * counter the outer loop checks above. */
                    if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::SHR {
                        self.at += 1;
                        self.shr_closes += 1;
                        break;
                    }
                    let before = self.at;
                    let g = unsafe { self.parse_type() };
                    unsafe {
                        kids.add(g, self.arena);
                    }
                    if !self.ok {
                        return core::ptr::null_mut();
                    }
                    if self.at == before {
                        unsafe {
                            self.err(b"expected '>' in generic list\0".as_ptr());
                        }
                        return core::ptr::null_mut();
                    }
                    if unsafe { self.is_punct(self.at, b',') } {
                        self.at += 1;
                        continue;
                    }
                }
                /* Path with generics ends here. The text spells "gpath" so
                 * the lower can tell a generic path from a plain multi-
                 * segment one — the AST kind table stays stable (TYPE). */
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::TYPE, line, b"gpath\0".as_ptr(), 5) };
                unsafe {
                    self.set_kids(n, &kids);
                }
                return n;
            }
            if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::DOUBLE_COLON {
                self.at += 1;
                continue;
            }
            break;
        }
        let n = unsafe { self.mk(pm_jit_rsx_ast_kind::TYPE, line, b"path\0".as_ptr(), 4) };
        unsafe {
            self.set_kids(n, &kids);
        }
        n
    }

    /* ---- patterns (match arms, for heads) ---- */

    /* After a literal (or negated literal) pattern atom: `..=`/`..` range
     * pattern (`b'0'..=b'7'`) and/or `|` or-pattern of literal atoms. The
     * shapes the lower knows: LITERAL, UNARY(-), PATH "range" [lo, hi],
     * PATH "or" [alts..]. */
    unsafe fn parse_pattern_suffix(
        &mut self,
        lo: *mut pm_jit_rsx_ast_t,
        line: u32,
    ) -> *mut pm_jit_rsx_ast_t {
        let mut alts = Kids::new();
        let mut cur = lo;
        loop {
            if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::RANGE {
                self.at += 1;
                /* open `..x` has no lower bound in this subset — literal
                 * atoms always carry one, so `.. hi` (from a bare `..`)
                 * is a prefix range the lower cannot test. Refuse. */
                let hk = unsafe { self.kind(self.at) };
                if hk != pm_jit_rsx_tok_kind::INT_LITERAL
                    && hk != pm_jit_rsx_tok_kind::FLOAT_LITERAL
                    && hk != pm_jit_rsx_tok_kind::CHAR_LITERAL
                    && hk != pm_jit_rsx_tok_kind::STRING_LITERAL
                    && hk != pm_jit_rsx_tok_kind::BYTE_STR_LITERAL
                    && !(hk == pm_jit_rsx_tok_kind::PUNCT
                        && unsafe { self.is_punct(self.at, b'-') })
                {
                    unsafe {
                        self.err(b"unsupported: open-ended range pattern\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
                let hi = unsafe { self.parse_pattern() };
                if !self.ok {
                    return core::ptr::null_mut();
                }
                let mut rk = Kids::new();
                unsafe {
                    rk.add(cur, self.arena);
                    rk.add(hi, self.arena);
                }
                cur = unsafe { self.mk(pm_jit_rsx_ast_kind::PATH, line, b"range\0".as_ptr(), 5) };
                unsafe {
                    self.set_kids(cur, &rk);
                }
            }
            unsafe {
                alts.add(cur, self.arena);
            }
            if unsafe { self.is_punct(self.at, b'|') } {
                self.at += 1;
                let nxt = unsafe { self.kind(self.at) };
                if nxt != pm_jit_rsx_tok_kind::INT_LITERAL
                    && nxt != pm_jit_rsx_tok_kind::FLOAT_LITERAL
                    && nxt != pm_jit_rsx_tok_kind::CHAR_LITERAL
                    && nxt != pm_jit_rsx_tok_kind::STRING_LITERAL
                    && nxt != pm_jit_rsx_tok_kind::BYTE_STR_LITERAL
                    && !(nxt == pm_jit_rsx_tok_kind::PUNCT
                        && unsafe { self.is_punct(self.at, b'-') })
                {
                    unsafe {
                        self.err(b"unsupported: or-pattern alternative\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
                cur = unsafe { self.parse_pattern() };
                if !self.ok {
                    return core::ptr::null_mut();
                }
                continue;
            }
            break;
        }
        if unsafe { alts.n } == 1 {
            let one: *mut pm_jit_rsx_ast_t = unsafe { *alts.fixed.as_ptr() };
            return one;
        }
        let n = unsafe { self.mk(pm_jit_rsx_ast_kind::PATH, line, b"or\0".as_ptr(), 2) };
        unsafe {
            self.set_kids(n, &alts);
        }
        n
    }

    unsafe fn parse_pattern(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        let mut k = unsafe { self.kind(self.at) };
        /* `mut ident` pattern binding (match arm / Some(mut x) inner):
         * mutability is a source-side annotation only — the lower's
         * bind decls are C locals (already assignable) — so skip the
         * `mut` and parse the binding itself. */
        if unsafe { self.is_kw(self.at, b"mut\0".as_ptr()) } {
            self.at += 1;
            k = unsafe { self.kind(self.at) };
        }
        /* Tuple pattern `(a, &b)`, `(i, &b)`, `Some((x, y))`'s inner — a
         * TUPLE node of sub-patterns. The lower destructures it against
         * the scrutinee's tuple type (let/match) or the enumerate pair
         * (for). `()` unit stays the unit pattern. */
        if k == pm_jit_rsx_tok_kind::PUNCT && unsafe { self.is_punct(self.at, b'(') } {
            self.at += 1;
            if unsafe { self.is_punct(self.at, b')') } {
                self.at += 1;
                return unsafe { self.mk(pm_jit_rsx_ast_kind::TUPLE, line, b"()\0".as_ptr(), 2) };
            }
            let mut kids = Kids::new();
            loop {
                if !self.ok {
                    return core::ptr::null_mut();
                }
                let sub = unsafe { self.parse_pattern() };
                if sub.is_null() {
                    return core::ptr::null_mut();
                }
                unsafe {
                    kids.add(sub, self.arena);
                }
                if unsafe { self.is_punct(self.at, b',') } {
                    self.at += 1;
                    if unsafe { self.is_punct(self.at, b')') } {
                        self.at += 1;
                        break;
                    }
                    continue;
                }
                if unsafe { self.is_punct(self.at, b')') } {
                    self.at += 1;
                    break;
                }
                unsafe {
                    self.err(b"expected ',' or ')' in tuple pattern\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            let n = unsafe { self.mk(pm_jit_rsx_ast_kind::TUPLE, line, b"tuple\0".as_ptr(), 5) };
            unsafe {
                self.set_kids(n, &kids);
            }
            return n;
        }
        if k == pm_jit_rsx_tok_kind::INT_LITERAL
            || k == pm_jit_rsx_tok_kind::FLOAT_LITERAL
            || k == pm_jit_rsx_tok_kind::CHAR_LITERAL
            || k == pm_jit_rsx_tok_kind::STRING_LITERAL
            || k == pm_jit_rsx_tok_kind::BYTE_STR_LITERAL
        {
            let n = unsafe {
                self.mk(
                    pm_jit_rsx_ast_kind::LITERAL,
                    line,
                    self.text(self.at),
                    self.text_len(self.at),
                )
            };
            self.at += 1;
            return unsafe { self.parse_pattern_suffix(n, line) };
        }
        if k == pm_jit_rsx_tok_kind::PUNCT && unsafe { self.is_punct(self.at, b'-') } {
            /* negative literal pattern */
            self.at += 1;
            if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::INT_LITERAL
                && unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::FLOAT_LITERAL
            {
                unsafe {
                    self.err(b"expected number after '-' in pattern\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            let lit = unsafe {
                self.mk(
                    pm_jit_rsx_ast_kind::LITERAL,
                    line,
                    self.text(self.at),
                    self.text_len(self.at),
                )
            };
            self.at += 1;
            let mut kids = Kids::new();
            unsafe {
                kids.add(lit, self.arena);
            }
            let n = unsafe { self.mk(pm_jit_rsx_ast_kind::UNARY, line, b"-\0".as_ptr(), 1) };
            unsafe {
                self.set_kids(n, &kids);
            }
            return unsafe { self.parse_pattern_suffix(n, line) };
        }
        if k == pm_jit_rsx_tok_kind::PUNCT && unsafe { self.is_punct(self.at, b'&') } {
            self.at += 1;
            if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::LIFETIME {
                self.at += 1;
            }
            if unsafe { self.is_kw(self.at, b"mut\0".as_ptr()) } {
                self.at += 1;
            }
            let inner = unsafe { self.parse_pattern() };
            let mut kids = Kids::new();
            unsafe {
                kids.add(inner, self.arena);
            }
            let n = unsafe { self.mk(pm_jit_rsx_ast_kind::UNARY, line, b"&\0".as_ptr(), 1) };
            unsafe {
                self.set_kids(n, &kids);
            }
            return n;
        }
        if k == pm_jit_rsx_tok_kind::IDENT {
            /* `_`, `None`, `Some(x)`, enum variants, plain bindings,
             * or-patterns of literals/variants. */
            let mut kids = Kids::new();
            let first = unsafe { self.parse_path_expr() };
            unsafe {
                kids.add(first, self.arena);
            }
            /* `Some(bind)` — the lower reads this as PATH kids [Some, bind]
             * (the bind declared at the arm body top). Only the single-bind
             * form: `Some((a, b))` and friends are tuple patterns, refused
             * below as an unsupported pattern form via the inner parse. */
            if unsafe { self.is_punct(self.at, b'(') } {
                self.at += 1;
                let inner = unsafe { self.parse_pattern() };
                if !self.ok {
                    return core::ptr::null_mut();
                }
                if !unsafe { self.is_punct(self.at, b')') } {
                    unsafe {
                        self.err(b"expected ')' in Some(bind) pattern\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
                self.at += 1;
                /* unwrap the path wrapper: the pattern node's kids are the
                 * path SEGMENTS (text = Some), then the bind. */
                let mut segs = Kids::new();
                if unsafe { (*first).kind } == pm_jit_rsx_ast_kind::PATH {
                    let fk = unsafe { (*first).kids };
                    let fkn = unsafe { (*first).n_kids } as usize;
                    let mut j = 0usize;
                    while j < fkn {
                        unsafe {
                            segs.add(*fk.add(j), self.arena);
                        }
                        j += 1;
                    }
                }
                if unsafe { segs.n } == 0 {
                    unsafe {
                        segs.add(first, self.arena);
                    }
                }
                unsafe {
                    segs.add(inner, self.arena);
                }
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::PATH, line, b"pat\0".as_ptr(), 3) };
                unsafe {
                    self.set_kids(n, &segs);
                }
                return n;
            }
            if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::PUNCT
                && unsafe { self.is_punct(self.at, b'|') }
            {
                loop {
                    if !unsafe { self.is_punct(self.at, b'|') } {
                        break;
                    }
                    self.at += 1;
                    let alt = unsafe { self.parse_path_expr() };
                    unsafe {
                        kids.add(alt, self.arena);
                    }
                }
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::PATH, line, b"or\0".as_ptr(), 2) };
                unsafe {
                    self.set_kids(n, &kids);
                }
                return n;
            }
            if kids.n == 1 {
                unsafe {
                    let one = *kids.fixed.as_ptr();
                    return one;
                }
            }
            let n = unsafe { self.mk(pm_jit_rsx_ast_kind::PATH, line, b"or\0".as_ptr(), 2) };
            unsafe {
                self.set_kids(n, &kids);
            }
            return n;
        }
        unsafe {
            self.err(b"unsupported: pattern form\0".as_ptr());
        }
        core::ptr::null_mut()
    }

    /* ---- expressions ---- */

    unsafe fn parse_expr(&mut self) -> *mut pm_jit_rsx_ast_t {
        unsafe { self.parse_assign_expr() }
    }

    unsafe fn parse_assign_expr(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        let lhs = unsafe { self.parse_range_expr() };
        if !self.ok || lhs.is_null() {
            return lhs;
        }
        let k = unsafe { self.kind(self.at) };
        if k == pm_jit_rsx_tok_kind::PUNCT
            && unsafe { self.is_punct(self.at, b'=') }
        {
            self.at += 1;
            let rhs = unsafe { self.parse_assign_expr() };
            let mut kids = Kids::new();
            unsafe {
                kids.add(lhs, self.arena);
                kids.add(rhs, self.arena);
            }
            let n = unsafe { self.mk(pm_jit_rsx_ast_kind::ASSIGN, line, b"=\0".as_ptr(), 1) };
            unsafe {
                self.set_kids(n, &kids);
            }
            return n;
        }
        if k == pm_jit_rsx_tok_kind::PLUSEQ
            || k == pm_jit_rsx_tok_kind::MINUSEQ
            || k == pm_jit_rsx_tok_kind::STAREQ
            || k == pm_jit_rsx_tok_kind::SLASHEQ
            || k == pm_jit_rsx_tok_kind::PERCENTEQ
            || k == pm_jit_rsx_tok_kind::CARETEQ
            || k == pm_jit_rsx_tok_kind::AMPEQ
            || k == pm_jit_rsx_tok_kind::OREQ
            || k == pm_jit_rsx_tok_kind::SHLEQ
            || k == pm_jit_rsx_tok_kind::SHREQ
        {
            /* op text is in the token (e.g. "+="). */
            let opz = unsafe { self.text(self.at) };
            let opn = unsafe { self.text_len(self.at) };
            self.at += 1;
            let rhs = unsafe { self.parse_assign_expr() };
            let mut kids = Kids::new();
            unsafe {
                kids.add(lhs, self.arena);
                kids.add(rhs, self.arena);
            }
            let n = unsafe {
                self.mk(
                    pm_jit_rsx_ast_kind::ASSIGN,
                    line,
                    opz,
                    opn,
                )
            };
            unsafe {
                self.set_kids(n, &kids);
            }
            return n;
        }
        lhs
    }

    unsafe fn parse_range_expr(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        let lhs = unsafe { self.parse_or_expr() };
        if !self.ok || lhs.is_null() {
            return lhs;
        }
        if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::RANGE {
            let inclusive = unsafe { self.text_len(self.at) } == 3;
            self.at += 1;
            let rhs = unsafe { self.parse_or_expr() };
            let mut kids = Kids::new();
            unsafe {
                kids.add(lhs, self.arena);
                kids.add(rhs, self.arena);
            }
            let z = if inclusive {
                b"..=\0".as_ptr()
            } else {
                b"..\0".as_ptr()
            };
            let n = unsafe { self.mk(pm_jit_rsx_ast_kind::BINARY, line, z, 2) };
            unsafe {
                self.set_kids(n, &kids);
            }
            return n;
        }
        lhs
    }

    unsafe fn parse_or_expr(&mut self) -> *mut pm_jit_rsx_ast_t {
        let mut lhs = unsafe { self.parse_and_expr() };
        loop {
            if !self.ok || lhs.is_null() {
                return lhs;
            }
            if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::OROR {
                return lhs;
            }
            if !unsafe { self.op_continues(lhs) } {
                return lhs;
            }
            let line = unsafe { self.line(self.at) };
            self.at += 1;
            let rhs = unsafe { self.parse_and_expr() };
            let mut kids = Kids::new();
            unsafe {
                kids.add(lhs, self.arena);
                kids.add(rhs, self.arena);
            }
            lhs = unsafe { self.mk(pm_jit_rsx_ast_kind::BINARY, line, b"||\0".as_ptr(), 2) };
            unsafe {
                self.set_kids(lhs, &kids);
            }
        }
    }

    unsafe fn parse_and_expr(&mut self) -> *mut pm_jit_rsx_ast_t {
        let mut lhs = unsafe { self.parse_cmp_expr() };
        loop {
            if !self.ok || lhs.is_null() {
                return lhs;
            }
            if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::ANDAND {
                return lhs;
            }
            /* let-chain glue: in an `if` condition a top-level `&&` whose
             * right side is a `let` joins chain segments (`a && let
             * Some(x) = e && let ..`); the if parser consumes it, never the
             * expression grammar. `&&` before a plain operand stays a
             * binary operator — existing `if a && b` output is unchanged
             * byte for byte. */
            if self.chain_ctx && unsafe { self.is_kw(self.at + 1, b"let\0".as_ptr()) } {
                return lhs;
            }
            if !unsafe { self.op_continues(lhs) } {
                return lhs;
            }
            let line = unsafe { self.line(self.at) };
            self.at += 1;
            let rhs = unsafe { self.parse_cmp_expr() };
            let mut kids = Kids::new();
            unsafe {
                kids.add(lhs, self.arena);
                kids.add(rhs, self.arena);
            }
            lhs = unsafe { self.mk(pm_jit_rsx_ast_kind::BINARY, line, b"&&\0".as_ptr(), 2) };
            unsafe {
                self.set_kids(lhs, &kids);
            }
        }
    }

    unsafe fn parse_cmp_expr(&mut self) -> *mut pm_jit_rsx_ast_t {
        let mut lhs = unsafe { self.parse_bitor_expr() };
        loop {
            if !self.ok || lhs.is_null() {
                return lhs;
            }
            let k = unsafe { self.kind(self.at) };
            let mut z: *const u8 = b"\0".as_ptr();
            let mut zlen = 0usize;
            if k == pm_jit_rsx_tok_kind::EQ {
                z = b"==\0".as_ptr();
                zlen = 2;
            } else if k == pm_jit_rsx_tok_kind::NE {
                z = b"!=\0".as_ptr();
                zlen = 2;
            } else if k == pm_jit_rsx_tok_kind::LE {
                z = b"<=\0".as_ptr();
                zlen = 2;
            } else if k == pm_jit_rsx_tok_kind::GE {
                z = b">=\0".as_ptr();
                zlen = 2;
            } else if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::PUNCT
                && unsafe { self.is_punct(self.at, b'<') }
            {
                z = b"<\0".as_ptr();
                zlen = 1;
            } else if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::PUNCT
                && unsafe { self.is_punct(self.at, b'>') }
            {
                z = b">\0".as_ptr();
                zlen = 1;
            } else {
                return lhs;
            }
            if !unsafe { self.op_continues(lhs) } {
                return lhs;
            }
            let line = unsafe { self.line(self.at) };
            self.at += 1;
            let rhs = unsafe { self.parse_bitor_expr() };
            let mut kids = Kids::new();
            unsafe {
                kids.add(lhs, self.arena);
                kids.add(rhs, self.arena);
            }
            lhs = unsafe { self.mk(pm_jit_rsx_ast_kind::BINARY, line, z, zlen) };
            unsafe {
                self.set_kids(lhs, &kids);
            }
        }
    }

    unsafe fn parse_bitor_expr(&mut self) -> *mut pm_jit_rsx_ast_t {
        let mut lhs = unsafe { self.parse_bitxor_expr() };
        loop {
            if !self.ok || lhs.is_null() {
                return lhs;
            }
            if !(unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::PUNCT
                && unsafe { self.is_punct(self.at, b'|') })
            {
                return lhs;
            }
            if !unsafe { self.op_continues(lhs) } {
                return lhs;
            }
            let line = unsafe { self.line(self.at) };
            self.at += 1;
            let rhs = unsafe { self.parse_bitxor_expr() };
            let mut kids = Kids::new();
            unsafe {
                kids.add(lhs, self.arena);
                kids.add(rhs, self.arena);
            }
            lhs = unsafe { self.mk(pm_jit_rsx_ast_kind::BINARY, line, b"|\0".as_ptr(), 1) };
            unsafe {
                self.set_kids(lhs, &kids);
            }
        }
    }

    unsafe fn parse_bitxor_expr(&mut self) -> *mut pm_jit_rsx_ast_t {
        let mut lhs = unsafe { self.parse_bitand_expr() };
        loop {
            if !self.ok || lhs.is_null() {
                return lhs;
            }
            if !(unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::PUNCT
                && unsafe { self.is_punct(self.at, b'^') })
            {
                return lhs;
            }
            if !unsafe { self.op_continues(lhs) } {
                return lhs;
            }
            let line = unsafe { self.line(self.at) };
            self.at += 1;
            let rhs = unsafe { self.parse_bitand_expr() };
            let mut kids = Kids::new();
            unsafe {
                kids.add(lhs, self.arena);
                kids.add(rhs, self.arena);
            }
            lhs = unsafe { self.mk(pm_jit_rsx_ast_kind::BINARY, line, b"^\0".as_ptr(), 1) };
            unsafe {
                self.set_kids(lhs, &kids);
            }
        }
    }

    unsafe fn parse_bitand_expr(&mut self) -> *mut pm_jit_rsx_ast_t {
        let mut lhs = unsafe { self.parse_shift_expr() };
        loop {
            if !self.ok || lhs.is_null() {
                return lhs;
            }
            if !(unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::PUNCT
                && unsafe { self.is_punct(self.at, b'&') })
            {
                return lhs;
            }
            if !unsafe { self.op_continues(lhs) } {
                return lhs;
            }
            let line = unsafe { self.line(self.at) };
            self.at += 1;
            let rhs = unsafe { self.parse_shift_expr() };
            let mut kids = Kids::new();
            unsafe {
                kids.add(lhs, self.arena);
                kids.add(rhs, self.arena);
            }
            lhs = unsafe { self.mk(pm_jit_rsx_ast_kind::BINARY, line, b"&\0".as_ptr(), 1) };
            unsafe {
                self.set_kids(lhs, &kids);
            }
        }
    }

    unsafe fn parse_shift_expr(&mut self) -> *mut pm_jit_rsx_ast_t {
        let mut lhs = unsafe { self.parse_add_expr() };
        loop {
            if !self.ok || lhs.is_null() {
                return lhs;
            }
            let k = unsafe { self.kind(self.at) };
            if k != pm_jit_rsx_tok_kind::SHL && k != pm_jit_rsx_tok_kind::SHR {
                return lhs;
            }
            if !unsafe { self.op_continues(lhs) } {
                return lhs;
            }
            let line = unsafe { self.line(self.at) };
            let z = if k == pm_jit_rsx_tok_kind::SHL {
                b"<<\0".as_ptr()
            } else {
                b">>\0".as_ptr()
            };
            self.at += 1;
            let rhs = unsafe { self.parse_add_expr() };
            let mut kids = Kids::new();
            unsafe {
                kids.add(lhs, self.arena);
                kids.add(rhs, self.arena);
            }
            lhs = unsafe { self.mk(pm_jit_rsx_ast_kind::BINARY, line, z, 2) };
            unsafe {
                self.set_kids(lhs, &kids);
            }
        }
    }

    unsafe fn parse_add_expr(&mut self) -> *mut pm_jit_rsx_ast_t {
        let mut lhs = unsafe { self.parse_mul_expr() };
        loop {
            if !self.ok || lhs.is_null() {
                return lhs;
            }
            if !(unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::PUNCT
                && (unsafe { self.is_punct(self.at, b'+') }
                    || unsafe { self.is_punct(self.at, b'-') }))
            {
                return lhs;
            }
            if !unsafe { self.op_continues(lhs) } {
                return lhs;
            }
            let line = unsafe { self.line(self.at) };
            let z = if unsafe { self.is_punct(self.at, b'+') } {
                b"+\0".as_ptr()
            } else {
                b"-\0".as_ptr()
            };
            self.at += 1;
            let rhs = unsafe { self.parse_mul_expr() };
            let mut kids = Kids::new();
            unsafe {
                kids.add(lhs, self.arena);
                kids.add(rhs, self.arena);
            }
            lhs = unsafe { self.mk(pm_jit_rsx_ast_kind::BINARY, line, z, 1) };
            unsafe {
                self.set_kids(lhs, &kids);
            }
        }
    }

    unsafe fn parse_mul_expr(&mut self) -> *mut pm_jit_rsx_ast_t {
        let mut lhs = unsafe { self.parse_cast_expr() };
        loop {
            if !self.ok || lhs.is_null() {
                return lhs;
            }
            if !(unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::PUNCT
                && (unsafe { self.is_punct(self.at, b'*') }
                    || unsafe { self.is_punct(self.at, b'/') }
                    || unsafe { self.is_punct(self.at, b'%') }))
            {
                return lhs;
            }
            if !unsafe { self.op_continues(lhs) } {
                return lhs;
            }
            let line = unsafe { self.line(self.at) };
            let z = if unsafe { self.is_punct(self.at, b'*') } {
                b"*\0".as_ptr()
            } else if unsafe { self.is_punct(self.at, b'/') } {
                b"/\0".as_ptr()
            } else {
                b"%\0".as_ptr()
            };
            self.at += 1;
            let rhs = unsafe { self.parse_cast_expr() };
            let mut kids = Kids::new();
            unsafe {
                kids.add(lhs, self.arena);
                kids.add(rhs, self.arena);
            }
            lhs = unsafe { self.mk(pm_jit_rsx_ast_kind::BINARY, line, z, 1) };
            unsafe {
                self.set_kids(lhs, &kids);
            }
        }
    }

    unsafe fn parse_cast_expr(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        let lhs = unsafe { self.parse_unary_expr() };
        if !self.ok || lhs.is_null() {
            return lhs;
        }
        if unsafe { self.is_kw(self.at, b"as\0".as_ptr()) } {
            self.at += 1;
            let ty = unsafe { self.parse_type() };
            let mut kids = Kids::new();
            unsafe {
                kids.add(lhs, self.arena);
                kids.add(ty, self.arena);
            }
            let n = unsafe { self.mk(pm_jit_rsx_ast_kind::CAST, line, b"as\0".as_ptr(), 2) };
            unsafe {
                self.set_kids(n, &kids);
            }
            return n;
        }
        lhs
    }

    unsafe fn parse_unary_expr(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::PUNCT {
            if unsafe { self.is_punct(self.at, b'!') } {
                self.at += 1;
                let inner = unsafe { self.parse_unary_expr() };
                let mut kids = Kids::new();
                unsafe {
                    kids.add(inner, self.arena);
                }
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::UNARY, line, b"!\0".as_ptr(), 1) };
                unsafe {
                    self.set_kids(n, &kids);
                }
                return n;
            }
            if unsafe { self.is_punct(self.at, b'-') } {
                self.at += 1;
                let inner = unsafe { self.parse_unary_expr() };
                let mut kids = Kids::new();
                unsafe {
                    kids.add(inner, self.arena);
                }
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::UNARY, line, b"-\0".as_ptr(), 1) };
                unsafe {
                    self.set_kids(n, &kids);
                }
                return n;
            }
            if unsafe { self.is_punct(self.at, b'*') } {
                self.at += 1;
                let inner = unsafe { self.parse_unary_expr() };
                let mut kids = Kids::new();
                unsafe {
                    kids.add(inner, self.arena);
                }
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::UNARY, line, b"*\0".as_ptr(), 1) };
                unsafe {
                    self.set_kids(n, &kids);
                }
                return n;
            }
            if unsafe { self.is_punct(self.at, b'&') } {
                self.at += 1;
                if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::LIFETIME {
                    self.at += 1;
                }
                let mut z = b"&\0".as_ptr();
                let mut zl = 1usize;
                if unsafe { self.is_kw(self.at, b"mut\0".as_ptr()) } {
                    z = b"&mut\0".as_ptr();
                    zl = 4;
                    self.at += 1;
                }
                let inner = unsafe { self.parse_unary_expr() };
                let mut kids = Kids::new();
                unsafe {
                    kids.add(inner, self.arena);
                }
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::UNARY, line, z, zl) };
                unsafe {
                    self.set_kids(n, &kids);
                }
                return n;
            }
        }
        unsafe { self.parse_postfix_expr() }
    }

    /* Postfix chain: call `f(a)`, method `a.m(b)`, field `a.f`, index `a[i]`. */
    unsafe fn parse_postfix_expr(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        let mut e = unsafe { self.parse_primary_expr() };
        /* Block-valued primaries (`if`/`match`/`loop`/`while`/`{..}`) never
         * take postfix ops in Rust — a following `(` belongs to the next
         * statement, not a call on the block. Without this guard, the postfix
         * loop eats the next statement's leading `(...)` as call args and the
         * parse silently corrupts everything after. */
        if !e.is_null() {
            let ek = unsafe { (*e).kind };
            if ek == pm_jit_rsx_ast_kind::IF
                || ek == pm_jit_rsx_ast_kind::MATCH
                || ek == pm_jit_rsx_ast_kind::LOOP
                || ek == pm_jit_rsx_ast_kind::WHILE
            {
                return e;
            }
            /* Plain statement blocks never take postfix ops (the `(` of the
             * next statement would be eaten as call args). An `unsafe { .. }`
             * block is an *expression* wrapper — `unsafe { f() }.len()` is
             * ordinary Rust and must keep parsing postfix. */
            if ek == pm_jit_rsx_ast_kind::BLOCK {
                let bt = unsafe { (*e).text };
                let bl = unsafe { (*e).text_len };
                let is_unsafe = bl == 6 && unsafe { z_eq(bt, bl, b"unsafe\0".as_ptr()) };
                if !is_unsafe {
                    return e;
                }
            }
        }
        loop {
            if !self.ok || e.is_null() {
                return e;
            }
            let k = unsafe { self.kind(self.at) };
            let is_dot = k == pm_jit_rsx_tok_kind::DOT
                || (k == pm_jit_rsx_tok_kind::PUNCT && unsafe { self.is_punct(self.at, b'.') });
            if is_dot {
                self.at += 1;
                /* `expr.<number>` (tuple field `.0`) parses when the token
                 * is a plain integer — the transparent-newtype unwrap; the
                 * emitter refuses it for any base that is not one. */
                let num_field = unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::INT_LITERAL;
                if !num_field && unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::IDENT {
                    unsafe {
                        self.err(b"unsupported: tuple field access\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
                if num_field {
                    let nname = unsafe { self.text(self.at) };
                    let nlen = unsafe { self.text_len(self.at) };
                    self.at += 1;
                    let nnode = unsafe {
                        self.mk(
                            pm_jit_rsx_ast_kind::PATH,
                            line,
                            nname,
                            nlen,
                        )
                    };
                    let mut kids = Kids::new();
                    unsafe {
                        kids.add(e, self.arena);
                        kids.add(nnode, self.arena);
                    }
                    e = unsafe { self.mk(pm_jit_rsx_ast_kind::FIELD, line, nname, nlen) };
                    unsafe {
                        self.set_kids(e, &kids);
                    }
                    continue;
                }
                let name = unsafe { self.text(self.at) };
                let name_len = unsafe { self.text_len(self.at) };
                let name_node = unsafe {
                    self.mk(
                        pm_jit_rsx_ast_kind::PATH,
                        unsafe { self.line(self.at) },
                        name,
                        name_len,
                    )
                };
                self.at += 1;
                /* `::<T>` turbofish on a method (`ptr.cast::<Route>()`):
                 * a single-ident target is kept as a 4th kid (a TYPE node)
                 * so the lower can use it; any other shape is skipped
                 * balanced (the method itself is refused later if it needs
                 * the type). */
                let mut gty: *mut pm_jit_rsx_ast_t = core::ptr::null_mut();
                if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::DOUBLE_COLON
                    && unsafe { self.kind(self.at + 1) } == pm_jit_rsx_tok_kind::PUNCT
                    && unsafe { self.is_punct(self.at + 1, b'<') }
                {
                    if unsafe { self.kind(self.at + 2) } == pm_jit_rsx_tok_kind::IDENT
                        && unsafe { self.kind(self.at + 3) } == pm_jit_rsx_tok_kind::PUNCT
                        && unsafe { self.is_punct(self.at + 3, b'>') }
                    {
                        gty = unsafe {
                            self.mk(
                                pm_jit_rsx_ast_kind::TYPE,
                                line,
                                self.text(self.at + 2),
                                self.text_len(self.at + 2),
                            )
                        };
                        self.at += 4;
                    } else {
                        self.at += 2;
                        let mut depth = 1i32;
                        while self.at < self.n_toks && depth > 0 {
                            let k = unsafe { self.kind(self.at) };
                            /* `>>`/`<<` lex as single tokens but open or
                             * close two generic lists each. */
                            if k == pm_jit_rsx_tok_kind::SHL {
                                depth += 2;
                                self.at += 1;
                                continue;
                            }
                            if k == pm_jit_rsx_tok_kind::SHR {
                                depth -= 2;
                                if depth <= 0 {
                                    /* leave `at` ON the SHR: the shared
                                     * tail below eats it as the final
                                     * closing token. */
                                    depth = 0;
                                    break;
                                }
                                self.at += 1;
                                continue;
                            }
                            if k == pm_jit_rsx_tok_kind::PUNCT {
                                if unsafe { self.is_punct(self.at, b'<') } {
                                    depth += 1;
                                } else if unsafe { self.is_punct(self.at, b'>') } {
                                    depth -= 1;
                                    if depth == 0 {
                                        break;
                                    }
                                }
                            }
                            self.at += 1;
                        }
                        if depth != 0 {
                            unsafe {
                                self.err(b"unterminated method turbofish\0".as_ptr());
                            }
                            return core::ptr::null_mut();
                        }
                        self.at += 1;
                    }
                }
                if unsafe { self.is_punct(self.at, b'(') } {
                    let args = unsafe { self.parse_call_args() };
                    let mut kids = Kids::new();
                    unsafe {
                        kids.add(e, self.arena);
                        kids.add(name_node, self.arena);
                        kids.add(args, self.arena);
                        if !gty.is_null() {
                            kids.add(gty, self.arena);
                        }
                    }
                    e = unsafe { self.mk(pm_jit_rsx_ast_kind::METHOD_CALL, line, name, name_len) };
                    unsafe {
                        self.set_kids(e, &kids);
                    }
                    continue;
                }
                let mut kids = Kids::new();
                unsafe {
                    kids.add(e, self.arena);
                    kids.add(name_node, self.arena);
                }
                e = unsafe { self.mk(pm_jit_rsx_ast_kind::FIELD, line, name, name_len) };
                unsafe {
                    self.set_kids(e, &kids);
                }
                continue;
            }
            if k == pm_jit_rsx_tok_kind::PUNCT && unsafe { self.is_punct(self.at, b'[') } {
                self.at += 1;
                /* index or range index: `a[i]`, `a[..n]`, `a[n..]`, `a[n..m]`.
                 * A range keeps the slice a pointer (start side only —
                 * rsx slice lengths are not carried, matching `&[T]`). */
                let idx = unsafe { self.parse_index_body() };
                if !unsafe { self.is_punct(self.at, b']') } {
                    unsafe {
                        self.err(b"expected ']'\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
                self.at += 1;
                let mut kids = Kids::new();
                unsafe {
                    kids.add(e, self.arena);
                    kids.add(idx, self.arena);
                }
                e = unsafe { self.mk(pm_jit_rsx_ast_kind::INDEX, line, b"[]\0".as_ptr(), 2) };
                unsafe {
                    self.set_kids(e, &kids);
                }
                continue;
            }
            if k == pm_jit_rsx_tok_kind::PUNCT && unsafe { self.is_punct(self.at, b'(') } {
                let args = unsafe { self.parse_call_args() };
                let mut kids = Kids::new();
                unsafe {
                    kids.add(e, self.arena);
                    kids.add(args, self.arena);
                }
                e = unsafe { self.mk(pm_jit_rsx_ast_kind::CALL, line, b"()\0".as_ptr(), 2) };
                unsafe {
                    self.set_kids(e, &kids);
                }
                continue;
            }
            if k == pm_jit_rsx_tok_kind::PUNCT && unsafe { self.is_punct(self.at, b'?') } {
                /* `expr?` — Try. UNARY with text "?" (the AST kind table is a
                 * stable __types__.h contract; a new kind would churn every
                 * face for one operator). */
                self.at += 1;
                let mut kids = Kids::new();
                unsafe {
                    kids.add(e, self.arena);
                }
                e = unsafe { self.mk(pm_jit_rsx_ast_kind::UNARY, line, b"?\0".as_ptr(), 1) };
                unsafe {
                    self.set_kids(e, &kids);
                }
                continue;
            }
            return e;
        }
    }

    /* Body of an index bracket (the `[` is consumed): either a plain
     * expression or a range `lo..hi` with either side optional. A missing
     * side is a TUPLE `()` node — the lowering reads bounds positionally.
     * Bounds parse at or-expr level so the `..` stays ours to read. */
    unsafe fn parse_index_body(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        let has_lo = unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::RANGE;
        if !has_lo && unsafe { self.is_punct(self.at, b']') } {
            /* `a[]` is not a range — empty index; refuse at the caller's
             * expected-expression wall by producing nothing sane. */
            unsafe {
                self.err(b"expected index expression\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        let lo = if has_lo {
            unsafe { self.parse_or_expr() }
        } else {
            unsafe { self.mk(pm_jit_rsx_ast_kind::TUPLE, line, b"()\0".as_ptr(), 2) }
        };
        if !self.ok || lo.is_null() {
            return lo;
        }
        if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::RANGE {
            return lo;
        }
        let inclusive = unsafe { self.text_len(self.at) } == 3;
        self.at += 1;
        let hi = if unsafe { self.is_punct(self.at, b']') } {
            unsafe { self.mk(pm_jit_rsx_ast_kind::TUPLE, line, b"()\0".as_ptr(), 2) }
        } else {
            unsafe { self.parse_or_expr() }
        };
        let mut kids = Kids::new();
        unsafe {
            kids.add(lo, self.arena);
            kids.add(hi, self.arena);
        }
        let z = if inclusive {
            b"..=\0".as_ptr()
        } else {
            b"..\0".as_ptr()
        };
        let n = unsafe { self.mk(pm_jit_rsx_ast_kind::BINARY, line, z, 2) };
        unsafe {
            self.set_kids(n, &kids);
        }
        n
    }

    /* `(` is at self.at; returns a TUPLE node of arg exprs. */
    unsafe fn parse_call_args(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        let mut kids = Kids::new();
        self.at += 1;
        loop {
            if unsafe { self.is_punct(self.at, b')') } {
                self.at += 1;
                break;
            }
            if !self.ok {
                return core::ptr::null_mut();
            }
            if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::END {
                unsafe {
                    self.err(b"expected ')' before end of file\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            let before = self.at;
            let a = unsafe { self.parse_expr() };
            unsafe {
                kids.add(a, self.arena);
            }
            if self.ok && self.at == before {
                unsafe {
                    self.err(b"internal: call arg consumed no tokens\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            if unsafe { self.is_punct(self.at, b',') } {
                self.at += 1;
                continue;
            }
            if !unsafe { self.is_punct(self.at, b')') } {
                unsafe {
                    self.err(b"expected ')' after call args\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
        }
        let n = unsafe { self.mk(pm_jit_rsx_ast_kind::TUPLE, line, b"args\0".as_ptr(), 4) };
        unsafe {
            self.set_kids(n, &kids);
        }
        n
    }

    unsafe fn parse_primary_expr(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        let k = unsafe { self.kind(self.at) };
        if k == pm_jit_rsx_tok_kind::INT_LITERAL
            || k == pm_jit_rsx_tok_kind::FLOAT_LITERAL
            || k == pm_jit_rsx_tok_kind::CHAR_LITERAL
            || k == pm_jit_rsx_tok_kind::STRING_LITERAL
            || k == pm_jit_rsx_tok_kind::BYTE_STR_LITERAL
        {
            let n = unsafe {
                self.mk(
                    pm_jit_rsx_ast_kind::LITERAL,
                    line,
                    self.text(self.at),
                    self.text_len(self.at),
                )
            };
            self.at += 1;
            return n;
        }
        if k == pm_jit_rsx_tok_kind::MACRO_INVOC {
            /* Preserve the whole invocation text for the lowering pass. */
            let n = unsafe {
                self.mk(
                    pm_jit_rsx_ast_kind::MACRO,
                    line,
                    self.text(self.at),
                    self.text_len(self.at),
                )
            };
            self.at += 1;
            return n;
        }
        if k == pm_jit_rsx_tok_kind::PUNCT {
            if unsafe { self.is_punct(self.at, b'(') } {
                self.at += 1;
                if unsafe { self.is_punct(self.at, b')') } {
                    self.at += 1;
                    return unsafe { self.mk(pm_jit_rsx_ast_kind::TUPLE, line, b"()\0".as_ptr(), 2) };
                }
                /* parentheses bind `&&` as the binary operator again —
                 * `if (a && b) && let ..` is one expr segment + a chain. */
                let save_chain = self.chain_ctx;
                self.chain_ctx = false;
                let e = unsafe { self.parse_expr() };
                self.chain_ctx = save_chain;
                if unsafe { self.is_punct(self.at, b',') } {
                    /* tuple expression */
                    let mut kids = Kids::new();
                    unsafe {
                        kids.add(e, self.arena);
                    }
                    loop {
                        if !unsafe { self.is_punct(self.at, b',') } {
                            break;
                        }
                        self.at += 1;
                        if unsafe { self.is_punct(self.at, b')') } {
                            break;
                        }
                        let a = unsafe { self.parse_expr() };
                        unsafe {
                            kids.add(a, self.arena);
                        }
                    }
                    if !unsafe { self.is_punct(self.at, b')') } {
                        unsafe {
                            self.err(b"expected ')'\0".as_ptr());
                        }
                        return core::ptr::null_mut();
                    }
                    self.at += 1;
                    let n = unsafe { self.mk(pm_jit_rsx_ast_kind::TUPLE, line, b"tuple\0".as_ptr(), 5) };
                    unsafe {
                        self.set_kids(n, &kids);
                    }
                    return n;
                }
                if !unsafe { self.is_punct(self.at, b')') } {
                    unsafe {
                        self.err(b"expected ')'\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
                self.at += 1;
                let mut kids = Kids::new();
                unsafe {
                    kids.add(e, self.arena);
                }
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::PAREN, line, b"()\0".as_ptr(), 2) };
                unsafe {
                    self.set_kids(n, &kids);
                }
                return n;
            }
            if unsafe { self.is_punct(self.at, b'{') } {
                return unsafe { self.parse_block() };
            }
            if unsafe { self.is_punct(self.at, b'[') } {
                /* array literal: `[a, b, ..]` or `[elem; count]` */
                self.at += 1;
                let mut kids = Kids::new();
                let mut is_repeat = false;
                if unsafe { self.is_punct(self.at, b']') } {
                    self.at += 1;
                    return unsafe { self.mk(pm_jit_rsx_ast_kind::ARRAY, line, b"[]\0".as_ptr(), 2) };
                }
                loop {
                    if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::END {
                        unsafe {
                            self.err(b"expected ']' in array literal\0".as_ptr());
                        }
                        return core::ptr::null_mut();
                    }
                    let e = unsafe { self.parse_expr() };
                    unsafe {
                        kids.add(e, self.arena);
                    }
                    if unsafe { self.is_punct(self.at, b';') } {
                        is_repeat = true;
                        self.at += 1;
                        let cnt = unsafe { self.parse_expr() };
                        unsafe {
                            kids.add(cnt, self.arena);
                        }
                        if !unsafe { self.is_punct(self.at, b']') } {
                            unsafe {
                                self.err(b"expected ']' after array repeat count\0".as_ptr());
                            }
                            return core::ptr::null_mut();
                        }
                        break;
                    }
                    if unsafe { self.is_punct(self.at, b',') } {
                        self.at += 1;
                        if unsafe { self.is_punct(self.at, b']') } {
                            break;
                        }
                        continue;
                    }
                    if !unsafe { self.is_punct(self.at, b']') } {
                        unsafe {
                            self.err(b"expected ',' or ']' in array literal\0".as_ptr());
                        }
                        return core::ptr::null_mut();
                    }
                    break;
                }
                self.at += 1;
                let z = if is_repeat {
                    b"[;]\0".as_ptr()
                } else {
                    b"[]\0".as_ptr()
                };
                let zl = if is_repeat { 3 } else { 2 };
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::ARRAY, line, z, zl) };
                unsafe {
                    self.set_kids(n, &kids);
                }
                return n;
            }
            if unsafe { self.is_punct(self.at, b'|') } {
                return unsafe { self.parse_closure() };
            }
        }
        /* `||` at expression start is an empty-param closure (the logical-or
         * operator never begins an expression). */
        if k == pm_jit_rsx_tok_kind::OROR {
            return unsafe { self.parse_closure() };
        }
        /* Labeled loop: `'name: loop/while/for …`. The label token's text
         * (with the leading `'`) rides on the loop node's text field — the
         * plain forms keep "loop"/"while"/"for" and the lowering tells the
         * two apart by the leading quote. */
        if k == pm_jit_rsx_tok_kind::LIFETIME
            && (unsafe { self.is_punct(self.at + 1, b':') })
            && (unsafe { self.is_kw(self.at + 2, b"loop\0".as_ptr()) }
                || unsafe { self.is_kw(self.at + 2, b"while\0".as_ptr()) }
                || unsafe { self.is_kw(self.at + 2, b"for\0".as_ptr()) })
        {
            let lname = unsafe { self.text(self.at) };
            let llen = unsafe { self.text_len(self.at) };
            self.at += 2;
            return unsafe { self.parse_labeled_or_plain_loop(lname, llen) };
        }
        if k == pm_jit_rsx_tok_kind::IDENT {
            if unsafe { self.is_kw(self.at, b"if\0".as_ptr()) } {
                return unsafe { self.parse_if_expr() };
            }
            if unsafe { self.is_kw(self.at, b"match\0".as_ptr()) } {
                return unsafe { self.parse_match_expr() };
            }
            if unsafe { self.is_kw(self.at, b"loop\0".as_ptr()) } {
                self.at += 1;
                let body = unsafe { self.parse_block() };
                let mut kids = Kids::new();
                unsafe {
                    kids.add(body, self.arena);
                }
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::LOOP, line, b"loop\0".as_ptr(), 4) };
                unsafe {
                    self.set_kids(n, &kids);
                }
                return n;
            }
            if unsafe { self.is_kw(self.at, b"while\0".as_ptr()) } {
                self.at += 1;
                let save = self.cond_ctx;
                self.cond_ctx = true;
                let cond = unsafe { self.parse_expr() };
                self.cond_ctx = save;
                let body = unsafe { self.parse_block() };
                let mut kids = Kids::new();
                unsafe {
                    kids.add(cond, self.arena);
                    kids.add(body, self.arena);
                }
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::WHILE, line, b"while\0".as_ptr(), 5) };
                unsafe {
                    self.set_kids(n, &kids);
                }
                return n;
            }
            if unsafe { self.is_kw(self.at, b"for\0".as_ptr()) } {
                self.at += 1;
                let pat = unsafe { self.parse_pattern() };
                if !unsafe { self.is_kw(self.at, b"in\0".as_ptr()) } {
                    unsafe {
                        self.err(b"expected 'in' in for loop\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
                self.at += 1;
                let save = self.cond_ctx;
                self.cond_ctx = true;
                let iter = unsafe { self.parse_expr() };
                self.cond_ctx = save;
                let body = unsafe { self.parse_block() };
                let mut kids = Kids::new();
                unsafe {
                    kids.add(pat, self.arena);
                    kids.add(iter, self.arena);
                    kids.add(body, self.arena);
                }
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::FOR, line, b"for\0".as_ptr(), 3) };
                unsafe {
                    self.set_kids(n, &kids);
                }
                return n;
            }
            if unsafe { self.is_kw(self.at, b"unsafe\0".as_ptr()) }
                && unsafe { self.kind(self.at + 1) } == pm_jit_rsx_tok_kind::PUNCT
                && unsafe { self.is_punct(self.at + 1, b'{') }
            {
                /* unsafe block expression */
                self.at += 1;
                let body = unsafe { self.parse_block() };
                let mut kids = Kids::new();
                unsafe {
                    kids.add(body, self.arena);
                }
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::BLOCK, line, b"unsafe\0".as_ptr(), 6) };
                unsafe {
                    self.set_kids(n, &kids);
                }
                return n;
            }
            if unsafe { self.is_kw(self.at, b"return\0".as_ptr()) } {
                self.at += 1;
                let mut kids = Kids::new();
                let mut has_val = false;
                let nk = unsafe { self.kind(self.at) };
                if !(nk == pm_jit_rsx_tok_kind::PUNCT
                    && (unsafe { self.is_punct(self.at, b';') }
                        || unsafe { self.is_punct(self.at, b'}') }
                        || unsafe { self.is_punct(self.at, b',') }))
                    && nk != pm_jit_rsx_tok_kind::END
                {
                    let v = unsafe { self.parse_expr() };
                    unsafe {
                        kids.add(v, self.arena);
                    }
                    has_val = true;
                }
                let _ = has_val;
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::RETURN, line, b"return\0".as_ptr(), 6) };
                unsafe {
                    self.set_kids(n, &kids);
                }
                return n;
            }
            if unsafe { self.is_kw(self.at, b"break\0".as_ptr()) } {
                self.at += 1;
                /* `break 'name` — the label rides in the node text so the
                 * lowering can emit the goto form; plain keeps "break". */
                if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::LIFETIME {
                    let lt = unsafe { self.text(self.at) };
                    let ll = unsafe { self.text_len(self.at) };
                    self.at += 1;
                    let n = unsafe { self.mk(pm_jit_rsx_ast_kind::BREAK, line, lt, ll) };
                    return n;
                }
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::BREAK, line, b"break\0".as_ptr(), 5) };
                return n;
            }
            if unsafe { self.is_kw(self.at, b"continue\0".as_ptr()) } {
                self.at += 1;
                /* `continue 'name` — same label-in-text scheme as break. */
                if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::LIFETIME {
                    let lt = unsafe { self.text(self.at) };
                    let ll = unsafe { self.text_len(self.at) };
                    self.at += 1;
                    let n = unsafe { self.mk(pm_jit_rsx_ast_kind::CONTINUE, line, lt, ll) };
                    return n;
                }
                let n = unsafe {
                    self.mk(pm_jit_rsx_ast_kind::CONTINUE, line, b"continue\0".as_ptr(), 8)
                };
                return n;
            }
            /* Path or struct literal: `a::b::c` or `S { f: v }`. */
            return unsafe { self.parse_path_expr() };
        }
        unsafe {
            self.err(b"expected expression\0".as_ptr());
        }
        core::ptr::null_mut()
    }

    /* Loop with a label (lname non-NULL, includes the leading `'`) or a
     * plain loop (lname NULL) — one body for all three keyword forms; the
     * label rides in the node text so the lowering can emit goto targets. */
    unsafe fn parse_labeled_or_plain_loop(
        &mut self,
        lname: *const u8,
        llen: usize,
    ) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        let mut kids = Kids::new();
        if unsafe { self.is_kw(self.at, b"loop\0".as_ptr()) } {
            self.at += 1;
            let body = unsafe { self.parse_block() };
            unsafe {
                kids.add(body, self.arena);
            }
            let n = unsafe { self.mk(pm_jit_rsx_ast_kind::LOOP, line, b"loop\0".as_ptr(), 4) };
            unsafe {
                self.set_kids(n, &kids);
            }
            return unsafe { self.relabel(n, lname, llen) };
        }
        if unsafe { self.is_kw(self.at, b"while\0".as_ptr()) } {
            self.at += 1;
            let save = self.cond_ctx;
            self.cond_ctx = true;
            let cond = unsafe { self.parse_expr() };
            self.cond_ctx = save;
            let body = unsafe { self.parse_block() };
            unsafe {
                kids.add(cond, self.arena);
                kids.add(body, self.arena);
            }
            let n = unsafe { self.mk(pm_jit_rsx_ast_kind::WHILE, line, b"while\0".as_ptr(), 5) };
            unsafe {
                self.set_kids(n, &kids);
            }
            return unsafe { self.relabel(n, lname, llen) };
        }
        if unsafe { self.is_kw(self.at, b"for\0".as_ptr()) } {
            self.at += 1;
            let pat = unsafe { self.parse_pattern() };
            if !unsafe { self.is_kw(self.at, b"in\0".as_ptr()) } {
                unsafe {
                    self.err(b"expected 'in' in for loop\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            self.at += 1;
            let save = self.cond_ctx;
            self.cond_ctx = true;
            let iter = unsafe { self.parse_expr() };
            self.cond_ctx = save;
            let body = unsafe { self.parse_block() };
            unsafe {
                kids.add(pat, self.arena);
                kids.add(iter, self.arena);
                kids.add(body, self.arena);
            }
            let n = unsafe { self.mk(pm_jit_rsx_ast_kind::FOR, line, b"for\0".as_ptr(), 3) };
            unsafe {
                self.set_kids(n, &kids);
            }
            return unsafe { self.relabel(n, lname, llen) };
        }
        unsafe {
            self.err(b"expected loop after label\0".as_ptr());
        }
        core::ptr::null_mut()
    }

    /* Overwrite a loop node's text with the label span (re-arena'd): the
     * plain keyword forms keep their "loop"/"while"/"for" text. */
    unsafe fn relabel(
        &mut self,
        n: *mut pm_jit_rsx_ast_t,
        lname: *const u8,
        llen: usize,
    ) -> *mut pm_jit_rsx_ast_t {
        if n.is_null() || lname.is_null() || llen == 0 {
            return n;
        }
        unsafe {
            (*n).text = self.nd.span(lname, llen, (*n).line);
            (*n).text_len = llen;
        }
        n
    }

    /* Closure: `|a, b| expr`, `|a| { .. }`, `|| expr` — and `move |..| ..`
     * when the caller already consumed the `move`. Params are plain idents
     * (no patterns/types — the lowering refuses closures anyway, so this is
     * parse-only structure for honest diagnostics + ast_dump). */
    unsafe fn parse_closure(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        let mut kids = Kids::new();
        /* opening bar (or `||` when the param list is empty — `||` may lex
         * as one OROR token or two PUNCT bars) */
        if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::OROR {
            self.at += 1;
        } else if unsafe { self.is_punct(self.at, b'|') }
            && unsafe { self.is_punct(self.at + 1, b'|') }
        {
            self.at += 2;
        } else if unsafe { self.is_punct(self.at, b'|') } {
            self.at += 1;
            loop {
                /* by-ref bind `|&b|` (optionally `|&'a b|`): the `&` rides as
                 * an UNARY child on the PARAM so the closure builtins can
                 * bind a pointer instead of a copy. */
                let mut by_ref = false;
                if unsafe { self.is_punct(self.at, b'&') } {
                    by_ref = true;
                    self.at += 1;
                    if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::LIFETIME {
                        self.at += 1;
                    }
                }
                /* tuple-pattern bind `|(a, b)|` — one PARAM node
                 * (text "tup") whose kids are the element PARAMs, so
                 * closure builtins destructure element binds the same
                 * way let-tuples do. */
                if unsafe { self.is_punct(self.at, b'(') } {
                    self.at += 1;
                    let mut tk = Kids::new();
                    loop {
                        if !self.ok {
                            return core::ptr::null_mut();
                        }
                        if unsafe { self.is_punct(self.at, b'&') } {
                            self.at += 1;
                            if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::LIFETIME {
                                self.at += 1;
                            }
                        }
                        if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::IDENT {
                            unsafe {
                                self.err(b"expected closure tuple parameter\0".as_ptr());
                            }
                            return core::ptr::null_mut();
                        }
                        let tp = unsafe {
                            self.mk(
                                pm_jit_rsx_ast_kind::PARAM,
                                line,
                                self.text(self.at),
                                self.text_len(self.at),
                            )
                        };
                        self.at += 1;
                        unsafe {
                            tk.add(tp, self.arena);
                        }
                        if unsafe { self.is_punct(self.at, b',') } {
                            self.at += 1;
                            if unsafe { self.is_punct(self.at, b')') } {
                                self.at += 1;
                                break;
                            }
                            continue;
                        }
                        if unsafe { self.is_punct(self.at, b')') } {
                            self.at += 1;
                            break;
                        }
                        unsafe {
                            self.err(b"expected ',' or ')' in closure tuple parameter\0".as_ptr());
                        }
                        return core::ptr::null_mut();
                    }
                    let tup = unsafe {
                        self.mk(pm_jit_rsx_ast_kind::PARAM, line, b"tup\0".as_ptr(), 3)
                    };
                    unsafe {
                        self.set_kids(tup, &tk);
                        kids.add(tup, self.arena);
                    }
                    if unsafe { self.is_punct(self.at, b',') } {
                        self.at += 1;
                        continue;
                    }
                    if !unsafe { self.is_punct(self.at, b'|') } {
                        unsafe {
                            self.err(b"expected '|' after closure parameters\0".as_ptr());
                        }
                        return core::ptr::null_mut();
                    }
                    self.at += 1;
                    break;
                }
                if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::IDENT {
                    unsafe {
                        self.err(b"expected closure parameter\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
                let p = unsafe {
                    self.mk(
                        pm_jit_rsx_ast_kind::PARAM,
                        line,
                        self.text(self.at),
                        self.text_len(self.at),
                    )
                };
                self.at += 1;
                /* optional `: TYPE` ascription — accepted and skipped:
                 * the C closure body infers param types from the call
                 * site, the same way unannotated params do. */
                if unsafe { self.is_punct(self.at, b':') } {
                    self.at += 1;
                    let ty = unsafe { self.parse_type() };
                    if !self.ok || ty.is_null() {
                        return core::ptr::null_mut();
                    }
                    unsafe {
                        kids.add(ty, self.arena);
                    }
                }
                if by_ref {
                    let amp = unsafe { self.mk(pm_jit_rsx_ast_kind::UNARY, line, b"&\0".as_ptr(), 1) };
                    let mut pk = Kids::new();
                    unsafe {
                        pk.add(amp, self.arena);
                    }
                    unsafe {
                        self.set_kids(p, &pk);
                    }
                }
                unsafe {
                    kids.add(p, self.arena);
                }
                if unsafe { self.is_punct(self.at, b',') } {
                    self.at += 1;
                    continue;
                }
                if !unsafe { self.is_punct(self.at, b'|') } {
                    unsafe {
                        self.err(b"expected '|' after closure parameters\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
                self.at += 1;
                break;
            }
        }
        let body = unsafe { self.parse_expr() };
        unsafe {
            kids.add(body, self.arena);
        }
        let n = unsafe { self.mk(pm_jit_rsx_ast_kind::CLOSURE, line, b"|..|\0".as_ptr(), 4) };
        unsafe {
            self.set_kids(n, &kids);
        }
        n
    }

    /* Path expr with call / struct-literal continuation. */
    unsafe fn parse_path_expr(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        let mut kids = Kids::new();
        /* `::`-prefixed paths (crate::…) — segments collected into kids. */
        if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::DOUBLE_COLON {
            self.at += 1;
        }
        loop {
            if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::IDENT {
                unsafe {
                    self.err(b"expected path segment\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            let seg = unsafe {
                self.mk(
                    pm_jit_rsx_ast_kind::PATH,
                    line,
                    self.text(self.at),
                    self.text_len(self.at),
                )
            };
            unsafe {
                kids.add(seg, self.arena);
            }
            self.at += 1;
            if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::DOUBLE_COLON {
                /* `::<T>` turbofish ends the path; a next segment continues it. */
                if unsafe { self.kind(self.at + 1) } == pm_jit_rsx_tok_kind::IDENT {
                    self.at += 1;
                    continue;
                }
                /* `path::mac!(..)` — the invocation names its path; keep the
                 * MACRO_INVOC token so the lower refuses expr macros BY
                 * NAME (never a stranded `::`). */
                if unsafe { self.kind(self.at + 1) } == pm_jit_rsx_tok_kind::MACRO_INVOC {
                    self.at += 1;
                    let m = unsafe {
                        self.mk(
                            pm_jit_rsx_ast_kind::MACRO,
                            line,
                            self.text(self.at),
                            self.text_len(self.at),
                        )
                    };
                    self.at += 1;
                    return m;
                }
                break;
            }
            break;
        }
        /* Generic args on a path expr: `name::<T>(…)`. Only the `::<` form —
         * a bare `<` after an ident is a comparison (`while i < n`), never
         * generics in expr position. */
        if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::DOUBLE_COLON
            && unsafe { self.kind(self.at + 1) } == pm_jit_rsx_tok_kind::PUNCT
            && unsafe { self.is_punct(self.at + 1, b'<') }
        {
            /* Single-segment generic arg (`size_of::<T>()`) or a pointer
             * spelling (`size_of::<*mut T>()`): kept as a TYPE child on the
             * path (pointer args carry a rendered `T *` spelling in text) so
             * lowering can emit `sizeof(T)`. Anything else is skipped. */
            if unsafe { self.kind(self.at + 2) } == pm_jit_rsx_tok_kind::IDENT
                && unsafe { self.kind(self.at + 3) } == pm_jit_rsx_tok_kind::PUNCT
                && unsafe { self.is_punct(self.at + 3, b'>') }
            {
                let gt = unsafe {
                    self.mk(
                        pm_jit_rsx_ast_kind::TYPE,
                        line,
                        self.text(self.at + 2),
                        self.text_len(self.at + 2),
                    )
                };
                unsafe {
                    kids.add(gt, self.arena);
                }
                self.at += 4;
            } else if unsafe { self.kind(self.at + 2) } == pm_jit_rsx_tok_kind::PUNCT
                && unsafe { self.is_punct(self.at + 2, b'*') }
                && (unsafe { self.is_kw(self.at + 3, b"mut\0".as_ptr()) }
                    || unsafe { self.is_kw(self.at + 3, b"const\0".as_ptr()) })
                && unsafe { self.kind(self.at + 4) } == pm_jit_rsx_tok_kind::IDENT
                && unsafe { self.kind(self.at + 5) } == pm_jit_rsx_tok_kind::PUNCT
                && unsafe { self.is_punct(self.at + 5, b'>') }
            {
                /* render `<ident> *` from the pieces (arena-owned copy) */
                let ty_len = self.text_len(self.at + 4);
                let gp = unsafe { pm_util_mem_alloc(self.arena, ty_len + 3) };
                if !gp.is_null() {
                    unsafe {
                        core::ptr::copy_nonoverlapping(self.text(self.at + 4), gp, ty_len);
                        *gp.add(ty_len) = b' ';
                        *gp.add(ty_len + 1) = b'*';
                        *gp.add(ty_len + 2) = 0;
                    }
                    let gt = unsafe {
                        self.mk(pm_jit_rsx_ast_kind::TYPE, line, gp, ty_len + 2)
                    };
                    unsafe {
                        kids.add(gt, self.arena);
                    }
                }
                self.at += 6;
            } else {
                self.at += 2;
                let mut depth = 1i32;
                while self.at < self.n_toks && depth > 0 {
                    let k = unsafe { self.kind(self.at) };
                    /* `>>`/`<<` lex as single tokens but open or close
                     * two generic lists each. */
                    if k == pm_jit_rsx_tok_kind::SHL {
                        depth += 2;
                        self.at += 1;
                        continue;
                    }
                    if k == pm_jit_rsx_tok_kind::SHR {
                        depth -= 2;
                        if depth <= 0 {
                            /* leave `at` ON the SHR: the shared tail
                             * below eats it as the final closing token. */
                            depth = 0;
                            break;
                        }
                        self.at += 1;
                        continue;
                    }
                    if k == pm_jit_rsx_tok_kind::PUNCT {
                        if unsafe { self.is_punct(self.at, b'<') } {
                            depth += 1;
                        } else if unsafe { self.is_punct(self.at, b'>') } {
                            depth -= 1;
                            if depth == 0 {
                                break;
                            }
                        }
                    }
                    self.at += 1;
                }
                if depth != 0 {
                    unsafe {
                        self.err(b"unterminated generic arguments\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
                self.at += 1;
            }
        }
        /* `path::macro!(..)` — the path names the macro; the MACRO_INVOC
         * token carries the whole invocation text. Consume it so the lower
         * can refuse expression macros BY NAME (a bare path parse would
         * strand the `!` and misreport "expected ';' after let"). */
        if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::MACRO_INVOC {
            let m = unsafe {
                self.mk(
                    pm_jit_rsx_ast_kind::MACRO,
                    line,
                    self.text(self.at),
                    self.text_len(self.at),
                )
            };
            self.at += 1;
            return m;
        }
        /* Struct literal `S { f: v, .. }` — only when `{` follows the path
         * outside a condition (there `cond {` is the loop/if body). */
        if unsafe { self.is_punct(self.at, b'{') } && !self.cond_ctx {
            self.at += 1;
            let mut fields = Kids::new();
            loop {
                if unsafe { self.is_punct(self.at, b'}') } {
                    self.at += 1;
                    break;
                }
                if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::END {
                    unsafe {
                        self.err(b"expected '}' in struct literal before end of file\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
                if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::RANGE {
                    unsafe {
                        self.err(b"unsupported: struct update syntax\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
                if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::IDENT {
                    unsafe {
                        self.err(b"expected field name in struct literal\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
                let fl = unsafe { self.line(self.at) };
                let fname = unsafe { self.text(self.at) };
                let fname_len = unsafe { self.text_len(self.at) };
                self.at += 1;
                if !unsafe { self.is_punct(self.at, b':') } {
                    /* shorthand `S { a }` == `S { a: a }` — build the missing
                     * value expr from the field name itself. */
                    let v = unsafe {
                        self.mk(pm_jit_rsx_ast_kind::PATH, fl, fname, fname_len)
                    };
                    let fv = unsafe { self.mk(pm_jit_rsx_ast_kind::STRUCT_FIELD, fl, fname, fname_len) };
                    unsafe {
                        fields.add(fv, self.arena);
                        fields.add(v, self.arena);
                    }
                    if unsafe { self.is_punct(self.at, b',') } {
                        self.at += 1;
                        continue;
                    }
                    if !unsafe { self.is_punct(self.at, b'}') } {
                        unsafe {
                            self.err(b"expected ',' or '}' in struct literal\0".as_ptr());
                        }
                        return core::ptr::null_mut();
                    }
                    continue;
                }
                self.at += 1;
                let v = unsafe { self.parse_expr() };
                let fv = unsafe { self.mk(pm_jit_rsx_ast_kind::STRUCT_FIELD, fl, fname, fname_len) };
                let mut pk = Kids::new();
                unsafe {
                    pk.add(fv, self.arena);
                    pk.add(v, self.arena);
                }
                let fn_node = unsafe {
                    self.mk(pm_jit_rsx_ast_kind::STRUCT_LIT, fl, fname, fname_len)
                };
                unsafe {
                    self.set_kids(fn_node, &pk);
                }
                let _ = fn_node;
                unsafe {
                    fields.add(fv, self.arena);
                    fields.add(v, self.arena);
                }
                if unsafe { self.is_punct(self.at, b',') } {
                    self.at += 1;
                    continue;
                }
                if !unsafe { self.is_punct(self.at, b'}') } {
                    unsafe {
                        self.err(b"expected ',' or '}' in struct literal\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
            }
            let n = unsafe {
                self.mk(pm_jit_rsx_ast_kind::STRUCT_LIT, line, b"struct-lit\0".as_ptr(), 10)
            };
            unsafe {
                self.set_kids(n, &fields);
            }
            /* Prepend the path segments so lowering knows the type name. */
            unsafe {
                kids.add(n, self.arena);
            }
            let pn = unsafe { self.mk(pm_jit_rsx_ast_kind::PATH, line, b"path\0".as_ptr(), 4) };
            unsafe {
                self.set_kids(pn, &kids);
            }
            return pn;
        }
        let n = unsafe { self.mk(pm_jit_rsx_ast_kind::PATH, line, b"path\0".as_ptr(), 4) };
        unsafe {
            self.set_kids(n, &kids);
        }
        n
    }

    /* Parse one `if` chain segment: either `let PAT = EXPR` or a plain
     * expression, into the out-params (is_let, pat, expr). */
    unsafe fn parse_if_chain_seg(
        &mut self,
        is_let: bool,
        pat_out: *mut *mut pm_jit_rsx_ast_t,
        expr_out: *mut *mut pm_jit_rsx_ast_t,
    ) -> bool {
        if is_let {
            self.at += 1; /* the `let` keyword */
            let pat = unsafe { self.parse_pattern() };
            if !self.ok || pat.is_null() {
                return false;
            }
            if !unsafe { self.is_punct(self.at, b'=') } {
                unsafe {
                    self.err(b"expected '=' in if-let\0".as_ptr());
                }
                return false;
            }
            self.at += 1;
            let save = self.cond_ctx;
            self.cond_ctx = true;
            let e = unsafe { self.parse_expr() };
            self.cond_ctx = save;
            unsafe {
                *pat_out = pat;
                *expr_out = e;
            }
            true
        } else {
            let save = self.cond_ctx;
            self.cond_ctx = true;
            let e = unsafe { self.parse_expr() };
            self.cond_ctx = save;
            unsafe {
                *pat_out = core::ptr::null_mut();
                *expr_out = e;
            }
            !e.is_null()
        }
    }

    /* Fold the parsed if-chain right-to-left around then/else.
     * Parallel arrays hold the segments in source order (only n_seg
     * live): seg_let[i] (is this a let segment), seg_pat[i] (its
     * pattern), seg_expr[i] (its condition or scrutinee). The i-th
     * segment wraps the (i+1)-th; the last wraps `then`. A false
     * segment folds into an IF whose else is the continuation's failure
     * branch — the else subtree is shared: lowering is a read-only
     * printer and a plain block re-emits inside its own C scope. A let
     * segment folds into a MATCH (the `if let` desugar, one arm per
     * pattern + wildcard). */
    unsafe fn fold_if_chain(
        &mut self,
        seg_let: *const bool,
        seg_pat: *const *mut pm_jit_rsx_ast_t,
        seg_expr: *const *mut pm_jit_rsx_ast_t,
        n_seg: usize,
        i: usize,
        then_b: *mut pm_jit_rsx_ast_t,
        els: *mut pm_jit_rsx_ast_t,
        line: u32,
    ) -> *mut pm_jit_rsx_ast_t {
        if i == n_seg {
            return then_b;
        }
        let is_let = unsafe { *seg_let.add(i) };
        let pat = unsafe { *seg_pat.add(i) };
        let e = unsafe { *seg_expr.add(i) };
        if !is_let {
            let inner = unsafe {
                self.fold_if_chain(seg_let, seg_pat, seg_expr, n_seg, i + 1, then_b, els, line)
            };
            /* emit_if's then-slot holds a BLOCK: a bare continuation
             * (nested IF/MATCH) rides inside a one-statement synthetic
             * block — the same node the source `if e { if .. }` gives. */
            let then_slot = if unsafe { (*inner).kind } == pm_jit_rsx_ast_kind::BLOCK {
                inner
            } else {
                let wrap = unsafe {
                    self.mk(pm_jit_rsx_ast_kind::BLOCK, line, b"block\0".as_ptr(), 5)
                };
                let mut wk = Kids::new();
                unsafe {
                    wk.add(inner, self.arena);
                    self.set_kids(wrap, &wk);
                }
                wrap
            };
            let mut kids = Kids::new();
            unsafe {
                kids.add(e, self.arena);
                kids.add(then_slot, self.arena);
                kids.add(els, self.arena);
            }
            let n = unsafe { self.mk(pm_jit_rsx_ast_kind::IF, line, b"if\0".as_ptr(), 2) };
            unsafe {
                self.set_kids(n, &kids);
            }
            return n;
        }
        /* let segment: MATCH on the scrutinee, pattern arm -> continuation,
         * wildcard arm -> els. The continuation lands in an arm BODY slot —
         * a bare node there emits statement-form (value dropped); a BLOCK
         * body routes its value tail to emit_match_value with the temp. */
        let cont = unsafe {
            self.fold_if_chain(seg_let, seg_pat, seg_expr, n_seg, i + 1, then_b, els, line)
        };
        let inner = if unsafe { (*cont).kind } == pm_jit_rsx_ast_kind::BLOCK {
            cont
        } else {
            let wrap = unsafe {
                self.mk(pm_jit_rsx_ast_kind::BLOCK, line, b"block\0".as_ptr(), 5)
            };
            let mut wk = Kids::new();
            unsafe {
                wk.add(cont, self.arena);
                self.set_kids(wrap, &wk);
            }
            wrap
        };
        let a1 = unsafe { self.mk(pm_jit_rsx_ast_kind::MATCH_ARM, line, b"arm\0".as_ptr(), 3) };
        let mut k1 = Kids::new();
        unsafe {
            k1.add(pat, self.arena);
            k1.add(inner, self.arena);
            self.set_kids(a1, &k1);
        }
        let wc = unsafe { self.mk(pm_jit_rsx_ast_kind::PATH, line, b"_\0".as_ptr(), 1) };
        let a2 = unsafe { self.mk(pm_jit_rsx_ast_kind::MATCH_ARM, line, b"arm\0".as_ptr(), 3) };
        let mut k2 = Kids::new();
        unsafe {
            k2.add(wc, self.arena);
            k2.add(els, self.arena);
            self.set_kids(a2, &k2);
        }
        let n = unsafe { self.mk(pm_jit_rsx_ast_kind::MATCH, line, b"match\0".as_ptr(), 5) };
        let mut mk = Kids::new();
        unsafe {
            mk.add(e, self.arena);
            mk.add(a1, self.arena);
            mk.add(a2, self.arena);
            self.set_kids(n, &mk);
        }
        n
    }

    unsafe fn parse_if_expr(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        self.at += 1;
        /* `if` conditions are let-chains (Rust 2024): `if a && let Some(x)
         * = e && b { .. } else { .. }`. Each `&&`-joined segment is either
         * a `let PAT = EXPR` or a plain expression; `parse_and_expr`
         * leaves top-level `&&` for this parser when chain_ctx is set.
         * Single-segment conditions build exactly the nodes the pre-chain
         * parser built (`if let` desugar / plain IF); the chain fold only
         * nests when there is a chain. */
        let mut seg_let: [bool; 8] = [false; 8];
        let mut seg_pat: [*mut pm_jit_rsx_ast_t; 8] = [core::ptr::null_mut(); 8];
        let mut seg_expr: [*mut pm_jit_rsx_ast_t; 8] = [core::ptr::null_mut(); 8];
        let mut n_seg = 0usize;
        let save_chain = self.chain_ctx;
        self.chain_ctx = true;
        loop {
            let is_let = unsafe { self.is_kw(self.at, b"let\0".as_ptr()) };
            if n_seg == 8 {
                unsafe {
                    self.err(b"unsupported: if chain longer than 8 segments\0".as_ptr());
                }
                self.chain_ctx = save_chain;
                return core::ptr::null_mut();
            }
            let mut pat: *mut pm_jit_rsx_ast_t = core::ptr::null_mut();
            let mut e: *mut pm_jit_rsx_ast_t = core::ptr::null_mut();
            if !unsafe { self.parse_if_chain_seg(is_let, &mut pat, &mut e) } || !self.ok {
                self.chain_ctx = save_chain;
                return core::ptr::null_mut();
            }
            seg_let[n_seg] = is_let;
            seg_pat[n_seg] = pat;
            seg_expr[n_seg] = e;
            n_seg += 1;
            if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::ANDAND {
                break;
            }
            /* chain glue continues only into a `let` segment; `&& expr`
             * was already folded into the segment's expression. */
            if !unsafe { self.is_kw(self.at + 1, b"let\0".as_ptr()) } {
                break;
            }
            /* chain glue — consume `&&`, loop parses the next segment */
            self.at += 1;
        }
        self.chain_ctx = save_chain;
        let then_b = unsafe { self.parse_block() };
        if !self.ok {
            return core::ptr::null_mut();
        }
        /* else: block / else-if chain / none. A no-else `if` carries NO
         * else kid (the pre-chain AST shape) — the empty BLOCK placeholder
         * exists only for the chain fold, where every nested if needs an
         * else arm to lower honestly. `had_else` keeps the two paths
         * distinct. */
        let els: *mut pm_jit_rsx_ast_t;
        let had_else: bool;
        if unsafe { self.is_kw(self.at, b"else\0".as_ptr()) } {
            self.at += 1;
            had_else = true;
            if unsafe { self.is_kw(self.at, b"if\0".as_ptr()) } {
                els = unsafe { self.parse_if_expr() };
            } else {
                els = unsafe { self.parse_block() };
            }
            if !self.ok {
                return core::ptr::null_mut();
            }
        } else {
            had_else = false;
            els = unsafe {
                self.mk(pm_jit_rsx_ast_kind::BLOCK, line, b"block\0".as_ptr(), 5)
            };
        }
        /* single plain segment: the pre-chain IF node (cond, then, [else])
         * — byte-identical output to before. */
        if n_seg == 1 && !seg_let[0] {
            let mut kids = Kids::new();
            unsafe {
                kids.add(seg_expr[0], self.arena);
                kids.add(then_b, self.arena);
            }
            if had_else {
                unsafe {
                    kids.add(els, self.arena);
                }
            }
            let n = unsafe { self.mk(pm_jit_rsx_ast_kind::IF, line, b"if\0".as_ptr(), 2) };
            unsafe {
                self.set_kids(n, &kids);
            }
            return n;
        }
        unsafe {
            self.fold_if_chain(
                seg_let.as_ptr(),
                seg_pat.as_ptr(),
                seg_expr.as_ptr(),
                n_seg,
                0,
                then_b,
                els,
                line,
            )
        }
    }

    unsafe fn parse_match_expr(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        self.at += 1;
        let save = self.cond_ctx;
        self.cond_ctx = true;
        let scrut = unsafe { self.parse_expr() };
        self.cond_ctx = save;
        let mut kids = Kids::new();
        unsafe {
            kids.add(scrut, self.arena);
        }
        if !unsafe { self.is_punct(self.at, b'{') } {
            unsafe {
                self.err(b"expected '{' after match scrutinee\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        self.at += 1;
        loop {
            if unsafe { self.is_punct(self.at, b'}') } {
                self.at += 1;
                break;
            }
            if !self.ok {
                return core::ptr::null_mut();
            }
            if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::END {
                unsafe {
                    self.err(b"expected '}' in match before end of file\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            let before = self.at;
            let aline = unsafe { self.line(self.at) };
            let pat = unsafe { self.parse_pattern() };
            let mut ak = Kids::new();
            unsafe {
                ak.add(pat, self.arena);
            }
            /* `pat if guard => body` — the guard rides as a middle kid on
             * the arm; lowering ANDs it into the arm's `if (...)` test. */
            if unsafe { self.is_kw(self.at, b"if\0".as_ptr()) } {
                self.at += 1;
                let save = self.cond_ctx;
                self.cond_ctx = true;
                let guard = unsafe { self.parse_expr() };
                self.cond_ctx = save;
                if !self.ok || guard.is_null() {
                    return core::ptr::null_mut();
                }
                unsafe {
                    ak.add(guard, self.arena);
                }
            }
            if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::FAT_ARROW {
                unsafe {
                    self.err(b"expected '=>' in match arm\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            self.at += 1;
            let body = unsafe { self.parse_expr() };
            unsafe {
                ak.add(body, self.arena);
            }
            let arm = unsafe {
                self.mk(pm_jit_rsx_ast_kind::MATCH_ARM, aline, b"=>\0".as_ptr(), 2)
            };
            unsafe {
                self.set_kids(arm, &ak);
            }
            unsafe {
                kids.add(arm, self.arena);
            }
            if self.ok && self.at == before {
                unsafe {
                    self.err(b"internal: match arm consumed no tokens\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            if unsafe { self.is_punct(self.at, b',') } {
                self.at += 1;
                continue;
            }
        }
        let n = unsafe { self.mk(pm_jit_rsx_ast_kind::MATCH, line, b"match\0".as_ptr(), 5) };
        unsafe {
            self.set_kids(n, &kids);
        }
        n
    }

    /* ---- blocks and statements ---- */

    unsafe fn parse_block(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        if !unsafe { self.is_punct(self.at, b'{') } {
            unsafe {
                self.err(b"expected '{'\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        self.at += 1;
        let mut kids = Kids::new();
        loop {
            if unsafe { self.is_punct(self.at, b'}') } {
                self.at += 1;
                break;
            }
            if !self.ok {
                return core::ptr::null_mut();
            }
            if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::END {
                unsafe {
                    self.err(b"expected '}' before end of file\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            let before = self.at;
            let s = unsafe { self.parse_stmt() };
            /* cfg-stripped statement: parsed-and-skipped, never lowered. */
            if !s.is_null()
                && unsafe { (*s).kind } == pm_jit_rsx_ast_kind::STMT
                && unsafe { z_eq(unsafe { (*s).text }, unsafe { (*s).text_len }, b"cfgskip\0".as_ptr()) }
            {
                if self.ok && self.at == before {
                    unsafe {
                        self.err(b"internal: statement consumed no tokens\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
                continue;
            }
            unsafe {
                kids.add(s, self.arena);
            }
            if self.ok && self.at == before {
                unsafe {
                    self.err(b"internal: statement consumed no tokens\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
        }
        let n = unsafe { self.mk(pm_jit_rsx_ast_kind::BLOCK, line, b"block\0".as_ptr(), 5) };
        unsafe {
            self.set_kids(n, &kids);
        }
        n
    }

    unsafe fn parse_stmt(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        /* Statement attributes: `#[cfg(..)]` is EVALUATED against the
         * active features (a false cfg strips the statement the same way
         * items strip); every other statement attribute parses into a
         * scratch list and drops — the subset has no statement-attribute
         * lowering (the same tokens ride an item's ATTR nodes). */
        if unsafe { self.is_punct(self.at, b'#') } && unsafe { self.is_punct(self.at + 1, b'[') } {
            let mut scratch = Kids::new();
            unsafe { self.parse_outer_attrs(&mut scratch) };
            if !self.ok {
                return core::ptr::null_mut();
            }
            if unsafe { self.cfg_verdict(&scratch) } == 0 {
                /* strip the statement: `{..}` block or up to `;` */
                unsafe {
                    self.skip_item_tokens();
                }
                return unsafe {
                    self.mk(pm_jit_rsx_ast_kind::STMT, line, b"cfgskip\0".as_ptr(), 7)
                };
            }
        }
        if unsafe { self.is_kw(self.at, b"let\0".as_ptr()) } {
            return unsafe { self.parse_let() };
        }
        if unsafe { self.is_punct(self.at, b';') } {
            /* empty statement */
            self.at += 1;
            return unsafe { self.mk(pm_jit_rsx_ast_kind::STMT, line, b"empty\0".as_ptr(), 5) };
        }
        /* Nested items in bodies refuse (one flat namespace per card). */
        if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::IDENT {
            let is_item_kw = unsafe { self.is_kw(self.at, b"fn\0".as_ptr()) }
                || unsafe { self.is_kw(self.at, b"struct\0".as_ptr()) }
                || unsafe { self.is_kw(self.at, b"enum\0".as_ptr()) }
                || unsafe { self.is_kw(self.at, b"impl\0".as_ptr()) }
                || unsafe { self.is_kw(self.at, b"trait\0".as_ptr()) }
                || unsafe { self.is_kw(self.at, b"mod\0".as_ptr()) }
                || unsafe { self.is_kw(self.at, b"use\0".as_ptr()) }
                || unsafe { self.is_kw(self.at, b"static\0".as_ptr()) }
                || unsafe { self.is_kw(self.at, b"const\0".as_ptr()) }
                || unsafe { self.is_kw(self.at, b"type\0".as_ptr()) };
            if is_item_kw {
                unsafe {
                    self.err(b"unsupported: nested item in fn body\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
        }
        /* Expression or assignment statement. Control-flow statements (`if`,
         * `match`, `for`, `while`, `loop`, `'label: loop`) parse their own
         * complete form — they must NOT enter the expression grammar, or the
         * binary/assign levels glue the next statement onto them (`for .. {}
         * - 1` parses as BINARY minus). Value-position if/match still goes
         * through parse_expr via the let-initializer path. */
        if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::IDENT {
            let is_ctrl = unsafe { self.is_kw(self.at, b"if\0".as_ptr()) }
                || unsafe { self.is_kw(self.at, b"match\0".as_ptr()) }
                || unsafe { self.is_kw(self.at, b"for\0".as_ptr()) }
                || unsafe { self.is_kw(self.at, b"while\0".as_ptr()) }
                || unsafe { self.is_kw(self.at, b"loop\0".as_ptr()) }
                /* `unsafe { .. }` as a whole statement: Rust ends the
                 * statement at the block's `}` — no binary glue onto the
                 * next statement (`unsafe { .. } \n -1` is block, then -1) */
                || (unsafe { self.is_kw(self.at, b"unsafe\0".as_ptr()) }
                    && unsafe { self.kind(self.at + 1) } == pm_jit_rsx_tok_kind::PUNCT
                    && unsafe { self.is_punct(self.at + 1, b'{') });
            if is_ctrl {
                let e = unsafe { self.parse_primary_expr() };
                if !self.ok {
                    return e;
                }
                let had_semi = unsafe { self.is_punct(self.at, b';') };
                if had_semi {
                    self.at += 1;
                }
                if !had_semi {
                    return e;
                }
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::EXPR_STMT, line, b";\0".as_ptr(), 1) };
                let mut kids = Kids::new();
                unsafe {
                    kids.add(e, self.arena);
                }
                unsafe {
                    self.set_kids(n, &kids);
                }
                return n;
            }
        }
        /* bare `{ .. }` block statement — same statement-ends-at-`}` rule */
        if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::PUNCT
            && unsafe { self.is_punct(self.at, b'{') }
        {
            return unsafe { self.parse_block() };
        }
        /* Labeled loop statement: `'name: loop|while|for ...` */
        if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::LIFETIME
            && unsafe { self.is_punct(self.at + 1, b':') }
        {
            let lname = unsafe { self.text(self.at) };
            let llen = unsafe { self.text_len(self.at) };
            self.at += 2;
            let e = unsafe { self.parse_labeled_or_plain_loop(lname, llen) };
            if !self.ok {
                return e;
            }
            let had_semi = unsafe { self.is_punct(self.at, b';') };
            if had_semi {
                self.at += 1;
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::EXPR_STMT, line, b";\0".as_ptr(), 1) };
                let mut kids = Kids::new();
                unsafe {
                    kids.add(e, self.arena);
                }
                unsafe {
                    self.set_kids(n, &kids);
                }
                return n;
            }
            return e;
        }
        /* Expression or assignment statement. */
        let e = unsafe { self.parse_expr() };
        if !self.ok {
            return e;
        }
        if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::MACRO_INVOC {
            let m = unsafe {
                self.mk(
                    pm_jit_rsx_ast_kind::MACRO,
                    line,
                    self.text(self.at),
                    self.text_len(self.at),
                )
            };
            self.at += 1;
            if unsafe { self.is_punct(self.at, b';') } {
                self.at += 1;
            }
            return m;
        }
        let had_semi = unsafe { self.is_punct(self.at, b';') };
        if had_semi {
            self.at += 1;
        }
        if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::MACRO_INVOC {
            let m = unsafe {
                self.mk(
                    pm_jit_rsx_ast_kind::MACRO,
                    line,
                    self.text(self.at),
                    self.text_len(self.at),
                )
            };
            self.at += 1;
            return m;
        }
        /* Block-like tail expr (no semicolon) is the block value. */
        if !had_semi {
            return e;
        }
        let n = unsafe { self.mk(pm_jit_rsx_ast_kind::EXPR_STMT, line, b";\0".as_ptr(), 1) };
        let mut kids = Kids::new();
        unsafe {
            kids.add(e, self.arena);
        }
        unsafe {
            self.set_kids(n, &kids);
        }
        n
    }

    unsafe fn parse_let(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        self.at += 1;
        /* tuple pattern `let (a, b) = ..`: parse_pattern builds the TUPLE
         * node of sub-patterns; the lower destructures it element-wise
         * against the initializer's tuple type. */
        if unsafe { self.is_punct(self.at, b'(') } {
            let pat = unsafe { self.parse_pattern() };
            if !self.ok || pat.is_null() {
                return core::ptr::null_mut();
            }
            if unsafe { (*pat).kind } != pm_jit_rsx_ast_kind::TUPLE {
                unsafe {
                    self.err(b"unsupported: let pattern\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            /* `let (Some(a), Some(b)) = .. else { .. }`: a tuple pattern
             * whose every element is a Some-pattern (or `_`). The lower
             * tests each tuple field's Option in one `if` and runs the
             * diverging else-block when any is None — so let-else is
             * allowed here, same contract as the single-bind form. */
            let mut all_some = true;
            {
                let pk = unsafe { (*pat).kids };
                let pn = unsafe { (*pat).n_kids } as usize;
                let mut f = 0usize;
                while f < pn {
                    let sub = unsafe { *pk.add(f) };
                    let mut is_some = false;
                    if unsafe { (*sub).kind } == pm_jit_rsx_ast_kind::PATH
                        && unsafe { z_eq((*sub).text, (*sub).text_len, b"pat\0".as_ptr()) }
                        && unsafe { (*sub).n_kids } as usize >= 1
                    {
                        let seg0 = unsafe { *(*sub).kids.add(0) };
                        if unsafe { (*seg0).kind } == pm_jit_rsx_ast_kind::PATH
                            && unsafe { z_eq((*seg0).text, (*seg0).text_len, b"Some\0".as_ptr()) }
                        {
                            is_some = true;
                        }
                    }
                    if !is_some {
                        all_some = false;
                    }
                    f += 1;
                }
            }
            let mut kids = Kids::new();
            unsafe {
                kids.add(pat, self.arena);
            }
            return unsafe { self.parse_let_rest(line, kids, all_some) };
        }
        /* pattern: ident, mut ident, `_`, or `Some(bind)` (single-bind —
         * the lower lowers it to a ._has test + inner decl). */
        let mut pat_name: *const u8 = b"_\0".as_ptr();
        let mut pat_len: usize = 1;
        let mut mutable = false;
        if unsafe { self.is_kw(self.at, b"mut\0".as_ptr()) } {
            mutable = true;
            self.at += 1;
        }
        if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::IDENT {
            pat_name = unsafe { self.text(self.at) };
            pat_len = unsafe { self.text_len(self.at) };
            self.at += 1;
        } else if !(unsafe { self.is_punct(self.at, b'_') }) {
            unsafe {
                self.err(b"unsupported: let pattern\0".as_ptr());
            }
            return core::ptr::null_mut();
        } else {
            self.at += 1;
        }
        let mut kids = Kids::new();
        /* `Some(bind)` let-pattern: same node shape parse_pattern builds
         * (PATH "pat", kids = [Some seg, bind]) so the lower's match/let
         * Some-branch reads both forms identically. */
        let mut is_some_pat = false;
        if pat_len == 4
            && unsafe { z_eq(pat_name, 4, b"Some\0".as_ptr()) }
            && unsafe { self.is_punct(self.at, b'(') }
        {
            self.at += 1;
            let inner = unsafe { self.parse_pattern() };
            if !self.ok || inner.is_null() {
                return core::ptr::null_mut();
            }
            if !unsafe { self.is_punct(self.at, b')') } {
                unsafe {
                    self.err(b"expected ')' in Some(bind) let pattern\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            self.at += 1;
            let mut segs = Kids::new();
            let seg = unsafe { self.mk(pm_jit_rsx_ast_kind::PATH, line, b"Some\0".as_ptr(), 4) };
            unsafe {
                segs.add(seg, self.arena);
                segs.add(inner, self.arena);
            }
            let pat = unsafe { self.mk(pm_jit_rsx_ast_kind::PATH, line, b"pat\0".as_ptr(), 3) };
            unsafe {
                self.set_kids(pat, &segs);
                kids.add(pat, self.arena);
            }
            is_some_pat = true;
            let _ = is_some_pat;
        } else {
            let pat = unsafe { self.mk(pm_jit_rsx_ast_kind::PATH, line, pat_name, pat_len) };
            unsafe {
                kids.add(pat, self.arena);
            }
        }
        if mutable {
            let m = unsafe { self.mk(pm_jit_rsx_ast_kind::ATTR, line, b"mut\0".as_ptr(), 3) };
            unsafe {
                kids.add(m, self.arena);
            }
        }
        unsafe { self.parse_let_rest(line, kids, is_some_pat) }
    }

    /* Shared let tail after the pattern kids are built: optional `: T`,
     * `= init`, let-else (Some-patterns only), `;`. The LET node's text tag
     * is "let", or "letelse" when an else-block is present. */
    unsafe fn parse_let_rest(&mut self, line: u32, mut kids: Kids, is_some_pat: bool) -> *mut pm_jit_rsx_ast_t {
        let mut ty: *mut pm_jit_rsx_ast_t = core::ptr::null_mut();
        if unsafe { self.is_punct(self.at, b':') } {
            self.at += 1;
            ty = unsafe { self.parse_type() };
            unsafe {
                kids.add(ty, self.arena);
            }
        }
        let mut init: *mut pm_jit_rsx_ast_t = core::ptr::null_mut();
        if unsafe { self.is_punct(self.at, b'=') } {
            self.at += 1;
            init = unsafe { self.parse_expr() };
            unsafe {
                kids.add(init, self.arena);
            }
        }
        /* let-else: the else-block must diverge (its last statement is a
         * return/break/continue or a block ending in one) — a falling-off
         * else would leave the bind live with garbage, which is unsound to
         * lower flat. Checked at parse so the refusal names the construct.
         * The LET node's text tag becomes "letelse" for the lower. */
        let mut els: *mut pm_jit_rsx_ast_t = core::ptr::null_mut();
        if unsafe { self.is_kw(self.at, b"else\0".as_ptr()) } {
            if !is_some_pat {
                unsafe {
                    self.err(b"unsupported: let-else on a non-Some pattern\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            self.at += 1;
            els = unsafe { self.parse_block() };
            if !self.ok {
                return core::ptr::null_mut();
            }
            if !unsafe { block_diverges(els) } {
                unsafe {
                    self.err(b"unsupported: let-else block must return/break/continue\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            unsafe {
                kids.add(els, self.arena);
            }
        }
        if els.is_null() {
            if !unsafe { self.is_punct(self.at, b';') } {
                unsafe {
                    self.err(b"expected ';' after let\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            self.at += 1;
            let n = unsafe { self.mk(pm_jit_rsx_ast_kind::LET, line, b"let\0".as_ptr(), 3) };
            unsafe {
                self.set_kids(n, &kids);
            }
            return n;
        }
        let n = unsafe { self.mk(pm_jit_rsx_ast_kind::LET, line, b"letelse\0".as_ptr(), 7) };
        unsafe {
            self.set_kids(n, &kids);
        }
        n
    }

    /* ---- items ---- */

    /* Visibility: `pub`, `pub(crate)`, `pub(super)` — recorded as one flag. */
    unsafe fn parse_vis(&mut self, kids: &mut Kids) {
        if !unsafe { self.is_kw(self.at, b"pub\0".as_ptr()) } {
            return;
        }
        let line = unsafe { self.line(self.at) };
        let v = unsafe { self.mk(pm_jit_rsx_ast_kind::ATTR, line, b"pub\0".as_ptr(), 3) };
        unsafe {
            kids.add(v, self.arena);
        }
        self.at += 1;
        if unsafe { self.is_punct(self.at, b'(') } {
            self.at += 1;
            while self.at < self.n_toks
                && !(unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::PUNCT
                    && unsafe { self.is_punct(self.at, b')') })
            {
                self.at += 1;
            }
            if unsafe { self.is_punct(self.at, b')') } {
                self.at += 1;
            }
        }
    }

    /* Outer attrs + visibility, into `kids` (ATTR nodes first). */
    unsafe fn parse_attrs_and_vis(&mut self, kids: &mut Kids) {
        loop {
            if unsafe { self.is_punct(self.at, b'#') } {
                let before = kids.n;
                unsafe {
                    self.parse_outer_attrs(kids);
                }
                if kids.n == before {
                    return;
                }
                continue;
            }
            if unsafe { self.is_kw(self.at, b"pub\0".as_ptr()) } {
                unsafe {
                    self.parse_vis(kids);
                }
                continue;
            }
            return;
        }
    }

    /* Evaluate `#[cfg(..)]` ATTR kids against self.feats. Returns:
     * 1 = at least one cfg and all cfgs true (keep, attrs already fine),
     * 0 = a cfg is false (strip the item),
     * -1 = no cfg attr present. */
    unsafe fn cfg_verdict(&self, kids: &Kids) -> i32 {
        let mut verdict: i32 = -1;
        let mut i = 0usize;
        while i < kids.n {
            let k: *mut pm_jit_rsx_ast_t = if i < KIDS_INLINE {
                unsafe { *kids.fixed.as_ptr().add(i) }
            } else {
                unsafe { *kids.spill.add(i - KIDS_INLINE) }
            };
            if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::ATTR {
                let t = unsafe { (*k).text };
                let tl = unsafe { (*k).text_len };
                /* ATTR text shape: `# [ cfg ( feature = "gen" ) ]` (tokens
                 * joined with single spaces). Compact it (drop spaces)
                 * into an arena buffer so cfg_eval sees `#[cfg(...)`
                 * forms without whitespace. */
                if tl >= 3 && unsafe { *t == b'#' } {
                    let buf = unsafe { pm_util_mem_alloc(self.arena, tl + 1) };
                    if buf.is_null() {
                        return -1;
                    }
                    let mut w = 0usize;
                    let mut j = 0usize;
                    while j < tl {
                        let c = unsafe { *t.add(j) };
                        if c != b' ' {
                            unsafe {
                                *buf.add(w) = c;
                            }
                            w += 1;
                        }
                        j += 1;
                    }
                    unsafe {
                        *buf.add(w) = 0;
                    }
                    /* find `cfg(` */
                    if w >= 6 {
                        let mut p = 1usize;
                        let mut found = false;
                        while p + 4 <= w {
                            if unsafe { z_eq(buf.add(p), 3, b"cfg\0".as_ptr()) }
                                && unsafe { *buf.add(p + 3) } == b'('
                            {
                                found = true;
                                break;
                            }
                            p += 1;
                        }
                        if found {
                            /* inner = buf[p+4 .. close) where close is the
                             * matching ')' — the attr's `)` (tokens end
                             * with `) ]` so w-2 is the close in the plain
                             * case; walk a paren count to be exact). */
                            let inner = unsafe { buf.add(p + 4) };
                            let mut depth = 1i32;
                            let mut q = p + 4usize;
                            let mut close = w;
                            while q < w {
                                if unsafe { *buf.add(q) } == b'(' {
                                    depth += 1;
                                } else if unsafe { *buf.add(q) } == b')' {
                                    depth -= 1;
                                    if depth == 0 {
                                        close = q;
                                        break;
                                    }
                                }
                                q += 1;
                            }
                            let inner_len = close - (p + 4);
                            let v = unsafe { cfg_eval(inner, inner_len, self.feats) };
                            if verdict == -1 || verdict == 1 {
                                verdict = if v > 0 { 1 } else { 0 };
                            }
                        }
                    }
                }
            }
            i += 1;
        }
        verdict
    }

    /* Skip one item's tokens: a `{ .. }` block (bracket-balanced) or
     * anything up to and including a `;` at depth 0. Used by cfg-false
     * items. A depth-0 `}` that closes the item body ends the item —
     * but a `use a::{b, c};` tree also closes to depth 0 and its `;`
     * still belongs to the item: when the next token is `;`, consume it
     * too (a fn/impl body is never followed by `;`). Parens/brackets
     * count toward depth so a `->` or param list cannot fake a stop. */
    unsafe fn skip_item_tokens(&mut self) {
        /* All three bracket kinds balance; only `}` (the item's body)
         * or a depth-0 `;` terminates. `(`/`)` and `[`/`]` balance
         * without terminating — a `fn name(params) -> T { .. }` skip
         * must not stop at the param list's `)`. */
        let mut depth = 0i32;
        while self.at < self.n_toks {
            let k = unsafe { self.kind(self.at) };
            if k == pm_jit_rsx_tok_kind::PUNCT {
                if unsafe { self.is_punct(self.at, b'{') }
                    || unsafe { self.is_punct(self.at, b'(') }
                    || unsafe { self.is_punct(self.at, b'[') }
                {
                    depth += 1;
                } else if unsafe { self.is_punct(self.at, b')') }
                    || unsafe { self.is_punct(self.at, b']') }
                {
                    if depth > 0 {
                        depth -= 1;
                    }
                } else if unsafe { self.is_punct(self.at, b'}') } {
                    depth -= 1;
                    self.at += 1;
                    if depth <= 0 {
                        if unsafe { self.is_punct(self.at, b';') } {
                            self.at += 1;
                        }
                        return;
                    }
                    continue;
                } else if depth == 0 && unsafe { self.is_punct(self.at, b';') } {
                    self.at += 1;
                    return;
                }
            }
            self.at += 1;
        }
    }

    unsafe fn parse_use(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        self.at += 1;
        let start = self.at;
        while self.at < self.n_toks {
            if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::PUNCT
                && unsafe { self.is_punct(self.at, b';') }
            {
                break;
            }
            self.at += 1;
        }
        /* Join token texts into one span for the USE node. */
        let mut buf = Out::new(self.arena);
        let mut i = start;
        while i < self.at {
            if i > start {
                unsafe {
                    buf.putc(b' ');
                }
            }
            unsafe {
                buf.put(self.text(i), self.text_len(i));
            }
            i += 1;
        }
        if !buf.ok {
            unsafe {
                self.nd.oom(line);
            }
            self.ok = false;
            return core::ptr::null_mut();
        }
        self.at += 1;
        unsafe { self.mk(pm_jit_rsx_ast_kind::USE, line, buf.p, buf.len) }
    }

    /* Module item: `mod name;` (path form, `#[path]`-attributed test mods
     * included) or `mod name { items }`. The C translation is flat — a
     * module is compile-time path structure, so both forms lower to a
     * comment naming the module; the inline body's items would need the
     * same pass batching as file level, which no in-tree card uses. */
    unsafe fn parse_mod(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        self.at += 1;
        if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::IDENT {
            unsafe {
                self.err(b"expected module name\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        let name = unsafe { self.text(self.at) };
        let name_len = unsafe { self.text_len(self.at) };
        let n = unsafe { self.mk(pm_jit_rsx_ast_kind::MODULE, line, name, name_len) };
        self.at += 1;
        if unsafe { self.is_punct(self.at, b';') } {
            self.at += 1;
            return n;
        }
        /* `mod name { .. }`: skip the balanced braces. The skipped items are
         * not lowered — a refusal here would be about code no in-tree card
         * writes, so the honest shape is a plain comment. */
        if !unsafe { self.is_punct(self.at, b'{') } {
            unsafe {
                self.err(b"expected ';' or '{' after mod name\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        let mut depth = 0usize;
        while self.at < self.n_toks {
            if unsafe { self.is_punct(self.at, b'{') } {
                depth += 1;
            } else if unsafe { self.is_punct(self.at, b'}') } {
                depth -= 1;
                if depth == 0 {
                    self.at += 1;
                    return n;
                }
            }
            self.at += 1;
        }
        unsafe {
            self.err(b"unterminated module body\0".as_ptr());
        }
        core::ptr::null_mut()
    }

    /* Consumes a balanced `<..>` generic list starting AT the `<`, counting
     * top-level parameters (comma-separated at depth 1). Returns the count
     * and leaves `self.at` on the token after `>`. Bounds (`T: Send`) are
     * tokens like any other; a `<`/`>` imbalance is a parse error by the
     * caller (returns 0 without consuming). */
    unsafe fn count_generic_params(&mut self) -> usize {
        if !unsafe { self.is_punct(self.at, b'<') } {
            return 0;
        }
        self.at += 1;
        let mut depth = 1usize;
        let mut nparams = 0usize;
        let mut at_top = true;
        while self.at < self.n_toks && depth > 0 {
            if unsafe { self.is_punct(self.at, b'<') } {
                depth += 1;
            } else if unsafe { self.is_punct(self.at, b'>') } {
                depth -= 1;
                if depth == 0 {
                    self.at += 1;
                    break;
                }
            } else if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::SHR {
                if depth >= 2 {
                    depth -= 2;
                    if depth == 0 {
                        self.at += 1;
                        break;
                    }
                } else {
                    depth -= 1;
                    self.at += 1;
                    break;
                }
            } else if depth == 1 && unsafe { self.is_punct(self.at, b',') } {
                at_top = true;
                self.at += 1;
                continue;
            } else if depth == 1 && at_top && unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::IDENT {
                nparams += 1;
                at_top = false;
            }
            self.at += 1;
        }
        if depth != 0 {
            unsafe {
                self.err(b"expected '>' in generic list\0".as_ptr());
            }
            return 0;
        }
        nparams
    }

    /* Struct: named fields, tuple form, or unit form. */
    unsafe fn parse_struct(&mut self, kids: &mut Kids) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        self.at += 1;
        if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::IDENT {
            unsafe {
                self.err(b"expected struct name\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        let name = unsafe { self.text(self.at) };
        let name_len = unsafe { self.text_len(self.at) };
        self.at += 1;
        if unsafe { self.is_punct(self.at, b'<') } {
            /* Generic parameter list on a struct (`SpinLock<T>`,
             * `Mut<T>`): accepted when the struct is a single-field tuple
             * form — the transparent-newtype case the lower knows — and
             * refused otherwise (two fields would need real monomorphized
             * C layout, which the subset does not define). Consume the
             * balanced `<..>` as tokens; the param names are inert (the
             * lower resolves `T` from the *use* site's generic arg). */
            let nparams = unsafe { self.count_generic_params() };
            if nparams == 0 {
                return core::ptr::null_mut();
            }
            /* Marker ATTR so the lower knows this item was generic (the
             * param count is not needed later — the field count is). */
            let g = unsafe {
                self.mk(
                    pm_jit_rsx_ast_kind::ATTR,
                    line,
                    b"generic\0".as_ptr(),
                    7,
                )
            };
            unsafe {
                kids.add(g, self.arena);
            }
        }
        let mut body = Kids::new();
        if unsafe { self.is_punct(self.at, b'(') } {
            /* tuple struct */
            self.at += 1;
            loop {
                if unsafe { self.is_punct(self.at, b')') } {
                    self.at += 1;
                    break;
                }
                let fty = unsafe { self.parse_type() };
                let mut fk = Kids::new();
                unsafe {
                    fk.add(fty, self.arena);
                }
                let f = unsafe { self.mk(pm_jit_rsx_ast_kind::STRUCT_FIELD, line, b"tuple\0".as_ptr(), 5) };
                unsafe {
                    self.set_kids(f, &fk);
                }
                unsafe {
                    body.add(f, self.arena);
                }
                if unsafe { self.is_punct(self.at, b',') } {
                    self.at += 1;
                    continue;
                }
            }
        } else if unsafe { self.is_punct(self.at, b'{') } {
            self.at += 1;
            loop {
                if unsafe { self.is_punct(self.at, b'}') } {
                    self.at += 1;
                    break;
                }
                /* field visibility: `pub`, `pub(crate)`, `pub(super)` */
                if unsafe { self.is_kw(self.at, b"pub\0".as_ptr()) } {
                    self.at += 1;
                    if unsafe { self.is_punct(self.at, b'(') } {
                        let mut depth = 1usize;
                        self.at += 1;
                        while self.at < self.n_toks && depth > 0 {
                            if unsafe { self.is_punct(self.at, b'(') } {
                                depth += 1;
                            } else if unsafe { self.is_punct(self.at, b')') } {
                                depth -= 1;
                            }
                            self.at += 1;
                        }
                    }
                }
                if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::IDENT {
                    unsafe {
                        self.err(b"expected field name\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
                let fname = unsafe { self.text(self.at) };
                let fname_len = unsafe { self.text_len(self.at) };
                self.at += 1;
                if !unsafe { self.is_punct(self.at, b':') } {
                    unsafe {
                        self.err(b"expected ':' after field name\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
                self.at += 1;
                let fty = unsafe { self.parse_type() };
                let f = unsafe {
                    self.mk(pm_jit_rsx_ast_kind::STRUCT_FIELD, line, fname, fname_len)
                };
                let mut fk = Kids::new();
                unsafe {
                    fk.add(fty, self.arena);
                }
                unsafe {
                    self.set_kids(f, &fk);
                }
                unsafe {
                    body.add(f, self.arena);
                }
                if unsafe { self.is_punct(self.at, b',') } {
                    self.at += 1;
                    continue;
                }
            }
        } else {
            /* unit struct */
            let f = unsafe { self.mk(pm_jit_rsx_ast_kind::STRUCT_FIELD, line, b"unit\0".as_ptr(), 4) };
            unsafe {
                body.add(f, self.arena);
            }
        }
        if unsafe { self.is_punct(self.at, b';') } {
            self.at += 1;
        }
        let n = unsafe { self.mk(pm_jit_rsx_ast_kind::STRUCT, line, name, name_len) };
        /* attrs/vis first, then fields */
        let mut all = Kids::new();
        let mut i = 0usize;
        while i < kids.n {
            unsafe {
                all.add(*kids.fixed.as_ptr().add(i), self.arena);
            }
            i += 1;
        }
        let mut j = 0usize;
        while j < body.n {
            let one: *mut pm_jit_rsx_ast_t = if j < KIDS_INLINE {
                unsafe { *body.fixed.as_ptr().add(j) }
            } else {
                unsafe { *body.spill.add(j - KIDS_INLINE) }
            };
            unsafe {
                all.add(one, self.arena);
            }
            j += 1;
        }
        unsafe {
            self.set_kids(n, &all);
        }
        n
    }

    unsafe fn parse_enum(&mut self, kids: &mut Kids) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        self.at += 1;
        if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::IDENT {
            unsafe {
                self.err(b"expected enum name\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        let name = unsafe { self.text(self.at) };
        let name_len = unsafe { self.text_len(self.at) };
        self.at += 1;
        if unsafe { self.is_punct(self.at, b'<') } {
            unsafe {
                self.err(b"unsupported: generics on enum\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        let mut body = Kids::new();
        if unsafe { self.is_punct(self.at, b'{') } {
            self.at += 1;
            loop {
                if unsafe { self.is_punct(self.at, b'}') } {
                    self.at += 1;
                    break;
                }
                if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::IDENT {
                    unsafe {
                        self.err(b"expected enum variant name\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
                let vline = unsafe { self.line(self.at) };
                let vname = unsafe { self.text(self.at) };
                let vlen = unsafe { self.text_len(self.at) };
                self.at += 1;
                let v = unsafe {
                    self.mk(pm_jit_rsx_ast_kind::ENUM_VARIANT, vline, vname, vlen)
                };
                let mut vk = Kids::new();
                let mut data = false;
                if unsafe { self.is_punct(self.at, b'(') } {
                    data = true;
                    self.at += 1;
                    loop {
                        if unsafe { self.is_punct(self.at, b')') } {
                            self.at += 1;
                            break;
                        }
                        let fty = unsafe { self.parse_type() };
                        unsafe {
                            vk.add(fty, self.arena);
                        }
                        if unsafe { self.is_punct(self.at, b',') } {
                            self.at += 1;
                            continue;
                        }
                    }
                } else if unsafe { self.is_punct(self.at, b'{') } {
                    data = true;
                    self.at += 1;
                    loop {
                        if unsafe { self.is_punct(self.at, b'}') } {
                            self.at += 1;
                            break;
                        }
                        if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::IDENT {
                            unsafe {
                                self.err(b"expected variant field\0".as_ptr());
                            }
                            return core::ptr::null_mut();
                        }
                        self.at += 1;
                        if !unsafe { self.is_punct(self.at, b':') } {
                            unsafe {
                                self.err(b"expected ':'\0".as_ptr());
                            }
                            return core::ptr::null_mut();
                        }
                        self.at += 1;
                        let fty = unsafe { self.parse_type() };
                        unsafe {
                            vk.add(fty, self.arena);
                        }
                        if unsafe { self.is_punct(self.at, b',') } {
                            self.at += 1;
                            continue;
                        }
                    }
                }
                let _ = data;
                if unsafe { self.is_punct(self.at, b'=') } {
                    self.at += 1;
                    if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::INT_LITERAL {
                        unsafe {
                            self.err(b"expected discriminant literal\0".as_ptr());
                        }
                        return core::ptr::null_mut();
                    }
                    let d = unsafe {
                        self.mk(
                            pm_jit_rsx_ast_kind::LITERAL,
                            unsafe { self.line(self.at) },
                            self.text(self.at),
                            self.text_len(self.at),
                        )
                    };
                    unsafe {
                        vk.add(d, self.arena);
                    }
                    self.at += 1;
                }
                unsafe {
                    self.set_kids(v, &vk);
                }
                unsafe {
                    body.add(v, self.arena);
                }
                if unsafe { self.is_punct(self.at, b',') } {
                    self.at += 1;
                    continue;
                }
            }
        }
        if unsafe { self.is_punct(self.at, b';') } {
            self.at += 1;
        }
        let n = unsafe { self.mk(pm_jit_rsx_ast_kind::ENUM, line, name, name_len) };
        let mut all = Kids::new();
        let mut i = 0usize;
        while i < kids.n {
            unsafe {
                all.add(*kids.fixed.as_ptr().add(i), self.arena);
            }
            i += 1;
        }
        let mut j = 0usize;
        while j < body.n {
            let one: *mut pm_jit_rsx_ast_t = if j < KIDS_INLINE {
                unsafe { *body.fixed.as_ptr().add(j) }
            } else {
                unsafe { *body.spill.add(j - KIDS_INLINE) }
            };
            unsafe {
                all.add(one, self.arena);
            }
            j += 1;
        }
        unsafe {
            self.set_kids(n, &all);
        }
        n
    }

    /* extern block: `[unsafe] extern "C" { fn f(..); static S: T; }` */
    unsafe fn parse_extern_block(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        if unsafe { self.is_kw(self.at, b"unsafe\0".as_ptr()) } {
            self.at += 1;
        }
        self.at += 1;
        let mut kids = Kids::new();
        if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::STRING_LITERAL {
            let abi = unsafe {
                self.mk(
                    pm_jit_rsx_ast_kind::TYPE,
                    line,
                    self.text(self.at),
                    self.text_len(self.at),
                )
            };
            unsafe {
                kids.add(abi, self.arena);
            }
            self.at += 1;
        }
        if !unsafe { self.is_punct(self.at, b'{') } {
            unsafe {
                self.err(b"expected '{' in extern block\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        self.at += 1;
        loop {
            if unsafe { self.is_punct(self.at, b'}') } {
                self.at += 1;
                break;
            }
            let mut ik = Kids::new();
            unsafe {
                self.parse_attrs_and_vis(&mut ik);
            }
            if unsafe { self.is_kw(self.at, b"fn\0".as_ptr()) } {
                let f = unsafe { self.parse_fn_sig(&mut ik, 1) };
                unsafe {
                    kids.add(f, self.arena);
                }
                continue;
            }
            if unsafe { self.is_kw(self.at, b"static\0".as_ptr()) } {
                let s = unsafe { self.parse_static(&mut ik, 1) };
                unsafe {
                    kids.add(s, self.arena);
                }
                continue;
            }
            unsafe {
                self.err(b"unsupported: extern block item\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        let n = unsafe { self.mk(pm_jit_rsx_ast_kind::EXTERN_BLOCK, line, b"extern\0".as_ptr(), 6) };
        unsafe {
            self.set_kids(n, &kids);
        }
        n
    }

    /* `static`/`const` item (or extern block member when declare_only). */
    unsafe fn parse_static(&mut self, kids: &mut Kids, declare_only: usize) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        let kind = pm_jit_rsx_ast_kind::STATIC;
        let mut mutable = false;
        let mut is_const = false;
        if unsafe { self.is_kw(self.at, b"const\0".as_ptr()) } {
            is_const = true;
            self.at += 1;
            /* `const fn` is a function, not a const item. */
            if unsafe { self.is_kw(self.at, b"fn\0".as_ptr()) } {
                return unsafe { self.parse_fn(kids, 0) };
            }
        } else {
            self.at += 1;
            if unsafe { self.is_kw(self.at, b"mut\0".as_ptr()) } {
                mutable = true;
                self.at += 1;
            }
        }
        if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::IDENT {
            unsafe {
                self.err(b"expected static name\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        let name = unsafe { self.text(self.at) };
        let name_len = unsafe { self.text_len(self.at) };
        self.at += 1;
        let mut body = Kids::new();
        if !unsafe { self.is_punct(self.at, b':') } {
            unsafe {
                self.err(b"expected ':' after static name\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        self.at += 1;
        let ty = unsafe { self.parse_type() };
        unsafe {
            body.add(ty, self.arena);
        }
        if unsafe { self.is_punct(self.at, b'=') } {
            self.at += 1;
            let v = unsafe { self.parse_expr() };
            unsafe {
                body.add(v, self.arena);
            }
        }
        if !unsafe { self.is_punct(self.at, b';') } {
            unsafe {
                self.err(b"expected ';' after static\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        self.at += 1;
        let nkind = if is_const { pm_jit_rsx_ast_kind::CONST } else { kind };
        let n = unsafe { self.mk(nkind, line, name, name_len) };
        /* attrs/vis then type/init */
        let mut all = Kids::new();
        let mut i = 0usize;
        while i < kids.n {
            unsafe {
                all.add(*kids.fixed.as_ptr().add(i), self.arena);
            }
            i += 1;
        }
        let mut j = 0usize;
        while j < body.n {
            let one: *mut pm_jit_rsx_ast_t = if j < KIDS_INLINE {
                unsafe { *body.fixed.as_ptr().add(j) }
            } else {
                unsafe { *body.spill.add(j - KIDS_INLINE) }
            };
            unsafe {
                all.add(one, self.arena);
            }
            j += 1;
        }
        unsafe {
            self.set_kids(n, &all);
        }
        let _ = mutable;
        let _ = declare_only;
        n
    }

    unsafe fn parse_type_alias(&mut self, kids: &mut Kids) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        self.at += 1;
        if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::IDENT {
            unsafe {
                self.err(b"expected type alias name\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        let name = unsafe { self.text(self.at) };
        let name_len = unsafe { self.text_len(self.at) };
        self.at += 1;
        if !unsafe { self.is_punct(self.at, b'=') } {
            unsafe {
                self.err(b"expected '=' in type alias\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        self.at += 1;
        let ty = unsafe { self.parse_type() };
        if !unsafe { self.is_punct(self.at, b';') } {
            unsafe {
                self.err(b"expected ';' after type alias\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        self.at += 1;
        let n = unsafe { self.mk(pm_jit_rsx_ast_kind::TYPE_ALIAS, line, name, name_len) };
        let mut all = Kids::new();
        let mut i = 0usize;
        while i < kids.n {
            unsafe {
                all.add(*kids.fixed.as_ptr().add(i), self.arena);
            }
            i += 1;
        }
        unsafe {
            all.add(ty, self.arena);
        }
        unsafe {
            self.set_kids(n, &all);
        }
        n
    }

    /* Function signature (no body when declare_only != 0). */
    unsafe fn parse_fn_sig(&mut self, kids: &mut Kids, declare_only: usize) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        let mut quals = Kids::new();
        let mut is_unsafe = false;
        let mut is_extern = false;
        if unsafe { self.is_kw(self.at, b"const\0".as_ptr()) } {
            let q = unsafe { self.mk(pm_jit_rsx_ast_kind::ATTR, line, b"const\0".as_ptr(), 5) };
            unsafe {
                quals.add(q, self.arena);
            }
            self.at += 1;
        }
        if unsafe { self.is_kw(self.at, b"unsafe\0".as_ptr()) } {
            is_unsafe = true;
            let q = unsafe { self.mk(pm_jit_rsx_ast_kind::ATTR, line, b"unsafe\0".as_ptr(), 6) };
            unsafe {
                quals.add(q, self.arena);
            }
            self.at += 1;
        }
        if unsafe { self.is_kw(self.at, b"extern\0".as_ptr()) } {
            is_extern = true;
            let q = unsafe { self.mk(pm_jit_rsx_ast_kind::ATTR, line, b"extern\0".as_ptr(), 6) };
            unsafe {
                quals.add(q, self.arena);
            }
            self.at += 1;
            if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::STRING_LITERAL {
                let a = unsafe {
                    self.mk(
                        pm_jit_rsx_ast_kind::TYPE,
                        line,
                        self.text(self.at),
                        self.text_len(self.at),
                    )
                };
                unsafe {
                    quals.add(a, self.arena);
                }
                self.at += 1;
            }
        }
        if !unsafe { self.is_kw(self.at, b"fn\0".as_ptr()) } {
            unsafe {
                self.err(b"expected 'fn'\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        self.at += 1;
        if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::IDENT {
            unsafe {
                self.err(b"expected function name\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        let name = unsafe { self.text(self.at) };
        let name_len = unsafe { self.text_len(self.at) };
        self.at += 1;
        if unsafe { self.is_punct(self.at, b'<') } {
            /* Lifetime-only generics (`fn f<'a>(..)`) are compile-time
             * only — skip the bracket group when every top-level element
             * starts with a lifetime token; type params still refuse
             * (C codegen would silently drop them). Bounds on lifetimes
             * (`'a: 'b`) stay inside the group — skipped with it. */
            if unsafe { self.kind(self.at + 1) } == pm_jit_rsx_tok_kind::LIFETIME {
                let mut at = self.at + 1;
                let mut all_lifetimes = true;
                let mut depth = 1i32;
                at += 1;
                while at < self.n_toks && depth > 0 {
                    if unsafe { self.kind(at) } == pm_jit_rsx_tok_kind::PUNCT {
                        if unsafe { self.is_punct(at, b'<') } {
                            depth += 1;
                        } else if unsafe { self.is_punct(at, b'>') } {
                            depth -= 1;
                            if depth == 0 {
                                break;
                            }
                        }
                    }
                    at += 1;
                }
                /* every element must open with a lifetime: check the token
                 * right after `<` and after each depth-1 `,` */
                let mut p = self.at + 2;
                let mut d = 1i32;
                while p < at {
                    if unsafe { self.kind(p) } == pm_jit_rsx_tok_kind::PUNCT {
                        if unsafe { self.is_punct(p, b'<') } {
                            d += 1;
                        } else if unsafe { self.is_punct(p, b'>') } {
                            d -= 1;
                        } else if unsafe { self.is_punct(p, b',') } && d == 1 {
                            if unsafe { self.kind(p + 1) } != pm_jit_rsx_tok_kind::LIFETIME {
                                all_lifetimes = false;
                            }
                        }
                    }
                    p += 1;
                }
                if all_lifetimes && depth == 0 {
                    self.at = at + 1;
                } else {
                    unsafe {
                        self.err(b"unsupported: generics on fn\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
            } else {
                unsafe {
                    self.err(b"unsupported: generics on fn\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
        }
        let _ = is_unsafe;
        let _ = is_extern;
        let mut body = Kids::new();
        if !unsafe { self.is_punct(self.at, b'(') } {
            unsafe {
                self.err(b"expected '(' in fn params\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        self.at += 1;
        loop {
            if unsafe { self.is_punct(self.at, b')') } {
                self.at += 1;
                break;
            }
            if !self.ok {
                return core::ptr::null_mut();
            }
            if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::END {
                unsafe {
                    self.err(b"expected ')' in fn params before end of file\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            let before = self.at;
            /* receiver: self, &self, &mut self, mut self */
            let is_self = unsafe { self.is_kw(self.at, b"self\0".as_ptr()) };
            let amp_self = unsafe { self.is_punct(self.at, b'&') }
                && unsafe { self.is_kw(self.at + 1, b"self\0".as_ptr()) };
            let amp_mut_self = unsafe { self.is_punct(self.at, b'&') }
                && unsafe { self.is_kw(self.at + 1, b"mut\0".as_ptr()) }
                && unsafe { self.is_kw(self.at + 2, b"self\0".as_ptr()) };
            if is_self || amp_self || amp_mut_self {
                let rec: *const u8 = if is_self {
                    b"self\0".as_ptr()
                } else if amp_mut_self {
                    b"&mut self\0".as_ptr()
                } else {
                    b"&self\0".as_ptr()
                };
                let rl: usize = if is_self {
                    4
                } else if amp_mut_self {
                    9
                } else {
                    5
                };
                let p = unsafe { self.mk(pm_jit_rsx_ast_kind::PARAM, line, rec, rl) };
                unsafe {
                    body.add(p, self.arena);
                }
                if is_self {
                    self.at += 1;
                } else if amp_mut_self {
                    self.at += 3;
                } else {
                    self.at += 2;
                }
                if unsafe { self.is_punct(self.at, b',') } {
                    self.at += 1;
                }
                continue;
            }
            /* typed param: [mut] name : Type */
            let mut pname: *const u8 = b"_\0".as_ptr();
            let mut plen: usize = 1;
            if unsafe { self.is_kw(self.at, b"mut\0".as_ptr()) } {
                self.at += 1;
            }
            if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::IDENT {
                pname = unsafe { self.text(self.at) };
                plen = unsafe { self.text_len(self.at) };
                self.at += 1;
            } else {
                unsafe {
                    self.err(b"expected parameter name\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            if !unsafe { self.is_punct(self.at, b':') } {
                unsafe {
                    self.err(b"expected ':' after parameter name\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            self.at += 1;
            let pty = unsafe { self.parse_type() };
            let p = unsafe { self.mk(pm_jit_rsx_ast_kind::PARAM, line, pname, plen) };
            let mut pk = Kids::new();
            unsafe {
                pk.add(pty, self.arena);
            }
            unsafe {
                self.set_kids(p, &pk);
            }
            unsafe {
                body.add(p, self.arena);
            }
            if self.ok && self.at == before {
                unsafe {
                    self.err(b"internal: fn param consumed no tokens\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            if unsafe { self.is_punct(self.at, b',') } {
                self.at += 1;
                continue;
            }
            if !unsafe { self.is_punct(self.at, b')') } {
                unsafe {
                    self.err(b"expected ',' or ')' in fn params\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
        }
        /* return type */
        let mut ret: *mut pm_jit_rsx_ast_t = core::ptr::null_mut();
        if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::ARROW {
            self.at += 1;
            ret = unsafe { self.parse_type() };
            unsafe {
                body.add(ret, self.arena);
            }
        }
        /* where clause */
        if unsafe { self.is_kw(self.at, b"where\0".as_ptr()) } {
            unsafe {
                self.err(b"unsupported: where clause\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        if declare_only != 0 {
            if !unsafe { self.is_punct(self.at, b';') } {
                unsafe {
                    self.err(b"expected ';' after extern fn\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            self.at += 1;
        } else {
            let b = unsafe { self.parse_block() };
            unsafe {
                body.add(b, self.arena);
            }
        }
        let n = unsafe { self.mk(pm_jit_rsx_ast_kind::FN, line, name, name_len) };
        /* attrs/vis, quals, params, ret, body */
        let mut all = Kids::new();
        let mut i = 0usize;
        while i < kids.n {
            unsafe {
                all.add(*kids.fixed.as_ptr().add(i), self.arena);
            }
            i += 1;
        }
        let mut q = 0usize;
        while q < quals.n {
            let one: *mut pm_jit_rsx_ast_t = if q < KIDS_INLINE {
                unsafe { *quals.fixed.as_ptr().add(q) }
            } else {
                unsafe { *quals.spill.add(q - KIDS_INLINE) }
            };
            unsafe {
                all.add(one, self.arena);
            }
            q += 1;
        }
        let mut j = 0usize;
        while j < body.n {
            let one: *mut pm_jit_rsx_ast_t = if j < KIDS_INLINE {
                unsafe { *body.fixed.as_ptr().add(j) }
            } else {
                unsafe { *body.spill.add(j - KIDS_INLINE) }
            };
            unsafe {
                all.add(one, self.arena);
            }
            j += 1;
        }
        unsafe {
            self.set_kids(n, &all);
        }
        n
    }

    unsafe fn parse_fn(&mut self, kids: &mut Kids, declare_only: usize) -> *mut pm_jit_rsx_ast_t {
        unsafe { self.parse_fn_sig(kids, declare_only) }
    }

    /* Trait declaration item: `pub trait NAME[: Super] { fn sigs }`.
     * Lowered as an IMPL-shaped node carrying a "traitdecl" ATTR marker
     * — the trait object type (a `&mut dyn NAME`) is a C struct whose
     * fields are the trait's method fn-ptrs (the vtable inlined), so
     * the DECLARATION lowers a struct typedef + fn-ptr types, and the
     * trait IMPLS fill it. Methods parse declare-only (parse_fn_sig
     * with declare_only=1), including `&self`/`&mut self` receivers —
     * the object is the first param in the fn-ptr type. Supertrait
     * bounds (`: Send`) after the name are skipped balanced (like
     * impl generics): the object's vtable covers the declared methods
     * only; supertraits that are marker traits need no slots. */
    unsafe fn parse_trait(&mut self, kids: &mut Kids) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        self.at += 1;
        /* the object name */
        if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::IDENT {
            unsafe {
                self.err(b"expected trait name\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        let name_tok = self.at;
        self.at += 1;
        /* supertrait bounds: `: Send` / `: Send + Sync` — skip balanced
         * to the '{' (bounds can carry generics/paths, none of which
         * change the object shape: marker supertraits carry no slots,
         * and the object type is spelled by leaf name anyway). */
        if unsafe { self.is_punct(self.at, b':') } {
            let mut depth = 0usize;
            loop {
                let k = unsafe { self.kind(self.at) };
                if k == pm_jit_rsx_tok_kind::END {
                    unsafe {
                        self.err(b"expected '{' in trait\0".as_ptr());
                    }
                    return core::ptr::null_mut();
                }
                if unsafe { self.is_punct(self.at, b'{') } {
                    if depth == 0 {
                        break;
                    }
                    depth -= 1;
                } else if unsafe { self.is_punct(self.at, b'(') }
                    || unsafe { self.is_punct(self.at, b'<') }
                {
                    depth += 1;
                }
                self.at += 1;
            }
        }
        if !unsafe { self.is_punct(self.at, b'{') } {
            unsafe {
                self.err(b"expected '{' in trait\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        self.at += 1;
        /* method sigs — declare-only FN kids (body-less) */
        let mut body = Kids::new();
        loop {
            if unsafe { self.is_punct(self.at, b'}') } {
                self.at += 1;
                break;
            }
            if !self.ok {
                return core::ptr::null_mut();
            }
            if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::END {
                unsafe {
                    self.err(b"expected '}' in trait before end of file\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            let before = self.at;
            if unsafe { self.is_punct(self.at, b'#') } {
                let mut ik = Kids::new();
                unsafe {
                    self.parse_outer_attrs(&mut ik);
                }
                if unsafe { self.is_kw(self.at, b"fn\0".as_ptr()) } {
                    let f = unsafe { self.parse_fn_sig(&mut ik, 1) };
                    unsafe {
                        body.add(f, self.arena);
                    }
                    continue;
                }
                unsafe {
                    self.err(b"unsupported: trait member\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            if unsafe { self.is_kw(self.at, b"fn\0".as_ptr()) } {
                let mut ik = Kids::new();
                let f = unsafe { self.parse_fn_sig(&mut ik, 1) };
                unsafe {
                    body.add(f, self.arena);
                }
                continue;
            }
            if self.ok && self.at == before {
                unsafe {
                    self.err(b"internal: trait member consumed no tokens\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            unsafe {
                self.err(b"unsupported: trait member\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        /* TRAIT-kind node (the kind table's reserved slot 10): the
         * object name as the node's text, the declare-only method sigs
         * as FN kids. The item pass emits the object typedef (a C
         * struct of method fn-ptrs — the vtable inlined) and records
         * each method's slot for dyn dispatch; trait impls fill it. */
        let n = unsafe {
            self.mk(
                pm_jit_rsx_ast_kind::TRAIT,
                line,
                self.text(name_tok),
                self.text_len(name_tok),
            )
        };
        let mut all = Kids::new();
        let mut i = 0usize;
        while i < kids.n {
            unsafe {
                all.add(*kids.fixed.as_ptr().add(i), self.arena);
            }
            i += 1;
        }
        let mut j = 0usize;
        while j < body.n {
            let one: *mut pm_jit_rsx_ast_t = if j < KIDS_INLINE {
                unsafe { *body.fixed.as_ptr().add(j) }
            } else {
                unsafe { *body.spill.add(j - KIDS_INLINE) }
            };
            unsafe {
                all.add(one, self.arena);
            }
            j += 1;
        }
        unsafe {
            self.set_kids(n, &all);
        }
        n
    }

    /* impl block: inherent or trait impl; members are fns. */
    unsafe fn parse_impl(&mut self, kids: &mut Kids) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        self.at += 1;
        /* Generic parameter list on the impl itself (`impl<T>`, `impl<T:
         * Send>`): accepted and skipped — the only supported use is a
         * marker-trait impl on a transparent newtype (`unsafe impl<T> Sync
         * for Mut<T> {}`), whose empty body lowers nothing. Bounds are
         * consumed as balanced tokens until the closing `>`. */
        if unsafe { self.is_punct(self.at, b'<') } {
            self.at += 1;
            let mut depth = 1usize;
            while self.at < self.n_toks && depth > 0 {
                if unsafe { self.is_punct(self.at, b'<') } {
                    depth += 1;
                } else if unsafe { self.is_punct(self.at, b'>') } {
                    depth -= 1;
                } else if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::SHR {
                    /* `>>` closes two levels when bounds nest */
                    if depth >= 2 {
                        depth -= 2;
                    } else {
                        depth -= 1;
                    }
                }
                if depth > 0 {
                    self.at += 1;
                }
            }
            if depth == 0 {
                self.at += 1;
            }
        }
        /* Type path (no generics on the impl itself). In `impl Trait for
         * Type` the FIRST path names the trait and the path after `for`
         * names the SELF type — swap so self_ty stays the self type and
         * the trait node wraps the trait path (both impl forms put the
         * self type where the lowering's mangling and receiver typing
         * read it). */
        let mut self_ty = unsafe { self.parse_path_type() };
        let mut body = Kids::new();
        /* `impl Trait for Type` — the first path names the TRAIT and the
         * path after `for` names the SELF type: re-swap so self_ty is the
         * self type and the trait node (TYPE with text "trait") wraps the
         * trait path — the shape every lowering reader expects. */
        let mut trait_node: *mut pm_jit_rsx_ast_t = core::ptr::null_mut();
        if unsafe { self.is_kw(self.at, b"for\0".as_ptr()) } {
            self.at += 1;
            let real_self = unsafe { self.parse_path_type() };
            trait_node = unsafe {
                self.mk(pm_jit_rsx_ast_kind::TYPE, line, b"trait\0".as_ptr(), 5)
            };
            let mut tk = Kids::new();
            unsafe {
                tk.add(self_ty, self.arena);
            }
            unsafe {
                self.set_kids(trait_node, &tk);
            }
            self_ty = real_self;
        }
        if !unsafe { self.is_punct(self.at, b'{') } {
            unsafe {
                self.err(b"expected '{' in impl\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        self.at += 1;
        loop {
            if unsafe { self.is_punct(self.at, b'}') } {
                self.at += 1;
                break;
            }
            if !self.ok {
                return core::ptr::null_mut();
            }
            if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::END {
                unsafe {
                    self.err(b"expected '}' in impl before end of file\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            let before = self.at;
            if unsafe { self.is_punct(self.at, b'#') } {
                let mut ik = Kids::new();
                unsafe {
                    self.parse_outer_attrs(&mut ik);
                }
                if unsafe { self.is_kw(self.at, b"fn\0".as_ptr()) } {
                    let f = unsafe { self.parse_fn(&mut ik, 0) };
                    unsafe {
                        body.add(f, self.arena);
                    }
                    continue;
                }
                unsafe {
                    self.err(b"unsupported: impl member\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            if unsafe { self.is_kw(self.at, b"pub\0".as_ptr()) } {
                let mut ik = Kids::new();
                unsafe {
                    self.parse_vis(&mut ik);
                }
                let f = unsafe { self.parse_fn(&mut ik, 0) };
                unsafe {
                    body.add(f, self.arena);
                }
                continue;
            }
            if unsafe { self.is_kw(self.at, b"fn\0".as_ptr()) } {
                let mut ik = Kids::new();
                let f = unsafe { self.parse_fn(&mut ik, 0) };
                unsafe {
                    body.add(f, self.arena);
                }
                continue;
            }
            /* `unsafe fn` (and `pub unsafe fn`) members. */
            if unsafe { self.is_kw(self.at, b"unsafe\0".as_ptr()) } {
                let mut ik = Kids::new();
                let f = unsafe { self.parse_fn(&mut ik, 0) };
                unsafe {
                    body.add(f, self.arena);
                }
                continue;
            }
            if unsafe { self.is_kw(self.at, b"const\0".as_ptr()) } {
                let mut ik = Kids::new();
                let s = unsafe { self.parse_static(&mut ik, 0) };
                unsafe {
                    body.add(s, self.arena);
                }
                continue;
            }
            if self.ok && self.at == before {
                unsafe {
                    self.err(b"internal: impl member consumed no tokens\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            unsafe {
                self.err(b"unsupported: impl member\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        let n = unsafe { self.mk(pm_jit_rsx_ast_kind::IMPL, line, b"impl\0".as_ptr(), 4) };
        let mut all = Kids::new();
        let mut i = 0usize;
        while i < kids.n {
            unsafe {
                all.add(*kids.fixed.as_ptr().add(i), self.arena);
            }
            i += 1;
        }
        unsafe {
            all.add(self_ty, self.arena);
        }
        if !trait_node.is_null() {
            unsafe {
                all.add(trait_node, self.arena);
            }
        }
        let mut j = 0usize;
        while j < body.n {
            let one: *mut pm_jit_rsx_ast_t = if j < KIDS_INLINE {
                unsafe { *body.fixed.as_ptr().add(j) }
            } else {
                unsafe { *body.spill.add(j - KIDS_INLINE) }
            };
            unsafe {
                all.add(one, self.arena);
            }
            j += 1;
        }
        unsafe {
            self.set_kids(n, &all);
        }
        n
    }

    /* Top-level item dispatch. */
    unsafe fn parse_item(&mut self) -> *mut pm_jit_rsx_ast_t {
        let mut kids = Kids::new();
        unsafe {
            self.parse_attrs_and_vis(&mut kids);
        }
        if !self.ok {
            return core::ptr::null_mut();
        }
        /* cfg-false item: strip (skip its tokens balanced). The lowered
         * unit never sees it — same discipline as cfg(test) at splice
         * time, but here inside rsx where the predicate is evaluated
         * against the active feature set. */
        if unsafe { self.cfg_verdict(&kids) } == 0 {
            unsafe {
                self.skip_item_tokens();
            }
            let line = unsafe { self.line(self.at) };
            return unsafe {
                self.mk(pm_jit_rsx_ast_kind::STMT, line, b"cfgskip\0".as_ptr(), 7)
            };
        }
        if unsafe { self.is_kw(self.at, b"use\0".as_ptr()) } {
            return unsafe { self.parse_use() };
        }
        if unsafe { self.is_kw(self.at, b"struct\0".as_ptr()) } {
            return unsafe { self.parse_struct(&mut kids) };
        }
        if unsafe { self.is_kw(self.at, b"union\0".as_ptr()) } {
            /* `pub union U { f: T, .. }` — same field body grammar as a
             * struct; a marker ATTR kid ("union") tells the lowering to
             * emit a C union instead of a struct. No new AST kind — the
             * kind table is a stable __types__.h contract. */
            let line = unsafe { self.line(self.at) };
            let marker = unsafe { self.mk(pm_jit_rsx_ast_kind::ATTR, line, b"union\0".as_ptr(), 5) };
            unsafe {
                kids.add(marker, self.arena);
            }
            return unsafe { self.parse_struct(&mut kids) };
        }
        if unsafe { self.is_kw(self.at, b"enum\0".as_ptr()) } {
            return unsafe { self.parse_enum(&mut kids) };
        }
        if unsafe { self.is_kw(self.at, b"extern\0".as_ptr()) } {
            /* `extern "C" fn ..` / `extern fn ..` is a function;
             * `extern { .. }` / `extern "C" { .. }` is a block. */
            let mut look = self.at + 1;
            if unsafe { self.kind(look) } == pm_jit_rsx_tok_kind::STRING_LITERAL {
                look += 1;
            }
            if unsafe { self.is_kw(look, b"fn\0".as_ptr()) } {
                return unsafe { self.parse_fn(&mut kids, 0) };
            }
            return unsafe { self.parse_extern_block() };
        }
        /* `unsafe extern "C" fn ..` is a function; `unsafe extern { .. }`
         * (no fn) is an extern block. Distinguish by lookahead. */
        if unsafe { self.is_kw(self.at, b"unsafe\0".as_ptr()) }
            && unsafe { self.is_kw(self.at + 1, b"extern\0".as_ptr()) }
        {
            let mut look = self.at + 2;
            if unsafe { self.kind(look) } == pm_jit_rsx_tok_kind::STRING_LITERAL {
                look += 1;
            }
            if !unsafe { self.is_kw(look, b"fn\0".as_ptr()) } {
                return unsafe { self.parse_extern_block() };
            }
        }
        /* `unsafe impl ..` (marker traits like Send/Sync) is an impl — the
         * unsafe qualifier is inert for the C translation. */
        if unsafe { self.is_kw(self.at, b"unsafe\0".as_ptr()) }
            && unsafe { self.is_kw(self.at + 1, b"impl\0".as_ptr()) }
        {
            self.at += 1;
            return unsafe { self.parse_impl(&mut kids) };
        }
        if unsafe { self.is_kw(self.at, b"static\0".as_ptr()) } {
            return unsafe { self.parse_static(&mut kids, 0) };
        }
        if unsafe { self.is_kw(self.at, b"const\0".as_ptr()) } {
            return unsafe { self.parse_static(&mut kids, 0) };
        }
        if unsafe { self.is_kw(self.at, b"type\0".as_ptr()) } {
            return unsafe { self.parse_type_alias(&mut kids) };
        }
        if unsafe { self.is_kw(self.at, b"fn\0".as_ptr()) }
            || unsafe { self.is_kw(self.at, b"unsafe\0".as_ptr()) }
            || unsafe { self.is_kw(self.at, b"extern\0".as_ptr()) }
            || unsafe { self.is_kw(self.at, b"const\0".as_ptr()) }
        {
            return unsafe { self.parse_fn(&mut kids, 0) };
        }
        if unsafe { self.is_kw(self.at, b"impl\0".as_ptr()) } {
            return unsafe { self.parse_impl(&mut kids) };
        }
        if unsafe { self.is_kw(self.at, b"mod\0".as_ptr()) } {
            return unsafe { self.parse_mod() };
        }
        if unsafe { self.is_kw(self.at, b"trait\0".as_ptr()) } {
            return unsafe { self.parse_trait(&mut kids) };
        }
        if unsafe { self.is_kw(self.at, b"macro_rules\0".as_ptr()) }
            || unsafe { self.is_kw(self.at, b"async\0".as_ptr()) }
        {
            unsafe {
                self.err(b"unsupported: item kind\0".as_ptr());
            }
            return core::ptr::null_mut();
        }
        /* Path-qualified item macro: `module::MACRO!(..)`. Skip the path. */
        if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::IDENT
            && unsafe { self.kind(self.at + 1) } == pm_jit_rsx_tok_kind::DOUBLE_COLON
        {
            let mut look = self.at + 2;
            loop {
                if unsafe { self.kind(look) } == pm_jit_rsx_tok_kind::MACRO_INVOC {
                    let line = unsafe { self.line(self.at) };
                    self.at = look + 1;
                    if unsafe { self.is_punct(self.at, b';') } {
                        self.at += 1;
                    }
                    return unsafe {
                        self.mk(pm_jit_rsx_ast_kind::MACRO, line, b"\0".as_ptr(), 0)
                    };
                }
                if unsafe { self.kind(look) } == pm_jit_rsx_tok_kind::IDENT
                    && unsafe { self.kind(look + 1) } == pm_jit_rsx_tok_kind::DOUBLE_COLON
                {
                    look += 2;
                    continue;
                }
                break;
            }
        }
        /* Item-level macro invocation (e.g. PM_MOD_EXPORT_RS! ctors). */
        if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::IDENT
            && unsafe { self.kind(self.at + 1) } == pm_jit_rsx_tok_kind::MACRO_INVOC
        {
            self.at += 2;
            if unsafe { self.is_punct(self.at, b';') } {
                self.at += 1;
            }
            return unsafe { self.mk(pm_jit_rsx_ast_kind::MACRO, unsafe { self.line(self.at) }, b"\0".as_ptr(), 0) };
        }
        if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::MACRO_INVOC {
            /* paths like `pymergetic_wasmmod::PM_MOD_EXPORT_RS!(...)` lex as
             * ident :: MACRO_INVOC — handle the DOUBLE_COLON form here. */
            let line = unsafe { self.line(self.at) };
            self.at += 1;
            if unsafe { self.is_punct(self.at, b';') } {
                self.at += 1;
            }
            return unsafe { self.mk(pm_jit_rsx_ast_kind::MACRO, line, b"\0".as_ptr(), 0) };
        }
        if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::DOUBLE_COLON {
            /* leading `::` path macro: `::core::…` or `::crate::…` */
            self.at += 1;
            return unsafe { self.parse_item() };
        }
        if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::END {
            return core::ptr::null_mut();
        }
        unsafe {
            self.err(b"unsupported: item kind\0".as_ptr());
        }
        core::ptr::null_mut()
    }

    unsafe fn parse_file(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        let mut kids = Kids::new();
        loop {
            if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::END {
                break;
            }
            /* inner attributes `#![...]` — skipped, not attached to an item.
             * Anywhere in the file, not just the top: the build face
             * splices `#[path]`-included face files into a unit, and a
             * face carries its own `#![...]` where the splice lands. */
            if unsafe { self.is_punct(self.at, b'#') }
                && unsafe { self.is_punct(self.at + 1, b'!') }
                && unsafe { self.is_punct(self.at + 2, b'[') }
            {
                /* skip to matching ']' */
                self.at += 3;
                let mut depth = 1i32;
                while self.at < self.n_toks && depth > 0 {
                    if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::PUNCT {
                        if unsafe { self.is_punct(self.at, b'[') } {
                            depth += 1;
                        } else if unsafe { self.is_punct(self.at, b']') } {
                            depth -= 1;
                            if depth == 0 {
                                break;
                            }
                        }
                    }
                    self.at += 1;
                }
                self.at += 1;
                continue;
            }
            let before = self.at;
            let item = unsafe { self.parse_item() };
            if !self.ok {
                return core::ptr::null_mut();
            }
            if self.at == before {
                unsafe {
                    self.err(b"internal: item consumed no tokens\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
            /* cfg-stripped item: parsed-and-skipped, never lowered. */
            if unsafe { (*item).kind } == pm_jit_rsx_ast_kind::STMT
                && unsafe { z_eq(unsafe { (*item).text }, unsafe { (*item).text_len }, b"cfgskip\0".as_ptr()) }
            {
                continue;
            }
            unsafe {
                kids.add(item, self.arena);
            }
        }
        let n = unsafe { self.mk(pm_jit_rsx_ast_kind::FILE, line, b"file\0".as_ptr(), 4) };
        unsafe {
            self.set_kids(n, &kids);
        }
        n
    }
}

