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
                /* 2-segment enum variant: `State::Ready` -> State_Ready */
                if unsafe { (*last).kind } == pm_jit_rsx_ast_kind::PATH {
                    let mut full = self.arena_tmp();
                    let mut at = 0usize;
                    at = unsafe { bput(full, 128, at, ftext, flen) };
                    at = unsafe { bput(full, 128, at, b"_\0".as_ptr(), 1) };
                    at = unsafe { bput(full, 128, at, ltext, llen) };
                    unsafe {
                        *full.add(at) = 0;
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
            unsafe {
                self.err(b"unsupported: tuple expression\0".as_ptr(), unsafe { (*e).line });
            }
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
                unsafe {
                    self.err(b"cannot infer tuple element type - ascribe it\0".as_ptr(), unsafe { (*e).line });
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
                    tname = unsafe { (*seg).text };
                    tlen = sl2;
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
        /* struct slot for field-ctype lookups (None needs the field's
         * Option shape to zero-init) */
        let ss = unsafe { (*self.syms).find(tname, tlen) };
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
                        unsafe { self.emit_expr(*kids.add(0), locals) };
                        if !lo_empty {
                            self.out.puts(b" + \0".as_ptr());
                            unsafe { self.emit_expr(lo, locals) };
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
            /* `&slice[lo..hi]` / `&arr[lo..hi]`: the range index already
             * lowers to the sub-slice pointer — taking its address would be
             * one indirection too many. Emit the index expr itself. */
            let inner = unsafe { *kids.add(0) };
            let is_range_idx = unsafe { rsx_idx_is_range(inner) };
            if is_range_idx {
                unsafe { self.emit_expr(inner, locals) };
                return;
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
             *    expression's value is the payload (usize etc.). */
            let is_ptr_ret = self.cur_ret_len > 0 && unsafe { self.cur_ret[self.cur_ret_len - 1] } == b'*';
            let is_opt_ret = self.cur_ret_len >= 8
                && unsafe { z_eq(self.cur_ret.as_ptr(), 8, b"rsx_opt_\0".as_ptr()) };
            if !is_ptr_ret && !is_opt_ret {
                unsafe {
                    self.err(
                        b"unsupported: ? outside a pointer- or Option-returning fn\0".as_ptr(),
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
                        || unsafe { z_eq(wt, wl, b"String\0".as_ptr()) }
                        || unsafe { z_eq(wt, wl, b"BTreeMap\0".as_ptr()) }
                    {
                        self.out.puts(b"{0}\0".as_ptr());
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
                            unsafe { self.emit_expr(*ak.add(i), locals) };
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
        /* .is_null() */
        if unsafe { z_eq(mname, mlen, b"is_null\0".as_ptr()) } && an == 0 {
            self.out.putc(b'(');
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b" == 0)\0".as_ptr());
            return;
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
        {
            let rct = self.arena_tmp();
            let rctl = unsafe { self.expr_ctype(recv, rct, 128, locals) };
            if rctl > 8 && rctl < 128 && unsafe { z_eq(rct, 8, b"rsx_vec_\0".as_ptr()) } {
                /* the interned element spelling from the typedef name */
                if an == 1 && unsafe { z_eq(mname, mlen, b"push\0".as_ptr()) } {
                    self.out.put(rct, rctl);
                    self.out.puts(b"_push(&\0".as_ptr());
                    unsafe { self.emit_expr(recv, locals) };
                    self.out.puts(b", \0".as_ptr());
                    unsafe { self.emit_expr(*ak.add(0), locals) };
                    self.out.puts(b")\0".as_ptr());
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
         * the chain is a select. Any other unwrap_or receiver refuses —
         * Option-payload unwraps are pointer-shaped elsewhere. */
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
        /* .as_ptr() / .as_mut_ptr() on an &str fat reference: the
         * struct's p (a byte span pointer, not NUL-terminated) — BEFORE
         * the generic identity arm, which would hand back the struct. */
        if an == 0
            && (unsafe { z_eq(mname, mlen, b"as_ptr\0".as_ptr()) }
                || unsafe { z_eq(mname, mlen, b"as_mut_ptr\0".as_ptr()) }
                || unsafe { z_eq(mname, mlen, b"as_bytes\0".as_ptr()) })
        {
            let tbuf = self.arena_tmp();
            let tn = unsafe { self.expr_ctype(recv, tbuf, 128, locals) };
            if tn == 13 && unsafe { z_eq(tbuf, 13, b"rsx_str_ref_t\0".as_ptr()) } {
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
            /* .is_empty() on an &str fat reference */
            if an == 0 && unsafe { z_eq(mname, mlen, b"is_empty\0".as_ptr()) } {
                let tbuf = self.arena_tmp();
                let tn = unsafe { self.expr_ctype(recv, tbuf, 128, locals) };
                if tn == 13 && unsafe { z_eq(tbuf, 13, b"rsx_str_ref_t\0".as_ptr()) } {
                    self.out.puts(b"((\0".as_ptr());
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

