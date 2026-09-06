/* ==== item lowering ==== */

impl Lower {
    /* ---- type-emission topological ordering ----
     *
     * A struct/union field with a by-value named type needs that type's
     * typedef COMPLETE at the field's declaration (C rule), while a
     * pointer field only needs it declared. The rsx source order is not
     * a valid C order for two reasons: face splices append `#[path]`
     * types AFTER the muscle (so a muscle struct naming a face type comes
     * first), and Rust item order is free. The emit passes therefore need
     * a dependency walk: before emitting a STRUCT/UNION, emit every
     * unit-defined type its by-value fields name, recursively.
     *
     * `dep_collect` walks one TYPE node tree collecting named types that
     * sit in value position (not behind a pointer — `*T`, `&T` only need
     * a declaration, which the forward `typedef struct S S;` already
     * gives every struct). Option<T>/Cell/UnsafeCell/Mut wrappers
     * recurse into their arg. Arrays/tuples recurse into their element
     * types. A path's leaf name that is neither primitive nor a unit type
     * is an opaque extern — opq_note already hoists a declaration for it.
     */
    unsafe fn dep_collect(
        &mut self,
        ty: *const pm_jit_rsx_ast_t,
        out: *mut u8,
        out_lens: *mut usize,
        out_n: *mut usize,
        cap: usize,
    ) {
        if ty.is_null() {
            return;
        }
        let kind = unsafe { (*ty).kind };
        if kind != pm_jit_rsx_ast_kind::TYPE {
            return;
        }
        let text = unsafe { (*ty).text };
        let text_len = unsafe { (*ty).text_len };
        let kids = unsafe { (*ty).kids };
        let nk = unsafe { (*ty).n_kids } as usize;
        /* pointer / reference wrappers: the pointee only needs a
         * declaration — the hoisted forward typedef covers it. Do NOT
         * recurse (a struct naming `*Later` may emit before `Later`). */
        if unsafe { z_eq(text, text_len, b"*\0".as_ptr()) }
            || unsafe { z_eq(text, text_len, b"&\0".as_ptr()) }
            || unsafe { z_eq(text, text_len, b"&mut\0".as_ptr()) }
        {
            return;
        }
        /* fn-ptr: params/ret are values? No — params and returns are
         * themselves passed by value, so a fn-ptr field whose param is a
         * struct needs the struct complete... but a fn-ptr PARAM type in
         * a prototype only needs a declaration (C promotes parameter
         * declarations to prototypes). Leave fn-ptrs un-walked: the
         * prototype declaration suffices for C to parse the fnptr type. */
        if unsafe { z_eq(text, text_len, b"fnptr\0".as_ptr()) } {
            return;
        }
        /* arrays: element type is a value dep */
        if unsafe { z_eq(text, text_len, b"[]\0".as_ptr()) }
            || unsafe { z_eq(text, text_len, b"[;]\0".as_ptr()) }
        {
            if nk >= 1 {
                let inner = unsafe { *kids.add(0) };
                unsafe { self.dep_collect(inner, out, out_lens, out_n, cap) };
            }
            return;
        }
        /* tuple: every element is a value dep */
        if text_len == 5 && unsafe { z_eq(text, text_len, b"tuple\0".as_ptr()) } {
            let mut j = 0usize;
            while j < nk {
                let e = unsafe { *kids.add(j) };
                unsafe { self.dep_collect(e, out, out_lens, out_n, cap) };
                j += 1;
            }
            return;
        }
        /* generic wrappers with a value arg: recurse into the arg */
        if unsafe { z_eq(text, text_len, b"path\0".as_ptr()) }
            || unsafe { z_eq(text, text_len, b"gpath\0".as_ptr()) }
        {
            if nk == 0 {
                return;
            }
            let first = unsafe { *kids.add(0) };
            let fname = unsafe { (*first).text };
            let flen = unsafe { (*first).text_len };
            /* Option<T>: pointer payload needs nothing; value payload
             * (rsx_opt_<elem> struct) needs elem complete. */
            if unsafe { z_eq(fname, flen, b"Option\0".as_ptr()) } {
                if nk >= 2 {
                    let inner = unsafe { *kids.add(1) };
                    unsafe { self.dep_collect(inner, out, out_lens, out_n, cap) };
                }
                return;
            }
            if unsafe { z_eq(fname, flen, b"UnsafeCell\0".as_ptr()) }
                || unsafe { z_eq(fname, flen, b"Cell\0".as_ptr()) }
            {
                if nk >= 2 {
                    let inner = unsafe { *kids.add(1) };
                    unsafe { self.dep_collect(inner, out, out_lens, out_n, cap) };
                }
                return;
            }
            /* single-segment path: the leaf name is the dep candidate */
            if nk == 1 {
                unsafe { self.dep_name(fname, flen, out, out_lens, out_n, cap) };
                return;
            }
            /* multi-segment path: leaf is the candidate */
            let leaf = unsafe { *kids.add(nk - 1) };
            let lname = unsafe { (*leaf).text };
            let llen = unsafe { (*leaf).text_len };
            unsafe { self.dep_name(lname, llen, out, out_lens, out_n, cap) };
            return;
        }
        /* leaf TYPE: a primitive spelling names no dep */
        let prim_test = self.arena_tmp();
        let pn = unsafe { self.prim_ctype(text, text_len, prim_test, 160) };
        if pn > 0 {
            return;
        }
        /* anything else: a named type in value position */
        unsafe { self.dep_name(text, text_len, out, out_lens, out_n, cap) };
    }

    /* Record one candidate dep name if the unit defines it as a struct /
     * union (only those need complete-before-use; enums ride pass A0 and
     * aliases resolve through their own target). Duplicates are fine —
     * the emit side dedups by emitted-set. */
    unsafe fn dep_name(
        &mut self,
        name: *const u8,
        nlen: usize,
        out: *mut u8,
        out_lens: *mut usize,
        out_n: *mut usize,
        cap: usize,
    ) {
        if name.is_null() || nlen == 0 || nlen >= 48 || unsafe { *out_n } >= cap {
            return;
        }
        /* primitives / bool / void spell no dep */
        let pt = self.arena_tmp();
        let pn = unsafe { self.prim_ctype(name, nlen, pt, 160) };
        if pn > 0 {
            return;
        }
        if unsafe { z_eq(name, nlen, b"tuple\0".as_ptr()) } {
            return;
        }
        let n = unsafe { *out_n };
        let mut i = 0usize;
        while i < nlen {
            unsafe {
                *(out.add(n * 48 + i)) = *name.add(i);
            }
            i += 1;
        }
        unsafe {
            *out_lens.add(n) = nlen;
            *out_n = n + 1;
        }
    }

    /* Is `name` a type this unit declares as a STRUCT/UNION (not a
     * transparent newtype, not an alias — those are not field-complete
     * targets)? Returns the item pointer, or null when the unit does not
     * declare it (then it is an enum from pass A0, or opaque extern). */
    unsafe fn tyorder_find(
        &mut self,
        kids: *mut *mut pm_jit_rsx_ast_t,
        nk: usize,
        name: *const u8,
        nlen: usize,
    ) -> *const pm_jit_rsx_ast_t {
        let mut i = 0usize;
        while i < nk {
            let item = unsafe { *kids.add(i) };
            if item.is_null() {
                i += 1;
                continue;
            }
            if unsafe { (*item).kind } != pm_jit_rsx_ast_kind::STRUCT {
                i += 1;
                continue;
            }
            if unsafe { self.has_generic_marker(item) } {
                i += 1;
                continue;
            }
            let t = unsafe { (*item).text };
            let tl = unsafe { (*item).text_len };
            if tl == nlen && !t.is_null() {
                let mut j = 0usize;
                let mut eq = true;
                while j < nlen {
                    if unsafe { *t.add(j) } != unsafe { *name.add(j) } {
                        eq = false;
                        break;
                    }
                    j += 1;
                }
                if eq {
                    return item;
                }
            }
            i += 1;
        }
        core::ptr::null()
    }

    /* Emit one STRUCT/UNION item with its by-value field deps emitted
     * first (recursive). Pass A calls this per item in file order; a dep
     * found later in the file (splice-append case) or in any order rides
     * ahead. The tydone set prevents double emission (C redefinition) and
     * catches cycles: a dep that is already ON the current recursion
     * chain but not yet in tydone is cyclic — refuse with the site. */
    unsafe fn emit_struct_ordered(
        &mut self,
        kids: *mut *mut pm_jit_rsx_ast_t,
        nk: usize,
        item: *const pm_jit_rsx_ast_t,
    ) {
        if item.is_null() {
            return;
        }
        let name = unsafe { (*item).text };
        let nlen = unsafe { (*item).text_len };
        if self.tyorder_depth > 32 {
            unsafe {
                self.err(
                    b"unsupported: struct value-dependency cycle or nesting too deep\0".as_ptr(),
                    unsafe { (*item).line },
                );
            }
            return;
        }
        /* deps first */
        let mut dep_bufs: [[u8; 48]; 16] = [[0; 48]; 16];
        let mut dep_lens: [usize; 16] = [0; 16];
        let mut dep_n: usize = 0;
        let ikids = unsafe { (*item).kids };
        let ikn = unsafe { (*item).n_kids } as usize;
        let mut j = 0usize;
        while j < ikn {
            let f = unsafe { *ikids.add(j) };
            if unsafe { (*f).kind } == pm_jit_rsx_ast_kind::STRUCT_FIELD {
                let fk = unsafe { (*f).kids };
                let fkn = unsafe { (*f).n_kids } as usize;
                if fkn >= 1 {
                    let fty = unsafe { *fk.add(0) };
                    unsafe {
                        self.dep_collect(
                            fty,
                            dep_bufs.as_mut_ptr() as *mut u8,
                            dep_lens.as_mut_ptr(),
                            &mut dep_n,
                            16,
                        );
                    };
                }
            }
            j += 1;
        }
        let mut d = 0usize;
        while d < dep_n {
            let dn = dep_bufs[d].as_ptr();
            let dl = dep_lens[d];
            d += 1;
            if unsafe { self.tydone_find(dn, dl) } {
                continue;
            }
            let dep_item = unsafe { self.tyorder_find(kids, nk, dn, dl) };
            if dep_item.is_null() {
                continue;
            }
            self.tyorder_depth += 1;
            unsafe { self.emit_struct_ordered(kids, nk, dep_item) };
            self.tyorder_depth -= 1;
            if !self.ok {
                return;
            }
        }
        /* self, unless a sibling already pulled it in */
        if unsafe { self.tydone_find(name, nlen) } {
            return;
        }
        unsafe { self.tydone_add(name, nlen) };
        /* Option<fn-ptr> fields name rsx_opt_Rrsx_fnp_N — the fn-ptr row
         * and the Option row must both precede the struct. Only fnp-
         * payload rows flush here (a struct-payload row still waits for
         * its struct — opt_emit_for fires when that lands). */
        unsafe { self.fnp_emit_rest() };
        unsafe { self.opt_emit_fnp() };
        unsafe { self.lower_struct(item) };
        /* Option/tuple typedefs whose payloads name this struct follow it */
        unsafe { self.opt_emit_for(name, nlen) };
        unsafe { self.tup_emit_for(kids, nk, name, nlen) };
    }

