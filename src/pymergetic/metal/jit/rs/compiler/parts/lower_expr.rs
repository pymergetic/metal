/* ==== expression emitter ==== */

impl Lower {
    unsafe fn emit_literal(&mut self, t: *const u8, n: usize) {
        if n == 0 || t.is_null() {
            self.out.puts(b"0\0".as_ptr());
            return;
        }
        let c = unsafe { *t };
        /* strip the integer suffix for C (C literals are typed by context) */
        if c.is_ascii_digit() || c == b'0' {
            let mut end = 0usize;
            while end < n {
                let d = unsafe { *t.add(end) };
                if d.is_ascii_digit() || d == b'_' || d == b'x' || d == b'X' {
                    end += 1;
                    continue;
                }
                if (d >= b'a' && d <= b'f') || (d >= b'A' && d <= b'F') {
                    /* hex digit — only inside a 0x literal; conservative: keep */
                    end += 1;
                    continue;
                }
                break;
            }
            if end == 0 {
                end = 1;
            }
            /* emit without Rust's `_` separators — C numbers do not take
             * them (`1_000` is an invalid number to tcc) */
            let mut i = 0usize;
            while i < end {
                let d = unsafe { *t.add(i) };
                if d != b'_' {
                    self.out.putc(d);
                }
                i += 1;
            }
            return;
        }
        if c == b'-' || c == b'+' {
            self.out.put(t, n);
            return;
        }
        if c == b'\'' {
            /* char literal -> integer constant (UTF-32-ish); escapes kept as
             * C escapes ('\n' is the same in both). */
            self.out.put(t, n);
            return;
        }
        if c == b'"' {
            /* string literal: pass through (Rust escapes are C escapes for
             * the common cases: \n \t \r \\ \" \0) */
            self.out.put(t, n);
            return;
        }
        if c == b'b' {
            /* byte string b"..." / byte char b'c' — emit as C string/char */
            if n >= 2 && unsafe { *t.add(1) } == b'"' {
                self.out.put(t.add(1), n - 1);
                return;
            }
            if n >= 4 && unsafe { *t.add(1) } == b'\'' {
                /* b'x' -> 'x': strip the leading b, keep the quoted char
                 * (text spans include both quotes). */
                self.out.put(t.add(1), n - 1);
                return;
            }
        }
        if c == b'r' {
            /* raw string r"..." / r#"..."# -> C string with the quotes kept */
            let mut i = 1usize;
            let mut hashes = 0usize;
            while i < n && unsafe { *t.add(i) } == b'#' {
                hashes += 1;
                i += 1;
            }
            if i < n && unsafe { *t.add(i) } == b'"' {
                i += 1;
                let mut end = n;
                /* strip trailing "### */
                let mut k = 0usize;
                while k < hashes + 1 {
                    if end == 0 {
                        break;
                    }
                    end -= 1;
                    k += 1;
                }
                self.out.putc(b'"');
                self.out.put(t.add(i), end - i);
                self.out.putc(b'"');
                return;
            }
        }
        self.out.put(t, n);
    }

    /* Emit a path expression (values: locals, constants, enum variants,
     * known core paths). */
    unsafe fn emit_path_expr(&mut self, e: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        let kids = unsafe { (*e).kids };
        let nk = unsafe { (*e).n_kids } as usize;
        if nk == 0 {
            /* bare PATH carries its name in its own text (parser shorthand
             * for struct-lit field init `S { a }` builds it this way). */
            if unsafe { (*e).text_len } > 0 {
                self.out.put(unsafe { (*e).text }, unsafe { (*e).text_len });
            }
            return;
        }
        /* `None` building a struct-shaped Option: zero + clear tag, as a
         * compound literal of the fn's own rsx_opt_<elem> return type.
         * Pointer-Option None keeps the bare `0` render below. */
        if nk == 1 && self.cur_ret_len > 8 {
            let only = unsafe { *kids.add(0) };
            if unsafe { z_eq(unsafe { (*only).text }, unsafe { (*only).text_len }, b"None\0".as_ptr()) } {
                let eb = self.arena_tmp();
                let eln = unsafe {
                    Lower::opt_typedef_elem(self.cur_ret.as_ptr(), self.cur_ret_len, eb, 160)
                };
                if eln > 0 {
                    let tdn = self.arena_tmp();
                    let tdn_len = unsafe { Lower::opt_typedef_name(eb, eln, tdn, 160) };
                    if tdn_len > 0 {
                        self.out.putc(b'(');
                        self.out.put(tdn, tdn_len);
                        self.out.puts(b"){ ._v = {0}, ._has = 0 }\0".as_ptr());
                        return;
                    }
                }
            }
        }
        /* multi-segment: core::ptr::null(), Type::MAX, enum variants. */
        if nk >= 2 {
            let last = unsafe { *kids.add(nk - 1) };
            let ltext = unsafe { (*last).text };
            let llen = unsafe { (*last).text_len };
            /* `S { .. }` / `m::S { .. }`: the path wraps a struct literal —
             * the wrapping node is what emit_struct_lit wants. */
            if unsafe { (*last).kind } == pm_jit_rsx_ast_kind::STRUCT_LIT {
                unsafe { self.emit_struct_lit(e, locals) };
                return;
            }
            /* core::… known paths (2+ segments, first is `core` or `crate`). */
            let first = unsafe { *kids.add(0) };
            let ftext = unsafe { (*first).text };
            let flen = unsafe { (*first).text_len };
            let second = unsafe { *kids.add(1) };
            let stext = unsafe { (*second).text };
            let slen = unsafe { (*second).text_len };
            if unsafe { z_eq(ftext, flen, b"core\0".as_ptr()) } && nk >= 3 {
                if unsafe { z_eq(stext, slen, b"ptr\0".as_ptr()) } {
                    if unsafe { z_eq(ltext, llen, b"null\0".as_ptr()) } {
                        self.out.puts(b"0\0".as_ptr());
                        return;
                    }
                    if unsafe { z_eq(ltext, llen, b"null_mut\0".as_ptr()) } {
                        self.out.puts(b"0\0".as_ptr());
                        return;
                    }
                    if unsafe { z_eq(ltext, llen, b"null\0".as_ptr()) } {
                        self.out.puts(b"0\0".as_ptr());
                        return;
                    }
                }
                if unsafe { z_eq(stext, slen, b"mem\0".as_ptr()) } {
                    if unsafe { z_eq(ltext, llen, b"size_of\0".as_ptr()) } {
                        /* size_of::<T>() arrives as a CALL node; bare path
                         * (function value) refuses. */
                        unsafe {
                            self.err(b"unsupported: fn as value\0".as_ptr(), unsafe { (*e).line });
                        }
                        return;
                    }
                }
            }
            /* iN::MAX/MIN and uN::MAX/MIN -> stdint limit macros. */
            if nk == 2 {
                let limit = self.int_limit(ftext, flen, ltext, llen);
                if !limit.is_null() {
                    self.out.puts(limit);
                    return;
                }
                /* `Ordering::X` -> the C __ATOMIC_* value as a literal
                 * (same numbering stdatomic.h assigns). */
                if unsafe { z_eq(ftext, flen, b"Ordering\0".as_ptr()) } {
                    let ord = unsafe { self.atomic_order(e) };
                    if ord >= 0 {
                        self.out.put_u32(ord as u32);
                        return;
                    }
                    unsafe {
                        self.err(b"unsupported: unknown Ordering variant\0".as_ptr(), unsafe { (*e).line });
                    }
                    return;
                }
                /* 2-segment enum variant: `State::Ready` -> State_Ready.
                 * A TAGGED enum's fieldless variant is a compound literal:
                 * the value is the struct, never the bare tag constant. */
                if unsafe { (*last).kind } == pm_jit_rsx_ast_kind::PATH {
                    let mut full = self.arena_tmp();
                    let mut at = 0usize;
                    at = unsafe { bput(full, 128, at, ftext, flen) };
                    at = unsafe { bput(full, 128, at, b"_\0".as_ptr(), 1) };
                    at = unsafe { bput(full, 128, at, ltext, llen) };
                    unsafe {
                        *full.add(at) = 0;
                    }
                    if unsafe { self.enumtags.has(ftext, flen) } {
                        /* designated init zero-fills `_u` — no nested
                         * brace warning for fieldless variants */
                        self.out.putc(b'(');
                        self.out.put(ftext, flen);
                        self.out.puts(b"){ ._tag = \0".as_ptr());
                        self.out.put(full, at);
                        self.out.puts(b" }\0".as_ptr());
                        return;
                    }
                    /* A 2-segment path may also be a static (SYM::NAME) —
                     * emit the joined name either way; C sees the typedef'd
                     * constant or the enum member. */
                    self.out.put(full, at);
                    return;
                }
            }
            /* 3+ segment non-core path: keep segments joined with '_' —
             * matches the enum lowering (Enum_Variant). */
            if unsafe { (*last).kind } == pm_jit_rsx_ast_kind::PATH {
                let mut full = self.arena_tmp();
                let mut at = 0usize;
                let mut i = 0usize;
                while i < nk {
                    let seg = unsafe { *kids.add(i) };
                    if unsafe { (*seg).kind } != pm_jit_rsx_ast_kind::PATH {
                        i += 1;
                        continue;
                    }
                    if at > 0 {
                        at = unsafe { bput(full, 128, at, b"_\0".as_ptr(), 1) };
                    }
                    at = unsafe { bput(full, 128, at, unsafe { (*seg).text }, unsafe { (*seg).text_len }) };
                    i += 1;
                }
                unsafe {
                    *full.add(at) = 0;
                }
                self.out.put(full, at);
                return;
            }
            self.out.put(ltext, llen);
            return;
        }
        /* single segment */
        let only = unsafe { *kids.add(0) };
        if unsafe { (*only).kind } == pm_jit_rsx_ast_kind::STRUCT_LIT {
            unsafe { self.emit_struct_lit(only, locals) };
            return;
        }
        /* rename-shadow: a type-changing `let t = ..` re-binds the C
         * spelling; every later source reference emits the C name. */
        if !locals.is_null() {
            let nt = unsafe { (*only).text };
            let ntl = unsafe { (*only).text_len };
            let cnb = self.arena_tmp();
            let cnl = unsafe { (*locals).lookup_cname(nt, ntl, cnb) };
            if cnl > 0 {
                self.out.put(cnb, cnl);
                return;
            }
        }
        self.out.put(unsafe { (*only).text }, unsafe { (*only).text_len });
    }

    /* iN::MIN/MAX, uN::MAX, usize::MAX -> stdint limit macro text. */
    unsafe fn int_limit(&mut self, ty: *const u8, tlen: usize, name: *const u8, nlen: usize) -> *const u8 {
        let is_min = unsafe { z_eq(name, nlen, b"MIN\0".as_ptr()) };
        let is_max = unsafe { z_eq(name, nlen, b"MAX\0".as_ptr()) };
        if !is_min && !is_max {
            return core::ptr::null();
        }
        if unsafe { z_eq(ty, tlen, b"i8\0".as_ptr()) } {
            return if is_min { b"INT8_MIN\0".as_ptr() } else { b"INT8_MAX\0".as_ptr() };
        }
        if unsafe { z_eq(ty, tlen, b"i16\0".as_ptr()) } {
            return if is_min { b"INT16_MIN\0".as_ptr() } else { b"INT16_MAX\0".as_ptr() };
        }
        if unsafe { z_eq(ty, tlen, b"i32\0".as_ptr()) } {
            return if is_min { b"INT32_MIN\0".as_ptr() } else { b"INT32_MAX\0".as_ptr() };
        }
        if unsafe { z_eq(ty, tlen, b"i64\0".as_ptr()) } {
            return if is_min { b"INT64_MIN\0".as_ptr() } else { b"INT64_MAX\0".as_ptr() };
        }
        if unsafe { z_eq(ty, tlen, b"u8\0".as_ptr()) } {
            return if is_min { b"0\0".as_ptr() } else { b"UINT8_MAX\0".as_ptr() };
        }
        if unsafe { z_eq(ty, tlen, b"u16\0".as_ptr()) } {
            return if is_min { b"0\0".as_ptr() } else { b"UINT16_MAX\0".as_ptr() };
        }
        if unsafe { z_eq(ty, tlen, b"u32\0".as_ptr()) } {
            return if is_min { b"0\0".as_ptr() } else { b"UINT32_MAX\0".as_ptr() };
        }
        if unsafe { z_eq(ty, tlen, b"u64\0".as_ptr()) } {
            return if is_min { b"0\0".as_ptr() } else { b"UINT64_MAX\0".as_ptr() };
        }
        if unsafe { z_eq(ty, tlen, b"usize\0".as_ptr()) } {
            return if is_min { b"0\0".as_ptr() } else { b"SIZE_MAX\0".as_ptr() };
        }
        if unsafe { z_eq(ty, tlen, b"isize\0".as_ptr()) } {
            return if is_min { b"INTPTR_MIN\0".as_ptr() } else { b"INTPTR_MAX\0".as_ptr() };
        }
        core::ptr::null()
    }

    /* Memory-order argument of an atomic op: an `Ordering::X` path (2
     * segments) or a bare `X`. Returns the C __ATOMIC_* value — the
     * same numbering stdatomic.h assigns: Relaxed=0, Consume=1,
     * Acquire=2, Release=3, AcqRel=4, SeqCst=5 — or -1 (not an order:
     * the caller refuses). */
    unsafe fn atomic_order(&mut self, arg: *const pm_jit_rsx_ast_t) -> i32 {
        let mut a = arg;
        while unsafe { (*a).kind } == pm_jit_rsx_ast_kind::PAREN
            && unsafe { (*a).n_kids } as usize >= 1
        {
            a = unsafe { *(*a).kids.add(0) };
        }
        if unsafe { (*a).kind } != pm_jit_rsx_ast_kind::PATH {
            return -1;
        }
        let kids = unsafe { (*a).kids };
        let nk = unsafe { (*a).n_kids } as usize;
        let mut t = unsafe { (*a).text };
        let mut tl = unsafe { (*a).text_len };
        if nk >= 2 {
            t = unsafe { (*(*kids.add(nk - 1))).text };
            tl = unsafe { (*(*kids.add(nk - 1))).text_len };
        }
        if unsafe { z_eq(t, tl, b"Relaxed\0".as_ptr()) } {
            return 0;
        }
        if unsafe { z_eq(t, tl, b"Consume\0".as_ptr()) } {
            return 1;
        }
        if unsafe { z_eq(t, tl, b"Acquire\0".as_ptr()) } {
            return 2;
        }
        if unsafe { z_eq(t, tl, b"Release\0".as_ptr()) } {
            return 3;
        }
        if unsafe { z_eq(t, tl, b"AcqRel\0".as_ptr()) } {
            return 4;
        }
        if unsafe { z_eq(t, tl, b"SeqCst\0".as_ptr()) } {
            return 5;
        }
        -1
    }

    /* Is this expression a bare `None`? Parser shape: a PATH whose single
     * kid is a PATH named None (field/arg positions); the wrapping node's
     * own text may also be None (pattern shorthands). */
    unsafe fn expr_is_none(&mut self, e: *const pm_jit_rsx_ast_t) -> bool {
        if e.is_null() || unsafe { (*e).kind } != pm_jit_rsx_ast_kind::PATH {
            return false;
        }
        let t = unsafe { (*e).text };
        let tl = unsafe { (*e).text_len };
        if tl == 4 && !t.is_null() && unsafe { z_eq(t, tl, b"None\0".as_ptr()) } {
            return true;
        }
        let kids = unsafe { (*e).kids };
        let nk = unsafe { (*e).n_kids } as usize;
        if nk == 1 {
            let only = unsafe { *kids.add(0) };
            if unsafe { (*only).kind } == pm_jit_rsx_ast_kind::PATH {
                let ot = unsafe { (*only).text };
                let otl = unsafe { (*only).text_len };
                if otl == 4 && !ot.is_null() && unsafe { z_eq(ot, otl, b"None\0".as_ptr()) } {
                    return true;
                }
            }
        }
        false
    }

    /* Is this static initializer provably all-zero? A `static` whose
     * initializer folds to zero at every byte has no .data content worth
     * materializing: the initializer is dropped and the C declaration is
     * emitted bare (a tentative definition), so the object carries the
     * storage in .bss with zero file bytes instead of a multi-megabyte
     * zero blob through the compiler's section machinery. The predicate
     * walks the accepted static-initializer shapes only:
     *
     *   - `0` (and `0u32`, `-0` is excluded by the shape check anyway)
     *   - `false`
     *   - `None` (both the pointer Option — NULL — and the struct-shaped
     *     Option — { ._v = {0}, ._has = 0 })
     *   - `core::ptr::null[_mut]()` — renders 0
     *   - an enum variant whose discriminant is 0 (`E::V`, bare `V`,
     *     joined `E_V`)
     *   - an array literal / `[e; n]` repeat whose every element (resp.
     *     the one repeated element) is all-zero
     *   - a struct literal whose every field value is all-zero
     *   - the constant-lowering wrappers over any of those: a transparent
     *     newtype ctor (`Mut(..)`) and `UnsafeCell::new(..)` / `Cell::new(..)`
     *
     * Anything else — a nonzero literal, a fn-named initializer, a
     * non-constant expression — is not provably zero, so the full
     * initializer is emitted as before. False positives are impossible
     * (every leaf case maps to a C zero initializer); false negatives
     * merely keep the explicit emission, never a miscompile. */
    unsafe fn init_all_zero(&mut self, e: *const pm_jit_rsx_ast_t) -> bool {
        if e.is_null() {
            return false;
        }
        let kind = unsafe { (*e).kind };
        match kind {
            pm_jit_rsx_ast_kind::LITERAL => {
                let t = unsafe { (*e).text };
                let tl = unsafe { (*e).text_len };
                if tl == 0 || t.is_null() {
                    return false;
                }
                let mut at = 0usize;
                if unsafe { *t } == b'0'
                    && tl > 1
                    && (unsafe { *t.add(1) } == b'x' || unsafe { *t.add(1) } == b'X')
                {
                    at = 2;
                }
                let mut all_zero = at < tl;
                while at < tl {
                    let c = unsafe { *t.add(at) };
                    /* digits (with _ separators and type suffixes): every
                     * digit must be 0; suffix chars only follow digits */
                    if c == b'_' {
                        at += 1;
                        continue;
                    }
                    if c < b'0' || c > b'9' {
                        if c == b'u' || c == b'U' || c == b'i' || c == b'I' {
                            at += 1;
                            continue;
                        }
                        all_zero = false;
                        break;
                    }
                    if c != b'0' {
                        all_zero = false;
                        break;
                    }
                    at += 1;
                }
                all_zero
            }
            pm_jit_rsx_ast_kind::PAREN => {
                if unsafe { (*e).n_kids } as usize >= 1 {
                    unsafe { self.init_all_zero(*(*e).kids.add(0)) }
                } else {
                    false
                }
            }
            pm_jit_rsx_ast_kind::PATH => unsafe { self.path_is_zero(e) },
            pm_jit_rsx_ast_kind::ARRAY => {
                let kids = unsafe { (*e).kids };
                let nk = unsafe { (*e).n_kids } as usize;
                let is_repeat = unsafe { z_eq((*e).text, (*e).text_len, b"[;]\0".as_ptr()) };
                if is_repeat {
                    if nk == 2 {
                        unsafe { self.init_all_zero(*kids.add(0)) }
                    } else {
                        false
                    }
                } else {
                    let mut i = 0usize;
                    while i < nk {
                        if !unsafe { self.init_all_zero(*kids.add(i)) } {
                            return false;
                        }
                        i += 1;
                    }
                    true
                }
            }
            pm_jit_rsx_ast_kind::STRUCT_LIT => unsafe { self.struct_lit_is_zero(e) },
            pm_jit_rsx_ast_kind::CALL => {
                /* the constant-lowering wrappers: newtype ctor (`Mut(x)`),
                 * UnsafeCell/Cell::new — zero-ness rides the sole argument.
                 * Node shape mirrors emit_call: kids[0] = callee PATH,
                 * kids[1] = args container. */
                let kids = unsafe { (*e).kids };
                let nk = unsafe { (*e).n_kids } as usize;
                if nk < 2 {
                    return false;
                }
                let callee = unsafe { *kids.add(0) };
                let args = unsafe { *kids.add(1) };
                if unsafe { (*callee).kind } != pm_jit_rsx_ast_kind::PATH {
                    return false;
                }
                let an = unsafe { (*args).n_kids } as usize;
                let ak = unsafe { (*args).kids };
                let ck = unsafe { (*callee).kids };
                let cn = unsafe { (*callee).n_kids } as usize;
                if cn == 0 {
                    return false;
                }
                let leaf = unsafe { *ck.add(cn - 1) };
                let lt = unsafe { (*leaf).text };
                let ll = unsafe { (*leaf).text_len };
                /* `ptr::null[_mut]()` — no args, renders NULL (checked
                 * before the arity-1 wrappers so an empty arg list never
                 * falls into the newtype branches). Any qualification
                 * depth (`ptr::null_mut`, `core::ptr::null_mut`): the
                 * segment before the leaf names the module. */
                if unsafe { z_eq(lt, ll, b"null\0".as_ptr()) }
                    || unsafe { z_eq(lt, ll, b"null_mut\0".as_ptr()) }
                {
                    if cn >= 2 {
                        let wrap = unsafe { *ck.add(cn - 2) };
                        let wt = unsafe { (*wrap).text };
                        let wl = unsafe { (*wrap).text_len };
                        if unsafe { z_eq(wt, wl, b"ptr\0".as_ptr()) } {
                            return true;
                        }
                    }
                }
                if an != 1 {
                    return false;
                }
                /* single-segment callee: a transparent-newtype ctor */
                if cn == 1 && unsafe { self.nt_find(lt, ll) } {
                    return unsafe { self.init_all_zero(*ak.add(0)) };
                }
                if unsafe { z_eq(lt, ll, b"new\0".as_ptr()) } && cn >= 2 {
                    /* `UnsafeCell::new` / `Cell::new`, however deeply
                     * qualified (`core::cell::UnsafeCell::new`): the segment
                     * before `new` names the wrapper, not the path head. */
                    let wrap = unsafe { *ck.add(cn - 2) };
                    let wt = unsafe { (*wrap).text };
                    let wl = unsafe { (*wrap).text_len };
                    if unsafe { z_eq(wt, wl, b"UnsafeCell\0".as_ptr()) }
                        || unsafe { z_eq(wt, wl, b"Cell\0".as_ptr()) }
                    {
                        return unsafe { self.init_all_zero(*ak.add(0)) };
                    }
                }
                false
            }
            _ => false,
        }
    }

    /* Zero-ness of a PATH initializer: `None`, `false`, a zero-valued
     * enum variant (`E::V`, joined `E_V`, or the bare variant name), or
     * a wrapping path around a struct literal (zero-ness is the fields').
     * A PATH wrapping a STRUCT_LIT delegates; any other multi-segment
     * path (a static name, a fn name) is not provably zero. */
    unsafe fn path_is_zero(&mut self, e: *const pm_jit_rsx_ast_t) -> bool {
        let kids = unsafe { (*e).kids };
        let nk = unsafe { (*e).n_kids } as usize;
        if nk == 0 {
            /* bare PATH carries its name in its own text */
            let t = unsafe { (*e).text };
            let tl = unsafe { (*e).text_len };
            if tl == 5 && !t.is_null() && unsafe { z_eq(t, tl, b"false\0".as_ptr()) } {
                return true;
            }
            return false;
        }
        /* struct-lit wrapper: the last kid is the STRUCT_LIT — the
         * zero-ness is the field values' */
        let last = unsafe { *kids.add(nk - 1) };
        if unsafe { (*last).kind } == pm_jit_rsx_ast_kind::STRUCT_LIT {
            return unsafe { self.struct_lit_is_zero(e) };
        }
        /* single segment naming None / a variant */
        if nk == 1 {
            let only = unsafe { *kids.add(0) };
            let t = unsafe { (*only).text };
            let tl = unsafe { (*only).text_len };
            if tl == 4 && !t.is_null() && unsafe { z_eq(t, tl, b"None\0".as_ptr()) } {
                return true;
            }
            /* `false` parses as a single-segment path whose leaf text is
             * the keyword — C `false` is 0 (stdbool) */
            if tl == 5 && !t.is_null() && unsafe { z_eq(t, tl, b"false\0".as_ptr()) } {
                return true;
            }
            if !t.is_null() && tl > 0 {
                let mut found = false;
                let v = unsafe { self.enums.lookup(t, tl, &mut found) };
                if found {
                    return v == 0;
                }
                /* a named const: zero-ness is its recorded value (BODY_NATIVE
                 * and friends are 0 — the consts table already knows it) */
                let mut cfound = false;
                let cv = unsafe { self.consts.lookup(t, tl, &mut cfound) };
                if cfound {
                    return cv == 0;
                }
            }
            return false;
        }
        /* 2-segment `E::V`: zero iff the variant discriminant is 0 */
        let head = unsafe { *kids.add(0) };
        let lastseg = unsafe { *kids.add(nk - 1) };
        if unsafe { (*lastseg).kind } == pm_jit_rsx_ast_kind::PATH
            && unsafe { (*head).kind } == pm_jit_rsx_ast_kind::PATH
        {
            let ht = unsafe { (*head).text };
            let hl = unsafe { (*head).text_len };
            let lt = unsafe { (*lastseg).text };
            let ll = unsafe { (*lastseg).text_len };
            /* joined spelling Enum_Variant — same text the emission
             * produces and the enum table stores */
            let mut joined = self.arena_tmp();
            let mut at = 0usize;
            at = unsafe { bput(joined, 64, at, ht, hl) };
            at = unsafe { bput(joined, 64, at, b"_\0".as_ptr(), 1) };
            at = unsafe { bput(joined, 64, at, lt, ll) };
            unsafe {
                *joined.add(at) = 0;
            }
            let mut found = false;
            let v = unsafe { self.enums.lookup(joined, at, &mut found) };
            if found {
                return v == 0;
            }
        }
        false
    }

    /* Zero-ness of a struct literal: every field's value must be
     * all-zero. Field names (STRUCT_FIELD kids) alternate with values;
     * a fieldless literal `S { }` is all-zero by definition. The wrapping
     * PATH node (parser shape for `S { .. }`) is unwrapped first. */
    unsafe fn struct_lit_is_zero(&mut self, lit: *const pm_jit_rsx_ast_t) -> bool {
        let mut sl: *const pm_jit_rsx_ast_t = lit;
        if unsafe { (*lit).kind } == pm_jit_rsx_ast_kind::PATH {
            let kids = unsafe { (*lit).kids };
            let nk = unsafe { (*lit).n_kids } as usize;
            let mut inner: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
            let mut i = 0usize;
            while i < nk {
                let k = unsafe { *kids.add(i) };
                if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::STRUCT_LIT {
                    inner = k;
                    break;
                }
                i += 1;
            }
            if inner.is_null() {
                return false;
            }
            sl = inner;
        }
        if unsafe { (*sl).kind } != pm_jit_rsx_ast_kind::STRUCT_LIT {
            return false;
        }
        let fk = unsafe { (*sl).kids };
        let fkn = unsafe { (*sl).n_kids } as usize;
        let mut i = 0usize;
        while i + 1 < fkn {
            let fnode = unsafe { *fk.add(i) };
            let v = unsafe { *fk.add(i + 1) };
            if unsafe { (*fnode).kind } == pm_jit_rsx_ast_kind::STRUCT_FIELD {
                /* the `..base` marker is not a field — a copy of an
                 * arbitrary base is never provably zero */
                if unsafe { (*fnode).text_len } == 2
                    && unsafe { z_eq(unsafe { (*fnode).text }, 2, b"..\0".as_ptr()) }
                {
                    return false;
                }
                if !unsafe { self.init_all_zero(v) } {
                    return false;
                }
            }
            i += 2;
        }
        true
    }

    /* Struct literal S { a: 1, b: 2 }. The parser attaches the fields to
     * the wrapping PATH node: kids = segments…, STRUCT_LIT node whose kids
     * are [STRUCT_FIELD(name), value, STRUCT_FIELD(name), value, …].
     * Emits a C99 compound literal `(S){ .a = 1, .b = 2 }`. */
    /* Tuple expression `(a, b, ..)`: a compound literal of the shared
     * rsx_tuple_<sig> struct, one designated field per element. The
     * signature registers from the elements' inferred C types — same
     * table the tuple *type* path fills, so `(x, y)` in a fn returning
     * `(usize, usize)` picks up exactly that typedef. Unit `()` (text
     * "()" — no kids) stays a refusal: C void is not a value. */
    /* Tuple-element (or any initializer-position) render of a `None` /
     * `Some(x)` where the expected C type is known (the tuple slot's
     * element type): struct-Option -> compound literal of rsx_opt_..,
     * pointer-Option -> NULL / x bare. Returns false when v is neither
     * (the caller emits v normally). The expected type MUST be known —
     * an Option element with an unknown expected shape refuses (audit:
     * never assume the pointer-Option collapse). */
    unsafe fn emit_opt_elem(
        &mut self,
        v: *const pm_jit_rsx_ast_t,
        expect: *const u8,
        expect_len: usize,
        locals: *mut LocalTab,
        line: u32,
    ) -> bool {
        if expect_len == 0 || expect.is_null() {
            return false;
        }
        /* bare None? */
        if unsafe { (*v).kind } == pm_jit_rsx_ast_kind::PATH
            && unsafe { (*v).n_kids } as usize == 1
        {
            let only = unsafe { *(*v).kids.add(0) };
            if unsafe { (*only).kind } == pm_jit_rsx_ast_kind::PATH
                && unsafe { z_eq(unsafe { (*only).text }, unsafe { (*only).text_len }, b"None\0".as_ptr()) }
            {
                if expect_len >= 8 && unsafe { z_eq(expect, 8, b"rsx_opt_\0".as_ptr()) } {
                    self.out.putc(b'(');
                    self.out.put(expect, expect_len);
                    self.out.puts(b"){ ._v = {0}, ._has = 0 }\0".as_ptr());
                } else if expect_len > 0 && unsafe { *expect.add(expect_len - 1) } == b'*' {
                    self.out.puts(b"NULL\0".as_ptr());
                } else {
                    unsafe {
                        self.err(
                            b"unsupported: None with a non-Option expected type\0".as_ptr(),
                            line,
                        );
                    }
                }
                return true;
            }
        }
        /* Some(x)? — CALL node callee Some */
        if unsafe { (*v).kind } == pm_jit_rsx_ast_kind::CALL {
            let vck = unsafe { (*v).kids };
            if unsafe { (*v).n_kids } as usize == 2 {
                let callee = unsafe { *vck.add(0) };
                if unsafe { (*callee).kind } == pm_jit_rsx_ast_kind::PATH
                    && unsafe { (*callee).n_kids } as usize == 1
                {
                    let cseg = unsafe { *(*callee).kids.add(0) };
                    if unsafe { z_eq(unsafe { (*cseg).text }, unsafe { (*cseg).text_len }, b"Some\0".as_ptr()) }
                    {
                        let args = unsafe { *vck.add(1) };
                        if unsafe { (*args).n_kids } as usize == 1 {
                            let inner = unsafe { *(*args).kids.add(0) };
                            if expect_len >= 8 && unsafe { z_eq(expect, 8, b"rsx_opt_\0".as_ptr()) } {
                                self.out.putc(b'(');
                                self.out.put(expect, expect_len);
                                self.out.puts(b"){ ._v = \0".as_ptr());
                                unsafe { self.emit_expr(inner, locals) };
                                self.out.puts(b", ._has = 1 }\0".as_ptr());
                            } else if expect_len > 0
                                && unsafe { *expect.add(expect_len - 1) } == b'*'
                            {
                                unsafe { self.emit_expr(inner, locals) };
                            } else {
                                unsafe {
                                    self.err(
                                        b"unsupported: Some with a non-Option expected type\0".as_ptr(),
                                        line,
                                    );
                                }
                            }
                            return true;
                        }
                    }
                }
            }
        }
        false
    }

