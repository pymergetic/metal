/* ==== fn lowering + file driver ==== */

impl Lower {
    /* Params of a FN node: kids layout is attrs/quals(PARAM TYPE nodes and
     * ATTR nodes), then PARAM nodes, then optional ret TYPE, then body
     * BLOCK. layout_prefix_len tells how many kids are attrs. */
    unsafe fn lower_fn(&mut self, fnitem: *const pm_jit_rsx_ast_t, declare_only: usize) {
        let name = unsafe { (*fnitem).text };
        let nlen = unsafe { (*fnitem).text_len };
        let line = unsafe { (*fnitem).line };
        let kids = unsafe { (*fnitem).kids };
        let nk = unsafe { (*fnitem).n_kids } as usize;
        /* split: attrs (ATTR), quals (ATTR/TYPE with unsafe/extern text),
         * params (PARAM), ret (TYPE), body (BLOCK). The parser pushes them
         * in order: attrs+vis, quals, params, ret, body. */
        let mut params: [*const pm_jit_rsx_ast_t; 12] = [core::ptr::null(); 12];
        let mut n_params = 0usize;
        let mut ret: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
        let mut body: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
        let mut i = 0usize;
        while i < nk {
            let k = unsafe { *kids.add(i) };
            let kk = unsafe { (*k).kind };
            if kk == pm_jit_rsx_ast_kind::PARAM {
                if n_params < 12 {
                    params[n_params] = k;
                    n_params += 1;
                }
            } else if kk == pm_jit_rsx_ast_kind::BLOCK {
                body = k;
            } else if kk == pm_jit_rsx_ast_kind::TYPE {
                /* ret type is the last TYPE kid (quals also arrive as TYPE
                 * with text "extern"/"unsafe" or the ABI string literal) */
                let t = unsafe { (*k).text };
                let tl = unsafe { (*k).text_len };
                if tl == 0 || t.is_null() {
                    continue;
                }
                if unsafe { z_eq(t, tl, b"unsafe\0".as_ptr()) }
                    || unsafe { z_eq(t, tl, b"extern\0".as_ptr()) }
                    || (unsafe { *t } == b'"')
                {
                    /* qualifier — skip */
                } else {
                    ret = k;
                }
            }
            i += 1;
        }
        /* self param detection (method) for the mangled free-fn name. */
        let has_self = n_params > 0
            && unsafe { z_eq(
                unsafe { (*params[0]).text },
                unsafe { (*params[0]).text_len },
                b"self\0".as_ptr(),
            ) };
        let is_method = has_self;
        let _ = is_method;
        /* A bare top-level FN node with a `self` receiver is not a valid
         * free function — methods only appear inside impl blocks, where
         * lower_impl sets the receiver type and mangles the name before
         * delegating here. */
        if has_self && self.recv_len == 0 {
            unsafe {
                self.err(b"method outside impl\0".as_ptr(), line);
            }
            return;
        }
        /* C type of the return */
        let ret_ct = self.arena_tmp();
        let mut ret_len = 0usize;
        if !ret.is_null() {
            ret_len = unsafe { self.ctype(ret, ret_ct, 128) };
        } else {
            ret_len = unsafe { zput(ret_ct, 128, 0, b"void\0".as_ptr()) };
        }
        if ret_len == 0 {
            return;
        }
        /* Unit type pre-scan (bodies only — declare-only pass must not
         * run it): intern every Vec container this fn names — params,
         * ret, ascribed lets, casts — and flush their typedefs+ops at
         * file scope BEFORE the signature. A typedef after the
         * declarator (between it and the body's `{`) is a C parse
         * error; the pre-scan makes the block land ahead of the
         * definition. Idempotent by done[] marks. The opaque-hoist
         * notes ctype makes for bare names stay body-time (after every
         * opq_emit pass) exactly as before this pre-scan existed —
         * running it in the prototype pass would note them ahead of
         * pass D's hoist and mint bogus `typedef struct X * X *;`
         * lines. Options and tuples ride the same walk harmlessly
         * (their flushes are done-marked too). */
        /* ONE LocalTab per fn, shared by the pre-scan and the body
         * emission. The pre-scan's rows (params + speculative lets)
         * are rewound before emission starts — the emission
         * re-registers the same names itself. Two full tables per
         * fn was ~60 KiB/fn of arena (the self-host draw crept past
         * its 64 MiB gate); one table plus a rewind keeps the same
         * semantics at half the cost. */
        let mut locals: *mut LocalTab = core::ptr::null_mut();
        if declare_only == 0 && !body.is_null() {
            let before = self.vecs.n;
            let before_a = self.arrs.n;
            let before_b = self.btms.n;
            let before_l = self.locks.n;
            let before_o = self.opt_n;
            let before_r = self.res_n;
            let before_t = self.tup_n;
            locals = LocalTab::new(self.arena);
            if locals.is_null() {
                self.ok = false;
                return;
            }
            /* Param-holding locals for the expression probes: the
             * pre-scan now walks METHOD_CALL nodes too (a body-local
             * `x.rsplit(c).next()` interns an Option row only expr_ctype
             * can name), and its receiver is usually a param. */
            let pre_locals = locals;
            if !pre_locals.is_null() {
                let mut p = 0usize;
                while p < n_params {
                    let pk = params[p];
                    let pt = unsafe { (*pk).text };
                    let ptl = unsafe { (*pk).text_len };
                    let pkk = unsafe { (*pk).kids };
                    let pkn = unsafe { (*pk).n_kids } as usize;
                    if pkn >= 1 && ptl > 0 && !pt.is_null() {
                        let pty = unsafe { *pkk.add(0) };
                        let ct = self.arena_tmp();
                        let n = unsafe { self.ctype(pty, ct, 128) };
                        if n > 0 {
                            unsafe {
                                (*pre_locals).add(pt, ptl, ct, n, 1);
                            }
                        }
                    }
                    p += 1;
                }
            }
            self.pre_mode = true;
            unsafe { self.body_intern_types(fnitem, pre_locals) };
            self.pre_mode = false;
            self.pre_pool_at = 0;
            /* String typedefs land BEFORE the container rows: a tuple/vec/
             * opt row with a String payload names rsx_str_t in its own
             * typedef, so the string typedef must already exist (the
             * pre-scan flush order is the C order). Every emit here is
             * dep-gated (row_dep_pending), so a row the body names over
             * a not-yet-complete dep self-defers to the next pre-scan
             * or the file-scope fixpoint — the guards below only decide
             * whether the flush call is worth making at all. */
            unsafe { self.str_emit_rest() };
            unsafe { self.str_own_emit_rest() };
            if self.opt_n != before_o {
                unsafe { self.fnp_emit_rest() };
                unsafe { self.opt_emit_rest(0) };
            }
            if self.res_n != before_r {
                unsafe { self.res_emit_rest() };
            }
            if self.vecs.n != before {
                unsafe { self.vec_emit_rest(0) };
            }
            if self.arrs.n != before_a {
                unsafe { self.arr_emit_rest(0) };
            }
            if self.btms.n != before_b {
                unsafe { self.btm_emit_rest(0) };
            }
            if self.locks.n != before_l {
                unsafe { self.lock_emit_rest() };
            }
            if self.tup_n != before_t {
                unsafe { self.tup_emit_rest() };
            }
            /* rewind the pre-scan's rows: the body emission below starts
             * from a clean table (its own param/self registrations are
             * authoritative). Stale spans are unreachable arena garbage,
             * bounded by the pre-scan's walk — `oom` stays sticky so a
             * pre-scan allocation failure still refuses the compile. */
            unsafe {
                (*locals).n = 0;
                (*locals).nmarks = 0;
                (*locals).epoch += 1;
            }
        }
        /* #line + signature */
        self.out.puts(b"#line \0".as_ptr());
        unsafe { self.out.put_u32(line) };
        self.out.puts(b" \"__impl__.rs\"\n\0".as_ptr());
        self.out.put(ret_ct, ret_len);
        self.out.putc(b' ');
        self.out.put(name, nlen);
        self.out.putc(b'(');
        /* params */
        let mut p = 0usize;
        let mut first = true;
        while p < n_params {
            let pk = params[p];
            let pt = unsafe { (*pk).text };
            let ptl = unsafe { (*pk).text_len };
            /* receiver params are `self`/`&self`/`&mut self` spellings; the
             * method case was refused above, so this is a param named self
             * in a free fn — treat as a typed param below. */
            let is_recv = unsafe { z_eq(pt, ptl, b"self\0".as_ptr()) }
                || unsafe { z_eq(pt, ptl, b"&self\0".as_ptr()) }
                || unsafe { z_eq(pt, ptl, b"&mut self\0".as_ptr()) };
            if is_recv {
                if self.recv_len > 0 {
                    if !first {
                        self.out.puts(b", \0".as_ptr());
                    }
                    first = false;
                    self.out.put(self.recv_type.as_ptr(), self.recv_len);
                    self.out.puts(b" self\0".as_ptr());
                }
                p += 1;
                continue;
            }
            if !first {
                self.out.puts(b", \0".as_ptr());
            }
            first = false;
            let pkk = unsafe { (*pk).kids };
            let pkn = unsafe { (*pk).n_kids } as usize;
            if pkn >= 1 {
                let pty = unsafe { *pkk.add(0) };
                let ct = self.arena_tmp();
                let n = unsafe { self.ctype(pty, ct, 128) };
                if n == 0 {
                    return;
                }
                unsafe {
                    self.emit_declarator(ct, n, pt, ptl);
                }
            } else {
                /* untyped param — not valid Rust; refuse */
                unsafe {
                    self.err(b"parameter without a type\0".as_ptr(), line);
                }
                return;
            }
            p += 1;
        }
        if first {
            self.out.puts(b"void\0".as_ptr());
        }
        self.out.puts(b")\n\0".as_ptr());
        if declare_only != 0 {
            self.out.puts(b";\n\0".as_ptr());
            return;
        }
        /* body lowering from here on: `?` needs the fn's return type to
         * soundly emit its early `return 0`. */
        {
            let mut j = 0usize;
            while j < ret_len && j < 127 {
                self.cur_ret[j] = unsafe { *ret_ct.add(j) };
                j += 1;
            }
            self.cur_ret_len = j;
        }
        self.out.puts(b"{\n\0".as_ptr());
        /* body: the tail expression of a value-returning fn becomes
         * `return expr;` — C has no implicit block value. */
        if !body.is_null() {
            /* rename-shadow spellings are per-fn: name__0 restarts each
             * body so generated C is deterministic across fns. */
            self.shadow_ctr = 0;
            /* the shared per-fn tab — allocated in the pre-scan above
             * (declare_only==0 && body is guaranteed here: the fn
             * returned early otherwise). */
            if locals.is_null() {
                self.ok = false;
                self.cur_ret_len = 0;
                self.out.puts(b"}\n\0".as_ptr());
                self.out.putc(b'\n');
                return;
            }
            if self.recv_len > 0 {
                unsafe {
                    (*locals).add(b"self\0".as_ptr(), 4, self.recv_type.as_ptr(), self.recv_len, 1);
                }
            }
            /* Register every parameter so body inference can see them. */
            {
                let mut p = 0usize;
                while p < n_params {
                    let pk = params[p];
                    let pt = unsafe { (*pk).text };
                    let ptl = unsafe { (*pk).text_len };
                    let is_recv = unsafe { z_eq(pt, ptl, b"self\0".as_ptr()) }
                        || unsafe { z_eq(pt, ptl, b"&self\0".as_ptr()) }
                        || unsafe { z_eq(pt, ptl, b"&mut self\0".as_ptr()) };
                    if !is_recv {
                        let pkk = unsafe { (*pk).kids };
                        let pkn = unsafe { (*pk).n_kids } as usize;
                        if pkn >= 1 {
                            let pty = unsafe { *pkk.add(0) };
                            let ct = self.arena_tmp();
                            let n = unsafe { self.ctype(pty, ct, 128) };
                            if n > 0 {
                                unsafe {
                                    (*locals).add(pt, ptl, ct, n, 1);
                                }
                            }
                        }
                    }
                    p += 1;
                }
            }
            self.depth = 1;
            self.cur_body = body;
            if !ret.is_null() {
                let bk = unsafe { (*body).kids };
                let bn = unsafe { (*body).n_kids } as usize;
                let mut i = 0usize;
                while i + 1 < bn {
                    let st = unsafe { *bk.add(i) };
                    unsafe { self.emit_stmt(st, &mut *locals, 0) };
                    i += 1;
                }
                if bn > 0 {
                    let mut tail = unsafe { *bk.add(bn - 1) };
                    /* a tail expr stmt (`if .. { a } else { b }` with no
                     * trailing `;`) wraps the value form — unwrap so the
                     * tail classification sees IF/MATCH, not EXPR_STMT. */
                    if unsafe { (*tail).kind } == pm_jit_rsx_ast_kind::EXPR_STMT
                        && unsafe { (*tail).n_kids } >= 1
                    {
                        tail = unsafe { *(*tail).kids.add(0) };
                    }
                    let k = unsafe { (*tail).kind };
                    if k == pm_jit_rsx_ast_kind::IF {
                        /* tail if: each branch returns its own tail expr */
                        unsafe { self.emit_if_tail(tail, &mut *locals) };
                    } else if k == pm_jit_rsx_ast_kind::MATCH {
                        /* tail match: declare `ret` from the fn's C type and
                         * let each arm store into it, then return. */
                        self.indent();
                        self.out.put(ret_ct, ret_len);
                        self.out.puts(b" ret;\n\0".as_ptr());
                        unsafe { self.emit_match_value(tail, &mut *locals, b"ret\0".as_ptr(), 3) };
                        /* live guards release before the tail return — the
                         * value-tail path skips end_block entirely. */
                        unsafe { self.release_guards_all(&mut *locals) };
                        self.indent();
                        self.out.puts(b"return ret;\n\0".as_ptr());
                    } else if k == pm_jit_rsx_ast_kind::RETURN
                        || k == pm_jit_rsx_ast_kind::LET
                        || k == pm_jit_rsx_ast_kind::STMT
                        || k == pm_jit_rsx_ast_kind::EXPR_STMT
                        || k == pm_jit_rsx_ast_kind::ASSIGN
                        || k == pm_jit_rsx_ast_kind::LOOP
                        /* a value-typed fn with a statement-shaped macro
                         * tail: the expression macros (vec!/format!)
                         * ARE the return value; anything else keeps the
                         * old statement-skip (void-tail call macros,
                         * assert!-style) rather than a bogus `return`. */
                        || (k == pm_jit_rsx_ast_kind::MACRO && !self.macro_is_value(tail))
                    {
                        unsafe { self.emit_stmt(tail, &mut *locals, 0) };
                    } else if k == pm_jit_rsx_ast_kind::BLOCK {
                        /* tail block expr (typically an `unsafe { value }`
                         * wrapper): walk to the inner tail and return it. */
                        unsafe { self.emit_block_tail_ret(tail, &mut *locals) };
                    } else {
                        self.indent();
                        self.out.puts(b"return \0".as_ptr());
                        unsafe { self.emit_ret_value(tail, &mut *locals) };
                        self.out.puts(b";\n\0".as_ptr());
                    }
                    /* any tail shape: live guards release before the fn's
                     * closing brace (statement tails and diverging tails
                     * both land here after their own emission). */
                    unsafe { self.release_guards_all(&mut *locals) };
                }
            } else {
                unsafe { self.emit_block_stmt(body, &mut *locals) };
            }
            self.depth = 0;
            /* a span OOM inside this body's locals must refuse the whole
             * compile (specific error), not degrade inference */
            if unsafe { (*locals).oom } {
                self.ok = false;
            }
            /* the body is emitted: the LocalTab is fn-scoped scratch
             * (every span it hands out is an arena COPY, names live
             * inline) — free the ~23 KiB block so a 1400-fn unit does
             * not strand 32 MB of dead tables ahead of the C-compile
             * phase that shares this arena. The `oom` read above is the
             * table's last consumer. */
            unsafe {
                pm_util_mem_free(self.arena, locals as *mut u8);
            }
        }
        self.cur_ret_len = 0;
        self.cur_body = core::ptr::null();
        self.out.puts(b"}\n\0".as_ptr());
        self.out.putc(b'\n');
    }