    unsafe fn tydone_find(&mut self, name: *const u8, nlen: usize) -> bool {
        let mut s = 0usize;
        while s < self.tydone_n {
            if self.tydone_lens[s] == nlen {
                let mut j = 0usize;
                let mut eq = true;
                while j < nlen {
                    if unsafe { *self.tydone_names[s].as_ptr().add(j) } != unsafe { *name.add(j) } {
                        eq = false;
                        break;
                    }
                    j += 1;
                }
                if eq {
                    return true;
                }
            }
            s += 1;
        }
        false
    }

    unsafe fn tydone_add(&mut self, name: *const u8, nlen: usize) {
        if nlen >= 48 || self.tydone_n >= TYD_CAP {
            return;
        }
        let mut j = 0usize;
        while j < nlen {
            unsafe {
                self.tydone_names[self.tydone_n][j] = *name.add(j);
            }
            j += 1;
        }
        self.tydone_lens[self.tydone_n] = nlen;
        self.tydone_n += 1;
    }

    /* C declarator: a ctype spelling may carry trailing array dimensions
     * (`T [N]`, `T [A] [B]`); C wants them after the declared name
     * (`T name[A][B]`). Scans trailing `[...]` groups from the right and
     * re-emits them in order (outermost first). Pointer/fnptr spellings
     * never end in `]`, so they pass through unchanged. */
    unsafe fn emit_declarator(&mut self, ct: *const u8, n: usize, name: *const u8, name_len: usize) {
        /* fn-pointer ctype: `RET (*)(params)` — the name goes inside the
         * parens (`RET (*name)(params)`), not after the whole spelling. */
        {
            let mut i = 0usize;
            while i + 3 <= n {
                if unsafe { *ct.add(i) } == b'('
                    && unsafe { *ct.add(i + 1) } == b'*'
                    && unsafe { *ct.add(i + 2) } == b')'
                {
                    self.out.put(ct, i + 2);
                    self.out.put(name, name_len);
                    self.out.put(ct.add(i + 2), n - i - 2);
                    return;
                }
                i += 1;
            }
        }
        /* dim boundaries flattened: dim_starts[i] .. dim_ends[i] */
        let mut dim_starts: [usize; 8] = [0; 8];
        let mut dim_ends: [usize; 8] = [0; 8];
        let mut ndims = 0usize;
        let mut e = n;
        loop {
            /* skip trailing spaces, expect ']' */
            let mut s = e;
            while s > 0 && unsafe { *ct.add(s - 1) } == b' ' {
                s -= 1;
            }
            if s == 0 || unsafe { *ct.add(s - 1) } != b']' {
                break;
            }
            /* find the matching '[' (dims contain no brackets) */
            let mut d = s - 1;
            while d > 0 && unsafe { *ct.add(d) } != b'[' {
                d -= 1;
            }
            if unsafe { *ct.add(d) } != b'[' {
                break;
            }
            /* group = ct[d..s), inclusive of '[' and ']' */
            if ndims < 8 {
                dim_starts[ndims] = d;
                dim_ends[ndims] = s;
                ndims += 1;
            }
            e = d;
        }
        /* base type = ct[0..e], then name, then dims outermost-first:
         * collected right-to-left already lands outermost first. */
        let mut be = e;
        while be > 0 && unsafe { *ct.add(be - 1) } == b' ' {
            be -= 1;
        }
        self.out.put(ct, be);
        if be > 0 {
            self.out.putc(b' ');
        }
        self.out.put(name, name_len);
        let mut i = 0usize;
        while i < ndims {
            let ds = dim_starts[i];
            let de = dim_ends[i];
            let mut j = ds;
            while j < de {
                self.out.putc(unsafe { *ct.add(j) });
                j += 1;
            }
            i += 1;
        }
    }

    /* `typedef struct S { C_T f; } S;` — declaration order is the layout.
     * A marker ATTR kid ("union", from the `union` item keyword) swaps the
     * C tag to `union` — same fields, same registration, `of.i32` field
     * access reads the active member like any C union. */
    unsafe fn lower_struct(&mut self, item: *const pm_jit_rsx_ast_t) {
        let name = unsafe { (*item).text };
        let nlen = unsafe { (*item).text_len };
        let line = unsafe { (*item).line };
        let kids = unsafe { (*item).kids };
        let nk = unsafe { (*item).n_kids } as usize;
        /* Generic tuple struct: the transparent-newtype case — registered
         * during collect, C emission skipped (the type renders as its
         * inner at use sites; constructors unwrap). Any other generic
         * shape needs monomorphized C layout the subset does not define.
         * Plain (non-generic) tuple structs refuse too: the C render would
         * name the fields "tuple", which no consumer can spell — a refusal
         * here keeps `.0` unwraps sound (only newtypes can ever resolve). */
        if unsafe { self.has_generic_marker(item) } {
            if unsafe { self.nt_find(name, nlen) } {
                return;
            }
            unsafe {
                self.err(b"unsupported: generic struct needs monomorphized layout (only single-field newtypes are supported)\0".as_ptr(), line);
            }
            return;
        }
        {
            let mut i = 0usize;
            let mut tuple_fields = 0usize;
            while i < nk {
                let k = unsafe { *kids.add(i) };
                if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::STRUCT_FIELD {
                    let ftxt = unsafe { (*k).text };
                    let fl = unsafe { (*k).text_len };
                    if fl == 5 && !ftxt.is_null() && unsafe { z_eq(ftxt, fl, b"tuple\0".as_ptr()) } {
                        tuple_fields += 1;
                    }
                }
                i += 1;
            }
            if tuple_fields > 0 {
                unsafe {
                    self.err(b"unsupported: tuple struct without generics (write named fields)\0".as_ptr(), line);
                }
                return;
            }
        }
        let is_union = unsafe { self.has_union_marker(item) };
        let tag: *const u8 = if is_union {
            b"union\0".as_ptr()
        } else {
            b"struct\0".as_ptr()
        };
        let tag_len: usize = if is_union { 5 } else { 6 };
        self.out.puts(b"#line \0".as_ptr());
        unsafe { self.out.put_u32(line) };
        self.out.puts(b" \"__impl__.rs\"\n\0".as_ptr());
        /* named-field struct: real body; unit struct: empty forward decl */
        let mut i = 0usize;
        let mut has_fields = false;
        while i < nk {
            let k = unsafe { *kids.add(i) };
            if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::STRUCT_FIELD {
                has_fields = true;
                break;
            }
            i += 1;
        }
        if has_fields {
            /* forward typedef so fields may name their own struct type
             * (C: the typedef name is not in scope inside the body). */
            self.out.puts(b"typedef \0".as_ptr());
            self.out.put(tag, tag_len);
            self.out.putc(b' ');
            self.out.put(name, nlen);
            self.out.putc(b' ');
            self.out.put(name, nlen);
            self.out.puts(b";\n\0".as_ptr());
            self.out.put(tag, tag_len);
            self.out.putc(b' ');
            self.out.put(name, nlen);
            self.out.puts(b" {\n\0".as_ptr());
            i = 0;
            while i < nk {
                let k = unsafe { *kids.add(i) };
                if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::STRUCT_FIELD {
                    let fk = unsafe { (*k).kids };
                    let fkn = unsafe { (*k).n_kids } as usize;
                    if fkn >= 1 {
                        let fty = unsafe { *fk.add(0) };
                        let ct = self.arena_tmp();
                        let n = unsafe { self.ctype(fty, ct, 128) };
                        if n == 0 {
                            return;
                        }
                        self.out.puts(b"    \0".as_ptr());
                        unsafe {
                            self.emit_declarator(
                                ct,
                                n,
                                unsafe { (*k).text },
                                unsafe { (*k).text_len },
                            );
                        }
                        self.out.puts(b";\n\0".as_ptr());
                    }
                }
                i += 1;
            }
            self.out.puts(b"};\n\0".as_ptr());
            self.out.puts(b"typedef \0".as_ptr());
            self.out.put(tag, tag_len);
            self.out.putc(b' ');
            self.out.put(name, nlen);
            self.out.putc(b' ');
            self.out.put(name, nlen);
            self.out.puts(b";\n\0".as_ptr());
        } else {
            self.out.puts(b"typedef \0".as_ptr());
            self.out.put(tag, tag_len);
            self.out.putc(b' ');
            self.out.put(name, nlen);
            self.out.puts(b" \0".as_ptr());
            self.out.put(name, nlen);
            self.out.puts(b";\n\0".as_ptr());
        }
        self.out.putc(b'\n');
    }

    /* Does this STRUCT item carry the generic marker ATTR (a generic
     * tuple struct — transparent newtype when single-field)? */
    unsafe fn has_generic_marker(&mut self, item: *const pm_jit_rsx_ast_t) -> bool {
        let kids = unsafe { (*item).kids };
        let nk = unsafe { (*item).n_kids } as usize;
        let mut i = 0usize;
        while i < nk {
            let k = unsafe { *kids.add(i) };
            if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::ATTR {
                let t = unsafe { (*k).text };
                let tl = unsafe { (*k).text_len };
                if tl == 7 && !t.is_null() && unsafe { z_eq(t, tl, b"generic\0".as_ptr()) } {
                    return true;
                }
            }
            i += 1;
        }
        false
    }