    unsafe fn emit_tuple_expr(&mut self, e: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        let kids = unsafe { (*e).kids };
        let nk = unsafe { (*e).n_kids } as usize;
        if nk == 0 {
            /* the unit `()` in value position: the Result plane
             * materializes unit payloads as a 1-byte placeholder
             * (rsx_res_._v is uint8_t), so the zero literal IS the unit
             * value — `Ok(())` fills ._v with exactly this. */
            self.out.puts(b"0\0".as_ptr());
            return;
        }
        if nk > TUP_MAXF {
            unsafe {
                self.err(b"unsupported: tuple with more than 4 elements\0".as_ptr(), unsafe { (*e).line });
            }
            return;
        }
        /* The fn's own return type wins when it is a tuple — unsuffixed
         * literals inside would otherwise infer int32_t and mint a second
         * signature (same rustc rule: expected type from context). */
        if self.cur_ret_len >= 10 && unsafe { z_eq(self.cur_ret.as_ptr(), 10, b"rsx_tuple_\0".as_ptr()) } {
            let slot = unsafe { self.tup_find(self.cur_ret.as_ptr(), self.cur_ret_len) };
            if slot < TUP_CAP && self.tup_counts[slot] == nk {
                let basep = self.tup_elems.as_ptr().add(slot * TUP_MAXF);
                let blens = self.tup_lens.as_ptr().add(slot * TUP_MAXF);
                let need = unsafe { Lower::tup_name_need(blens, nk) };
                let tdn = if need == 0 {
                    core::ptr::null_mut()
                } else {
                    unsafe { self.name_tmp(need) }
                };
                if tdn.is_null() {
                    unsafe {
                        self.err(b"internal: tuple typedef name too long\0".as_ptr(), unsafe { (*e).line });
                    }
                    return;
                }
                let tdn_len = unsafe { Lower::tup_typedef_name(basep, blens, nk, tdn, need) };
                if tdn_len == 0 {
                    unsafe {
                        self.err(b"internal: tuple typedef name too long\0".as_ptr(), unsafe { (*e).line });
                    }
                    return;
                }
                self.out.putc(b'(');
                self.out.put(tdn, tdn_len);
                self.out.puts(b"){ \0".as_ptr());
                let mut f2 = 0usize;
                while f2 < nk {
                    if f2 > 0 {
                        self.out.puts(b", \0".as_ptr());
                    }
                    self.out.puts(b"._\0".as_ptr());
                    let d = b'0' + f2 as u8;
                    self.out.putc(d);
                    self.out.puts(b" = \0".as_ptr());
                    /* Option elements (None / Some(x)) render by the
                     * slot's element type — the expected shape */
                    let ea = slot * TUP_MAXF + f2;
                    let eexp = self.tup_elems[ea].as_ptr();
                    let eexpl = self.tup_lens[ea];
                    let v = unsafe { *kids.add(f2) };
                    let rendered = unsafe {
                        self.emit_opt_elem(v, eexp, eexpl, locals, unsafe { (*e).line })
                    };
                    if !rendered {
                        unsafe { self.emit_expr(v, locals) };
                    }
                    f2 += 1;
                }
                self.out.puts(b" }\0".as_ptr());
                return;
            }
        }
        let mut el_bufs: [[u8; 64]; TUP_MAXF] = [[0; 64]; TUP_MAXF];
        let mut el_lens: [usize; TUP_MAXF] = [0; TUP_MAXF];
        let mut f = 0usize;
        while f < nk {
            let kv = unsafe { *kids.add(f) };
            let n = unsafe { self.expr_ctype(kv, el_bufs[f].as_mut_ptr(), 64, locals) };
            if n == 0 || n >= 64 {
                /* name the failing element's AST kind (a NUL-terminated
                 * static string, so its own length is the z_len): a
                 * refusal chain that stops here must point at the
                 * poisoner, not just the tuple line */
                let kb = unsafe { ast_kind_name(unsafe { (*kv).kind }) };
                let mut kbl = 0usize;
                while kbl < 24 && unsafe { *kb.add(kbl) } != 0 {
                    kbl += 1;
                }
                unsafe {
                    self.err_let_name2(
                        b"cannot infer tuple element type - ascribe it\0".as_ptr(),
                        unsafe { (*e).line },
                        kb,
                        kbl,
                        kb,
                    );
                }
                return;
            }
            el_lens[f] = n;
            f += 1;
        }
        let slot = unsafe { self.tup_add(el_bufs.as_ptr(), el_lens.as_ptr(), nk) };
        if slot >= TUP_CAP {
            unsafe {
                self.err(b"internal: too many tuple types\0".as_ptr(), unsafe { (*e).line });
            }
            return;
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
                self.err(b"internal: tuple typedef name too long\0".as_ptr(), unsafe { (*e).line });
            }
            return;
        }
        let tdn_len = unsafe { Lower::tup_typedef_name(base, blens, nk, tdn, need) };
        if tdn_len == 0 {
            unsafe {
                self.err(b"internal: tuple typedef name too long\0".as_ptr(), unsafe { (*e).line });
            }
            return;
        }
        self.out.putc(b'(');
        self.out.put(tdn, tdn_len);
        self.out.puts(b"){ \0".as_ptr());
        let mut f2 = 0usize;
        while f2 < nk {
            if f2 > 0 {
                self.out.puts(b", \0".as_ptr());
            }
            self.out.puts(b"._\0".as_ptr());
            let d = b'0' + f2 as u8;
            self.out.putc(d);
            self.out.puts(b" = \0".as_ptr());
            /* Option elements (None / Some(x)) render by the slot's
             * element type — the expected shape */
            let ea = slot * TUP_MAXF + f2;
            let eexp = self.tup_elems[ea].as_ptr();
            let eexpl = self.tup_lens[ea];
            let v = unsafe { *kids.add(f2) };
            let rendered = unsafe {
                self.emit_opt_elem(v, eexp, eexpl, locals, unsafe { (*e).line })
            };
            if !rendered {
                unsafe { self.emit_expr(v, locals) };
            }
            f2 += 1;
        }
        self.out.puts(b" }\0".as_ptr());
    }

    unsafe fn emit_struct_lit(&mut self, lit: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        let kids = unsafe { (*lit).kids };
        let nk = unsafe { (*lit).n_kids } as usize;
        /* find the STRUCT_LIT node inside a PATH */
        let mut sl: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
        if unsafe { (*lit).kind } == pm_jit_rsx_ast_kind::PATH {
            let mut i = 0usize;
            while i < nk {
                let k = unsafe { *kids.add(i) };
                if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::STRUCT_LIT {
                    sl = k;
                    break;
                }
                i += 1;
            }
        }
        if sl.is_null() && unsafe { (*lit).kind } == pm_jit_rsx_ast_kind::STRUCT_LIT {
            sl = lit;
        }
        if sl.is_null() {
            unsafe {
                self.err(b"bad struct literal\0".as_ptr(), unsafe { (*lit).line });
            }
            return;
        }
        /* type name: the last path segment of the wrapping node — the
         * same rule expr_ctype applies, so `m::S { .. }` and `S { .. }`
         * both resolve to S. */
        let mut tname: *const u8 = b"\0".as_ptr();
        let mut tlen = 0usize;
        if unsafe { (*lit).kind } == pm_jit_rsx_ast_kind::PATH {
            let mut i = nk;
            while i > 0 {
                i -= 1;
                let seg = unsafe { *kids.add(i) };
                if unsafe { (*seg).kind } != pm_jit_rsx_ast_kind::PATH {
                    continue;
                }
                let sl2 = unsafe { (*seg).text_len };
                if sl2 > 0 {
                    /* `Self { .. }` — the enclosing impl's type */
                    if unsafe { z_eq(unsafe { (*seg).text }, sl2, b"Self\0".as_ptr()) }
                        && self.self_ty_len > 0
                    {
                        tname = self.self_ty.as_ptr();
                        tlen = self.self_ty_len;
                    } else {
                        tname = unsafe { (*seg).text };
                        tlen = sl2;
                    }
                    break;
                }
            }
        }
        if tlen == 0 {
            unsafe {
                self.err(b"bad struct literal type\0".as_ptr(), unsafe { (*lit).line });
            }
            return;
        }
        /* fields on sl's kids: alternating STRUCT_FIELD, value */
        let fk = unsafe { (*sl).kids };
        let fkn = unsafe { (*sl).n_kids } as usize;
        /* struct update `..base` present? scan the STRUCT_FIELD nodes for
         * the ".." marker — emission then switches from a compound
         * literal to a copy-then-override statement expression. */
        let mut up_base: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
        {
            let mut j = 0usize;
            while j + 1 < fkn {
                let fnode = unsafe { *fk.add(j) };
                if unsafe { (*fnode).kind } == pm_jit_rsx_ast_kind::STRUCT_FIELD
                    && unsafe { (*fnode).text_len } == 2
                    && unsafe { z_eq(unsafe { (*fnode).text }, 2, b"..\0".as_ptr()) }
                {
                    up_base = unsafe { *fk.add(j + 1) };
                    break;
                }
                j += 2;
            }
        }
        /* struct slot for field-ctype lookups (None needs the field's
         * Option shape to zero-init) */
        let ss = unsafe { (*self.syms).find(tname, tlen) };
        if !up_base.is_null() {
            /* ({ T _u = base; _u.f = v; ...; _u; }) — the base is copied
             * whole, then the explicit fields override. */
            let uvar = self.arena_tmp();
            let mut ul = unsafe { bput(uvar, 160, 0, b"__rsx_u\0".as_ptr(), 7) };
            let mut cnt = self.atom_tmp_n;
            self.atom_tmp_n += 1;
            let mut digs: [u8; 10] = [0; 10];
            let mut nd = 0usize;
            if cnt == 0 {
                digs[0] = b'0';
                nd = 1;
            } else {
                while cnt > 0 && nd < 10 {
                    digs[nd] = b'0' + (cnt % 10) as u8;
                    cnt /= 10;
                    nd += 1;
                }
            }
            let mut q = nd;
            while q > 0 {
                q -= 1;
                ul = unsafe { bput(uvar, 160, ul, &digs[q], 1) };
            }
            unsafe {
                *uvar.add(ul) = 0;
            }
            self.out.puts(b"({ \0".as_ptr());
            self.out.put(tname, tlen);
            self.out.putc(b' ');
            self.out.put(uvar, ul);
            self.out.puts(b" = \0".as_ptr());
            unsafe { self.emit_expr(up_base, locals) };
            self.out.puts(b"; \0".as_ptr());
            let mut i = 0usize;
            while i + 1 < fkn {
                let fnode = unsafe { *fk.add(i) };
                let v = unsafe { *fk.add(i + 1) };
                if unsafe { (*fnode).kind } == pm_jit_rsx_ast_kind::STRUCT_FIELD
                    && !(unsafe { (*fnode).text_len } == 2
                        && unsafe { z_eq(unsafe { (*fnode).text }, 2, b"..\0".as_ptr()) })
                {
                    self.out.put(uvar, ul);
                    self.out.puts(b".\0".as_ptr());
                    self.out.put(unsafe { (*fnode).text }, unsafe { (*fnode).text_len });
                    self.out.puts(b" = \0".as_ptr());
                    unsafe { self.emit_expr(v, locals) };
                    self.out.puts(b"; \0".as_ptr());
                }
                i += 2;
            }
            self.out.put(uvar, ul);
            self.out.puts(b"; })\0".as_ptr());
            return;
        }
        self.out.puts(b"(\0".as_ptr());
        self.out.put(tname, tlen);
        self.out.puts(b"){ \0".as_ptr());
        let mut i = 0usize;
        let mut first = true;
        while i + 1 < fkn {
            let fnode = unsafe { *fk.add(i) };
            let v = unsafe { *fk.add(i + 1) };
            if unsafe { (*fnode).kind } == pm_jit_rsx_ast_kind::STRUCT_FIELD {
                if !first {
                    self.out.puts(b", \0".as_ptr());
                }
                first = false;
                self.out.puts(b".\0".as_ptr());
                self.out.put(unsafe { (*fnode).text }, unsafe { (*fnode).text_len });
                self.out.puts(b" = \0".as_ptr());
                /* `None` needs the field's type: struct-Option zeroes the
                 * tagged pair, pointer-Option is NULL */
                let fname = unsafe { (*fnode).text };
                let flen = unsafe { (*fnode).text_len };
                let mut done = false;
                if unsafe { (*v).kind } == pm_jit_rsx_ast_kind::PATH
                    && unsafe { (*v).n_kids == 1 }
                {
                    let only = unsafe { *(*v).kids.add(0) };
                    if unsafe { (*only).kind } == pm_jit_rsx_ast_kind::PATH
                        && unsafe { z_eq(unsafe { (*only).text }, unsafe { (*only).text_len }, b"None\0".as_ptr()) }
                    {
                        let ct = self.arena_tmp();
                        let cl = unsafe {
                            (*self.syms).field_ctype(ss, fname, flen, ct)
                        };
                        if cl > 0 {
                            if cl >= 8
                                && unsafe { z_eq(ct, 8, b"rsx_opt_\0".as_ptr()) }
                            {
                                /* struct-shaped Option: the compound
                                 * literal needs its typedef name — a bare
                                 * braced init is not a C initializer */
                                self.out.putc(b'(');
                                self.out.put(ct, cl);
                                self.out.puts(b"){ ._v = {0}, ._has = 0 }\0".as_ptr());
                            } else {
                                self.out.puts(b"NULL\0".as_ptr());
                            }
                            done = true;
                        }
                    }
                }
                if !done {
                    unsafe { self.emit_expr(v, locals) };
                }
            }
            i += 2;
        }
        self.out.puts(b" }\0".as_ptr());
    }

    /* Constant-fold an expression: int literals, named consts, + - * /
     * << >> on those. Returns 0 and *found=false when not foldable. */
    unsafe fn const_eval(&mut self, e: *const pm_jit_rsx_ast_t, found: *mut bool) -> u64 {
        unsafe {
            *found = false;
        }
        if e.is_null() {
            return 0;
        }
        let kind = unsafe { (*e).kind };
        if kind == pm_jit_rsx_ast_kind::LITERAL {
            let t = unsafe { (*e).text };
            let n = unsafe { (*e).text_len };
            if t.is_null() || n == 0 {
                return 0;
            }
            let mut v: u64 = 0;
            let mut at = 0usize;
            let hex = n > 2 && unsafe { *t } == b'0'
                && (unsafe { *t.add(1) } == b'x' || unsafe { *t.add(1) } == b'X');
            if hex {
                at = 2;
            }
            while at < n {
                let c = unsafe { *t.add(at) };
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
                        return 0;
                    };
                    v = v.wrapping_mul(16).wrapping_add(d);
                } else {
                    if c < b'0' || c > b'9' {
                        return 0;
                    }
                    v = v.wrapping_mul(10).wrapping_add((c - b'0') as u64);
                }
                at += 1;
            }
            if at == 0 {
                return 0;
            }
            unsafe {
                *found = true;
            }
            return v;
        }
        if kind == pm_jit_rsx_ast_kind::PATH {
            /* single-segment name only (consts are file-local) */
            let kids = unsafe { (*e).kids };
            let nk = unsafe { (*e).n_kids } as usize;
            if nk == 1 {
                let seg = unsafe { *kids.add(0) };
                if unsafe { (*seg).kind } == pm_jit_rsx_ast_kind::PATH {
                    return unsafe { self.consts.lookup(unsafe { (*seg).text }, unsafe { (*seg).text_len }, found) };
                }
            }
            return 0;
        }
        if kind == pm_jit_rsx_ast_kind::BINARY {
            let kids = unsafe { (*e).kids };
            if unsafe { (*e).n_kids } < 2 {
                return 0;
            }
            let mut f1 = false;
            let mut f2 = false;
            let a = unsafe { self.const_eval(*kids.add(0), &mut f1) };
            let b = unsafe { self.const_eval(*kids.add(1), &mut f2) };
            if !f1 || !f2 {
                return 0;
            }
            let op = unsafe { (*e).text };
            let ol = unsafe { (*e).text_len };
            let r: u64 = if unsafe { z_eq(op, ol, b"+\0".as_ptr()) } {
                a.wrapping_add(b)
            } else if unsafe { z_eq(op, ol, b"-\0".as_ptr()) } {
                a.wrapping_sub(b)
            } else if unsafe { z_eq(op, ol, b"*\0".as_ptr()) } {
                a.wrapping_mul(b)
            } else if unsafe { z_eq(op, ol, b"/\0".as_ptr()) } {
                if b == 0 {
                    return 0;
                }
                a / b
            } else if unsafe { z_eq(op, ol, b"<<\0".as_ptr()) } {
                if b >= 64 {
                    return 0;
                }
                a.wrapping_shl(b as u32)
            } else if unsafe { z_eq(op, ol, b">>\0".as_ptr()) } {
                if b >= 64 {
                    return 0;
                }
                a >> b
            } else {
                return 0;
            };
            unsafe {
                *found = true;
            }
            return r;
        }
        0
    }

    unsafe fn emit_expr(&mut self, e: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        if e.is_null() || !self.ok {
            return;
        }
        let kind = unsafe { (*e).kind };
        match kind {
            pm_jit_rsx_ast_kind::LITERAL => {
                unsafe { self.emit_literal(unsafe { (*e).text }, unsafe { (*e).text_len }) };
            }
            pm_jit_rsx_ast_kind::PATH => unsafe { self.emit_path_expr(e, locals) },
            pm_jit_rsx_ast_kind::EXPR_STMT => {
                let kids = unsafe { (*e).kids };
                if unsafe { (*e).n_kids } >= 1 {
                    unsafe { self.emit_expr(*kids.add(0), locals) };
                }
            }
            pm_jit_rsx_ast_kind::PAREN => {
                self.out.putc(b'(');
                let kids = unsafe { (*e).kids };
                if unsafe { (*e).n_kids } >= 1 {
                    unsafe { self.emit_expr(*kids.add(0), locals) };
                }
                self.out.putc(b')');
            }
            pm_jit_rsx_ast_kind::BINARY => unsafe { self.emit_binary(e, locals) },
            pm_jit_rsx_ast_kind::UNARY => unsafe { self.emit_unary(e, locals) },
            pm_jit_rsx_ast_kind::CAST => {
                let kids = unsafe { (*e).kids };
                self.out.putc(b'(');
                if unsafe { (*e).n_kids } >= 2 {
                    let ty = unsafe { *kids.add(1) };
                    let ct = self.arena_tmp();
                    let n = unsafe { self.ctype(ty, ct, 128) };
                    if n == 0 {
                        return;
                    }
                    self.out.put(ct, n);
                    self.out.puts(b")(\0".as_ptr());
                    unsafe { self.emit_expr(*kids.add(0), locals) };
                    self.out.putc(b')');
                }
            }
            pm_jit_rsx_ast_kind::ASSIGN => unsafe { self.emit_assign(e, locals) },
            pm_jit_rsx_ast_kind::CALL => unsafe { self.emit_call(e, locals) },
            pm_jit_rsx_ast_kind::METHOD_CALL => unsafe { self.emit_method_call(e, locals) },
            pm_jit_rsx_ast_kind::FIELD => unsafe { self.emit_field(e, locals) },
            pm_jit_rsx_ast_kind::INDEX => {
                let kids = unsafe { (*e).kids };
                self.out.putc(b'(');
                if unsafe { (*e).n_kids } >= 2 {
                    let idx: *const pm_jit_rsx_ast_t = unsafe { *kids.add(1) };
                    /* range index `a[lo..hi]` / `a[..hi]` / `a[lo..]`: the
                     * slice stays a pointer to the start element — `a+lo`.
                     * The length side is not carried (rsx slices are ptr+len
                     * pairs by convention, same as `&[T]`). */
                    let is_range = unsafe { rsx_idx_is_range(e) };
                    if is_range {
                        let ik = unsafe { (*idx).kids };
                        let lo = unsafe { *ik.add(0) };
                        let lo_empty = unsafe { (*lo).kind } == pm_jit_rsx_ast_kind::TUPLE
                            && unsafe { (*lo).n_kids } == 0;
                        let rnk = unsafe { (*idx).n_kids } as usize;
                        let hi = if rnk >= 2 { unsafe { *ik.add(1) } } else { core::ptr::null_mut() };
                        let hi_empty = hi.is_null()
                            || (unsafe { (*hi).kind } == pm_jit_rsx_ast_kind::TUPLE
                                && unsafe { (*hi).n_kids } == 0);
                        /* fat bases (&str, &[T]): the sub-slice is itself a
                         * fat value — { base.p + lo, len } — so later
                         * .trim()/.len()/String conversions see the true
                         * window. Plain bases keep the pointer convention. */
                        let bct2 = self.arena_tmp();
                        let bn2 = unsafe { self.expr_ctype(*kids.add(0), bct2, 128, locals) };
                        let base_is_arr = bn2 > 8 && bn2 < 128 && unsafe { z_eq(bct2, 8, b"rsx_arr_\0".as_ptr()) };
                        let base_is_strref = bn2 == 13 && unsafe { z_eq(bct2, 13, b"rsx_str_ref_t\0".as_ptr()) };
                        if base_is_arr || base_is_strref {
                            self.out.puts(b"({ \0".as_ptr());
                            self.out.put(bct2, bn2);
                            self.out.puts(b" _s; _s.p = (\0".as_ptr());
                            unsafe { self.emit_expr(*kids.add(0), locals) };
                            self.out.puts(b").p\0".as_ptr());
                            if !lo_empty {
                                self.out.puts(b" + \0".as_ptr());
                                unsafe { self.emit_expr(lo, locals) };
                            }
                            self.out.puts(b"; _s.n = \0".as_ptr());
                            if !hi_empty {
                                unsafe { self.emit_expr(hi, locals) };
                                if !lo_empty {
                                    self.out.puts(b" - \0".as_ptr());
                                    unsafe { self.emit_expr(lo, locals) };
                                }
                            } else {
                                self.out.puts(b"(\0".as_ptr());
                                unsafe { self.emit_expr(*kids.add(0), locals) };
                                self.out.puts(b").n\0".as_ptr());
                                if !lo_empty {
                                    self.out.puts(b" - \0".as_ptr());
                                    unsafe { self.emit_expr(lo, locals) };
                                }
                            }
                            self.out.puts(b"; _s; })\0".as_ptr());
                        } else {
                            unsafe { self.emit_expr(*kids.add(0), locals) };
                            if !lo_empty {
                                self.out.puts(b" + \0".as_ptr());
                                unsafe { self.emit_expr(lo, locals) };
                            }
                        }
                    } else {
                        /* Vec base: the container indexes its heap slab —
                         * `v.p[i]`. Gate on the rendered type (an interned
                         * rsx_vec_<elem> typedef), not on the base's name. */
                        let vct = self.arena_tmp();
                        let vn = unsafe { self.expr_ctype(*kids.add(0), vct, 128, locals) };
                        if vn > 8 && vn < 128 && unsafe { z_eq(vct, 8, b"rsx_vec_\0".as_ptr()) } {
                            self.out.puts(b"(\0".as_ptr());
                            unsafe { self.emit_expr(*kids.add(0), locals) };
                            self.out.puts(b").p[\0".as_ptr());
                            unsafe { self.emit_expr(idx, locals) };
                            self.out.puts(b"]\0".as_ptr());
                        } else if vn > 8 && vn < 128 && unsafe { z_eq(vct, 8, b"rsx_arr_\0".as_ptr()) } {
                            /* &[T] slice base: the fat pair's .p */
                            self.out.puts(b"(\0".as_ptr());
                            unsafe { self.emit_expr(*kids.add(0), locals) };
                            self.out.puts(b").p[\0".as_ptr());
                            unsafe { self.emit_expr(idx, locals) };
                            self.out.puts(b"]\0".as_ptr());
                        } else {
                        /* pointer-to-array base (`&mut [T; N]` place): C
                         * `arr[i]` would index the pointer (stride = whole
                         * array) — deref to the array first: `(*arr)[i]`. */
                        let b_buf = self.arena_tmp();
                        let bn = unsafe {
                            self.expr_ctype(*kids.add(0), b_buf, 128, locals)
                        };
                        let mut is_parr = false;
                        if bn >= 3 {
                            let mut i = 0usize;
                            while i + 3 <= bn {
                                if unsafe { *b_buf.add(i) } == b'('
                                    && unsafe { *b_buf.add(i + 1) } == b'*'
                                    && unsafe { *b_buf.add(i + 2) } == b')'
                                {
                                    is_parr = true;
                                    break;
                                }
                                i += 1;
                            }
                        }
                        if is_parr {
                            self.out.puts(b"(*\0".as_ptr());
                            unsafe { self.emit_expr(*kids.add(0), locals) };
                            self.out.puts(b")\0".as_ptr());
                        } else {
                            unsafe { self.emit_expr(*kids.add(0), locals) };
                        }
                        self.out.putc(b'[');
                        unsafe { self.emit_expr(idx, locals) };
                        self.out.putc(b']');
                        }
                    }
                }
                self.out.putc(b')');
            }
            pm_jit_rsx_ast_kind::ARRAY => {
                /* `[a, b]` -> compound initializer; `[e; n]` -> `{e, e, ..}`.
                 * Repeat counts: integer literal or a named const. */
                let kids = unsafe { (*e).kids };
                let nk = unsafe { (*e).n_kids };
                let is_repeat = unsafe { z_eq((*e).text, (*e).text_len, b"[;]\0".as_ptr()) };
                self.out.putc(b'{');
                if is_repeat && nk == 2 {
                    let mut found = false;
                    let n = unsafe { self.const_eval(*kids.add(1), &mut found) };
                    if !found || n == 0 || n > 65536 {
                        unsafe {
                            self.err(b"unsupported: array repeat count must be a constant expression\0".as_ptr(), unsafe { (*e).line });
                        }
                        return;
                    }
                    let mut j: u64 = 0;
                    while j < n {
                        if j > 0 {
                            self.out.putc(b',');
                        }
                        unsafe { self.emit_expr(*kids.add(0), locals) };
                        j += 1;
                    }
                } else {
                    let mut i: u32 = 0;
                    while i < nk {
                        if i > 0 {
                            self.out.putc(b',');
                        }
                        unsafe { self.emit_expr(*kids.add(i as usize), locals) };
                        i += 1;
                    }
                }
                self.out.putc(b'}');
            }
            pm_jit_rsx_ast_kind::TUPLE => unsafe { self.emit_tuple_expr(e, locals) },
            pm_jit_rsx_ast_kind::STRUCT_LIT => unsafe { self.emit_struct_lit(e, locals) },
            pm_jit_rsx_ast_kind::CLOSURE => unsafe {
                self.err(b"unsupported: closure\0".as_ptr(), unsafe { (*e).line });
            },
            pm_jit_rsx_ast_kind::MACRO => unsafe {
                /* vec![..] — emit the container ctor. The typing pass
                 * (expr_ctype) already interned the row from the same
                 * scan; re-scan here and emit either the repeat form
                 * (calloc for zero elems, else a fill loop) or the list
                 * form (malloc + per-element assignment). */
                let mut row: usize = 0;
                let mut count: *mut pm_jit_rsx_ast_t = core::ptr::null_mut();
                let mut elems: [*mut pm_jit_rsx_ast_t; 16] = [core::ptr::null_mut(); 16];
                let mut list_n: usize = 0;
                if !self.vec_macro_scan(
                    (*e).text,
                    (*e).text_len,
                    locals,
                    &mut row,
                    &mut count,
                    elems.as_mut_ptr(),
                    &mut list_n,
                ) {
                    /* not a vec! — the write!/writeln! io macro is next */
                    if unsafe { self.emit_write_macro(e, locals) } {
                        return;
                    }
                    /* the format! interpolation macro is
                     * the other expression macro the plane lowers */
                    if unsafe { self.emit_format_value(e, locals) } {
                        return;
                    }
                    self.err(b"unsupported: expression macro\0".as_ptr(), unsafe { (*e).line });
                    return;
                }
                if !self.ok {
                    return;
                }
                let tdn = self.arena_tmp();
                let tdn_len = unsafe { VecTab::name_for(row, tdn, 96) };
                if tdn_len == 0 {
                    unsafe {
                        self.err(b"internal: vec typedef name too long\0".as_ptr(), unsafe { (*e).line });
                    }
                    return;
                }
                /* the ELEMENT type — the row's interned spelling (the
                 * p pointer's pointee, the malloc/calloc unit) */
                let eln = self.vecs.elem_lens[row];
                if eln == 0 {
                    unsafe {
                        self.err(b"internal: vec row has no element type\0".as_ptr(), unsafe { (*e).line });
                    }
                    return;
                }
                unsafe {
                    self.out.puts(b"({ \0".as_ptr());
                    self.out.put(tdn, tdn_len);
                    self.out.puts(b" _r; _r.p = (\0".as_ptr());
                    self.out.put(self.vecs.elems[row].as_ptr(), eln);
                    self.out.puts(b"*)\0".as_ptr());
                    if list_n > 0 {
                        /* list form: malloc n * sizeof(elem) */
                        self.out.puts(b"malloc(\0".as_ptr());
                        self.out.put_u32(list_n as u32);
                        self.out.puts(b" * sizeof(\0".as_ptr());
                        self.out.put(self.vecs.elems[row].as_ptr(), eln);
                        self.out.puts(b"))\0".as_ptr());
                    } else {
                        /* repeat form: calloc(n, sizeof(elem)) */
                        self.out.puts(b"calloc((size_t)(\0".as_ptr());
                        self.emit_expr(count, locals);
                        self.out.puts(b"), sizeof(\0".as_ptr());
                        self.out.put(self.vecs.elems[row].as_ptr(), eln);
                        self.out.puts(b"))\0".as_ptr());
                    }
                    self.out.puts(b"; _r.n = _r.cap = \0".as_ptr());
                    if list_n > 0 {
                        self.out.put_u32(list_n as u32);
                    } else {
                        self.out.puts(b"(size_t)(\0".as_ptr());
                        self.emit_expr(count, locals);
                        self.out.puts(b")\0".as_ptr());
                    }
                    self.out.puts(b";\0".as_ptr());
                    /* list form: fill */
                    let mut i = 0usize;
                    while i < list_n {
                        self.out.puts(b" _r.p[\0".as_ptr());
                        self.out.put_u32(i as u32);
                        self.out.puts(b"] = \0".as_ptr());
                        self.emit_expr(elems[i], locals);
                        self.out.puts(b";\0".as_ptr());
                        i += 1;
                    }
                    /* repeat form with a non-zero elem: fill loop */
                    if list_n == 0 && !count.is_null() {
                        let eb = self.arena_tmp();
                        let en = unsafe { self.expr_ctype(elems[0], eb, 128, locals) };
                        if en == 0 {
                            return;
                        }
                        /* zero-literal elems keep calloc's zeros (vec![0..]) */
                        let el = unsafe { (*elems[0]).text_len };
                        let et = unsafe { (*elems[0]).text };
                        let is_zero_lit = el > 0
                            && unsafe { *et == b'0' }
                            && (el == 1 || unsafe { *et.add(1) == b'u' } || unsafe { *et.add(1) == b'i' });
                        if !is_zero_lit {
                            self.out.puts(b" for (size_t _i = 0; _i < _r.n; _i++) _r.p[_i] = \0".as_ptr());
                            self.emit_expr(elems[0], locals);
                            self.out.puts(b";\0".as_ptr());
                        }
                    }
                    self.out.puts(b" _r; })\0".as_ptr());
                }
            },
            pm_jit_rsx_ast_kind::IF => unsafe {
                /* value-position if: a two-branch value if lowers to the
                 * ternary `(cond ? tv : ev)`; branches that carry statements
                 * need a temp (`emit_if_value`) and refuse here. */
                let kids = unsafe { (*e).kids };
                let nk = unsafe { (*e).n_kids } as usize;
                if nk >= 3 {
                    let tv = self.block_tail_node(unsafe { *kids.add(1) });
                    let ev = self.block_tail_node(unsafe { *kids.add(2) });
                    self.out.putc(b'(');
                    unsafe { self.emit_expr(*kids.add(0), locals) };
                    self.out.puts(b" ? \0".as_ptr());
                    unsafe { self.emit_expr(tv, locals) };
                    self.out.puts(b" : \0".as_ptr());
                    unsafe { self.emit_expr(ev, locals) };
                    self.out.putc(b')');
                    return;
                }
                self.err(b"unsupported: value-position if without ascription\0".as_ptr(), unsafe { (*e).line });
            },
            pm_jit_rsx_ast_kind::MATCH => unsafe {
                self.err(b"unsupported: value-position match without ascription\0".as_ptr(), unsafe { (*e).line });
            },
            pm_jit_rsx_ast_kind::BLOCK => {
                /* value-position block (incl. `unsafe { .. }`): its tail
                 * expression, unwrapping EXPR_STMT/BLOCK wrappers. */
                let kids = unsafe { (*e).kids };
                let nk = unsafe { (*e).n_kids } as usize;
                if nk == 0 {
                    return;
                }
                let tail = unsafe { *kids.add(nk - 1) };
                let tk = unsafe { (*tail).kind };
                if tk == pm_jit_rsx_ast_kind::EXPR_STMT && unsafe { (*tail).n_kids } >= 1 {
                    unsafe { self.emit_expr(*(*tail).kids.add(0), locals) };
                    return;
                }
                if tk == pm_jit_rsx_ast_kind::BLOCK || tk == pm_jit_rsx_ast_kind::STMT {
                    unsafe { self.emit_expr(tail, locals) };
                    return;
                }
                unsafe { self.emit_expr(tail, locals) };
            }
            pm_jit_rsx_ast_kind::LOOP => {
                /* `loop { .. }` as a function's tail: statement-form emission. */
                unsafe { self.emit_loop(e, locals) };
            }
            /* statement forms reachable through EXPR_STMT wrappers */
            pm_jit_rsx_ast_kind::RETURN => unsafe { self.emit_return(e, locals) },
            pm_jit_rsx_ast_kind::BREAK => {
                let t = unsafe { (*e).text };
                let tl = unsafe { (*e).text_len };
                let labeled = tl > 0 && !t.is_null() && unsafe { *t == b'\'' };
                if labeled {
                    self.out.puts(b"goto \0".as_ptr());
                    unsafe { self.put_lbl(t, tl, b"_end\0".as_ptr()) };
                    self.out.putc(b';');
                } else {
                    self.out.puts(b"break\0".as_ptr());
                }
            }
            pm_jit_rsx_ast_kind::CONTINUE => {
                let t = unsafe { (*e).text };
                let tl = unsafe { (*e).text_len };
                let labeled = tl > 0 && !t.is_null() && unsafe { *t == b'\'' };
                if labeled {
                    self.out.puts(b"goto \0".as_ptr());
                    unsafe { self.put_lbl(t, tl, b"_cont\0".as_ptr()) };
                    self.out.putc(b';');
                } else {
                    self.out.puts(b"continue\0".as_ptr());
                }
            }
            _ => unsafe {
                self.err(b"unsupported: expression form\0".as_ptr(), unsafe { (*e).line });
            },
        }
    }

    unsafe fn emit_binary(&mut self, e: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        let kids = unsafe { (*e).kids };
        let op = unsafe { (*e).text };
        let op_len = unsafe { (*e).text_len };
        if unsafe { (*e).n_kids } < 2 {
            return;
        }
        /* str view equality: `s == "lit"` (or `!=`, mirrored forms) — the
         * length + memcmp compare, never a C == between the struct and
         * a char* literal. */
        if unsafe { z_eq(op, op_len, b"==\0".as_ptr()) } || unsafe { z_eq(op, op_len, b"!=\0".as_ptr()) } {
            let l = unsafe { *kids.add(0) };
            let r = unsafe { *kids.add(1) };
            let l_lit = unsafe { !l.is_null() } && unsafe { (*l).kind } == pm_jit_rsx_ast_kind::LITERAL && unsafe { (*l).text_len } >= 2 && unsafe { *(*l).text } == b'"';
            let r_lit = unsafe { !r.is_null() } && unsafe { (*r).kind } == pm_jit_rsx_ast_kind::LITERAL && unsafe { (*r).text_len } >= 2 && unsafe { *(*r).text } == b'"';
            if l_lit || r_lit {
                let other = if l_lit { r } else { l };
                let ctb = self.arena_tmp();
                let n2 = unsafe { self.expr_ctype(other, ctb, 128, locals) };
                /* the fat view OR the owned String — both carry the
                 * same .p/.n members, so the one memcmp shape serves
                 * either side of the compare. */
                if (n2 == 13 && unsafe { z_eq(ctb, 13, b"rsx_str_ref_t\0".as_ptr()) })
                    || (n2 == 9 && unsafe { z_eq(ctb, 9, b"rsx_str_t\0".as_ptr()) })
                {
                    let lit = if l_lit { l } else { r };
                    let lit_t = unsafe { (*lit).text };
                    let lit_tl = unsafe { (*lit).text_len };
                    let clen = unsafe { Lower::c_str_len(lit_t.add(1), lit_tl - 2) };
                    let lit_inner = lit_t.wrapping_add(1);
                    let eq_ = unsafe { z_eq(op, op_len, b"==\0".as_ptr()) };
                    if !eq_ {
                        self.out.puts(b"(!\0".as_ptr());
                    }
                    self.out.puts(b"((\0".as_ptr());
                    unsafe { self.emit_expr(other, locals) };
                    self.out.puts(b").n == \0".as_ptr());
                    self.out.put_u32(clen);
                    self.out.puts(b" && !memcmp((\0".as_ptr());
                    unsafe { self.emit_expr(other, locals) };
                    self.out.puts(b").p, \"\0".as_ptr());
                    self.out.put(lit_inner, lit_tl.wrapping_sub(2));
                    self.out.puts(b"\", \0".as_ptr());
                    self.out.put_u32(clen);
                    self.out.puts(b"))\0".as_ptr());
                    if !eq_ {
                        self.out.putc(b')');
                    }
                    return;
                }
            }
        }
        /* Rust `a << b` on a pointer is not pointer math — plain C. */
        self.out.putc(b'(');
        unsafe { self.emit_expr(*kids.add(0), locals) };
        self.out.putc(b' ');
        self.out.put(op, op_len);
        self.out.putc(b' ');
        unsafe { self.emit_expr(*kids.add(1), locals) };
        self.out.putc(b')');
    }

    unsafe fn emit_unary(&mut self, e: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        let kids = unsafe { (*e).kids };
        let op = unsafe { (*e).text };
        let op_len = unsafe { (*e).text_len };
        if unsafe { (*e).n_kids } < 1 {
            return;
        }
        if unsafe { z_eq(op, op_len, b"-\0".as_ptr()) } {
            self.out.puts(b"(-\0".as_ptr());
            unsafe { self.emit_expr(*kids.add(0), locals) };
            self.out.putc(b')');
            return;
        }
        if unsafe { z_eq(op, op_len, b"!\0".as_ptr()) } {
            self.out.puts(b"(!\0".as_ptr());
            unsafe { self.emit_expr(*kids.add(0), locals) };
            self.out.putc(b')');
            return;
        }
        if unsafe { z_eq(op, op_len, b"*\0".as_ptr()) } {
            /* Deref of a non-pointer C type is identity: a transparent
             * newtype static (`Mut<usize>`) IS the value lvalue in C, so
             * Rust's `*x.get()` must not emit a C deref. */
            let b_buf = self.arena_tmp();
            let bn = unsafe { self.expr_ctype(*kids.add(0), b_buf, 128, locals) };
            if bn > 0 {
                let mut j = bn;
                while j > 0 && unsafe { *b_buf.add(j - 1) } == b' ' {
                    j -= 1;
                }
                if j == 0 || unsafe { *b_buf.add(j - 1) } != b'*' {
                    /* pointer-to-array operand (`T (*)[N]`): emit the real C
                     * deref — the identity drop would make `*(tbl + f)` into
                     * `tbl + f`, silently indexing whole arrays. */
                    let pp = unsafe { self.parr_declarator(b_buf, bn) };
                    if pp != usize::MAX {
                        self.out.puts(b"(*\0".as_ptr());
                        unsafe { self.emit_expr(*kids.add(0), locals) };
                        self.out.putc(b')');
                        return;
                    }
                    unsafe { self.emit_expr(*kids.add(0), locals) };
                    return;
                }
            }
            self.out.puts(b"(*\0".as_ptr());
            unsafe { self.emit_expr(*kids.add(0), locals) };
            self.out.putc(b')');
            return;
        }
        if unsafe { z_eq(op, op_len, b"&\0".as_ptr()) }
            || unsafe { z_eq(op, op_len, b"&mut\0".as_ptr()) }
        {
            /* `&[a, b, …]`: a reference to an array literal — the slice
             * fat pointer as a compound literal of the element's arr row.
             * Static and expression positions share this shape (the SKIP
             * pattern: `const X: &[&str] = &["a", "b"]`). The generic
             * `(&inner)` fall-through would emit `(&{...})` — not C.
             * The element spelling comes from the FIRST element's own
             * ctype — `&str` literals type as rsx_str_ref_t there (the
             * array-literal path types them as bare char*, a mismatch
             * that would mint a second, wrong row). */
            {
                let inner0 = unsafe { *kids.add(0) };
                if unsafe { (*inner0).kind } == pm_jit_rsx_ast_kind::ARRAY {
                    let akids = unsafe { (*inner0).kids };
                    let ank = unsafe { (*inner0).n_kids } as usize;
                    if ank >= 1 {
                        let e0 = unsafe { *akids.add(0) };
                        /* unwrap a borrow on the element: `&lit`/`&mut x`
                         * types through the inner expr */
                        let mut e0p = e0;
                        let mut hops = 0;
                        while hops < 4
                            && unsafe { (*e0p).kind } == pm_jit_rsx_ast_kind::UNARY
                        {
                            let t0 = unsafe { (*e0p).text };
                            let t0l = unsafe { (*e0p).text_len };
                            if (unsafe { z_eq(t0, t0l, b"&\0".as_ptr()) }
                                || unsafe { z_eq(t0, t0l, b"&mut\0".as_ptr()) })
                                && unsafe { (*e0p).n_kids } as usize >= 1
                            {
                                e0p = unsafe { *(*e0p).kids.add(0) };
                            } else {
                                break;
                            }
                            hops += 1;
                        }
                        let eb = self.arena_tmp();
                        let en = unsafe { self.expr_ctype(e0p, eb, 128, locals) };
                        if en > 0 && en < ARR_SIG {
                            let row = unsafe { self.arrs.intern(eb, en) };
                            if row < ARR_CAP {
                                let nb = self.arena_tmp();
                                let nn = unsafe { ArrTab::name_for(row, nb, 96) };
                                if nn > 0 {
                                    self.out.putc(b'(');
                                    self.out.put(nb, nn);
                                    self.out.puts(b"){ \0".as_ptr());
                                    unsafe { self.emit_expr(inner0, locals) };
                                    self.out.puts(b" }\0".as_ptr());
                                    return;
                                }
                            }
                        }
                    }
                }
            }
            /* `&slice[lo..hi]` / `&arr[lo..hi]`: the range index already
             * lowers to the sub-slice pointer — taking its address would be
             * one indirection too many. Emit the index expr itself. */
            let inner = unsafe { *kids.add(0) };
            let is_range_idx = unsafe { rsx_idx_is_range(inner) };
            if is_range_idx {
                unsafe { self.emit_expr(inner, locals) };
                return;
            }
            /* `&v` where v: Vec<T> — the &[T] coercion: the fat pair
             * {v.p, v.n} as a compound literal of the element's arr row
             * (expr_ctype already types this borrow as that row). */
            {
                let vb = self.arena_tmp();
                let vn = unsafe { self.expr_ctype(inner, vb, 128, locals) };
                if vn > 8 && unsafe { z_eq(vb, 8, b"rsx_vec_\0".as_ptr()) } {
                    let vs = unsafe { self.vecs.find_by_name(vb, vn) };
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
                                    self.out.putc(b'(');
                                    self.out.put(nb, nn);
                                    self.out.puts(b"){ (\0".as_ptr());
                                    unsafe { self.emit_expr(inner, locals) };
                                    self.out.puts(b").p, (\0".as_ptr());
                                    unsafe { self.emit_expr(inner, locals) };
                                    self.out.puts(b").n }\0".as_ptr());
                                    return;
                                }
                            }
                        }
                    }
                }
            }
            self.out.puts(b"(&\0".as_ptr());
            unsafe { self.emit_expr(inner, locals) };
            self.out.putc(b')');
            return;
        }
        if unsafe { z_eq(op, op_len, b"?\0".as_ptr()) } {
            /* `expr?` — two shapes, both GNU statement expressions (TCC,
             * the kernel C backend, accepts them), both testing once and
             * early-returning the fn's own None:
             *  - Option-of-pointer: the payload IS the C value; None is 0.
             *    Sound only inside a fn whose own return is that pointer.
             *  - struct-Option (rsx_opt_<elem>): the payload is ._v, the
             *    test is ._has, and the fn's None is the zero struct. The
             *    expression's value is the payload (usize etc.).
             *  - Result (rsx_res_<T>_<E>): the operand and the fn's own
             *    return are the SAME row — test ._ok, on failure return
             *    the fn's Err carrying the operand's ._e, yield ._v. The
             *    Err type travels unchanged (sink.rs's `String` error
             *    crosses apply_one -> apply_faces whole). */
            let is_ptr_ret = self.cur_ret_len > 0 && unsafe { self.cur_ret[self.cur_ret_len - 1] } == b'*';
            let is_opt_ret = self.cur_ret_len >= 8
                && unsafe { z_eq(self.cur_ret.as_ptr(), 8, b"rsx_opt_\0".as_ptr()) };
            let is_res_ret = self.cur_ret_len >= 8
                && unsafe { z_eq(self.cur_ret.as_ptr(), 8, b"rsx_res_\0".as_ptr()) };
            if !is_ptr_ret && !is_opt_ret && !is_res_ret {
                unsafe {
                    self.err(
                        b"unsupported: ? outside a pointer-, Option- or Result-returning fn\0".as_ptr(),
                        unsafe { (*e).line },
                    );
                }
                return;
            }
            let operand = unsafe { *kids.add(0) };
            let ct = self.arena_tmp();
            let ct_len = unsafe { self.expr_ctype(operand, ct, 128, locals) };
            if ct_len == 0 || ct_len >= 128 {
                unsafe {
                    self.err(b"unsupported: ? on an untyped operand\0".as_ptr(), unsafe { (*e).line });
                }
                return;
            }
            if is_res_ret && ct_len >= 8 && unsafe { z_eq(ct, 8, b"rsx_res_\0".as_ptr()) } {
                /* Result chain: same row as the fn's own return — the
                 * early exit must produce the fn's Err, not a zero
                 * struct: { ._v = 0, ._e = try._e, ._ok = 0 }. */
                self.out.puts(b"({ \0".as_ptr());
                self.out.put(ct, ct_len);
                self.out.puts(b" __rsx_try = (\0".as_ptr());
                unsafe { self.emit_expr(operand, locals) };
                self.out.puts(b"); if (!__rsx_try._ok) { return (\0".as_ptr());
                self.out.put(self.cur_ret.as_ptr(), self.cur_ret_len);
                self.out.puts(b"){ ._v = 0, ._e = __rsx_try._e, ._ok = 0 }; } __rsx_try._v; })\0".as_ptr());
                return;
            }
            if is_opt_ret && ct_len >= 8 && unsafe { z_eq(ct, 8, b"rsx_opt_\0".as_ptr()) } {
                /* struct-Option chain: test ._has, return the zero struct,
                 * yield the payload */
                self.out.puts(b"({ \0".as_ptr());
                self.out.put(ct, ct_len);
                self.out.puts(b" __rsx_try = (\0".as_ptr());
                unsafe { self.emit_expr(operand, locals) };
                self.out.puts(b"); if (!__rsx_try._has) { return (\0".as_ptr());
                self.out.put(self.cur_ret.as_ptr(), self.cur_ret_len);
                self.out.puts(b"){0}; } __rsx_try._v; })\0".as_ptr());
                return;
            }
            if unsafe { *ct.add(ct_len - 1) } != b'*' {
                unsafe {
                    self.err(
                        b"unsupported: ? on a non-pointer Option (payload must be a pointer)\0".as_ptr(),
                        unsafe { (*e).line },
                    );
                }
                return;
            }
            self.out.puts(b"({ \0".as_ptr());
            self.out.put(ct, ct_len);
            self.out.puts(b" __rsx_try = (\0".as_ptr());
            unsafe { self.emit_expr(operand, locals) };
            self.out.puts(b"); if (__rsx_try == 0) { return 0; } __rsx_try; })\0".as_ptr());
            return;
        }
        unsafe {
            self.err(b"unsupported: unary operator\0".as_ptr(), unsafe { (*e).line });
        }
    }

    unsafe fn emit_assign(&mut self, e: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        let kids = unsafe { (*e).kids };
        let op = unsafe { (*e).text };
        let op_len = unsafe { (*e).text_len };
        if unsafe { (*e).n_kids } < 2 {
            return;
        }
        let lhs = unsafe { *kids.add(0) };
        let rhs = unsafe { *kids.add(1) };
        let rk = unsafe { (*rhs).kind };
        if rk == pm_jit_rsx_ast_kind::IF || rk == pm_jit_rsx_ast_kind::MATCH {
            /* assign-from-if/match: branch bodies store into the lhs. Only a
             * plain local (PATH) lhs — the branch emitter needs the variable
             * spelling. The PATH node's own text is the marker "path", the
             * name lives in its last segment kid. */
            let mut nm: *const u8 = b"__rsx_v\0".as_ptr();
            let mut nl = 7usize;
            if unsafe { (*lhs).kind } == pm_jit_rsx_ast_kind::PATH {
                let lk = unsafe { (*lhs).kids };
                let ln = unsafe { (*lhs).n_kids } as usize;
                if ln > 0 {
                    nm = unsafe { (**lk.add(ln - 1)).text };
                    nl = unsafe { (**lk.add(ln - 1)).text_len };
                }
            }
            if rk == pm_jit_rsx_ast_kind::IF {
                unsafe { self.emit_if_value(rhs, locals, nm, nl) };
            } else {
                unsafe { self.emit_match_value(rhs, locals, nm, nl) };
            }
            return;
        }
        /* `x.field = None` / `x = None` / `x = Some(v)`: the Option shape
         * decides the C render — struct-Option zeroes/fills the tagged
         * pair, pointer-Option is NULL / the value. The bare `None` PATH
         * and call-shaped `Some(v)` carry no type of their own (cur_ret is
         * only set in tail-value context), so the lhs's ctype drives it. */
        if unsafe { (*rhs).kind } == pm_jit_rsx_ast_kind::PATH
            && unsafe { (*rhs).n_kids } == 1
        {
            let only = unsafe { *(*rhs).kids.add(0) };
            let is_none = unsafe { (*only).kind } == pm_jit_rsx_ast_kind::PATH
                && unsafe {
                    z_eq(unsafe { (*only).text }, unsafe { (*only).text_len }, b"None\0".as_ptr())
                };
            /* Some(v): CALL node under the single PATH kid */
            let mut some_v: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
            if !is_none && unsafe { (*only).kind } == pm_jit_rsx_ast_kind::CALL {
                let ck = unsafe { (*only).kids };
                let cn = unsafe { (*only).n_kids } as usize;
                if cn == 2 {
                    let callee = unsafe { *ck.add(0) };
                    if unsafe { (*callee).kind } == pm_jit_rsx_ast_kind::PATH
                        && unsafe { (*callee).n_kids == 1 }
                    {
                        let cseg = unsafe { *(*callee).kids.add(0) };
                        if unsafe {
                            z_eq(
                                unsafe { (*cseg).text },
                                unsafe { (*cseg).text_len },
                                b"Some\0".as_ptr(),
                            )
                        } {
                            let av = unsafe { *ck.add(1) };
                            let an = unsafe { (*av).n_kids } as usize;
                            if an == 1 {
                                some_v = unsafe { *(*av).kids.add(0) };
                            }
                        }
                    }
                }
            }
            if is_none || !some_v.is_null() {
                let ct = self.arena_tmp();
                let cl = unsafe { self.expr_ctype(lhs, ct, 128, locals) };
                unsafe { self.emit_expr(lhs, locals) };
                self.out.putc(b' ');
                self.out.put(op, op_len);
                self.out.putc(b' ');
                if cl >= 8 && unsafe { z_eq(ct, 8, b"rsx_opt_\0".as_ptr()) } {
                    /* compound literal: a bare braced list is not a C
                     * assignment RHS, the tagged pair needs its type */
                    self.out.putc(b'(');
                    self.out.put(ct, cl);
                    if is_none {
                        self.out.puts(b"){ ._v = {0}, ._has = 0 }\0".as_ptr());
                    } else {
                        self.out.puts(b"){ ._v = \0".as_ptr());
                        unsafe { self.emit_expr(some_v, locals) };
                        self.out.puts(b", ._has = 1 }\0".as_ptr());
                    }
                } else if is_none {
                    self.out.puts(b"NULL\0".as_ptr());
                } else {
                    unsafe { self.emit_expr(some_v, locals) };
                }
                return;
            }
        }
        /* call-shaped `x = Some(v)` — rhs is a CALL whose callee path is
         * the single-segment `Some` */
        if unsafe { (*rhs).kind } == pm_jit_rsx_ast_kind::CALL {
            let rk = unsafe { (*rhs).kids };
            let rn = unsafe { (*rhs).n_kids } as usize;
            if rn == 2 {
                let callee = unsafe { *rk.add(0) };
                let av = unsafe { *rk.add(1) };
                let mut some_v: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
                if unsafe { (*callee).kind } == pm_jit_rsx_ast_kind::PATH
                    && unsafe { (*callee).n_kids == 1 }
                {
                    let cseg = unsafe { *(*callee).kids.add(0) };
                    if unsafe {
                        z_eq(unsafe { (*cseg).text }, unsafe { (*cseg).text_len }, b"Some\0".as_ptr())
                    } {
                        let an = unsafe { (*av).n_kids } as usize;
                        if an == 1 {
                            some_v = unsafe { *(*av).kids.add(0) };
                        }
                    }
                }
                if !some_v.is_null() {
                    let ct = self.arena_tmp();
                    let cl = unsafe { self.expr_ctype(lhs, ct, 128, locals) };
                    unsafe { self.emit_expr(lhs, locals) };
                    self.out.putc(b' ');
                    self.out.put(op, op_len);
                    self.out.putc(b' ');
                    if cl >= 8 && unsafe { z_eq(ct, 8, b"rsx_opt_\0".as_ptr()) } {
                        self.out.putc(b'(');
                        self.out.put(ct, cl);
                        self.out.puts(b"){ ._v = \0".as_ptr());
                        unsafe { self.emit_expr(some_v, locals) };
                        self.out.puts(b", ._has = 1 }\0".as_ptr());
                    } else {
                        unsafe { self.emit_expr(some_v, locals) };
                    }
                    return;
                }
            }
        }
        /* array-typed `=`: C arrays are not assignable — memcpy the
         * bytes instead (`a = b;` on arrays is a gcc extension; TCC
         * refuses it, and the self-host prove compiles through TCC). */
        if op_len == 1 && unsafe { *op } == b'=' {
            /* `str_ref = "lit"` — the fat-ref compound literal: a match
             * arm (`"void" => "()"`) stores a literal into an
             * rsx_str_ref_t binding; a raw "..." is char[N], not the
             * struct. */
            {
                let lt = self.arena_tmp();
                let ln = unsafe { self.expr_ctype(lhs, lt, 128, locals) };
                if ln == 13 && unsafe { z_eq(lt, 13, b"rsx_str_ref_t\0".as_ptr()) }
                    && unsafe { (*rhs).kind } == pm_jit_rsx_ast_kind::LITERAL
                {
                    let t = unsafe { (*rhs).text };
                    let tl = unsafe { (*rhs).text_len };
                    if tl >= 2 && unsafe { *t } == b'"' {
                        unsafe { self.emit_expr(lhs, locals) };
                        self.out.puts(b" = (rsx_str_ref_t){ (const uint8_t *)\0".as_ptr());
                        self.out.put(t, tl);
                        self.out.puts(b", \0".as_ptr());
                        let lit_inner = t.wrapping_add(1);
                        let clen = unsafe { Lower::c_str_len(lit_inner, tl.wrapping_sub(2)) };
                        self.out.put_u32(clen);
                        self.out.puts(b" }\0".as_ptr());
                        return;
                    }
                }
            }
            let lt = self.arena_tmp();
            let ln = unsafe { self.expr_ctype(lhs, lt, 160, locals) };
            if ln > 0 && unsafe { self.ctype_is_fixed_array(lt, ln) } {
                self.out.puts(b"memcpy(\0".as_ptr());
                self.out.putc(b'&');
                unsafe { self.emit_expr(lhs, locals) };
                self.out.puts(b", \0".as_ptr());
                self.out.putc(b'&');
                unsafe { self.emit_expr(rhs, locals) };
                self.out.puts(b", sizeof(\0".as_ptr());
                unsafe { self.emit_expr(lhs, locals) };
                self.out.puts(b"))\0".as_ptr());
                return;
            }
        }
        unsafe { self.emit_expr(lhs, locals) };
        self.out.putc(b' ');
        self.out.put(op, op_len);
        self.out.putc(b' ');
        unsafe { self.emit_expr(rhs, locals) };
    }

    /* Call: callee path + known core fns; else direct name with args. */
    unsafe fn emit_call(&mut self, e: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        let kids = unsafe { (*e).kids };
        let nk = unsafe { (*e).n_kids } as usize;
        if nk < 2 {
            return;
        }
        let callee = unsafe { *kids.add(0) };
        let args = unsafe { *kids.add(1) };
        if unsafe { (*callee).kind } != pm_jit_rsx_ast_kind::PATH {
            /* Call through any fn-pointer expression — `(hook.f)(x)` or the
             * paren-less `hook.f(x)` field call. A non-PATH callee in call
             * position IS a fn-ptr call: render the callee expression, then
             * the arg list. The callee-side twin of the PATH local-bind
             * arm and of the expr_ctype general fn-ptr arm. */
            self.out.putc(b'(');
            unsafe { self.emit_expr(callee, locals) };
            self.out.putc(b')');
            self.out.putc(b'(');
            let an = unsafe { (*args).n_kids } as usize;
            let ak = unsafe { (*args).kids };
            let mut ai = 0usize;
            while ai < an {
                if ai > 0 {
                    self.out.puts(b", \0".as_ptr());
                }
                unsafe { self.emit_expr(*ak.add(ai), locals) };
                ai += 1;
            }
            self.out.putc(b')');
            return;
        }
        /* Newtype constructor `Mut(x)` / transparent `UnsafeCell::new(x)`:
         * the C value is the argument itself. Registered newtypes take the
         * bare leaf; UnsafeCell/Cell::new unwraps unconditionally (they
         * are transparent by the type map). */
        {
            /* `Box::leak(x)` — the escape hatch to a 'static borrow. The
             * subset's String heap IS arena-owned (the compile arena
             * outlives the value), so leak is the identity: the argument
             * renders as-is and the let's ctype is the argument's. */
            /* `Box::leak(x)` — the escape hatch to a 'static borrow. The
             * subset's String heap IS arena-owned (the compile arena
             * outlives the value), so leak is the identity value-wise;
             * the borrow it hands back is the &str view of the same
             * p/n pair — a str_ref even when the argument is an owned
             * String (the let binding's ctype is rsx_str_ref_t). */
            {
                let ck2 = unsafe { (*callee).kids };
                let cn2 = unsafe { (*callee).n_kids } as usize;
                if cn2 >= 2 {
                    let last = unsafe { *ck2.add(cn2 - 1) };
                    let lt2 = unsafe { (*last).text };
                    let ll2 = unsafe { (*last).text_len };
                    let an2 = unsafe { (*args).n_kids } as usize;
                    if an2 == 1 && unsafe { z_eq(lt2, ll2, b"leak\0".as_ptr()) } {
                        let wrap = unsafe { *ck2.add(cn2 - 2) };
                        let wt2 = unsafe { (*wrap).text };
                        let wl2 = unsafe { (*wrap).text_len };
                        if unsafe { z_eq(wt2, wl2, b"Box\0".as_ptr()) } {
                            /* receiver shape: an owned String takes the
                             * p/n view; a fat-ref passes through. */
                            let arg0 = unsafe { *(*args).kids.add(0) };
                            let rb = self.arena_tmp();
                            let rl = unsafe { self.expr_ctype(arg0, rb, 128, locals) };
                            if rl == 9 && unsafe { z_eq(rb, 9, b"rsx_str_t\0".as_ptr()) } {
                                self.out.puts(b"(rsx_str_ref_t){ \0".as_ptr());
                                unsafe { self.emit_expr(arg0, locals) };
                                self.out.puts(b".p, \0".as_ptr());
                                unsafe { self.emit_expr(arg0, locals) };
                                self.out.puts(b".n }\0".as_ptr());
                            } else {
                                unsafe { self.emit_expr(arg0, locals) };
                            }
                            return;
                        }
                    }
                }
            }
            let ck2 = unsafe { (*callee).kids };
            let cn2 = unsafe { (*callee).n_kids } as usize;
            if cn2 >= 1 {
                let leaf = unsafe { *ck2.add(cn2 - 1) };
                let lname = unsafe { (*leaf).text };
                let llen = unsafe { (*leaf).text_len };
                let an = unsafe { (*args).n_kids } as usize;
                let ak = unsafe { (*args).kids };
                if an == 1
                    && ((unsafe { self.nt_find(lname, llen) })
                        || (cn2 >= 2 && unsafe { z_eq(lname, llen, b"new\0".as_ptr()) }))
                {
                    let mut unwrap = unsafe { self.nt_find(lname, llen) };
                    if !unwrap && cn2 >= 2 {
                        /* segment before `new` names the wrapper */
                        let wrap = unsafe { *ck2.add(cn2 - 2) };
                        let wname = unsafe { (*wrap).text };
                        let wlen = unsafe { (*wrap).text_len };
                        if unsafe { z_eq(wname, wlen, b"UnsafeCell\0".as_ptr()) }
                            || unsafe { z_eq(wname, wlen, b"Cell\0".as_ptr()) }
                        {
                            unwrap = true;
                        }
                    }
                    if unwrap {
                        unsafe { self.emit_expr(*ak.add(0), locals) };
                        return;
                    }
                }
            }
        }
        /* `alloc::boxed::Box::new(v)` — the box IS the pointee pointer: a
         * malloc'd cell carrying v. Statement-expression (the same GNU
         * shape to_string rides): `({ T *p = malloc(sizeof *p); *p = v;
         * p; })`. `Box::from_raw(p)`/`into_raw(b)` — the pointer passes
         * through unchanged (identity). */
        {
            let ck2 = unsafe { (*callee).kids };
            let cn2 = unsafe { (*callee).n_kids } as usize;
            if cn2 >= 2 {
                let last2 = unsafe { *ck2.add(cn2 - 1) };
                let l2t = unsafe { (*last2).text };
                let l2l = unsafe { (*last2).text_len };
                let prev = unsafe { *ck2.add(cn2 - 2) };
                let pt = unsafe { (*prev).text };
                let pl = unsafe { (*prev).text_len };
                if unsafe { z_eq(pt, pl, b"Box\0".as_ptr()) }
                    && unsafe { (*args).n_kids } as usize == 1
                {
                    let arg0 = unsafe { *(*args).kids.add(0) };
                    if unsafe { z_eq(l2t, l2l, b"new\0".as_ptr()) } {
                        let ab = self.arena_tmp();
                        let an = unsafe { self.expr_ctype(arg0, ab, 128, locals) };
                        if an > 0 {
                            self.out.puts(b"({ \0".as_ptr());
                            self.out.put(ab, an);
                            self.out.puts(b" * _b = (\0".as_ptr());
                            self.out.put(ab, an);
                            self.out.puts(b" *)malloc(sizeof(\0".as_ptr());
                            self.out.put(ab, an);
                            self.out.puts(b")); if (_b) { *_b = \0".as_ptr());
                            unsafe { self.emit_expr(arg0, locals) };
                            self.out.puts(b"; } _b; })\0".as_ptr());
                            return;
                        }
                    }
                    if unsafe { z_eq(l2t, l2l, b"from_raw\0".as_ptr()) }
                        || unsafe { z_eq(l2t, l2l, b"into_raw\0".as_ptr()) }
                    {
                        unsafe { self.emit_expr(arg0, locals) };
                        return;
                    }
                }
            }
        }
        /* `core::slice::from_raw_parts(p, n)` — the &[u8] view of a raw
         * buffer: the rsx_arr compound literal (uint8_t row). */
        {
            let ck2 = unsafe { (*callee).kids };
            let cn2 = unsafe { (*callee).n_kids } as usize;
            if cn2 == 3 && unsafe { (*args).n_kids } as usize == 2 {
                let s0 = unsafe { *ck2.add(0) };
                let s1 = unsafe { *ck2.add(1) };
                let s2 = unsafe { *ck2.add(2) };
                if unsafe { z_eq(unsafe { (*s0).text }, unsafe { (*s0).text_len }, b"core\0".as_ptr()) }
                    && unsafe { z_eq(unsafe { (*s1).text }, unsafe { (*s1).text_len }, b"slice\0".as_ptr()) }
                    && unsafe { z_eq(unsafe { (*s2).text }, unsafe { (*s2).text_len }, b"from_raw_parts\0".as_ptr()) }
                {
                    let row = unsafe { self.arrs.intern(b"uint8_t\0".as_ptr(), 7) };
                    if row < ARR_CAP {
                        let nb = self.arena_tmp();
                        let nn = unsafe { ArrTab::name_for(row, nb, 96) };
                        if nn > 0 {
                            let ak = unsafe { (*args).kids };
                            self.out.putc(b'(');
                            self.out.put(nb, nn);
                            self.out.puts(b"){ \0".as_ptr());
                            unsafe { self.emit_expr(*ak.add(0), locals) };
                            self.out.puts(b", \0".as_ptr());
                            unsafe { self.emit_expr(*ak.add(1), locals) };
                            self.out.puts(b" }\0".as_ptr());
                            return;
                        }
                    }
                }
                /* `core::str::from_utf8(bytes)` — the view itself (the
                 * bytes are UTF-8 by the callers' contract); `.ok()` and
                 * the Some-bind take it from here. */
                if unsafe { z_eq(unsafe { (*s0).text }, unsafe { (*s0).text_len }, b"core\0".as_ptr()) }
                    && unsafe { z_eq(unsafe { (*s1).text }, unsafe { (*s1).text_len }, b"str\0".as_ptr()) }
                    && unsafe { z_eq(unsafe { (*s2).text }, unsafe { (*s2).text_len }, b"from_utf8\0".as_ptr()) }
                {
                    let ak = unsafe { (*args).kids };
                    unsafe { self.emit_expr(*ak.add(0), locals) };
                    return;
                }
            }
        }
        /* Enum payload ctor `E::V(x)`: the tagged-union compound literal
         * `(E){ E_V, { x } }`. The joined variant name must have an
         * enumpays row (fieldless variants take the plain-constant path
         * further down: they have no argument list at all). */
        {
            let ck2 = unsafe { (*callee).kids };
            let cn2 = unsafe { (*callee).n_kids } as usize;
            if cn2 >= 2 {
                /* joined Enum_Variant — same spelling the tables hold */
                let mut full = self.arena_tmp();
                let mut at = 0usize;
                let mut ok_join = true;
                let mut ci = 0usize;
                while ci < cn2 {
                    let seg = unsafe { *ck2.add(ci) };
                    if unsafe { (*seg).kind } != pm_jit_rsx_ast_kind::PATH {
                        ok_join = false;
                        break;
                    }
                    if at > 0 {
                        at = unsafe { bput(full, 128, at, b"_\0".as_ptr(), 1) };
                    }
                    at = unsafe { bput(full, 128, at, unsafe { (*seg).text }, unsafe { (*seg).text_len }) };
                    ci += 1;
                }
                if ok_join {
                    unsafe {
                        *full.add(at) = 0;
                    }
                    let pb = self.arena_tmp();
                    let pn = unsafe { self.enumpays.lookup(full, at, pb, 96) };
                    if pn > 0 && unsafe { (*args).n_kids } as usize == 1 {
                        let ak = unsafe { (*args).kids };
                        /* the enum name: all segments before the variant */
                        let ename = self.arena_tmp();
                        let mut eat = 0usize;
                        eat = unsafe {
                            bput(ename, 96, eat, unsafe { (*(*ck2.add(0))).text }, unsafe { (*(*ck2.add(0))).text_len })
                        };
                        unsafe {
                            *ename.add(eat) = 0;
                        }
                        /* the variant's union field: the last segment */
                        let vleaf = unsafe { *ck2.add(cn2 - 1) };
                        let vt = unsafe { (*vleaf).text };
                        let vl = unsafe { (*vleaf).text_len };
                        self.out.putc(b'(');
                        self.out.put(ename, eat);
                        self.out.puts(b"){ ._tag = \0".as_ptr());
                        self.out.put(full, at);
                        self.out.puts(b", ._u = { .\0".as_ptr());
                        self.out.put(vt, vl);
                        self.out.puts(b" = \0".as_ptr());
                        unsafe { self.emit_expr(*ak.add(0), locals) };
                        self.out.puts(b" } }\0".as_ptr());
                        return;
                    }
                }
            }
        }
        /* Some(x) building a struct-shaped Option: a compound literal of
         * the fn's own rsx_opt_<elem> return type. Sound when the current
         * fn returns that shape (return position, or a let with ascription
         * whose inferred type matches). Pointer-Option Some never arrives
         * here — it is the argument itself by the type map. */
        {
            let ck2 = unsafe { (*callee).kids };
            let cn2 = unsafe { (*callee).n_kids } as usize;
            if cn2 == 1 {
                let leaf = unsafe { *ck2.add(0) };
                let lname = unsafe { (*leaf).text };
                let llen = unsafe { (*leaf).text_len };
                let an = unsafe { (*args).n_kids } as usize;
                let ak = unsafe { (*args).kids };
                if an == 1 && unsafe { z_eq(lname, llen, b"Some\0".as_ptr()) } && self.cur_ret_len > 8 {
                    let eb = self.arena_tmp();
                    let eln = unsafe {
                        Lower::opt_typedef_elem(self.cur_ret.as_ptr(), self.cur_ret_len, eb, 160)
                    };
                    if eln > 0 {
                        let tdn = self.arena_tmp();
                        let tdn_len = unsafe { Lower::opt_typedef_name(eb, eln, tdn, 160) };
                        if tdn_len > 0 {
                            self.out.putc(b'(');
                            self.out.put(tdn, tdn_len);
                            self.out.puts(b"){ ._v = \0".as_ptr());
                            unsafe { self.emit_expr(*ak.add(0), locals) };
                            self.out.puts(b", ._has = 1 }\0".as_ptr());
                            return;
                        }
                    }
                }
                /* `Ok(x)` / `Err(e)` building the Result row from the
                 * expected type (cur_ret — the let ascription, the fn's
                 * own return, or the let-else initializer's row). The
                 * compound literal fills BOTH payloads: the unused one
                 * zeroes (never read — _ok gates). `Err(())`/`Ok(())`
                 * unit payloads emit the 0 placeholder directly. */
                if (an == 1 || an == 0)
                    && (unsafe { z_eq(lname, llen, b"Ok\0".as_ptr()) }
                        || unsafe { z_eq(lname, llen, b"Err\0".as_ptr()) })
                    && self.cur_ret_len > 8
                {
                    let is_ok = unsafe { z_eq(lname, llen, b"Ok\0".as_ptr()) };
                    let ob = self.arena_tmp();
                    let eb2 = self.arena_tmp();
                    let on = unsafe {
                        Lower::res_typedef_elem(
                            self.cur_ret.as_ptr(),
                            self.cur_ret_len,
                            ob,
                            96,
                            eb2,
                            96,
                        )
                    };
                    if on > 0 {
                        let mut el2 = 0usize;
                        while el2 < 96 && unsafe { *eb2.add(el2) } != 0 {
                            el2 += 1;
                        }
                        let tdn = self.arena_tmp();
                        let tdn_len = unsafe {
                            Lower::res_typedef_name(ob, on, eb2, el2, tdn, 192)
                        };
                        if tdn_len > 0 {
                            self.out.putc(b'(');
                            self.out.put(tdn, tdn_len);
                            if is_ok {
                                self.out.puts(b"){ ._v = \0".as_ptr());
                                if an == 1 {
                                    unsafe { self.emit_expr(*ak.add(0), locals) };
                                } else {
                                    self.out.putc(b'0');
                                }
                                self.out.puts(b", ._e = 0, ._ok = 1 }\0".as_ptr());
                            } else {
                                self.out.puts(b"){ ._v = 0, ._e = \0".as_ptr());
                                if an == 1 {
                                    unsafe { self.emit_expr(*ak.add(0), locals) };
                                } else {
                                    self.out.putc(b'0');
                                }
                                self.out.puts(b", ._ok = 0 }\0".as_ptr());
                            }
                            return;
                        }
                    }
                }
            }
        }
        /* size_of::<T>() — generic args already skipped by the parser; the
         * callee path is core::mem::size_of. */
        let ck = unsafe { (*callee).kids };
        let cn = unsafe { (*callee).n_kids } as usize;
        /* short `ptr::null[_mut]` — the common spelling after `use core::ptr` */
        if cn == 2 {
            let f = unsafe { *ck.add(0) };
            let l = unsafe { *ck.add(1) };
            let ftext = unsafe { (*f).text };
            let flen = unsafe { (*f).text_len };
            let ltext = unsafe { (*l).text };
            let llen = unsafe { (*l).text_len };
            if unsafe { z_eq(ftext, flen, b"ptr\0".as_ptr()) }
                && (unsafe { z_eq(ltext, llen, b"null_mut\0".as_ptr()) }
                    || unsafe { z_eq(ltext, llen, b"null\0".as_ptr()) })
            {
                self.out.puts(b"NULL\0".as_ptr());
                return;
            }
            /* short `ptr::copy_nonoverlapping(s,d,n)` — same mapping as the
             * fully-qualified `core::ptr::` spelling below */
            if unsafe { z_eq(ftext, flen, b"ptr\0".as_ptr()) }
                && unsafe { z_eq(ltext, llen, b"copy_nonoverlapping\0".as_ptr()) }
            {
                let ak = unsafe { (*args).kids };
                if unsafe { (*args).n_kids } >= 3 {
                    self.out.puts(b"memcpy(\0".as_ptr());
                    unsafe { self.emit_expr(*ak.add(1), locals) };
                    self.out.puts(b", \0".as_ptr());
                    unsafe { self.emit_expr(*ak.add(0), locals) };
                    self.out.puts(b", (\0".as_ptr());
                    unsafe { self.emit_expr(*ak.add(2), locals) };
                    self.out.puts(b") * sizeof(*\0".as_ptr());
                    unsafe { self.emit_expr(*ak.add(0), locals) };
                    self.out.puts(b"))\0".as_ptr());
                    return;
                }
            }
        }
        if cn >= 3 {
            let f = unsafe { *ck.add(0) };
            let ftext = unsafe { (*f).text };
            let flen = unsafe { (*f).text_len };
            let s = unsafe { *ck.add(1) };
            let stext = unsafe { (*s).text };
            let slen = unsafe { (*s).text_len };
            /* Last PATH child; a trailing TYPE child (turbofish) is skipped. */
            let mut li = cn - 1;
            while li > 1 && unsafe { (**ck.add(li)).kind } != pm_jit_rsx_ast_kind::PATH {
                li -= 1;
            }
            let l = unsafe { *ck.add(li) };
            let ltext = unsafe { (*l).text };
            let llen = unsafe { (*l).text_len };
            if unsafe { z_eq(ftext, flen, b"core\0".as_ptr()) } {
                if unsafe { z_eq(stext, slen, b"ptr\0".as_ptr()) }
                    && (unsafe { z_eq(ltext, llen, b"null_mut\0".as_ptr()) }
                        || unsafe { z_eq(ltext, llen, b"null\0".as_ptr()) })
                {
                    /* core::ptr::null_mut() -> NULL; the variable's declared
                     * (or branch-inferred) type carries the pointee. */
                    self.out.puts(b"NULL\0".as_ptr());
                    return;
                }
                if unsafe { z_eq(stext, slen, b"mem\0".as_ptr()) }
                    && unsafe { z_eq(ltext, llen, b"size_of\0".as_ptr()) }
                {
                    /* sizeof(T) — T came in as the path's generic-args TYPE
                     * child (single-segment turbofish). */
                    let gt = unsafe { *ck.add(cn - 1) };
                    if unsafe { (*gt).kind } == pm_jit_rsx_ast_kind::TYPE {
                        self.out.puts(b"(sizeof(\0".as_ptr());
                        let gtt = unsafe { (*gt).text };
                        let gtl = unsafe { (*gt).text_len };
                        self.out.put(gtt, gtl);
                        self.out.puts(b"))\0".as_ptr());
                        return;
                    }
                    unsafe {
                        self.err(b"unsupported: size_of without a type here\0".as_ptr(), unsafe { (*e).line });
                    }
                    return;
                }
                if unsafe { z_eq(stext, slen, b"cmp\0".as_ptr()) }
                    && (unsafe { z_eq(ltext, llen, b"min\0".as_ptr()) }
                        || unsafe { z_eq(ltext, llen, b"max\0".as_ptr()) })
                {
                    /* cmp::min(a, b) / cmp::max(a, b) — the ternary; the
                     * operands are the same type by construction (gen
                     * clamps a len against a len), so the C type of the
                     * whole is the first operand's. */
                    let ak = unsafe { (*args).kids };
                    if unsafe { (*args).n_kids } >= 2 {
                        self.out.puts(b"((\0".as_ptr());
                        unsafe { self.emit_expr(*ak.add(0), locals) };
                        self.out.putc(b')');
                        if unsafe { z_eq(ltext, llen, b"min\0".as_ptr()) } {
                            self.out.puts(b" < (\0".as_ptr());
                        } else {
                            self.out.puts(b" > (\0".as_ptr());
                        }
                        unsafe { self.emit_expr(*ak.add(1), locals) };
                        self.out.puts(b") ? (\0".as_ptr());
                        unsafe { self.emit_expr(*ak.add(0), locals) };
                        self.out.puts(b") : (\0".as_ptr());
                        unsafe { self.emit_expr(*ak.add(1), locals) };
                        self.out.puts(b"))\0".as_ptr());
                        return;
                    }
                }
                if unsafe { z_eq(stext, slen, b"ptr\0".as_ptr()) } {
                    if unsafe { z_eq(ltext, llen, b"copy_nonoverlapping\0".as_ptr()) } {
                        /* copy_nonoverlapping(src, dst, n) -> memcpy(dst, src,
                         * n * sizeof(*src)) — Rust counts elements, memcpy
                         * counts bytes. */
                        let ak = unsafe { (*args).kids };
                        if unsafe { (*args).n_kids } >= 3 {
                            self.out.puts(b"memcpy(\0".as_ptr());
                            unsafe { self.emit_expr(*ak.add(1), locals) };
                            self.out.puts(b", \0".as_ptr());
                            unsafe { self.emit_expr(*ak.add(0), locals) };
                            self.out.puts(b", (\0".as_ptr());
                            unsafe { self.emit_expr(*ak.add(2), locals) };
                            self.out.puts(b") * sizeof(*\0".as_ptr());
                            unsafe { self.emit_expr(*ak.add(0), locals) };
                            self.out.puts(b"))\0".as_ptr());
                            return;
                        }
                    }
                    if unsafe { z_eq(ltext, llen, b"read\0".as_ptr()) } {
                        /* ptr::read(p) -> *p */
                        let ak = unsafe { (*args).kids };
                        if unsafe { (*args).n_kids } >= 1 {
                            self.out.puts(b"(*\0".as_ptr());
                            unsafe { self.emit_expr(*ak.add(0), locals) };
                            self.out.putc(b')');
                            return;
                        }
                    }
                    if unsafe { z_eq(ltext, llen, b"write\0".as_ptr()) } {
                        /* ptr::write(p, v) -> *p = v */
                        let ak = unsafe { (*args).kids };
                        if unsafe { (*args).n_kids } >= 2 {
                            self.out.puts(b"(*\0".as_ptr());
                            unsafe { self.emit_expr(*ak.add(0), locals) };
                            self.out.puts(b") = \0".as_ptr());
                            unsafe { self.emit_expr(*ak.add(1), locals) };
                            return;
                        }
                    }
                }
            }
        }
        /* plain call: last path segment is the fn name */
        if cn >= 1 {
            let leaf = unsafe { *ck.add(cn - 1) };
            if unsafe { (*leaf).kind } == pm_jit_rsx_ast_kind::PATH {
                /* `core::hint::spin_loop()` — no C equivalent needed: the
                 * pause itself is a perf nicety, not a correctness one;
                 * emit a bare `0` statement-expression-neutral value. */
                if cn >= 3 {
                    let h0 = unsafe { *ck.add(0) };
                    let h1 = unsafe { *ck.add(1) };
                    if unsafe { z_eq(unsafe { (*h0).text }, unsafe { (*h0).text_len }, b"core\0".as_ptr()) }
                        && unsafe { z_eq(unsafe { (*h1).text }, unsafe { (*h1).text_len }, b"hint\0".as_ptr()) }
                        && unsafe { z_eq(unsafe { (*leaf).text }, unsafe { (*leaf).text_len }, b"spin_loop\0".as_ptr()) }
                        && unsafe { (*args).n_kids } as usize == 0
                    {
                        self.out.putc(b'0');
                        return;
                    }
                }
                /* `AtomicU32::new(v)` -> the value itself: the C field IS
                 * a `_Atomic uint32_t`, construction is plain init. The
                 * segment before `new` names the atomic (any depth). */
                if cn >= 2 {
                    let wrap = unsafe { *ck.add(cn - 2) };
                    if unsafe { z_eq(unsafe { (*wrap).text }, unsafe { (*wrap).text_len }, b"AtomicU32\0".as_ptr()) }
                        && unsafe { z_eq(unsafe { (*leaf).text }, unsafe { (*leaf).text_len }, b"new\0".as_ptr()) }
                        && unsafe { (*args).n_kids } as usize == 1
                    {
                        let ak2 = unsafe { (*args).kids };
                        self.out.putc(b'(');
                        unsafe { self.emit_expr(*ak2.add(0), locals) };
                        self.out.putc(b')');
                        return;
                    }
                }
                /* `String::from(x)` / `String::from_utf8_lossy(x)` — the
                 * owned-String ctors: a fresh rsx_str_t built by append
                 * from the arg's (ptr, len) shape. The lossy variant is
                 * the same ctor at the ABI level: the kernel's from is
                 * copy-out, and invalid UTF-8 sequences in registry
                 * spellings are not a live path (the bytes come from C
                 * strings; the op copies them verbatim). */
                if (unsafe { z_eq(unsafe { (*leaf).text }, unsafe { (*leaf).text_len }, b"from\0".as_ptr()) }
                    || unsafe { z_eq(unsafe { (*leaf).text }, unsafe { (*leaf).text_len }, b"from_utf8_lossy\0".as_ptr()) })
                    && unsafe { (*args).n_kids } as usize == 1
                    && cn >= 2
                {
                    let wrap = unsafe { *ck.add(cn - 2) };
                    let wt = unsafe { (*wrap).text };
                    let wl = unsafe { (*wrap).text_len };
                    if unsafe { z_eq(wt, wl, b"String\0".as_ptr()) } {
                        let ak2 = unsafe { (*args).kids };
                        self.out.puts(b"({ rsx_str_t _t = {0}; rsx_str_append(&_t, \0".as_ptr());
                        self.emit_str_arg_append(*ak2.add(0), locals);
                        self.out.puts(b"); _t; })\0".as_ptr());
                        return;
                    }
                }
                /* `Vec::new()` / `String::new()` / `BTreeMap::new()` — the
                 * container ctors: a zero literal is the empty container
                 * (p == 0, n == 0, cap == 0). The declaration context
                 * (ascribed let / return / field init) supplies the type;
                 * as a bare expression the `{0}` compound literal is not
                 * typed, so the general zero-constructor path applies. */
                if unsafe { z_eq(unsafe { (*leaf).text }, unsafe { (*leaf).text_len }, b"new\0".as_ptr()) }
                    && unsafe { (*args).n_kids } as usize == 0
                    && cn >= 2
                {
                    let wrap = unsafe { *ck.add(cn - 2) };
                    let wt = unsafe { (*wrap).text };
                    let wl = unsafe { (*wrap).text_len };
                    if unsafe { z_eq(wt, wl, b"Vec\0".as_ptr()) }
                        || unsafe { z_eq(wt, wl, b"BTreeMap\0".as_ptr()) }
                    {
                        self.out.puts(b"{0}\0".as_ptr());
                        return;
                    }
                    if unsafe { z_eq(wt, wl, b"String\0".as_ptr()) } {
                        /* the owned row's zero IS the empty string; mark
                         * the plane so the typedef lands before this
                         * fn's body (the let typing may have taken the
                         * cur_ret path, but a bare `String::new()` in
                         * an expr position must not dangle) */
                        self.str_own_used = true;
                        self.out.puts(b"{0}\0".as_ptr());
                        return;
                    }
                }
                /* `X::default()` — a #[derive(Default)] struct's zeroed
                 * ctor (any qualification depth). `Self::default()` in
                 * an impl resolves Self to the impl's type first. */
                if unsafe { z_eq(unsafe { (*leaf).text }, unsafe { (*leaf).text_len }, b"default\0".as_ptr()) }
                    && unsafe { (*args).n_kids } as usize == 0
                    && cn >= 2
                {
                    let wrap = unsafe { *ck.add(cn - 2) };
                    let mut wt = unsafe { (*wrap).text };
                    let mut wl = unsafe { (*wrap).text_len };
                    /* Self -> the enclosing impl's type */
                    if unsafe { z_eq(wt, wl, b"Self\0".as_ptr()) } && self.self_ty_len > 0 {
                        wt = self.self_ty.as_ptr();
                        wl = self.self_ty_len;
                    }
                    if wl > 0 && unsafe { self.def_find(wt, wl) } {
                        self.out.puts(b"_\0".as_ptr());
                        self.out.put(wt, wl);
                        self.out.puts(b"_default()\0".as_ptr());
                        return;
                    }
                }
                /* `Mutex::new(v)` / `SpinLock::new(v)` (any qualification
                 * depth — `crate::util::lock::Mutex::new`): the lock ctor.
                 * A one-arg call lowers to the compound literal
                 * { {0}, (v) } — raw lock zeroed (UNLOCKED), payload
                 * copied. The declaration context supplies the row type;
                 * in a static initializer the all-zero path covers
                 * `Mutex::new(None)` (the zero IS a valid lock+None). */
                if unsafe { z_eq(unsafe { (*leaf).text }, unsafe { (*leaf).text_len }, b"new\0".as_ptr()) }
                    && unsafe { (*args).n_kids } as usize == 1
                    && cn >= 2
                {
                    let wrap = unsafe { *ck.add(cn - 2) };
                    let wt = unsafe { (*wrap).text };
                    let wl = unsafe { (*wrap).text_len };
                    if unsafe { z_eq(wt, wl, b"Mutex\0".as_ptr()) }
                        || unsafe { z_eq(wt, wl, b"SpinLock\0".as_ptr()) }
                    {
                        let ak2 = unsafe { (*args).kids };
                        /* None payload: the zero literal — both the lock
                         * bit and the Option's tag/payload are zero, and
                         * `Mutex::new(None)` in a static initializer
                         * stays a constant expression. */
                        if unsafe { self.expr_is_none(*ak2.add(0)) } {
                            self.out.puts(b"{ {0}, {0} }\0".as_ptr());
                            return;
                        }
                        self.out.puts(b"{ {0}, \0".as_ptr());
                        unsafe { self.emit_expr(*ak2.add(0), locals) };
                        self.out.puts(b" }\0".as_ptr());
                        return;
                    }
                }
                /* `Type::fn(..)` — associated fn: mangle to Type_fn. */
                if cn >= 2 {
                    let head = unsafe { *ck.add(0) };
                    let hname = unsafe { (*head).text };
                    let hlen = unsafe { (*head).text_len };
                    if hlen > 0 && unsafe { (*self.syms).find(hname, hlen) } < SYM_CAP {
                        self.out.put(hname, hlen);
                        self.out.putc(b'_');
                    }
                }
                self.out.put(unsafe { (*leaf).text }, unsafe { (*leaf).text_len });
                self.out.putc(b'(');
                let ak = unsafe { (*args).kids };
                let an = unsafe { (*args).n_kids } as usize;
                /* callee name for param-type lookups (a `None` arg needs
                 * the param's Option shape: NULL for pointer payloads,
                 * the rsx_opt_ zero literal for struct-shaped ones) */
                let callok = cn == 1;
                let mut cname: *const u8 = b"\0".as_ptr();
                let mut cnamelen = 0usize;
                if callok {
                    let seg0 = unsafe { *ck.add(0) };
                    cname = unsafe { (*seg0).text };
                    cnamelen = unsafe { (*seg0).text_len };
                }
                let mut i = 0usize;
                while i < an {
                    if i > 0 {
                        self.out.puts(b", \0".as_ptr());
                    }
                    /* `None` in argument position: the param type decides
                     * the C spelling — known unit fn -> exact shape
                     * (rsx_opt_ zero literal for struct-Options, NULL for
                     * pointer payloads). UNKNOWN callee (indirect call,
                     * multi-segment path, unregistered fn): REFUSE. A
                     * bare NULL here would assume the pointer-Option
                     * collapse and miscompile a struct-Option param. */
                    let mut done_none = false;
                    if unsafe { self.expr_is_none(*ak.add(i)) } {
                        if !callok || cname.is_null() || cnamelen == 0 {
                            unsafe {
                                self.err(
                                    b"unsupported: None argument to an unknown callee - declare the fn or pass the Option explicitly\0".as_ptr(),
                                    unsafe { (*e).line },
                                );
                            }
                            return;
                        }
                        let pt = self.arena_tmp();
                        let pl = unsafe {
                            (*self.fns).param_ctype(cname, cnamelen, i, pt)
                        };
                        if pl == 0 {
                            unsafe {
                                self.err(
                                    b"unsupported: None argument - param type unknown\0".as_ptr(),
                                    unsafe { (*e).line },
                                );
                            }
                            return;
                        }
                        if pl >= 8 && unsafe { z_eq(pt, 8, b"rsx_opt_\0".as_ptr()) } {
                            /* struct-shaped Option: the compound
                             * literal carries the typedef name —
                             * a bare braced init is not a C
                             * expression at a call argument */
                            self.out.putc(b'(');
                            self.out.put(pt, pl);
                            self.out.puts(b"){ ._v = {0}, ._has = 0 }\0".as_ptr());
                        } else {
                            self.out.puts(b"NULL\0".as_ptr());
                        }
                        done_none = true;
                    }
                    /* `Some(x)` in argument position: symmetric with None —
                     * the param's Option shape decides. A struct-shaped
                     * param wraps x in the rsx_opt_<elem> compound literal
                     * (the enclosing fn's own return type is NOT the
                     * deciding context here, unlike return-position Some);
                     * a pointer-shaped (or unknown) param passes x bare —
                     * the Option-of-pointer collapse. */
                    if !done_none {
                        let mut some_v: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
                        let ai = unsafe { *ak.add(i) };
                        if unsafe { (*ai).kind } == pm_jit_rsx_ast_kind::CALL {
                            let aick = unsafe { (*ai).kids };
                            let aicn = unsafe { (*ai).n_kids } as usize;
                            if aicn == 2 {
                                let callee2 = unsafe { *aick.add(0) };
                                if unsafe { (*callee2).kind } == pm_jit_rsx_ast_kind::PATH
                                    && unsafe { (*callee2).n_kids } == 1
                                {
                                    let cseg2 = unsafe { *(*callee2).kids.add(0) };
                                    if unsafe {
                                        z_eq(
                                            unsafe { (*cseg2).text },
                                            unsafe { (*cseg2).text_len },
                                            b"Some\0".as_ptr(),
                                        )
                                    } {
                                        let av = unsafe { *aick.add(1) };
                                        if unsafe { (*av).n_kids } as usize == 1 {
                                            some_v = unsafe { *(*av).kids.add(0) };
                                        }
                                    }
                                }
                            }
                        }
                        if !some_v.is_null() {
                            if !callok || cname.is_null() || cnamelen == 0 {
                                unsafe {
                                    self.err(
                                        b"unsupported: Some argument to an unknown callee - declare the fn or pass the Option explicitly\0".as_ptr(),
                                        unsafe { (*e).line },
                                    );
                                }
                                return;
                            }
                            let pt = self.arena_tmp();
                            let pl = unsafe {
                                (*self.fns).param_ctype(cname, cnamelen, i, pt)
                            };
                            if pl == 0 {
                                unsafe {
                                    self.err(
                                        b"unsupported: Some argument - param type unknown\0".as_ptr(),
                                        unsafe { (*e).line },
                                    );
                                }
                                return;
                            }
                            if pl >= 8 && unsafe { z_eq(pt, 8, b"rsx_opt_\0".as_ptr()) } {
                                self.out.putc(b'(');
                                self.out.put(pt, pl);
                                self.out.puts(b"){ ._v = \0".as_ptr());
                                unsafe { self.emit_expr(some_v, locals) };
                                self.out.puts(b", ._has = 1 }\0".as_ptr());
                                done_none = true;
                            }
                            /* pointer-shaped payload: x passes bare
                             * (the Option-of-pointer collapse) — pl > 0
                             * proves the param is a pointer, not an
                             * unknown shape */
                        }
                    }
                    if !done_none {
                        /* dyn coercion: the param is a trait object
                         * (`Trait *`) and the arg is a concrete struct
                         * ref (`S *`) whose unit impls the trait —
                         * materialize the object as a compound literal:
                         * `&(Trait){ ._self = <arg>, .m = S_Trait_m, .. }`.
                         * The literal lives for the full call expression
                         * (C99), exactly Rust's reborrow window. A pair
                         * (S, Trait) the unit never impls refuses loudly
                         * — never a bare cast that would dispatch NULL
                         * slots. */
                        let mut done_coerce = false;
                        if callok && !cname.is_null() && cnamelen > 0 {
                            let pt = self.arena_tmp();
                            let pl = unsafe {
                                (*self.fns).param_ctype(cname, cnamelen, i, pt)
                            };
                            /* param `Trait *`? */
                            if pl > 2 && unsafe { *pt.add(pl - 1) } == b'*' {
                                let mut pl2 = pl - 1;
                                while pl2 > 0 && unsafe { *pt.add(pl2 - 1) } == b' ' {
                                    pl2 -= 1;
                                }
                                let tr = unsafe { self.traits.find(pt, pl2) };
                                if tr < TRAIT_CAP {
                                    let act = self.arena_tmp();
                                    let al = unsafe {
                                        self.expr_ctype(*ak.add(i), act, 128, locals)
                                    };
                                    /* arg `S *`? */
                                    if al > 2 && unsafe { *act.add(al - 1) } == b'*' {
                                        let mut al2 = al - 1;
                                        while al2 > 0 && unsafe { *act.add(al2 - 1) } == b' ' {
                                            al2 -= 1;
                                        }
                                        /* find the ti pair (S, Trait) */
                                        let mut p2 = 0usize;
                                        while p2 < self.ti_n {
                                            let s_nm = self.ti_self[p2].as_ptr();
                                            let s_ln = self.ti_self_lens[p2];
                                            let t_nm2 = self.ti_trait[p2].as_ptr();
                                            let t_ln2 = self.ti_trait_lens[p2];
                                            if s_ln == al2
                                                && t_ln2 == pl2
                                                && unsafe { z_eq(act, al2, s_nm) }
                                                && unsafe { z_eq(pt, pl2, t_nm2) }
                                            {
                                                /* materialize */
                                                self.out.puts(b"&(\0".as_ptr());
                                                self.out.put(pt, pl2);
                                                self.out.puts(b"){ ._self = \0".as_ptr());
                                                unsafe {
                                                    self.emit_expr(*ak.add(i), locals)
                                                };
                                                let mut m2 = 0usize;
                                                while m2 < self.traits.m_counts[tr] {
                                                    let row = tr * TRAIT_MCAP + m2;
                                                    self.out.puts(b", .\0".as_ptr());
                                                    self.out.put(
                                                        self.traits.m_names[row].as_ptr(),
                                                        self.traits.m_name_lens[row],
                                                    );
                                                    self.out.puts(b" = \0".as_ptr());
                                                    /* cast to the slot's fn-ptr type: the
                                                     * impl fn takes S*, the slot takes
                                                     * void* — same call shape, but the
                                                     * init needs the explicit conversion
                                                     * (gcc -Wpedantic and tcc both flag
                                                     * the implicit one). The cast type is
                                                     * the sig with the name spliced out:
                                                     * `ret (*m)(..)` -> `ret (*)(..)`. */
                                                    self.out.putc(b'(');
                                                    {
                                                        let sg = self.traits.m_sigs[row].as_ptr();
                                                        let sl = self.traits.m_sig_lens[row];
                                                        /* find "(*" then the matching name end ")"
                                                         * — usize::MAX is not in the subset
                                                         * (self-host: it would refuse), a
                                                         * found flag + plain usize carry
                                                         * the same sentinel contract. */
                                                        let mut a = 0usize;
                                                        let mut name_at = 0usize;
                                                        let mut name_end = 0usize;
                                                        let mut have = false;
                                                        while a + 1 < sl {
                                                            if unsafe { *sg.add(a) } == b'('
                                                                && unsafe { *sg.add(a + 1) } == b'*'
                                                            {
                                                                name_at = a + 2;
                                                                have = true;
                                                                break;
                                                            }
                                                            a += 1;
                                                        }
                                                        if have {
                                                            let mut b3 = name_at;
                                                            while b3 < sl {
                                                                if unsafe { *sg.add(b3) } == b')' {
                                                                    name_end = b3;
                                                                    break;
                                                                }
                                                                b3 += 1;
                                                            }
                                                        }
                                                        if have && name_end > name_at {
                                                            let mut r = 0usize;
                                                            while r < sl {
                                                                if r >= name_at && r < name_end {
                                                                    r += 1;
                                                                    continue;
                                                                }
                                                                self.out.putc(unsafe { *sg.add(r) });
                                                                r += 1;
                                                            }
                                                        }
                                                    }
                                                    self.out.puts(b")\0".as_ptr());
                                                    self.out.put(s_nm, s_ln);
                                                    self.out.putc(b'_');
                                                    self.out.put(pt, pl2);
                                                    self.out.putc(b'_');
                                                    self.out.put(
                                                        self.traits.m_names[row].as_ptr(),
                                                        self.traits.m_name_lens[row],
                                                    );
                                                    m2 += 1;
                                                }
                                                self.out.puts(b" }\0".as_ptr());
                                                done_coerce = true;
                                                break;
                                            }
                                            p2 += 1;
                                        }
                                    }
                                    /* arg type unknown (al == 0): the
                                     * pair scan found nothing — the
                                     * plain emit below passes the arg
                                     * as-is and the C compiler flags a
                                     * genuinely wrong arg at TCC time. */
                                }
                            }
                        }
                        if !done_coerce {
                            /* `&arr` (borrow of a fixed array `T [N]`) into
                             * an `&[T]` slice param (rsx_arr_<row>): the fat
                             * pair {arr, N} — the array lvalue decays to T*
                             * in the literal's first field. The arg types
                             * as `T (*)[N]` (pointer-to-array); match it by
                             * its dims tail. */
                            let mut done_arr = false;
                            if callok && !cname.is_null() && cnamelen > 0 {
                                let pt = self.arena_tmp();
                                let pl = unsafe {
                                    (*self.fns).param_ctype(cname, cnamelen, i, pt)
                                };
                                if pl > 8 && unsafe { z_eq(pt, 8, b"rsx_arr_\0".as_ptr()) } {
                                    /* the arg must be `&arr` (a borrow whose
                                     * operand is a place): a pointer-to-array
                                     * or a bare array lvalue both work — the
                                     * compound literal takes the array
                                     * itself (it decays to T*) and lets C
                                     * compute the element count. */
                                    let mut inner: *const pm_jit_rsx_ast_t =
                                        core::ptr::null_mut();
                                    let argn = unsafe { *ak.add(i) };
                                    if unsafe { (*argn).kind } == pm_jit_rsx_ast_kind::UNARY
                                        && unsafe { (*argn).n_kids } as usize >= 1
                                    {
                                        let op = unsafe { (*argn).text };
                                        let ol = unsafe { (*argn).text_len };
                                        if unsafe { z_eq(op, ol, b"&\0".as_ptr()) }
                                            || unsafe { z_eq(op, ol, b"&mut\0".as_ptr()) }
                                        {
                                            let kk = unsafe { (*argn).kids };
                                            inner = unsafe { *kk.add(0) };
                                        }
                                    }
                                    if !inner.is_null() {
                                        let act = self.arena_tmp();
                                        let al = unsafe {
                                            self.expr_ctype(inner, act, 128, locals)
                                        };
                                        /* array-shaped operand: dims tail
                                         * (`T [N]`, `T []`) or the
                                         * pointer-to-array declarator. */
                                        let has_dims = al > 0
                                            && unsafe { *act.add(al - 1) } == b']';
                                        let is_parr = al > 0
                                            && unsafe {
                                                self.parr_declarator(act, al)
                                            } != usize::MAX;
                                        if has_dims || is_parr {
                                            self.out.putc(b'(');
                                            self.out.put(pt, pl);
                                            self.out.puts(b"){ \0".as_ptr());
                                            unsafe { self.emit_expr(inner, locals) };
                                            self.out.puts(b", sizeof(\0".as_ptr());
                                            unsafe { self.emit_expr(inner, locals) };
                                            self.out.puts(b") / sizeof((\0".as_ptr());
                                            unsafe { self.emit_expr(inner, locals) };
                                            self.out.puts(b")[0]) }\0".as_ptr());
                                            done_arr = true;
                                        }
                                    }
                                }
                            }
                            if !done_arr {
                                /* `&str` param taking a LITERAL: the fat
                                 * ref compound literal — a raw "..." is
                                 * char[N] in C, not the struct (statics'
                                 * FACE_* twins already coerce; the call
                                 * arg path needs its own). */
                                let mut done_lit = false;
                                if callok && !cname.is_null() && cnamelen > 0 {
                                    let pt = self.arena_tmp();
                                    let pl = unsafe {
                                        (*self.fns).param_ctype(cname, cnamelen, i, pt)
                                    };
                                    if pl == 13
                                        && unsafe { z_eq(pt, 13, b"rsx_str_ref_t\0".as_ptr()) }
                                    {
                                        let argn = unsafe { *ak.add(i) };
                                        if unsafe { (*argn).kind }
                                            == pm_jit_rsx_ast_kind::LITERAL
                                        {
                                            let t = unsafe { (*argn).text };
                                            let tl = unsafe { (*argn).text_len };
                                            if tl >= 2 && unsafe { *t } == b'"' {
                                                self.out.puts(
                                                    b"(rsx_str_ref_t){ (const uint8_t *)\0"
                                                        .as_ptr(),
                                                );
                                                self.out.put(t, tl);
                                                self.out.puts(
                                                    b", \0".as_ptr(),
                                                );
                                                let lit_inner = t.wrapping_add(1);
                                                let clen = unsafe {
                                                    Lower::c_str_len(
                                                        lit_inner,
                                                        tl.wrapping_sub(2),
                                                    )
                                                };
                                                self.out.put_u32(clen);
                                                self.out.puts(b" }\0".as_ptr());
                                                done_lit = true;
                                            }
                                        }
                                    }
                                }
                                if !done_lit {
                                    /* `&String` into an `&str` param
                                     * (`c_sig_needs_local_types(&e.sig)`):
                                     * the deref coercion — the fat view of
                                     * the owned string, never the `T *`
                                     * pointer a raw borrow renders. */
                                    let mut done_deref = false;
                                    if callok && !cname.is_null() && cnamelen > 0 {
                                        let pt = self.arena_tmp();
                                        let pl = unsafe {
                                            (*self.fns).param_ctype(cname, cnamelen, i, pt)
                                        };
                                        if pl == 13
                                            && unsafe { z_eq(pt, 13, b"rsx_str_ref_t\0".as_ptr()) }
                                        {
                                            let argn = unsafe { *ak.add(i) };
                                            if unsafe { (*argn).kind }
                                                == pm_jit_rsx_ast_kind::UNARY
                                            {
                                                let aop = unsafe { (*argn).text };
                                                let aol = unsafe { (*argn).text_len };
                                                if unsafe { z_eq(aop, aol, b"&\0".as_ptr()) }
                                                    || unsafe {
                                                        z_eq(aop, aol, b"&mut\0".as_ptr())
                                                    }
                                                {
                                                    let ik = unsafe { (*argn).kids };
                                                    if unsafe { (*argn).n_kids } as usize >= 1 {
                                                        let inner = unsafe { *ik.add(0) };
                                                        let ib = self.arena_tmp();
                                                        let il = unsafe {
                                                            self.expr_ctype(
                                                                inner,
                                                                ib,
                                                                128,
                                                                locals,
                                                            )
                                                        };
                                                        if il == 9
                                                            && unsafe {
                                                                z_eq(
                                                                    ib,
                                                                    9,
                                                                    b"rsx_str_t\0".as_ptr(),
                                                                )
                                                            }
                                                        {
                                                            self.out.puts(
                                                                b"(rsx_str_ref_t){ (\0".as_ptr(),
                                                            );
                                                            unsafe {
                                                                self.emit_expr(
                                                                    inner, locals,
                                                                )
                                                            };
                                                            self.out.puts(
                                                                b").p, (\0".as_ptr(),
                                                            );
                                                            unsafe {
                                                                self.emit_expr(
                                                                    inner, locals,
                                                                )
                                                            };
                                                            self.out.puts(
                                                                b").n }\0".as_ptr(),
                                                            );
                                                            done_deref = true;
                                                        }
                                                    }
                                                }
                                            }
                                            if !done_deref {
                                                /* bare owned String — `f(s)` where
                                                 * s: String is the same deref
                                                 * coercion `f(&s)` takes (the
                                                 * arg itself types rsx_str_t,
                                                 * no UNARY wrapper): the fat
                                                 * view of the place. */
                                                let ib = self.arena_tmp();
                                                let il = unsafe {
                                                    self.expr_ctype(argn, ib, 128, locals)
                                                };
                                                if il == 9
                                                    && unsafe {
                                                        z_eq(ib, 9, b"rsx_str_t\0".as_ptr())
                                                    }
                                                {
                                                    self.out.puts(
                                                        b"(rsx_str_ref_t){ (\0".as_ptr(),
                                                    );
                                                    unsafe {
                                                        self.emit_expr(argn, locals)
                                                    };
                                                    self.out.puts(b").p, (\0".as_ptr());
                                                    unsafe {
                                                        self.emit_expr(argn, locals)
                                                    };
                                                    self.out.puts(b").n }\0".as_ptr());
                                                    done_deref = true;
                                                }
                                            }
                                        }
                                    }
                                    if !done_deref {
                                        unsafe { self.emit_expr(*ak.add(i), locals) };
                                    }
                                }
                            }
                        }
                    }
                    i += 1;
                }
                self.out.putc(b')');
                return;
            }
        }
        unsafe {
            self.err(b"unsupported: call target\0".as_ptr(), unsafe { (*e).line });
        }
    }

    /* String-plane argument shape: lower a string-typed expr to the raw
     * (ptr, len) pair the rsx_str_append face takes. The arg's own type
     * decides — a literal lowers to (lit, strlen) [the compound literal
     * a &str param takes; strlen is folded by C], an owned String to
     * (s.p ? s.p : "", s.n), an &str fat ref to (r.p, r.n). The
     * str_ref compound literal for a literal arg is the &str plane's
     * own emission (emit_ret_value), so this only ever runs on values
     * that ARE the fat ref shape already. */
    /* The C-string byte length of a raw source run: each escape pair
     * (`\n`, `\t`, `\"`, `\\`) is one C byte, every other byte is one.
     * The format! walker emits runs verbatim from the source literal,
     * so the appended LENGTH must be the C length, not the source
     * length. */
    unsafe fn c_str_len(p: *const u8, n: usize) -> u32 {
        let mut i = 0usize;
        let mut len = 0usize;
        while i < n {
            if unsafe { *p.add(i) } == b'\\' && i + 1 < n {
                i += 2;
            } else {
                i += 1;
            }
            len += 1;
        }
        len as u32
    }

    /* write!(sink, ..) / writeln!(sink, ..) — the io-Write macro as a
     * VALUE (`let _ = writeln!(out, ..)`): builds the payload with the
     * same segment walker format! uses, fwrites it to the FILE * sink,
     * appends the newline for writeln!, and yields 0 (the discarded
     * io::Result — the `let _ =` shape is the only in-tree spelling).
     * Returns true when the MACRO node was write-family (emission done). */
    unsafe fn emit_write_macro(
        &mut self,
        e: *const pm_jit_rsx_ast_t,
        locals: *mut LocalTab,
    ) -> bool {
        let mut sink: *mut pm_jit_rsx_ast_t = core::ptr::null_mut();
        let mut ffmt: *const u8 = core::ptr::null();
        let mut ffmt_len: usize = 0;
        let mut fargs: [*mut pm_jit_rsx_ast_t; 8] = [core::ptr::null_mut(); 8];
        let mut fargs_n: usize = 0;
        let mut fcaps: [*mut pm_jit_rsx_ast_t; 16] = [core::ptr::null_mut(); 16];
        let mut fcaps_n: usize = 0;
        let mut newline = false;
        if !unsafe {
            self.write_macro_scan(
                (*e).text,
                (*e).text_len,
                locals,
                &mut sink,
                &mut ffmt,
                &mut ffmt_len,
                fargs.as_mut_ptr(),
                &mut fargs_n,
                fcaps.as_mut_ptr(),
                &mut fcaps_n,
                &mut newline,
            )
        } {
            return false;
        }
        if !self.ok {
            return true;
        }
        self.file_used = true;
        if ffmt_len == 0 {
            /* the bare form: writeln!(out) — one newline, no payload */
            self.out.puts(b"({ fputc('\\n', \0".as_ptr());
            unsafe { self.emit_expr(sink, locals) };
            self.out.puts(b"); 0; })\0".as_ptr());
            return true;
        }
        /* payload build + write + optional newline — one statement expr */
        self.str_own_used = true;
        self.out.puts(b"({ rsx_str_t _f = {0};\0".as_ptr());
        let ok = unsafe { self.format_segments(e, ffmt, ffmt_len, &fargs, fargs_n, &fcaps, fcaps_n, locals) };
        if !ok {
            return true;
        }
        if newline {
            self.out.puts(b" rsx_str_append(&_f, \"\\n\", 1);\0".as_ptr());
        }
        self.out.puts(b" fwrite(_f.p, 1, _f.n, \0".as_ptr());
        unsafe { self.emit_expr(sink, locals) };
        self.out.puts(b"); 0; })\0".as_ptr());
        true
    }

    /* format!(..) — the interpolation macro as a VALUE: a GNU statement
     * expression building a block-local rsx_str_t _f, segment by
     * segment (literal runs verbatim, `{ident}` implicit captures and
     * `{}` positional args by their shape — str-shaped appends through
     * the one face, integers in decimal), the last statement yields _f.
     * Returns true when the MACRO node was a format! (emission done),
     * false when the macro is something else. */
    unsafe fn emit_format_value(
        &mut self,
        e: *const pm_jit_rsx_ast_t,
        locals: *mut LocalTab,
    ) -> bool {
        let mut ffmt: *const u8 = core::ptr::null();
        let mut ffmt_len: usize = 0;
        let mut fargs: [*mut pm_jit_rsx_ast_t; 8] = [core::ptr::null_mut(); 8];
        let mut fargs_n: usize = 0;
        let mut fcaps: [*mut pm_jit_rsx_ast_t; 16] = [core::ptr::null_mut(); 16];
        let mut fcaps_n: usize = 0;
        if !unsafe {
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
            return false;
        }
        if !self.ok {
            return true;
        }
        self.str_own_used = true;
        self.out.puts(b"({ rsx_str_t _f = {0};\0".as_ptr());
        let ok = unsafe { self.format_segments(e, ffmt, ffmt_len, &fargs, fargs_n, &fcaps, fcaps_n, locals) };
        if !ok {
            return true;
        }
        self.out.puts(b" _f; })\0".as_ptr());
        true
    }

    /* The format! segment walker — emits the `rsx_str_append(&_f, ..)`
     * statements for a scanned format! into out. Shared by the value
     * form (emit_format_value) and the push_str-into-receiver form
     * (emit_format_into). Returns false after a loud refusal. */
    unsafe fn format_segments(
        &mut self,
        e: *const pm_jit_rsx_ast_t,
        ffmt: *const u8,
        ffmt_len: usize,
        fargs: &[*mut pm_jit_rsx_ast_t; 8],
        fargs_n: usize,
        fcaps: &[*mut pm_jit_rsx_ast_t; 16],
        fcaps_n: usize,
        locals: *mut LocalTab,
    ) -> bool {
        let mut at = 0usize;
        let mut run_start = 0usize;
        let mut arg_i = 0usize;
        let mut cap_i = 0usize;
        while at < ffmt_len {
            let c = unsafe { *ffmt.add(at) };
            if c == b'{' {
                if at + 1 < ffmt_len && unsafe { *ffmt.add(at + 1) } == b'{' {
                    /* `{{` — an escaped brace: flush the run with the
                     * first `{` included, resume after the pair */
                    self.out.puts(b" rsx_str_append(&_f, \"\0".as_ptr());
                    self.out.put(ffmt.add(run_start), at + 1 - run_start);
                    self.out.puts(b"\", \0".as_ptr());
                    self.out.put_u32(unsafe { Lower::c_str_len(ffmt.add(run_start), at + 1 - run_start) });
                    self.out.puts(b");\0".as_ptr());
                    at += 2;
                    run_start = at;
                    continue;
                }
                /* flush the pending literal run first */
                if at > run_start {
                    self.out.puts(b" rsx_str_append(&_f, \"\0".as_ptr());
                    self.out.put(ffmt.add(run_start), at - run_start);
                    self.out.puts(b"\", \0".as_ptr());
                    self.out.put_u32(unsafe { Lower::c_str_len(ffmt.add(run_start), at - run_start) });
                    self.out.puts(b");\0".as_ptr());
                }
                /* find the hole's end */
                let close = at + 1;
                let mut j = close;
                let mut ident = false;
                while j < ffmt_len {
                    let hc = unsafe { *ffmt.add(j) };
                    if hc == b'}' {
                        ident = j > close;
                        break;
                    }
                    if !(hc.is_ascii_alphanumeric() || hc == b'_') {
                        ident = false;
                        break;
                    }
                    j += 1;
                }
                if j >= ffmt_len {
                    unsafe {
                        self.err(
                            b"unsupported: format! hole unterminated\0".as_ptr(),
                            unsafe { (*e).line },
                        );
                    }
                    return false;
                }
                let expr: *mut pm_jit_rsx_ast_t = if ident {
                    let x = if cap_i < fcaps_n { fcaps[cap_i] } else { core::ptr::null_mut() };
                    cap_i += 1;
                    x
                } else {
                    let x = if arg_i < fargs_n { fargs[arg_i] } else { core::ptr::null_mut() };
                    arg_i += 1;
                    x
                };
                if expr.is_null() {
                    unsafe {
                        self.err(
                            b"unsupported: format! hole has no argument\0".as_ptr(),
                            unsafe { (*e).line },
                        );
                    }
                    return false;
                }
                /* the hole expr's shape: str-shaped appends through the
                 * one face, integers go decimal. Anything else refuses
                 * loudly. */
                let is_lit_str = unsafe { (*expr).kind } == pm_jit_rsx_ast_kind::LITERAL
                    && !unsafe { (*expr).text }.is_null()
                    && unsafe { *(*expr).text } == b'"';
                let sb = self.arena_tmp();
                let sl = unsafe { self.expr_ctype(expr, sb, 128, locals) };
                let is_str = is_lit_str
                    || (sl == 9 && unsafe { z_eq(sb, 9, b"rsx_str_t\0".as_ptr()) })
                    || (sl == 13 && unsafe { z_eq(sb, 13, b"rsx_str_ref_t\0".as_ptr()) });
                /* integer spellings: uint8_t..uint64_t / int8_t..int64_t
                 * / size_t / ssize_t / uintptr_t / the bare `int` */
                let int_name = !is_str
                    && sl > 0
                    && (unsafe { *sb == b'u' }
                        || unsafe { *sb == b'i' }
                        || unsafe { *sb == b's' }
                        || (sl >= 3 && unsafe { z_eq(sb, 3, b"int\0".as_ptr()) }));
                if is_str {
                    self.out.puts(b" rsx_str_append(&_f, \0".as_ptr());
                    unsafe { self.emit_str_arg_append(expr, locals) };
                    self.out.puts(b");\0".as_ptr());
                } else if int_name {
                    self.out.puts(b" rsx_str_push_i64(&_f, (int64_t)(\0".as_ptr());
                    unsafe { self.emit_expr(expr, locals) };
                    self.out.puts(b"));\0".as_ptr());
                } else {
                    unsafe {
                        self.err(
                            b"unsupported: format! hole type\0".as_ptr(),
                            unsafe { (*e).line },
                        );
                    }
                    return false;
                }
                at = j + 1;
                run_start = at;
                continue;
            }
            if c == b'}' && at + 1 < ffmt_len && unsafe { *ffmt.add(at + 1) } == b'}' {
                /* `}}` — flush the run with the first `}` */
                self.out.puts(b" rsx_str_append(&_f, \"\0".as_ptr());
                self.out.put(ffmt.add(run_start), at + 1 - run_start);
                self.out.puts(b"\", \0".as_ptr());
                self.out.put_u32(unsafe { Lower::c_str_len(ffmt.add(run_start), at + 1 - run_start) });
                self.out.puts(b");\0".as_ptr());
                at += 2;
                run_start = at;
                continue;
            }
            at += 1;
        }
        /* flush the trailing run */
        if ffmt_len > run_start {
            self.out.puts(b" rsx_str_append(&_f, \"\0".as_ptr());
            self.out.put(ffmt.add(run_start), ffmt_len - run_start);
            self.out.puts(b"\", \0".as_ptr());
            self.out.put_u32(unsafe { Lower::c_str_len(ffmt.add(run_start), ffmt_len - run_start) });
            self.out.puts(b");\0".as_ptr());
        }
        true
    }

    unsafe fn emit_str_arg_append(&mut self, a: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        if a.is_null() {
            self.out.puts(b"(const char *)\"\", 0\0".as_ptr());
            return;
        }
        /* slice-ref argument `&arr[lo..hi]` / `&arr[..hi]` / `&vec[lo..hi]`:
         * the (ptr, len) pair directly — base+lo pointer, hi-lo length.
         * This is the `&[u8]` view String::from_utf8_lossy and friends
         * consume; the plain emission path drops the hi bound (slices
         * are ptr-only by convention) which loses exactly the length
         * the string plane needs. */
        {
            let mut inner: *const pm_jit_rsx_ast_t = a;
            let mut is_borrow = false;
            if unsafe { (*inner).kind } == pm_jit_rsx_ast_kind::UNARY {
                let t = unsafe { (*inner).text };
                let tl = unsafe { (*inner).text_len };
                if unsafe { z_eq(t, tl, b"&\0".as_ptr()) }
                    || unsafe { z_eq(t, tl, b"&mut\0".as_ptr()) }
                {
                    let uk = unsafe { (*inner).kids };
                    if unsafe { (*inner).n_kids } >= 1 {
                        inner = unsafe { *uk.add(0) };
                        is_borrow = true;
                    }
                }
            }
            if is_borrow && unsafe { rsx_idx_is_range(inner) } {
                let ik = unsafe { (*inner).kids };
                let base = unsafe { *ik.add(0) };
                let rng = unsafe { *ik.add(1) };
                let rk = unsafe { (*rng).kids };
                let rn = unsafe { (*rng).n_kids } as usize;
                /* BINARY(..) kids: [lo, hi] — lo may be an empty TUPLE
                 * (the `..hi` form), hi is present for a bounded range */
                let lo = if rn >= 1 { unsafe { *rk.add(0) } } else { core::ptr::null_mut() };
                let hi = if rn >= 2 { unsafe { *rk.add(1) } } else { core::ptr::null_mut() };
                let lo_empty = !lo.is_null()
                    && unsafe { (*lo).kind } == pm_jit_rsx_ast_kind::TUPLE
                    && unsafe { (*lo).n_kids } == 0;
                let hi_empty = hi.is_null()
                    || (unsafe { (*hi).kind } == pm_jit_rsx_ast_kind::TUPLE
                        && unsafe { (*hi).n_kids } == 0);
                if !hi_empty {
                    /* Vec/slice-ref base: the slab lives in .p */
                    let bct = self.arena_tmp();
                    let bn2 = unsafe { self.expr_ctype(base, bct, 128, locals) };
                    let is_vec = bn2 > 8 && bn2 < 128 && unsafe { z_eq(bct, 8, b"rsx_vec_\0".as_ptr()) };
                    let is_arr = bn2 > 8 && bn2 < 128 && unsafe { z_eq(bct, 8, b"rsx_arr_\0".as_ptr()) };
                    self.out.puts(b"(const char *)(\0".as_ptr());
                    if is_vec || is_arr {
                        self.out.puts(b"(\0".as_ptr());
                        unsafe { self.emit_expr(base, locals) };
                        self.out.puts(b").p\0".as_ptr());
                    } else {
                        unsafe { self.emit_expr(base, locals) };
                    }
                    if !lo_empty {
                        self.out.puts(b" + \0".as_ptr());
                        unsafe { self.emit_expr(lo, locals) };
                    }
                    self.out.puts(b"), \0".as_ptr());
                    /* length: hi - lo (lo=0 when empty) */
                    unsafe { self.emit_expr(hi, locals) };
                    if !lo_empty {
                        self.out.puts(b" - \0".as_ptr());
                        unsafe { self.emit_expr(lo, locals) };
                    }
                    return;
                }
            }
        }
        /* literal: emit as a NUL-terminated C string + strlen — the
         * literal's bytes carry no escape hazards (the lexer scanned
         * them raw; a Rust literal with quotes escapes at emit). */
        if unsafe { (*a).kind } == pm_jit_rsx_ast_kind::LITERAL {
            let t = unsafe { (*a).text };
            let tl = unsafe { (*a).text_len };
            if tl >= 2 && unsafe { *t } == b'"' {
                self.out.puts(b"(const char *)\0".as_ptr());
                self.out.put(t, tl);
                self.out.puts(b", strlen(\0".as_ptr());
                self.out.put(t, tl);
                self.out.puts(b")\0".as_ptr());
                return;
            }
        }
        /* typed: owned String vs fat ref — the C spelling decides */
        let ab = self.arena_tmp();
        let mut probe = a;
        /* unwrap a borrow: `&vec`/`&mut vec` (and `&str`-shaped refs)
         * type through the inner expr — the rendered borrow spelling is
         * a pointer, which loses the container identity. */
        let mut hops = 0;
        while !probe.is_null()
            && hops < 4
            && unsafe { (*probe).kind } == pm_jit_rsx_ast_kind::UNARY
        {
            let t = unsafe { (*probe).text };
            let tl = unsafe { (*probe).text_len };
            /* unwrap a borrow: `&vec`/`&mut vec` (and `&str`-shaped refs)
             * type through the inner expr — the rendered borrow spelling is
             * a pointer, which loses the container identity. A deref `*n`
             * on a fat ref (|n|: &&str) is the same view — unwrap both. */
            if unsafe { z_eq(t, tl, b"&\0".as_ptr()) }
                || unsafe { z_eq(t, tl, b"&mut\0".as_ptr()) }
                || unsafe { z_eq(t, tl, b"*\0".as_ptr()) }
            {
                if unsafe { (*probe).n_kids } as usize >= 1 {
                    probe = unsafe { *(*probe).kids.add(0) };
                } else {
                    break;
                }
            } else {
                break;
            }
            hops += 1;
        }
        let an2 = unsafe { self.expr_ctype(probe, ab, 128, locals) };
        /* `&vec` (borrowed Vec<u8>): the (p, n) view of the slab — a
         * String::from_utf8_lossy(&buf) takes the bytes, not the
         * container. .p may be NULL on an empty Vec; the append's
         * ternary guards it. The borrow unwraps above, so the pair
         * selects on the place itself. NO outer group around the
         * ternary+length pair — the comma must split at the CALL level
         * (an outer group makes the append see one comma-expression
         * arg: a pointer arg from the length integer, and one arg too
         * few). Each select wraps its OWN copy of the place. */
        if an2 > 8 && an2 < 128 && unsafe { z_eq(ab, 8, b"rsx_vec_\0".as_ptr()) } {
            self.out.puts(b"(\0".as_ptr());
            unsafe { self.emit_expr(probe, locals) };
            self.out.puts(b").p ? (\0".as_ptr());
            unsafe { self.emit_expr(probe, locals) };
            self.out.puts(b").p : \"\", (\0".as_ptr());
            unsafe { self.emit_expr(probe, locals) };
            self.out.puts(b").n\0".as_ptr());
            return;
        }
        if an2 == 9 && unsafe { z_eq(ab, 9, b"rsx_str_t\0".as_ptr()) } {
            /* owned: p may be NULL when empty — append guards it. Each
             * select wraps its OWN copy of the receiver in parens
             * (X may be a statement-expr — a bare `X).p` would leak its
             * closing paren), and NO outer group: the comma between
             * the ternary and the length must split at the CALL level
             * so the append sees all three arguments.
             * A `&call(..)` arg is not C — the call's result is an
             * rvalue, it has no address — but the member selects read
             * the VALUE, so the borrow is dropped: emit the operand
             * (one hop, `&`/`&mut` only; a `*` deref keeps its own
             * operand). `&place` keeps its meaning either way. */
            let mut val = a;
            let mut vh = 0;
            while vh < 4
                && unsafe { (*val).kind } == pm_jit_rsx_ast_kind::UNARY
                && unsafe { (*val).n_kids } as usize >= 1
                && (unsafe { z_eq(unsafe { (*val).text }, unsafe { (*val).text_len }, b"&\0".as_ptr()) }
                    || unsafe {
                        z_eq(unsafe { (*val).text }, unsafe { (*val).text_len }, b"&mut\0".as_ptr())
                    })
            {
                val = unsafe { *(*val).kids.add(0) };
                vh += 1;
            }
            self.out.puts(b"(\0".as_ptr());
            unsafe { self.emit_expr(val, locals) };
            self.out.puts(b").p ? (\0".as_ptr());
            unsafe { self.emit_expr(val, locals) };
            self.out.puts(b").p : \"\", (\0".as_ptr());
            unsafe { self.emit_expr(val, locals) };
            self.out.puts(b").n\0".as_ptr());
            return;
        }
        if an2 == 13 && unsafe { z_eq(ab, 13, b"rsx_str_ref_t\0".as_ptr()) } {
            /* fat ref: the struct value itself selects .p/.n — NO cast on
             * the struct (a cast to char* would strip the struct and .p
             * would be a member-of-scalar error); the .p SELECT is cast
             * to const char* for rsx_str_append's signature. */
            self.out.puts(b"(const char *)(\0".as_ptr());
            unsafe { self.emit_expr(a, locals) };
            self.out.puts(b").p, (\0".as_ptr());
            unsafe { self.emit_expr(a, locals) };
            self.out.puts(b").n\0".as_ptr());
            return;
        }
        /* unknown shape — refuse loudly, never append garbage */
        unsafe {
            self.err(
                b"unsupported: string append from a non-string value\0".as_ptr(),
                unsafe { (*a).line },
            );
        }
    }

    /* .to_string()/.to_owned()/.into_owned() receiver gate: only a
     * string-shaped receiver builds an owned String (an integer or
     * user-struct receiver with the same method name must fall through
     * to its own lowering, not miscompile into a bogus String). */
    unsafe fn str_receiver_ok(&mut self, recv: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) -> bool {
        if recv.is_null() {
            return false;
        }
        /* a literal string receiver is string-shaped by construction */
        if unsafe { (*recv).kind } == pm_jit_rsx_ast_kind::LITERAL {
            let t = unsafe { (*recv).text };
            let tl = unsafe { (*recv).text_len };
            if tl >= 2 && unsafe { *t } == b'"' {
                return true;
            }
        }
        let rb = self.arena_tmp();
        let rn = unsafe { self.expr_ctype(recv, rb, 128, locals) };
        if rn == 9 && unsafe { z_eq(rb, 9, b"rsx_str_t\0".as_ptr()) } {
            return true;
        }
        if rn == 13 && unsafe { z_eq(rb, 13, b"rsx_str_ref_t\0".as_ptr()) } {
            return true;
        }
        false
    }

    /* one String-op call shape: op(amp-recv, str-arg) — used by push_str
     * (the arg lowered to its (ptr,len) pair) and push(char) (the arg
     * emitted raw, a char scalar). arg_pair decides the arg's shape;
     * both op names are NUL-terminated literals (the puts contract). */
    unsafe fn emit_str_call(
        &mut self,
        op: *const u8,
        amp: *const u8,
        recv: *const pm_jit_rsx_ast_t,
        arg: *const pm_jit_rsx_ast_t,
        arg_pair: bool,
        locals: *mut LocalTab,
    ) {
        self.out.puts(op);
        self.out.putc(b'(');
        if !amp.is_null() {
            self.out.puts(amp);
        }
        unsafe { self.emit_expr(recv, locals) };
        self.out.puts(b", \0".as_ptr());
        if arg_pair {
            self.emit_str_arg_append(arg, locals);
        } else {
            unsafe { self.emit_expr(arg, locals) };
        }
        self.out.putc(b')');
    }

    unsafe fn emit_method_call(&mut self, e: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        /* kids: recv, name(PATH), args(TUPLE) */
        let kids = unsafe { (*e).kids };
        let nk = unsafe { (*e).n_kids } as usize;
        if nk < 3 {
            return;
        }
        let recv = unsafe { *kids.add(0) };
        let name = unsafe { *kids.add(1) };
        let args = unsafe { *kids.add(2) };
        let mname = unsafe { (*name).text };
        let mlen = unsafe { (*name).text_len };
        let an = unsafe { (*args).n_kids } as usize;
        let ak = unsafe { (*args).kids };
        /* split_whitespace().collect::<Vec<_>>().join(sep) — the
         * whitespace-normalize chain, one stmt-expr producing the
         * joined rsx_str_t. Walk the view's bytes: skip ws runs, each
         * word appends (with the separator before every word but the
         * first). sep is a string literal. */
        if unsafe { z_eq(mname, mlen, b"join\0".as_ptr()) }
            && an == 1
            && unsafe { (*recv).kind } == pm_jit_rsx_ast_kind::METHOD_CALL
            && (unsafe { (*recv).n_kids } as usize) >= 2
        {
            let rk = unsafe { (*recv).kids };
            let rname = unsafe { *rk.add(1) };
            if unsafe { (*rname).text_len } == 7
                && unsafe { z_eq(unsafe { (*rname).text }, 7, b"collect\0".as_ptr()) }
            {
                let inner_recv = unsafe { *rk.add(0) };
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
                        if sl == 13 && unsafe { z_eq(sb, 13, b"rsx_str_ref_t\0".as_ptr()) } {
                            /* sep: a string literal (its C length via
                             * c_str_len — escapes fold) */
                            let sep = unsafe { *ak.add(0) };
                            if unsafe { (*sep).kind } == pm_jit_rsx_ast_kind::LITERAL {
                                let st = unsafe { (*sep).text };
                                let stl = unsafe { (*sep).text_len };
                                if stl >= 2 && unsafe { *st } == b'"' {
                                    let sep_inner = st.wrapping_add(1);
                                    let sep_c_len =
                                        unsafe { Lower::c_str_len(st.add(1), stl - 2) } as u64;
                                    self.str_ref_used = true;
                                    self.str_own_used = true;
                                    self.out.puts(b"({ rsx_str_t _j = {0}; size_t _i = 0; size_t _n = (\0".as_ptr());
                                    unsafe { self.emit_expr(base, locals) };
                                    self.out.puts(b").n; const uint8_t *_p = (\0".as_ptr());
                                    unsafe { self.emit_expr(base, locals) };
                                    self.out.puts(b").p; while (_i < _n) { while (_i < _n && (_p[_i]==' '||_p[_i]=='\\t'||_p[_i]=='\\n'||_p[_i]=='\\r'||_p[_i]=='\\x0c')) { _i++; } if (_i >= _n) { break; } size_t _s = _i; while (_i < _n && !(_p[_i]==' '||_p[_i]=='\\t'||_p[_i]=='\\n'||_p[_i]=='\\r'||_p[_i]=='\\x0c')) { _i++; } if (_j.n != 0) { rsx_str_append(&_j, \"\0".as_ptr());
                                    self.out.put(sep_inner, stl - 2);
                                    self.out.puts(b"\", \0".as_ptr());
                                    self.out.put_u32(sep_c_len as u32);
                                    self.out.puts(b"); } rsx_str_append(&_j, _p + _s, _i - _s); } _j; })\0".as_ptr());
                                    return;
                                }
                            }
                        }
                    }
                }
            }
        }
        /* `Vec<String>.join(sep)` — one owned String: each element's
         * bytes appended, the sep literal between neighbors. Handles
         * both the by-value row and the &Vec<T> param (auto-deref). */
        if unsafe { z_eq(mname, mlen, b"join\0".as_ptr()) } && an == 1 {
            let sep = unsafe { *ak.add(0) };
            if unsafe { (*sep).kind } == pm_jit_rsx_ast_kind::LITERAL {
                let st = unsafe { (*sep).text };
                let stl = unsafe { (*sep).text_len };
                if stl >= 2 && unsafe { *st } == b'"' {
                    let rbuf = self.arena_tmp();
                    let rl = unsafe { self.expr_ctype(recv, rbuf, 128, locals) };
                    let mut b0 = 0usize;
                    if rl > 6 && unsafe { z_eq(rbuf, 6, b"const \0".as_ptr()) } {
                        b0 = 6;
                    }
                    let mut el = rl - b0;
                    let mut behind_ptr = false;
                    if el > 2 && unsafe { *rbuf.add(rl - 1) } == b'*' {
                        el -= 2;
                        behind_ptr = true;
                    }
                    let mut is_vec_str = false;
                    if el > 8 && unsafe { z_eq(rbuf.add(b0), 8, b"rsx_vec_\0".as_ptr()) } {
                        let row = unsafe { self.vecs.find_by_name(rbuf.add(b0), el) };
                        if row < VEC_CAP {
                            let elen2 = self.vecs.elem_lens[row];
                            is_vec_str = elen2 == 9
                                && unsafe {
                                    z_eq(self.vecs.elems[row].as_ptr(), 9, b"rsx_str_t\0".as_ptr())
                                };
                        }
                    }
                    if is_vec_str {
                        let sep_inner = st.wrapping_add(1);
                        let sep_c_len =
                            unsafe { Lower::c_str_len(st.add(1), stl - 2) } as u64;
                        self.str_ref_used = true;
                        self.str_own_used = true;
                        self.out.puts(b"({ rsx_str_t _j = {0}; size_t _k; for (_k = 0; _k < (\0".as_ptr());
                        if behind_ptr {
                            self.out.putc(b'*');
                        }
                        unsafe { self.emit_expr(recv, locals) };
                        self.out.puts(b").n; _k++) { if (_k != 0) { rsx_str_append(&_j, \"\0".as_ptr());
                        self.out.put(sep_inner, stl - 2);
                        self.out.puts(b"\", \0".as_ptr());
                        self.out.put_u32(sep_c_len as u32);
                        self.out.puts(b"); } rsx_str_append(&_j, (\0".as_ptr());
                        if behind_ptr {
                            self.out.putc(b'*');
                        }
                        unsafe { self.emit_expr(recv, locals) };
                        self.out.puts(b").p[_k].p, (\0".as_ptr());
                        if behind_ptr {
                            self.out.putc(b'*');
                        }
                        unsafe { self.emit_expr(recv, locals) };
                        self.out.puts(b").p[_k].n); } _j; })\0".as_ptr());
                        return;
                    }
                }
            }
        }
        /* .is_null() */
        if unsafe { z_eq(mname, mlen, b"is_null\0".as_ptr()) } && an == 0 {
            self.out.putc(b'(');
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b" == 0)\0".as_ptr());
            return;
        }
        /* `.into_iter()` — consuming identity on the subset's
         * containers: the iterator IS the container (index/cursor
         * loops), so emit the receiver parenthesized. */
        if an == 0 && unsafe { z_eq(mname, mlen, b"into_iter\0".as_ptr()) } {
            let rb = self.arena_tmp();
            let rl = unsafe { self.expr_ctype(recv, rb, 128, locals) };
            let is_cont = (rl > 8 && unsafe { z_eq(rb, 8, b"rsx_vec_\0".as_ptr()) })
                || (rl > 8 && unsafe { z_eq(rb, 8, b"rsx_arr_\0".as_ptr()) })
                || (rl == 9 && unsafe { z_eq(rb, 9, b"rsx_str_t\0".as_ptr()) })
                || (rl == 13 && unsafe { z_eq(rb, 13, b"rsx_str_ref_t\0".as_ptr()) });
            if is_cont {
                self.out.putc(b'(');
                unsafe { self.emit_expr(recv, locals) };
                self.out.putc(b')');
                return;
            }
        }
        /* `x.rsplit(SEP).next()` — the last-segment view: scan for the
         * last SEP byte, yield Option<&str> (rsx_opt_rsx_str_ref_t):
         * Some({p = x.p + i + 1, n = x.n - i - 1}) when found and the
         * tail non-empty, the zero struct otherwise. The receiver types
         * the fat ref; the separator arg is a char literal. */
        if an == 0 && unsafe { z_eq(mname, mlen, b"next\0".as_ptr()) } {
            if unsafe { (*recv).kind } == pm_jit_rsx_ast_kind::METHOD_CALL
                && unsafe { (*recv).n_kids } as usize >= 3
            {
                let rname = unsafe { *(*recv).kids.add(1) };
                let rkids = unsafe { (*recv).kids };
                if unsafe { (*rname).text_len } == 6
                    && unsafe { z_eq(unsafe { (*rname).text }, 6, b"rsplit\0".as_ptr()) }
                {
                    let base = unsafe { *rkids.add(0) };
                    let rargs = unsafe { *rkids.add(2) };
                    let sb = self.arena_tmp();
                    let sl = unsafe { self.expr_ctype(base, sb, 128, locals) };
                    if sl == 13 && unsafe { z_eq(sb, 13, b"rsx_str_ref_t\0".as_ptr()) } {
                        /* separator: a char literal 'c' — emit its byte */
                        let sk = unsafe { (*rargs).kids };
                        let sn = unsafe { (*rargs).n_kids } as usize;
                        if sn >= 1 {
                            let sep = unsafe { *sk.add(0) };
                            if unsafe { (*sep).kind } == pm_jit_rsx_ast_kind::LITERAL {
                                let st = unsafe { (*sep).text };
                                let stl = unsafe { (*sep).text_len };
                                if stl >= 3 && unsafe { *st } == b'\'' {
                                    let ch = unsafe { *st.add(1) };
                                    self.str_ref_used = true;
                                    /* the ENCODED Option typedef name — same
                                     * encoder as every Option row, so the
                                     * Some-bind reads the payload back. */
                                    let _ = unsafe { self.opt_add(b"rsx_str_ref_t\0".as_ptr(), 13) };
                                    let tdn = self.name_tmp(160);
                                    let tdn_len = unsafe {
                                        Lower::opt_typedef_name(
                                            b"rsx_str_ref_t\0".as_ptr(),
                                            13,
                                            tdn,
                                            160,
                                        )
                                    };
                                    if tdn_len == 0 {
                                        unsafe {
                                            self.err(
                                                b"internal: option typedef name too long\0".as_ptr(),
                                                unsafe { (*e).line },
                                            );
                                        }
                                        return;
                                    }
                                    self.out.puts(b"({ \0".as_ptr());
                                    self.out.put(tdn, tdn_len);
                                    self.out.puts(b" _o = {0}; for (size_t _i = (\0".as_ptr());
                                    unsafe { self.emit_expr(base, locals) };
                                    self.out.puts(b").n; _i-- > 0; ) { if ((\0".as_ptr());
                                    unsafe { self.emit_expr(base, locals) };
                                    self.out.puts(b").p[_i] == '\0".as_ptr());
                                    self.out.putc(ch);
                                    self.out.puts(b"') { _o._v.p = (\0".as_ptr());
                                    unsafe { self.emit_expr(base, locals) };
                                    self.out.puts(b").p + _i + 1; _o._v.n = (\0".as_ptr());
                                    unsafe { self.emit_expr(base, locals) };
                                    self.out.puts(b").n - _i - 1; _o._has = _o._v.n != 0; break; } } _o; })\0".as_ptr());
                                    return;
                                }
                            }
                        }
                    }
                }
            }
        }
        /* ---- &str view methods (receiver rsx_str_ref_t) ----
         *
         * trim/trim_end/trim_start: a tighter {p,n} window over the same
         * bytes — a statement-expr computing the trimmed view in a
         * block-local _t. starts_with/ends_with: length + memcmp against
         * the literal. find('c'): the struct Option<usize> scan.
         * strip_prefix/strip_suffix: struct Option<&str>. Every gate is
         * the receiver's C type (rsx_str_ref_t), never the name alone. */
        {
            let is_trim = an == 0
                && ((mlen == 4 && unsafe { z_eq(mname, mlen, b"trim\0".as_ptr()) })
                    || (mlen == 8 && unsafe { z_eq(mname, mlen, b"trim_end\0".as_ptr()) })
                    || (mlen == 10 && unsafe { z_eq(mname, mlen, b"trim_start\0".as_ptr()) }));
            let is_pred = an == 1
                && ((mlen == 11 && unsafe { z_eq(mname, mlen, b"starts_with\0".as_ptr()) })
                    || (mlen == 9 && unsafe { z_eq(mname, mlen, b"ends_with\0".as_ptr()) }));
            let is_find = an == 1
                && ((mlen == 4 && unsafe { z_eq(mname, mlen, b"find\0".as_ptr()) })
                    || (mlen == 5 && unsafe { z_eq(mname, mlen, b"rfind\0".as_ptr()) }));
            let is_rfind = an == 1
                && mlen == 5
                && unsafe { z_eq(mname, mlen, b"rfind\0".as_ptr()) };
            let is_strip = an == 1
                && ((mlen == 12 && unsafe { z_eq(mname, mlen, b"strip_prefix\0".as_ptr()) })
                    || (mlen == 12 && unsafe { z_eq(mname, mlen, b"strip_suffix\0".as_ptr()) }));
            let is_sonce = an == 1
                && ((mlen == 10 && unsafe { z_eq(mname, mlen, b"split_once\0".as_ptr()) })
                    || (mlen == 11 && unsafe { z_eq(mname, mlen, b"rsplit_once\0".as_ptr()) }));
            if is_trim || is_pred || is_find || is_strip || is_sonce {
                let rb = self.arena_tmp();
                let rl = unsafe { self.expr_ctype(recv, rb, 128, locals) };
                /* an owned String borrows to the same view: rsx_str_t's
                 * p/n pair is the identical layout, so the window/memcmp
                 * math below applies unchanged to either receiver. */
                let recv_is_view = (rl == 13 && unsafe { z_eq(rb, 13, b"rsx_str_ref_t\0".as_ptr()) })
                    || (rl == 9 && unsafe { z_eq(rb, 9, b"rsx_str_t\0".as_ptr()) });
                if recv_is_view {
                    if is_trim {
                        self.str_ref_used = true;
                        self.str_own_used = true;
                        self.out.puts(b"({ rsx_str_ref_t _t; _t.p = (\0".as_ptr());
                        unsafe { self.emit_expr(recv, locals) };
                        self.out.puts(b").p; _t.n = (\0".as_ptr());
                        unsafe { self.emit_expr(recv, locals) };
                        self.out.puts(b").n;\0".as_ptr());
                        if mlen == 4 || mlen == 10 {
                            /* leading edge (trim, trim_start) */
                            self.out.puts(b" while (_t.n > 0 && (_t.p[0] == ' ' || _t.p[0] == '\\t' || _t.p[0] == '\\r' || _t.p[0] == '\\n')) { _t.p++; _t.n--; }\0".as_ptr());
                        }
                        if mlen == 4 || mlen == 8 {
                            /* trailing edge (trim, trim_end) */
                            self.out.puts(b" while (_t.n > 0 && (_t.p[_t.n - 1] == ' ' || _t.p[_t.n - 1] == '\\t' || _t.p[_t.n - 1] == '\\r' || _t.p[_t.n - 1] == '\\n')) { _t.n--; }\0".as_ptr());
                        }
                        self.out.puts(b" _t; })\0".as_ptr());
                        return;
                    }
                    let arg0 = unsafe { *ak.add(0) };
                    if is_pred {
                        /* char-literal arg: a one-byte prefix/suffix test */
                        if unsafe { (*arg0).kind } == pm_jit_rsx_ast_kind::LITERAL
                            && unsafe { *(*arg0).text } == b'\''
                        {
                            let ct2 = unsafe { (*arg0).text };
                            let ctl2 = unsafe { (*arg0).text_len };
                            if ctl2 >= 3 {
                                let ch = unsafe { *ct2.add(1) };
                                self.str_ref_used = true;
                                let is_starts = mlen == 11;
                                if is_starts {
                                    self.out.puts(b"(((\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.puts(b").n >= 1) && ((\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.puts(b").p[0] == '\0".as_ptr());
                                    self.out.putc(ch);
                                    self.out.puts(b"'))\0".as_ptr());
                                } else {
                                    self.out.puts(b"(((\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.puts(b").n >= 1) && ((\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.puts(b").p[(\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.puts(b").n - 1] == '\0".as_ptr());
                                    self.out.putc(ch);
                                    self.out.puts(b"'))\0".as_ptr());
                                }
                                return;
                            }
                        }
                        /* literal arg only — the length is in the text */
                        if unsafe { (*arg0).kind } == pm_jit_rsx_ast_kind::LITERAL
                            && unsafe { *(*arg0).text } == b'"'
                        {
                            self.str_ref_used = true;
                            let at = unsafe { (*arg0).text };
                            let atl = unsafe { (*arg0).text_len };
                            /* inner byte length: text minus the two quotes */
                            let inner = if atl >= 2 { atl - 2 } else { 0 };
                            let is_starts = mlen == 11;
                            self.out.puts(b"((\0".as_ptr());
                            unsafe { self.emit_expr(recv, locals) };
                            self.out.puts(b").n >= \0".as_ptr());
                            self.out.put_u32(inner as u32);
                            if is_starts {
                                self.out.puts(b" && !memcmp((\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p, \0".as_ptr());
                                self.out.put(at, atl);
                                self.out.puts(b", \0".as_ptr());
                                self.out.put_u32(inner as u32);
                                self.out.puts(b"))\0".as_ptr());
                            } else {
                                self.out.puts(b" && !memcmp((\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p + (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").n - \0".as_ptr());
                                self.out.put_u32(inner as u32);
                                self.out.puts(b", \0".as_ptr());
                                self.out.put(at, atl);
                                self.out.puts(b", \0".as_ptr());
                                self.out.put_u32(inner as u32);
                                self.out.puts(b"))\0".as_ptr());
                            }
                            return;
                        }
                    }
                    if is_find {
                        if unsafe { (*arg0).kind } == pm_jit_rsx_ast_kind::LITERAL {
                            let st = unsafe { (*arg0).text };
                            let stl = unsafe { (*arg0).text_len };
                            if stl >= 3 && unsafe { *st } == b'\'' {
                                let ch = unsafe { *st.add(1) };
                                self.str_ref_used = true;
                                let _ = unsafe { self.opt_add(b"size_t\0".as_ptr(), 6) };
                                let tdn = self.name_tmp(160);
                                let tdn_len = unsafe {
                                    Lower::opt_typedef_name(b"size_t\0".as_ptr(), 6, tdn, 160)
                                };
                                if tdn_len == 0 {
                                    unsafe {
                                        self.err(
                                            b"internal: option typedef name too long\0".as_ptr(),
                                            unsafe { (*e).line },
                                        );
                                    }
                                    return;
                                }
                                if is_rfind {
                                    /* reverse scan: start at the last byte */
                                    self.out.puts(b"({ \0".as_ptr());
                                    self.out.put(tdn, tdn_len);
                                    self.out.puts(b" _o = {0}; for (size_t _i = (\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.puts(b").n; _i-- > 0;) { if ((\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.puts(b").p[_i] == '\0".as_ptr());
                                    self.out.putc(ch);
                                    self.out.puts(b"') { _o._v = _i; _o._has = 1; break; } } _o; })\0".as_ptr());
                                } else {
                                self.out.puts(b"({ \0".as_ptr());
                                self.out.put(tdn, tdn_len);
                                self.out.puts(b" _o = {0}; for (size_t _i = 0; _i < (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").n; _i++) { if ((\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p[_i] == '\0".as_ptr());
                                self.out.putc(ch);
                                self.out.puts(b"') { _o._v = _i; _o._has = 1; break; } } _o; })\0".as_ptr());
                                }
                                return;
                            }
                        }
                        /* find(&str) — the needle is a string literal or a
                         * &str-typed expr (a MARK-style const): a memcmp
                         * window scan, Option<usize> of the first (resp.
                         * last) byte index where the needle fits. */
                        {
                            /* literal needle: the bytes + the exact inner
                             * length (the lexer's text keeps the quotes). */
                            let mut lit_ptr: *const u8 = core::ptr::null_mut();
                            let mut lit_len = 0usize;
                            if unsafe { (*arg0).kind } == pm_jit_rsx_ast_kind::LITERAL
                                && unsafe { *(*arg0).text } == b'"'
                            {
                                lit_ptr = unsafe { (*arg0).text };
                                lit_len = unsafe { (*arg0).text_len } - 2;
                            }
                            if !lit_ptr.is_null() {
                                self.str_ref_used = true;
                                let _ = unsafe { self.opt_add(b"size_t\0".as_ptr(), 6) };
                                let tdn = self.name_tmp(160);
                                let tdn_len = unsafe {
                                    Lower::opt_typedef_name(b"size_t\0".as_ptr(), 6, tdn, 160)
                                };
                                if tdn_len == 0 {
                                    unsafe {
                                        self.err(
                                            b"internal: option typedef name too long\0".as_ptr(),
                                            unsafe { (*e).line },
                                        );
                                    }
                                    return;
                                }
                                if is_rfind {
                                    self.out.puts(b"({ \0".as_ptr());
                                    self.out.put(tdn, tdn_len);
                                    self.out.puts(b" _o = {0}; for (size_t _i = (\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.puts(b").n; _i-- > 0;) { if (\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.put_u32(lit_len as u32);
                                    self.out.puts(b" <= _i && !memcmp((\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.puts(b").p + _i, \0".as_ptr());
                                    self.out.put(lit_ptr, lit_len + 2);
                                    self.out.puts(b", \0".as_ptr());
                                    self.out.put_u32(lit_len as u32);
                                    self.out.puts(b") { _o._v = _i; _o._has = 1; break; } } _o; })\0".as_ptr());
                                } else {
                                    self.out.puts(b"({ \0".as_ptr());
                                    self.out.put(tdn, tdn_len);
                                    self.out.puts(b" _o = {0}; if ((\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.puts(b").n >= \0".as_ptr());
                                    self.out.put_u32(lit_len as u32);
                                    self.out.puts(b") { for (size_t _i = 0; _i + \0".as_ptr());
                                    self.out.put_u32(lit_len as u32);
                                    self.out.puts(b" <= (\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.puts(b").n; _i++) { if (!memcmp((\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.puts(b").p + _i, \0".as_ptr());
                                    self.out.put(lit_ptr, lit_len + 2);
                                    self.out.puts(b", \0".as_ptr());
                                    self.out.put_u32(lit_len as u32);
                                    self.out.puts(b")) { _o._v = _i; _o._has = 1; break; } } } _o; })\0".as_ptr());
                                }
                                return;
                            }
                        }
                        {
                            let nb = self.arena_tmp();
                            let nl = unsafe { self.expr_ctype(arg0, nb, 128, locals) };
                            let needle_is_view = (nl == 13
                                && unsafe { z_eq(nb, 13, b"rsx_str_ref_t\0".as_ptr()) })
                                || (nl == 9 && unsafe { z_eq(nb, 9, b"rsx_str_t\0".as_ptr()) });
                            if needle_is_view {
                                self.str_ref_used = true;
                                let _ = unsafe { self.opt_add(b"size_t\0".as_ptr(), 6) };
                                let tdn = self.name_tmp(160);
                                let tdn_len = unsafe {
                                    Lower::opt_typedef_name(b"size_t\0".as_ptr(), 6, tdn, 160)
                                };
                                if tdn_len == 0 {
                                    unsafe {
                                        self.err(
                                            b"internal: option typedef name too long\0".as_ptr(),
                                            unsafe { (*e).line },
                                        );
                                    }
                                    return;
                                }
                                if is_rfind {
                                    self.out.puts(b"({ \0".as_ptr());
                                    self.out.put(tdn, tdn_len);
                                    self.out.puts(b" _o = {0}; for (size_t _i = (\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.puts(b").n; _i-- > 0;) { if (\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.puts(b").n - _i >= (\0".as_ptr());
                                    unsafe { self.emit_expr(arg0, locals) };
                                    self.out.puts(b").n && !memcmp((\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.puts(b").p + _i, (\0".as_ptr());
                                    unsafe { self.emit_expr(arg0, locals) };
                                    self.out.puts(b").p, (\0".as_ptr());
                                    unsafe { self.emit_expr(arg0, locals) };
                                    self.out.puts(b").n)) { _o._v = _i; _o._has = 1; break; } } _o; })\0".as_ptr());
                                } else {
                                    self.out.puts(b"({ \0".as_ptr());
                                    self.out.put(tdn, tdn_len);
                                    self.out.puts(b" _o = {0}; if ((\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.puts(b").n >= (\0".as_ptr());
                                    unsafe { self.emit_expr(arg0, locals) };
                                    self.out.puts(b").n) { for (size_t _i = 0; _i + (\0".as_ptr());
                                    unsafe { self.emit_expr(arg0, locals) };
                                    self.out.puts(b").n <= (\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.puts(b").n; _i++) { if (!memcmp((\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.puts(b").p + _i, (\0".as_ptr());
                                    unsafe { self.emit_expr(arg0, locals) };
                                    self.out.puts(b").p, (\0".as_ptr());
                                    unsafe { self.emit_expr(arg0, locals) };
                                    self.out.puts(b").n)) { _o._v = _i; _o._has = 1; break; } } } _o; })\0".as_ptr());
                                }
                                return;
                            }
                        }
                    }
                    if is_sonce {
                        /* split_once(c)/rsplit_once(c) -> Option<(&str,&str)>:
                         * a stmt-expr scanning for the first (resp. last)
                         * sep byte; found -> the {before, after} pair as
                         * rsx_strpair_t, else the zero struct. The typing
                         * pass interned the Option row — reintern here to
                         * render the same typedef name. */
                        if unsafe { (*arg0).kind } == pm_jit_rsx_ast_kind::LITERAL
                            && unsafe { *(*arg0).text } == b'\''
                        {
                            let ct2 = unsafe { (*arg0).text };
                            let ctl2 = unsafe { (*arg0).text_len };
                            let ch = if ctl2 >= 3 { unsafe { *ct2.add(1) } } else { 0 };
                            let _ = unsafe { self.opt_add(b"rsx_strpair_t\0".as_ptr(), 13) };
                            let otdn = self.name_tmp(160);
                            let otdn_len = unsafe {
                                Lower::opt_typedef_name(b"rsx_strpair_t\0".as_ptr(), 13, otdn, 160)
                            };
                            if otdn_len > 0 && otdn_len < 160 {
                                self.str_ref_used = true;
                                self.strpair_used = true;
                                let is_first = mlen == 10;
                                self.out.puts(b"({ \0".as_ptr());
                                self.out.put(otdn, otdn_len);
                                self.out.puts(b" _o = {0}; size_t _i = \0".as_ptr());
                                if is_first {
                                    self.out.puts(b"(size_t)-1; for (_i = 0; _i < (\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.puts(b").n; _i++) { if ((\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.puts(b").p[_i] == '\0".as_ptr());
                                    self.out.putc(ch);
                                    self.out.puts(b"') { break; } } \0".as_ptr());
                                } else {
                                    self.out.puts(b"(\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.puts(b").n; for (; _i-- > 0;) { if ((\0".as_ptr());
                                    unsafe { self.emit_expr(recv, locals) };
                                    self.out.puts(b").p[_i] == '\0".as_ptr());
                                    self.out.putc(ch);
                                    self.out.puts(b"') { break; } } \0".as_ptr());
                                }
                                self.out.puts(b" if (_i < (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").n) { _o._has = 1; _o._v._0.p = (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p; _o._v._0.n = _i; _o._v._1.p = (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p + _i + 1; _o._v._1.n = (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").n - _i - 1; } _o; })\0".as_ptr());
                                return;
                            }
                        }
                    }
                    if is_strip {
                        /* expression arg (&str-typed): the runtime prefix/
                         * suffix test — memcmp against the arg's own
                         * (p,n) pair (a String arg borrows to the same
                         * view). Falls through to the literal branches
                         * when the arg does not type as a view. */
                        let pb = self.arena_tmp();
                        let pl = unsafe { self.expr_ctype(arg0, pb, 128, locals) };
                        let arg_is_view = (pl == 13
                            && unsafe { z_eq(pb, 13, b"rsx_str_ref_t\0".as_ptr()) })
                            || (pl == 9 && unsafe { z_eq(pb, 9, b"rsx_str_t\0".as_ptr()) });
                        if arg_is_view {
                            self.str_ref_used = true;
                            let _ = unsafe { self.opt_add(b"rsx_str_ref_t\0".as_ptr(), 13) };
                            let tdn = self.name_tmp(160);
                            let tdn_len = unsafe {
                                Lower::opt_typedef_name(b"rsx_str_ref_t\0".as_ptr(), 13, tdn, 160)
                            };
                            if tdn_len == 0 {
                                unsafe {
                                    self.err(
                                        b"internal: option typedef name too long\0".as_ptr(),
                                        unsafe { (*e).line },
                                    );
                                }
                                return;
                            }
                            self.out.puts(b"({ \0".as_ptr());
                            self.out.put(tdn, tdn_len);
                            let is_pfx_e = mlen == 12 && unsafe { *mname.add(6) == b'p' };
                            self.out.puts(b" _o = {0}; if ((\0".as_ptr());
                            unsafe { self.emit_expr(recv, locals) };
                            self.out.puts(b").n >= (\0".as_ptr());
                            unsafe { self.emit_expr(arg0, locals) };
                            self.out.puts(b").n && (\0".as_ptr());
                            unsafe { self.emit_expr(arg0, locals) };
                            self.out.puts(b").n > 0 && !memcmp((\0".as_ptr());
                            if is_pfx_e {
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p, (\0".as_ptr());
                                unsafe { self.emit_expr(arg0, locals) };
                                self.out.puts(b").p, (\0".as_ptr());
                                unsafe { self.emit_expr(arg0, locals) };
                                self.out.puts(b").n)) { _o._v.p = (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p + (\0".as_ptr());
                                unsafe { self.emit_expr(arg0, locals) };
                                self.out.puts(b").n; _o._v.n = (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").n - (\0".as_ptr());
                                unsafe { self.emit_expr(arg0, locals) };
                                self.out.puts(b").n; _o._has = 1; } _o; })\0".as_ptr());
                            } else {
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p + (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").n - (\0".as_ptr());
                                unsafe { self.emit_expr(arg0, locals) };
                                self.out.puts(b").n, (\0".as_ptr());
                                unsafe { self.emit_expr(arg0, locals) };
                                self.out.puts(b").p, (\0".as_ptr());
                                unsafe { self.emit_expr(arg0, locals) };
                                self.out.puts(b").n)) { _o._v.p = (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p; _o._v.n = (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").n - (\0".as_ptr());
                                unsafe { self.emit_expr(arg0, locals) };
                                self.out.puts(b").n; _o._has = 1; } _o; })\0".as_ptr());
                            }
                            return;
                        }
                        /* char-literal arg: a one-byte strip — the same
                         * memcmp shape with the char spelled as a string
                         * of length 1. */
                        if unsafe { (*arg0).kind } == pm_jit_rsx_ast_kind::LITERAL
                            && unsafe { *(*arg0).text } == b'\''
                        {
                            let ct = unsafe { (*arg0).text };
                            let ctl = unsafe { (*arg0).text_len };
                            /* the char byte: first byte inside the quotes
                             * ('x' -> x; escapes share Rust/C spelling) */
                            let ch = if ctl >= 3 { unsafe { *ct.add(1) } } else { 0 };
                            self.str_ref_used = true;
                            let _ = unsafe { self.opt_add(b"rsx_str_ref_t\0".as_ptr(), 13) };
                            let tdn = self.name_tmp(160);
                            let tdn_len = unsafe {
                                Lower::opt_typedef_name(b"rsx_str_ref_t\0".as_ptr(), 13, tdn, 160)
                            };
                            if tdn_len == 0 {
                                unsafe {
                                    self.err(
                                        b"internal: option typedef name too long\0".as_ptr(),
                                        unsafe { (*e).line },
                                    );
                                }
                                return;
                            }
                            self.out.puts(b"({ \0".as_ptr());
                            self.out.put(tdn, tdn_len);
                            let is_pfx_c = mlen == 12 && unsafe { *mname.add(6) == b'p' };
                            self.out.puts(b" _o = {0}; if ((\0".as_ptr());
                            unsafe { self.emit_expr(recv, locals) };
                            self.out.puts(b").n >= 1 && (\0".as_ptr());
                            if is_pfx_c {
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p[0] == '\0".as_ptr());
                                self.out.putc(ch);
                                self.out.puts(b"') { _o._v.p = (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p + 1; _o._v.n = (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").n - 1; _o._has = 1; } _o; })\0".as_ptr());
                            } else {
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p[(\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").n - 1] == '\0".as_ptr());
                                self.out.putc(ch);
                                self.out.puts(b"') { _o._v.p = (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p; _o._v.n = (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").n - 1; _o._has = 1; } _o; })\0".as_ptr());
                            }
                            return;
                        }
                        if unsafe { (*arg0).kind } == pm_jit_rsx_ast_kind::LITERAL
                            && unsafe { *(*arg0).text } == b'"'
                        {
                            self.str_ref_used = true;
                            let at = unsafe { (*arg0).text };
                            let atl = unsafe { (*arg0).text_len };
                            let inner = if atl >= 2 { atl - 2 } else { 0 };
                            let _ = unsafe { self.opt_add(b"rsx_str_ref_t\0".as_ptr(), 13) };
                            let tdn = self.name_tmp(160);
                            let tdn_len = unsafe {
                                Lower::opt_typedef_name(b"rsx_str_ref_t\0".as_ptr(), 13, tdn, 160)
                            };
                            if tdn_len == 0 {
                                unsafe {
                                    self.err(
                                        b"internal: option typedef name too long\0".as_ptr(),
                                        unsafe { (*e).line },
                                    );
                                }
                                return;
                            }
                            self.out.puts(b"({ \0".as_ptr());
                            self.out.put(tdn, tdn_len);
                            let is_pfx = mlen == 12 && unsafe { *mname.add(6) == b'p' };
                            if is_pfx {
                                self.out.puts(b" _o = {0}; if ((\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").n >= \0".as_ptr());
                                self.out.put_u32(inner as u32);
                                self.out.puts(b" && !memcmp((\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p, \0".as_ptr());
                                self.out.put(at, atl);
                                self.out.puts(b", \0".as_ptr());
                                self.out.put_u32(inner as u32);
                                self.out.puts(b")) { _o._v.p = (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p + \0".as_ptr());
                                self.out.put_u32(inner as u32);
                                self.out.puts(b"; _o._v.n = (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").n - \0".as_ptr());
                                self.out.put_u32(inner as u32);
                                self.out.puts(b"; _o._has = 1; } _o; })\0".as_ptr());
                            } else {
                                self.out.puts(b" _o = {0}; if ((\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").n >= \0".as_ptr());
                                self.out.put_u32(inner as u32);
                                self.out.puts(b" && !memcmp((\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p + (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").n - \0".as_ptr());
                                self.out.put_u32(inner as u32);
                                self.out.puts(b", \0".as_ptr());
                                self.out.put(at, atl);
                                self.out.puts(b", \0".as_ptr());
                                self.out.put_u32(inner as u32);
                                self.out.puts(b")) { _o._v.p = (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p; _o._v.n = (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").n - \0".as_ptr());
                                self.out.put_u32(inner as u32);
                                self.out.puts(b"; _o._has = 1; } _o; })\0".as_ptr());
                            }
                            return;
                        }
                    }
                }
            }
        }
        /* char predicates — `c.is_ascii_alphanumeric()` and friends on
         * the uint32_t char scalar: folded range tests (the C `x - lo
         * < span` idiom — one comparison, no libc). */
        if an == 0
            && ((mlen == 20
                && unsafe { z_eq(mname, mlen, b"is_ascii_alphanumeric\0".as_ptr()) })
                || (mlen == 14 && unsafe { z_eq(mname, mlen, b"is_ascii_digit\0".as_ptr()) })
                || (mlen == 19
                    && unsafe { z_eq(mname, mlen, b"is_ascii_alphabetic\0".as_ptr()) })
                || (mlen == 14 && unsafe { z_eq(mname, mlen, b"is_ascii_upper\0".as_ptr()) })
                || (mlen == 13 && unsafe { z_eq(mname, mlen, b"is_ascii_lower\0".as_ptr()) }))
        {
            let rb = self.arena_tmp();
            let rl = unsafe { self.expr_ctype(recv, rb, 128, locals) };
            if rl == 9 && unsafe { z_eq(rb, 9, b"uint32_t\0".as_ptr()) } {
                self.out.putc(b'(');
                if mlen == 20 {
                    self.out.puts(b"((unsigned)((\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b") - '0') < 10u || (unsigned)((\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b") - 'A') < 26u || (unsigned)((\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b") - 'a') < 26u)\0".as_ptr());
                } else if mlen == 14 && unsafe { z_eq(mname, mlen, b"is_ascii_digit\0".as_ptr()) } {
                    self.out.puts(b"((unsigned)((\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b") - '0') < 10u)\0".as_ptr());
                } else if mlen == 19 {
                    self.out.puts(b"((unsigned)((\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b") - 'A') < 26u || (unsigned)((\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b") - 'a') < 26u)\0".as_ptr());
                } else if mlen == 14 && unsafe { z_eq(mname, mlen, b"is_ascii_upper\0".as_ptr()) } {
                    self.out.puts(b"((unsigned)((\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b") - 'A') < 26u)\0".as_ptr());
                } else {
                    self.out.puts(b"((unsigned)((\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b") - 'a') < 26u)\0".as_ptr());
                }
                self.out.putc(b')');
                return;
            }
        }
        /* `.replace(from_char, to_lit)` — the substitution op: the
         * receiver's (p, n) view through the rsx_str_replace_ch helper,
         * a fresh owned String out. Both &str and String receivers. */
        if an == 2 && mlen == 7 && unsafe { z_eq(mname, mlen, b"replace\0".as_ptr()) } {
            let a0 = unsafe { *ak.add(0) };
            let a1 = unsafe { *ak.add(1) };
            let from_char = if unsafe { (*a0).kind } == pm_jit_rsx_ast_kind::LITERAL
                && unsafe { (*a0).text_len } >= 3
                && unsafe { *(*a0).text } == b'\''
            {
                unsafe { *(*a0).text.add(1) }
            } else {
                0u8
            };
            let to_lit = if unsafe { (*a1).kind } == pm_jit_rsx_ast_kind::LITERAL
                && unsafe { *(*a1).text } == b'"'
            {
                true
            } else {
                false
            };
            if from_char != 0 && to_lit {
                let rb = self.arena_tmp();
                let rl = unsafe { self.expr_ctype(recv, rb, 128, locals) };
                let is_str = (rl == 13 && unsafe { z_eq(rb, 13, b"rsx_str_ref_t\0".as_ptr()) })
                    || (rl == 9 && unsafe { z_eq(rb, 9, b"rsx_str_t\0".as_ptr()) });
                if is_str {
                    self.str_ref_used = true;
                    self.str_own_used = true;
                    let at = unsafe { (*a1).text };
                    let atl = unsafe { (*a1).text_len };
                    let inner = if atl >= 2 { atl - 2 } else { 0 };
                    self.out.puts(b"rsx_str_replace_ch(\0".as_ptr());
                    if rl == 9 && unsafe { z_eq(rb, 9, b"rsx_str_t\0".as_ptr()) } {
                        /* owned receiver: (p ? p : "", n) */
                        self.out.puts(b"(\0".as_ptr());
                        unsafe { self.emit_expr(recv, locals) };
                        self.out.puts(b").p ? (\0".as_ptr());
                        unsafe { self.emit_expr(recv, locals) };
                        self.out.puts(b").p : (const uint8_t *)\"\", (\0".as_ptr());
                        unsafe { self.emit_expr(recv, locals) };
                        self.out.puts(b").n, \0".as_ptr());
                    } else {
                        self.out.puts(b"(\0".as_ptr());
                        unsafe { self.emit_expr(recv, locals) };
                        self.out.puts(b").p, (\0".as_ptr());
                        unsafe { self.emit_expr(recv, locals) };
                        self.out.puts(b").n, \0".as_ptr());
                    }
                    self.out.putc(b'\'');
                    self.out.putc(from_char);
                    self.out.puts(b"', \0".as_ptr());
                    self.out.put(at, atl);
                    self.out.puts(b", \0".as_ptr());
                    self.out.put_u32(inner as u32);
                    self.out.puts(b")\0".as_ptr());
                    return;
                }
            }
        }
        /* Vec container ops — the receiver's rendered type names one of
         * the interned rsx_vec_<elem> typedefs. Gate on that (the method
         * name alone is not validation), then lower to the unit-static
         * helpers the preamble emitted. push takes the element by value
         * (v is &mut: pass the address); len/is_empty are reads. */
        if (an == 1 && unsafe { z_eq(mname, mlen, b"push\0".as_ptr()) })
            || (an == 0
                && (unsafe { z_eq(mname, mlen, b"len\0".as_ptr()) }
                    || unsafe { z_eq(mname, mlen, b"is_empty\0".as_ptr()) }))
            || (an == 0 && unsafe { z_eq(mname, mlen, b"free\0".as_ptr()) })
            || (an == 1 && unsafe { z_eq(mname, mlen, b"truncate\0".as_ptr()) })
            || (an == 0 && unsafe { z_eq(mname, mlen, b"sort\0".as_ptr()) })
            || (an == 0 && unsafe { z_eq(mname, mlen, b"dedup\0".as_ptr()) })
            || (an == 1 && unsafe { z_eq(mname, mlen, b"extend\0".as_ptr()) })
        {
            let rct = self.arena_tmp();
            let rctl = unsafe { self.expr_ctype(recv, rct, 128, locals) };
            if rctl > 8 && rctl < 128 && unsafe { z_eq(rct, 8, b"rsx_vec_\0".as_ptr()) } {
                /* the interned element spelling from the typedef name */
                if an == 1 && unsafe { z_eq(mname, mlen, b"push\0".as_ptr()) } {
                    /* the arg may need the deref coercion to the row's
                     * element: a Vec<&str> row (elem rsx_str_ref_t)
                     * pushed with an owned String (or its &String borrow)
                     * takes the FAT VIEW of the value, never the struct
                     * itself (a raw struct is a type mismatch; a `&s`
                     * would be a pointer). */
                    let vs = unsafe { self.vecs.find_by_name(rct, rctl) };
                    if vs < VEC_CAP {
                        let el = self.vecs.elem_lens[vs];
                        let eb = self.vecs.elems[vs].as_ptr();
                        if el == 13
                            && unsafe { z_eq(eb, 13, b"rsx_str_ref_t\0".as_ptr()) }
                        {
                            let ab = self.arena_tmp();
                            let a0 = unsafe { *ak.add(0) };
                            /* unwrap a `&`/`&mut` borrow on the arg */
                            let mut inner = a0;
                            let mut hops = 0;
                            while hops < 2
                                && unsafe { (*inner).kind } == pm_jit_rsx_ast_kind::UNARY
                                && unsafe { (*inner).n_kids } as usize >= 1
                                && (unsafe { z_eq(unsafe { (*inner).text }, unsafe { (*inner).text_len }, b"&\0".as_ptr()) }
                                    || unsafe { z_eq(unsafe { (*inner).text }, unsafe { (*inner).text_len }, b"&mut\0".as_ptr()) })
                            {
                                inner = unsafe { *(*inner).kids.add(0) };
                                hops += 1;
                            }
                            let al = unsafe { self.expr_ctype(inner, ab, 128, locals) };
                            if al == 9
                                && unsafe { z_eq(ab, 9, b"rsx_str_t\0".as_ptr()) }
                            {
                                self.str_ref_used = true;
                                self.out.put(rct, rctl);
                                self.out.puts(b"_push(&\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b", (rsx_str_ref_t){ (\0".as_ptr());
                                unsafe { self.emit_expr(inner, locals) };
                                self.out.puts(b").p, (\0".as_ptr());
                                unsafe { self.emit_expr(inner, locals) };
                                self.out.puts(b").n })\0".as_ptr());
                                return;
                            }
                        }
                    }
                    self.out.put(rct, rctl);
                    self.out.puts(b"_push(&\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b", \0".as_ptr());
                    unsafe { self.emit_expr(*ak.add(0), locals) };
                    self.out.puts(b")\0".as_ptr());
                    return;
                }
                /* extend(vec): append every element of the arg row — a
                 * statement-expr loop of _push. The arg types as the
                 * same rsx_vec_<elem> row (checked; a mismatched row
                 * refuses below rather than miscompile). */
                if an == 1 && unsafe { z_eq(mname, mlen, b"extend\0".as_ptr()) } {
                    let vb = self.arena_tmp();
                    let vl = unsafe { self.expr_ctype(*ak.add(0), vb, 128, locals) };
                    /* `&Vec<T>` (a `const rsx_vec_<row> *` param) auto-
                     * derefs to the row — the emitted `(*arg)` read is
                     * exactly the borrowed Vec. Strip a leading `const `
                     * before the pointer check. */
                    let mut vbase = 0usize;
                    if vl > 6
                        && unsafe { z_eq(vb, 6, b"const \0".as_ptr()) }
                    {
                        vbase = 6;
                    }
                    let vcore = vl - vbase;
                    /* row-prefix compare without expression-position lets
                     * (the self-hosting subset keeps lets at stmt level):
                     * one loop over both checks. */
                    let mut pref_ok = true;
                    if vcore == rctl + 2 && unsafe { *vb.add(vl - 1) } == b'*' {
                        let mut q3 = 0usize;
                        while q3 < rctl {
                            if unsafe { *vb.add(vbase + q3) } != unsafe { *rct.add(q3) } {
                                pref_ok = false;
                                break;
                            }
                            q3 += 1;
                        }
                    } else {
                        pref_ok = false;
                    }
                    let arg_is_ref = pref_ok;
                    let mut same_row = vl == rctl;
                    if same_row {
                        let mut q2 = 0usize;
                        while q2 < vl {
                            if unsafe { *vb.add(q2) } != unsafe { *rct.add(q2) } {
                                same_row = false;
                                break;
                            }
                            q2 += 1;
                        }
                    }
                    let row_ok = same_row;
                    if row_ok || arg_is_ref {
                        let argref: *const u8 = if row_ok {
                            b"\0".as_ptr()
                        } else {
                            b"*\0".as_ptr()
                        };
                        self.out.puts(b"{ size_t _k; for (_k = 0; _k < (\0".as_ptr());
                        if !row_ok {
                            self.out.puts(b"*\0".as_ptr());
                        }
                        unsafe { self.emit_expr(*ak.add(0), locals) };
                        self.out.puts(b").n; _k++) { \0".as_ptr());
                        self.out.put(rct, rctl);
                        self.out.puts(b"_push(&\0".as_ptr());
                        unsafe { self.emit_expr(recv, locals) };
                        self.out.puts(b", (\0".as_ptr());
                        if !row_ok {
                            self.out.puts(b"*\0".as_ptr());
                        }
                        unsafe { self.emit_expr(*ak.add(0), locals) };
                        self.out.puts(b").p[_k]); } }\0".as_ptr());
                        let _ = argref;
                        return;
                    }
                }
                /* truncate(n): keep the first min(n, v.n) elements — the
                 * length drop IS the whole op (the slab keeps its
                 * allocation, matching Rust Vec::truncate). A by-value
                 * statement-expr receiver would drop the write, so the
                 * expression form is only sound on a place; gen's use is
                 * exactly a let-bound local. */
                if an == 1 && unsafe { z_eq(mname, mlen, b"truncate\0".as_ptr()) } {
                    self.out.puts(b"{ size_t _tn = \0".as_ptr());
                    unsafe { self.emit_expr(*ak.add(0), locals) };
                    self.out.puts(b"; if ((size_t)(\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b").n > _tn) { (\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b").n = _tn; } }\0".as_ptr());
                    return;
                }
                if an == 0 && unsafe { z_eq(mname, mlen, b"len\0".as_ptr()) } {
                    self.out.puts(b"(\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b").n\0".as_ptr());
                    return;
                }
                if an == 0 && unsafe { z_eq(mname, mlen, b"is_empty\0".as_ptr()) } {
                    self.out.puts(b"((\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b").n == 0)\0".as_ptr());
                    return;
                }
                if an == 0 && unsafe { z_eq(mname, mlen, b"free\0".as_ptr()) } {
                    self.out.put(rct, rctl);
                    self.out.puts(b"_free(&\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b")\0".as_ptr());
                    return;
                }
                /* sort()/dedup(): in-place over the slab, GNU statement
                 * expr. String elements compare lexicographically via
                 * strcmp on .p (gen's Vec<String> sorts); other element
                 * types refuse here rather than miscompile. */
                if an == 0
                    && (unsafe { z_eq(mname, mlen, b"sort\0".as_ptr()) }
                        || unsafe { z_eq(mname, mlen, b"dedup\0".as_ptr()) })
                {
                    let vs = unsafe { self.vecs.find_by_name(rct, rctl) };
                    let el = if vs < VEC_CAP { self.vecs.elem_lens[vs] } else { 0 };
                    if el == 9 && vs < VEC_CAP {
                        let elb = self.arena_tmp();
                        let mut w = 0usize;
                        while w < 9 {
                            unsafe {
                                *elb.add(w) = self.vecs.elems[vs][w];
                            }
                            w += 1;
                        }
                        let is_str = unsafe { z_eq(elb, 9, b"rsx_str_t\0".as_ptr()) };
                        if is_str {
                            if unsafe { z_eq(mname, mlen, b"sort\0".as_ptr()) } {
                                self.out.puts(b"({ size_t _i,_j; for (_i=1;_i<(\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").n;_i++) { rsx_str_t _t=(\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p[_i]; _j=_i; while (_j>0 && strcmp((\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p[_j-1].p,_t.p)>0) { (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p[_j]=(\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p[_j-1]; _j--; } (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p[_j]=_t; } 0; })\0".as_ptr());
                            } else {
                                self.out.puts(b"({ size_t _w=1,_r; if ((\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").n>0) { for (_r=1;_r<(\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").n;_r++) { if (strcmp((\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p[_r].p,(\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p[_r-1].p)!=0) { (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p[_w++]=(\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p[_r]; } } (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").n=_w; } 0; })\0".as_ptr());
                            }
                            return;
                        }
                    }
                    unsafe {
                        self.err(b"unsupported: sort/dedup on non-String Vec elements\0".as_ptr(), unsafe { (*e).line });
                    }
                    return;
                }
            }
        }
        /* BTreeMap method plane — the receiver's rendered type names one
         * of the interned rsx_btm_<row> typedefs; the unit-static ops the
         * preamble emitted take the map by address. insert is the plain
         * op call (void, statement position); get yields the payload
         * POINTER (NULL = absent) — the pointer-Option convention the
         * .map/.cloned combinators wrap. */
        if (an == 2 && unsafe { z_eq(mname, mlen, b"insert\0".as_ptr()) })
            || (an == 1 && unsafe { z_eq(mname, mlen, b"get\0".as_ptr()) })
            || (an == 0 && (unsafe { z_eq(mname, mlen, b"len\0".as_ptr()) }
                || unsafe { z_eq(mname, mlen, b"is_empty\0".as_ptr()) }))
        {
            let rct = self.arena_tmp();
            let rctl = unsafe { self.expr_ctype(recv, rct, 128, locals) };
            if rctl > 8 && rctl < 128 && unsafe { z_eq(rct, 8, b"rsx_btm_\0".as_ptr()) } {
                if an == 2 && unsafe { z_eq(mname, mlen, b"insert\0".as_ptr()) } {
                    self.out.put(rct, rctl);
                    self.out.puts(b"_insert(&\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b", \0".as_ptr());
                    unsafe { self.emit_expr(*ak.add(0), locals) };
                    self.out.puts(b", \0".as_ptr());
                    unsafe { self.emit_expr(*ak.add(1), locals) };
                    self.out.puts(b")\0".as_ptr());
                    return;
                }
                if an == 1 && unsafe { z_eq(mname, mlen, b"get\0".as_ptr()) } {
                    self.out.put(rct, rctl);
                    self.out.puts(b"_get(&\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b", \0".as_ptr());
                    unsafe { self.emit_expr(*ak.add(0), locals) };
                    self.out.putc(b')');
                    return;
                }
                if an == 0 && unsafe { z_eq(mname, mlen, b"len\0".as_ptr()) } {
                    self.out.putc(b'(');
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b").n\0".as_ptr());
                    return;
                }
                if an == 0 && unsafe { z_eq(mname, mlen, b"is_empty\0".as_ptr()) } {
                    self.out.putc(b'(');
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b").n == 0\0".as_ptr());
                    return;
                }
            }
        }
        /* `.cloned()` on a btm `.get(k)` — the get's payload pointer
         * materialized as the struct-Option of the payload row. The
         * pointer is bound once in a stmt-expr (the get is pure but
         * re-emitting the receiver twice would double the lookup). */
        if an == 0 && mlen == 6 && unsafe { z_eq(mname, mlen, b"cloned\0".as_ptr()) } {
            if unsafe { (*recv).kind } == pm_jit_rsx_ast_kind::METHOD_CALL
                && (unsafe { (*recv).n_kids } as usize) >= 3
            {
                let rk = unsafe { (*recv).kids };
                let rname = unsafe { *rk.add(1) };
                if unsafe { (*rname).text_len } == 3
                    && unsafe { z_eq(unsafe { (*rname).text }, 3, b"get\0".as_ptr()) }
                {
                    let grb = self.arena_tmp();
                    let grl = unsafe { self.expr_ctype(recv, grb, 128, locals) };
                    if grl > 2
                        && unsafe { *grb.add(grl - 1) } == b'*'
                        && grl < 126
                    {
                        /* payload spelling without the trailing ` *` */
                        let vl = grl - 2;
                        /* opt row + typedef name */
                        let slot = unsafe { self.opt_add(grb, vl) };
                        if slot < OPT_CAP {
                            let on = self.arena_tmp();
                            let onl = unsafe { Lower::opt_typedef_name(grb, vl, on, 160) };
                            if onl > 0 {
                                self.out.puts(b"({ \0".as_ptr());
                                self.out.put(grb, grl);
                                self.out.puts(b" _g = \0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b"; \0".as_ptr());
                                self.out.put(on, onl);
                                self.out.puts(b" _o; _o._has = (_g != 0); if (_g) { _o._v = (*_g); } else { _o._v = (\0".as_ptr());
                                self.out.put(grb, vl);
                                self.out.puts(b"){0}; } _o; })\0".as_ptr());
                                return;
                            }
                        }
                    }
                }
            }
        }
        /* `.as_slice()` on a Vec row (or a &Vec row behind a pointer):
         * the fat &[T] view of the container — (rsx_arr_<row>){ .p, .n }.
         * The receiver emits once (the borrow is the same buffer). */
        if an == 0 && mlen == 8 && unsafe { z_eq(mname, mlen, b"as_slice\0".as_ptr()) } {
            let rct = self.arena_tmp();
            let rctl = unsafe { self.expr_ctype(recv, rct, 128, locals) };
            let mut base = rct;
            let mut basel = rctl;
            /* `&Vec<T>` — strip `const `/`const ` + pointer */
            if rctl > 6 && unsafe { z_eq(rct, 6, b"const \0".as_ptr()) } {
                base = unsafe { base.add(6) };
                basel -= 6;
            }
            if basel > 2
                && unsafe { *base.add(basel - 1) } == b'*'
                && basel > 8
                && unsafe { z_eq(base, 8, b"rsx_vec_\0".as_ptr()) }
            {
                basel -= 1;
                while basel > 0 && unsafe { *base.add(basel - 1) } == b' ' {
                    basel -= 1;
                }
                let vs = unsafe { self.vecs.find_by_name(base, basel) };
                if vs < VEC_CAP {
                    /* the element spelling from the row */
                    let ep = self.vecs.elems[vs].as_ptr();
                    let el = self.vecs.elem_lens[vs];
                    let row = unsafe { self.arrs.intern(ep, el) };
                    if row < ARR_CAP {
                        let an2 = self.arena_tmp();
                        let an2l = unsafe { ArrTab::name_for(row, an2, 96) };
                        if an2l > 0 {
                            self.out.puts(b"(\0".as_ptr());
                            self.out.put(an2, an2l);
                            self.out.puts(b"){ (const \0".as_ptr());
                            self.out.put(ep, el);
                            self.out.puts(b" *)(\0".as_ptr());
                            unsafe { self.emit_expr(recv, locals) };
                            self.out.puts(b")->p, (\0".as_ptr());
                            unsafe { self.emit_expr(recv, locals) };
                            self.out.puts(b")->n }\0".as_ptr());
                            return;
                        }
                    }
                }
            }
            /* plain Vec<T> receiver */
            if rctl > 8 && rctl < 128 && unsafe { z_eq(rct, 8, b"rsx_vec_\0".as_ptr()) } {
                let vs = unsafe { self.vecs.find_by_name(rct, rctl) };
                if vs < VEC_CAP {
                    let ep = self.vecs.elems[vs].as_ptr();
                    let el = self.vecs.elem_lens[vs];
                    let row = unsafe { self.arrs.intern(ep, el) };
                    if row < ARR_CAP {
                        let an2 = self.arena_tmp();
                        let an2l = unsafe { ArrTab::name_for(row, an2, 96) };
                        if an2l > 0 {
                            self.out.puts(b"(\0".as_ptr());
                            self.out.put(an2, an2l);
                            self.out.puts(b"){ (const \0".as_ptr());
                            self.out.put(ep, el);
                            self.out.puts(b" *)(\0".as_ptr());
                            unsafe { self.emit_expr(recv, locals) };
                            self.out.puts(b").p, (\0".as_ptr());
                            unsafe { self.emit_expr(recv, locals) };
                            self.out.puts(b").n }\0".as_ptr());
                            return;
                        }
                    }
                }
            }
        }
        /* `.to_vec()` on an &[T] fat-ref receiver: the owned copy — a
         * malloc'd buffer memcpy'd from the view (n bytes of T), then
         * the rsx_vec_<row> triple. The unit aborts on OOM (Vec's own
         * contract, same as _push/_grow). */
        if an == 0 && mlen == 6 && unsafe { z_eq(mname, mlen, b"to_vec\0".as_ptr()) } {
            let rct = self.arena_tmp();
            let rctl = unsafe { self.expr_ctype(recv, rct, 128, locals) };
            let mut base = rct;
            let mut basel = rctl;
            if rctl > 6 && unsafe { z_eq(rct, 6, b"const \0".as_ptr()) } {
                base = unsafe { base.add(6) };
                basel -= 6;
            }
            if basel > 8 && basel < 128 && unsafe { z_eq(base, 8, b"rsx_arr_\0".as_ptr()) } {
                let rs = unsafe { self.arrs.find_by_name(base, basel) };
                if rs < ARR_CAP {
                    let ep = self.arrs.elems[rs].as_ptr();
                    let el = self.arrs.elem_lens[rs];
                    let vrow = unsafe { self.vecs.intern(ep, el) };
                    if vrow < VEC_CAP {
                        let vn = self.arena_tmp();
                        let vnl = unsafe { VecTab::name_for(vrow, vn, 96) };
                        if vnl > 0 {
                            self.out.puts(b"({ \0".as_ptr());
                            self.out.put(vn, vnl);
                            self.out.puts(b" _v; _v.n = _v.cap = (\0".as_ptr());
                            unsafe { self.emit_expr(recv, locals) };
                            self.out.puts(b").n; _v.p = (\0".as_ptr());
                            self.out.put(ep, el);
                            self.out.puts(b" *)malloc(_v.n ? _v.n * sizeof(\0".as_ptr());
                            self.out.put(ep, el);
                            self.out.puts(b") : 1); if (!_v.p) { abort(); } if (_v.n) { memcpy(_v.p, (\0".as_ptr());
                            unsafe { self.emit_expr(recv, locals) };
                            self.out.puts(b").p, _v.n * sizeof(\0".as_ptr());
                            self.out.put(ep, el);
                            self.out.puts(b")); } _v; })\0".as_ptr());
                            return;
                        }
                    }
                }
            }
        }
        /* Lock plane: `.lock()` on an rsx_lock_<row> receiver. The guard
         * IS the payload address — acquire, then the expression's value
         * is `&recv.value` (deref/deref-assign go through it, and the
         * scope-end release is the block epilogue's job — the caller
         * registers the guard for release when the binding leaves
         * scope). try_lock is the same shape gated on the primitive's
         * return. */
        if (an == 0
            && (unsafe { z_eq(mname, mlen, b"lock\0".as_ptr()) }
                || unsafe { z_eq(mname, mlen, b"try_lock\0".as_ptr()) }))
        {
            let rct = self.arena_tmp();
            let rctl = unsafe { self.expr_ctype(recv, rct, 128, locals) };
            if rctl > 9 && rctl < 128 && unsafe { z_eq(rct, 9, b"rsx_lock_\0".as_ptr()) } {
                if an == 0 && unsafe { z_eq(mname, mlen, b"lock\0".as_ptr()) } {
                    self.out.puts(b"(pm_util_lock_acquire(&\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b".raw), (&\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b".value))\0".as_ptr());
                    return;
                }
                if an == 0 && unsafe { z_eq(mname, mlen, b"try_lock\0".as_ptr()) } {
                    self.out.puts(b"(pm_util_lock_try_acquire(&\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b".raw) ? (&\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b".value) : 0)\0".as_ptr());
                    return;
                }
            }
        }
        /* ---- owned-String plane ops ----
         *
         * Receiver-typed against the one monomorphic rsx_str_t — the
         * method name alone is never validation (a user struct may own
         * a push_str method; the C-type gate is what routes it). The
         * unit-static ops the preamble emitted take &rsx_str_t; the
         * receiver emits as the value (C's struct rvalue feeds &). */
        if (an == 1
            && (unsafe { z_eq(mname, mlen, b"push_str\0".as_ptr()) }
                || unsafe { z_eq(mname, mlen, b"push\0".as_ptr()) }
                || unsafe { z_eq(mname, mlen, b"contains\0".as_ptr()) }))
            || (an == 0
                && (unsafe { z_eq(mname, mlen, b"len\0".as_ptr()) }
                    || unsafe { z_eq(mname, mlen, b"is_empty\0".as_ptr()) }
                    || unsafe { z_eq(mname, mlen, b"clear\0".as_ptr()) }
                    || unsafe { z_eq(mname, mlen, b"clone\0".as_ptr()) }
                    || unsafe { z_eq(mname, mlen, b"free\0".as_ptr()) }))
        {
            let rct = self.arena_tmp();
            let rctl = unsafe { self.expr_ctype(recv, rct, 128, locals) };
            /* the value receiver (rsx_str_t) or the &mut/& receiver
             * (rsx_str_t * — a fn param of type &mut String renders as
             * the pointer; the ops take the address, and an emitted
             * reference param IS the address, so no & prefix) */
            let is_own = rctl == 9 && unsafe { z_eq(rct, 9, b"rsx_str_t\0".as_ptr()) };
            let is_refptr = rctl == 11 && unsafe { z_eq(rct, 11, b"rsx_str_t *\0".as_ptr()) };
            if is_own || is_refptr {
                if an == 1 && unsafe { z_eq(mname, mlen, b"push_str\0".as_ptr()) } {
                    /* `push_str(&format!(..))` — the arg is a reference
                     * to the macro's temporary; `&(stmt-expr)` is not C,
                     * so the append folds INTO the builder: build _f,
                     * append it to the receiver, unit value. */
                    let mut a0 = unsafe { *ak.add(0) };
                    if unsafe { (*a0).kind } == pm_jit_rsx_ast_kind::UNARY {
                        let uk = unsafe { (*a0).kids };
                        if unsafe { (*a0).n_kids } as usize >= 1
                            && (unsafe { z_eq(unsafe { (*a0).text }, unsafe { (*a0).text_len }, b"&\0".as_ptr()) })
                        {
                            a0 = unsafe { *uk.add(0) };
                        }
                    }
                    if unsafe { (*a0).kind } == pm_jit_rsx_ast_kind::MACRO {
                        let mut ffmt: *const u8 = core::ptr::null();
                        let mut ffmt_len: usize = 0;
                        let mut fargs: [*mut pm_jit_rsx_ast_t; 8] = [core::ptr::null_mut(); 8];
                        let mut fargs_n: usize = 0;
                        let mut fcaps: [*mut pm_jit_rsx_ast_t; 16] = [core::ptr::null_mut(); 16];
                        let mut fcaps_n: usize = 0;
                        if unsafe {
                            self.format_macro_scan(
                                (*a0).text,
                                (*a0).text_len,
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
                                return;
                            }
                            self.str_own_used = true;
                            self.out.puts(b"({ rsx_str_t _f = {0};\0".as_ptr());
                            let ok = unsafe {
                                self.format_segments(
                                    a0, ffmt, ffmt_len, &fargs, fargs_n, &fcaps, fcaps_n, locals,
                                )
                            };
                            if !ok {
                                return;
                            }
                            self.out.puts(b" rsx_str_append(\0".as_ptr());
                            if !is_refptr {
                                self.out.puts(b"&\0".as_ptr());
                            }
                            unsafe { self.emit_expr(recv, locals) };
                            self.out.puts(b", (_f).p ? (_f).p : \"\", (_f).n); 0; })\0".as_ptr());
                            return;
                        }
                    }
                    /* append takes (ptr, len): the arg's shape decides —
                     * literal, owned String or &str fat ref all lower
                     * through the one face (emit_str_arg_append) */
                    self.emit_str_call(
                        b"rsx_str_append\0".as_ptr(),
                        if is_refptr {
                            core::ptr::null()
                        } else {
                            b"&\0".as_ptr()
                        },
                        recv,
                        *ak.add(0),
                        true,
                        locals,
                    );
                    return;
                }
                if an == 1 && unsafe { z_eq(mname, mlen, b"push\0".as_ptr()) } {
                    /* push(char) — the UTF-8 encoding face */
                    self.emit_str_call(
                        b"rsx_str_push_char\0".as_ptr(),
                        if is_refptr {
                            core::ptr::null()
                        } else {
                            b"&\0".as_ptr()
                        },
                        recv,
                        *ak.add(0),
                        false,
                        locals,
                    );
                    return;
                }
                if an == 0 && unsafe { z_eq(mname, mlen, b"len\0".as_ptr()) } {
                    self.out.puts(b"(\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    if is_refptr {
                        self.out.puts(b")->n\0".as_ptr());
                    } else {
                        self.out.puts(b").n\0".as_ptr());
                    }
                    return;
                }
                if an == 0 && unsafe { z_eq(mname, mlen, b"is_empty\0".as_ptr()) } {
                    self.out.puts(b"((\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b").n == 0)\0".as_ptr());
                    return;
                }
                if an == 0 && unsafe { z_eq(mname, mlen, b"clear\0".as_ptr()) } {
                    self.out.puts(b"rsx_str_clear(\0".as_ptr());
                    if !is_refptr {
                        self.out.puts(b"&\0".as_ptr());
                    }
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b")\0".as_ptr());
                    return;
                }
                if an == 0 && unsafe { z_eq(mname, mlen, b"clone\0".as_ptr()) } {
                    self.out.puts(b"rsx_str_clone(\0".as_ptr());
                    if !is_refptr {
                        self.out.puts(b"&\0".as_ptr());
                    }
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b")\0".as_ptr());
                    return;
                }
                if an == 1 && unsafe { z_eq(mname, mlen, b"contains\0".as_ptr()) } {
                    self.out.puts(b"rsx_str_contains(\0".as_ptr());
                    if !is_refptr {
                        self.out.puts(b"&\0".as_ptr());
                    }
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b", \0".as_ptr());
                    unsafe { self.emit_expr(*ak.add(0), locals) };
                    self.out.puts(b")\0".as_ptr());
                    return;
                }
                if an == 0 && unsafe { z_eq(mname, mlen, b"free\0".as_ptr()) } {
                    self.out.puts(b"rsx_str_free(\0".as_ptr());
                    if !is_refptr {
                        self.out.puts(b"&\0".as_ptr());
                    }
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b")\0".as_ptr());
                    return;
                }
            }
            /* .contains() on the borrowed &str view (rsx_str_ref_t) —
             * the view face, no owned temp */
            if an == 1 && unsafe { z_eq(mname, mlen, b"contains\0".as_ptr()) } {
                let rb2 = self.arena_tmp();
                let rl2 = unsafe { self.expr_ctype(recv, rb2, 128, locals) };
                if rl2 == 13 && unsafe { z_eq(rb2, 13, b"rsx_str_ref_t\0".as_ptr()) } {
                    self.out.puts(b"rsx_view_contains((const char *)(\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b").p, (\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b").n, \0".as_ptr());
                    unsafe { self.emit_expr(*ak.add(0), locals) };
                    self.out.puts(b")\0".as_ptr());
                    return;
                }
            }
        }
        /* `.ok()` on `core::str::from_utf8(bytes)` — Result<&str,_> to
         * Option<&str>: the Some wrap of the view (typing's twin gate:
         * the receiver is the from_utf8 call; the payload is the view). */
        if an == 0 && unsafe { z_eq(mname, mlen, b"ok\0".as_ptr()) } {
            let rb = self.arena_tmp();
            let rl = unsafe { self.expr_ctype(recv, rb, 128, locals) };
            if rl == 13 && unsafe { z_eq(rb, 13, b"rsx_str_ref_t\0".as_ptr()) }
                && unsafe { (*recv).kind } == pm_jit_rsx_ast_kind::CALL
            {
                let rk = unsafe { (*recv).kids };
                let callee0 = unsafe { *rk.add(0) };
                if unsafe { (*callee0).kind } == pm_jit_rsx_ast_kind::PATH {
                    let c0k = unsafe { (*callee0).kids };
                    let c0n = unsafe { (*callee0).n_kids } as usize;
                    if c0n == 3 {
                        let r2 = unsafe { *c0k.add(2) };
                        if unsafe { z_eq(unsafe { (*r2).text }, unsafe { (*r2).text_len }, b"from_utf8\0".as_ptr()) } {
                        let tdn = self.arena_tmp();
                        let tdn_len = unsafe {
                            Lower::opt_typedef_name(b"rsx_str_ref_t\0".as_ptr(), 13, tdn, 160)
                        };
                        if tdn_len > 0 {
                            self.out.putc(b'(');
                            self.out.put(tdn, tdn_len);
                            self.out.puts(b"){ ._v = \0".as_ptr());
                            unsafe { self.emit_expr(recv, locals) };
                            self.out.puts(b", ._has = 1 }\0".as_ptr());
                            return;
                        }
                    }
                }
            }
            }
        }
        /* ---- str_ref/owned shared methods ----
         *
         * .to_string()/.to_owned()/.into_owned() on ANY string-shaped
         * receiver (&str fat ref, &mut String via the guard, a plain
         * String value) — the owned plane's one ctor: a fresh rsx_str_t
         * built by append. emit_str_arg_append lowers the receiver's
         * shape (the type gate lives there — a non-string receiver
         * must refuse, not build a bogus String). */
        if (an == 0 && unsafe { z_eq(mname, mlen, b"to_string\0".as_ptr()) })
            || (an == 0 && unsafe { z_eq(mname, mlen, b"to_owned\0".as_ptr()) })
            || (an == 0 && unsafe { z_eq(mname, mlen, b"into_owned\0".as_ptr()) })
            || (an == 0 && unsafe { z_eq(mname, mlen, b"into_boxed_str\0".as_ptr()) })
        {
            let ok = self.str_receiver_ok(recv, locals);
            if ok {
                self.out.puts(b"({ rsx_str_t _t = {0}; rsx_str_append(&_t, \0".as_ptr());
                self.emit_str_arg_append(recv, locals);
                self.out.puts(b"); _t; })\0".as_ptr());
                return;
            }
        }
        /* Atomic loads/stores/swap on an `AtomicU32` (C: `_Atomic uint32_t`)
         * — a plain u32 field of the receiver. The GNU statement expression
         * holds the value tmp the pointer-based `__atomic_*` builtins need
         * (tcc/gcc/clang all lower them to real RMW machine code). Memory
         * orders map by value: Relaxed/Acquire/Release/AcqRel/SeqCst ->
         * 0/2/3/4/5 (the __ATOMIC_* macros' numbering). `swap` is the
         * test-and-set: acquire == "old == UNLOCKED". load takes the order
         * alone; store/swap are (value, order). */
        if (an == 1 && unsafe { z_eq(mname, mlen, b"load\0".as_ptr()) })
            || (an == 2
                && (unsafe { z_eq(mname, mlen, b"store\0".as_ptr()) }
                    || unsafe { z_eq(mname, mlen, b"swap\0".as_ptr()) }))
        {
            /* Receiver typing gate: the __atomic_* builtins require a C11
             * `_Atomic` object — the method name alone is not validation.
             * A non-atomic receiver naming .store/.load/.swap must refuse,
             * never miscompile to __atomic_* on a plain field. */
            let rct = self.arena_tmp();
            let rctl = unsafe { self.expr_ctype(recv, rct, 128, locals) };
            if rctl < 8 || !unsafe { z_eq(rct, 7, b"_Atomic\0".as_ptr()) } {
                unsafe {
                    self.err(
                        b"unsupported: atomic op on a non-atomic receiver\0".as_ptr(),
                        unsafe { (*e).line },
                    );
                }
                return;
            }
            /* load(order) — order is arg 0; store/swap(value, order) — order is arg 1 */
            let oi = if an == 1 { 0usize } else { 1usize };
            let ord = unsafe { self.atomic_order(*ak.add(oi)) };
            if ord >= 0 {
                let tmp = self.arena_tmp();
                let mut tl = unsafe { bput(tmp, 160, 0, b"__rsx_at\0".as_ptr(), 8) };
                let mut cnt = self.atom_tmp_n;
                self.atom_tmp_n += 1;
                let mut digs: [u8; 10] = [0; 10];
                let mut nd = 0usize;
                if cnt == 0 {
                    digs[0] = b'0';
                    nd = 1;
                } else {
                    while cnt > 0 && nd < 10 {
                        digs[nd] = b'0' + (cnt % 10) as u8;
                        cnt /= 10;
                        nd += 1;
                    }
                }
                let mut q = nd;
                while q > 0 {
                    q -= 1;
                    tl = unsafe { bput(tmp, 160, tl, &digs[q], 1) };
                }
                unsafe {
                    *tmp.add(tl) = 0;
                }
                self.out.puts(b"({\0".as_ptr());
                self.out.putc(b'\n');
                self.out.puts(b"uint32_t \0".as_ptr());
                self.out.put(tmp, tl);
                self.out.puts(b";\0".as_ptr());
                self.out.putc(b'\n');
                if unsafe { z_eq(mname, mlen, b"load\0".as_ptr()) } {
                    self.out.puts(b"__atomic_load(&\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b", &\0".as_ptr());
                    self.out.put(tmp, tl);
                    self.out.puts(b", \0".as_ptr());
                    self.out.put_u32(ord as u32);
                    self.out.puts(b");\0".as_ptr());
                    self.out.putc(b'\n');
                    self.out.put(tmp, tl);
                    self.out.puts(b"; })\0".as_ptr());
                    return;
                }
                if unsafe { z_eq(mname, mlen, b"store\0".as_ptr()) } {
                    /* the desired value MUST land in the temp before the
                     * builtin's pointer read — `(v);` would drop it and
                     * store garbage */
                    self.out.put(tmp, tl);
                    self.out.puts(b" = \0".as_ptr());
                    unsafe { self.emit_expr(*ak.add(0), locals) };
                    self.out.puts(b";\0".as_ptr());
                    self.out.putc(b'\n');
                    self.out.puts(b"__atomic_store(&\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b", &\0".as_ptr());
                    self.out.put(tmp, tl);
                    self.out.puts(b", \0".as_ptr());
                    self.out.put_u32(ord as u32);
                    self.out.puts(b");\0".as_ptr());
                    self.out.putc(b'\n');
                    self.out.putc(b'0');
                    self.out.puts(b"; })\0".as_ptr());
                    return;
                }
                /* swap: desired and old are SEPARATE objects — the builtin
                 * contract forbids aliasing them; the old value rides its
                 * own temp */
                {
                    let otmp = self.arena_tmp();
                    let mut otl = unsafe { bput(otmp, 160, 0, b"__rsx_at\0".as_ptr(), 8) };
                    let mut ocnt = self.atom_tmp_n;
                    self.atom_tmp_n += 1;
                    let mut odigs: [u8; 10] = [0; 10];
                    let mut ond = 0usize;
                    if ocnt == 0 {
                        odigs[0] = b'0';
                        ond = 1;
                    } else {
                        while ocnt > 0 && ond < 10 {
                            odigs[ond] = b'0' + (ocnt % 10) as u8;
                            ocnt /= 10;
                            ond += 1;
                        }
                    }
                    let mut oq = ond;
                    while oq > 0 {
                        oq -= 1;
                        otl = unsafe { bput(otmp, 160, otl, &odigs[oq], 1) };
                    }
                    unsafe {
                        *otmp.add(otl) = 0;
                    }
                    self.out.puts(b"uint32_t \0".as_ptr());
                    self.out.put(otmp, otl);
                    self.out.puts(b";\0".as_ptr());
                    self.out.putc(b'\n');
                    self.out.put(tmp, tl);
                    self.out.puts(b" = \0".as_ptr());
                    unsafe { self.emit_expr(*ak.add(0), locals) };
                    self.out.puts(b";\0".as_ptr());
                    self.out.putc(b'\n');
                    self.out.puts(b"__atomic_exchange(&\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b", &\0".as_ptr());
                    self.out.put(tmp, tl);
                    self.out.puts(b", &\0".as_ptr());
                    self.out.put(otmp, otl);
                    self.out.puts(b", \0".as_ptr());
                    self.out.put_u32(ord as u32);
                    self.out.puts(b");\0".as_ptr());
                    self.out.putc(b'\n');
                    self.out.put(otmp, otl);
                    self.out.puts(b"; })\0".as_ptr());
                    return;
                }
            }
        }
        /* `.fill(0)` / `.fill(byte)` on a fixed array — memset. The
         * receiver's C type carries [N] so the byte count is known.
         * Slices refuse (no length). */
        if an == 1 && unsafe { z_eq(mname, mlen, b"fill\0".as_ptr()) } {
            let rbuf = self.arena_tmp();
            let rn = unsafe { self.expr_ctype(recv, rbuf, 128, locals) };
            let mut has_br = false;
            let mut bi = 0usize;
            while bi < rn {
                if unsafe { *rbuf.add(bi) } == b'[' {
                    has_br = true;
                }
                bi += 1;
            }
            if rn > 0 && has_br {
                self.out.puts(b"memset(\0".as_ptr());
                unsafe { self.emit_expr(recv, locals) };
                self.out.puts(b", \0".as_ptr());
                unsafe { self.emit_expr(*ak.add(0), locals) };
                self.out.puts(b", sizeof(\0".as_ptr());
                unsafe { self.emit_expr(recv, locals) };
                self.out.puts(b"))\0".as_ptr());
                return;
            }
            unsafe {
                self.err(b"unsupported: fill on a non-array receiver\0".as_ptr(), unsafe { (*e).line });
            }
            return;
        }
        /* `.cast::<T>()` — pointer reborrow: `((T *)(recv))`. The turbofish
         * type rides as a 4th kid (TYPE node) from the parser; the method
         * form has no args. Rust's cast never fails and preserves pointer
         * mutability in practice — the corpus only reborrows alloc results. */
        if unsafe { z_eq(mname, mlen, b"cast\0".as_ptr()) } && an == 0 {
            if nk >= 4 {
                let gty = unsafe { *kids.add(3) };
                let ct = self.arena_tmp();
                let ct_len = unsafe { self.ctype(gty, ct, 128) };
                if ct_len > 0 {
                    self.out.puts(b"((\0".as_ptr());
                    self.out.put(ct, ct_len);
                    self.out.puts(b" *)\0".as_ptr());
                    self.out.putc(b'(');
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b"))\0".as_ptr());
                    return;
                }
            }
            unsafe {
                self.err(b"unsupported: cast needs a single type argument\0".as_ptr(), unsafe { (*e).line });
            }
            return;
        }
        /* `.saturating_mul(d)` / `.saturating_sub(d)` / `.min(d)` on
         * integers — the corpus calls these on usize/size_t where plain C
         * arithmetic is exact (no UB; a negative sub result only appears
         * when d > a, which the corpus guards). */
        if an == 1
            && (unsafe { z_eq(mname, mlen, b"saturating_mul\0".as_ptr()) }
                || unsafe { z_eq(mname, mlen, b"saturating_sub\0".as_ptr()) }
                || unsafe { z_eq(mname, mlen, b"min\0".as_ptr()) }
                || unsafe { z_eq(mname, mlen, b"max\0".as_ptr()) })
        {
            let op = if unsafe { z_eq(mname, mlen, b"saturating_mul\0".as_ptr()) } {
                b" * \0".as_ptr()
            } else if unsafe { z_eq(mname, mlen, b"saturating_sub\0".as_ptr()) } {
                b" - \0".as_ptr()
            } else {
                /* min/max: GNU-ish ternary — both sides evaluated once via
                 * the parens form; corpus receivers are side-effect-free. */
                let gt = unsafe { z_eq(mname, mlen, b"max\0".as_ptr()) };
                self.out.puts(b"((\0".as_ptr());
                unsafe { self.emit_expr(recv, locals) };
                if gt {
                    self.out.puts(b" >= \0".as_ptr());
                } else {
                    self.out.puts(b" <= \0".as_ptr());
                }
                unsafe { self.emit_expr(*ak.add(0), locals) };
                self.out.puts(b") ? (\0".as_ptr());
                unsafe { self.emit_expr(recv, locals) };
                self.out.puts(b") : (\0".as_ptr());
                unsafe { self.emit_expr(*ak.add(0), locals) };
                self.out.puts(b"))\0".as_ptr());
                return;
            };
            self.out.putc(b'(');
            unsafe { self.emit_expr(recv, locals) };
            self.out.put(op, 3);
            unsafe { self.emit_expr(*ak.add(0), locals) };
            self.out.putc(b')');
            return;
        }
        /* .is_some() / .is_none() — the receiver's C type decides: a
         * struct-Option (`rsx_opt_*`) tests `._has`, a pointer-Option is
         * the NULL compare. `expr_ctype` on a receiver with no type (a
         * bare call) yields 0, the pointer compare still fires. */
        if (unsafe { z_eq(mname, mlen, b"is_some\0".as_ptr()) }
            || unsafe { z_eq(mname, mlen, b"is_none\0".as_ptr()) })
            && an == 0
        {
            let some = unsafe { z_eq(mname, mlen, b"is_some\0".as_ptr()) };
            let rbuf = self.arena_tmp();
            let rn = unsafe { self.expr_ctype(recv, rbuf, 128, locals) };
            let is_opt = rn >= 8 && unsafe { z_eq(rbuf, 8, b"rsx_opt_\0".as_ptr()) };
            self.out.putc(b'(');
            if is_opt && !some {
                self.out.putc(b'!');
            }
            unsafe { self.emit_expr(recv, locals) };
            if is_opt {
                self.out.puts(b"._has)\0".as_ptr());
            } else if some {
                self.out.puts(b" != 0)\0".as_ptr());
            } else {
                self.out.puts(b" == 0)\0".as_ptr());
            }
            return;
        }
        /* `.as_ref()` on a struct-shaped Option: the payload address
         * (Some -> &recv._v, None -> NULL — a NULL test downstream works
         * because the pointer is only dereferenced under the Some arm).
         * A receiver that IS a pointer to the struct-Option (the lock
         * guard) goes through ->_v. A pointer-Option's as_ref is the
         * receiver unchanged. */
        if an == 0 && unsafe { z_eq(mname, mlen, b"as_ref\0".as_ptr()) } {
            let rbuf = self.arena_tmp();
            let rn = unsafe { self.expr_ctype(recv, rbuf, 128, locals) };
            let mut sp = rbuf;
            let mut sl = rn;
            if sl >= 6 && unsafe { z_eq(sp, 6, b"const \0".as_ptr()) } {
                sp = unsafe { sp.add(6) };
                sl -= 6;
            }
            let mut behind_ptr = false;
            if sl > 2 && unsafe { *sp.add(sl - 1) } == b'*' {
                behind_ptr = true;
                sl -= 1;
                while sl > 0 && unsafe { *sp.add(sl - 1) } == b' ' {
                    sl -= 1;
                }
            }
            if sl > 8 && unsafe { z_eq(sp, 8, b"rsx_opt_\0".as_ptr()) } {
                self.out.puts(b"(&\0".as_ptr());
                unsafe { self.emit_expr(recv, locals) };
                if behind_ptr {
                    self.out.puts(b"->_v)\0".as_ptr());
                } else {
                    self.out.puts(b"._v)\0".as_ptr());
                }
                return;
            }
        }
        /* `.map(|p| body)` on an Option — the single-closure combinator.
         * Receiver shape decides the payload: a struct-Option
         * (rsx_opt_*) reads ._v/._has, a pointer-Option is the value/NULL
         * test. The body types the result (struct -> struct-Option out,
         * pointer -> pointer-Option out): the param binds as a local of
         * the statement expression's scope, so the body's PATH lookups
         * resolve. GNU statement expression, same posture as the closure
         * builtins over arrays. */
        /* `.map(clo).collect()` — the map-collect chain over a
         * Vec/&[T]/array base (gen's `names.iter().map(..).collect()`). */
        if unsafe { z_eq(mname, mlen, b"collect\0".as_ptr()) } && an == 0 {
            if unsafe { self.try_emit_map_collect(e, recv, args, locals) } != 0 {
                return;
            }
        }
        if an == 1
            && unsafe { z_eq(mname, mlen, b"map\0".as_ptr()) }
            && unsafe { (**ak.add(0)).kind } == pm_jit_rsx_ast_kind::CLOSURE
        {
            if unsafe { self.try_emit_opt_map(recv, *ak.add(0), locals, unsafe { (*e).line }) } != 0 {
                return;
            }
        }
        /* Closure builtins over a fixed array: `.all(|&b| ..)`,
         * `.any(|&b| ..)`, `.position(|&b| ..)` — a GNU statement expression
         * wrapping the loop, the bind declared as a local of the loop
         * scope. Arrays only: a slice carries no length in this subset. */
        if an == 1
            && (unsafe { z_eq(mname, mlen, b"all\0".as_ptr()) }
                || unsafe { z_eq(mname, mlen, b"any\0".as_ptr()) }
                || unsafe { z_eq(mname, mlen, b"position\0".as_ptr()) })
            && unsafe { (**ak.add(0)).kind } == pm_jit_rsx_ast_kind::CLOSURE
        {
            if unsafe { self.try_emit_closure_loop(recv, mname, mlen, *ak.add(0), locals, unsafe { (*e).line }) } != 0 {
                return;
            }
        }
        /* `.contains(&x)` on a literal/expr range `(lo..hi)` / `(lo..=hi)`
         * — the receiver is a BINARY range node (parens unwrapped: the
         * call site almost always parenthesizes the range). */
        if an == 1 && unsafe { z_eq(mname, mlen, b"contains\0".as_ptr()) } {
            let mut r2 = recv;
            while unsafe { (*r2).kind } == pm_jit_rsx_ast_kind::PAREN
                && unsafe { (*r2).n_kids } as usize >= 1
            {
                r2 = unsafe { *(*r2).kids.add(0) };
            }
            if unsafe { (*r2).kind } == pm_jit_rsx_ast_kind::BINARY {
            let rk = unsafe { (*r2).kids };
            let rt = unsafe { (*r2).text };
            let rtl = unsafe { (*r2).text_len };
            if unsafe { (*r2).n_kids } as usize >= 2
                && (unsafe { z_eq(rt, rtl, b"..\0".as_ptr()) } || unsafe { z_eq(rt, rtl, b"..=\0".as_ptr()) })
            {
                let inc = unsafe { z_eq(rt, rtl, b"..=\0".as_ptr()) };
                let arg = unsafe { *ak.add(0) };
                let mut val = arg;
                /* `&x` — a reference arg: test the pointee */
                if unsafe { (*val).kind } == pm_jit_rsx_ast_kind::UNARY
                    && unsafe { z_eq((*val).text, (*val).text_len, b"&\0".as_ptr()) }
                    && unsafe { (*val).n_kids } as usize >= 1
                {
                    val = unsafe { *(*val).kids.add(0) };
                }
                self.out.puts(b"(\0".as_ptr());
                unsafe { self.emit_expr(val, locals) };
                self.out.puts(b" >= \0".as_ptr());
                unsafe { self.emit_expr(*rk.add(0), locals) };
                self.out.puts(b" && \0".as_ptr());
                unsafe { self.emit_expr(val, locals) };
                if inc {
                    self.out.puts(b" <= \0".as_ptr());
                } else {
                    self.out.puts(b" < \0".as_ptr());
                }
                unsafe { self.emit_expr(*rk.add(1), locals) };
                self.out.putc(b')');
                return;
            }
            }
        }
        /* `.div_ceil(d)` on an integer: ((a + d - 1) / d) — valid for
         * unsigned a; the corpus only calls it on usize. */
        if an == 1 && unsafe { z_eq(mname, mlen, b"div_ceil\0".as_ptr()) } {
            self.out.puts(b"(((\0".as_ptr());
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b") + (\0".as_ptr());
            unsafe { self.emit_expr(*ak.add(0), locals) };
            self.out.puts(b") - 1) / (\0".as_ptr());
            unsafe { self.emit_expr(*ak.add(0), locals) };
            self.out.puts(b"))\0".as_ptr());
            return;
        }
        /* `.unwrap_or(x)` on a position() chain: the not-found marker is
         * (size_t)-1 (set inside the position statement expression), so
         * the chain is a select. A struct-Option receiver (rsx_opt_<elem>)
         * is the payload select `._has ? ._v : x`. Other receivers refuse
         * — Option-payload unwraps are pointer-shaped elsewhere. */
        if an == 1 && unsafe { z_eq(mname, mlen, b"unwrap_or\0".as_ptr()) } {
            if unsafe { (*recv).kind } == pm_jit_rsx_ast_kind::METHOD_CALL {
                let rk2 = unsafe { (*recv).kids };
                let rn2 = unsafe { (*recv).n_kids } as usize;
                if rn2 >= 3 {
                    let rname2 = unsafe { *rk2.add(1) };
                    let rt2 = unsafe { (*rname2).text };
                    let rtl2 = unsafe { (*rname2).text_len };
                    if unsafe { z_eq(rt2, rtl2, b"position\0".as_ptr()) } {
                        self.out.puts(b"((\0".as_ptr());
                        unsafe { self.emit_expr(recv, locals) };
                        self.out.puts(b") == (size_t)-1 ? (\0".as_ptr());
                        unsafe { self.emit_expr(*ak.add(0), locals) };
                        self.out.puts(b") : (\0".as_ptr());
                        unsafe { self.emit_expr(recv, locals) };
                        self.out.puts(b"))\0".as_ptr());
                        return;
                    }
                }
            }
            /* struct-Option receiver: the payload select. The default
             * arg must land in the PAYLOAD's C type: `&String` against
             * an rsx_str_ref_t payload is a fat view (a bare `&s` would
             * hand the ternary an rsx_str_t * — a type mismatch). */
            {
                let rb = self.arena_tmp();
                let rl = unsafe { self.expr_ctype(recv, rb, 128, locals) };
                if rl > 8
                    && rl < 128
                    && unsafe { z_eq(rb, 8, b"rsx_opt_\0".as_ptr()) }
                {
                    /* payload spelling: the Option row's elem */
                    let pay = self.arena_tmp();
                    let payn = unsafe { Lower::opt_typedef_elem(rb, rl, pay, 96) };
                    let abuf = self.arena_tmp();
                    let an_ = if an == 1 {
                        unsafe { self.expr_ctype(*ak.add(0), abuf, 128, locals) }
                    } else {
                        0
                    };
                    let need_view = payn == 13
                        && unsafe { z_eq(pay, 13, b"rsx_str_ref_t\0".as_ptr()) }
                        && an == 1
                        && an_ == 11
                        && unsafe { z_eq(abuf, 11, b"rsx_str_t *\0".as_ptr()) };
                    self.out.puts(b"((\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b")._has ? (\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b")._v : (\0".as_ptr());
                    if need_view {
                        /* `&String` default: the fat view of the same value */
                        self.out.puts(b"(rsx_str_ref_t){ (\0".as_ptr());
                        unsafe { self.emit_expr(*ak.add(0), locals) };
                        self.out.puts(b")->p, (\0".as_ptr());
                        unsafe { self.emit_expr(*ak.add(0), locals) };
                        self.out.puts(b")->n }\0".as_ptr());
                    } else {
                        unsafe { self.emit_expr(*ak.add(0), locals) };
                    }
                    self.out.puts(b"))\0".as_ptr());
                    return;
                }
            }
            unsafe {
                self.err(b"unsupported: unwrap_or on a non-position chain\0".as_ptr(), unsafe { (*e).line });
            }
            return;
        }
        /* `.get()` on a newtype unwrap (`X.0.get()`, UnsafeCell inside):
         * numeric fields only parse for transparent newtypes, so the
         * receiver IS the C value — `.get()` is its address (`&X`),
         * matching Rust's `UnsafeCell::get() -> *mut T` and keeping a
         * later `*` deref sound through both levels. */
        if an == 0
            && unsafe { z_eq(mname, mlen, b"get\0".as_ptr()) }
            && unsafe { (*recv).kind } == pm_jit_rsx_ast_kind::FIELD
        {
            let rk = unsafe { (*recv).kids };
            let rn = unsafe { (*recv).n_kids } as usize;
            if rn >= 2 {
                let rname = unsafe { *rk.add(1) };
                let rt = unsafe { (*rname).text };
                let rtl = unsafe { (*rname).text_len };
                if rtl > 0 && !rt.is_null() && unsafe { *rt } >= b'0' && unsafe { *rt } <= b'9' {
                    self.out.puts(b"(&\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.putc(b')');
                    return;
                }
            }
        }
        /* .add(k) / .sub(k) / wrapping arithmetic on integers */
        if an == 1 && unsafe { z_eq(mname, mlen, b"add\0".as_ptr()) } {
            self.out.putc(b'(');
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b" + \0".as_ptr());
            unsafe { self.emit_expr(*ak.add(0), locals) };
            self.out.putc(b')');
            return;
        }
        if an == 1 && unsafe { z_eq(mname, mlen, b"sub\0".as_ptr()) } {
            self.out.putc(b'(');
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b" - \0".as_ptr());
            unsafe { self.emit_expr(*ak.add(0), locals) };
            self.out.putc(b')');
            return;
        }
        if an == 1 && unsafe { z_eq(mname, mlen, b"wrapping_mul\0".as_ptr()) } {
            self.out.putc(b'(');
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b" * \0".as_ptr());
            unsafe { self.emit_expr(*ak.add(0), locals) };
            self.out.putc(b')');
            return;
        }
        if an == 1 && unsafe { z_eq(mname, mlen, b"wrapping_add\0".as_ptr()) } {
            self.out.putc(b'(');
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b" + \0".as_ptr());
            unsafe { self.emit_expr(*ak.add(0), locals) };
            self.out.putc(b')');
            return;
        }
        if an == 1 && unsafe { z_eq(mname, mlen, b"wrapping_sub\0".as_ptr()) } {
            self.out.putc(b'(');
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b" - \0".as_ptr());
            unsafe { self.emit_expr(*ak.add(0), locals) };
            self.out.putc(b')');
            return;
        }
        if an == 1 && unsafe { z_eq(mname, mlen, b"wrapping_shl\0".as_ptr()) } {
            self.out.putc(b'(');
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b" << \0".as_ptr());
            unsafe { self.emit_expr(*ak.add(0), locals) };
            self.out.putc(b')');
            return;
        }
        if an == 1 && unsafe { z_eq(mname, mlen, b"wrapping_shr\0".as_ptr()) } {
            self.out.putc(b'(');
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b" >> \0".as_ptr());
            unsafe { self.emit_expr(*ak.add(0), locals) };
            self.out.putc(b')');
            return;
        }
        /* .as_str() on an owned String: the fat borrow view — a compound
         * literal built from the String's p/n fields. Must precede the
         * generic identity arm (which would hand back the rsx_str_t). */
        if an == 0 && unsafe { z_eq(mname, mlen, b"as_str\0".as_ptr()) } {
            let tbuf = self.arena_tmp();
            let tn = unsafe { self.expr_ctype(recv, tbuf, 128, locals) };
            if tn == 9 && unsafe { z_eq(tbuf, 9, b"rsx_str_t\0".as_ptr()) } {
                self.out.puts(b"(rsx_str_ref_t){ (\0".as_ptr());
                unsafe { self.emit_expr(recv, locals) };
                self.out.puts(b").p, (\0".as_ptr());
                unsafe { self.emit_expr(recv, locals) };
                self.out.puts(b").n }\0".as_ptr());
                return;
            }
            /* &str receiver: already the fat view — identity (the
             * generic identity arm below would also work; keep this
             * explicit so the arm reads as intent). */
            if tn == 13 && unsafe { z_eq(tbuf, 13, b"rsx_str_ref_t\0".as_ptr()) } {
                self.out.putc(b'(');
                unsafe { self.emit_expr(recv, locals) };
                self.out.putc(b')');
                return;
            }
        }
        /* .as_ptr() / .as_mut_ptr() on an &str fat reference: the
         * struct's p (a byte span pointer, not NUL-terminated) — BEFORE
         * the generic identity arm, which would hand back the struct.
         * .as_bytes() is the FAT &[u8] view: the same p/n pair as an
         * rsx_arr_<uint8_t> compound literal (its typing names that row). */
        if an == 0
            && (unsafe { z_eq(mname, mlen, b"as_ptr\0".as_ptr()) }
                || unsafe { z_eq(mname, mlen, b"as_mut_ptr\0".as_ptr()) }
                || unsafe { z_eq(mname, mlen, b"as_bytes\0".as_ptr()) })
        {
            let tbuf = self.arena_tmp();
            let tn = unsafe { self.expr_ctype(recv, tbuf, 128, locals) };
            if tn == 13 && unsafe { z_eq(tbuf, 13, b"rsx_str_ref_t\0".as_ptr()) } {
                if mlen == 8 {
                    let row = unsafe { self.arrs.intern(b"uint8_t\0".as_ptr(), 7) };
                    if row < ARR_CAP {
                        let nb = self.arena_tmp();
                        let nn = unsafe { ArrTab::name_for(row, nb, 96) };
                        if nn > 0 {
                            self.out.putc(b'(');
                            self.out.put(nb, nn);
                            self.out.puts(b"){ (\0".as_ptr());
                            unsafe { self.emit_expr(recv, locals) };
                            self.out.puts(b").p, (\0".as_ptr());
                            unsafe { self.emit_expr(recv, locals) };
                            self.out.puts(b").n }\0".as_ptr());
                            return;
                        }
                    }
                }
                self.out.putc(b'(');
                unsafe { self.emit_expr(recv, locals) };
                self.out.puts(b").p\0".as_ptr());
                return;
            }
            /* &[T] slice rows share the fat pair: .as_ptr() is the row's
             * .p (a plain .as_bytes() on a &[u8] names the same bytes —
             * identity, the slice already is the view). */
            if tn > 8 && tn < 128 && unsafe { z_eq(tbuf, 8, b"rsx_arr_\0".as_ptr()) } {
                if mlen == 8 {
                    self.out.putc(b'(');
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.putc(b')');
                    return;
                }
                self.out.putc(b'(');
                unsafe { self.emit_expr(recv, locals) };
                self.out.puts(b").p\0".as_ptr());
                return;
            }
            /* Vec<T> receiver: .as_ptr()/.as_mut_ptr() is the row's .p
             * (a `T *`, mutable by the C type's own const-ness — the
             * vec row's p is T*, not const T*); .as_bytes() the FAT view
             * over the owned buffer. */
            if tn > 8 && tn < 128 && unsafe { z_eq(tbuf, 8, b"rsx_vec_\0".as_ptr()) } {
                let row = unsafe { self.vecs.find_by_name(tbuf, tn) };
                if row < VEC_CAP {
                    let el = self.vecs.elems[row].as_ptr();
                    let eln = self.vecs.elem_lens[row];
                    if mlen == 8 {
                        let arow = unsafe { self.arrs.intern(el, eln) };
                        if arow < ARR_CAP {
                            let nb = self.arena_tmp();
                            let nn = unsafe { ArrTab::name_for(arow, nb, 96) };
                            if nn > 0 {
                                self.out.putc(b'(');
                                self.out.put(nb, nn);
                                self.out.puts(b"){ (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p, (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").n }\0".as_ptr());
                                return;
                            }
                        }
                    }
                }
                self.out.putc(b'(');
                unsafe { self.emit_expr(recv, locals) };
                self.out.puts(b").p\0".as_ptr());
                return;
            }
        }
        /* .as_ptr() / .as_mut_ptr() on a known literal/array — identity.
         * Pointer-to-array receiver (`T (*)[N]`): the identity would hand
         * back the array pointer itself, but `.as_ptr()` is the first
         * element — emit the C deref; the array lvalue decays to `T *`. */
        if an == 0
            && (unsafe { z_eq(mname, mlen, b"as_ptr\0".as_ptr()) }
                || unsafe { z_eq(mname, mlen, b"as_mut_ptr\0".as_ptr()) })
        {
            let abuf = self.arena_tmp();
            let an2 = unsafe { self.expr_ctype(recv, abuf, 128, locals) };
            if an2 > 0 && unsafe { self.parr_declarator(abuf, an2) } != usize::MAX {
                self.out.puts(b"(*\0".as_ptr());
                unsafe { self.emit_expr(recv, locals) };
                self.out.putc(b')');
                return;
            }
            unsafe { self.emit_expr(recv, locals) };
            return;
        }
        /* .is_null() -> (x == NULL) */
        if an == 0 && unsafe { z_eq(mname, mlen, b"is_null\0".as_ptr()) } {
            self.out.puts(b"((\0".as_ptr());
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b") == NULL)\0".as_ptr());
            return;
        }
        /* .is_ascii_digit/alphabetic/alphanumeric() — byte classification:
         * (c >= lo && c <= hi) or (…||…) for alphanumeric. The receiver is
         * emitted twice (it is an rvalue expression, cheap to recompute). */
        if an == 0 {
            if unsafe { z_eq(mname, mlen, b"is_ascii_digit\0".as_ptr()) } {
                self.out.puts(b"((\0".as_ptr());
                unsafe { self.emit_expr(recv, locals) };
                self.out.puts(b") >= '0' && (\0".as_ptr());
                unsafe { self.emit_expr(recv, locals) };
                self.out.puts(b") <= '9')\0".as_ptr());
                return;
            }
            if unsafe { z_eq(mname, mlen, b"is_ascii_alphabetic\0".as_ptr()) } {
                self.out.puts(b"(((\0".as_ptr());
                unsafe { self.emit_expr(recv, locals) };
                self.out.puts(b") >= 'a' && (\0".as_ptr());
                unsafe { self.emit_expr(recv, locals) };
                self.out.puts(b") <= 'z') || ((\0".as_ptr());
                unsafe { self.emit_expr(recv, locals) };
                self.out.puts(b") >= 'A' && (\0".as_ptr());
                unsafe { self.emit_expr(recv, locals) };
                self.out.puts(b") <= 'Z'))\0".as_ptr());
                return;
            }
            if unsafe { z_eq(mname, mlen, b"is_ascii_alphanumeric\0".as_ptr()) } {
                self.out.puts(b"(((\0".as_ptr());
                unsafe { self.emit_expr(recv, locals) };
                self.out.puts(b") >= '0' && (\0".as_ptr());
                unsafe { self.emit_expr(recv, locals) };
                self.out.puts(b") <= '9') || ((\0".as_ptr());
                unsafe { self.emit_expr(recv, locals) };
                self.out.puts(b") >= 'a' && (\0".as_ptr());
                unsafe { self.emit_expr(recv, locals) };
                self.out.puts(b") <= 'z') || ((\0".as_ptr());
                unsafe { self.emit_expr(recv, locals) };
                self.out.puts(b") >= 'A' && (\0".as_ptr());
                unsafe { self.emit_expr(recv, locals) };
                self.out.puts(b") <= 'Z'))\0".as_ptr());
                return;
            }
            if unsafe { z_eq(mname, mlen, b"len\0".as_ptr()) } {
                /* `s.len()` on an &str fat reference: the struct's n */
                let tbuf = self.arena_tmp();
                let tn = unsafe { self.expr_ctype(recv, tbuf, 128, locals) };
                if tn == 13 && unsafe { z_eq(tbuf, 13, b"rsx_str_ref_t\0".as_ptr()) } {
                    self.out.putc(b'(');
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b").n\0".as_ptr());
                    return;
                }
                /* `v.len()` on a `&Vec<T>` param (a `const rsx_vec_<row>
                 * *` C spelling): auto-deref to the row — `(*v).n`. */
                if tn > 9 && unsafe { *tbuf.add(tn - 1) } == b'*' {
                    let mut q4 = 0usize;
                    while q4 + 9 < tn
                        && !unsafe { z_eq(tbuf.add(q4), 8, b"rsx_vec_\0".as_ptr()) }
                    {
                        q4 += 1;
                    }
                    if q4 + 9 <= tn {
                        self.out.puts(b"(\0".as_ptr());
                        if q4 > 0 {
                            self.out.putc(b'*');
                        }
                        unsafe { self.emit_expr(recv, locals) };
                        self.out.puts(b").n\0".as_ptr());
                        return;
                    }
                }
                /* `s.len()` on a &[T] fat slice row: the same .n */
                if tn > 8 && unsafe { z_eq(tbuf, 8, b"rsx_arr_\0".as_ptr()) } {
                    self.out.putc(b'(');
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b").n\0".as_ptr());
                    return;
                }
                /* `arr.len()` on a fixed-size array type: `sizeof(a)/sizeof(a[0])`.
                 * Unsized slices still refuse — pass lengths explicitly. */
                if tn > 3 && unsafe { *tbuf.add(tn - 1) } == b']' {
                    self.out.puts(b"((sizeof(\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b")) / (sizeof((\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b")[0])))\0".as_ptr());
                    return;
                }
                unsafe {
                    self.err(b"unsupported: len() on a slice - pass lengths explicitly\0".as_ptr(), unsafe { (*e).line });
                    self.err_parts(tbuf, tn, b"recv\0".as_ptr(), 4);
                }
                return;
            }
            /* .is_empty() on an &str fat reference or a &[T] slice row */
            if an == 0 && unsafe { z_eq(mname, mlen, b"is_empty\0".as_ptr()) } {
                let tbuf = self.arena_tmp();
                let tn = unsafe { self.expr_ctype(recv, tbuf, 128, locals) };
                /* `&Vec<T>` param (`const rsx_vec_<row> *`): auto-deref */
                let mut vbase5 = 0usize;
                if tn > 6 && unsafe { z_eq(tbuf, 6, b"const \0".as_ptr()) } {
                    vbase5 = 6;
                }
                let vcore5 = tn - vbase5;
                let is_ref_vec = vcore5 > 10
                    && unsafe { *tbuf.add(tn - 1) } == b'*'
                    && unsafe { z_eq(tbuf.add(vbase5), 8, b"rsx_vec_\0".as_ptr()) };
                if (tn == 13 && unsafe { z_eq(tbuf, 13, b"rsx_str_ref_t\0".as_ptr()) })
                    || (tn > 8 && unsafe { z_eq(tbuf, 8, b"rsx_arr_\0".as_ptr()) })
                    || is_ref_vec
                {
                    self.out.puts(b"((\0".as_ptr());
                    if is_ref_vec {
                        self.out.putc(b'*');
                    }
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b").n == 0)\0".as_ptr());
                    return;
                }
            }
        }
        /* User-defined method: `recv.m(args)` -> `Type_m(recv, args)`.
         * The receiver's struct type decides the mangled prefix. */
        {
            let tbuf = self.arena_tmp();
            let tn = unsafe { self.expr_ctype(recv, tbuf, 128, locals) };
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
                /* strip a leading `const ` for &self receivers */
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
                    self.out.put(tbuf.add(b0), j - b0);
                    self.out.putc(b'_');
                    self.out.put(mname, mlen);
                    self.out.puts(b"(\0".as_ptr());
                    /* Rust `a.m(x)` with a value receiver lowers to the
                     * C `Type_m(&a, x)` — the method takes `T *self`. */
                    let was_ptr = tn > j + 1;
                    if was_ptr {
                        unsafe { self.emit_expr(recv, locals) };
                    } else {
                        self.out.puts(b"&(\0".as_ptr());
                        unsafe { self.emit_expr(recv, locals) };
                        self.out.puts(b")\0".as_ptr());
                    }
                    if an > 0 {
                        let ak = unsafe { (*args).kids };
                        let mut i = 0usize;
                        while i < an {
                            self.out.puts(b", \0".as_ptr());
                            unsafe { self.emit_expr(*ak.add(i), locals) };
                            i += 1;
                        }
                    }
                    self.out.puts(b")\0".as_ptr());
                    return;
                }
                /* trait-object dispatch: the receiver's C type names a
                 * declared trait (a `&mut dyn T` param/local lowers to
                 * `T *`) — the method call is a vtable slot:
                 * `p->m(p, args..)`. The receiver must arrive as the
                 * object pointer (every trait receiver in the subset is
                 * `&self`/`&mut self`, by-value self in a trait was
                 * refused at collect), so the recv expression itself is
                 * the first argument. */
                if j > b0 {
                    let tr = unsafe { self.traits.find(tbuf.add(b0), j - b0) };
                    if tr < TRAIT_CAP
                        && unsafe { self.traits.find_method(tr, mname, mlen) }
                    {
                        /* recv is the `Trait *` object; the slot takes
                         * the DATA pointer: `p->m(p->_self, ..)` */
                        unsafe { self.emit_expr(recv, locals) };
                        self.out.putc(b'-');
                        self.out.putc(b'>');
                        self.out.put(mname, mlen);
                        self.out.puts(b"(\0".as_ptr());
                        unsafe { self.emit_expr(recv, locals) };
                        self.out.puts(b"->_self\0".as_ptr());
                        if an > 0 {
                            let ak = unsafe { (*args).kids };
                            let mut i = 0usize;
                            while i < an {
                                self.out.puts(b", \0".as_ptr());
                                unsafe { self.emit_expr(*ak.add(i), locals) };
                                i += 1;
                            }
                        }
                        self.out.puts(b")\0".as_ptr());
                        return;
                    }
                }
            }
        }
        /* Everything else: refuse. */
        unsafe {
            self.err_let_name(
                b"unsupported: method call\0".as_ptr(),
                unsafe { (*e).line },
                mname,
                mlen,
            );
        }
    }

    unsafe fn emit_field(&mut self, e: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        /* kids: base, name — `.` vs `->` decided by the base's C type:
         * pointer -> `->`, value -> `.`. */
        let kids = unsafe { (*e).kids };
        if unsafe { (*e).n_kids } < 2 {
            return;
        }
        let base = unsafe { *kids.add(0) };
        let name = unsafe { *kids.add(1) };
        let fname = unsafe { (*name).text };
        let flen = unsafe { (*name).text_len };
        /* Numeric tuple field `.N`: on a transparent newtype the newtype IS
         * its inner in C (unwrap = the base itself); on a real tuple
         * (rsx_tuple_*) it is the designated element field. The base may
         * carry a `const ` prefix (a `*const (..)` deref registers the
         * pointee WITH its qualifier) — skip it, the tuple check is a
         * prefix test and `const rsx_tuple_` would otherwise fall through
         * to the newtype path and silently emit the whole tuple. */
        if flen > 0 && !fname.is_null() && unsafe { *fname } >= b'0' && unsafe { *fname } <= b'9' {
            let bt0 = self.arena_tmp();
            let mut bn0 = unsafe { self.expr_ctype(base, bt0, 128, locals) };
            if bn0 >= 6 && unsafe { z_eq(bt0, 6, b"const \0".as_ptr()) } {
                unsafe {
                    core::ptr::copy_nonoverlapping(bt0.add(6), bt0, bn0 - 6);
                    *(bt0.add(bn0 - 6)) = 0;
                }
                bn0 -= 6;
            }
            if bn0 >= 10 && unsafe { z_eq(bt0, 10, b"rsx_tuple_\0".as_ptr()) } {
                self.out.puts(b"(\0".as_ptr());
                self.out.putc(b'(');
                unsafe { self.emit_expr(base, locals) };
                self.out.puts(b")._\0".as_ptr());
                self.out.put(fname, flen);
                self.out.putc(b')');
                return;
            }
            if bn0 > 0 {
                unsafe { self.emit_expr(base, locals) };
                return;
            }
            unsafe {
                self.err(b"unsupported: .0 on a non-newtype base\0".as_ptr(), unsafe { (*e).line });
            }
            return;
        }
        /* base type */
        let bt = self.arena_tmp();
        let bn = unsafe { self.expr_ctype(base, bt, 128, locals) };
        let mut arrow = false;
        if bn > 0 {
            let mut j = bn;
            unsafe {
                while j > 0 {
                    if *bt.add(j - 1) == b'*' {
                        arrow = true;
                        break;
                    }
                    if *bt.add(j - 1) == b' ' {
                        j -= 1;
                        continue;
                    }
                    break;
                }
            }
        } else {
            /* unknown base type — refuse rather than guess `.` vs `->`. */
            unsafe {
                self.err(b"cannot infer field base type - ascribe it\0".as_ptr(), unsafe { (*e).line });
            }
            return;
        }
        self.out.puts(b"(\0".as_ptr());
        self.out.putc(b'(');
        unsafe { self.emit_expr(base, locals) };
        if arrow {
            self.out.puts(b")->\0".as_ptr());
        } else {
            self.out.puts(b").\0".as_ptr());
        }
        self.out.put(fname, flen);
        self.out.putc(b')');
    }
}