    /* Methods inside an impl: free fn `Type_method` / `Type_Trait_method`. */
    unsafe fn lower_impl(&mut self, item: *const pm_jit_rsx_ast_t, declare_only: usize) {
        let kids = unsafe { (*item).kids };
        let nk = unsafe { (*item).n_kids } as usize;
        /* self type: first TYPE-path kid; trait: a TYPE node with text "trait" */
        let mut ty_name: *const u8 = b"\0".as_ptr();
        let mut ty_len = 0usize;
        let mut trait_name: *const u8 = b"\0".as_ptr();
        let mut trait_len = 0usize;
        let mut methods_start = 0usize;
        let mut i = 0usize;
        while i < nk {
            let k = unsafe { *kids.add(i) };
            if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::TYPE {
                let t = unsafe { (*k).text };
                let tl = unsafe { (*k).text_len };
                if unsafe { z_eq(t, tl, b"trait\0".as_ptr()) } {
                    /* trait TYPE node wraps the trait path */
                    let tk = unsafe { (*k).kids };
                    let tkn = unsafe { (*k).n_kids } as usize;
                    if tkn >= 1 {
                        let tpath = unsafe { *tk.add(0) };
                        let pk = unsafe { (*tpath).kids };
                        let pkn = unsafe { (*tpath).n_kids } as usize;
                        if pkn >= 1 {
                            let leaf = unsafe { *pk.add(pkn - 1) };
                            trait_name = unsafe { (*leaf).text };
                            trait_len = unsafe { (*leaf).text_len };
                        }
                    }
                } else if unsafe { z_eq(t, tl, b"path\0".as_ptr()) }
                    || unsafe { z_eq(t, tl, b"gpath\0".as_ptr()) }
                {
                    /* self type path — take the leaf */
                    let pk = unsafe { (*k).kids };
                    let pkn = unsafe { (*k).n_kids } as usize;
                    if pkn >= 1 {
                        let leaf = unsafe { *pk.add(pkn - 1) };
                        ty_name = unsafe { (*leaf).text };
                        ty_len = unsafe { (*leaf).text_len };
                    }
                }
            }
            if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::FN && methods_start == 0 {
                methods_start = i;
            }
            i += 1;
        }
        if ty_len == 0 {
            unsafe {
                self.err(b"impl without a self type\0".as_ptr(), unsafe { (*item).line });
            }
            return;
        }
        if methods_start == 0 {
            methods_start = nk;
        }
        i = methods_start;
        while i < nk {
            let k = unsafe { *kids.add(i) };
            if unsafe { (*k).kind } != pm_jit_rsx_ast_kind::FN {
                i += 1;
                continue;
            }
            /* mangled: Type_method or Type_Trait_method */
            let mname = self.arena_tmp();
            let mut at = 0usize;
            at = unsafe { bput(mname, 160, at, ty_name, ty_len) };
            if trait_len > 0 {
                at = unsafe { bput(mname, 160, at, b"_\0".as_ptr(), 1) };
                at = unsafe { bput(mname, 160, at, trait_name, trait_len) };
            }
            at = unsafe { bput(mname, 160, at, b"_\0".as_ptr(), 1) };
            at = unsafe { bput(mname, 160, at, unsafe { (*k).text }, unsafe { (*k).text_len }) };
            unsafe {
                *mname.add(at) = 0;
            }
            /* receiver C type: `Ty *` for &self/&mut self, `Ty` for self */
            let recv_buf = self.arena_tmp();
            let mut rl = unsafe { bput(recv_buf, 64, 0, ty_name, ty_len) };
            let kids_k = unsafe { (*k).kids };
            let nk_k = unsafe { (*k).n_kids } as usize;
            let mut by_value = false;
            let mut mi = 0usize;
            while mi < nk_k {
                let mk = unsafe { *kids_k.add(mi) };
                if unsafe { (*mk).kind } == pm_jit_rsx_ast_kind::PARAM {
                    let mt = unsafe { (*mk).text };
                    let mtl = unsafe { (*mk).text_len };
                    if unsafe { z_eq(mt, mtl, b"self\0".as_ptr()) } {
                        by_value = true;
                    }
                    break;
                }
                mi += 1;
            }
            if !by_value {
                rl = unsafe { bput(recv_buf, 64, rl, b" *\0".as_ptr(), 2) };
            }
            self.recv_len = 0usize;
            let mut ri = 0usize;
            while ri < rl && ri < 64 {
                self.recv_type[ri] = unsafe { *recv_buf.add(ri) };
                ri += 1;
            }
            self.recv_len = ri;
            /* Self resolution: the impl's own type — ctype renders any
             * `Self` spelling (return type, local ascriptions, struct
             * literals) as this name while the method lowers. NUL-
             * terminated: zput reads to the NUL and an unterminated
             * tail renders stale bytes. */
            {
                let mut si = 0usize;
                while si < ty_len && si < 63 {
                    self.self_ty[si] = unsafe { *ty_name.add(si) };
                    si += 1;
                }
                self.self_ty[si] = 0;
                self.self_ty_len = si;
            }
            /* emit with the mangled name: reuse lower_fn logic via a temp
             * FN-node copy with overridden text. */
            let saved = self.arena_tmp();
            unsafe {
                let mut cpy: *mut pm_jit_rsx_ast_t = self.arena_tmp() as *mut pm_jit_rsx_ast_t;
                core::ptr::copy_nonoverlapping(k, cpy, 1);
                /* text override: the mangled name */
                (*cpy).text = mname;
                (*cpy).text_len = at;
                self.lower_fn(cpy, declare_only);
            }
            self.recv_len = 0;
            self.self_ty_len = 0;
            let _ = saved;
            if !self.ok {
                return;
            }
            i += 1;
        }
    }

    /* Emit a pending tuple typedef (signature slot s) — the shared
     * `struct { A _0; B _1; } rsx_tuple_A_B;`. */
    unsafe fn tup_emit_one(&mut self, s: usize) {
        if self.tup_done[s] {
            return;
        }
        let n = self.tup_counts[s];
        if n == 0 || n > TUP_MAXF {
            return;
        }
        /* Any element naming an incomplete dep (a not-yet-emitted struct,
         * a pending container row) defers the whole row — a later
         * flush round (the file-scope fixpoint) retries it. Without
         * this, a tuple over a tuple, or a tuple over a Vec row still
         * pending, emits forward and names an unknown type. */
        {
            let mut fpre = 0usize;
            while fpre < n {
                let a = s * TUP_MAXF + fpre;
                let elen = self.tup_lens[a];
                let elem = self.tup_elems[a].as_ptr();
                if unsafe { self.row_dep_pending(elem, elen) } {
                    return;
                }
                fpre += 1;
            }
        }
        /* A tuple element may itself be a struct-Option
         * (`rsx_opt_<raw_len>e<hex>`) whose typedef is still pending —
         * emit those FIRST, or the tuple body references an undeclared
         * name (tuple-of-Option, e.g. `(Option<u32>, Option<u32>)`).
         * The element IS the Option's typedef name, so the lookup is by
         * name. */
        {
            let mut f0 = 0usize;
            while f0 < n {
                let a = s * TUP_MAXF + f0;
                let elen = self.tup_lens[a];
                let elem = self.tup_elems[a].as_ptr();
                unsafe { self.opt_emit_by_name(elem, elen) };
                f0 += 1;
            }
        }
        let base = self.tup_elems.as_ptr().add(s * TUP_MAXF);
        let blens = self.tup_lens.as_ptr().add(s * TUP_MAXF);
        let need = unsafe { Lower::tup_name_need(blens, n) };
        if need == 0 {
            unsafe {
                self.err(b"internal: tuple typedef name too long\0".as_ptr(), 0);
            }
            return;
        }
        let tdn = unsafe { self.name_tmp(need) };
        if tdn.is_null() {
            unsafe {
                self.err(b"internal: tuple typedef name too long\0".as_ptr(), 0);
            }
            return;
        }
        let tdn_len = unsafe { Lower::tup_typedef_name(base, blens, n, tdn, need) };
        if tdn_len == 0 {
            unsafe {
                self.err(b"internal: tuple typedef name too long\0".as_ptr(), 0);
            }
            return;
        }
        self.out.puts(b"typedef struct { \0".as_ptr());
        let mut f = 0usize;
        while f < n {
            let a = s * TUP_MAXF + f;
            let elen = self.tup_lens[a];
            let elem = self.tup_elems[a].as_ptr();
            self.out.put(elem, elen);
            self.out.puts(b" _\0".as_ptr());
            /* field index as ASCII — f < 4, one digit */
            let d = b'0' + f as u8;
            self.out.putc(d);
            self.out.puts(b"; \0".as_ptr());
            f += 1;
        }
        self.out.puts(b"} \0".as_ptr());
        self.out.put(tdn, tdn_len);
        self.out.puts(b";\n\0".as_ptr());
        self.tup_done[s] = true;
    }

    /* Emit any still-pending struct-Option typedef whose RENDERED NAME
     * equals `name` — a tuple-of-Option element references the Option by
     * its typedef name (`rsx_opt_<raw_len>e<hex>`), so the pending Option
     * is looked up by name, not by payload. */
    unsafe fn opt_emit_by_name(&mut self, name: *const u8, nlen: usize) {
        if nlen == 0 || name.is_null() || nlen > 160 {
            return;
        }
        let mut s = 0usize;
        while s < self.opt_n {
            if unsafe { self.opt_done[s] } {
                s += 1;
                continue;
            }
            let elem = self.opt_elems[s].as_ptr();
            let elen = self.opt_lens[s];
            let tdn = self.arena_tmp();
            let tdn_len = unsafe { Lower::opt_typedef_name(elem, elen, tdn, 160) };
            if tdn_len > 0 && tdn_len == nlen {
                let mut j = 0usize;
                let mut eq = true;
                while j < tdn_len {
                    if unsafe { *tdn.add(j) } != unsafe { *name.add(j) } {
                        eq = false;
                        break;
                    }
                    j += 1;
                }
                if eq {
                    self.out.puts(b"typedef struct { \0".as_ptr());
                    self.out.put(elem, elen);
                    self.out.puts(b" _v; bool _has; } \0".as_ptr());
                    self.out.put(tdn, tdn_len);
                    self.out.puts(b";\n\0".as_ptr());
                    unsafe {
                        self.opt_done[s] = true;
                    }
                }
            }
            s += 1;
        }
    }

    /* Emit pending tuple typedefs whose element spelling names the type
     * just declared — same pass-A ordering contract as opt_emit_for.
     * Readiness: a tuple typedef body names EVERY element by value, so it
     * may only land once each element that names a unit STRUCT has that
     * struct's typedef complete. A tuple matching the just-declared type
     * while another element still names an unemitted struct (file order
     * puts it later) must wait: a later tup_emit_for for that struct, or
     * tup_emit_rest after the type pass, emits it. */
    unsafe fn tup_ready(
        &mut self,
        kids: *mut *mut pm_jit_rsx_ast_t,
        nk: usize,
        s: usize,
    ) -> bool {
        let n = self.tup_counts[s];
        let mut f = 0usize;
        while f < n {
            let a = s * TUP_MAXF + f;
            let elen = self.tup_lens[a];
            let elem = self.tup_elems[a].as_ptr();
            let dep = unsafe { self.tyorder_find(kids, nk, elem, elen) };
            if !dep.is_null() && !unsafe { self.tydone_find(elem, elen) } {
                return false;
            }
            f += 1;
        }
        true
    }

    unsafe fn tup_emit_for(
        &mut self,
        kids: *mut *mut pm_jit_rsx_ast_t,
        nk: usize,
        name: *const u8,
        nlen: usize,
    ) {
        if nlen == 0 || name.is_null() {
            return;
        }
        let mut s = 0usize;
        while s < self.tup_n {
            if self.tup_done[s] {
                s += 1;
                continue;
            }
            let n = self.tup_counts[s];
            let mut matched = false;
            let mut f = 0usize;
            while f < n {
                let a = s * TUP_MAXF + f;
                if self.tup_lens[a] == nlen {
                    let elen = self.tup_lens[a];
                    let elem = self.tup_elems[a].as_ptr();
                    let mut j = 0usize;
                    let mut eq = true;
                    while j < elen {
                        if unsafe { *elem.add(j) } != unsafe { *name.add(j) } {
                            eq = false;
                            break;
                        }
                        j += 1;
                    }
                    if eq {
                        matched = true;
                        break;
                    }
                }
                f += 1;
            }
            if matched && unsafe { self.tup_ready(kids, nk, s) } {
                unsafe { self.tup_emit_one(s) };
            }
            s += 1;
        }
    }

    /* Emit every still-pending tuple typedef (primitive element types,
     * and anything whose naming type never matched). */
    unsafe fn tup_emit_rest(&mut self) {
        let mut s = 0usize;
        while s < self.tup_n {
            unsafe { self.tup_emit_one(s) };
            s += 1;
        }
    }