    unsafe fn nt_add(&mut self, name: *const u8, len: usize) {
        if self.nt_n >= NT_CAP || len > 48 {
            return;
        }
        let s = self.nt_n;
        let mut i = 0usize;
        while i < len {
            unsafe {
                self.nt_names[s][i] = *name.add(i);
            }
            i += 1;
        }
        self.nt_lens[s] = len;
        self.nt_n += 1;
    }

    unsafe fn nt_find(&self, name: *const u8, len: usize) -> bool {
        let mut s = 0usize;
        while s < self.nt_n {
            if self.nt_lens[s] == len {
                let mut i = 0usize;
                let mut same = true;
                while i < len {
                    if unsafe { self.nt_names[s][i] } != unsafe { *name.add(i) } {
                        same = false;
                        break;
                    }
                    i += 1;
                }
                if same {
                    return true;
                }
            }
            s += 1;
        }
        false
    }

    /* A rendered `struct { T _v; bool _has; }` is the struct-shaped Option
     * (integer payload). Returns the payload type's length (0 when the
     * spelling is not that shape). The prefix/suffix are fixed texts. */
    /* A rendered `struct { T _v; bool _has; }` is the struct-shaped Option
     * (integer payload). Returns the payload type's length (0 when the
     * spelling is not that shape). The prefix/suffix are fixed texts.
     * (Legacy inline shape — superseded by the named rsx_opt_<elem>
     * typedefs, kept until no emitter renders the inline form.) */
    unsafe fn opt_struct_elem(ct: *const u8, n: usize) -> usize {
        if n < 28 {
            return 0;
        }
        let mut i = 0usize;
        while i < 9 {
            if unsafe { *ct.add(i) } != unsafe { *b"struct { \0".as_ptr().add(i) } {
                return 0;
            }
            i += 1;
        }
        /* suffix " _v; bool _has; }" is 17 bytes */
        let suf = b" _v; bool _has; }\0".as_ptr();
        let mut j = 0usize;
        while j < 17 {
            if unsafe { *ct.add(n - 17 + j) } != unsafe { *suf.add(j) } {
                return 0;
            }
            j += 1;
        }
        n - 9 - 17
    }

    /* Lowercase hex digit for a value < 16 (canonical identifier form). */
    unsafe fn hex_lo(v: u8) -> u8 {
        if v < 10 {
            b'0' + v
        } else {
            b'a' + (v - 10)
        }
    }

    /* Canonical lowercase hex value of a hex digit, or 0xFF when c is not
     * [0-9a-f]. Uppercase is rejected — one canonical spelling only. */
    unsafe fn hex_val(c: u8) -> u8 {
        if c >= b'0' && c <= b'9' {
            c - b'0'
        } else if c >= b'a' && c <= b'f' {
            c - b'a' + 10
        } else {
            0xFF
        }
    }

    /* `<raw_len>e<2*raw_len lowercase hex digits>` — the canonical
     * injective byte encoding of one payload type. Every raw byte maps to
     * exactly two identifier-safe hex digits, so distinct payloads encode
     * to distinct components AND distinct components decode back to the
     * exact payload bytes (hex is one-to-one per byte). The decimal
     * raw_len is a validation field (the encoded char count is 2*raw_len,
     * never raw_len). Writes into out at `at`, NUL-terminates when it
     * fits. Returns the new offset; when the component would not fit in
     * [at, cap) the write is refused entirely (nothing partial) and cap is
     * returned — the caller treats name_len == cap as a refusal. */
    unsafe fn hex_put_len_e(
        elem: *const u8,
        elen: usize,
        out: *mut u8,
        cap: usize,
        at: usize,
    ) -> usize {
        /* exact encoded size: <decimal raw_len> + 'e' + 2*elen, +1 for NUL
         * reservation. Overflow is impossible on this architecture's
         * usize for the byte payloads the compiler handles (elen < 64),
         * but guard anyway — a refusal beats a wraparound. */
        if elen > (usize::MAX - 2) / 2 {
            return cap;
        }
        let hex_len = elen * 2;
        let mut digs = [0u8; 20];
        let mut dn = 0usize;
        let mut lc = elen;
        if lc == 0 {
            digs[0] = b'0';
            dn = 1;
        } else {
            while lc > 0 && dn < digs.len() {
                digs[dn] = b'0' + (lc % 10) as u8;
                lc /= 10;
                dn += 1;
            }
            /* decimal did not fit 20 digits — refuse rather than print a
             * wrong length (the validation field must be exact) */
            if lc > 0 {
                return cap;
            }
        }
        let need = dn + 1 + hex_len;
        /* refuse when the component (+ NUL) does not fit whole */
        if out.is_null() || cap == 0 || at >= cap || need > cap - at - 1 {
            return cap;
        }
        let mut a = at;
        let mut q = dn;
        while q > 0 {
            q -= 1;
            unsafe {
                *out.add(a) = digs[q];
            }
            a += 1;
        }
        unsafe {
            *out.add(a) = b'e';
        }
        a += 1;
        let mut i = 0usize;
        while i < elen {
            let b = unsafe { *elem.add(i) };
            let hi = unsafe { Lower::hex_lo(b >> 4) };
            let lo = unsafe { Lower::hex_lo(b & 0x0F) };
            unsafe {
                *out.add(a) = hi;
                *out.add(a + 1) = lo;
            }
            a += 2;
            i += 1;
        }
        unsafe {
            *out.add(a) = 0;
        }
        a
    }

    /* Canonical Option typedef name:
     *   rsx_opt_<raw_len>e<2*raw_len lowercase hex digits>
     * Reversible: opt_typedef_elem decodes the component back to the exact
     * payload C-type bytes. Returns the name length; 0 when the name does
     * not fit cap whole (never a truncation). */
    unsafe fn opt_typedef_name(elem: *const u8, elen: usize, out: *mut u8, cap: usize) -> usize {
        let at = unsafe { bput(out, cap, 0, b"rsx_opt_\0".as_ptr(), 8) };
        if at != 8 || cap <= 9 {
            return 0;
        }
        /* Row-named payloads (every byte [A-Za-z0-9_]) take the short raw
         * form `rsx_opt_R<raw>` — but only when the hex form would be
         * refused for length: payloads under the 48-byte gate keep the
         * injective `<len>e<hex>` encoding every existing card object
         * and test assertion spells. Hex digits are [0-9a-f], never 'R',
         * so the two forms cannot collide; the decode reads either. */
        let mut raw_ok = elen >= 48;
        let mut i = 0usize;
        while raw_ok && i < elen {
            let c = unsafe { *elem.add(i) };
            if !(c.is_ascii_alphanumeric() || c == b'_') {
                raw_ok = false;
                break;
            }
            i += 1;
        }
        if raw_ok {
            if 9 + elen >= cap {
                return 0;
            }
            let end = unsafe { bput(out, cap, at, b"R\0".as_ptr(), 1) };
            if end != 9 {
                return 0;
            }
            let end2 = unsafe { bput(out, cap, 9, elem, elen) };
            if end2 >= cap {
                return 0;
            }
            return end2;
        }
        let end = unsafe { Lower::hex_put_len_e(elem, elen, out, cap, at) };
        if end >= cap {
            return 0;
        }
        end
    }

    /* Register a payload spelling (idempotent). Returns the slot, or
     * OPT_CAP when the table is full — the caller refuses then. */
    unsafe fn opt_add(&mut self, elem: *const u8, elen: usize) -> usize {
        if elen >= 128 {
            return OPT_CAP;
        }
        let mut s = 0usize;
        while s < self.opt_n {
            if self.opt_lens[s] == elen {
                let mut same = true;
                let mut i = 0usize;
                while i < elen {
                    if unsafe { self.opt_elems[s][i] } != unsafe { *elem.add(i) } {
                        same = false;
                        break;
                    }
                    i += 1;
                }
                if same {
                    return s;
                }
            }
            s += 1;
        }
        if self.opt_n >= OPT_CAP {
            return OPT_CAP;
        }
        let slot = self.opt_n;
        let mut i = 0usize;
        while i < elen {
            unsafe {
                self.opt_elems[slot][i] = *elem.add(i);
            }
            i += 1;
        }
        self.opt_lens[slot] = elen;
        self.opt_n += 1;
        slot
    }

    /* Result typedef name:
     *   rsx_res_<raw_lenT>e<2*raw_lenT hex>_<raw_lenE>e<2*raw_lenE hex>
     * Both payloads use the shared canonical hex component (hex_put_len_e)
     * — same identity rule as rsx_opt_/rsx_tuple_. */
    unsafe fn res_typedef_name(okt: *const u8, okl: usize, ert: *const u8, erl: usize, out: *mut u8, cap: usize) -> usize {
        let at = unsafe { bput(out, cap, 0, b"rsx_res_\0".as_ptr(), 8) };
        if at != 8 || cap <= 9 {
            return 0;
        }
        let mid = unsafe { Lower::hex_put_len_e(okt, okl, out, cap, at) };
        if mid >= cap || mid == 0 {
            return 0;
        }
        let mid2 = unsafe { bput(out, cap, mid, b"_\0".as_ptr(), 1) };
        if mid2 >= cap || mid2 == mid {
            return 0;
        }
        let end = unsafe { Lower::hex_put_len_e(ert, erl, out, cap, mid2) };
        if end >= cap {
            return 0;
        }
        end
    }

    /* Register a (T, E) payload pair (idempotent). Returns the slot, or
     * RES_CAP when the table is full — the caller refuses then. */
    unsafe fn res_add(&mut self, okt: *const u8, okl: usize, ert: *const u8, erl: usize) -> usize {
        if okl >= 128 || erl >= 128 {
            return RES_CAP;
        }
        let mut s = 0usize;
        while s < self.res_n {
            if self.res_ok_lens[s] == okl && self.res_err_lens[s] == erl {
                let mut same = true;
                let mut i = 0usize;
                while i < okl {
                    if unsafe { self.res_oks[s][i] } != unsafe { *okt.add(i) } {
                        same = false;
                        break;
                    }
                    i += 1;
                }
                if same {
                    let mut j = 0usize;
                    while j < erl {
                        if unsafe { self.res_errs[s][j] } != unsafe { *ert.add(j) } {
                            same = false;
                            break;
                        }
                        j += 1;
                    }
                }
                if same {
                    return s;
                }
            }
            s += 1;
        }
        if self.res_n >= RES_CAP {
            return RES_CAP;
        }
        let slot = self.res_n;
        let mut i = 0usize;
        while i < okl {
            unsafe {
                self.res_oks[slot][i] = *okt.add(i);
            }
            i += 1;
        }
        let mut j = 0usize;
        while j < erl {
            unsafe {
                self.res_errs[slot][j] = *ert.add(j);
            }
            j += 1;
        }
        self.res_ok_lens[slot] = okl;
        self.res_err_lens[slot] = erl;
        self.res_n += 1;
        slot
    }

    /* Tuple typedef name:
     *   rsx_tuple_<count>_<raw_len0>e<2*raw_len0 hex>_<raw_len1>e<2*raw_len1 hex>_…
     * Each element is encoded with the shared canonical hex component
     * (hex_put_len_e), so every element boundary is unambiguous (the
     * decimal raw_len fixes how many hex digits follow the 'e') and the
     * bytes used for identity are the exact raw canonical C-type bytes.
     * Returns the name length; 0 when the name does not fit cap whole. */
    unsafe fn tup_typedef_name(elems: *const [u8; 64], lens: *const usize, n: usize, out: *mut u8, cap: usize) -> usize {
        let mut at = unsafe { bput(out, cap, 0, b"rsx_tuple_\0".as_ptr(), 10) };
        if at != 10 || cap <= 11 {
            return 0;
        }
        /* element count first: `rsx_tuple_2_...` */
        let mut cd = [0u8; 20];
        let mut cn = 0usize;
        let mut cnt = n;
        if cnt == 0 {
            cd[0] = b'0';
            cn = 1;
        } else {
            while cnt > 0 && cn < cd.len() {
                cd[cn] = b'0' + (cnt % 10) as u8;
                cnt /= 10;
                cn += 1;
            }
            /* count decimal did not fit — refuse rather than print wrong */
            if cnt > 0 {
                return 0;
            }
        }
        if 10 + cn >= cap {
            return 0;
        }
        let mut q = cn;
        while q > 0 {
            q -= 1;
            unsafe {
                *out.add(at) = cd[q];
            }
            at += 1;
        }
        unsafe {
            *out.add(at) = 0;
        }
        let mut f = 0usize;
        while f < n {
            if at + 1 >= cap {
                return 0;
            }
            unsafe {
                *out.add(at) = b'_';
            }
            at += 1;
            let elen = unsafe { *lens.add(f) };
            let elem = unsafe { (*elems.add(f)).as_ptr() };
            let end = unsafe { Lower::hex_put_len_e(elem, elen, out, cap, at) };
            if end >= cap {
                return 0;
            }
            at = end;
            f += 1;
        }
        at
    }

    /* Register a tuple signature (idempotent). elems/lens describe the
     * rendered C element types; returns the slot or TUP_CAP when the
     * table is full — the caller refuses then. */
    unsafe fn tup_add(&mut self, elems: *const [u8; 64], lens: *const usize, n: usize) -> usize {
        if n == 0 || n > TUP_MAXF {
            return TUP_CAP;
        }
        let mut s = 0usize;
        while s < self.tup_n {
            if self.tup_counts[s] != n {
                s += 1;
                continue;
            }
            let mut same = true;
            let mut f = 0usize;
            while f < n {
                let a = s * TUP_MAXF + f;
                if self.tup_lens[a] != unsafe { *lens.add(f) } {
                    same = false;
                    break;
                }
                let mut i = 0usize;
                while i < self.tup_lens[a] {
                    if self.tup_elems[a][i] != unsafe { (*elems.add(f))[i] } {
                        same = false;
                        break;
                    }
                    i += 1;
                }
                if !same {
                    break;
                }
                f += 1;
            }
            if same {
                return s;
            }
            s += 1;
        }
        if self.tup_n >= TUP_CAP {
            return TUP_CAP;
        }
        let slot = self.tup_n;
        let mut f = 0usize;
        while f < n {
            let a = slot * TUP_MAXF + f;
            let elen = unsafe { *lens.add(f) };
            let mut i = 0usize;
            while i < elen {
                self.tup_elems[a][i] = unsafe { (*elems.add(f))[i] };
                i += 1;
            }
            self.tup_lens[a] = elen;
            f += 1;
        }
        self.tup_counts[slot] = n;
        self.tup_n += 1;
        slot
    }

    /* Find a tuple signature slot by its typedef name (name_len bytes).
     * Returns the slot or TUP_CAP when no signature matches. A signature
     * whose canonical name refuses to render in the scratch cap is not a
     * match for any input name (its name is not representable here). */
    unsafe fn tup_find(&mut self, name: *const u8, name_len: usize) -> usize {
        let mut s = 0usize;
        while s < self.tup_n {
            let n = self.tup_counts[s];
            let basep = self.tup_elems.as_ptr().add(s * TUP_MAXF);
            let blens = self.tup_lens.as_ptr().add(s * TUP_MAXF);
            let need = unsafe { Lower::tup_name_need(blens, n) };
            let tdn = if need == 0 {
                core::ptr::null_mut()
            } else {
                unsafe { self.name_tmp(need) }
            };
            if tdn.is_null() {
                s += 1;
                continue;
            }
            let tdl = unsafe { Lower::tup_typedef_name(basep, blens, n, tdn, need) };
            if tdl > 0 && tdl == name_len {
                let mut same = true;
                let mut j = 0usize;
                while j < tdl {
                    if unsafe { *tdn.add(j) } != unsafe { *name.add(j) } {
                        same = false;
                        break;
                    }
                    j += 1;
                }
                if same {
                    return s;
                }
            }
            s += 1;
        }
        TUP_CAP
    }

    /* Static/const type table: register a top-level name with its rendered
     * C type (declare-only pass; before fn bodies). Idempotent: a re-run
     * (declare + full pass) overwrites the same slot by name. */
    unsafe fn st_add(&mut self, name: *const u8, nlen: usize, ct: *const u8, ctlen: usize) {
        if nlen >= 64 || ctlen >= 128 {
            return;
        }
        let mut s = 0usize;
        while s < self.st_n {
            if self.st_name_lens[s] == nlen {
                let mut same = true;
                let mut i = 0usize;
                while i < nlen {
                    if unsafe { self.st_names[s][i] } != unsafe { *name.add(i) } {
                        same = false;
                        break;
                    }
                    i += 1;
                }
                if same {
                    /* refresh the type (declare + full pass) */
                    let mut j = 0usize;
                    while j < ctlen {
                        unsafe {
                            self.st_cts[s][j] = *ct.add(j);
                        }
                        j += 1;
                    }
                    self.st_ct_lens[s] = ctlen;
                    return;
                }
            }
            s += 1;
        }
        if self.st_n >= ST_CAP {
            return;
        }
        let slot = self.st_n;
        let mut i = 0usize;
        while i < nlen {
            unsafe {
                self.st_names[slot][i] = *name.add(i);
            }
            i += 1;
        }
        let mut j = 0usize;
        while j < ctlen {
            unsafe {
                self.st_cts[slot][j] = *ct.add(j);
            }
            j += 1;
        }
        self.st_name_lens[slot] = nlen;
        self.st_ct_lens[slot] = ctlen;
        self.st_n += 1;
    }

    /* Type of a bare name, if it names a top-level static/const. Returns
     * the spelling into `out`; 0 when unknown. */
    unsafe fn st_find(&self, name: *const u8, nlen: usize, out: *mut u8, cap: usize) -> usize {
        let mut s = 0usize;
        while s < self.st_n {
            if self.st_name_lens[s] == nlen {
                let mut same = true;
                let mut i = 0usize;
                while i < nlen {
                    if unsafe { self.st_names[s][i] } != unsafe { *name.add(i) } {
                        same = false;
                        break;
                    }
                    i += 1;
                }
                if same {
                    let cl = self.st_ct_lens[s];
                    if cl >= cap {
                        return 0;
                    }
                    let mut j = 0usize;
                    while j < cl {
                        unsafe {
                            *out.add(j) = self.st_cts[s][j];
                        }
                        j += 1;
                    }
                    unsafe {
                        *out.add(cl) = 0;
                    }
                    return cl;
                }
            }
            s += 1;
        }
        0
    }