    /* ..._from variants: flush only rows with index >= from. The struct
     * pass's two-phase flush snapshots each table's n before rendering
     * fields — rows earlier passes interned (fn sigs the collect pass
     * rendered, whose deps may include the very struct being emitted,
     * which is only forward-declared at that moment) stay pending for
     * the file-scope fixpoint. */
    unsafe fn res_emit_rest_from(&mut self, from: usize) {
        let mut s = from;
        let mut flushed = false;
        while s < self.res_n {
            if !unsafe { self.res_done[s] } {
                let okt = self.res_oks[s].as_ptr();
                let okl = self.res_ok_lens[s];
                let ert = self.res_errs[s].as_ptr();
                let erl = self.res_err_lens[s];
                if unsafe { self.row_dep_pending(okt, okl) }
                    || unsafe { self.row_dep_pending(ert, erl) }
                {
                    s += 1;
                    continue;
                }
                let tdn = self.arena_tmp();
                let tdn_len = unsafe { Lower::res_typedef_name(okt, okl, ert, erl, tdn, 192) };
                if tdn_len == 0 {
                    unsafe {
                        self.err(b"internal: Result typedef name too long\0".as_ptr(), 0);
                    }
                    return;
                }
                self.out.puts(b"typedef struct { \0".as_ptr());
                self.out.put(okt, okl);
                self.out.puts(b" _v; \0".as_ptr());
                self.out.put(ert, erl);
                self.out.puts(b" _e; bool _ok; } \0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b";\n\0".as_ptr());
                unsafe {
                    self.res_done[s] = true;
                }
                flushed = true;
            }
            s += 1;
        }
        if flushed {
            self.out.putc(b'\n');
        }
    }

    /* Emit struct-Option typedefs (`rsx_opt_<elem>`) whose payload spelling
     * matches `name` — called right after the alias/struct declaring that
     * name lands in the C, so the typedef body's payload is declared first.
     * The preamble cannot hoist them: `rsx_opt_Handler` needs `Handler`, and
     * structs with Option fields need `rsx_opt_*` — pass-A order is the only
     * order that satisfies both directions. */
    unsafe fn opt_emit_for(&mut self, name: *const u8, nlen: usize) {
        if nlen == 0 || name.is_null() {
            return;
        }
        let mut s = 0usize;
        while s < self.opt_n {
            if unsafe { self.opt_done[s] } {
                s += 1;
                continue;
            }
            let elen = self.opt_lens[s];
            if elen != nlen {
                s += 1;
                continue;
            }
            let elem = self.opt_elems[s].as_ptr();
            let mut j = 0usize;
            let mut eq = true;
            while j < elen {
                if unsafe { *elem.add(j) } != unsafe { *name.add(j) } {
                    eq = false;
                    break;
                }
                j += 1;
            }
            if eq {
                let tdn = self.arena_tmp();
                let tdn_len = unsafe { Lower::opt_typedef_name(elem, elen, tdn, 160) };
                if tdn_len == 0 {
                    unsafe {
                        self.err(b"internal: Option typedef name too long\0".as_ptr(), 0);
                    }
                    return;
                }
                self.out.puts(b"typedef struct { \0".as_ptr());
                self.out.put(elem, elen);
                self.out.puts(b" _v; bool _has; } \0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b";\n\0".as_ptr());
                unsafe {
                    self.opt_done[s] = true;
                }
            }
            s += 1;
        }
    }

    /* Emit the pending Option rows whose payload names an fn-ptr row
     * (`rsx_opt_Rrsx_fnp_N`) — complete as soon as the fnp typedefs land,
     * so a struct field naming one can flush them mid-pass-A without
     * waiting for a naming struct. */
    unsafe fn opt_emit_fnp(&mut self) {
        let mut s = 0usize;
        while s < self.opt_n {
            if unsafe { self.opt_done[s] } {
                s += 1;
                continue;
            }
            let elen = self.opt_lens[s];
            let elem = self.opt_elems[s].as_ptr();
            if elen < 8 || !unsafe { z_eq(elem, 8, b"rsx_fnp_\0".as_ptr()) } {
                s += 1;
                continue;
            }
            let tdn = self.arena_tmp();
            let tdn_len = unsafe { Lower::opt_typedef_name(elem, elen, tdn, 160) };
            if tdn_len == 0 {
                unsafe {
                    self.err(b"internal: Option typedef name too long\0".as_ptr(), 0);
                }
                return;
            }
            self.out.puts(b"typedef struct { \0".as_ptr());
            self.out.put(elem, elen);
            self.out.puts(b" _v; bool _has; } \0".as_ptr());
            self.out.put(tdn, tdn_len);
            self.out.puts(b";\n\0".as_ptr());
            unsafe {
                self.opt_done[s] = true;
            }
            s += 1;
        }
    }

    /* Emit every still-pending Option typedef (primitive payloads — size_t
     * and friends — and anything whose naming type never matched). */
    unsafe fn opt_emit_rest(&mut self, from: usize) {
        let mut s = from;
        let mut flushed = false;
        while s < self.opt_n {
            if !unsafe { self.opt_done[s] } {
                let elem = self.opt_elems[s].as_ptr();
                let elen = self.opt_lens[s];
                /* a payload naming an incomplete row/type waits for a
                 * later flush round (Result<Option<Vec<u8>>, ..) — the
                 * res row's payload names this opt row */
                if unsafe { self.row_dep_pending(elem, elen) } {
                    s += 1;
                    continue;
                }
                let tdn = self.arena_tmp();
                let tdn_len = unsafe { Lower::opt_typedef_name(elem, elen, tdn, 160) };
                if tdn_len == 0 {
                    unsafe {
                        self.err(b"internal: Option typedef name too long\0".as_ptr(), 0);
                    }
                    return;
                }
                self.out.puts(b"typedef struct { \0".as_ptr());
                self.out.put(elem, elen);
                self.out.puts(b" _v; bool _has; } \0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b";\n\0".as_ptr());
                unsafe {
                    self.opt_done[s] = true;
                }
                flushed = true;
            }
            s += 1;
        }
        if flushed {
            self.out.putc(b'\n');
        }
    }

    /* Emit every still-pending fn-pointer row typedef. Runs BEFORE the
     * Option rows that name them (an rsx_opt_Rrsx_fnp_N payload needs the
     * fn-ptr typedef complete first). */
    unsafe fn fnp_emit_rest(&mut self) {
        let mut s = 0usize;
        while s < self.fnps.n {
            if !self.fnps.done[s] {
                let tdn = self.arena_tmp();
                let tdn_len = unsafe { FnPtrTab::name_for(s, tdn, 96) };
                if tdn_len == 0 {
                    unsafe {
                        self.err(b"internal: fnptr typedef name too long\0".as_ptr(), 0);
                    }
                    return;
                }
                /* the stored sig is `RET (*)(params)` — the typedef needs
                 * the name INSIDE the declarator parens: `typedef RET
                 * (*name)(params)`. Splice at the first ` (*)`. A found
                 * flag carries the sentinel — `usize::MAX` is not in the
                 * subset (self-host: it would refuse at let inference). */
                let sig = self.fnps.sigs[s].as_ptr();
                let slen = self.fnps.sig_lens[s];
                let mut star = 0usize;
                let mut have = false;
                let mut i = 0usize;
                while i + 2 < slen {
                    if unsafe { *sig.add(i) } == b' '
                        && unsafe { *sig.add(i + 1) } == b'('
                        && unsafe { *sig.add(i + 2) } == b'*'
                        && i + 3 < slen
                        && unsafe { *sig.add(i + 3) } == b')'
                    {
                        star = i;
                        have = true;
                        break;
                    }
                    i += 1;
                }
                if !have {
                    unsafe {
                        self.err(b"internal: fnptr sig has no declarator\0".as_ptr(), 0);
                    }
                    return;
                }
                self.out.puts(b"typedef \0".as_ptr());
                self.out.put(sig, star);
                self.out.puts(b" (*\0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.put(sig.add(star + 3), slen - star - 3);
                self.out.puts(b";\n\0".as_ptr());
                self.fnps.done[s] = true;
            }
            s += 1;
        }
    }

    /* Emit every still-pending Result typedef. Called after the type
     * passes (a payload naming a unit struct needs that struct's
     * typedef first) — the same window opt_emit_rest runs in. */
    unsafe fn res_emit_rest(&mut self) {
        let mut s = 0usize;
        let mut flushed = false;
        while s < self.res_n {
            if !unsafe { self.res_done[s] } {
                let okt = self.res_oks[s].as_ptr();
                let okl = self.res_ok_lens[s];
                let ert = self.res_errs[s].as_ptr();
                let erl = self.res_err_lens[s];
                /* both payloads must be complete — a Result over a not-
                 * yet-emitted row (Result<Option<Vec<u8>>, ..>) waits for
                 * a later flush round instead of naming an unknown type */
                if unsafe { self.row_dep_pending(okt, okl) }
                    || unsafe { self.row_dep_pending(ert, erl) }
                {
                    s += 1;
                    continue;
                }
                let tdn = self.arena_tmp();
                let tdn_len = unsafe { Lower::res_typedef_name(okt, okl, ert, erl, tdn, 192) };
                if tdn_len == 0 {
                    unsafe {
                        self.err(b"internal: Result typedef name too long\0".as_ptr(), 0);
                    }
                    return;
                }
                self.out.puts(b"typedef struct { \0".as_ptr());
                self.out.put(okt, okl);
                self.out.puts(b" _v; \0".as_ptr());
                self.out.put(ert, erl);
                self.out.puts(b" _e; bool _ok; } \0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b";\n\0".as_ptr());
                unsafe {
                    self.res_done[s] = true;
                }
                flushed = true;
            }
            s += 1;
        }
        if flushed {
            self.out.putc(b'\n');
        }
    }

    /* Vec container typedefs + ops — one block per interned element
     * spelling, emitted once (after the unit's own types, before the
     * fns whose signatures name these). The ops are unit-static helpers
     * against libc realloc/free: the generated unit is self-contained,
     * the caller owns the container's lifetime exactly as the source's
     * own free faces spell it (rsx lowers no drops). `from` scopes the
     * walk: the struct pass's two-phase flush passes its snapshot so
     * rows earlier passes interned stay pending (see res_emit_rest_from). */
    unsafe fn vec_emit_rest(&mut self, from: usize) {
        if self.vecs.n == 0 || self.vecs.n <= from {
            return;
        }
        /* the ops call realloc/free/abort: ISO C prototypes, else the
         * implicit-int return truncates realloc's pointer (0x5555… ->
         * low 32 bits) and the first store faults. <stdlib.h> is pulled
         * in only with the container plane — container-free units keep
         * their byte-identical output. */
        self.out.puts(b"#include <stdlib.h>\n\0".as_ptr());
        let mut s = from;
        while s < self.vecs.n {
            if !unsafe { self.vecs.done[s] } {
                let elem = self.vecs.elems[s].as_ptr();
                let elen = self.vecs.elem_lens[s];
                /* a row over an incomplete element (a struct pass A has
                 * not emitted, a tuple row still pending) waits for a
                 * later flush round — the ops sizeof() the element */
                if unsafe { self.row_dep_pending(elem, elen) } {
                    s += 1;
                    continue;
                }
                let tdn = self.arena_tmp();
                let tdn_len = unsafe { VecTab::name_for(s, tdn, 96) };
                if tdn_len == 0 {
                    unsafe {
                        self.err(b"internal: vec typedef name too long\0".as_ptr(), 0);
                    }
                    return;
                }
                /* typedef struct { T *p; size_t n, cap; } rsx_vec_<elem>; */
                self.out.puts(b"typedef struct { \0".as_ptr());
                self.out.put(elem, elen);
                self.out.puts(b" *p; size_t n; size_t cap; } \0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b";\n\0".as_ptr());
                /* push: grow by doubling, refuse-on-OOM aborts the unit at
                 * the C level (the generated C's contract is the source's
                 * own: Rust Vec::push aborts the process on alloc failure,
                 * the generated C matches with a hard exit). */
                self.out.puts(b"static void *\0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b"_grow(\0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b" *v, size_t need) {\n\0".as_ptr());
                self.out.puts(b"    size_t c = v->cap ? v->cap * 2 : 8;\n\0".as_ptr());
                self.out.puts(b"    while (c < need) { c *= 2; }\n\0".as_ptr());
                self.out.puts(b"    void *q = realloc(v->p, c * sizeof(*v->p));\n\0".as_ptr());
                self.out.puts(b"    if (!q) { abort(); }\n\0".as_ptr());
                self.out.puts(b"    v->p = q; v->cap = c;\n\0".as_ptr());
                self.out.puts(b"    return v;\n\0".as_ptr());
                self.out.puts(b"}\n\0".as_ptr());
                self.out.puts(b"static void \0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b"_push(\0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b" *v, \0".as_ptr());
                self.out.put(elem, elen);
                self.out.puts(b" e) {\n\0".as_ptr());
                self.out.puts(b"    if (v->n == v->cap) { \0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b"_grow(v, v->n + 1); }\n\0".as_ptr());
                self.out.puts(b"    v->p[v->n] = e; v->n++;\n\0".as_ptr());
                self.out.puts(b"}\n\0".as_ptr());
                self.out.puts(b"static void \0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b"_free(\0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b" *v) {\n\0".as_ptr());
                self.out.puts(b"    free(v->p); v->p = 0; v->n = 0; v->cap = 0;\n\0".as_ptr());
                self.out.puts(b"}\n\0".as_ptr());
                unsafe {
                    self.vecs.done[s] = true;
                }
            }
            s += 1;
        }
        self.out.putc(b'\n');
    }

    /* BTreeMap rows: the node + map structs and the get/insert/entry/
     * pairs ops, once per interned (K, V) pair. Key order: str-shaped
     * keys compare (p, n) bytes; every other spelling compares raw
     * bytes (integers/pointers — sizeof-based). */
    unsafe fn btm_emit_rest(&mut self, from: usize) {
        if self.btms.n == 0 || self.btms.n <= from {
            return;
        }
        self.out.puts(b"#include <stdlib.h>\n\0".as_ptr());
        let mut s = from;
        while s < self.btms.n {
            let kb = self.btms.keys[s].as_ptr();
            let kl = self.btms.key_lens[s];
            let vb = self.btms.vals[s].as_ptr();
            let vl = self.btms.val_lens[s];
            let tdn = self.arena_tmp();
            let tdn_len = unsafe { BtmTab::name_for(s, tdn, 96) };
            let ndn = self.arena_tmp();
            let ndn_len = unsafe { BtmTab::node_name_for(s, ndn, 96) };
            if tdn_len == 0 || ndn_len == 0 {
                unsafe {
                    self.err(b"internal: btm typedef name too long\0".as_ptr(), 0);
                }
                return;
            }
            /* FORWARD half — map typedef + node fwd decl: names only the
             * node POINTER, never the val, so it may precede the val
             * type's body. A recursive val (TreeNode { kids: BTreeMap<
             * String, TreeNode> }) closes through this half; the node
             * body, which stores the val BY VALUE, waits below. */
            if !self.btms.fwd_done[s] {
                self.out.puts(b"typedef struct \0".as_ptr());
                self.out.put(ndn, ndn_len);
                self.out.puts(b"_s \0".as_ptr());
                self.out.put(ndn, ndn_len);
                self.out.puts(b";\n\0".as_ptr());
                self.out.puts(b"typedef struct { \0".as_ptr());
                self.out.put(ndn, ndn_len);
                self.out.puts(b" *root; size_t n; } \0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b";\n\0".as_ptr());
                self.btms.fwd_done[s] = true;
            }
            /* BODY half — node struct (val by value) + the ops: waits
             * until the val spelling is complete (row_dep_pending — a
             * later flush round re-runs this loop and lands it). */
            if !self.btms.done[s] {
                /* a value spelling that names an incomplete row/type
                 * waits for a later flush round (the node stores the
                 * value by value — sizeof rides the typedef) */
                if unsafe { self.row_dep_pending(vb, vl) } {
                    s += 1;
                    continue;
                }
                /* key compare: str-shaped keys are (p, n) memcmp; raw
                 * memcmp otherwise (scalars, pointers). */
                let k_is_str = (kl == 9 && unsafe { z_eq(kb, 9, b"rsx_str_t\0".as_ptr()) })
                    || (kl == 13 && unsafe { z_eq(kb, 13, b"rsx_str_ref_t\0".as_ptr()) });
                self.out.puts(b"struct \0".as_ptr());
                self.out.put(ndn, ndn_len);
                self.out.puts(b"_s { \0".as_ptr());
                self.out.put(kb, kl);
                self.out.puts(b" key; \0".as_ptr());
                self.out.put(vb, vl);
                self.out.puts(b" val; struct \0".as_ptr());
                self.out.put(ndn, ndn_len);
                self.out.puts(b"_s *l; struct \0".as_ptr());
                self.out.put(ndn, ndn_len);
                self.out.puts(b"_s *r; };\n\0".as_ptr());
                /* key compare: <0 / 0 / >0 */
                self.out.puts(b"static int \0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b"_cmp(\0".as_ptr());
                self.out.put(kb, kl);
                self.out.puts(b" a, \0".as_ptr());
                self.out.put(kb, kl);
                self.out.puts(b" b) {\n\0".as_ptr());
                if k_is_str {
                    self.out.puts(b"    size_t m = a.n < b.n ? a.n : b.n; int c = m ? memcmp(a.p, b.p, m) : 0;\n\0".as_ptr());
                    self.out.puts(b"    if (c) { return c; } return (int)(a.n > b.n) - (int)(a.n < b.n);\n\0".as_ptr());
                } else {
                    self.out.puts(b"    return memcmp(&a, &b, sizeof(\0".as_ptr());
                    self.out.put(kb, kl);
                    self.out.puts(b"));\n\0".as_ptr());
                }
                self.out.puts(b"}\n\0".as_ptr());
                /* find: node* or NULL */
                self.out.puts(b"static \0".as_ptr());
                self.out.put(ndn, ndn_len);
                self.out.puts(b" *\0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b"_find(\0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b" *m, \0".as_ptr());
                self.out.put(kb, kl);
                self.out.puts(b" k) {\n\0".as_ptr());
                self.out.put(ndn, ndn_len);
                self.out.puts(b" *p = m->root;\n\0".as_ptr());
                self.out.puts(b"    while (p) { int c = \0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b"_cmp(k, p->key); if (c == 0) { return p; } p = c < 0 ? p->l : p->r; }\n\0".as_ptr());
                self.out.puts(b"    return 0;\n\0".as_ptr());
                self.out.puts(b"}\n\0".as_ptr());
                /* get: V* or NULL */
                self.out.puts(b"static \0".as_ptr());
                self.out.put(vb, vl);
                self.out.puts(b" *\0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b"_get(\0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b" *m, \0".as_ptr());
                self.out.put(kb, kl);
                self.out.puts(b" k) {\n\0".as_ptr());
                self.out.put(ndn, ndn_len);
                self.out.puts(b" *p = \0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b"_find(m, k); return p ? &p->val : 0;\n\0".as_ptr());
                self.out.puts(b"}\n\0".as_ptr());
                /* insert: replace-on-existing, count only new nodes */
                self.out.puts(b"static void \0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b"_insert(\0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b" *m, \0".as_ptr());
                self.out.put(kb, kl);
                self.out.puts(b" k, \0".as_ptr());
                self.out.put(vb, vl);
                self.out.puts(b" v) {\n\0".as_ptr());
                self.out.puts(b"    \0".as_ptr());
                self.out.put(ndn, ndn_len);
                self.out.puts(b" **pp = &m->root;\n\0".as_ptr());
                self.out.puts(b"    while (*pp) { int c = \0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b"_cmp(k, (*pp)->key); if (c == 0) { (*pp)->val = v; return; } pp = c < 0 ? &(*pp)->l : &(*pp)->r; }\n\0".as_ptr());
                self.out.puts(b"    *pp = (\0".as_ptr());
                self.out.put(ndn, ndn_len);
                self.out.puts(b" *)calloc(1, sizeof(\0".as_ptr());
                self.out.put(ndn, ndn_len);
                self.out.puts(b")); if (!*pp) { abort(); }\n\0".as_ptr());
                self.out.puts(b"    (*pp)->key = k; (*pp)->val = v; m->n++;\n\0".as_ptr());
                self.out.puts(b"}\n\0".as_ptr());
                /* entry: the find-or-insert face returning V* — the
                 * or_insert_with default is pre-evaluated by the lowering
                 * (the in-tree ctors are pure; the divergence is
                 * documented in the subset). */
                self.out.puts(b"static \0".as_ptr());
                self.out.put(vb, vl);
                self.out.puts(b" *\0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b"_entry(\0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b" *m, \0".as_ptr());
                self.out.put(kb, kl);
                self.out.puts(b" k, \0".as_ptr());
                self.out.put(vb, vl);
                self.out.puts(b" d) {\n\0".as_ptr());
                self.out.put(ndn, ndn_len);
                self.out.puts(b" *p = \0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b"_find(m, k);\n\0".as_ptr());
                self.out.puts(b"    if (p) { return &p->val; }\n\0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b"_insert(m, k, d); return \0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b"_get(m, k);\n\0".as_ptr());
                self.out.puts(b"}\n\0".as_ptr());
                /* pairs: an in-order snapshot Vec row of the tuple
                 * (K, V) — registered by the typing pass (btm_pairs_row);
                 * iteration reuses the Vec-for machinery. */
                self.out.puts(b"static \0".as_ptr());
                self.out.put(vb, vl);
                self.out.puts(b" *\0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b"_entry_raw(\0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b" *m, \0".as_ptr());
                self.out.put(kb, kl);
                self.out.puts(b" k) {\n\0".as_ptr());
                self.out.put(ndn, ndn_len);
                self.out.puts(b" *p = \0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b"_find(m, k);\n\0".as_ptr());
                self.out.puts(b"    return p ? &p->val : 0;\n\0".as_ptr());
                self.out.puts(b"}\n\0".as_ptr());
                unsafe {
                    self.btms.done[s] = true;
                }
            }
            s += 1;
        }
        self.out.putc(b'\n');
    }

    /* &[T] slice-ref rows: one typedef per interned element type —
     * typedef struct { const T *p; size_t n; } rsx_arr_<row>;
     * No ops (the fat pair is data, not a container): done-marked so
     * the flush is idempotent across the pre-scan and body passes.
     * `from` scopes the walk (see res_emit_rest_from). */
    unsafe fn arr_emit_rest(&mut self, from: usize) {
        if self.arrs.n == 0 || self.arrs.n <= from {
            return;
        }
        let mut s = from;
        let mut flushed = false;
        while s < self.arrs.n {
            if !unsafe { self.arrs.done[s] } {
                let elem = self.arrs.elems[s].as_ptr();
                let elen = self.arrs.elem_lens[s];
                /* a row over an incomplete unit type waits for the struct
                 * pass (pass-0a statics intern `&[LiveExport]` rows long
                 * before pass A emits LiveExport) */
                if unsafe { self.row_dep_pending(elem, elen) } {
                    s += 1;
                    continue;
                }
                let tdn = self.arena_tmp();
                let tdn_len = unsafe { ArrTab::name_for(s, tdn, 96) };
                if tdn_len == 0 {
                    unsafe {
                        self.err(b"internal: arr typedef name too long\0".as_ptr(), 0);
                    }
                    return;
                }
                self.out.puts(b"typedef struct { const \0".as_ptr());
                self.out.put(elem, elen);
                self.out.puts(b" *p; size_t n; } \0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b";\n\0".as_ptr());
                unsafe {
                    self.arrs.done[s] = true;
                }
                flushed = true;
            }
            s += 1;
        }
        if flushed {
            self.out.putc(b'\n');
        }
    }

    /* The pass-0a window's arr flush: rows whose deps are complete only
     * (row_dep_pending). The const/static faces that need a row typedef
     * here spell byte/prim elements (`const X: &[u8] = b".."`); a row
     * over a unit type or another container row (a fn sig's
     * `&[(String,String)]` param the collect pass interned before pass
     * A) waits for the file-scope fixpoint flush, where its deps are
     * complete. done[] keeps the two windows from double-emitting. */
    unsafe fn arr_emit_prim_rest(&mut self) {
        let mut s = 0usize;
        let mut flushed = false;
        while s < self.arrs.n {
            if !unsafe { self.arrs.done[s] } {
                let elem = self.arrs.elems[s].as_ptr();
                let elen = self.arrs.elem_lens[s];
                if unsafe { self.row_dep_pending(elem, elen) } {
                    s += 1;
                    continue;
                }
                let tdn = self.arena_tmp();
                let tdn_len = unsafe { ArrTab::name_for(s, tdn, 96) };
                if tdn_len == 0 {
                    unsafe {
                        self.err(b"internal: arr typedef name too long\0".as_ptr(), 0);
                    }
                    return;
                }
                self.out.puts(b"typedef struct { const \0".as_ptr());
                self.out.put(elem, elen);
                self.out.puts(b" *p; size_t n; } \0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b";\n\0".as_ptr());
                unsafe {
                    self.arrs.done[s] = true;
                }
                flushed = true;
            }
            s += 1;
        }
        if flushed {
            self.out.putc(b'\n');
        }
    }

    /* Does the unit declare a STRUCT/ENUM/TYPE_ALIAS named (nm, nl)?
     * Reads the FILE node lower_file parked — the pass-0a deferral test
     * has no other view of the item list (collect's SymTab stores C
     * spellings, not the declaring items). */
    unsafe fn unit_type_exists(&mut self, nm: *const u8, nl: usize) -> bool {
        let f = self.file_node;
        if f.is_null() || nl == 0 || nl >= 48 {
            return false;
        }
        let kids = unsafe { (*f).kids };
        let nk = unsafe { (*f).n_kids } as usize;
        let mut i = 0usize;
        while i < nk {
            let item = unsafe { *kids.add(i) };
            if !item.is_null() {
                let k = unsafe { (*item).kind };
                /* byte compare, NOT z_eq: nm is a SPAN inside a row
                 * buffer (`Foo` inside `Foo *`) with no NUL terminator —
                 * z_eq would read past nl into the star/space and refuse
                 * a name the unit does declare. */
                if k == pm_jit_rsx_ast_kind::STRUCT
                    || k == pm_jit_rsx_ast_kind::ENUM
                    || k == pm_jit_rsx_ast_kind::TYPE_ALIAS
                {
                    let it = unsafe { (*item).text };
                    let itl = unsafe { (*item).text_len };
                    if itl == nl && !it.is_null() {
                        let mut j = 0usize;
                        let mut eq = true;
                        while j < nl {
                            if unsafe { *it.add(j) } != unsafe { *nm.add(j) } {
                                eq = false;
                                break;
                            }
                            j += 1;
                        }
                        if eq {
                            return true;
                        }
                    }
                }
            }
            i += 1;
        }
        false
    }

    /* pending-row counters — the file-scope fixpoint loop's progress
     * test (a round that flushes nothing new ends the loop). */
    unsafe fn tup_pending_n(&mut self) -> usize {
        let mut c = 0usize;
        let mut s = 0usize;
        while s < self.tup_n {
            if !self.tup_done[s] {
                c += 1;
            }
            s += 1;
        }
        c
    }
    unsafe fn opt_pending_n(&mut self) -> usize {
        let mut c = 0usize;
        let mut s = 0usize;
        while s < self.opt_n {
            if !self.opt_done[s] {
                c += 1;
            }
            s += 1;
        }
        c
    }
    unsafe fn res_pending_n(&mut self) -> usize {
        let mut c = 0usize;
        let mut s = 0usize;
        while s < self.res_n {
            if !self.res_done[s] {
                c += 1;
            }
            s += 1;
        }
        c
    }
    unsafe fn vec_pending_n(&mut self) -> usize {
        let mut c = 0usize;
        let mut s = 0usize;
        while s < self.vecs.n {
            if !self.vecs.done[s] {
                c += 1;
            }
            s += 1;
        }
        c
    }
    unsafe fn btm_pending_n(&mut self) -> usize {
        let mut c = 0usize;
        let mut s = 0usize;
        while s < self.btms.n {
            if !self.btms.done[s] {
                c += 1;
            }
            s += 1;
        }
        c
    }
    unsafe fn arr_pending_n(&mut self) -> usize {
        let mut c = 0usize;
        let mut s = 0usize;
        while s < self.arrs.n {
            if !self.arrs.done[s] {
                c += 1;
            }
            s += 1;
        }
        c
    }

    /* Is a row/payload spelling (elem, elen) NOT complete yet — i.e. an
     * emit_rest must NOT flush a row that names it? The container rows
     * interleave with user types and with each other (a Vec over a
     * tuple, a Result over an Option over a vec row), and the safe
     * invariant is: flush only rows whose every named dep is complete.
     * A pending row stays done[]=false and a later flush retries it —
     * the file-scope window runs the flushes to a fixpoint, so no row
     * is left behind. True = PENDING (do not emit). */
    unsafe fn row_dep_pending(&mut self, elem: *const u8, elen: usize) -> bool {
        if elen == 0 || elem.is_null() {
            return false;
        }
        /* strip qualifiers/whitespace around the core spelling — a
         * POINTER spelling carries the star after a space (`Foo *`):
         * stripping the star alone leaves `Foo ` and the trailing space
         * would fall into the primitive branch below, naming an
         * undeclared struct. Alternate space/star from both ends until
         * neither peels. */
        let mut b0 = 0usize;
        let mut bn = elen;
        loop {
            if b0 < bn
                && (unsafe { *elem.add(b0) } == b' ' || unsafe { *elem.add(b0) } == b'*')
            {
                b0 += 1;
                continue;
            }
            if bn > b0
                && (unsafe { *elem.add(bn - 1) } == b' ' || unsafe { *elem.add(bn - 1) } == b'*')
            {
                bn -= 1;
                continue;
            }
            break;
        }
        /* a spelling with an inner space (int32_t, const uint8_t,
         * struct X) is a primitive/rendered C type — complete by the
         * render that produced it. */
        let nm = elem.add(b0);
        let nl = bn - b0;
        if nl == 0 {
            return false;
        }
        let mut k = b0;
        while k < bn {
            if unsafe { *elem.add(k) } == b' ' {
                return false;
            }
            k += 1;
        }
        /* the str plane: rsx_str_t / rsx_str_ref_t are complete after the
         * preamble flushes (str_emit_rest runs before every row flush). */
        if nl == 9 && unsafe { z_eq(nm, nl, b"rsx_str_t\0".as_ptr()) } {
            return false;
        }
        if nl == 13 && unsafe { z_eq(nm, nl, b"rsx_str_ref_t\0".as_ptr()) } {
            return false;
        }
        if nl == 12 && unsafe { z_eq(nm, nl, b"rsx_strpair_t\0".as_ptr()) } {
            return false;
        }
        /* another container row: pending while its typedef is not done */
        if nl > 8 && unsafe { z_eq(nm, 8, b"rsx_opt_\0".as_ptr()) } {
            let mut s = 0usize;
            while s < self.opt_n {
                if !self.opt_done[s] {
                    /* the row's typedef NAME — opt_typedef_name renders the
                     * encoded spelling; compare against the queried name */
                    let kb = self.arena_tmp();
                    let kn = unsafe {
                        Lower::opt_typedef_name(
                            self.opt_elems[s].as_ptr(),
                            self.opt_lens[s],
                            kb,
                            192,
                        )
                    };
                    if kn == elen && kn > 0 && unsafe { z_eq(kb, kn, elem) } {
                        return true;
                    }
                }
                s += 1;
            }
            return false;
        }
        if nl > 8 && unsafe { z_eq(nm, 8, b"rsx_res_\0".as_ptr()) } {
            let mut s = 0usize;
            while s < self.res_n {
                if !self.res_done[s] {
                    let okn = self.res_ok_lens[s];
                    let ern = self.res_err_lens[s];
                    /* rsx_res_<hexlen>T_<hexlen>E: match row by payload
                     * pair — decode lengths from the name would re-derive
                     * the hex; the row's own spellings are exact. */
                    let mut kb = self.arena_tmp();
                    let kn = unsafe {
                        Lower::res_typedef_name(
                            self.res_oks[s].as_ptr(),
                            okn,
                            self.res_errs[s].as_ptr(),
                            ern,
                            kb,
                            192,
                        )
                    };
                    if kn == elen && kn > 0 && unsafe { z_eq(kb, kn, elem) } {
                        return true;
                    }
                }
                s += 1;
            }
            return false;
        }
        if nl > 8 && unsafe { z_eq(nm, 8, b"rsx_vec_\0".as_ptr()) } {
            let row = unsafe { self.vecs.find_by_name(elem, elen) };
            if row < VEC_CAP {
                return !self.vecs.done[row];
            }
            return false;
        }
        if nl > 8 && unsafe { z_eq(nm, 8, b"rsx_arr_\0".as_ptr()) } {
            let row = unsafe { self.arrs.find_by_name(elem, elen) };
            if row < ARR_CAP {
                return !self.arrs.done[row];
            }
            return false;
        }
        if nl > 8 && unsafe { z_eq(nm, 8, b"rsx_btm_\0".as_ptr()) } {
            /* rsx_btm_<d> — the row number is the trailing digit(s) */
            let mut v = 0usize;
            let mut k2 = 8usize;
            let mut ok = true;
            while k2 < nl {
                let c = unsafe { *nm.add(k2) };
                if c < b'0' || c > b'9' {
                    ok = false;
                    break;
                }
                v = v * 10 + (c - b'0') as usize;
                k2 += 1;
            }
            if ok && v < self.btms.n {
                return !self.btms.done[v];
            }
            return false;
        }
        if nl > 10 && unsafe { z_eq(nm, 10, b"rsx_tuple_\0".as_ptr()) } {
            let slot = unsafe { self.tup_find(elem, elen) };
            if slot < TUP_CAP {
                return !self.tup_done[slot];
            }
            return false;
        }
        /* a unit-declared type: pending until pass A/A0 emitted it. The
         * struct CURRENTLY being emitted is a special case — its forward
         * typedef exists but the body has not closed, so any row naming
         * it (a fn sig's `Vec<ThisStruct>`) defers to the file-scope
         * window even though tydone marks the name. */
        if nl < 48 && nl == self.cur_emit_type_len && self.cur_emit_type_len > 0 {
            let mut j2 = 0usize;
            let mut same = true;
            while j2 < nl {
                if unsafe { *nm.add(j2) } != self.cur_emit_type[j2] {
                    same = false;
                    break;
                }
                j2 += 1;
            }
            if same {
                return true;
            }
        }
        if nl < 48 && unsafe { self.unit_type_exists(nm, nl) } {
            return !unsafe { self.tydone_find(nm, nl) };
        }
        /* opaque extern (pm_util_lock_t etc.): hoisted early — complete */
        false
    }

    /* Lock-plane emission: one typedef per interned payload row —
     * the pm_util_lock_t typedef (the lock card's exact ABI shape:
     * { uint32_t locked; }) and the extern prototypes for the card's
     * own acquire/release faces (link-time resolved against the lock
     * card's rs muscle — one mechanism, not a second C lock). The
     * typedef is emitted only when a lock row exists: lock-free units
     * keep their byte-identical output. A local pm_util_lock_t typedef
     * collides with nothing in a unit that never declares its own —
     * registry's muscle DOES declare one, but a lock row there means
     * the muscle itself uses Mutex<T>, which its subset forbids. */
    unsafe fn lock_emit_rest(&mut self) {
        if self.locks.n == 0 {
            return;
        }
        self.out.puts(b"typedef struct { uint32_t locked; } pm_util_lock_t;\n\0".as_ptr());
        self.out.puts(b"extern void pm_util_lock_acquire(pm_util_lock_t *);\n\0".as_ptr());
        self.out.puts(b"extern void pm_util_lock_release(pm_util_lock_t *);\n\0".as_ptr());
        let mut s = 0usize;
        while s < self.locks.n {
            if !unsafe { self.locks.done[s] } {
                let elem = self.locks.elems[s].as_ptr();
                let elen = self.locks.elem_lens[s];
                let tdn = self.arena_tmp();
                let tdn_len = unsafe { LockTab::name_for(s, tdn, 96) };
                if tdn_len == 0 {
                    unsafe {
                        self.err(b"internal: lock typedef name too long\0".as_ptr(), 0);
                    }
                    return;
                }
                /* typedef struct { pm_util_lock_t raw; T value; } rsx_lock_<row>; */
                self.out.puts(b"typedef struct { pm_util_lock_t raw; \0".as_ptr());
                self.out.put(elem, elen);
                self.out.puts(b" value; } \0".as_ptr());
                self.out.put(tdn, tdn_len);
                self.out.puts(b";\n\0".as_ptr());
                unsafe {
                    self.locks.done[s] = true;
                }
            }
            s += 1;
        }
        self.out.putc(b'\n');
    }

    /* &str plane: the one fat-reference typedef, emitted at most once
     * per unit (before the first use — the same contract as the
     * Option/tuple typedefs: file scope, complete at every reference). */
    unsafe fn str_emit_rest(&mut self) {
        if self.str_ref_done {
            /* the str typedef already landed — but a LATER split_once may
             * still have armed strpair_used; the pair row rides its own
             * done flag, not this early-return. */
            if self.strpair_used && !self.strpair_done {
                self.out.puts(
                    b"typedef struct { rsx_str_ref_t _0; rsx_str_ref_t _1; } rsx_strpair_t;\n\0".as_ptr(),
                );
                self.strpair_done = true;
            }
            return;
        }
        if !self.str_ref_used {
            return;
        }
        self.out.puts(
            b"typedef struct { const uint8_t *p; size_t n; } rsx_str_ref_t;\n\0".as_ptr(),
        );
        if self.strpair_used {
            self.out.puts(
                b"typedef struct { rsx_str_ref_t _0; rsx_str_ref_t _1; } rsx_strpair_t;\n\0".as_ptr(),
            );
            self.strpair_done = true;
        }
        self.str_ref_done = true;
    }

    /* Owned-String plane: the one monomorphic struct + its unit-static
     * ops. rsx_str_t is { char *p; size_t n, cap; } — p == NULL and
     * n == 0 is the empty string (the {0} compound literal), so every
     * empty ctor and static initializer is a constant expression. The
     * ops mirror the Vec rows' contract: unit-local helpers against
     * libc realloc/free, grow-by-double, refuse-on-OOM aborts (the
     * process contract is the source's own — Rust's allocator aborts,
     * the generated C matches). Appending takes raw ptr+len so the
     * &str fat reference, String values and literals all feed one
     * append face; ownership never transfers into p (the ops own
     * their one allocation, the caller keeps the rest exactly as the
     * source's own free faces spell it). */
    unsafe fn str_own_emit_rest(&mut self) {
        if self.str_own_done {
            return;
        }
        if !self.str_own_used {
            return;
        }
        self.out.puts(b"#include <stdlib.h>\n\0".as_ptr());
        self.out.puts(
            b"typedef struct { char *p; size_t n; size_t cap; } rsx_str_t;\n\0".as_ptr(),
        );
        /* grow: cap doubles to >= need; abort on OOM (the source's own
         * process contract — see the Vec rows' note) */
        self.out.puts(
            b"static void rsx_str_grow(rsx_str_t *s, size_t need) {\n\0".as_ptr(),
        );
        self.out.puts(b"    size_t c = s->cap ? s->cap * 2 : 16;\n\0".as_ptr());
        self.out.puts(b"    while (c < need) { c *= 2; }\n\0".as_ptr());
        self.out.puts(
            b"    char *q = (char *)realloc(s->p, c + 1);\n\0".as_ptr(),
        );
        self.out.puts(b"    if (!q) { abort(); }\n\0".as_ptr());
        self.out.puts(b"    s->p = q; s->cap = c;\n\0".as_ptr());
        self.out.puts(b"}\n\0".as_ptr());
        /* append raw bytes: the one appending face every caller shape
         * (literal, &str ref, owned String) lowers to */
        self.out.puts(
            b"static void rsx_str_append(rsx_str_t *s, const char *p, size_t n) {\n\0".as_ptr(),
        );
        self.out.puts(b"    if (n == 0) { return; }\n\0".as_ptr());
        self.out.puts(b"    if (s->n + n > s->cap) { rsx_str_grow(s, s->n + n); }\n\0".as_ptr());
        self.out.puts(b"    for (size_t i = 0; i < n; i++) { s->p[s->n + i] = p[i]; }\n\0".as_ptr());
        self.out.puts(b"    s->n += n; s->p[s->n] = 0;\n\0".as_ptr());
        self.out.puts(b"}\n\0".as_ptr());
        /* append a NUL-terminated literal (strlen once) */
        self.out.puts(
            b"static void rsx_str_push_lit(rsx_str_t *s, const char *lit) {\n\0".as_ptr(),
        );
        self.out.puts(b"    rsx_str_append(s, lit, strlen(lit));\n\0".as_ptr());
        self.out.puts(b"}\n\0".as_ptr());
        /* append one char (Rust char is unicode scalar; the owned plane
         * stores UTF-8 — a scalar <= 0x7F is one byte, anything larger
         * encodes to up to 4 bytes) */
        self.out.puts(
            b"static void rsx_str_push_char(rsx_str_t *s, uint32_t ch) {\n\0".as_ptr(),
        );
        self.out.puts(b"    char b[4]; size_t k = 0;\n\0".as_ptr());
        self.out.puts(b"    if (ch < 0x80) { b[k++] = (char)ch; }\n\0".as_ptr());
        self.out.puts(b"    else if (ch < 0x800) { b[k++] = (char)(0xC0 | (ch >> 6)); b[k++] = (char)(0x80 | (ch & 0x3F)); }\n\0".as_ptr());
        self.out.puts(b"    else if (ch < 0x10000) { b[k++] = (char)(0xE0 | (ch >> 12)); b[k++] = (char)(0x80 | ((ch >> 6) & 0x3F)); b[k++] = (char)(0x80 | (ch & 0x3F)); }\n\0".as_ptr());
        self.out.puts(b"    else { b[k++] = (char)(0xF0 | (ch >> 18)); b[k++] = (char)(0x80 | ((ch >> 12) & 0x3F)); b[k++] = (char)(0x80 | ((ch >> 6) & 0x3F)); b[k++] = (char)(0x80 | (ch & 0x3F)); }\n\0".as_ptr());
        self.out.puts(b"    rsx_str_append(s, b, k);\n\0".as_ptr());
        self.out.puts(b"}\n\0".as_ptr());
        /* append an integer in decimal — the format! plane's {n} segment
         * for every integer-shaped capture (counts, indices, sizes).
         * Signed negatives print the '-' then digits; usize/uN values
         * pass through as unsigned. */
        self.out.puts(
            b"static void rsx_str_push_i64(rsx_str_t *s, int64_t v) {\n\0".as_ptr(),
        );
        self.out.puts(b"    char b[24]; size_t k = sizeof b;\n\0".as_ptr());
        self.out.puts(b"    uint64_t u; int neg = v < 0;\n\0".as_ptr());
        self.out.puts(b"    if (neg) { u = (uint64_t)(-(v + 1)) + 1; } else { u = (uint64_t)v; }\n\0".as_ptr());
        self.out.puts(b"    do { b[--k] = (char)('0' + (u % 10)); u /= 10; } while (u);\n\0".as_ptr());
        self.out.puts(b"    if (neg) { b[--k] = '-'; }\n\0".as_ptr());
        self.out.puts(b"    rsx_str_append(s, b + k, sizeof b - k);\n\0".as_ptr());
        self.out.puts(b"}\n\0".as_ptr());
        /* replace(from-char, to-lit): a fresh String with every byte
         * equal to `from` swapped for the `to` bytes — &str.replace's
         * one-arg-char shape (gen's path rewriting: '.' -> '/'). */
        self.out.puts(
            b"static rsx_str_t rsx_str_replace_ch(const uint8_t *p, size_t n, char from, const char *to, size_t to_n) {\n\0".as_ptr(),
        );
        self.out.puts(b"    rsx_str_t r = {0};\n\0".as_ptr());
        self.out.puts(b"    size_t i, run = 0;\n\0".as_ptr());
        self.out.puts(b"    for (i = 0; i < n; i++) {\n\0".as_ptr());
        self.out.puts(b"        if (p[i] == (uint8_t)from) {\n\0".as_ptr());
        self.out.puts(b"            if (i > run) { rsx_str_append(&r, (const char *)(p + run), i - run); }\n\0".as_ptr());
        self.out.puts(b"            rsx_str_append(&r, to, to_n);\n\0".as_ptr());
        self.out.puts(b"            run = i + 1;\n\0".as_ptr());
        self.out.puts(b"        }\n\0".as_ptr());
        self.out.puts(b"    }\n\0".as_ptr());
        self.out.puts(b"    if (n > run) { rsx_str_append(&r, (const char *)(p + run), n - run); }\n\0".as_ptr());
        self.out.puts(b"    return r;\n\0".as_ptr());
        self.out.puts(b"}\n\0".as_ptr());
        /* clone: a deep copy — the source's clone() is a fresh owning
         * String; the ops' one allocation + copy is exactly that */
        self.out.puts(
            b"static rsx_str_t rsx_str_clone(const rsx_str_t *s) {\n\0".as_ptr(),
        );
        self.out.puts(b"    rsx_str_t r = {0};\n\0".as_ptr());
        self.out.puts(b"    rsx_str_append(&r, s->p ? s->p : \"\", s->n);\n\0".as_ptr());
        self.out.puts(b"    return r;\n\0".as_ptr());
        self.out.puts(b"}\n\0".as_ptr());
        /* substring test against a NUL-terminated literal */
        self.out.puts(
            b"static int rsx_str_contains(const rsx_str_t *s, const char *lit) {\n\0".as_ptr(),
        );
        self.out.puts(b"    size_t m = strlen(lit);\n\0".as_ptr());
        self.out.puts(b"    if (m == 0) { return 1; }\n\0".as_ptr());
        self.out.puts(b"    if (m > s->n) { return 0; }\n\0".as_ptr());
        self.out.puts(b"    for (size_t i = 0; i + m <= s->n; i++) {\n\0".as_ptr());
        self.out.puts(b"        size_t j = 0;\n\0".as_ptr());
        self.out.puts(b"        while (j < m && s->p[i + j] == lit[j]) { j++; }\n\0".as_ptr());
        self.out.puts(b"        if (j == m) { return 1; }\n\0".as_ptr());
        self.out.puts(b"    }\n\0".as_ptr());
        self.out.puts(b"    return 0;\n\0".as_ptr());
        self.out.puts(b"}\n\0".as_ptr());
        /* substring test on a raw (p, n) view — the &str fat reference's
         * shape, so a borrowed receiver never builds an owned temp */
        self.out.puts(
            b"static int rsx_view_contains(const char *p, size_t n, const char *lit) {\n\0".as_ptr(),
        );
        self.out.puts(b"    rsx_str_t v = { (char *)p, n, 0 };\n\0".as_ptr());
        self.out.puts(b"    return rsx_str_contains(&v, lit);\n\0".as_ptr());
        self.out.puts(b"}\n\0".as_ptr());
        /* byte find: index of first ch, or s->n when absent (the
         * lowering maps that to Option::None — see emit_method_call) */
        self.out.puts(
            b"static size_t rsx_str_find(const rsx_str_t *s, char ch) {\n\0".as_ptr(),
        );
        self.out.puts(b"    for (size_t i = 0; i < s->n; i++) { if (s->p[i] == ch) { return i; } }\n\0".as_ptr());
        self.out.puts(b"    return s->n;\n\0".as_ptr());
        self.out.puts(b"}\n\0".as_ptr());
        /* clear: keep the allocation (Rust String::clear keeps cap) */
        self.out.puts(b"static void rsx_str_clear(rsx_str_t *s) {\n\0".as_ptr());
        self.out.puts(b"    s->n = 0; if (s->p) { s->p[0] = 0; }\n\0".as_ptr());
        self.out.puts(b"}\n\0".as_ptr());
        /* free: the explicit teardown face (the source spells its own
         * drops; rsx lowers none implicitly) */
        self.out.puts(b"static void rsx_str_free(rsx_str_t *s) {\n\0".as_ptr());
        self.out.puts(b"    free(s->p); s->p = 0; s->n = 0; s->cap = 0;\n\0".as_ptr());
        self.out.puts(b"}\n\0".as_ptr());
        self.out.putc(b'\n');
        self.str_own_done = true;
    }

    /* Recursive body pre-scan: render (and discard) every TYPE node's C
     * type so the container/tuple/Option tables intern everything the
     * body will name before its opening brace — the typedefs must sit at
     * file scope. Runs under pre_mode: arena_tmp carves from the
     * pre_pool instead of the arena, so the sweep costs no arena bytes
     * for its scratch (see Lower's pre_pool doc). Recursion depth is
     * bounded by the parser's own nesting (the AST is already built, so
     * no new input blowup). */
    unsafe fn body_intern_types(&mut self, e: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        if e.is_null() || !self.ok {
            return;
        }
        if unsafe { (*e).kind } == pm_jit_rsx_ast_kind::TYPE {
            /* per-node pool reset: a render's outstanding tmps never
             * span past its own return, so the next node's render can
             * reuse every slot. out stays the caller's buffer — the
             * pre-scan discards spellings anyway (it only interns). */
            self.pre_pool_at = 0;
            let sc = self.pre_pool.as_mut_ptr();
            let _ = unsafe { self.ctype(e, sc, 160) };
            return;
        }
        if unsafe { (*e).kind } == pm_jit_rsx_ast_kind::MATCH {
            /* probe ONLY the scrutinee: arm patterns are TUPLE/ARRAY
             * nodes too, but they are patterns — probing them renders
             * garbage element "types" from binding names and interns
             * bogus rows. Kids[0] is the scrutinee expr. */
            let mk = unsafe { (*e).kids };
            let mn = unsafe { (*e).n_kids } as usize;
            if mn >= 1 && !mk.is_null() {
                unsafe { self.body_intern_types(*mk.add(0), locals) };
                /* if-let desugars to MATCH at parse: type the scrutinee,
                 * decode the Option payload, register every Some-arm's
                 * bind (SAME registration emit_stmt does) — otherwise a
                 * method call on the bind (`leaf.as_bytes()[i]`) types
                 * nowhere in the pre-scan, its rsx_arr_ row interns only
                 * at emission (when no gated flush follows), and the
                 * typedef never lands before the fn body that names it. */
                if !locals.is_null() {
                    let scrut = unsafe { *mk.add(0) };
                    let sb = self.pre_pool.as_mut_ptr();
                    let save_ok = self.ok;
                    let save_nerrs = self.nerrs;
                    self.ok = true;
                    let sn = unsafe { self.expr_ctype(scrut, sb, 160, locals) };
                    self.ok = save_ok;
                    self.nerrs = save_nerrs;
                    if sn > 8
                        && sn < 160
                        && unsafe { z_eq(sb, 8, b"rsx_opt_\0".as_ptr()) }
                    {
                        let elem = self.stable_tmp();
                        let eln = unsafe { Lower::opt_typedef_elem(sb, sn, elem, 128) };
                        if eln > 0 && eln < 128 {
                            let mut a = 1usize;
                            while a < mn {
                                let arm = unsafe { *mk.add(a) };
                                if arm.is_null() {
                                    a += 1;
                                    continue;
                                }
                                let ak2 = unsafe { (*arm).kids };
                                let an2 = unsafe { (*arm).n_kids } as usize;
                                if an2 >= 2
                                    && unsafe { (*arm).kind } == pm_jit_rsx_ast_kind::MATCH_ARM
                                {
                                    let pat2 = unsafe { *ak2.add(0) };
                                    if unsafe { (*pat2).kind } == pm_jit_rsx_ast_kind::PATH {
                                        let pk2 = unsafe { (*pat2).kids };
                                        let pn2 = unsafe { (*pat2).n_kids } as usize;
                                        if pn2 >= 2 {
                                            let head2 = unsafe { *pk2.add(0) };
                                            let bind2 = unsafe { *pk2.add(1) };
                                            if unsafe { (*head2).kind } == pm_jit_rsx_ast_kind::PATH
                                                && unsafe { z_eq(unsafe { (*head2).text }, unsafe { (*head2).text_len }, b"Some\0".as_ptr()) }
                                            {
                                                /* bind2 is a PATH wrapper whose .text is the kind
                                                 * label ("path") — the identifier is its kids[0]
                                                 * (same shape emit_stmt's if-let registration
                                                 * derefs). */
                                                let bnode2 = if unsafe { (*bind2).n_kids } as usize >= 1 {
                                                    unsafe { *(*bind2).kids.add(0) }
                                                } else {
                                                    bind2
                                                };
                                                let bn2 = unsafe { (*bnode2).text };
                                                let bl2 = unsafe { (*bnode2).text_len };
                                                if bl2 > 0 && !bn2.is_null() {
                                                    unsafe {
                                                        (*locals).add(bn2, bl2, elem, eln, 0);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                a += 1;
                            }
                        }
                    }
                }
            }
            /* arm bodies still get walked (a match arm can name a Vec in
             * a later stmt) — but their pattern nodes are skipped by
             * construction: each arm kid is (pat, [guard], body) and only
             * the body subtree is recursed here. */
            let mut mi = 1usize;
            while mi < mn {
                let arm = unsafe { *mk.add(mi) };
                if arm.is_null() {
                    mi += 1;
                    continue;
                }
                let ak = unsafe { (*arm).kids };
                let an = unsafe { (*arm).n_kids } as usize;
                /* walk only the LAST kid (the arm body block); patterns
                 * and guards are exprs the emission re-derives */
                if an >= 1 {
                    unsafe { self.body_intern_types(*ak.add(an - 1), locals) };
                }
                mi += 1;
            }
            return;
        }
        if unsafe { (*e).kind } == pm_jit_rsx_ast_kind::LET
            && !unsafe { ((*e).text_len == 7 && z_eq((*e).text, 7, b"letelse\0".as_ptr())) }
            && !locals.is_null()
        {
            /* A plain `let name: Ty = init` — register the bind's C type
             * so LATER probes in this pre-scan (a `for x in &name`'s
             * iter expr, a method call on the bind) type instead of
             * refusing. Kids: name(PATH), [mut ATTR], [type TYPE],
             * [init expr] (same shape emit_let parses). The generic walk
             * below still visits every kid, so rows the init itself
             * interns (Vec::new's row) keep landing. */
            let lk = unsafe { (*e).kids };
            let ln = unsafe { (*e).n_kids } as usize;
            let mut name: *const u8 = core::ptr::null();
            let mut name_len: usize = 0;
            let mut ty: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
            let mut j = 0usize;
            while j < ln {
                let kj = unsafe { *lk.add(j) };
                let kk = unsafe { (*kj).kind };
                if kk == pm_jit_rsx_ast_kind::PATH && name_len == 0 {
                    /* PATH kid text is the kind label; the identifier is
                     * kids[0]'s text (single-segment name shape) */
                    let pn = unsafe { (*kj).n_kids } as usize;
                    let mut bn = unsafe { (*kj).text };
                    let mut bl = unsafe { (*kj).text_len };
                    if pn >= 1 {
                        let k0 = unsafe { *(*kj).kids.add(0) };
                        if unsafe { (*k0).kind } == pm_jit_rsx_ast_kind::PATH {
                            bn = unsafe { (*k0).text };
                            bl = unsafe { (*k0).text_len };
                        }
                    }
                    if bl > 0 && !bn.is_null() {
                        name = bn;
                        name_len = bl;
                    }
                } else if kk == pm_jit_rsx_ast_kind::TYPE {
                    ty = kj;
                }
                j += 1;
            }
            if name_len > 0 && !ty.is_null() {
                let ct = self.stable_tmp();
                let n = unsafe { self.ctype(ty, ct, 128) };
                if n > 0 {
                    unsafe {
                        (*locals).add(name, name_len, ct, n, 0);
                    }
                }
            } else if name_len > 0 {
                /* Unascribed `let name = init` — the bind's type is the
                 * init's expr_ctype (the same inference the emission's
                 * emit_let runs). Registering it here lets LATER probes
                 * in this pre-scan type method calls on the bind
                 * (`s.strip_prefix(..)`) and intern their Option/arr rows
                 * in the pre-scan WINDOW — where the gated flush emits
                 * the typedef at fn scope. Without this, the row interns
                 * only at emission and its mid-fn typedef flush lands
                 * inside the then-current block, whose closing brace
                 * kills the type for every later outer-scope use. */
                let ik = unsafe { (*e).kids };
                let inn = unsafe { (*e).n_kids } as usize;
                let mut init: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
                let mut j2 = 0usize;
                while j2 < inn {
                    let kj = unsafe { *ik.add(j2) };
                    let kkj = unsafe { (*kj).kind };
                    if kkj != pm_jit_rsx_ast_kind::PATH
                        && kkj != pm_jit_rsx_ast_kind::TYPE
                        && kkj != pm_jit_rsx_ast_kind::ATTR
                    {
                        init = kj;
                    }
                    j2 += 1;
                }
                /* unwrap EXPR_STMT wrappers (if-let desugar tails) */
                let mut hops = 0usize;
                while hops < 4
                    && !init.is_null()
                    && unsafe { (*init).kind } == pm_jit_rsx_ast_kind::EXPR_STMT
                    && unsafe { (*init).n_kids } as usize >= 1
                {
                    init = unsafe { *(*init).kids.add(0) };
                    hops += 1;
                }
                if !init.is_null() {
                    let tb = self.pre_pool.as_mut_ptr();
                    let save_ok = self.ok;
                    let save_nerrs = self.nerrs;
                    self.ok = true;
                    let tn = unsafe { self.expr_ctype(init, tb, 160, locals) };
                    self.ok = save_ok;
                    self.nerrs = save_nerrs;
                    if tn > 0 && tn < 128 {
                        let ct = self.stable_tmp();
                        let mut w = 0usize;
                        while w < tn {
                            unsafe {
                                *ct.add(w) = *tb.add(w);
                            }
                            w += 1;
                        }
                        unsafe {
                            *ct.add(tn) = 0;
                        }
                        unsafe {
                            (*locals).add(name, name_len, ct, tn, 0);
                        }
                    }
                }
            }
            /* fall through to the generic kid walk (rows + init probes) */
        }
        if unsafe { (*e).kind } == pm_jit_rsx_ast_kind::LET
            && unsafe { (*e).text_len } == 7
            && unsafe { z_eq((*e).text, 7, b"letelse\0".as_ptr()) }
            && !locals.is_null()
        {
            /* if-let's desugared `let Some(bind) = init else {..}` — the
             * pre-scan must register the bind's payload type, or a later
             * method call on the bind (`leaf.as_bytes()[i]` interned at
             * emission, when no flush follows) misses its row HERE:
             * typing the init interned the Option row, decoding the
             * payload registers `leaf` as rsx_str_ref_t, and the body
             * walk then types `leaf.as_bytes()` — interning the
             * rsx_arr_<uint8_t> row in the pre-scan window, where the
             * gated flushes below can emit its typedef. Same shape as
             * emit_stmt's if-let bind registration. */
            let lk = unsafe { (*e).kids };
            let ln = unsafe { (*e).n_kids } as usize;
            if ln >= 3 {
                let pat = unsafe { *lk.add(0) };
                let pk2 = unsafe { (*pat).kids };
                let pn2 = unsafe { (*pat).n_kids } as usize;
                if pn2 >= 2 {
                    let bind = unsafe { *pk2.add(1) };
                    let bn3 = unsafe { (*bind).text };
                    let bl3 = unsafe { (*bind).text_len };
                    let mut init2: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
                    let mut j = 1usize;
                    while j < ln {
                        let kj = unsafe { *lk.add(j) };
                        if unsafe { (*kj).kind } == pm_jit_rsx_ast_kind::BLOCK {
                            break;
                        }
                        if unsafe { (*kj).kind } != pm_jit_rsx_ast_kind::ATTR {
                            init2 = kj;
                        }
                        j += 1;
                    }
                    if bl3 > 0 && !init2.is_null() {
                        let tb = self.pre_pool.as_mut_ptr();
                        let save_ok = self.ok;
                        let save_nerrs = self.nerrs;
                        self.ok = true;
                        let tn = unsafe { self.expr_ctype(init2, tb, 160, locals) };
                        self.ok = save_ok;
                        self.nerrs = save_nerrs;
                        if tn > 8
                            && tn < 160
                            && unsafe { z_eq(tb, 8, b"rsx_opt_\0".as_ptr()) }
                        {
                            let elem = self.stable_tmp();
                            let eln = unsafe { Lower::opt_typedef_elem(tb, tn, elem, 128) };
                            if eln > 0 && eln < 128 {
                                unsafe {
                                    (*locals).add(bn3, bl3, elem, eln, 0);
                                }
                            }
                        }
                    }
                }
            }
            /* still walk the kids: the else-block may name its own rows */
            let kids = unsafe { (*e).kids };
            let nk = unsafe { (*e).n_kids } as usize;
            let mut i = 0usize;
            while i < nk {
                unsafe { self.body_intern_types(*kids.add(i), locals) };
                if !self.ok {
                    return;
                }
                i += 1;
            }
            return;
        }
        if unsafe { (*e).kind } == pm_jit_rsx_ast_kind::FOR && !locals.is_null() {
            /* `for bind in iter { .. }`: the emission's try_emit_for_arr
             * types the iter expr itself and interns its container row —
             * but only at body time, when no gated flush follows. Probe
             * the SAME expr here (the row lands in the pre-scan window
             * where the gated flushes below emit its typedef), and
             * register the bind with the element spelling so the body's
             * own method probes (`inc.as_str()`) type against it. */
            let fk = unsafe { (*e).kids };
            let fn2 = unsafe { (*e).n_kids } as usize;
            if fn2 >= 3 && !fk.is_null() {
                let pat = unsafe { *fk.add(0) };
                let iter = unsafe { *fk.add(1) };
                let body = unsafe { *fk.add(2) };
                /* probe the iter expr (row interning) */
                self.pre_pool_at = 0;
                let sc = self.pre_pool.as_mut_ptr();
                let save_ok = self.ok;
                let save_nerrs = self.nerrs;
                self.ok = true;
                let rl = unsafe { self.expr_ctype(iter, sc, 160, locals) };
                self.ok = save_ok;
                self.nerrs = save_nerrs;
                /* a str-walking for (split/bytes/chars/char_indices) emits
                 * rsx_str_ref_t locals inside the fn body — arm the plane
                 * HERE so the gated flush lands the typedef before the
                 * body (the emission's own arming is too late for the
                 * pre-scan window). */
                if unsafe { (*iter).kind } == pm_jit_rsx_ast_kind::METHOD_CALL {
                    let mk2 = unsafe { (*iter).kids };
                    let mn2 = unsafe { (*iter).n_kids } as usize;
                    if mn2 >= 2 {
                        let nm2 = unsafe { *mk2.add(1) };
                        let nl2 = unsafe { (*nm2).text_len };
                        let np2 = unsafe { (*nm2).text };
                        if (nl2 == 5 && unsafe { z_eq(np2, nl2, b"split\0".as_ptr()) })
                            || (nl2 == 5 && unsafe { z_eq(np2, nl2, b"bytes\0".as_ptr()) })
                            || (nl2 == 5 && unsafe { z_eq(np2, nl2, b"chars\0".as_ptr()) })
                            || (nl2 == 12 && unsafe { z_eq(np2, nl2, b"char_indices\0".as_ptr()) })
                        {
                            self.str_ref_used = true;
                        }
                    }
                }
                /* bind registration: a plain PATH bind names the element
                 * of the iter's container row (arr/vec share .p/.n). A
                 * TUPLE pattern's binds are typed from the interned
                 * tuple signature at emission; the row itself already
                 * interned above via the element probe the tuple path
                 * takes. */
                if rl > 8 && rl < 160 {
                    let mut eb: *const u8 = core::ptr::null();
                    let mut el: usize = 0;
                    if unsafe { z_eq(sc, 8, b"rsx_arr_\0".as_ptr()) } {
                        let rs = unsafe { self.arrs.find_by_name(sc, rl) };
                        if rs < ARR_CAP {
                            eb = self.arrs.elems[rs].as_ptr();
                            el = self.arrs.elem_lens[rs];
                        }
                    } else if unsafe { z_eq(sc, 8, b"rsx_vec_\0".as_ptr()) } {
                        let vs = unsafe { self.vecs.find_by_name(sc, rl) };
                        if vs < VEC_CAP {
                            eb = self.vecs.elems[vs].as_ptr();
                            el = self.vecs.elem_lens[vs];
                        }
                    }
                    if !eb.is_null() && el > 0 && el < 128 {
                        /* unwrap the "path" wrapper the parser wraps a
                         * single-segment bind pattern in */
                        let mut bnode = pat;
                        if unsafe { (*pat).kind } == pm_jit_rsx_ast_kind::PATH
                            && unsafe { z_eq(unsafe { (*pat).text }, unsafe { (*pat).text_len }, b"path\0".as_ptr()) }
                            && unsafe { (*pat).n_kids } as usize == 1
                        {
                            bnode = unsafe { *(*pat).kids.add(0) };
                        }
                        if unsafe { (*bnode).kind } == pm_jit_rsx_ast_kind::PATH {
                            let bn = unsafe { (*bnode).text };
                            let bl = unsafe { (*bnode).text_len };
                            if bl > 0 && !bn.is_null() {
                                unsafe {
                                    (*locals).add(bn, bl, eb, el, 0);
                                }
                            }
                        }
                    }
                }
                /* walk the body (binds the pattern declared above are
                 * registered; the emission re-registers its own) */
                unsafe { self.body_intern_types(body, locals) };
                return;
            }
        }
        if (unsafe { (*e).kind } == pm_jit_rsx_ast_kind::METHOD_CALL
                || unsafe { (*e).kind } == pm_jit_rsx_ast_kind::CALL
                || unsafe { (*e).kind } == pm_jit_rsx_ast_kind::TUPLE
                || unsafe { (*e).kind } == pm_jit_rsx_ast_kind::ARRAY)
            && !locals.is_null() {
            /* Expression probe: the container/Option planes intern rows
             * from expr_ctype alone (x.rsplit(c).next() types the
             * Option-of-str-ref row). The probe's own refusals are
             * inert — pre_mode discards spellings; only the interned
             * tables matter. err() poisons ok, so save/restore both
             * around the probe (the real emission re-derives any real
             * error later). */
            self.pre_pool_at = 0;
            let sc = self.pre_pool.as_mut_ptr();
            let save_ok = self.ok;
            let save_nerrs = self.nerrs;
            self.ok = true;
            let _ = unsafe { self.expr_ctype(e, sc, 160, locals) };
            self.ok = save_ok;
            self.nerrs = save_nerrs;
            /* The probe types the WHOLE call — but a receiver-op method
             * expr_ctype does not know (push_str: the emission-side ops
             * take the arg raw, no typing arm exists) never reaches the
             * ARGS, so a type the arg alone interns (inc.as_str() marks
             * the &str plane) is missed here and the typedef lands after
             * the body that names it. Walk the arg kids after the probe
             * — re-probing a nested call is idempotent (interning is
             * table-keyed) and bounded by the expr's own depth. Kids[0]
             * of a METHOD_CALL is the receiver (already typed by the
             * probe); a CALL's kids[0] is the callee path (nothing to
             * intern) — walking every kid keeps both shapes honest.
             * A call's argument container is a TUPLE node with text
             * "args" (parse 2233): it must NOT be probed as a value
             * tuple (phantom rows from the arg types) — descend into it
             * directly, one level of unwrapping that skips only the
             * probe, not the args themselves. */
            let kids = unsafe { (*e).kids };
            let nk = unsafe { (*e).n_kids } as usize;
            let mut i = 0usize;
            while i < nk {
                let kid = unsafe { *kids.add(i) };
                if !kid.is_null()
                    && unsafe { (*kid).kind } == pm_jit_rsx_ast_kind::TUPLE
                    && unsafe { (*kid).text_len } == 4
                    && unsafe { z_eq(unsafe { (*kid).text }, 4, b"args\0".as_ptr()) }
                {
                    /* the args container: walk ITS kids (each arg
                     * expr), never the container probe */
                    let ak = unsafe { (*kid).kids };
                    let an = unsafe { (*kid).n_kids } as usize;
                    let mut q = 0usize;
                    while q < an {
                        unsafe { self.body_intern_types(*ak.add(q), locals) };
                        q += 1;
                    }
                } else {
                    unsafe { self.body_intern_types(kid, locals) };
                }
                i += 1;
            }
            return;
        }
        let kids = unsafe { (*e).kids };
        let nk = unsafe { (*e).n_kids } as usize;
        let mut i = 0usize;
        while i < nk {
            unsafe { self.body_intern_types(*kids.add(i), locals) };
            if !self.ok {
                return;
            }
            i += 1;
        }
    }

    /* Record a bare type name seen in a signature/static for the opaque hoist.
     * The hoist (opq_emit) re-checks syms/st/nt, so names this unit declares
     * are filtered out; what remains are opaque extern types that need
     * `typedef struct X X;` for the self-contained C to compile. */
    unsafe fn opq_note(&mut self, name: *const u8, nlen: usize) {
        if nlen == 0 || nlen >= 48 || name.is_null() || self.opq_n >= 24 {
            return;
        }
        /* identifiers only — quals/ABI strings (`"C"`) reaching a type
         * render must not become forward typedefs */
        {
            let c = unsafe { *name };
            let okc = c == b'_'
                || (c >= b'a' && c <= b'z')
                || (c >= b'A' && c <= b'Z');
            if !okc {
                return;
            }
        }
        let mut s = 0usize;
        while s < self.opq_n {
            if self.opq_lens[s] == nlen {
                let mut j = 0usize;
                let mut eq = true;
                while j < nlen {
                    if unsafe { *self.opq_names[s].as_ptr().add(j) } != unsafe { *name.add(j) } {
                        eq = false;
                        break;
                    }
                    j += 1;
                }
                if eq {
                    return;
                }
            }
            s += 1;
        }
        let mut j = 0usize;
        while j < nlen {
            unsafe {
                self.opq_names[self.opq_n][j] = *name.add(j);
            }
            j += 1;
        }
        self.opq_lens[self.opq_n] = nlen;
        self.opq_n += 1;
    }

    /* Hoist `typedef struct X X;` for recorded names that no pass declares.
     * Flushed at the head of each emission pass (B/C/D): notes accrue as
     * signatures render, so each flush emits only names new since the last
     * (opq_flushed cursor); a second run would re-emit and clash. Names
     * this unit declares (struct/enum in syms, type aliases by a file scan)
     * are skipped — a hoisted typedef colliding with the real declaration
     * would be a C redefinition error. */
    unsafe fn opq_emit(&mut self, file: *const pm_jit_rsx_ast_t) {
        let mut s = self.opq_flushed;
        let mut emitted = false;
        while s < self.opq_n {
            let name = self.opq_names[s].as_ptr();
            let nlen = self.opq_lens[s];
            s += 1;
            if nlen == 0 {
                continue;
            }
            if unsafe { (*self.syms).find(name, nlen) } < SYM_CAP {
                continue;
            }
            if unsafe { self.nt_find(name, nlen) } {
                continue;
            }
            let stb = self.arena_tmp();
            if unsafe { self.st_find(name, nlen, stb, 64) } > 0 {
                continue;
            }
            let fs = unsafe { (*self.fns).slot(name, nlen) };
            if fs < SYM_CAP && unsafe { (*self.fns).used[fs] } {
                continue;
            }
            if nlen >= 8 && unsafe { z_eq(name, 8, b"rsx_opt_\0".as_ptr()) } {
                continue;
            }
            /* a declared trait names its own object typedef in pass A —
             * hoisting `typedef struct X X;` would collide with the
             * vtable struct's real typedef */
            if unsafe { self.traits.find(name, nlen) } < TRAIT_CAP {
                continue;
            }
            /* a type alias of this name declares itself in pass A */
            let mut is_alias = false;
            if !file.is_null() {
                let fk = unsafe { (*file).kids };
                let fnn = unsafe { (*file).n_kids } as usize;
                let mut i = 0usize;
                while i < fnn {
                    let it = unsafe { *fk.add(i) };
                    if !it.is_null()
                        && unsafe { (*it).kind } == pm_jit_rsx_ast_kind::TYPE_ALIAS
                    {
                        let tn = unsafe { (*it).text };
                        let tnl = unsafe { (*it).text_len };
                        if tnl == nlen {
                            let mut j = 0usize;
                            let mut eq = true;
                            while j < tnl {
                                if unsafe { *tn.add(j) } != unsafe { *name.add(j) } {
                                    eq = false;
                                    break;
                                }
                                j += 1;
                            }
                            if eq {
                                is_alias = true;
                                break;
                            }
                        }
                    }
                    i += 1;
                }
            }
            if is_alias {
                continue;
            }
            self.out.puts(b"typedef struct \0".as_ptr());
            self.out.put(name, nlen);
            self.out.putc(b' ');
            self.out.put(name, nlen);
            self.out.puts(b";\n\0".as_ptr());
            emitted = true;
            s += 1;
        }
        self.opq_flushed = s;
        if emitted {
            self.out.putc(b'\n');
        }
    }

    /* File: two passes — collect types, then emit in dependency order. */
    unsafe fn lower_file(&mut self, file: *const pm_jit_rsx_ast_t) -> bool {
        if file.is_null() || !self.ok {
            return false;
        }
        self.file_node = file;
        unsafe { self.collect(file) };
        if !self.ok {
            return false;
        }
        self.out.puts(b"/* generated by pymergetic.metal.jit.rs.compiler */\n\0".as_ptr());
        self.out.puts(b"#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n#include <string.h>\n\0".as_ptr());
        self.out.putc(b'\n');
        /* the terminal/env plane arms at collect time: a BODY use
         * (`io::stdout().is_terminal()`, `std::env::var_os(..)`) sets
         * its flag only when the fn's pre-scan runs (pass D), which is
         * after every file-scope include window — so the preamble walk
         * below re-scans the unit tree for the marker method names and
         * arms the includes here, before any fn body can name them. */
        unsafe { self.scan_std_includes(file) };
        /* the FILE * plane's include rides the same preamble window: an
         * impl-Write param (FILE * type) armed it at collect, and the
         * scan above catches every body-time use. */
        if self.file_used {
            self.out.puts(b"#include <stdio.h>\n\0".as_ptr());
        }
        if self.unistd_used {
            self.out.puts(b"#include <unistd.h>\n\0".as_ptr());
        }
        if self.env_used {
            self.out.puts(b"#include <stdlib.h>\n\0".as_ptr());
        }
        /* String-plane typedefs hoisted to the preamble. collect() has run,
         * so the flags are already set for every type the file can name
         * (fn sigs, struct fields, const/static declared types); pass 0a
         * lowers CONSTs next and a `pub const X: &str` would otherwise
         * spell rsx_str_ref_t before its typedef. The bodies' uses
         * (String::from, .to_string(), …) mark the flags later but only
         * add *functions* — the typedef itself is what must precede
         * everything, and one emission here is exactly that. */
        unsafe { self.str_emit_rest() };
        unsafe { self.str_own_emit_rest() };
        /* struct-shaped Option typedefs are NOT hoisted into the preamble:
         * a payload naming a type alias (`rsx_opt_Handler`) must follow that
         * alias's typedef, and structs with Option fields must follow the
         * Option typedef — pass A flushes them as each naming type lands,
         * and any remainder (primitive payloads) follows pass A. */
        let kids = unsafe { (*file).kids };
        let nk = unsafe { (*file).n_kids } as usize;
        /* loop slots — declared once; each pass reassigns (Rust shadowing
         * per-pass would redeclare the same name in one C scope). */
        let mut i = 0usize;
        let mut item: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
        let mut kind = pm_jit_rsx_ast_kind::FILE;
        /* Batching: a failed item records its refusal and the pass keeps
         * scanning the remaining items, so one run reports every gap (the
         * old single-refusal abort cost one rebuild per error). Output is
         * discarded on failure anyway, so half-emitted items are harmless.
         * `ok` is re-armed between items only inside the pass loops below. */
        let mut bad = false;
        /* pass 0a: consts — `#define`s first, so array-length constants are
         * visible to every struct/field declaration below. Statics follow
         * the type pass (0b): their declarations name struct/alias types. */
        i = 0;
        while i < nk {
            item = unsafe { *kids.add(i) };
            if item.is_null() {
                i += 1;
                continue;
            }
            kind = unsafe { (*item).kind };
            if kind == pm_jit_rsx_ast_kind::CONST {
                unsafe { self.lower_static(item, 0) };
            }
            if !self.ok {
                bad = true;
                self.ok = true;
            }
            i += 1;
        }
        /* pass A0: enums first. An enum is an opaque integer tag in C —
         * `typedef enum E E;` needs no other type — while a struct field
         * can name one. Face splices append `#[path]` types after the
         * muscle, so file order alone would emit a struct's field before
         * the enum typedef it names; hoisting every ENUM item (a full
         * sweep before the struct/alias sweep) makes the append-splice
         * sound. Aliases stay in the later sweep — an alias may name a
         * struct, so it must follow it. */
        i = 0;
        while i < nk {
            item = unsafe { *kids.add(i) };
            if item.is_null() {
                i += 1;
                continue;
            }
            if unsafe { (*item).kind } == pm_jit_rsx_ast_kind::ENUM {
                /* One C definition per name: an enum emitted once is in
                 * tydone, and a same-name struct later in the unit (the
                 * exports face's opaque `_opaque` spelling of a type the
                 * types face already declared) is skipped by the same
                 * set — the real definition wins. A TAGGED enum (payload
                 * variants) only takes a forward tag here: its union
                 * members name payload structs by value, which pass A
                 * completes — the full tagged-union definition rides
                 * pass A5, after structs. */
                let en = unsafe { (*item).text };
                let el = unsafe { (*item).text_len };
                if unsafe { self.enumtags.has(en, el) } {
                    /* tagged: forward tag only — pass A5 (after structs)
                     * lowers the full body and marks tydone. Skip here
                     * without marking, so A5 still owns it. */
                    self.out.puts(b"typedef struct \0".as_ptr());
                    self.out.put(en, el);
                    self.out.putc(b' ');
                    self.out.put(en, el);
                    self.out.puts(b";\n\0".as_ptr());
                } else if !unsafe { self.tydone_find(en, el) } {
                    unsafe { self.tydone_add(en, el) };
                    unsafe { self.lower_enum(item) };
                    unsafe { self.opt_emit_for(en, el) };
                    unsafe { self.tup_emit_for(kids, nk, en, el) };
                }
            }
            if !self.ok {
                bad = true;
                self.ok = true;
            }
            i += 1;
        }
        /* pass A: struct/typedef items — dependency-ordered. A struct's
         * by-value field naming another unit type needs that type's
         * typedef complete first (C rule); pointer fields only need the
         * forward declaration lower_struct already emits. File order is
         * not a valid C order (face splices append types after the
         * muscle), so emit_struct_ordered pulls deps ahead recursively.
         * Aliases: the aliased type is by-value (`typedef S S2;` needs S
         * complete) — dep-collect its TYPE node the same way, then emit
         * the alias itself in file order. */
        i = 0;
        while i < nk {
            item = unsafe { *kids.add(i) };
            if item.is_null() {
                i += 1;
                continue;
            }
            kind = unsafe { (*item).kind };
            if kind == pm_jit_rsx_ast_kind::STRUCT {
                unsafe { self.emit_struct_ordered(kids, nk, item) };
            } else if kind == pm_jit_rsx_ast_kind::TYPE_ALIAS {
                /* alias: pull in the struct/union the aliased type names
                 * by value before the typedef renders it */
                let akids = unsafe { (*item).kids };
                let akn = unsafe { (*item).n_kids } as usize;
                let mut ty: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
                let mut j = 0usize;
                while j < akn {
                    let k = unsafe { *akids.add(j) };
                    if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::TYPE {
                        ty = k;
                    }
                    j += 1;
                }
                if !ty.is_null() {
                    let mut dep_bufs: [[u8; 48]; 16] = [[0; 48]; 16];
                    let mut dep_lens: [usize; 16] = [0; 16];
                    let mut dep_n: usize = 0;
                    unsafe {
                        self.dep_collect(
                            ty,
                            dep_bufs.as_mut_ptr() as *mut u8,
                            dep_lens.as_mut_ptr(),
                            &mut dep_n,
                            16,
                        );
                    };
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
                            break;
                        }
                    }
                }
                unsafe { self.lower_type_alias(item) };
                unsafe { self.opt_emit_for(unsafe { (*item).text }, unsafe { (*item).text_len }) };
                unsafe { self.tup_emit_for(kids, nk, unsafe { (*item).text }, unsafe { (*item).text_len }) };
            }
            if !self.ok {
                bad = true;
                self.ok = true;
            }
            i += 1;
        }
        /* remaining tuple typedefs — primitive element types need no
         * naming type; before the Option typedefs whose payloads may
         * name them (rsx_opt_rsx_tuple_…) and before prototypes/fns */
        /* from here on every unit type (struct/alias) is emitted, so a
         * pending Option typedef can no longer name an unemitted payload
         * — lower_static's flush becomes safe */
        self.types_done = true;
        /* String-plane typedefs — BEFORE the tuple/Option/Vec/lock rows:
         * a tuple row's element (or a Vec's, an Option's, a static's
         * type) can name rsx_str_t / rsx_str_ref_t, and C needs those
         * complete first. One-shot file-scope, same contract as the
         * rows below them. */
        unsafe { self.str_emit_rest() };
        unsafe { self.str_own_emit_rest() };
        unsafe { self.tup_emit_rest() };
        /* remaining Option typedefs — primitive payloads need no naming
         * type; emit before the prototypes/fns that use them. fn-ptr rows
         * first: an Option payload can name rsx_fnp_<row>. The whole
         * window runs to a FIXPOINT: rows gate their own deps
         * (row_dep_pending) and a round may leave rows pending (a Vec
         * over a tuple, a Result over an Option); loop until a full
         * round flushes nothing new. Every pass-A type is emitted by
         * now, so the loop always terminates: each round either emits
         * (progress) or nothing pending remains whose deps are all
         * complete. */
        {
            let mut rounds = 0usize;
            loop {
                let before = self.tup_pending_n()
                    + self.opt_pending_n()
                    + self.res_pending_n()
                    + self.vec_pending_n()
                    + self.btm_pending_n()
                    + self.arr_pending_n();
                unsafe { self.tup_emit_rest() };
                unsafe { self.fnp_emit_rest() };
                unsafe { self.opt_emit_rest(0) };
                unsafe { self.res_emit_rest() };
                unsafe { self.vec_emit_rest(0) };
                unsafe { self.btm_emit_rest(0) };
                unsafe { self.arr_emit_rest(0) };
                let after = self.tup_pending_n()
                    + self.opt_pending_n()
                    + self.res_pending_n()
                    + self.vec_pending_n()
                    + self.btm_pending_n()
                    + self.arr_pending_n();
                if after == before || rounds > 8 {
                    break;
                }
                rounds += 1;
            }
        }
        /* Lock rows — same file-scope contract (a fn signature naming
         * Mutex<T> needs the typedef complete before the prototype) */
        unsafe { self.lock_emit_rest() };
        /* trait-object typedefs: one `typedef struct { ret (*m)(..); .. }
         * Name;` per declared trait — the vtable inlined as fields. The
         * fn-ptr sigs were rendered at collect; a sig may name a struct
         * (param/ret types — pass A has emitted every unit type) AND the
         * container rows (a GenSink read slot names the Result/Option/
         * Vec rows) — the flushes above have landed every one of those
         * typedefs, so the fields' C types are complete here. Traits are
         * unit-local (no generic traits — a generic trait's object type
         * has no single C spelling), and the dyn plane is ref-carried:
         * the typedef is complete where declared. */
        i = 0;
        while i < nk {
            item = unsafe { *kids.add(i) };
            if item.is_null() {
                i += 1;
                continue;
            }
            if unsafe { (*item).kind } == pm_jit_rsx_ast_kind::TRAIT {
                unsafe { self.lower_trait_decl(item) };
            }
            if !self.ok {
                bad = true;
                self.ok = true;
            }
            i += 1;
        }
        /* pass 0b: statics — after the type pass, their declarations name
         * struct/alias types; their initializers may also need complete
         * types for compound literals. */
        unsafe { self.opq_emit(file) };
        i = 0;
        while i < nk {
            item = unsafe { *kids.add(i) };
            if item.is_null() {
                i += 1;
                continue;
            }
            kind = unsafe { (*item).kind };
            if kind == pm_jit_rsx_ast_kind::STATIC {
                /* fn-naming initializers defer to pass E (after prototypes) */
                let mut defer = false;
                {
                    let sk = unsafe { (*item).kids };
                    let sn = unsafe { (*item).n_kids } as usize;
                    let mut j = 0usize;
                    let mut initn: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
                    while j < sn {
                        let k2 = unsafe { *sk.add(j) };
                        let k2k = unsafe { (*k2).kind };
                        if k2k == pm_jit_rsx_ast_kind::TYPE {
                            if !initn.is_null() {
                                initn = k2;
                            }
                        } else if k2k != pm_jit_rsx_ast_kind::ATTR {
                            initn = k2;
                        }
                        j += 1;
                    }
                    if !initn.is_null() && unsafe { self.init_names_fn(initn) } {
                        defer = true;
                    }
                }
                if defer {
                    unsafe { self.lower_static(item, 2) };
                } else {
                    unsafe { self.lower_static(item, 0) };
                }
            }
            if !self.ok {
                bad = true;
                self.ok = true;
            }
            i += 1;
        }
        /* pass A5: tagged enums — after pass A completed every struct,
         * the payload union members name complete types now. A0 emitted
         * only the forward tag; the full tagged-union body lands here. */
        i = 0;
        while i < nk {
            item = unsafe { *kids.add(i) };
            if item.is_null() {
                i += 1;
                continue;
            }
            if unsafe { (*item).kind } == pm_jit_rsx_ast_kind::ENUM {
                let en = unsafe { (*item).text };
                let el = unsafe { (*item).text_len };
                if unsafe { self.enumtags.has(en, el) }
                    && !unsafe { self.tydone_find(en, el) }
                {
                    unsafe { self.tydone_add(en, el) };
                    unsafe { self.lower_enum(item) };
                }
            }
            if !self.ok {
                bad = true;
                self.ok = true;
            }
            i += 1;
        }
        /* pass B: extern prototypes — after structs, they name borrowed
         * types in their signatures. */
        unsafe { self.opq_emit(file) };
        i = 0;
        while i < nk {
            item = unsafe { *kids.add(i) };
            if item.is_null() {
                i += 1;
                continue;
            }
            if unsafe { (*item).kind } == pm_jit_rsx_ast_kind::EXTERN_BLOCK {
                unsafe { self.lower_extern_block(item) };
            }
            if !self.ok {
                bad = true;
                self.ok = true;
            }
            i += 1;
        }
        /* pass C: fn prototypes + impl method prototypes */
        unsafe { self.opq_emit(file) };
        i = 0;
        while i < nk {
            item = unsafe { *kids.add(i) };
            if item.is_null() {
                i += 1;
                continue;
            }
            kind = unsafe { (*item).kind };
            if kind == pm_jit_rsx_ast_kind::FN {
                unsafe { self.lower_fn(item, 1) };
            } else if kind == pm_jit_rsx_ast_kind::IMPL {
                unsafe { self.lower_impl(item, 1) };
            }
            if !self.ok {
                bad = true;
                self.ok = true;
            }
            i += 1;
        }
        /* pass D: bodies */
        unsafe { self.opq_emit(file) };
        i = 0;
        while i < nk {
            item = unsafe { *kids.add(i) };
            if item.is_null() {
                i += 1;
                continue;
            }
            kind = unsafe { (*item).kind };
            if kind == pm_jit_rsx_ast_kind::FN {
                unsafe { self.lower_fn(item, 0) };
            } else if kind == pm_jit_rsx_ast_kind::IMPL {
                unsafe { self.lower_impl(item, 0) };
            }
            if !self.ok {
                bad = true;
                self.ok = true;
            }
            i += 1;
        }
        /* pass E: statics deferred past the fn-prototype pass — an
         * initializer that names a fn (`listen: pm_metal_net_http_asgi_listen`)
         * needs that fn declared before the file-scope initializer's C
         * definition; pass 0 emitted them declare-only. */
        i = 0;
        while i < nk {
            item = unsafe { *kids.add(i) };
            if item.is_null() {
                i += 1;
                continue;
            }
            if unsafe { (*item).kind } == pm_jit_rsx_ast_kind::STATIC {
                let kids2 = unsafe { (*item).kids };
                let nk2 = unsafe { (*item).n_kids } as usize;
                let mut j = 0usize;
                let mut initn: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
                while j < nk2 {
                    let k2 = unsafe { *kids2.add(j) };
                    let k2k = unsafe { (*k2).kind };
                    if k2k == pm_jit_rsx_ast_kind::TYPE {
                        if !initn.is_null() {
                            initn = k2;
                        }
                    } else if k2k != pm_jit_rsx_ast_kind::ATTR {
                        initn = k2;
                    }
                    j += 1;
                }
                if !initn.is_null() && unsafe { self.init_names_fn(initn) } {
                    unsafe { self.lower_static(item, 0) };
                }
            }
            if !self.ok {
                bad = true;
                self.ok = true;
            }
            i += 1;
        }
        if bad {
            self.ok = false;
            return false;
        }
        true
    }

    /* Preamble-time include scan: walks the whole unit tree arming the
     * stdio/unistd/stdlib flags from the marker method names a BODY may
     * carry (`io::stdout().is_terminal()`, `std::env::var_os(..).is_some()`)
     * — the fn-time flag sets land after every include window, so this
     * pass is what hoists those headers to the preamble. String-literal
     * strictness is dropped here (a false positive only costs an unused
     * include; a false negative is a broken build). */
    unsafe fn scan_std_includes(&mut self, e: *const pm_jit_rsx_ast_t) {
        if e.is_null() {
            return;
        }
        if unsafe { (*e).kind } == pm_jit_rsx_ast_kind::METHOD_CALL {
            let t = unsafe { (*e).text };
            let tl = unsafe { (*e).text_len };
            if tl == 11 && !t.is_null() && unsafe { z_eq(t, tl, b"is_terminal\0".as_ptr()) } {
                self.unistd_used = true;
                self.file_used = true;
            } else if tl == 7 && !t.is_null()
                && (unsafe { z_eq(t, tl, b"is_some\0".as_ptr()) }
                    || unsafe { z_eq(t, tl, b"is_none\0".as_ptr()) })
            {
                /* var_os probe — only when the receiver path ends var_os */
                let kk = unsafe { (*e).kids };
                let kn = unsafe { (*e).n_kids } as usize;
                if kn >= 1 {
                    let r = unsafe { *kk.add(0) };
                    if unsafe { (*r).kind } == pm_jit_rsx_ast_kind::CALL {
                        let rk = unsafe { (*r).kids };
                        if (unsafe { (*r).n_kids } as usize) >= 1 {
                            let hp = unsafe { *rk.add(0) };
                            if unsafe { (*hp).kind } == pm_jit_rsx_ast_kind::PATH {
                                let hk = unsafe { (*hp).kids };
                                let hn = unsafe { (*hp).n_kids } as usize;
                                if hn >= 1 {
                                    let last = unsafe { *hk.add(hn - 1) };
                                    let lt = unsafe { (*last).text };
                                    let ll = unsafe { (*last).text_len };
                                    if ll == 6 && !lt.is_null()
                                        && unsafe { z_eq(lt, ll, b"var_os\0".as_ptr()) }
                                    {
                                        self.env_used = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        let kids = unsafe { (*e).kids };
        let nk = unsafe { (*e).n_kids } as usize;
        let mut i = 0usize;
        while i < nk {
            unsafe { self.scan_std_includes(*kids.add(i)) };
            i += 1;
        }
    }

    /* Does a static initializer mention a fn by name (a fn-ptr field)? Such
     * initializers are legal C only after the named fns are declared, so pass
     * 0 defers them and pass E emits them. */
    unsafe fn init_names_fn(&mut self, e: *const pm_jit_rsx_ast_t) -> bool {        if e.is_null() {
            return false;
        }
        let kind = unsafe { (*e).kind };
        if kind == pm_jit_rsx_ast_kind::PATH {
            let t = unsafe { (*e).text };
            let tl = unsafe { (*e).text_len };
            if tl > 0 && !t.is_null() {
                let s = unsafe { (*self.fns).slot(t, tl) };
                if s < SYM_CAP && unsafe { (*self.fns).used[s] } {
                    return true;
                }
            }
        }
        let kids = unsafe { (*e).kids };
        let nk = unsafe { (*e).n_kids } as usize;
        let mut i = 0usize;
        while i < nk {
            if unsafe { self.init_names_fn(*kids.add(i)) } {
                return true;
            }
            i += 1;
        }
        false
    }
    /* Trait-object typedef: `typedef struct { void *_self; <sig0>;
     * <sig1>; .. } Name;` — the vtable inlined as fn-ptr fields, each
     * taking the DATA pointer the object carries (sigs rendered at
     * collect with a void* receiver). */
    unsafe fn lower_trait_decl(&mut self, item: *const pm_jit_rsx_ast_t) {
        let tname = unsafe { (*item).text };
        let tlen = unsafe { (*item).text_len };
        let t = unsafe { self.traits.find(tname, tlen) };
        if t >= TRAIT_CAP {
            unsafe {
                self.err(b"trait not collected\0".as_ptr(), unsafe { (*item).line });
            }
            return;
        }
        unsafe { self.out.puts(b"typedef struct {\n    void *_self;\n".as_ptr()) };
        let mut m = 0usize;
        while m < self.traits.m_counts[t] {
            let row = t * TRAIT_MCAP + m;
            unsafe { self.out.puts(b"    ".as_ptr()) };
            unsafe {
                self.out.put(
                    self.traits.m_sigs[row].as_ptr(),
                    self.traits.m_sig_lens[row],
                )
            };
            unsafe { self.out.puts(b";\n".as_ptr()) };
            m += 1;
        }
        unsafe { self.out.puts(b"} ".as_ptr()) };
        unsafe { self.out.put(tname, tlen) };
        unsafe { self.out.puts(b";\n".as_ptr()) };
    }

}