    /* Does the rendered C type name a struct-shaped Option typedef
     * (rsx_opt_<raw_len>e<2*raw_len hex>)? Strict decode of the canonical
     * component: the payload C-type bytes recovered exactly. Returns the
     * payload length copied into out (NUL-terminated), or 0 when ct is
     * not a well-formed canonical Option name — never a partial decode. */
    unsafe fn opt_typedef_elem(ct: *const u8, n: usize, out: *mut u8, cap: usize) -> usize {
        if n < 10 {
            return 0;
        }
        let pre = b"rsx_opt_\0".as_ptr();
        let mut i = 0usize;
        while i < 8 {
            if unsafe { *ct.add(i) } != unsafe { *pre.add(i) } {
                return 0;
            }
            i += 1;
        }
        /* `<raw_len>e` — decimal raw length, then 'e'. The payload is
         * exactly raw_len bytes encoded as 2*raw_len lowercase hex digits
         * (the encoded char count, not the raw length). The R form
         * (`rsx_opt_R<raw>`, row-named payloads) carries the payload
         * verbatim after the marker — no length digits at all. */
        let mut at = 8usize;
        if at < n && unsafe { *ct.add(at) } == b'R' {
            at += 1;
            let plen2 = n - at;
            if plen2 == 0 || plen2 + 1 > cap || out.is_null() {
                return 0;
            }
            unsafe {
                core::ptr::copy_nonoverlapping(ct.add(at), out, plen2);
                *out.add(plen2) = 0;
            }
            return plen2;
        }
        let mut plen: usize = 0;
        let mut sawdigit = false;
        while at < n && unsafe { *ct.add(at) } >= b'0' && unsafe { *ct.add(at) } <= b'9' {
            let d = (unsafe { *ct.add(at) } - b'0') as usize;
            /* decimal length overflow: refuse before it wraps */
            if plen > (usize::MAX - d) / 10 {
                return 0;
            }
            plen = plen * 10 + d;
            sawdigit = true;
            at += 1;
        }
        if !sawdigit || at >= n || unsafe { *ct.add(at) } != b'e' {
            return 0;
        }
        at += 1;
        /* zero-length payload: no valid representation requires it (every
         * C type is at least one byte) — refuse */
        if plen == 0 {
            return 0;
        }
        /* encoded-size multiplication overflow: 2*plen must not wrap */
        if plen > (usize::MAX - 1) / 2 {
            return 0;
        }
        let hex_len = plen * 2;
        /* overlong / truncated: the remaining digits are exactly 2*plen */
        if n - at != hex_len {
            return 0;
        }
        /* the decoded payload (+ NUL) must fit the caller's buffer whole */
        if plen + 1 > cap || out.is_null() {
            return 0;
        }
        /* decode: exactly 2*plen lowercase hex digits, byte pairs.
         * Two-pass: validate EVERY digit before the first destination
         * write — a malformed digit at position k leaves out untouched
         * (the caller's buffer holds no partial payload). */
        let mut w = 0usize;
        while w < plen {
            let hi = unsafe { Lower::hex_val(*ct.add(at + w * 2)) };
            let lo = unsafe { Lower::hex_val(*ct.add(at + w * 2 + 1)) };
            if hi == 0xFF || lo == 0xFF {
                return 0;
            }
            w += 1;
        }
        w = 0;
        while w < plen {
            let hi = unsafe { Lower::hex_val(*ct.add(at + w * 2)) };
            let lo = unsafe { Lower::hex_val(*ct.add(at + w * 2 + 1)) };
            unsafe {
                *out.add(w) = (hi << 4) | lo;
            }
            w += 1;
        }
        unsafe {
            *out.add(plen) = 0;
        }
        plen
    }

    /* Decode BOTH payload spellings from an rsx_res_<T>_<E> typedef name.
     * Mirrors opt_typedef_elem's <raw_len>e<2*raw_len hex> decode, twice,
     * split at the '_' between the two encoded components. Returns the
     * Ok-payload length (both buffers NUL-terminated); 0 on any
     * malformed input. */
    unsafe fn res_typedef_elem(ct: *const u8, n: usize, ok_out: *mut u8, ok_cap: usize, err_out: *mut u8, err_cap: usize) -> usize {
        if n < 9 {
            return 0;
        }
        let pre = b"rsx_res_\0".as_ptr();
        let mut i = 0usize;
        while i < 8 {
            if unsafe { *ct.add(i) } != unsafe { *pre.add(i) } {
                return 0;
            }
            i += 1;
        }
        /* first component: decimal len, 'e', hex digits, then '_' */
        let mut at = 8usize;
        let mut plen: usize = 0;
        while at < n && unsafe { *ct.add(at) } >= b'0' && unsafe { *ct.add(at) } <= b'9' {
            let d = (unsafe { *ct.add(at) } - b'0') as usize;
            if plen > (usize::MAX - d) / 10 {
                return 0;
            }
            plen = plen * 10 + d;
            at += 1;
        }
        if at >= n || unsafe { *ct.add(at) } != b'e' {
            return 0;
        }
        at += 1;
        if plen > (usize::MAX - 1) / 2 || at + plen * 2 > n {
            return 0;
        }
        /* decode component 1 into ok_out */
        if plen + 1 > ok_cap || ok_out.is_null() {
            return 0;
        }
        let mut w = 0usize;
        while w < plen {
            let hi = unsafe { Lower::hex_val(*ct.add(at + w * 2)) };
            let lo = unsafe { Lower::hex_val(*ct.add(at + w * 2 + 1)) };
            if hi == 0xFF || lo == 0xFF {
                return 0;
            }
            unsafe {
                *ok_out.add(w) = (hi << 4) | lo;
            }
            w += 1;
        }
        unsafe {
            *ok_out.add(plen) = 0;
        }
        at += plen * 2;
        /* separator '_' */
        if at >= n || unsafe { *ct.add(at) } != b'_' {
            return 0;
        }
        at += 1;
        /* second component */
        let mut plen2: usize = 0;
        while at < n && unsafe { *ct.add(at) } >= b'0' && unsafe { *ct.add(at) } <= b'9' {
            let d = (unsafe { *ct.add(at) } - b'0') as usize;
            if plen2 > (usize::MAX - d) / 10 {
                return 0;
            }
            plen2 = plen2 * 10 + d;
            at += 1;
        }
        if at >= n || unsafe { *ct.add(at) } != b'e' {
            return 0;
        }
        at += 1;
        if plen2 > (usize::MAX - 1) / 2 || at + plen2 * 2 != n {
            return 0;
        }
        if plen2 + 1 > err_cap || err_out.is_null() {
            return 0;
        }
        let mut w2 = 0usize;
        while w2 < plen2 {
            let hi = unsafe { Lower::hex_val(*ct.add(at + w2 * 2)) };
            let lo = unsafe { Lower::hex_val(*ct.add(at + w2 * 2 + 1)) };
            if hi == 0xFF || lo == 0xFF {
                return 0;
            }
            unsafe {
                *err_out.add(w2) = (hi << 4) | lo;
            }
            w2 += 1;
        }
        unsafe {
            *err_out.add(plen2) = 0;
        }
        plen
    }

    /* Does this STRUCT item carry the union marker ATTR? (from the `union`
     * keyword dispatch in parse_item — text is exactly "union"). */
    unsafe fn has_union_marker(&mut self, item: *const pm_jit_rsx_ast_t) -> bool {
        let kids = unsafe { (*item).kids };
        let nk = unsafe { (*item).n_kids } as usize;
        let mut i = 0usize;
        while i < nk {
            let k = unsafe { *kids.add(i) };
            if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::ATTR {
                let t = unsafe { (*k).text };
                let tl = unsafe { (*k).text_len };
                if tl == 5 && !t.is_null() && unsafe { z_eq(t, tl, b"union\0".as_ptr()) } {
                    return true;
                }
            }
            i += 1;
        }
        false
    }

    /* Fieldless enums lower to `enum Name { A, B };` + int constants so
     * variants work in expressions; data-carrying variants take the
     * tagged-union shape: `struct Name { uint32_t _tag; union { <per
     * variant payload fields> } _u; }` + `<Name>_<Variant>` tag
     * constants. Single-payload enums (one variant carries data) are
     * the shape gen's Plane/GuestKinds plane needs. */
    unsafe fn lower_enum(&mut self, item: *const pm_jit_rsx_ast_t) {
        let name = unsafe { (*item).text };
        let nlen = unsafe { (*item).text_len };
        let line = unsafe { (*item).line };
        let kids = unsafe { (*item).kids };
        let nk = unsafe { (*item).n_kids } as usize;
        /* pass 1: classify variants — fieldless, literal discriminant,
         * or single-payload (data-carrying). Multi-field or struct-variant
         * payloads still refuse. */
        let mut bad = false;
        let mut any_payload = false;
        let mut i = 0usize;
        while i < nk {
            let k = unsafe { *kids.add(i) };
            if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::ENUM_VARIANT {
                if unsafe { (*k).n_kids } > 0 {
                    let vk = unsafe { (*k).kids };
                    let first = unsafe { *vk.add(0) };
                    if unsafe { (*first).kind } == pm_jit_rsx_ast_kind::LITERAL {
                        /* lone INT discriminant — fine, not data */
                    } else {
                        any_payload = true;
                        /* single-payload: the variant has exactly one
                         * payload expression node (a tuple of one type);
                         * more than one kid is a multi-payload variant. */
                        if unsafe { (*k).n_kids } > 1 {
                            bad = true;
                        }
                    }
                }
            }
            i += 1;
        }
        if bad {
            unsafe {
                self.err(b"unsupported: enum with data-carrying variants\0".as_ptr(), line);
            }
            return;
        }
        if !any_payload {
            self.lower_enum_fieldless(item, name, nlen, line);
            return;
        }
        /* the tagged-union shape. Payload rows: variant text + its
         * payload's C type, rendered now (the payload is a TYPE node).
         * A variant with NO payload contributes no union member but
         * still gets its tag constant. pass A5 runs this AFTER pass A
         * completed every struct, so a payload naming a unit struct is
         * a complete type here — no forward tags needed. */
        self.out.puts(b"#line \0".as_ptr());
        unsafe { self.out.put_u32(line) };
        self.out.puts(b" \"__impl__.rs\"\n\0".as_ptr());
        /* the struct tag matches the A0 forward (`typedef struct N N;`)
         * — an anonymous body would conflict with it */
        self.out.puts(b"typedef struct \0".as_ptr());
        self.out.put(name, nlen);
        self.out.puts(b" { uint32_t _tag; union {\n\0".as_ptr());
        i = 0;
        while i < nk {
            let k = unsafe { *kids.add(i) };
            if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::ENUM_VARIANT
                && unsafe { (*k).n_kids } > 0
            {
                let vk = unsafe { (*k).kids };
                let first = unsafe { *vk.add(0) };
                if unsafe { (*first).kind } != pm_jit_rsx_ast_kind::LITERAL {
                    let pt = self.arena_tmp();
                    let pn = unsafe { self.ctype(first, pt, 128) };
                    if pn == 0 {
                        return;
                    }
                    self.out.puts(b"        \0".as_ptr());
                    self.out.put(pt, pn);
                    self.out.putc(b' ');
                    self.out.put(unsafe { (*k).text }, unsafe { (*k).text_len });
                    self.out.puts(b";\n\0".as_ptr());
                }
            }
            i += 1;
        }
        self.out.puts(b"    } _u; } \0".as_ptr());
        self.out.put(name, nlen);
        self.out.puts(b";\n\0".as_ptr());
        /* tag constants: <Name>_<Variant> — payload variants and
         * fieldless variants share one counter. */
        let mut tag: u32 = 0;
        i = 0;
        while i < nk {
            let k = unsafe { *kids.add(i) };
            if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::ENUM_VARIANT {
                self.out.puts(b"#define \0".as_ptr());
                self.out.put(name, nlen);
                self.out.putc(b'_');
                self.out.put(unsafe { (*k).text }, unsafe { (*k).text_len });
                self.out.puts(b" \0".as_ptr());
                self.out.put_u32(tag);
                self.out.putc(b'u');
                self.out.puts(b"\n\0".as_ptr());
                tag += 1;
            }
            i += 1;
        }
        self.out.putc(b'\n');
    }

    unsafe fn lower_enum_fieldless(&mut self, item: *const pm_jit_rsx_ast_t, name: *const u8, nlen: usize, line: u32) {
        let kids = unsafe { (*item).kids };
        let nk = unsafe { (*item).n_kids } as usize;
        let mut variants = 0usize;
        let _ = &variants;
        self.out.puts(b"#line \0".as_ptr());
        unsafe { self.out.put_u32(line) };
        self.out.puts(b" \"__impl__.rs\"\n\0".as_ptr());
        self.out.puts(b"typedef enum \0".as_ptr());
        self.out.put(name, nlen);
        self.out.puts(b" \0".as_ptr());
        self.out.put(name, nlen);
        self.out.puts(b";\n\0".as_ptr());
        self.out.puts(b"enum \0".as_ptr());
        self.out.put(name, nlen);
        self.out.puts(b" {\n\0".as_ptr());
        let mut i = 0usize;
        while i < nk {
            let k = unsafe { *kids.add(i) };
            if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::ENUM_VARIANT {
                /* member name: Enum_Variant (flat namespace; variants are
                 * referenced in exprs as `Name::Variant` -> joined with '_'). */
                self.out.puts(b"    \0".as_ptr());
                self.out.put(name, nlen);
                self.out.putc(b'_');
                self.out.put(unsafe { (*k).text }, unsafe { (*k).text_len });
                self.out.puts(b",\n\0".as_ptr());
            }
            i += 1;
        }
        self.out.puts(b"};\n\0".as_ptr());
        self.out.putc(b'\n');
        let _ = variants;
    }

    /* static/const: `static const C_T name = init;` */
    unsafe fn lower_static(&mut self, item: *const pm_jit_rsx_ast_t, declare_only: usize) {
        let name = unsafe { (*item).text };
        let nlen = unsafe { (*item).text_len };
        let line = unsafe { (*item).line };
        let kids = unsafe { (*item).kids };
        let nk = unsafe { (*item).n_kids } as usize;
        let mut ty: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
        let mut init: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
        let mut is_mut_static = false;
        let mut i = 0usize;
        while i < nk {
            let k = unsafe { *kids.add(i) };
            let kk = unsafe { (*k).kind };
            if kk == pm_jit_rsx_ast_kind::TYPE {
                if ty.is_null() {
                    ty = k;
                } else {
                    init = k;
                }
            } else if kk == pm_jit_rsx_ast_kind::ATTR {
                let at = unsafe { (*k).text };
                let atl = unsafe { (*k).text_len };
                if unsafe { z_eq(at, atl, b"mut\0".as_ptr()) } {
                    is_mut_static = true;
                }
            } else {
                init = k;
            }
            i += 1;
        }
        if ty.is_null() {
            unsafe {
                self.err(b"static without a type\0".as_ptr(), line);
            }
            return;
        }
        let ct = self.arena_tmp();
        let mut ct_len = unsafe { self.ctype(ty, ct, 128) };
        if ct_len == 0 {
            return;
        }
        /* Register the name with its rendered C type in the static table —
         * declare-only runs before any fn body, so use sites infer.
         * declare_only == 2: tentative declaration only (fn-naming
         * initializers defer the initialized definition to pass E, after
         * fn prototypes); fn bodies in pass D use the static, so the name
         * must be declared here even though the initializer comes later. */
        unsafe { self.st_add(name, nlen, ct, ct_len) };
        /* The declared type's render may have registered a fresh
         * Option-payload typedef (`Option<T_alias>` — the payload names a
         * type alias, so the pass-A matched flushes missed it and this is
         * the first ctype to render it). Flush pending Option typedefs now
         * — every one emits before this static's declaration line — but
         * only once the type passes are done: before that, a pending
         * payload may name a struct/alias the type passes have not
         * emitted yet (pass 0a consts run first and would emit the
         * Option typedef ahead of its own payload's typedef). The
         * per-slot done marks keep each typedef to one emission.
         * &[T] rows need the same flush and must NOT wait for
         * types_done: pass-0a consts (`const X: &[u8] = b".."`) intern
         * their row at the first static render — the typedef has to land
         * here or the declaration names an unknown type. Row typedefs
         * only depend on the element spelling (never a later item), so
         * the early flush is sound. */
        if self.types_done {
            unsafe { self.opt_emit_rest() };
        }
        unsafe { self.arr_emit_rest() };
        if declare_only == 2 {
            self.out.puts(b"#line \0".as_ptr());
            unsafe { self.out.put_u32(line) };
            self.out.puts(b" \"__impl__.rs\"\n\0".as_ptr());
            self.out.puts(b"static \0".as_ptr());
            self.out.put(ct, ct_len);
            self.out.putc(b' ');
            self.out.put(name, nlen);
            self.out.puts(b";\n\0".as_ptr());
            return;
        }
        /* Integer-literal const -> #define: C needs a constant expression
         * for array lengths / enum discriminants, and `static const size_t`
         * is not one. */
        if !init.is_null()
            && !is_mut_static
            && unsafe { (*init).kind } == pm_jit_rsx_ast_kind::LITERAL
        {
            let it = unsafe { (*init).text };
            let il = unsafe { (*init).text_len };
            if il > 0 && unsafe { *it } >= b'0' && unsafe { *it } <= b'9' {
                self.out.puts(b"#define \0".as_ptr());
                self.out.put(name, nlen);
                self.out.putc(b' ');
                unsafe { self.emit_literal(it, il) };
                self.out.puts(b"\n\0".as_ptr());
                return;
            }
        }
        self.out.puts(b"#line \0".as_ptr());
        unsafe { self.out.put_u32(line) };
        self.out.puts(b" \"__impl__.rs\"\n\0".as_ptr());
        self.out.puts(b"static \0".as_ptr());
        /* Interior mutability: a `static X: Mut<T>/UnsafeCell<T>` is written
         * through `.0.get()` even though the binding is not `mut`, so the C
         * storage must not be const. A transparent-newtype head (or
         * UnsafeCell/Cell) on the declared type marks it. */
        let mut interior_mut = false;
        {
            /* type nodes are TYPE-kind segs (`Mut`, `UnsafeCell`): the head
             * segment of the declared type names the wrapper. A qualified
             * generic path (`crate::util::lock::Mutex<T>`) carries its
             * plain-segment count in int_val — the wrapper is that count's
             * last plain segment, not the path's first segment (the same
             * head rule ctype_path applies). */
            let tk = unsafe { (*ty).kids };
            let tn = unsafe { (*ty).n_kids } as usize;
            if tn >= 1 {
                let nsegs = unsafe { (*ty).int_val } as usize;
                let head_i = if nsegs >= 1 && nsegs <= tn { nsegs - 1 } else { 0 };
                let head = unsafe { *tk.add(head_i) };
                let sn = unsafe { (*head).text };
                let sl = unsafe { (*head).text_len };
                if unsafe { z_eq(sn, sl, b"UnsafeCell\0".as_ptr()) }
                    || unsafe { z_eq(sn, sl, b"Cell\0".as_ptr()) }
                    || unsafe { z_eq(sn, sl, b"Mutex\0".as_ptr()) }
                    || unsafe { z_eq(sn, sl, b"SpinLock\0".as_ptr()) }
                    || unsafe { self.nt_find(sn, sl) }
                {
                    interior_mut = true;
                }
            }
        }
        if !is_mut_static && !interior_mut {
            self.out.puts(b"const \0".as_ptr());
            /* the ctype may already carry `const` from the Rust type (`&[u8]`
             * renders `const uint8_t *`) — one is enough, two is a syntax
             * error in C */
            if ct_len >= 6
                && unsafe { *ct.add(0) } == b'c'
                && unsafe { *ct.add(1) } == b'o'
                && unsafe { *ct.add(2) } == b'n'
                && unsafe { *ct.add(3) } == b's'
                && unsafe { *ct.add(4) } == b't'
                && unsafe { *ct.add(5) } == b' '
            {
                unsafe {
                    core::ptr::copy_nonoverlapping(ct.add(6), ct, ct_len - 6 + 1);
                }
                ct_len -= 6;
            }
        }
        unsafe {
            self.emit_declarator(ct, ct_len, name, nlen);
        }
        if declare_only == 0 {
            if !init.is_null() {
                /* C file-scope initializers must be constant expressions.
                 * Accepted shapes: literals, negated literals, array
                 * literals (incl. `[v; N]`), struct literals, and the
                 * constant-lowering calls (newtype/UnsafeCell::new wraps,
                 * core::ptr::null[_mut]). Anything else refuses here; a
                 * nested non-constant that slips through emission fails
                 * loudly in the C compiler, never a silent miscompile. */
                let mut init_top = init;
                while (unsafe { (*init_top).kind } == pm_jit_rsx_ast_kind::UNARY
                    && unsafe { (*init_top).n_kids } >= 1
                    && (unsafe { z_eq((*init_top).text, (*init_top).text_len, b"-\0".as_ptr()) }
                        /* `&[...]` / `&{...}`: a reference to a constant
                         * aggregate is a constant initializer — the fat
                         * pointer fields (ptr + len) are both constants.
                         * The ref operator in the whitelist: SKIP style
                         * `const X: &[&str] = &["a", "b"]`. */
                        || unsafe { z_eq((*init_top).text, (*init_top).text_len, b"&\0".as_ptr()) }))
                    && unsafe { (*init_top).text_len } == 1
                {
                    init_top = unsafe { *(*init_top).kids.add(0) };
                }
                let mut ok_shape = false;
                let ik = unsafe { (*init_top).kind };
                if ik == pm_jit_rsx_ast_kind::LITERAL
                    || ik == pm_jit_rsx_ast_kind::ARRAY
                    || ik == pm_jit_rsx_ast_kind::STRUCT_LIT
                {
                    ok_shape = true;
                }
                /* a PATH initializer is constant when it names a known
                 * enum variant — `Kind::Zero` (C: the enum member, a
                 * constant expression). Zero variants then take the
                 * all-zero elision; nonzero ones emit the member name. */
                if !ok_shape && ik == pm_jit_rsx_ast_kind::PATH {
                    let pk0 = unsafe { (*init_top).kids };
                    let pn0 = unsafe { (*init_top).n_kids } as usize;
                    if pn0 >= 1 {
                        let leaf = unsafe { *pk0.add(pn0 - 1) };
                        let lt = unsafe { (*leaf).text };
                        let ll = unsafe { (*leaf).text_len };
                        /* a PATH wrapping a STRUCT_LIT (`S { .. }` parses
                         * with the fields hung off the wrapping PATH) is
                         * the struct-literal initializer shape */
                        if unsafe { (*leaf).kind } == pm_jit_rsx_ast_kind::STRUCT_LIT {
                            ok_shape = true;
                        } else if !lt.is_null()
                            && ll > 0
                        {
                            let mut joined = self.arena_tmp();
                            let mut jat = 0usize;
                            let mut seg = 0usize;
                            while seg < pn0 {
                                let s = unsafe { *pk0.add(seg) };
                                if unsafe { (*s).kind } == pm_jit_rsx_ast_kind::PATH
                                    && unsafe { (*s).text_len } > 0
                                {
                                    jat = unsafe { bput(joined, 64, jat, (*s).text, (*s).text_len) };
                                    jat = unsafe { bput(joined, 64, jat, b"_\0".as_ptr(), 1) };
                                }
                                seg += 1;
                            }
                            if jat > 1 {
                                jat -= 1;
                            }
                            unsafe {
                                *joined.add(jat) = 0;
                            }
                            let mut found = false;
                            let _ = unsafe { self.enums.lookup(joined, jat, &mut found) };
                            if found {
                                ok_shape = true;
                            }
                        }
                    }
                }
                if !ok_shape && ik == pm_jit_rsx_ast_kind::CALL {
                    /* calls that lower to constants: newtype ctor (leaf in
                     * the nt table), UnsafeCell::new/Cell::new, ptr::null */
                    let ck0 = unsafe { (*init_top).kids };
                    let cn0 = unsafe { (*init_top).n_kids } as usize;
                    if cn0 >= 1 {
                        let callee = unsafe { *ck0.add(0) };
                        if unsafe { (*callee).kind } == pm_jit_rsx_ast_kind::PATH {
                            let pk0 = unsafe { (*callee).kids };
                            let pn0 = unsafe { (*callee).n_kids } as usize;
                            if pn0 >= 1 {
                                let leaf = unsafe { *pk0.add(pn0 - 1) };
                                let lt = unsafe { (*leaf).text };
                                let ll = unsafe { (*leaf).text_len };
                                if unsafe { self.nt_find(lt, ll) } {
                                    ok_shape = true;
                                }
                                if pn0 >= 2 {
                                    if unsafe { z_eq(lt, ll, b"new\0".as_ptr()) } {
                                        /* segment before `new` names the
                                         * wrapper, any qualification depth */
                                        let wrap = unsafe { *pk0.add(pn0 - 2) };
                                        let wt = unsafe { (*wrap).text };
                                        let wl = unsafe { (*wrap).text_len };
                                        if unsafe { z_eq(wt, wl, b"UnsafeCell\0".as_ptr()) }
                                            || unsafe { z_eq(wt, wl, b"Cell\0".as_ptr()) }
                                            || unsafe { z_eq(wt, wl, b"Mutex\0".as_ptr()) }
                                            || unsafe { z_eq(wt, wl, b"SpinLock\0".as_ptr()) }
                                        {
                                            ok_shape = true;
                                        }
                                    }
                                    if unsafe { z_eq(lt, ll, b"null_mut\0".as_ptr()) }
                                        || unsafe { z_eq(lt, ll, b"null\0".as_ptr()) }
                                    {
                                        /* any `..::ptr::null[_mut]` */
                                        let mut k = 0usize;
                                        while k + 1 < pn0 {
                                            let seg = unsafe { *pk0.add(k) };
                                            if unsafe { z_eq(unsafe { (*seg).text }, unsafe { (*seg).text_len }, b"ptr\0".as_ptr()) } {
                                                ok_shape = true;
                                                break;
                                            }
                                            k += 1;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                if !ok_shape {
                    self.err(b"unsupported: non-constant static initializer\0".as_ptr(), line);
                    return;
                }
                /* All-zero elision: an initializer that folds to zero at
                 * every byte has no .data content — emit the declaration
                 * bare (a C tentative definition) and let the storage live
                 * in .bss. Materializing a multi-megabyte zero blob (the
                 * fixed-cap registry table) through the object compiler's
                 * section machinery is exactly the allocation that breaks
                 * in-kernel compiles on a shared arena; a bare `static T x;`
                 * carries the same storage as SHT_NOBITS with zero file
                 * bytes. Only provably-zero initializers take this path —
                 * anything else keeps the explicit `= {...}` emission. */
                if unsafe { self.init_all_zero(init) } {
                    self.out.puts(b";\n\0".as_ptr());
                    self.out.putc(b'\n');
                    return;
                }
                self.out.puts(b" = \0".as_ptr());
                /* &str const with a literal initializer: the fat struct,
                 * not a bare char* — `(rsx_str_ref_t){"lit", sizeof-1}`.
                 * A bare literal initializes only the first field and
                 * leaves .n garbage (or fails outright). The owned
                 * String plane has NO constant literal form (a String
                 * owns heap bytes — a static initializer would point
                 * into read-only storage) and refuses here instead. */
                let is_str_ref = ct_len == 13
                    && unsafe { z_eq(ct, 13, b"rsx_str_ref_t\0".as_ptr()) };
                if is_str_ref
                    && !init.is_null()
                    && unsafe { (*init).kind } == pm_jit_rsx_ast_kind::LITERAL
                {
                    let t = unsafe { (*init).text };
                    let tl = unsafe { (*init).text_len };
                    if tl >= 2 && unsafe { *t } == b'"' {
                        self.out.puts(b"(\0".as_ptr());
                        self.out.put(ct, ct_len);
                        self.out.puts(b"){ (const uint8_t *)\0".as_ptr());
                        self.out.put(t, tl);
                        self.out.puts(b", sizeof \0".as_ptr());
                        self.out.put(t, tl);
                        self.out.puts(b" - 1 }\0".as_ptr());
                        self.out.puts(b";\n\0".as_ptr());
                        self.out.putc(b'\n');
                        return;
                    }
                }
                /* `static X: &[u8] = b"...";` — same fat-literal contract
                 * as the &str case above, on the slice row's typedef: a
                 * bare char* initializer would set only the first field
                 * and leave .n garbage (or refuse in the C compiler —
                 * tcc reports it as a mystery byte at the literal). */
                if ct_len > 8
                    && ct_len < 96
                    && unsafe { z_eq(ct, 8, b"rsx_arr_\0".as_ptr()) }
                    && !init.is_null()
                    && unsafe { (*init).kind } == pm_jit_rsx_ast_kind::LITERAL
                {
                    /* only the u8 row: the literal's bytes ARE the
                     * elements; any other element type has no literal
                     * form (refuse below via the generic path). */
                    let row = unsafe { self.arrs.find_by_name(ct, ct_len) };
                    if row < ARR_CAP {
                        let el = self.arrs.elems[row].as_ptr();
                        let eln = self.arrs.elem_lens[row];
                        if eln == 7 && unsafe { z_eq(el, 7, b"uint8_t\0".as_ptr()) } {
                            let t = unsafe { (*init).text };
                            let tl = unsafe { (*init).text_len };
                            /* byte-string literal text carries its `b`
                             * prefix (`b"asgi"`) — strip it for the C
                             * spelling; a plain str literal rides the
                             * &str arm above. */
                            let lit = if tl >= 3
                                && unsafe { *t } == b'b'
                                && unsafe { *t.add(1) } == b'"'
                            {
                                t.add(1)
                            } else {
                                t
                            };
                            let ll = if lit != t { tl - 1 } else { tl };
                            if ll >= 2 && unsafe { *lit } == b'"' {
                                self.out.puts(b"(\0".as_ptr());
                                self.out.put(ct, ct_len);
                                self.out.puts(b"){ (const uint8_t *)\0".as_ptr());
                                self.out.put(lit, ll);
                                self.out.puts(b", sizeof \0".as_ptr());
                                self.out.put(lit, ll);
                                self.out.puts(b" - 1 }\0".as_ptr());
                                self.out.puts(b";\n\0".as_ptr());
                                self.out.putc(b'\n');
                                return;
                            }
                        }
                    }
                }
                /* `const X: &[T] = &[...]`: the initializer borrows an
                 * array literal against the KNOWN declared row (ct is
                 * the row the ascription interned) — emit the compound
                 * literal against the declared row, never a re-derived
                 * one: expression-position re-derivation types `&str`
                 * elements as `const char *` (bare literal typing) and
                 * mints a second, mismatched row. */
                {
                    let mut it0 = init;
                    while unsafe { (*it0).kind } == pm_jit_rsx_ast_kind::UNARY
                        && unsafe { (*it0).n_kids } >= 1
                        && unsafe { z_eq((*it0).text, (*it0).text_len, b"&\0".as_ptr()) }
                    {
                        it0 = unsafe { *(*it0).kids.add(0) };
                    }
                    if ct_len > 8
                        && ct_len < 96
                        && unsafe { z_eq(ct, 8, b"rsx_arr_\0".as_ptr()) }
                        && unsafe { (*it0).kind } == pm_jit_rsx_ast_kind::ARRAY
                    {
                        let locals_sl = LocalTab::new(self.arena);
                        if locals_sl.is_null() {
                            self.ok = false;
                            return;
                        }
                        /* the ` = ` separator is already out (the shared
                         * constant-initializer prefix) — emit only the
                         * compound literal + terminator here. The fat
                         * slice is { elems-array, count }: the elements
                         * ride an anonymous array compound literal, the
                         * count is the array's literal length. */
                        let akids = unsafe { (*it0).kids };
                        let ank = unsafe { (*it0).n_kids } as usize;
                        let row = unsafe { self.arrs.find_by_name(ct, ct_len) };
                        if row >= ARR_CAP || ank == 0 {
                            self.ok = false;
                            return;
                        }
                        let el = self.arrs.elems[row].as_ptr();
                        let eln = self.arrs.elem_lens[row];
                        self.out.putc(b'(');
                        self.out.put(ct, ct_len);
                        self.out.puts(b"){ (\0".as_ptr());
                        self.out.put(el, eln);
                        self.out.puts(b"[]){ \0".as_ptr());
                        /* per-element: a `&str`-row element renders its
                         * literal as the fat struct, not a bare char* —
                         * the plain array emit has no element-type
                         * context and drops the length. */
                        {
                            let is_str_ref = eln == 13
                                && unsafe { z_eq(el, 13, b"rsx_str_ref_t\0".as_ptr()) };
                            let mut ai = 0usize;
                            while ai < ank {
                                if ai > 0 {
                                    self.out.putc(b',');
                                }
                                let av = unsafe { *akids.add(ai) };
                                if is_str_ref
                                    && unsafe { (*av).kind } == pm_jit_rsx_ast_kind::LITERAL
                                {
                                    let t = unsafe { (*av).text };
                                    let tl = unsafe { (*av).text_len };
                                    if tl >= 2 && unsafe { *t } == b'"' {
                                        self.out.puts(b"{ (const uint8_t *)\0".as_ptr());
                                        self.out.put(t, tl);
                                        self.out.puts(b", sizeof \0".as_ptr());
                                        self.out.put(t, tl);
                                        self.out.puts(b" - 1 }\0".as_ptr());
                                    } else {
                                        unsafe { self.emit_expr(av, &mut *locals_sl) };
                                    }
                                } else {
                                    unsafe { self.emit_expr(av, &mut *locals_sl) };
                                }
                                ai += 1;
                            }
                        }
                        self.out.puts(b" }, \0".as_ptr());
                        self.out.put_u32(ank as u32);
                        self.out.puts(b" };\n\0".as_ptr());
                        self.out.putc(b'\n');
                        return;
                    }
                }
                let locals = LocalTab::new(self.arena);
                if locals.is_null() {
                    self.ok = false;
                    return;
                }
                unsafe { self.emit_expr(init, &mut *locals) };
                /* a span OOM inside this body's locals must refuse the
                 * whole compile (specific error), not degrade inference */
                if unsafe { (*locals).oom } {
                    self.ok = false;
                }
            }
            self.out.puts(b";\n\0".as_ptr());
        } else {
            self.out.puts(b";\n\0".as_ptr());
        }
        self.out.putc(b'\n');
    }

    unsafe fn lower_type_alias(&mut self, item: *const pm_jit_rsx_ast_t) {
        let name = unsafe { (*item).text };
        let nlen = unsafe { (*item).text_len };
        let line = unsafe { (*item).line };
        let kids = unsafe { (*item).kids };
        let nk = unsafe { (*item).n_kids } as usize;
        /* last TYPE kid is the aliased type */
        let mut ty: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
        let mut i = 0usize;
        while i < nk {
            let k = unsafe { *kids.add(i) };
            if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::TYPE {
                ty = k;
            }
            i += 1;
        }
        if ty.is_null() {
            unsafe {
                self.err(b"type alias without a type\0".as_ptr(), line);
            }
            return;
        }
        /* A fn-ptr alias needs the C declarator form — `ret (*name)(params)` —
         * not the expression-style spelling ctype renders; the alias is the
         * one place the name sits inside the type. */
        if unsafe { (*ty).kind } == pm_jit_rsx_ast_kind::TYPE
            && unsafe { z_eq((*ty).text, (*ty).text_len, b"fnptr\0".as_ptr()) }
        {
            if unsafe { self.emit_fnptr_typedef(ty, name, nlen, line) } {
                /* register the alias with its resolved ret for call-site
                 * typing (call through a local bind typed by this alias) */
                let fk = unsafe { (*ty).kids };
                let fnk = unsafe { (*ty).n_kids } as usize;
                if fnk > 0 {
                    let rty = unsafe { *fk.add(fnk - 1) };
                    let rb = self.arena_tmp();
                    let rn = unsafe { self.ctype(rty, rb, 128) };
                    if rn > 0 {
                        unsafe { self.st_add(name, nlen, rb, rn) };
                    }
                }
                return;
            }
        }
        let ct = self.arena_tmp();
        let ct_len = unsafe { self.ctype(ty, ct, 128) };
        if ct_len == 0 {
            return;
        }
        self.out.puts(b"#line \0".as_ptr());
        unsafe { self.out.put_u32(line) };
        self.out.puts(b" \"__impl__.rs\"\n\0".as_ptr());
        self.out.puts(b"typedef \0".as_ptr());
        self.out.put(ct, ct_len);
        self.out.putc(b' ');
        self.out.put(name, nlen);
        self.out.puts(b";\n\0".as_ptr());
        self.out.putc(b'\n');
    }

    /* `typedef ret (*name)(param, ..);` for a `type X = .. fn(..) -> ..`
     * alias. kids layout: quals (unsafe/extern/"C"), params, ret (last,
     * always present — parse synthesizes an explicit void). */
    unsafe fn emit_fnptr_typedef(&mut self, ty: *const pm_jit_rsx_ast_t,
        name: *const u8, nlen: usize, line: u32) -> bool {
        let kids = unsafe { (*ty).kids };
        let nk = unsafe { (*ty).n_kids } as usize;
        if nk == 0 {
            return false;
        }
        let ret = unsafe { *kids.add(nk - 1) };
        let ret_buf = self.arena_tmp();
        let ret_len = unsafe { self.ctype(ret, ret_buf, 128) };
        if ret_len == 0 {
            return false;
        }
        self.out.puts(b"#line \0".as_ptr());
        unsafe { self.out.put_u32(line) };
        self.out.puts(b" \"__impl__.rs\"\n\0".as_ptr());
        self.out.puts(b"typedef \0".as_ptr());
        self.out.put(ret_buf, ret_len);
        self.out.puts(b" (*\0".as_ptr());
        self.out.put(name, nlen);
        self.out.puts(b")(\0".as_ptr());
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
                    i += 1;
                    continue;
                }
                if ptl > 0 && unsafe { *pt } == b'"' {
                    /* ABI string ("C") — not a C parameter */
                    i += 1;
                    continue;
                }
            }
            if !first {
                self.out.puts(b", \0".as_ptr());
            }
            let p_buf = self.arena_tmp();
            let pn = unsafe { self.ctype(pty, p_buf, 128) };
            if pn == 0 {
                return false;
            }
            self.out.put(p_buf, pn);
            first = false;
            i += 1;
        }
        if first {
            self.out.puts(b"void\0".as_ptr());
        }
        self.out.puts(b");\n\0".as_ptr());
        self.out.putc(b'\n');
        true
    }

    /* extern block members: prototypes only. */
    unsafe fn lower_extern_block(&mut self, item: *const pm_jit_rsx_ast_t) {
        let kids = unsafe { (*item).kids };
        let nk = unsafe { (*item).n_kids } as usize;
        /* pre-render every fn's param types (discard the spellings) so any
         * Option-payload typedefs register BEFORE the prototypes emit —
         * `Option<T_alias>` in a param (`fn set_wasm_test_runner(f:
         * Option<pm_wasmmod_registry_wasm_test_runner_t>)`) names an alias,
         * so the pass-A matched flushes missed it; the prototype below
         * spells `rsx_opt_<alias>` and needs the typedef ahead of it. Same
         * ordering contract as lower_static's flush, but here registration
         * itself must happen first (ctype runs inside the emit). */
        let mut i = 0usize;
        while i < nk {
            let k = unsafe { *kids.add(i) };
            let kk = unsafe { (*k).kind };
            if kk == pm_jit_rsx_ast_kind::FN {
                let fkd = unsafe { (*k).kids };
                let fkn = unsafe { (*k).n_kids } as usize;
                let mut j = 0usize;
                while j < fkn {
                    let c = unsafe { *fkd.add(j) };
                    let ckk = unsafe { (*c).kind };
                    let mut ty_node: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
                    if ckk == pm_jit_rsx_ast_kind::PARAM {
                        let ck = unsafe { (*c).kids };
                        let ckn = unsafe { (*c).n_kids } as usize;
                        if ckn >= 1 {
                            ty_node = unsafe { *ck.add(0) };
                        }
                    } else if ckk == pm_jit_rsx_ast_kind::TYPE {
                        /* ret type is the last TYPE kid (quals also arrive
                         * as TYPE — re-rendering them is harmless) */
                        ty_node = c;
                    }
                    if !ty_node.is_null() {
                        let scratch = self.arena_tmp();
                        let _ = unsafe { self.ctype(ty_node, scratch, 160) };
                    }
                    j += 1;
                }
            }
            i += 1;
        }
        if self.types_done {
            unsafe { self.opt_emit_rest() };
        }
        i = 0;
        while i < nk {
            let k = unsafe { *kids.add(i) };
            let kk = unsafe { (*k).kind };
            if kk == pm_jit_rsx_ast_kind::FN {
                /* declare-only: params + ret, no body */
                let mut body_kids: [*mut pm_jit_rsx_ast_t; 1] = [core::ptr::null_mut()];
                let _ = body_kids;
                unsafe { self.lower_fn(k, 1) };
            } else if kk == pm_jit_rsx_ast_kind::STATIC {
                unsafe { self.lower_static(k, 1) };
            }
            i += 1;
        }
    }
}

