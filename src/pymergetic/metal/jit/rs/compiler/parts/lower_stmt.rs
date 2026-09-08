/* ==== RSX-CONT-2 ==== */

/* ==== statement / expression emitters ==== */

impl Lower {
    /* Emit one statement (or block tail expr) into self.out at self.depth. */
    unsafe fn emit_stmt(
        &mut self,
        s: *const pm_jit_rsx_ast_t,
        locals: *mut LocalTab,
        as_value: usize,
    ) {
        if s.is_null() || !self.ok {
            return;
        }
        let kind = unsafe { (*s).kind };
        let line = unsafe { (*s).line };
        match kind {
            pm_jit_rsx_ast_kind::LET => unsafe { self.emit_let(s, locals) },
            pm_jit_rsx_ast_kind::IF => unsafe {
                /* fn-body tail if: each branch's tail expr returns. */
                if as_value != 0 {
                    self.emit_if_tail(s, locals);
                } else {
                    self.emit_if(s, locals);
                }
            },
            pm_jit_rsx_ast_kind::MATCH => unsafe {
                /* one C brace scope per statement-position match: two
                 * sequential if-lets each mint their own `__rsx_m` temp
                 * (different Option rows — a shared spelling at fn scope
                 * is a conflicting redeclaration) and their own arm
                 * binds (`leaf` in the first if-let colliding with the
                 * second's). The scope wraps the WHOLE match, so the
                 * temp and binds die at the closing brace. */
                self.indent();
                self.out.puts(b"{\n\0".as_ptr());
                self.depth += 1;
                unsafe { (*locals).note_scope() };
                self.emit_match(s, locals);
                unsafe { (*locals).drop_scope() };
                self.depth -= 1;
                self.indent();
                self.out.puts(b"}\n\0".as_ptr());
            },
            pm_jit_rsx_ast_kind::LOOP => unsafe { self.emit_loop(s, locals) },
            pm_jit_rsx_ast_kind::WHILE => unsafe { self.emit_while(s, locals) },
            pm_jit_rsx_ast_kind::FOR => unsafe { self.emit_for(s, locals) },
            pm_jit_rsx_ast_kind::RETURN => unsafe { self.emit_return(s, locals) },
            pm_jit_rsx_ast_kind::BREAK => unsafe {
                let t = unsafe { (*s).text };
                let tl = unsafe { (*s).text_len };
                let labeled = tl > 0 && !t.is_null() && unsafe { *t == b'\'' };
                self.indent();
                if labeled {
                    /* C has no labeled break: `break 'l` is a goto to the
                     * _end label the labeled loop emits after its body. */
                    self.out.puts(b"goto \0".as_ptr());
                    unsafe { self.put_lbl(t, tl, b"_end\0".as_ptr()) };
                    self.out.puts(b";\n\0".as_ptr());
                } else {
                    self.out.puts(b"break;\n\0".as_ptr());
                }
            },
            pm_jit_rsx_ast_kind::CONTINUE => unsafe {
                let t = unsafe { (*s).text };
                let tl = unsafe { (*s).text_len };
                let labeled = tl > 0 && !t.is_null() && unsafe { *t == b'\'' };
                self.indent();
                if labeled {
                    /* `continue 'l` is a goto to the _cont label just
                     * before the loop's closing brace — the for-header
                     * increment still runs on the way out. */
                    self.out.puts(b"goto \0".as_ptr());
                    unsafe { self.put_lbl(t, tl, b"_cont\0".as_ptr()) };
                    self.out.puts(b";\n\0".as_ptr());
                } else {
                    self.out.puts(b"continue;\n\0".as_ptr());
                }
            },
            pm_jit_rsx_ast_kind::EXPR_STMT => {
                /* one expr kid */
                let kids = unsafe { (*s).kids };
                if unsafe { (*s).n_kids } >= 1 {
                    let e = unsafe { *kids.add(0) };
                    self.indent();
                    unsafe { self.emit_expr(e, locals) };
                    self.out.puts(b";\n\0".as_ptr());
                }
            }
            pm_jit_rsx_ast_kind::STMT => {}
            pm_jit_rsx_ast_kind::BLOCK => unsafe { self.emit_block_stmt(s, locals) },
            pm_jit_rsx_ast_kind::MACRO => unsafe {
                self.indent();
                self.out.puts(b"/* macro skipped */\n\0".as_ptr());
            },
            pm_jit_rsx_ast_kind::ASSIGN => {
                self.indent();
                unsafe { self.emit_expr(s, locals) };
                self.out.puts(b";\n\0".as_ptr());
            }
            _ => {
                /* bare expression statement (incl. value position) */
                self.indent();
                unsafe { self.emit_expr(s, locals) };
                if as_value == 0 {
                    self.out.puts(b";\n\0".as_ptr());
                } else {
                    self.out.putc(b'\n');
                }
            }
        }
    }

    unsafe fn emit_block_stmt(&mut self, b: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        /* unsafe-blocks emit without C braces — they add no C scope, so
         * shadowing inside one must reuse the enclosing declaration.
         * Plain statement blocks DO open a C scope: a `let` inside one
         * may shadow an outer name, and without braces the later outer
         * `let` of the same name would be a C redefinition. */
        let bl = unsafe { (*b).text_len };
        let bt = unsafe { (*b).text };
        let is_unsafe_block = bl == 6 && unsafe { z_eq(bt, bl, b"unsafe\0".as_ptr()) };
        /* capture before note_scope: the restore target is the scope we
         * came from, not the one we just opened. */
        let saved_epoch = unsafe { (*locals).epoch };
        if !is_unsafe_block {
            unsafe {
                (*locals).note_scope();
            }
            self.indent();
            self.out.puts(b"{\n\0".as_ptr());
            self.depth += 1;
        }
        let kids = unsafe { (*b).kids };
        let nk = unsafe { (*b).n_kids } as usize;
        let mut i = 0usize;
        while i < nk {
            let st = unsafe { *kids.add(i) };
            unsafe { self.emit_stmt(st, locals, 0) };
            i += 1;
        }
        if !is_unsafe_block {
            self.depth -= 1;
            self.indent();
            self.out.puts(b"}\n\0".as_ptr());
        }
        unsafe { self.end_block(locals, is_unsafe_block, saved_epoch) };
    }

    /* Emit a block's statements; a non-void tail expr that is the block
     * value needs as_value (caller declares the temp and appends). */
    /* Close an emitted block: inner locals die, the epoch rolls back so
     * sibling scopes never alias. unsafe-blocks add no C scope. */
    unsafe fn end_block(
        &mut self,
        locals: *mut LocalTab,
        is_unsafe_block: bool,
        saved_epoch: usize,
    ) {
        /* Guard releases run at the closing brace: every lock-guard
         * binding the dying scope owns (index >= its scope mark) is
         * released before the locals table rewinds. Emission is on the
         * Lower (it owns the Out); the table only holds the spans. */
        unsafe {
            let m = (*locals).nmarks;
            let from = if m > 0 { (*locals).marks[m - 1] } else { 0 };
            let mut s = (*locals).n;
            while s > from {
                s -= 1;
                let ga = (*locals).guard_addrs[s];
                let gl = (*locals).guard_lens[s];
                if !ga.is_null() && gl > 0 {
                    self.indent();
                    self.out.puts(b"pm_util_lock_release(&\0".as_ptr());
                    self.out.put(ga, gl);
                    self.out.puts(b".raw);\n\0".as_ptr());
                    (*locals).guard_addrs[s] = core::ptr::null();
                    (*locals).guard_lens[s] = 0;
                }
            }
        }
        if !is_unsafe_block {
            unsafe {
                (*locals).drop_scope();
            }
        }
        unsafe {
            (*locals).epoch = saved_epoch;
        }
    }

    /* Release every live lock guard (any surviving entry carries one —
     * drop_scope already rewound the dead ones). Called before a fn's
     * tail return and before the fn's closing brace: the value-tail
     * paths never pass through end_block, so a fn-scope guard would
     * otherwise leak its acquire. Emitting clears the note — a second
     * call site (tail return + fn end) must not double-release. */
    unsafe fn release_guards_all(&mut self, locals: *mut LocalTab) {
        let mut s = 0usize;
        unsafe {
            while s < (*locals).n {
                let ga = (*locals).guard_addrs[s];
                let gl = (*locals).guard_lens[s];
                if !ga.is_null() && gl > 0 {
                    self.indent();
                    self.out.puts(b"pm_util_lock_release(&\0".as_ptr());
                    self.out.put(ga, gl);
                    self.out.puts(b".raw);\n\0".as_ptr());
                    (*locals).guard_addrs[s] = core::ptr::null();
                    (*locals).guard_lens[s] = 0;
                }
                s += 1;
            }
        }
    }

    unsafe fn emit_block_value(&mut self, b: *const pm_jit_rsx_ast_t, locals: *mut LocalTab, temp: *const u8, temp_len: usize) {
        /* arm bodies arrive as bare expressions too, not only blocks;
         * a non-void tail expr that is the block value assigns into temp. */
        let bk = unsafe { (*b).kind };
        if bk != pm_jit_rsx_ast_kind::BLOCK && bk != pm_jit_rsx_ast_kind::STMT {
            /* a bare diverging arm body (`None => return e`) never yields a
             * value — emit the control flow as a statement, never `temp = …`. */
            if bk == pm_jit_rsx_ast_kind::RETURN
                || bk == pm_jit_rsx_ast_kind::BREAK
                || bk == pm_jit_rsx_ast_kind::CONTINUE
            {
                unsafe { self.emit_stmt(b, locals, 0) };
                return;
            }
            /* a desugared else-if-let chain nests a bare MATCH/IF as the
             * arm body — statement-position control flow, not a value */
            if bk == pm_jit_rsx_ast_kind::MATCH {
                unsafe { self.emit_match(b, locals) };
                return;
            }
            if bk == pm_jit_rsx_ast_kind::IF {
                unsafe { self.emit_if(b, locals) };
                return;
            }
            if !temp.is_null() {
                self.indent();
                self.out.put(temp, temp_len);
                self.out.puts(b" = \0".as_ptr());
                unsafe { self.emit_assign_value(b, locals) };
                self.out.puts(b";\n\0".as_ptr());
            } else {
                self.indent();
                unsafe { self.emit_expr(b, locals) };
                self.out.puts(b";\n\0".as_ptr());
            }
            return;
        }
        let bk2 = unsafe { (*b).kind };
        let bl2 = unsafe { (*b).text_len };
        let bt2 = unsafe { (*b).text };
        let is_unsafe_block2 = bl2 == 6 && unsafe { z_eq(bt2, bl2, b"unsafe\0".as_ptr()) };
        /* capture before note_scope — restore must land in the scope we
         * came from. */
        let saved_epoch = unsafe { (*locals).epoch };
        if !is_unsafe_block2 {
            unsafe {
                (*locals).note_scope();
            }
        }
        /* a plain value block OPENS a C scope: its lets (`let (a,b,c) =
         * { let base = ..; ..; (base, ..) }`) must not collide with the
         * destructure binds that follow the block (same spellings, the
         * outer tuple pattern's own). Statements emit INSIDE the
         * braces; the tail assigns the temp — the temp is declared in
         * the enclosing scope, so the assignment stays visible after
         * the closing brace. An unsafe block adds no scope (same
         * contract as emit_block_stmt). */
        if !is_unsafe_block2 {
            self.indent();
            self.out.puts(b"{\n\0".as_ptr());
            self.depth += 1;
        }
        let kids = unsafe { (*b).kids };
        let nk = unsafe { (*b).n_kids } as usize;
        let mut i = 0usize;
        while i < nk {
            let st = unsafe { *kids.add(i) };
            let is_tail = i + 1 == nk;
            if is_tail {
                /* unwrap statement wrappers — `break;` arrives as
                 * EXPR_STMT(BREAK). */
                let mut st2 = st;
                let mut hops = 0;
                while hops < 3
                    && (unsafe { (*st2).kind } == pm_jit_rsx_ast_kind::EXPR_STMT
                        || unsafe { (*st2).kind } == pm_jit_rsx_ast_kind::STMT)
                {
                    let sk = unsafe { (*st2).kids };
                    let sn = unsafe { (*st2).n_kids } as usize;
                    if sn == 0 {
                        break;
                    }
                    st2 = unsafe { *sk.add(0) };
                    hops += 1;
                }
                let k = unsafe { (*st2).kind };
                /* control-flow statements end the block — the value never
                 * arrives; emit them as statements, never `temp = …`. */
                if k == pm_jit_rsx_ast_kind::RETURN
                    || k == pm_jit_rsx_ast_kind::BREAK
                    || k == pm_jit_rsx_ast_kind::CONTINUE
                {
                    unsafe { self.emit_stmt(st2, locals, 0) };
                    unsafe { self.end_block(locals, is_unsafe_block2, saved_epoch) };
                    if !is_unsafe_block2 {
                        self.depth -= 1;
                        self.indent();
                        self.out.puts(b"}\n\0".as_ptr());
                    }
                    return;
                }
                let valuey = k == pm_jit_rsx_ast_kind::IF
                    || k == pm_jit_rsx_ast_kind::MATCH
                    || k == pm_jit_rsx_ast_kind::BLOCK
                    || k == pm_jit_rsx_ast_kind::LOOP;
                if valuey {
                    /* value-position block-expr: assign into temp. Only
                     * if/match are supported; nested blocks recurse to
                     * their own tail. */
                    if k == pm_jit_rsx_ast_kind::IF {
                        unsafe { self.emit_if_value(st2, locals, temp, temp_len) };
                        unsafe {
                            (*locals).epoch = saved_epoch;
                        }
                        if !is_unsafe_block2 {
                            self.depth -= 1;
                            self.indent();
                            self.out.puts(b"}\n\0".as_ptr());
                        }
                        return;
                    }
                    if k == pm_jit_rsx_ast_kind::MATCH {
                        unsafe { self.emit_match_value(st2, locals, temp, temp_len) };
                        unsafe {
                            (*locals).epoch = saved_epoch;
                        }
                        if !is_unsafe_block2 {
                            self.depth -= 1;
                            self.indent();
                            self.out.puts(b"}\n\0".as_ptr());
                        }
                        return;
                    }
                    if k == pm_jit_rsx_ast_kind::BLOCK {
                        unsafe { self.emit_block_value(st2, locals, temp, temp_len) };
                        unsafe {
                            (*locals).epoch = saved_epoch;
                        }
                        if !is_unsafe_block2 {
                            self.depth -= 1;
                            self.indent();
                            self.out.puts(b"}\n\0".as_ptr());
                        }
                        return;
                    }
                    if k == pm_jit_rsx_ast_kind::LOOP {
                        /* a loop as the arm/block tail is control flow, not a
                         * value — it runs and falls out; nothing to assign */
                        unsafe { self.emit_loop(st2, locals) };
                        unsafe {
                            (*locals).epoch = saved_epoch;
                        }
                        if !is_unsafe_block2 {
                            self.depth -= 1;
                            self.indent();
                            self.out.puts(b"}\n\0".as_ptr());
                        }
                        return;
                    }
                    unsafe {
                        self.err(
                            b"unsupported: value-position block expression\0".as_ptr(),
                            unsafe { (*st2).line },
                        );
                    }
                    unsafe { self.end_block(locals, is_unsafe_block2, saved_epoch) };
                    if !is_unsafe_block2 {
                        self.depth -= 1;
                        self.indent();
                        self.out.puts(b"}\n\0".as_ptr());
                    }
                    return;
                }
                /* plain tail expr: emit as `temp = expr;` when a temp is given */
                if !temp.is_null() {
                    self.indent();
                    self.out.put(temp, temp_len);
                    self.out.puts(b" = \0".as_ptr());
                    unsafe { self.emit_assign_value(st2, locals) };
                    self.out.puts(b";\n\0".as_ptr());
                    unsafe { self.end_block(locals, is_unsafe_block2, saved_epoch) };
                    if !is_unsafe_block2 {
                        self.depth -= 1;
                        self.indent();
                        self.out.puts(b"}\n\0".as_ptr());
                    }
                    return;
                }
            }
            unsafe { self.emit_stmt(st, locals, 0) };
            i += 1;
        }
        unsafe { self.end_block(locals, is_unsafe_block2, saved_epoch) };
        if !is_unsafe_block2 {
            self.depth -= 1;
            self.indent();
            self.out.puts(b"}\n\0".as_ptr());
        }
    }

    /* `let Some(bind) = init else { diverging }` — the flat, single-eval
     * lowering. struct-Option: temp + ._v + !._has test. pointer-Option:
     * the bind IS the temp (None == 0). See emit_let's header comment. */
    unsafe fn emit_let_else(&mut self, s: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        let kids = unsafe { (*s).kids };
        let nk = unsafe { (*s).n_kids } as usize;
        let line = unsafe { (*s).line };
        if nk < 3 {
            return;
        }
        /* kids: [pat, (mut)?, init, else] — pat's kids [Some seg, bind] */
        let pat = unsafe { *kids.add(0) };
        /* tuple-of-Some pattern `let (Some(a), Some(b)) = (e0, e1) else ..`:
         * one temp tuple, each bind from the field's payload, one `if`
         * testing every field (._has for struct-Options, == 0 for pointer
         * payloads) running the diverging else-block. */
        if unsafe { (*pat).kind } == pm_jit_rsx_ast_kind::TUPLE {
            unsafe { self.emit_let_else_tuple(s, locals) };
            return;
        }
        let mut bind: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
        if unsafe { (*pat).kind } == pm_jit_rsx_ast_kind::PATH {
            let pk = unsafe { (*pat).kids };
            let pnk = unsafe { (*pat).n_kids } as usize;
            if pnk >= 2 {
                bind = unsafe { *pk.add(1) };
            }
        }
        let mut init: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
        let mut els: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
        let mut i = 1usize;
        while i < nk {
            let k = unsafe { *kids.add(i) };
            if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::BLOCK && init.is_null() == false {
                els = k;
            } else if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::ATTR {
                /* mut marker — C locals are never const */
            } else {
                init = k;
            }
            i += 1;
        }
        if bind.is_null() || init.is_null() || els.is_null() {
            unsafe {
                self.err(b"internal: malformed let-else\0".as_ptr(), line);
            }
            return;
        }
        /* the bind: a PATH (one identifier) or a TUPLE (Option-of-tuple) */
        let is_tuple_bind = unsafe { (*bind).kind } == pm_jit_rsx_ast_kind::TUPLE;
        if !is_tuple_bind && unsafe { (*bind).kind } != pm_jit_rsx_ast_kind::PATH {
            unsafe {
                self.err(b"unsupported: let-else binds one identifier\0".as_ptr(), line);
            }
            return;
        }
        let mut bleaf: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
        let mut bname: *const u8 = b"_\0".as_ptr();
        let mut blen = 1usize;
        if !is_tuple_bind {
            bleaf = unsafe { *(*bind).kids.add(0) };
            bname = unsafe { (*bleaf).text };
            blen = unsafe { (*bleaf).text_len };
        }
        let ct = self.arena_tmp();
        let ct_len = unsafe { self.expr_ctype(init, ct, 128, locals) };
        if ct_len == 0 {
            unsafe {
                self.err(b"cannot infer let-else initializer type\0".as_ptr(), line);
            }
            return;
        }
        /* struct-Option (rsx_opt_<elem>) vs pointer-Option (ends ` *`)? */
        let mut is_struct_opt = false;
        if ct_len >= 8 && unsafe { z_eq(ct, 8, b"rsx_opt_\0".as_ptr()) } {
            is_struct_opt = true;
        }
        /* Result (rsx_res_<T>_<E>): `let Ok(x) = e else { .. }` — same
         * shape as the struct-Option arm but the tag is `._ok` and the
         * payload `._v`. The Ok-pat's ctor segment ("Ok") is kids[0][0]
         * of the pat node; a Some-pat never types as rsx_res_, so the
         * pair (pat-ctor, ct) decides the arm unambiguously. */
        if ct_len >= 8 && unsafe { z_eq(ct, 8, b"rsx_res_\0".as_ptr()) } {
            /* the Ok-payload C type: parse the typedef name back to its
             * two payload spellings (res_typedef_elem mirrors
             * opt_typedef_elem's <len>e<hex> decode). */
            let ob = self.arena_tmp();
            let eb2 = self.arena_tmp();
            let on = unsafe { Lower::res_typedef_elem(ct, ct_len, ob, 96, eb2, 96) };
            if on == 0 {
                unsafe {
                    self.err(b"internal: let-else result payload\0".as_ptr(), line);
                }
                return;
            }
            unsafe { self.res_emit_rest() };
            let le = unsafe { self.le_temp() };
            let le_len = self.le_name_len;
            self.indent();
            self.out.put(ct, ct_len);
            self.out.putc(b' ');
            self.out.put(le, le_len);
            self.out.puts(b" = \0".as_ptr());
            unsafe { self.emit_expr(init, locals) };
            self.out.puts(b";\n\0".as_ptr());
            self.indent();
            self.out.puts(b"if (!\0".as_ptr());
            self.out.put(le, le_len);
            self.out.puts(b"._ok) {\n\0".as_ptr());
            self.depth += 1;
            unsafe { self.emit_block_stmt(els, locals) };
            self.depth -= 1;
            self.indent();
            self.out.puts(b"}\n\0".as_ptr());
            self.indent();
            self.out.put(ob, on);
            self.out.putc(b' ');
            self.out.put(bname, blen);
            self.out.puts(b" = \0".as_ptr());
            self.out.put(le, le_len);
            self.out.puts(b"._v;\n\0".as_ptr());
            unsafe {
                (*locals).add(bname, blen, ob, on, self.depth);
            }
            return;
        }
        if !is_struct_opt {
            if ct_len < 2 || unsafe { *ct.add(ct_len - 1) } != b'*' {
                unsafe {
                    self.err(b"unsupported: let-else on a non-Option initializer\0".as_ptr(), line);
                }
                return;
            }
        }
        if is_struct_opt {
            let elbuf = self.arena_tmp();
            let eln = unsafe { Lower::opt_typedef_elem(ct, ct_len, elbuf, 96) };
            if eln == 0 || eln > 96 {
                unsafe {
                    self.err(b"internal: let-else option payload\0".as_ptr(), line);
                }
                return;
            }
            /* struct-Option: one temp; the ._has test runs BEFORE the
             * payload copy — no `._v` of a None is ever read */
            /* the row may have interned past the preamble flush (the
             * pre-scan can't type mid-body locals, so finds like
             * `t.find('(')` register late) — flush pending typedefs
             * right here (mid-function typedefs are C; before use is
             * all the order the language needs) */
            unsafe { self.fnp_emit_rest() };
            unsafe { self.opt_emit_rest(0) };
            let le = unsafe { self.le_temp() };
            let le_len = self.le_name_len;
            self.indent();
            self.out.put(ct, ct_len);
            self.out.putc(b' ');
            self.out.put(le, le_len);
            self.out.puts(b" = \0".as_ptr());
            unsafe { self.emit_expr(init, locals) };
            self.out.puts(b";\n\0".as_ptr());
            self.indent();
            self.out.puts(b"if (!\0".as_ptr());
            self.out.put(le, le_len);
            self.out.puts(b"._has) {\n\0".as_ptr());
            self.depth += 1;
            unsafe { self.emit_block_stmt(els, locals) };
            self.depth -= 1;
            self.indent();
            self.out.puts(b"}\n\0".as_ptr());
            if is_tuple_bind {
                /* Option-of-tuple: cur_opt_elem drives the shared destructure */
                unsafe { core::ptr::copy_nonoverlapping(elbuf, self.cur_opt_elem.as_mut_ptr(), eln) };
                self.cur_opt_elem_len = eln;
                unsafe { self.emit_some_binds(bind, le, le_len, ct, ct_len, locals) };
            } else {
                self.indent();
                self.out.put(elbuf, eln);
                self.out.putc(b' ');
                self.out.put(bname, blen);
                self.out.puts(b" = \0".as_ptr());
                self.out.put(le, le_len);
                self.out.puts(b"._v;\n\0".as_ptr());
                unsafe {
                    (*locals).add(bname, blen, elbuf, eln, self.depth);
                }
            }
        } else {
            /* pointer-Option: the bind IS the temp; None == 0 */
            self.indent();
            self.out.put(ct, ct_len);
            self.out.putc(b' ');
            self.out.put(bname, blen);
            self.out.puts(b" = \0".as_ptr());
            unsafe { self.emit_expr(init, locals) };
            self.out.puts(b";\n\0".as_ptr());
            unsafe {
                (*locals).add(bname, blen, ct, ct_len, self.depth);
            }
            self.indent();
            self.out.puts(b"if (\0".as_ptr());
            self.out.put(bname, blen);
            self.out.puts(b" == 0) {\n\0".as_ptr());
            self.depth += 1;
            unsafe { self.emit_block_stmt(els, locals) };
            self.depth -= 1;
            self.indent();
            self.out.puts(b"}\n\0".as_ptr());
        }
    }

    /* `let (Some(a), Some(b)) = (e0, e1) else { diverging }` — the
     * tuple-of-Options let-else. Kids: [TUPLE pat, (TYPE)?, init, BLOCK].
     * One temp tuple holds the initializer (single evaluation); each
     * Some-bind declares from its field (payload `._v` for struct-Options,
     * the field itself for pointer payloads — None == 0); one `if` tests
     * every fielded Option and runs the diverging else-block. Parse only
     * lets all-Some element patterns through, so each sub is
     * PATH("pat", [Some seg, bind]). */
    unsafe fn emit_let_else_tuple(&mut self, s: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        let kids = unsafe { (*s).kids };
        let nk = unsafe { (*s).n_kids } as usize;
        let line = unsafe { (*s).line };
        if nk < 3 {
            unsafe {
                self.err(b"internal: malformed tuple let-else\0".as_ptr(), line);
            }
            return;
        }
        let pat = unsafe { *kids.add(0) };
        let pn = unsafe { (*pat).n_kids } as usize;
        if pn == 0 || pn > TUP_MAXF {
            unsafe {
                self.err(b"unsupported: tuple pattern with more than 4 binds\0".as_ptr(), line);
            }
            return;
        }
        /* remaining kids: optional TYPE, init expr, else BLOCK */
        let mut init: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
        let mut els: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
        let mut i = 1usize;
        while i < nk {
            let k = unsafe { *kids.add(i) };
            let kk = unsafe { (*k).kind };
            if kk == pm_jit_rsx_ast_kind::TYPE {
                /* ascription on a tuple let-else: the init's inferred type
                 * is the same tuple; nothing to record */
            } else if kk == pm_jit_rsx_ast_kind::BLOCK && !init.is_null() {
                els = k;
            } else if kk != pm_jit_rsx_ast_kind::ATTR {
                init = k;
            }
            i += 1;
        }
        if init.is_null() || els.is_null() {
            unsafe {
                self.err(b"internal: malformed tuple let-else\0".as_ptr(), line);
            }
            return;
        }
        while unsafe { (*init).kind } == pm_jit_rsx_ast_kind::EXPR_STMT
            && unsafe { (*init).n_kids } as usize >= 1
        {
            init = unsafe { *(*init).kids.add(0) };
        }
        let ct = self.arena_tmp();
        let mut ct_len = unsafe { self.expr_ctype(init, ct, 128, locals) };
        if ct_len == 0 || ct_len >= 128 {
            unsafe {
                self.err(b"cannot infer tuple let-else type\0".as_ptr(), line);
            }
            return;
        }
        /* `*const (..)` derefs carry a `const ` qualifier into the
         * inferred type — strip it so the tuple prefix test and the
         * signature lookup see the registered rsx_tuple_ name. */
        if ct_len >= 6 && unsafe { z_eq(ct, 6, b"const \0".as_ptr()) } {
            unsafe {
                core::ptr::copy_nonoverlapping(ct.add(6), ct, ct_len - 6);
                *(ct.add(ct_len - 6)) = 0;
            }
            ct_len -= 6;
        }
        if ct_len < 10 || !unsafe { z_eq(ct, 10, b"rsx_tuple_\0".as_ptr()) } {
            unsafe {
                self.err(b"tuple let-else on a non-tuple initializer\0".as_ptr(), line);
            }
            return;
        }
        let slot = unsafe { self.tup_find(ct, ct_len) };
        if slot >= TUP_CAP {
            unsafe {
                self.err(b"internal: tuple let-else signature not registered\0".as_ptr(), line);
            }
            return;
        }
        if self.tup_counts[slot] != pn {
            unsafe {
                self.err(b"tuple let-else pattern does not match the tuple type\0".as_ptr(), line);
            }
            return;
        }
        /* the temp name: __rsx_tup<N>, one counter for every tuple temp */
        let tmp = self.arena_tmp();
        let at0 = unsafe { bput(tmp, 160, 0, b"__rsx_tup\0".as_ptr(), 9) };
        let mut cnt = self.tup_tmp_n;
        self.tup_tmp_n += 1;
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
        let mut tmp_len = at0;
        while q > 0 {
            q -= 1;
            tmp_len = unsafe { bput(tmp, 160, tmp_len, &digs[q], 1) };
        }
        unsafe {
            if tmp_len < 160 {
                *tmp.add(tmp_len) = 0;
            }
        }
        self.indent();
        self.out.put(ct, ct_len);
        self.out.putc(b' ');
        self.out.put(tmp, tmp_len);
        self.out.puts(b" = \0".as_ptr());
        unsafe { self.emit_expr(init, locals) };
        self.out.puts(b";\n\0".as_ptr());
        /* ORDER: the ._has test runs BEFORE any payload read — no `._v`
         * is copied out of a None field. The else-block diverges (parse
         * proves it), so binding after the test is sound: when the test
         * passes, every field is Some; when it fails, control never
         * reaches the binds. */
        self.indent();
        self.out.puts(b"if (\0".as_ptr());
        let mut first = true;
        let mut f = 0usize;
        while f < pn {
            let a = slot * TUP_MAXF + f;
            let elct = self.tup_elems[a].as_ptr();
            let elct_len = self.tup_lens[a];
            if elct_len >= 8 && unsafe { z_eq(elct, 8, b"rsx_opt_\0".as_ptr()) } {
                if !first {
                    self.out.puts(b" || \0".as_ptr());
                }
                self.out.puts(b"!\0".as_ptr());
                self.out.put(tmp, tmp_len);
                self.out.puts(b"._\0".as_ptr());
                let d = b'0' + f as u8;
                self.out.putc(d);
                self.out.puts(b"._has\0".as_ptr());
                first = false;
            } else {
                if !first {
                    self.out.puts(b" || \0".as_ptr());
                }
                self.out.put(tmp, tmp_len);
                self.out.puts(b"._\0".as_ptr());
                let d = b'0' + f as u8;
                self.out.putc(d);
                self.out.puts(b" == 0\0".as_ptr());
                first = false;
            }
            f += 1;
        }
        self.out.puts(b") {\n\0".as_ptr());
        self.depth += 1;
        unsafe { self.emit_block_stmt(els, locals) };
        self.depth -= 1;
        self.indent();
        self.out.puts(b"}\n\0".as_ptr());
        /* binds from the fields, payload spelling per element — emitted
         * only after every ._has check succeeded */
        let pk = unsafe { (*pat).kids };
        f = 0;
        while f < pn {
            let sub = unsafe { *pk.add(f) };
            /* unwrap Some(bind): pat kids [Some seg, bind] */
            let mut bind: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
            if unsafe { (*sub).kind } == pm_jit_rsx_ast_kind::PATH
                && unsafe { z_eq((*sub).text, (*sub).text_len, b"pat\0".as_ptr()) }
                && unsafe { (*sub).n_kids } as usize >= 2
            {
                bind = unsafe { *(*sub).kids.add(1) };
            }
            if bind.is_null() {
                unsafe {
                    self.err(b"unsupported: tuple let-else element (expect Some binds)\0".as_ptr(), line);
                }
                return;
            }
            let mut bnode = bind;
            if unsafe { (*bnode).kind } == pm_jit_rsx_ast_kind::PATH
                && unsafe { z_eq((*bnode).text, (*bnode).text_len, b"path\0".as_ptr()) }
                && unsafe { (*bnode).n_kids } as usize == 1
            {
                bnode = unsafe { *(*bnode).kids.add(0) };
            }
            if unsafe { (*bnode).kind } != pm_jit_rsx_ast_kind::PATH {
                unsafe {
                    self.err(b"unsupported: tuple let-else binds one identifier per element\0".as_ptr(), line);
                }
                return;
            }
            let bn = unsafe { (*bnode).text };
            let bl = unsafe { (*bnode).text_len };
            let a = slot * TUP_MAXF + f;
            let elct = self.tup_elems[a].as_ptr();
            let elct_len = self.tup_lens[a];
            /* payload type: struct-Option (rsx_opt_<elem>) unwraps ._v;
             * pointer-Option binds the field itself */
            let mut payct = elct;
            let mut payct_len = elct_len;
            let mut is_struct_opt = false;
            if elct_len >= 8 && unsafe { z_eq(elct, 8, b"rsx_opt_\0".as_ptr()) } {
                is_struct_opt = true;
                let pb = self.arena_tmp();
                let pln = unsafe { Lower::opt_typedef_elem(elct, elct_len, pb, 96) };
                if pln == 0 || pln > 96 {
                    unsafe {
                        self.err(b"internal: tuple let-else payload\0".as_ptr(), line);
                    }
                    return;
                }
                payct = pb;
                payct_len = pln;
            }
            /* `_` drops the bind */
            if !(bl == 1 && unsafe { z_eq(bn, 1, b"_\0".as_ptr()) }) {
                self.indent();
                self.out.put(payct, payct_len);
                self.out.putc(b' ');
                self.out.put(bn, bl);
                self.out.puts(b" = \0".as_ptr());
                self.out.put(tmp, tmp_len);
                self.out.puts(b"._\0".as_ptr());
                let d = b'0' + f as u8;
                self.out.putc(d);
                if is_struct_opt {
                    self.out.puts(b"._v\0".as_ptr());
                }
                self.out.puts(b";\n\0".as_ptr());
                unsafe {
                    (*locals).add(bn, bl, payct, payct_len, self.depth);
                }
            }
            f += 1;
        }
    }

    /* `let (a, b) = expr` — kids: [TUPLE pat, (type TYPE), init]. One temp
     * holds the initializer's value (the expr's tuple type drives the
     * typedef; an ascription, when present, must agree — it is rendered
     * instead when the init has no inferable type, e.g. value-position
     * if/match). Each element bind then declares from `temp._N`. */
    unsafe fn emit_let_tuple(&mut self, s: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        let kids = unsafe { (*s).kids };
        let nk = unsafe { (*s).n_kids } as usize;
        let line = unsafe { (*s).line };
        if nk < 2 {
            unsafe {
                self.err(b"tuple let with no initializer\0".as_ptr(), line);
            }
            return;
        }
        let pat = unsafe { *kids.add(0) };
        let pn = unsafe { (*pat).n_kids } as usize;
        if pn == 0 || pn > TUP_MAXF {
            unsafe {
                self.err(b"unsupported: tuple pattern with more than 4 binds\0".as_ptr(), line);
            }
            return;
        }
        /* remaining kids: optional TYPE, then init expr */
        let mut ty: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
        let mut init: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
        let mut i = 1usize;
        while i < nk {
            let k = unsafe { *kids.add(i) };
            let kk = unsafe { (*k).kind };
            if kk == pm_jit_rsx_ast_kind::TYPE {
                ty = k;
            } else {
                init = k;
            }
            i += 1;
        }
        if init.is_null() {
            unsafe {
                self.err(b"tuple let with no initializer\0".as_ptr(), line);
            }
            return;
        }
        /* unwrap EXPR_STMT wrappers (desugared if-let arms) */
        while unsafe { (*init).kind } == pm_jit_rsx_ast_kind::EXPR_STMT
            && unsafe { (*init).n_kids } as usize >= 1
        {
            init = unsafe { *(*init).kids.add(0) };
        }
        /* the tuple C type: ascription first, else inferred from init */
        let ct = self.arena_tmp();
        let mut ct_len = 0usize;
        if !ty.is_null() {
            ct_len = unsafe { self.ctype(ty, ct, 128) };
        }
        if ct_len == 0 {
            ct_len = unsafe { self.expr_ctype(init, ct, 128, locals) };
        }
        if ct_len == 0 || ct_len >= 128 {
            unsafe {
                self.err(b"cannot infer tuple let type - ascribe it\0".as_ptr(), line);
            }
            return;
        }
        /* `*const (..)` derefs carry a `const ` qualifier into the
         * inferred type — strip it so the tuple prefix test and the
         * signature lookup see the registered rsx_tuple_ name. */
        if ct_len >= 6 && unsafe { z_eq(ct, 6, b"const \0".as_ptr()) } {
            unsafe {
                core::ptr::copy_nonoverlapping(ct.add(6), ct, ct_len - 6);
                *(ct.add(ct_len - 6)) = 0;
            }
            ct_len -= 6;
        }
        if ct_len < 10 || !unsafe { z_eq(ct, 10, b"rsx_tuple_\0".as_ptr()) } {
            unsafe {
                self.err(b"tuple let on a non-tuple initializer\0".as_ptr(), line);
            }
            return;
        }
        let slot = unsafe { self.tup_find(ct, ct_len) };
        if slot >= TUP_CAP {
            unsafe {
                self.err(b"internal: tuple let signature not registered\0".as_ptr(), line);
            }
            return;
        }
        if self.tup_counts[slot] != pn {
            unsafe {
                self.err(b"tuple let pattern does not match the tuple type\0".as_ptr(), line);
            }
            return;
        }
        /* the temp: declared, then filled (value-position if/match store
         * into it; any other expr assigns directly). Name carries a
         * per-unit counter — a second tuple let in the same C scope
         * would redeclare the temp. */
        let tmp = self.arena_tmp();
        let at0 = unsafe { bput(tmp, 160, 0, b"__rsx_tup\0".as_ptr(), 9) };
        let mut cnt = self.tup_tmp_n;
        self.tup_tmp_n += 1;
        /* counter digits, ASCII, most-significant first */
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
        let mut tmp_len = at0;
        while q > 0 {
            q -= 1;
            tmp_len = unsafe { bput(tmp, 160, tmp_len, &digs[q], 1) };
        }
        unsafe {
            if tmp_len < 160 {
                *tmp.add(tmp_len) = 0;
            }
        }
        self.indent();
        self.out.put(ct, ct_len);
        self.out.putc(b' ');
        self.out.put(tmp, tmp_len);
        self.out.puts(b" = {0};\n\0".as_ptr());
        let ik = unsafe { (*init).kind };
        /* cur_ret carries the LET'S OWN tuple row (not the fn's return)
         * into the initializer's emission: a block/if/match tail tuple
         * picks its row from cur_ret when the arity matches, so the fn's
         * return row would poison a same-arity-but-different destructure
         * (`let (base, stars, is_const) = { ..; (base, stars, is_const) }`
         * inside a fn returning (String, bool, Option<String>) minted
         * the return row's cast and misdeclared every bind). Bytes are
         * saved too — a length-only restore corrupts the row name when
         * the tuple row is shorter than the fn's own return type. */
        let saved_ret: [u8; 128] = self.cur_ret;
        let saved_ret_len = self.cur_ret_len;
        {
            let mut j = 0usize;
            while j < ct_len && j < 127 {
                self.cur_ret[j] = unsafe { *ct.add(j) };
                j += 1;
            }
            self.cur_ret_len = ct_len;
        }
        if ik == pm_jit_rsx_ast_kind::IF {
            /* cur_ret carries the expected tuple to the arm bodies so
             * unsuffixed literals in `(0, 0)` mint the right signature. */
            unsafe { self.emit_if_value(init, locals, tmp, tmp_len) };
        } else if ik == pm_jit_rsx_ast_kind::MATCH {
            unsafe { self.emit_match_value(init, locals, tmp, tmp_len) };
        } else if ik == pm_jit_rsx_ast_kind::BLOCK && (unsafe { (*init).n_kids } as usize) >= 2 {
            /* multi-statement value block: the temp is the block's value
             * target — the block's own lets live in its scope, the tail
             * tuple assigns the temp (typing walked the same lets via the
             * forked tab). */
            unsafe { self.emit_block_value(init, locals, tmp, tmp_len) };
        } else {
            self.indent();
            self.out.put(tmp, tmp_len);
            self.out.puts(b" = \0".as_ptr());
            unsafe { self.emit_expr(init, locals) };
            self.out.puts(b";\n\0".as_ptr());
        }
        self.cur_ret = saved_ret;
        self.cur_ret_len = saved_ret_len;
        /* element binds from the temp's fields */
        let pk = unsafe { (*pat).kids };
        let mut f = 0usize;
        while f < pn {
            let sub = unsafe { *pk.add(f) };
            /* unwrap the PATH wrapper around each bind */
            let mut bnode = sub;
            if unsafe { (*bnode).kind } == pm_jit_rsx_ast_kind::PATH
                && unsafe { z_eq((*bnode).text, (*bnode).text_len, b"path\0".as_ptr()) }
                && unsafe { (*bnode).n_kids } as usize == 1
            {
                bnode = unsafe { *(*bnode).kids.add(0) };
            }
            if unsafe { (*bnode).kind } != pm_jit_rsx_ast_kind::PATH {
                unsafe {
                    self.err(b"unsupported: tuple pattern element (expect binds)\0".as_ptr(), line);
                }
                return;
            }
            let bn = unsafe { (*bnode).text };
            let bl = unsafe { (*bnode).text_len };
            /* `_` drops the bind */
            if bl == 1 && unsafe { z_eq(bn, 1, b"_\0".as_ptr()) } {
                f += 1;
                continue;
            }
            let a = slot * TUP_MAXF + f;
            let elct = self.tup_elems[a].as_ptr();
            let elct_len = self.tup_lens[a];
            self.indent();
            self.out.put(elct, elct_len);
            self.out.putc(b' ');
            self.out.put(bn, bl);
            self.out.puts(b" = \0".as_ptr());
            self.out.put(tmp, tmp_len);
            self.out.puts(b"._\0".as_ptr());
            let d = b'0' + f as u8;
            self.out.putc(d);
            self.out.puts(b";\n\0".as_ptr());
            unsafe {
                (*locals).add(bn, bl, elct, elct_len, self.depth);
            }
            f += 1;
        }
    }

    /* `Vec::new()` — CALL whose callee path's leaf is `new` with the
     * `Vec` segment before it (no args). */
    unsafe fn init_is_vec_new(&mut self, init: *const pm_jit_rsx_ast_t) -> bool {
        if unsafe { (*init).kind } != pm_jit_rsx_ast_kind::CALL {
            return false;
        }
        let ik = unsafe { (*init).kids };
        let ink = unsafe { (*init).n_kids } as usize;
        if ink < 2 {
            return false;
        }
        let callee = unsafe { *ik.add(0) };
        if unsafe { (*callee).kind } != pm_jit_rsx_ast_kind::PATH {
            return false;
        }
        let ck = unsafe { (*callee).kids };
        let cn = unsafe { (*callee).n_kids } as usize;
        if cn < 2 {
            return false;
        }
        let leaf = unsafe { *ck.add(cn - 1) };
        if !(unsafe { (*leaf).text_len } == 3 && unsafe { z_eq(unsafe { (*leaf).text }, 3, b"new\0".as_ptr()) }) {
            return false;
        }
        let head = unsafe { *ck.add(cn - 2) };
        unsafe { z_eq(unsafe { (*head).text }, unsafe { (*head).text_len }, b"Vec\0".as_ptr()) }
    }

    /* Depth-bounded body scan for the binding's first `<name>.push(x)`:
     * returns the pushed element's ctype length into out (0 when none
     * found). Walks statements, blocks, and match/if arms (depth cap 6
     * guards against pathological nesting). */
    unsafe fn scan_first_push(
        &mut self,
        name: *const u8,
        nlen: usize,
        node: *const pm_jit_rsx_ast_t,
        out: *mut u8,
        cap: usize,
        locals: *mut LocalTab,
        depth: usize,
    ) -> usize {
        if node.is_null() || depth > 6 || nlen == 0 {
            return 0;
        }
        let nk = unsafe { (*node).n_kids } as usize;
        let kk = unsafe { (*node).kids };
        /* scope-aware walk: a LET or IF-LET above a push binds the names
         * the push arg uses — register them (typed) so expr_ctype on the
         * arg resolves. The registrations are bounded by the walk's own
         * depth cap; the emission pass re-derives the real bindings
         * (this table's rows are only the scan's inference input). */
        let node_kind = unsafe { (*node).kind };
        if node_kind == pm_jit_rsx_ast_kind::LET {
            /* kids: name(PATH), [mut ATTR], [type TYPE], init — type the
             * init (or the ascription) and add the bind. */
            let mut bname: *const u8 = b"\0".as_ptr();
            let mut blen = 0usize;
            let mut bty: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
            let mut binit: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
            let mut j = 0usize;
            while j < nk {
                let kj = unsafe { *kk.add(j) };
                let kjk = unsafe { (*kj).kind };
                if kjk == pm_jit_rsx_ast_kind::PATH && blen == 0 {
                    bname = unsafe { (*kj).text };
                    blen = unsafe { (*kj).text_len };
                } else if kjk == pm_jit_rsx_ast_kind::TYPE {
                    bty = kj;
                } else if kjk != pm_jit_rsx_ast_kind::ATTR {
                    binit = kj;
                }
                j += 1;
            }
            if blen > 0 && !locals.is_null() {
                let tb = self.arena_tmp();
                let mut tn = 0usize;
                if !bty.is_null() {
                    tn = unsafe { self.ctype(bty, tb, 128) };
                }
                if tn == 0 && !binit.is_null() {
                    let saved_pre = self.pre_pool_at;
                    tn = unsafe { self.expr_ctype(binit, tb, 128, locals) };
                    self.pre_pool_at = saved_pre;
                }
                if tn > 0 && tn < 128 {
                    unsafe {
                        (*locals).add(bname, blen, tb, tn, 0);
                    }
                }
            }
            /* `let (a, b, c) = init` — TUPLE pat: register every element
             * bind from the registered rsx_tuple_<sig> signature so the
             * later `opaques.push(ret_op_elem)` scan types. Mirrors
             * emit_let_tuple's registration. */
            if blen == 0 && !locals.is_null() && !binit.is_null() {
                let pat0 = unsafe { *kk.add(0) };
                if unsafe { (*pat0).kind } == pm_jit_rsx_ast_kind::TUPLE {
                    let pn3 = unsafe { (*pat0).n_kids } as usize;
                    if pn3 > 0 && pn3 <= TUP_MAXF {
                        let tb = self.arena_tmp();
                        let mut tn = 0usize;
                        if !bty.is_null() {
                            tn = unsafe { self.ctype(bty, tb, 128) };
                        }
                        if tn == 0 {
                            let saved_pre = self.pre_pool_at;
                            tn = unsafe { self.expr_ctype(binit, tb, 128, locals) };
                            self.pre_pool_at = saved_pre;
                        }
                        if tn > 0 && tn < 128 {
                            /* strip a `const ` qualifier the same way
                             * emit_let_tuple does */
                            if tn >= 6 && unsafe { z_eq(tb, 6, b"const \0".as_ptr()) } {
                                unsafe {
                                    core::ptr::copy_nonoverlapping(tb.add(6), tb, tn - 6);
                                    *(tb.add(tn - 6)) = 0;
                                }
                                tn -= 6;
                            }
                            if tn >= 10 && unsafe { z_eq(tb, 10, b"rsx_tuple_\0".as_ptr()) } {
                                let slot = unsafe { self.tup_find(tb, tn) };
                                if slot < TUP_CAP && self.tup_counts[slot] == pn3 {
                                    let pk3 = unsafe { (*pat0).kids };
                                    let mut f3 = 0usize;
                                    while f3 < pn3 {
                                        let sub3 = unsafe { *pk3.add(f3) };
                                        /* unwrap PATH("path", [leaf]) */
                                        let mut bnode3 = sub3;
                                        if unsafe { (*bnode3).kind } == pm_jit_rsx_ast_kind::PATH
                                            && unsafe { z_eq((*bnode3).text, (*bnode3).text_len, b"path\0".as_ptr()) }
                                            && unsafe { (*bnode3).n_kids } as usize == 1
                                        {
                                            bnode3 = unsafe { *(*bnode3).kids.add(0) };
                                        }
                                        if unsafe { (*bnode3).kind } == pm_jit_rsx_ast_kind::PATH {
                                            let bn3 = unsafe { (*bnode3).text };
                                            let bl3 = unsafe { (*bnode3).text_len };
                                            /* `_` drops the bind */
                                            if !(bl3 == 1 && unsafe { z_eq(bn3, 1, b"_\0".as_ptr()) }) {
                                                let a3 = slot * TUP_MAXF + f3;
                                                let elct = self.tup_elems[a3].as_ptr();
                                                let elct_len = self.tup_lens[a3];
                                                if elct_len > 0 && elct_len < 128 {
                                                    unsafe {
                                                        (*locals).add(bn3, bl3, elct, elct_len, 0);
                                                    }
                                                }
                                            }
                                        }
                                        f3 += 1;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        if node_kind == pm_jit_rsx_ast_kind::IF && nk >= 2 {
            /* if-let: kids[0] is the desugared condition carrying the
             * Some(bind) pattern, kids[1] the then-block. Find the bind:
             * the parser stores the pattern as a PATH [Some seg, bind]
             * under the cond's first LET ("letelse") or IF-LET node. */
            let cond = unsafe { *kk.add(0) };
            if unsafe { (*cond).kind } == pm_jit_rsx_ast_kind::LET
                && unsafe { (*cond).text_len } == 7
                && unsafe { z_eq((*cond).text, 7, b"letelse\0".as_ptr()) }
            {
                let ck = unsafe { (*cond).kids };
                let cn = unsafe { (*cond).n_kids } as usize;
                if cn >= 3 {
                    let pat = unsafe { *ck.add(0) };
                    let pk2 = unsafe { (*pat).kids };
                    let pn2 = unsafe { (*pat).n_kids } as usize;
                    if pn2 >= 2 {
                        let bind = unsafe { *pk2.add(1) };
                        let bn3 = unsafe { (*bind).text };
                        let bl3 = unsafe { (*bind).text_len };
                        let mut init2: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
                        let mut j = 1usize;
                        while j < cn {
                            let kj = unsafe { *ck.add(j) };
                            if unsafe { (*kj).kind } == pm_jit_rsx_ast_kind::BLOCK {
                                break;
                            }
                            if unsafe { (*kj).kind } != pm_jit_rsx_ast_kind::ATTR {
                                init2 = kj;
                            }
                            j += 1;
                        }
                        if bl3 > 0 && !init2.is_null() && !locals.is_null() {
                            let tb = self.arena_tmp();
                            let saved_pre = self.pre_pool_at;
                            let tn = unsafe { self.expr_ctype(init2, tb, 128, locals) };
                            self.pre_pool_at = saved_pre;
                            if tn > 8
                                && tn < 128
                                && unsafe { z_eq(tb, 8, b"rsx_opt_\0".as_ptr()) }
                            {
                                /* the Option payload: decode the
                                 * rsx_opt_<elem> name back to the elem
                                 * spelling — that IS the bind's type. */
                                let elem = self.arena_tmp();
                                let eln = unsafe {
                                    Lower::opt_typedef_elem(tb, tn, elem, 128)
                                };
                                if eln > 0 && eln < 128 {
                                    unsafe {
                                        (*locals).add(bn3, bl3, elem, eln, 0);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        /* `if let` desugars to MATCH (one Some-arm + wildcard): type the
         * scrutinee, decode the Option payload, register every Some-arm's
         * bind so pushes inside the then-block type. */
        if node_kind == pm_jit_rsx_ast_kind::MATCH && nk >= 2 && !locals.is_null() {
            let scrut = unsafe { *kk.add(0) };
            let sb2 = self.arena_tmp();
            let saved_pre2 = self.pre_pool_at;
            let sn2 = unsafe { self.expr_ctype(scrut, sb2, 128, locals) };
            self.pre_pool_at = saved_pre2;
            if sn2 > 8
                && sn2 < 128
                && unsafe { z_eq(sb2, 8, b"rsx_opt_\0".as_ptr()) }
            {
                let elem = self.arena_tmp();
                let eln2 = unsafe { Lower::opt_typedef_elem(sb2, sn2, elem, 128) };
                if eln2 > 0 && eln2 < 128 {
                    let mut a2 = 1usize;
                    while a2 < nk {
                        let arm2 = unsafe { *kk.add(a2) };
                        if unsafe { (*arm2).kind } == pm_jit_rsx_ast_kind::MATCH_ARM
                            && unsafe { (*arm2).n_kids } as usize >= 2
                        {
                            let ak2 = unsafe { (*arm2).kids };
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
                                        /* Some(bind): one PATH bind of the
                                         * payload's own type */
                                        if unsafe { (*bind2).kind } == pm_jit_rsx_ast_kind::PATH
                                            && unsafe { (*bind2).n_kids } as usize >= 1
                                        {
                                            let bleaf2 = unsafe { *(*bind2).kids.add(0) };
                                            unsafe {
                                                (*locals).add(
                                                    unsafe { (*bleaf2).text },
                                                    unsafe { (*bleaf2).text_len },
                                                    elem,
                                                    eln2,
                                                    0,
                                                );
                                            }
                                        }
                                        /* Some((a, b)) on a strpair payload:
                                         * each element bind types as the
                                         * &str view. */
                                        if unsafe { (*bind2).kind } == pm_jit_rsx_ast_kind::TUPLE
                                            && eln2 == 13
                                            && unsafe { z_eq(elem, 13, b"rsx_strpair_t\0".as_ptr()) }
                                        {
                                            let tk2 = unsafe { (*bind2).kids };
                                            let tn2 = unsafe { (*bind2).n_kids } as usize;
                                            let mut t2 = 0usize;
                                            while t2 < tn2 && t2 < 2 {
                                                let sub2 = unsafe { *tk2.add(t2) };
                                                let mut bnode2 = sub2;
                                                if unsafe { (*bnode2).kind } == pm_jit_rsx_ast_kind::PATH
                                                    && unsafe { z_eq((*bnode2).text, (*bnode2).text_len, b"path\0".as_ptr()) }
                                                    && unsafe { (*bnode2).n_kids } as usize == 1
                                                {
                                                    bnode2 = unsafe { *(*bnode2).kids.add(0) };
                                                }
                                                if unsafe { (*bnode2).kind } == pm_jit_rsx_ast_kind::PATH {
                                                    let bn2 = unsafe { (*bnode2).text };
                                                    let bl2 = unsafe { (*bnode2).text_len };
                                                    if !(bl2 == 1 && unsafe { z_eq(bn2, 1, b"_\0".as_ptr()) }) {
                                                        unsafe {
                                                            (*locals).add(
                                                                bn2,
                                                                bl2,
                                                                b"rsx_str_ref_t\0".as_ptr(),
                                                                13,
                                                                0,
                                                            );
                                                        }
                                                    }
                                                }
                                                t2 += 1;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        a2 += 1;
                    }
                }
            }
        }
        let mut i = 0usize;
        while i < nk {
            let k = unsafe { *kk.add(i) };
            if !k.is_null() && unsafe { (*k).kind } == pm_jit_rsx_ast_kind::METHOD_CALL {
                let mk = unsafe { (*k).kids };
                let mn = unsafe { (*k).n_kids } as usize;
                if mn >= 3 {
                    let recv = unsafe { *mk.add(0) };
                    let mname = unsafe { *mk.add(1) };
                    let margs = unsafe { *mk.add(2) };
                    /* unwrap a "path" wrapper on the receiver */
                    let mut bnode = recv;
                    if unsafe { (*recv).kind } == pm_jit_rsx_ast_kind::PATH
                        && unsafe { z_eq(unsafe { (*recv).text }, unsafe { (*recv).text_len }, b"path\0".as_ptr()) }
                        && unsafe { (*recv).n_kids } as usize == 1
                    {
                        bnode = unsafe { *(*recv).kids.add(0) };
                    }
                    let is_push = unsafe { z_eq(unsafe { (*mname).text }, unsafe { (*mname).text_len }, b"push\0".as_ptr()) }
                        && (unsafe { (*margs).n_kids } as usize) == 1;
                    /* extend(vec) — the element comes from the arg's own
                     * Vec row: `opaques.extend(ops)` with ops: Vec<String>
                     * gives String. */
                    let is_extend = unsafe { z_eq(unsafe { (*mname).text }, unsafe { (*mname).text_len }, b"extend\0".as_ptr()) }
                        && (unsafe { (*margs).n_kids } as usize) == 1;
                    let same = unsafe { (*bnode).kind } == pm_jit_rsx_ast_kind::PATH
                        && unsafe { (*bnode).text_len } == nlen;
                    if (is_push || is_extend) && same {
                        let mut j = 0usize;
                        let bn2 = unsafe { (*bnode).text };
                        while j < nlen {
                            if unsafe { *bn2.add(j) } != unsafe { *name.add(j) } {
                                break;
                            }
                            j += 1;
                        }
                        if j == nlen {
                            let ak2 = unsafe { (*margs).kids };
                            let arg = unsafe { *ak2.add(0) };
                            if is_push {
                                return unsafe { self.expr_ctype(arg, out, cap, locals) };
                            }
                            /* extend: the arg must type as a Vec row —
                             * its element spelling is the answer. */
                            let vb = self.arena_tmp();
                            let vl = unsafe { self.expr_ctype(arg, vb, 128, locals) };
                            if vl > 8
                                && vl < 128
                                && unsafe { z_eq(vb, 8, b"rsx_vec_\0".as_ptr()) }
                            {
                                let row = unsafe { self.vecs.find_by_name(vb, vl) };
                                if row < VEC_CAP {
                                    let el = self.vecs.elems[row].as_ptr();
                                    let eln = self.vecs.elem_lens[row];
                                    if eln > 0 && eln < cap {
                                        let mut w = 0usize;
                                        while w < eln {
                                            unsafe {
                                                *out.add(w) = *el.add(w);
                                            }
                                            w += 1;
                                        }
                                        unsafe {
                                            *out.add(eln) = 0;
                                        }
                                        return eln;
                                    }
                                }
                            }
                            return 0;
                        }
                    }
                }
            }
            /* recurse into every kid (blocks, arms, exprs) */
            let r = unsafe { self.scan_first_push(name, nlen, k, out, cap, locals, depth + 1) };
            if r > 0 {
                return r;
            }
            i += 1;
        }
        0
    }

    unsafe fn emit_let(&mut self, s: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        /* kids: name(PATH), [mut ATTR], [type TYPE], [init expr]. Text tag
         * "letelse" (see parse_let) carries [Some-pat PATH, (mut), (init),
         * else-block]: `let Some(x) = E else { diverging }`. Lowered flat
         * with one evaluation of E: a temp struct, the bind from ._v (or
         * the temp itself for pointer Options), then `if (!temp._has)`
         * running the diverging else-block. Sound because the else-block
         * provably never falls through (parse_let checked). */
        if unsafe { (*s).text_len } == 7 && unsafe { z_eq((*s).text, 7, b"letelse\0".as_ptr()) } {
            unsafe { self.emit_let_else(s, locals) };
            return;
        }
        /* tuple pattern `let (a, b) = ..`: destructure — one temp holds the
         * whole tuple, each element bind declares from the temp's field. */
        {
            let kids0 = unsafe { (*s).kids };
            let nk0 = unsafe { (*s).n_kids } as usize;
            if nk0 >= 1 {
                let k0 = unsafe { *kids0.add(0) };
                if unsafe { (*k0).kind } == pm_jit_rsx_ast_kind::TUPLE {
                    unsafe { self.emit_let_tuple(s, locals) };
                    return;
                }
            }
        }
        let kids = unsafe { (*s).kids };
        let nk = unsafe { (*s).n_kids } as usize;
        let line = unsafe { (*s).line };
        let mut name: *const u8 = b"_\0".as_ptr();
        let mut name_len: usize = 1;
        let mut have_name = false;
        let mut ty: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
        let mut init: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
        let mut i = 0usize;
        while i < nk {
            let k = unsafe { *kids.add(i) };
            let kk = unsafe { (*k).kind };
            if kk == pm_jit_rsx_ast_kind::PATH && !have_name {
                name = unsafe { (*k).text };
                name_len = unsafe { (*k).text_len };
                have_name = true;
            } else if kk == pm_jit_rsx_ast_kind::TYPE {
                ty = k;
            } else if kk == pm_jit_rsx_ast_kind::ATTR {
                /* mut marker — recorded but not needed for emit */
            } else {
                init = k;
            }
            i += 1;
        }
        /* a match/if initializer inside a desugared if-let arm arrives
         * EXPR_STMT-wrapped; unwrap so the value-position paths below fire */
        while !init.is_null()
            && unsafe { (*init).kind } == pm_jit_rsx_ast_kind::EXPR_STMT
            && unsafe { (*init).n_kids } as usize >= 1
        {
            init = unsafe { *(*init).kids.add(0) };
        }
        /* `_`-binding: the *binding* is dropped, but the initializer still
         * runs for side effects (registration calls, reserved values).
         * A TUPLE init is pure value assembly — no side effects to keep,
         * and its emission interns a row the discarded compound literal
         * would name before its typedef lands (`let _ = (a, b);`). A
         * tuple statement has no effect in C either, so the whole let
         * drops. */
        if !have_name || (name_len == 1 && unsafe { z_eq(name, 1, b"_\0".as_ptr()) } && ty.is_null()) {
            let is_pure_tuple = !init.is_null()
                && unsafe { (*init).kind } == pm_jit_rsx_ast_kind::TUPLE;
            if !init.is_null() && !is_pure_tuple {
                unsafe { self.emit_expr(init, locals) };
                self.out.puts(b";\0".as_ptr());
            }
            return;
        }
        let ct = self.arena_tmp();
        let mut ct_len = 0usize;
        if !ty.is_null() {
            ct_len = unsafe { self.ctype(ty, ct, 128) };
            if ct_len == 0 {
                return;
            }
        } else if !init.is_null() {
            ct_len = unsafe { self.expr_ctype(init, ct, 128, locals) };
            if ct_len == 0 {
                /* `let v = Vec::new()` — an empty container typed by its
                 * uses: scan the fn body for the binding's first .push(x)
                 * and take the element from x (bounded, honest — a bad
                 * guess surfaces as a loud C error at the first
                 * mismatched push). */
                let is_vec_new = unsafe { self.init_is_vec_new(init) };
                if is_vec_new && !self.cur_body.is_null() {
                    let el = self.arena_tmp();
                    let eln = unsafe {
                        self.scan_first_push(name, name_len, self.cur_body, el, 128, locals, 0)
                    };
                    if eln > 0 && eln < 128 {
                        let row = unsafe { self.vecs.intern(el, eln) };
                        if row < VEC_CAP {
                            let nb = self.arena_tmp();
                            let nn = unsafe { VecTab::name_for(row, nb, 96) };
                            if nn > 0 && nn < 128 {
                                let mut w = 0usize;
                                while w < nn {
                                    unsafe {
                                        *ct.add(w) = *nb.add(w);
                                    }
                                    w += 1;
                                }
                                unsafe {
                                    *ct.add(nn) = 0;
                                }
                                ct_len = nn;
                            }
                        }
                    }
                }
                if ct_len == 0 {
                    let mut kn = unsafe { ast_kind_name(unsafe { (*init).kind }) };
                    let mut init2 = init;
                    /* unwrap BLOCK/unsafe wrappers so the failing inner form shows */
                    let mut hops = 0;
                    while hops < 4 && unsafe { (*init2).kind } == pm_jit_rsx_ast_kind::BLOCK {
                        let ikids = unsafe { (*init2).kids };
                        let ink = unsafe { (*init2).n_kids } as usize;
                        if ink == 0 {
                            break;
                        }
                        init2 = unsafe { *ikids.add(ink - 1) };
                        kn = unsafe { ast_kind_name(unsafe { (*init2).kind }) };
                        hops += 1;
                    }
                    unsafe {
                        self.err_let_name2(b"cannot infer let type - add ': T'\0".as_ptr(), line, name, name_len, kn);
                    }
                    return;
                }
            }
        } else {
            unsafe {
                self.err(b"let with no type and no initializer\0".as_ptr(), line);
            }
            return;
        }
        /* Shadowing: same spelling + same C type in the same block reuses
         * the declaration (plain assignment). Any other scope declares
         * fresh — C block scopes take care of nesting. */
        /* locals are never const in C — Rust's deferred-init
         * (`let x; if c { x = 1 } else { x = 0 }`) writes them after the
         * declaration, and inference may have carried a `const ` prefix
         * (deref of a `*const T`). Strip it before any shadow test —
         * prior rows were registered stripped, so the type compare
         * must see both sides normalized. */
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
        let mut reuse = false;
        unsafe {
            if (*locals).same_type_same_scope(name, name_len, ct, ct_len, self.depth, (*locals).epoch)
            {
                reuse = true;
            }
        }
        /* type-changing shadow: `let t = t.trim()` — C refuses to
         * redeclare the name in one scope, so this binding gets a fresh
         * C spelling name__N and later references emit it (LocalTab's
         * cname rows). */
        let mut cname: *const u8 = core::ptr::null_mut();
        let mut cname_len = 0usize;
        if !reuse {
            let prior = self.arena_tmp();
            let plen = unsafe { (*locals).same_scope_prior_type(name, name_len, self.depth, (*locals).epoch, prior) };
            let mut same = plen == ct_len;
            if same {
                let mut j = 0usize;
                while j < plen {
                    if unsafe { *prior.add(j) } != unsafe { *ct.add(j) } {
                        same = false;
                        break;
                    }
                    j += 1;
                }
            }
            if plen > 0 && !same {
                let cb = self.arena_tmp();
                let mut at = 0usize;
                at = unsafe { bput(cb, 128, at, name, name_len) };
                at = unsafe { bput(cb, 128, at, b"__\0".as_ptr(), 2) };
                let mut digs = [0u8; 12];
                let mut v = self.shadow_ctr;
                let mut di = 0usize;
                while v > 0 && di < 12 {
                    digs[di] = b'0' + (v % 10) as u8;
                    v /= 10;
                    di += 1;
                }
                if di == 0 {
                    digs[0] = b'0';
                    di = 1;
                }
                let mut j = di;
                while j > 0 {
                    j -= 1;
                    at = unsafe { bput(cb, 128, at, digs.as_ptr().add(j), 1) };
                }
                self.shadow_ctr += 1;
                cname = cb;
                cname_len = at;
            }
        }
        /* type-changing shadow: the registration is DEFERRED until the
         * initializer is emitted — Rust scoping has the new binding not
         * yet live inside its own initializer, so every source reference
         * there resolves to the prior binding's C spelling. The
         * declaration (and later references) use the fresh C name. */
        if cname_len > 0 {
            if init.is_null() {
                self.indent();
                unsafe {
                    self.emit_declarator(ct, ct_len, cname, cname_len);
                }
                self.out.puts(b";\n\0".as_ptr());
                unsafe {
                    (*locals).add_renamed(name, name_len, cname, cname_len, ct, ct_len, self.depth);
                }
                return;
            }
            let ik = unsafe { (*init).kind };
            if ik == pm_jit_rsx_ast_kind::METHOD_CALL {
                let ikids = unsafe { (*init).kids };
                let inkn = unsafe { (*init).n_kids } as usize;
                if inkn >= 2 {
                    let mn = unsafe { *ikids.add(1) };
                    if unsafe { (*mn).text_len } == 4
                        && unsafe { z_eq(unsafe { (*mn).text }, 4, b"lock\0".as_ptr()) }
                    {
                        unsafe {
                            self.err(
                                b"unsupported: type-changing shadow of a lock guard\0".as_ptr(),
                                line,
                            );
                        }
                        return;
                    }
                }
            }
            if ik == pm_jit_rsx_ast_kind::IF || ik == pm_jit_rsx_ast_kind::MATCH {
                self.indent();
                unsafe {
                    self.emit_declarator(ct, ct_len, cname, cname_len);
                }
                self.out.puts(b";\n\0".as_ptr());
                /* cur_ret carries the let's own type into the branch
                 * tails — bare `Some(x)`/`None` (no ascription) mint
                 * the row from it, not the fn's return type. Save the
                 * BYTES too: restoring only the length leaves the row
                 * name corrupted whenever the let's type is shorter
                 * than the enclosing return type. */
                let saved_ret: [u8; 128] = self.cur_ret;
                let saved_ret_len = self.cur_ret_len;
                {
                    let mut j = 0usize;
                    while j < ct_len && j < 127 {
                        self.cur_ret[j] = unsafe { *ct.add(j) };
                        j += 1;
                    }
                    self.cur_ret_len = ct_len;
                }
                if ik == pm_jit_rsx_ast_kind::IF {
                    unsafe { self.emit_if_value(init, locals, cname, cname_len) };
                } else {
                    unsafe { self.emit_match_value(init, locals, cname, cname_len) };
                }
                self.cur_ret = saved_ret;
                self.cur_ret_len = saved_ret_len;
                unsafe {
                    (*locals).add_renamed(name, name_len, cname, cname_len, ct, ct_len, self.depth);
                }
                return;
            }
            if ik == pm_jit_rsx_ast_kind::BLOCK && (unsafe { (*init).n_kids } as usize) >= 2 {
                self.indent();
                unsafe {
                    self.emit_declarator(ct, ct_len, cname, cname_len);
                }
                self.out.puts(b";\n\0".as_ptr());
                unsafe { self.emit_block_value(init, locals, cname, cname_len) };
                unsafe {
                    (*locals).add_renamed(name, name_len, cname, cname_len, ct, ct_len, self.depth);
                }
                return;
            }
            /* fixed array: C arrays are not assignable — declare, then
             * memcpy the initializer's bytes (`uint8_t a[128] = b;` is a
             * gcc extension, never valid C; TCC refuses it). An ARRAY
             * literal init keeps the brace-list render. */
            if unsafe { self.ctype_is_fixed_array(ct, ct_len) }
                && unsafe { (*init).kind } != pm_jit_rsx_ast_kind::ARRAY
            {
                self.indent();
                unsafe {
                    self.emit_declarator(ct, ct_len, cname, cname_len);
                }
                self.out.puts(b";\n\0".as_ptr());
                self.indent();
                self.out.puts(b"memcpy(\0".as_ptr());
                self.out.putc(b'&');
                self.out.put(cname, cname_len);
                self.out.puts(b", \0".as_ptr());
                self.out.putc(b'&');
                unsafe { self.emit_expr(init, locals) };
                self.out.puts(b", sizeof(\0".as_ptr());
                self.out.put(cname, cname_len);
                self.out.puts(b"));\n\0".as_ptr());
                unsafe {
                    (*locals).add_renamed(name, name_len, cname, cname_len, ct, ct_len, self.depth);
                }
                return;
            }
            self.indent();
            unsafe {
                self.emit_declarator(ct, ct_len, cname, cname_len);
            }
            self.out.puts(b" = \0".as_ptr());
            unsafe { self.emit_expr(init, locals) };
            self.out.puts(b";\n\0".as_ptr());
            unsafe {
                (*locals).add_renamed(name, name_len, cname, cname_len, ct, ct_len, self.depth);
            }
            return;
        }
        unsafe {
            (*locals).add(name, name_len, ct, ct_len, self.depth);
        }
        if reuse {
            self.indent();
            self.out.put(name, name_len);
            self.out.puts(b" = \0".as_ptr());
            if !init.is_null() {
                unsafe { self.emit_expr(init, locals) };
            }
            self.out.puts(b";\n\0".as_ptr());
            return;
        }
        /* Lock guard: `let g = M.lock();` — the acquire runs as a
         * statement here, the binding is the payload address, and the
         * scope-exit release rides the LocalTab guard note (end_block
         * emits it at the closing brace). The receiver must be a lock
         * row (the method name alone is not validation — the recv's
         * rendered C type gates it). */
        if !init.is_null() && unsafe { (*init).kind } == pm_jit_rsx_ast_kind::METHOD_CALL {
            let ikids = unsafe { (*init).kids };
            let icn = unsafe { (*init).n_kids } as usize;
            if icn >= 2 {
                let recv = unsafe { *ikids.add(0) };
                let mnode = unsafe { *ikids.add(1) };
                let mt = unsafe { (*mnode).text };
                let ml = unsafe { (*mnode).text_len };
                let rbuf = self.arena_tmp();
                let rl = unsafe { self.expr_ctype(recv, rbuf, 128, locals) };
                if ml == 4
                    && unsafe { z_eq(mt, ml, b"lock\0".as_ptr()) }
                    && rl > 9
                    && rl < 128
                    && unsafe { z_eq(rbuf, 9, b"rsx_lock_\0".as_ptr()) }
                {
                        /* render the receiver's lvalue text into a
                         * scratch Out (self.out is the C stream):
                         * field-wise swap, Out is not Copy. */
                        let saved_arena = self.out.arena;
                        let saved_p = self.out.p;
                        let saved_len = self.out.len;
                        let saved_cap = self.out.cap;
                        let saved_ok = self.out.ok;
                        self.out.arena = self.arena;
                        self.out.p = core::ptr::null_mut();
                        self.out.len = 0;
                        self.out.cap = 0;
                        self.out.ok = true;
                        unsafe { self.emit_expr(recv, locals) };
                        let mut rtext: *const u8 = core::ptr::null();
                        let mut rlen = 0usize;
                        if self.out.ok && self.out.len > 0 && self.out.len < 4096 {
                            rtext = self.out.p;
                            rlen = self.out.len;
                        }
                        self.out.arena = saved_arena;
                        self.out.p = saved_p;
                        self.out.len = saved_len;
                        self.out.cap = saved_cap;
                        self.out.ok = saved_ok;
                        if !rtext.is_null() {
                            /* acquire statement */
                            self.indent();
                            self.out.puts(b"pm_util_lock_acquire(&\0".as_ptr());
                            self.out.put(rtext, rlen);
                            self.out.puts(b".raw);\n\0".as_ptr());
                            /* bind: T *g = &recv.value; */
                            self.indent();
                            unsafe {
                                self.emit_declarator(ct, ct_len, name, name_len);
                            }
                            self.out.puts(b" = &\0".as_ptr());
                            self.out.put(rtext, rlen);
                            self.out.puts(b".value;\n\0".as_ptr());
                            /* guard note for the scope-exit release */
                            unsafe {
                                (*locals).mark_guard(rtext, rlen);
                            }
                            return;
                        }
                    }
                }
            }
        /* value-position init: if/match need the declare-then-assign shape. */
        if !init.is_null() {
            let ik = unsafe { (*init).kind };
            let valuey = ik == pm_jit_rsx_ast_kind::IF || ik == pm_jit_rsx_ast_kind::MATCH;
            if valuey {
                self.indent();
                unsafe {
                    self.emit_declarator(ct, ct_len, name, name_len);
                }
                self.out.puts(b";\n\0".as_ptr());
                /* cur_ret carries the let's own type into the branch
                 * tails — bare `Some(x)`/`None` (no ascription) mint
                 * the row from it, not the fn's return type. Save the
                 * BYTES too: restoring only the length leaves the row
                 * name corrupted whenever the let's type is shorter
                 * than the enclosing return type. */
                let saved_ret: [u8; 128] = self.cur_ret;
                let saved_ret_len = self.cur_ret_len;
                {
                    let mut j = 0usize;
                    while j < ct_len && j < 127 {
                        self.cur_ret[j] = unsafe { *ct.add(j) };
                        j += 1;
                    }
                    self.cur_ret_len = ct_len;
                }
                if ik == pm_jit_rsx_ast_kind::IF {
                    unsafe { self.emit_if_value(init, locals, name, name_len) };
                } else {
                    unsafe { self.emit_match_value(init, locals, name, name_len) };
                }
                self.cur_ret = saved_ret;
                self.cur_ret_len = saved_ret_len;
                return;
            }
            if ik == pm_jit_rsx_ast_kind::CLOSURE {
                unsafe {
                    self.err(b"unsupported: closure in let initializer\0".as_ptr(), line);
                }
                return;
            }
            /* Block initializer with statements: the statements are the
             * point (a guard binding ahead of a tail match — the lock-
             * plane pattern). Declaring the name first, then running the
             * block with the name as the value temp, keeps every
             * statement and the tail's assignment in one C scope. A bare
             * tail-only block keeps the plain `= { tail }` render. */
            if ik == pm_jit_rsx_ast_kind::BLOCK {
                let ikids = unsafe { (*init).kids };
                let inkn = unsafe { (*init).n_kids } as usize;
                if inkn >= 2 {
                    self.indent();
                    unsafe {
                        self.emit_declarator(ct, ct_len, name, name_len);
                    }
                    self.out.puts(b";\n\0".as_ptr());
                    unsafe { self.emit_block_value(init, locals, name, name_len) };
                    return;
                }
            }
        }
        self.indent();
        unsafe {
            self.emit_declarator(ct, ct_len, name, name_len);
        }
        if !init.is_null() {
            /* fixed array: `uint8_t a[128] = b;` is a gcc extension
             * (array copy) — valid C needs memcpy. TCC refuses the
             * extension and the self-host prove compiles through TCC.
             * An ARRAY literal init keeps the brace-list render ({0,..}
             * is the one valid C array initializer). */
            if unsafe { self.ctype_is_fixed_array(ct, ct_len) }
                && unsafe { (*init).kind } != pm_jit_rsx_ast_kind::ARRAY
            {
                self.out.puts(b";\n\0".as_ptr());
                self.indent();
                self.out.puts(b"memcpy(&\0".as_ptr());
                self.out.put(name, name_len);
                self.out.puts(b", &\0".as_ptr());
                unsafe { self.emit_expr(init, locals) };
                self.out.puts(b", sizeof(\0".as_ptr());
                self.out.put(name, name_len);
                self.out.puts(b"));\n\0".as_ptr());
                return;
            }
            self.out.puts(b" = \0".as_ptr());
            /* &str view from a literal: `rsx_str_ref_t s = "x";` is not C —
             * wrap the bytes in the compound literal {p,n} (the same
             * coercion the return-position str path emits). */
            if ct_len == 13
                && unsafe { z_eq(ct, 13, b"rsx_str_ref_t\0".as_ptr()) }
                && unsafe { (*init).kind } == pm_jit_rsx_ast_kind::LITERAL
                && !unsafe { (*init).text }.is_null()
                && unsafe { *(*init).text } == b'"'
            {
                let lt = unsafe { (*init).text };
                let ltl = unsafe { (*init).text_len };
                let inner = if ltl >= 2 { ltl - 2 } else { 0 };
                self.str_ref_used = true;
                self.out.puts(b"(rsx_str_ref_t){ (const uint8_t *)\0".as_ptr());
                self.out.put(lt, ltl);
                self.out.puts(b", \0".as_ptr());
                self.out.put_u32(inner as u32);
                self.out.puts(b" }\0".as_ptr());
            } else {
                unsafe { self.emit_expr(init, locals) };
            }
        }
        self.out.puts(b";\n\0".as_ptr());
    }

    unsafe fn emit_if(&mut self, s: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        /* kids: cond, then-block, [else] */
        let kids = unsafe { (*s).kids };
        let nk = unsafe { (*s).n_kids } as usize;
        if nk < 2 {
            unsafe {
                self.err(b"bad if statement\0".as_ptr(), unsafe { (*s).line });
            }
            return;
        }
        let cond = unsafe { *kids.add(0) };
        let then_b = unsafe { *kids.add(1) };
        self.indent();
        self.out.puts(b"if (\0".as_ptr());
        unsafe { self.emit_expr(cond, locals) };
        self.out.puts(b") {\n\0".as_ptr());
        self.depth += 1;
        unsafe { self.emit_block_stmt(then_b, locals) };
        self.depth -= 1;
        if nk >= 3 {
            let els = unsafe { *kids.add(2) };
            self.indent();
            self.out.puts(b"} else \0".as_ptr());
            let ek = unsafe { (*els).kind };
            if ek == pm_jit_rsx_ast_kind::IF {
                /* `else if` — inline without braces */
                let ekids = unsafe { (*els).kids };
                let enk = unsafe { (*els).n_kids } as usize;
                if enk >= 2 {
                    self.out.puts(b"if (\0".as_ptr());
                    unsafe { self.emit_expr(*ekids.add(0), locals) };
                    self.out.puts(b") {\n\0".as_ptr());
                    self.depth += 1;
                    unsafe { self.emit_block_stmt(*ekids.add(1), locals) };
                    self.depth -= 1;
                    self.indent();
                    self.out.puts(b"}\n\0".as_ptr());
                    /* a trailing else-arm: chain (another `else if`) recurses,
                     * a plain block emits inline. */
                    if enk >= 3 {
                        let e3 = unsafe { *ekids.add(2) };
                        let e3k = unsafe { (*e3).kind };
                        self.indent();
                        if e3k == pm_jit_rsx_ast_kind::IF {
                            self.out.puts(b"else \0".as_ptr());
                            self.depth += 1;
                            unsafe { self.emit_if(e3, locals) };
                            self.depth -= 1;
                        } else {
                            self.out.puts(b"else {\n\0".as_ptr());
                            self.depth += 1;
                            unsafe { self.emit_block_stmt(e3, locals) };
                            self.depth -= 1;
                            self.indent();
                            self.out.puts(b"}\n\0".as_ptr());
                        }
                    }
                    return;
                }
            }
            if ek == pm_jit_rsx_ast_kind::BLOCK {
                self.out.puts(b"{\n\0".as_ptr());
                self.depth += 1;
                unsafe { self.emit_block_stmt(els, locals) };
                self.depth -= 1;
                self.indent();
                self.out.puts(b"}\n\0".as_ptr());
                return;
            }
            unsafe {
                self.err(b"unsupported: else arm\0".as_ptr(), unsafe { (*els).line });
            }
            return;
        }
        self.indent();
        self.out.puts(b"}\n\0".as_ptr());
    }

    /* if as a fn-body tail: branch tails become `return ..;`. */
    unsafe fn emit_if_tail(&mut self, s: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        let kids = unsafe { (*s).kids };
        let nk = unsafe { (*s).n_kids } as usize;
        if nk < 2 {
            unsafe {
                self.err(b"bad if statement\0".as_ptr(), unsafe { (*s).line });
            }
            return;
        }
        let cond = unsafe { *kids.add(0) };
        let then_b = unsafe { *kids.add(1) };
        self.indent();
        self.out.puts(b"if (\0".as_ptr());
        unsafe { self.emit_expr(cond, locals) };
        self.out.puts(b") {\n\0".as_ptr());
        self.depth += 1;
        /* Each branch is its own C scope (the braces) — mark it in the
         * locals table too, so a `let` in one branch never reuses the
         * sibling branch's declaration (same name+type across branches
         * is shadowing, not reuse: the if's decl is invisible in else). */
        unsafe { (*locals).note_scope() };
        unsafe { self.emit_block_tail_ret(then_b, locals) };
        unsafe { (*locals).drop_scope() };
        self.depth -= 1;
        if nk >= 3 {
            let els = unsafe { *kids.add(2) };
            let ek = unsafe { (*els).kind };
            self.indent();
            self.out.puts(b"} else \0".as_ptr());
            if ek == pm_jit_rsx_ast_kind::IF {
                self.depth += 1;
                unsafe { self.emit_if_tail(els, locals) };
                self.depth -= 1;
                return;
            }
            if ek == pm_jit_rsx_ast_kind::BLOCK {
                self.out.puts(b"{\n\0".as_ptr());
                self.depth += 1;
                unsafe { (*locals).note_scope() };
                unsafe { self.emit_block_tail_ret(els, locals) };
                unsafe { (*locals).drop_scope() };
                self.depth -= 1;
                self.indent();
                self.out.puts(b"}\n\0".as_ptr());
                return;
            }
        }
        self.indent();
        self.out.puts(b"}\n\0".as_ptr());
    }

    /* Block whose tail expr returns. */
    unsafe fn emit_block_tail_ret(&mut self, b: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        let kids = unsafe { (*b).kids };
        let nk = unsafe { (*b).n_kids } as usize;
        let mut i = 0usize;
        while i + 1 < nk {
            unsafe { self.emit_stmt(*kids.add(i), locals, 0) };
            i += 1;
        }
        if nk == 0 {
            return;
        }
        let st = unsafe { *kids.add(nk - 1) };
        let k = unsafe { (*st).kind };
        /* the parser stores a block's bare tail expr as a direct kid (no
         * EXPR_STMT wrapper) — any value-y kind returns. */
        let valuey = k == pm_jit_rsx_ast_kind::LITERAL
            || k == pm_jit_rsx_ast_kind::PATH
            || k == pm_jit_rsx_ast_kind::BINARY
            || k == pm_jit_rsx_ast_kind::UNARY
            || k == pm_jit_rsx_ast_kind::CALL
            || k == pm_jit_rsx_ast_kind::METHOD_CALL
            || k == pm_jit_rsx_ast_kind::FIELD
            || k == pm_jit_rsx_ast_kind::INDEX
            || k == pm_jit_rsx_ast_kind::CAST
            || k == pm_jit_rsx_ast_kind::STRUCT_LIT
            || k == pm_jit_rsx_ast_kind::TUPLE
            || k == pm_jit_rsx_ast_kind::CLOSURE
            || k == pm_jit_rsx_ast_kind::MACRO
            || k == pm_jit_rsx_ast_kind::BLOCK;
        if k == pm_jit_rsx_ast_kind::EXPR_STMT && unsafe { (*st).n_kids } >= 1 {
            let e = unsafe { *(*st).kids.add(0) };
            /* a wrapped control-flow tail (`return e;` parses as
             * EXPR_STMT(RETURN)) is a statement, not a value — emitting
             * `return <expr>` here would double the return (emit_expr on
             * a RETURN emits its own). */
            let ek = unsafe { (*e).kind };
            if ek == pm_jit_rsx_ast_kind::RETURN
                || ek == pm_jit_rsx_ast_kind::BREAK
                || ek == pm_jit_rsx_ast_kind::CONTINUE
            {
                unsafe { self.emit_stmt(st, locals, 0) };
                return;
            }
            self.indent();
            self.out.puts(b"return \0".as_ptr());
            unsafe { self.emit_ret_value(e, locals) };
            self.out.puts(b";\n\0".as_ptr());
            return;
        }
        if k == pm_jit_rsx_ast_kind::IF {
            unsafe { self.emit_if_tail(st, locals) };
            return;
        }
        if k == pm_jit_rsx_ast_kind::MATCH {
            /* nested tail match: arms store into a temp, then return it.
             * The temp's type is the ARMS' value type, not the scrutinee's
             * (`match index { 0 => f as *mut c_void, _ => null_mut() }`
             * joins pointers while the scrutinee is a usize — inferring
             * from the scrutinee emits `size_t __rsx_ret;` and the pointer
             * stores lose). First non-diverging arm decides (an arm that
             * `return`s contributes no value); falls back to the scrutinee
             * when every arm diverges (the temp is then unused). */
            let ct = self.arena_tmp();
            let mut ct_len = 0usize;
            {
                let mk = unsafe { (*st).kids };
                let mn = unsafe { (*st).n_kids } as usize;
                let mut a = 1usize;
                while a < mn {
                    let arm = unsafe { *mk.add(a) };
                    if unsafe { (*arm).kind } != pm_jit_rsx_ast_kind::MATCH_ARM {
                        a += 1;
                        continue;
                    }
                    let ak = unsafe { (*arm).kids };
                    let akn = unsafe { (*arm).n_kids } as usize;
                    if akn < 2 {
                        a += 1;
                        continue;
                    }
                    let body = unsafe { *ak.add(akn - 1) };
                    let bk = unsafe { (*body).kind };
                    if bk == pm_jit_rsx_ast_kind::RETURN
                        || bk == pm_jit_rsx_ast_kind::BREAK
                        || bk == pm_jit_rsx_ast_kind::CONTINUE
                    {
                        a += 1;
                        continue;
                    }
                    /* a BLOCK arm body carries its value in the tail expr
                     * (`0 => { 1 }`, or a let-chain fold's then-block):
                     * unwrap so the type comes from the value, not from a
                     * scrutinee fallback. EXPR_STMT wraps a statement
                     * block's tail (`{ return e; }`). */
                    let mut probe = body;
                    if bk == pm_jit_rsx_ast_kind::BLOCK {
                        let bkd = unsafe { (*body).kids };
                        let bkn = unsafe { (*body).n_kids } as usize;
                        if bkn == 0 {
                            a += 1;
                            continue;
                        }
                        probe = unsafe { *bkd.add(bkn - 1) };
                        if unsafe { (*probe).kind } == pm_jit_rsx_ast_kind::EXPR_STMT
                            && unsafe { (*probe).n_kids } >= 1
                        {
                            probe = unsafe { *(*probe).kids.add(0) };
                        }
                    }
                    let n = unsafe { self.expr_ctype(probe, ct, 128, locals) };
                    if n > 0 {
                        ct_len = n;
                        break;
                    }
                    /* this arm yielded no type (e.g. its value rides in a
                     * pattern-bound local invisible to expr_ctype) — the
                     * next non-diverging arm decides; the wildcard arm of a
                     * let-chain fold always carries an inferable body. */
                    a += 1;
                }
            }
            if ct_len == 0 {
                ct_len = unsafe {
                    self.expr_ctype(unsafe { *(*st).kids.add(0) }, ct, 128, locals)
                };
            }
            if ct_len > 0 {
                self.indent();
                self.out.put(ct, ct_len);
                self.out.puts(b" __rsx_ret;\n\0".as_ptr());
                unsafe { self.emit_match_value(st, locals, b"__rsx_ret\0".as_ptr(), 9) };
                self.indent();
                self.out.puts(b"return __rsx_ret;\n\0".as_ptr());
            }
            return;
        }
        if valuey && k != pm_jit_rsx_ast_kind::BLOCK {
            self.indent();
            self.out.puts(b"return \0".as_ptr());
            unsafe { self.emit_ret_value(st, locals) };
            self.out.puts(b";\n\0".as_ptr());
            return;
        }
        if k == pm_jit_rsx_ast_kind::BLOCK || k == pm_jit_rsx_ast_kind::IF {
            unsafe { self.emit_block_tail_ret(st, locals) };
            return;
        }
        if k == pm_jit_rsx_ast_kind::RETURN {
            unsafe { self.emit_stmt(st, locals, 0) };
            return;
        }
        unsafe { self.emit_stmt(st, locals, 0) };
    }

    /* if with a value: every branch stores into temp. */
    unsafe fn emit_if_value(&mut self, s: *const pm_jit_rsx_ast_t, locals: *mut LocalTab, temp: *const u8, temp_len: usize) {
        let kids = unsafe { (*s).kids };
        let nk = unsafe { (*s).n_kids } as usize;
        if nk < 2 {
            return;
        }
        let cond = unsafe { *kids.add(0) };
        let then_b = unsafe { *kids.add(1) };
        self.indent();
        self.out.puts(b"if (\0".as_ptr());
        unsafe { self.emit_expr(cond, locals) };
        self.out.puts(b") {\n\0".as_ptr());
        self.depth += 1;
        unsafe { self.emit_block_value(then_b, locals, temp, temp_len) };
        self.depth -= 1;
        if nk >= 3 {
            let els = unsafe { *kids.add(2) };
            self.indent();
            self.out.puts(b"} else \0".as_ptr());
            let ek = unsafe { (*els).kind };
            if ek == pm_jit_rsx_ast_kind::IF {
                unsafe { self.emit_if_value(els, locals, temp, temp_len) };
                return;
            }
            if ek == pm_jit_rsx_ast_kind::BLOCK {
                self.out.puts(b"{\n\0".as_ptr());
                self.depth += 1;
                unsafe { self.emit_block_value(els, locals, temp, temp_len) };
                self.depth -= 1;
                self.indent();
                self.out.puts(b"}\n\0".as_ptr());
                return;
            }
        }
        if temp.is_null() {
            /* value discarded: the if is a plain statement, no else needed */
            self.indent();
            self.out.puts(b"}\n\0".as_ptr());
            return;
        }
        unsafe {
            self.err(
                b"value-position if needs an else for every branch\0".as_ptr(),
                unsafe { (*s).line },
            );
        }
    }

    unsafe fn emit_match(&mut self, s: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        /* kids: scrutinee, arms... (MATCH_ARM: pat, body) */
        let kids = unsafe { (*s).kids };
        let nk = unsafe { (*s).n_kids } as usize;
        if nk < 1 {
            return;
        }
        let mut scrut = unsafe { *kids.add(0) };
        let temp = b"__rsx_m\0".as_ptr();
        let temp_len = 7usize;
        /* let-chain sugar: the if-let segment's scrutinee parsed with
         * cond_ctx=true, so `let P = X && guard` folded X && guard into
         * one BINARY. When the LHS types as an Option, the RHS is an arm
         * guard: unwrap it, AND it into every non-wildcard arm's test
         * (the Some-binds are declared before the test, so the guard
         * types against them). */
        let mut chain_guard: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
        if unsafe { (*scrut).kind } == pm_jit_rsx_ast_kind::BINARY
            && (unsafe { (*scrut).n_kids } as usize) >= 2
            && unsafe { z_eq(unsafe { (*scrut).text }, unsafe { (*scrut).text_len }, b"&&\0".as_ptr()) }
        {
            let sk = unsafe { (*scrut).kids };
            let lhs = unsafe { *sk.add(0) };
            let gb = self.arena_tmp();
            let saved_pre = self.pre_pool_at;
            let gl = unsafe { self.expr_ctype(lhs, gb, 128, locals) };
            self.pre_pool_at = saved_pre;
            if gl > 8 && gl < 128 && unsafe { z_eq(gb, 8, b"rsx_opt_\0".as_ptr()) } {
                chain_guard = unsafe { *sk.add(1) };
                scrut = lhs;
            }
        }
        let ct = self.arena_tmp();
        let ct_len = unsafe { self.expr_ctype(scrut, ct, 128, locals) };
        if ct_len == 0 {
            unsafe {
                self.err(b"cannot infer match scrutinee type\0".as_ptr(), unsafe { (*s).line });
            }
            return;
        }
        /* stash the scrutinee type for emit_pat_test — string-literal
         * arms compare the view (len + memcmp), not a scalar == */
        unsafe {
            core::ptr::copy_nonoverlapping(ct, self.cur_scrut.as_mut_ptr(), ct_len);
        }
        self.cur_scrut_len = ct_len;
        /* struct-shaped Option (integer payload) arms test ._has, not
         * pointer nullity — stash the payload type for emit_pat_test.
         * Named typedef (rsx_opt_<elem>) or legacy inline struct. */
        {
            let mut eln = unsafe {
                Lower::opt_typedef_elem(ct, ct_len, self.cur_opt_elem.as_mut_ptr(), 96)
            };
            if eln == 0 {
                eln = unsafe { Lower::opt_struct_elem(ct, ct_len) };
                if eln > 0 {
                    unsafe {
                        core::ptr::copy_nonoverlapping(ct.add(9), self.cur_opt_elem.as_mut_ptr(), eln);
                    }
                }
            }
            if eln > 96 {
                unsafe {
                    self.err(b"internal: option payload type too long\0".as_ptr(), unsafe { (*s).line });
                }
                return;
            }
            self.cur_opt_elem_len = eln;
        }
        /* Result scrutinee: decode both payload spellings for the Ok/Err
         * arm binds (._v / ._e). */
        {
            let on = unsafe {
                Lower::res_typedef_elem(
                    ct,
                    ct_len,
                    self.cur_res_ok.as_mut_ptr(),
                    96,
                    self.cur_res_err.as_mut_ptr(),
                    96,
                )
            };
            self.cur_res_ok_len = on;
            if on > 0 {
                let mut el2 = 0usize;
                while el2 < 96 && unsafe { *self.cur_res_err.as_ptr().add(el2) } != 0 {
                    el2 += 1;
                }
                self.cur_res_err_len = el2;
            } else {
                self.cur_res_err_len = 0;
            }
        }
        /* the scrutinee's row may have interned past the preamble flush
         * (a find() over a mid-body local registers its Option row when
         * the scrutinee types, after the pre-scan window) — flush pending
         * typedefs before the temp decl names them (mid-fn typedefs are C;
         * the let-else path does the same). */
        unsafe { self.fnp_emit_rest() };
        unsafe { self.opt_emit_rest(0) };
        unsafe { self.res_emit_rest() };
        self.indent();
        self.out.put(ct, ct_len);
        self.out.putc(b' ');
        self.out.put(temp, temp_len);
        self.out.puts(b" = \0".as_ptr());
        unsafe { self.emit_expr(scrut, locals) };
        self.out.puts(b";\n\0".as_ptr());
        /* pre-declare guarded Some(bind) arms' binds BEFORE the chain: a
         * guarded arm's guard types against the bind, but emitting the
         * decl mid-chain (`else T b = ...; if`) is not C. One decl per
         * arm (binds of distinct arms never clash — a match binds at
         * most one arm; same-named binds across arms are shadowing the
         * source level, one C decl serves both). */
        {
            let mut pi = 1usize;
            while pi < nk {
                let parm = unsafe { *kids.add(pi) };
                let pak = unsafe { (*parm).kids };
                let pank = unsafe { (*parm).n_kids } as usize;
                /* guarded arms (explicit guard) always hoist; a let-chain
                 * arm (2 kids + chain_guard) hoists too — its guard types
                 * against the bind the same way. */
                if pank >= 3 || (!chain_guard.is_null() && pank >= 2) {
                    let ppat = unsafe { *pak.add(0) };
                    if unsafe { (*ppat).kind } == pm_jit_rsx_ast_kind::PATH {
                        let ppk = unsafe { (*ppat).kids };
                        let ppnk = unsafe { (*ppat).n_kids } as usize;
                        if ppnk >= 2 {
                            let phead = unsafe { *ppk.add(0) };
                            let pbind = unsafe { *ppk.add(1) };
                            if unsafe { (*phead).kind } == pm_jit_rsx_ast_kind::PATH
                                && unsafe { z_eq(unsafe { (*phead).text }, unsafe { (*phead).text_len }, b"Some\0".as_ptr()) }
                                && (unsafe { (*pbind).kind } == pm_jit_rsx_ast_kind::PATH
                                    || unsafe { (*pbind).kind } == pm_jit_rsx_ast_kind::TUPLE)
                            {
                                /* dedup (PATH binds only): a same-named bind
                                 * hoisted by an earlier arm is already
                                 * declared — a second C decl would clash. */
                                let mut already = false;
                                if unsafe { (*pbind).kind } == pm_jit_rsx_ast_kind::PATH {
                                    let bleaf = unsafe { *(*pbind).kids.add(0) };
                                    let ob = self.arena_tmp();
                                    already = unsafe {
                                        (*locals).lookup(unsafe { (*bleaf).text }, unsafe { (*bleaf).text_len }, ob) > 0
                                    };
                                }
                                if !already {
                                    unsafe { self.emit_some_binds(pbind, temp, temp_len, ct, ct_len, locals) };
                                }
                            }
                        }
                        /* a guarded PLAIN bind arm (`s if s.starts_with(..)
                         * => ..`) on a value-shaped scrutinee: the guard
                         * types against the bind, so it must be declared
                         * before the chain — same hoist, the value-copy
                         * shape (T b = temp). A bare bind parses as the
                         * "path" wrapper over one segment (or a bare
                         * single-segment PATH); unwrap both. A unit-static
                         * name is a const pattern, not a bind; scalars
                         * stay bindless (not in the subset). */
                        {
                            let mut pbind2 = ppat;
                            if ppnk == 1
                                && unsafe { z_eq(unsafe { (*ppat).text }, unsafe { (*ppat).text_len }, b"path\0".as_ptr()) }
                            {
                                pbind2 = unsafe { *(*ppat).kids.add(0) };
                            }
                            let pb2nk = unsafe { (*pbind2).n_kids } as usize;
                            if pb2nk == 0 {
                                let ptl = unsafe { (*pbind2).text_len };
                                let pt = unsafe { (*pbind2).text };
                                let is_fat = ct_len == 13
                                    && unsafe { z_eq(ct, 13, b"rsx_str_ref_t\0".as_ptr()) };
                                let is_arr = ct_len > 8
                                    && ct_len < 128
                                    && unsafe { z_eq(ct, 8, b"rsx_arr_\0".as_ptr()) };
                                let is_vec = ct_len > 8
                                    && ct_len < 128
                                    && unsafe { z_eq(ct, 8, b"rsx_vec_\0".as_ptr()) };
                                let is_own = ct_len == 9
                                    && unsafe { z_eq(ct, 9, b"rsx_str_t\0".as_ptr()) };
                                let is_unit_static = {
                                    let sb = self.arena_tmp();
                                    let sl = unsafe { self.st_find(pt, ptl, sb, 128) };
                                    sl > 0
                                };
                                if ptl > 0
                                    && !pt.is_null()
                                    && !is_unit_static
                                    && !(ptl == 1 && unsafe { z_eq(pt, 1, b"_\0".as_ptr()) })
                                    && (is_fat || is_arr || is_vec || is_own)
                                {
                                    let ob2 = self.arena_tmp();
                                    let already2 = unsafe { (*locals).lookup(pt, ptl, ob2) > 0 };
                                    if !already2 {
                                        self.indent();
                                        self.out.put(ct, ct_len);
                                        self.out.putc(b' ');
                                        self.out.put(pt, ptl);
                                        self.out.puts(b" = \0".as_ptr());
                                        self.out.put(temp, temp_len);
                                        self.out.puts(b";\n\0".as_ptr());
                                        unsafe {
                                            (*locals).add(pt, ptl, ct, ct_len, self.depth + 1);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                pi += 1;
            }
        }
        let mut i = 1usize;
        let mut first = true;
        while i < nk {
            let arm = unsafe { *kids.add(i) };
            let ak = unsafe { (*arm).kids };
            let ank = unsafe { (*arm).n_kids } as usize;
            if ank < 2 {
                i += 1;
                continue;
            }
            let pat = unsafe { *ak.add(0) };
            /* arm kids: [pat, body] or [pat, guard, body] */
            let has_guard = ank >= 3;
            let body = unsafe { *ak.add(ank - 1) };
            /* true when this arm's Some(bind) was pre-declared before
             * the chain (a guarded Some arm): the chain path emits test +
             * body, the bind is already in scope. */
            let mut hoisted_bind = false;
            self.indent();
            if !first {
                self.out.puts(b"else \0".as_ptr());
            }
            first = false;
            /* wildcard arm: unconditional else (unless a guard narrows it) */
            let is_wc = unsafe { (*pat).kind } == pm_jit_rsx_ast_kind::PATH
                && unsafe { z_eq(unsafe { (*pat).text }, unsafe { (*pat).text_len }, b"_\0".as_ptr()) };
            /* Some(bind) with a guarded arm (explicit guard or a let-chain
             * tail): the guard types against the bind, so the bind decl
             * must precede the `if` test. Emitting it in the enclosing
             * block breaks source shadowing (`let runner = ...; if let
             * Some(runner) = runner` redeclares), so the whole arm gets
             * its own C brace scope: binds + if + body inside it. */
            let guarded = has_guard || !chain_guard.is_null();
            let mut some_bind: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
            /* Result twin: the Ok(x)/Err(x) bind — res_is_ok tells which
             * payload field the bind copies (._v / ._e). */
            let mut res_bind: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
            let mut res_is_ok = false;
            /* Tagged-enum twin: `E::V(bind)` — the variant carries a
             * payload; the bind copies `temp._u.<Variant>`. */
            let mut enum_bind: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
            let mut enum_pay: [u8; 96] = [0; 96];
            let mut enum_pay_len: usize = 0;
            let mut enum_vname: [u8; 64] = [0; 64];
            let mut enum_vname_len: usize = 0;
            if unsafe { (*pat).kind } == pm_jit_rsx_ast_kind::PATH {
                let pk = unsafe { (*pat).kids };
                let pnk = unsafe { (*pat).n_kids } as usize;
                if pnk >= 2 {
                    let head = unsafe { *pk.add(0) };
                    let bind = unsafe { *pk.add(1) };
                    if unsafe { (*head).kind } == pm_jit_rsx_ast_kind::PATH {
                        let ht = unsafe { (*head).text };
                        let hl = unsafe { (*head).text_len };
                        if unsafe { z_eq(ht, hl, b"Some\0".as_ptr()) }
                            && (unsafe { (*bind).kind } == pm_jit_rsx_ast_kind::PATH
                                || unsafe { (*bind).kind } == pm_jit_rsx_ast_kind::TUPLE)
                        {
                            some_bind = bind;
                            if guarded
                                && (unsafe { (*bind).kind } == pm_jit_rsx_ast_kind::PATH
                                    || unsafe { (*bind).kind } == pm_jit_rsx_ast_kind::TUPLE)
                            {
                                hoisted_bind = true;
                            }
                            /* let-chain arms are `guarded` by chain_guard —
                             * same hoist, same flag (the bind was already
                             * declared before the chain). */
                        } else if (unsafe { z_eq(ht, hl, b"Ok\0".as_ptr()) }
                            || unsafe { z_eq(ht, hl, b"Err\0".as_ptr()) })
                            && unsafe { (*bind).kind } == pm_jit_rsx_ast_kind::PATH
                            && self.cur_res_ok_len > 0
                        {
                            res_bind = bind;
                            res_is_ok = unsafe { z_eq(ht, hl, b"Ok\0".as_ptr()) };
                        }
                    }
                }
                /* tagged-enum `E::V(bind)`: 3+ kids where the first two
                 * join to an enumpays row. The bind is the LAST kid. */
                if enum_bind.is_null() && pnk >= 3 {
                    let mut joined = self.arena_tmp();
                    let mut jat = 0usize;
                    let mut ok_join = true;
                    let mut ji = 0usize;
                    while ji + 1 < pnk {
                        let seg = unsafe { *pk.add(ji) };
                        if unsafe { (*seg).kind } != pm_jit_rsx_ast_kind::PATH {
                            ok_join = false;
                            break;
                        }
                        if jat > 0 {
                            jat = unsafe { bput(joined, 128, jat, b"_\0".as_ptr(), 1) };
                        }
                        jat = unsafe { bput(joined, 128, jat, unsafe { (*seg).text }, unsafe { (*seg).text_len }) };
                        ji += 1;
                    }
                    if ok_join && jat > 0 {
                        unsafe {
                            *joined.add(jat) = 0;
                        }
                        let pvbuf = self.arena_tmp();
                        let plen = unsafe { self.enumpays.lookup(joined, jat, pvbuf, 96) };
                        if plen > 0 {
                            /* the variant leaf text (2nd-to-last kid) */
                            let vleaf = unsafe { *pk.add(pnk - 2) };
                            let vt2 = unsafe { (*vleaf).text };
                            let vl2 = unsafe { (*vleaf).text_len };
                            if vl2 > 0 && vl2 < 64 && !vt2.is_null() {
                                let mut c2 = 0usize;
                                while c2 < vl2 {
                                    enum_vname[c2] = unsafe { *vt2.add(c2) };
                                    c2 += 1;
                                }
                                enum_vname_len = vl2;
                                let mut c3 = 0usize;
                                while c3 < plen {
                                    enum_pay[c3] = unsafe { *pvbuf.add(c3) };
                                    c3 += 1;
                                }
                                enum_pay_len = plen;
                                enum_bind = unsafe { *pk.add(pnk - 1) };
                            }
                        }
                    }
                }
            }
            if guarded && !res_bind.is_null() {
                /* scoped Result arm: { T b = temp._v; if (temp._ok && g) { body } } */
                self.depth += 1;
                unsafe { (*locals).note_scope() };
                unsafe { self.emit_res_binds(res_bind, res_is_ok, temp, temp_len, locals) };
                self.indent();
                if !is_wc {
                    self.out.puts(b"if (\0".as_ptr());
                    unsafe { self.emit_pat_test(pat, temp, temp_len, locals) };
                    if has_guard {
                        self.out.puts(b" && (\0".as_ptr());
                        unsafe { self.emit_expr(*ak.add(1), locals) };
                        self.out.puts(b")\0".as_ptr());
                    } else if !chain_guard.is_null() {
                        self.out.puts(b" && (\0".as_ptr());
                        unsafe { self.emit_expr(chain_guard, locals) };
                        self.out.puts(b")\0".as_ptr());
                    }
                    self.out.puts(b") {\n\0".as_ptr());
                } else {
                    self.out.puts(b"{\n\0".as_ptr());
                }
                self.depth += 1;
                unsafe { (*locals).note_scope() };
                unsafe { self.emit_block_stmt(body, locals) };
                unsafe { (*locals).drop_scope() };
                self.depth -= 1;
                self.indent();
                self.out.puts(b"}\n\0".as_ptr());
                unsafe { (*locals).drop_scope() };
                self.depth -= 1;
                i += 1;
                continue;
            }
            /* guarded Some(bind) arms had their binds pre-declared above
             * (hoisted_bind == true): fall through to the normal chain
             * path — the test + body emit there, the bind is already in
             * scope, and the else-chain stays one C statement. */
            let eff_guard: *const pm_jit_rsx_ast_t = if has_guard {
                unsafe { *ak.add(1) }
            } else {
                chain_guard
            };
            let _ = eff_guard;
            if !is_wc || has_guard {
                self.out.puts(b"if (\0".as_ptr());
                if !is_wc {
                    unsafe { self.emit_pat_test(pat, temp, temp_len, locals) };
                }
                if has_guard {
                    if !is_wc {
                        self.out.puts(b" && \0".as_ptr());
                    }
                    self.out.puts(b"(\0".as_ptr());
                    unsafe { self.emit_expr(*ak.add(1), locals) };
                    self.out.puts(b")\0".as_ptr());
                } else if !chain_guard.is_null() && !is_wc {
                    self.out.puts(b" && (\0".as_ptr());
                    unsafe { self.emit_expr(chain_guard, locals) };
                    self.out.puts(b")\0".as_ptr());
                }
                self.out.puts(b") \0".as_ptr());
            }
            self.out.puts(b"{\n\0".as_ptr());
            /* Some(bind) on an unguarded arm: declare the bind inside the
             * arm's braces (the C if scope) — shadowing stays legal.
             * Hoisted (guarded) arms skip: their bind was declared before
             * the chain. */
            if !some_bind.is_null() && !hoisted_bind {
                unsafe { (*locals).note_scope() };
                unsafe { self.emit_some_binds(some_bind, temp, temp_len, ct, ct_len, locals) };
            }
            /* Ok(bind)/Err(bind) unguarded: same arm-brace placement. */
            if !res_bind.is_null() {
                unsafe { (*locals).note_scope() };
                unsafe { self.emit_res_binds(res_bind, res_is_ok, temp, temp_len, locals) };
            }
            /* E::V(bind) unguarded: the tagged-union payload copy. */
            if !enum_bind.is_null() && enum_pay_len > 0 {
                unsafe { (*locals).note_scope() };
                let bleaf2 = unsafe { *(*enum_bind).kids.add(0) };
                let bt2 = unsafe { (*bleaf2).text };
                let btl2 = unsafe { (*bleaf2).text_len };
                if !(btl2 == 1 && unsafe { z_eq(bt2, 1, b"_\0".as_ptr()) }) {
                    self.indent();
                    self.out.put(enum_pay.as_ptr(), enum_pay_len);
                    self.out.putc(b' ');
                    self.out.put(bt2, btl2);
                    self.out.puts(b" = \0".as_ptr());
                    self.out.put(temp, temp_len);
                    self.out.puts(b"._u.\0".as_ptr());
                    self.out.put(enum_vname.as_ptr(), enum_vname_len);
                    self.out.puts(b";\n\0".as_ptr());
                    unsafe {
                        (*locals).add(bt2, btl2, enum_pay.as_ptr(), enum_pay_len, 1);
                    }
                }
            }
            self.depth += 1;
            unsafe { (*locals).note_scope() };
            /* plain binding arm (`other => ..`) on a value-shaped scrutinee
             * (&str, &[T], String, Vec rows): declare the bind as a copy of
             * the scrutinee temp so the arm body can use it — the pattern
             * test emitted `1` (matches anything), the bind is the value.
             * Only fat/value rows take this; scalar scrutinees keep the
             * bindless path (a scalar binding arm is not in the subset). */
            if unsafe { (*pat).kind } == pm_jit_rsx_ast_kind::PATH {
                /* unwrap the parser's "path" wrapper when present */
                let mut bnode = pat;
                if unsafe { (*pat).n_kids } as usize == 1
                    && unsafe { z_eq(unsafe { (*pat).text }, unsafe { (*pat).text_len }, b"path\0".as_ptr()) }
                {
                    bnode = unsafe { *(*pat).kids.add(0) };
                }
                let pk = unsafe { (*bnode).kids };
                let pnk = unsafe { (*bnode).n_kids } as usize;
                let pt = unsafe { (*bnode).text };
                let ptl = unsafe { (*bnode).text_len };
                let is_fat = ct_len == 13
                    && unsafe { z_eq(ct, 13, b"rsx_str_ref_t\0".as_ptr()) };
                let is_arr = ct_len > 8
                    && ct_len < 128
                    && unsafe { z_eq(ct, 8, b"rsx_arr_\0".as_ptr()) };
                let is_vec = ct_len > 8
                    && ct_len < 128
                    && unsafe { z_eq(ct, 8, b"rsx_vec_\0".as_ptr()) };
                let is_own = ct_len == 9 && unsafe { z_eq(ct, 9, b"rsx_str_t\0".as_ptr()) };
                /* a single-segment name that IS a unit static (a `pub
                 * const X: &str`) is a CONST PATTERN, not a binding —
                 * emit_pat_test compares against the value; declaring it
                 * here would shadow the C static with a local. */
                let is_unit_static = {
                    let sb = self.arena_tmp();
                    let sl = unsafe { self.st_find(pt, ptl, sb, 128) };
                    sl > 0
                };
                if pnk == 0
                    && ptl > 0
                    && !pt.is_null()
                    && !is_unit_static
                    && !(ptl == 1 && unsafe { z_eq(pt, 1, b"_\0".as_ptr()) })
                    && !(ptl == 4 && unsafe { z_eq(pt, 4, b"None\0".as_ptr()) })
                    && (is_fat || is_arr || is_vec || is_own)
                {
                    /* a guarded arm already hoisted this bind before the
                     * chain — skip the second decl (the arm's braces open
                     * a C scope where a re-decl would shadow, which C
                     * allows, but the hoist is the one in scope for the
                     * body either way; emitting both is noise). */
                    let ob3 = self.arena_tmp();
                    let already3 = if guarded {
                        unsafe { (*locals).lookup(pt, ptl, ob3) > 0 }
                    } else {
                        false
                    };
                    if !already3 {
                        self.indent();
                        self.out.put(ct, ct_len);
                        self.out.putc(b' ');
                        self.out.put(pt, ptl);
                        self.out.puts(b" = \0".as_ptr());
                        self.out.put(temp, temp_len);
                        self.out.puts(b";\n\0".as_ptr());
                        unsafe {
                            (*locals).add(pt, ptl, ct, ct_len, self.depth);
                        }
                    }
                }
            }
            unsafe { self.emit_block_value(body, locals, core::ptr::null(), 0) };
            unsafe { (*locals).drop_scope() };
            self.depth -= 1;
            self.indent();
            self.out.puts(b"}\n\0".as_ptr());
            i += 1;
        }
        /* end chain */
        self.indent();
        self.out.putc(b'\n');
        self.cur_opt_elem_len = 0;
        self.cur_scrut_len = 0;
        self.cur_res_ok_len = 0;
        self.cur_res_err_len = 0;
    }

    /* match arms with pattern-local bindings: the binding declared inside the
     * `if (…)` test needs to be visible in the body. `Some(x)` lowers to a
     * pointer nullity test with the bind declared before the if — done by
     * rewriting the arm as: `if (sv != 0) { T x = sv; body }`. That is what
     * emit_pat_test's Some-branch does inline (it emits the decl after the
     * test, still inside the if's condition, which is wrong for scoping), so
     * match with Some-patterns uses a pre-declared temp instead. */

    /* Declare Ok(bind)/Err(bind)'s bind from the Result scrutinee temp:
     * `T b = temp._v;` (Ok) or `E b = temp._e;` (Err), the payload types
     * decoded from the row (cur_res_ok / cur_res_err). */
    unsafe fn emit_res_binds(
        &mut self,
        bind: *const pm_jit_rsx_ast_t,
        is_ok: bool,
        temp: *const u8,
        temp_len: usize,
        locals: *mut LocalTab,
    ) {
        if unsafe { (*bind).kind } != pm_jit_rsx_ast_kind::PATH
            || (unsafe { (*bind).n_kids } as usize) < 1
        {
            return;
        }
        let bleaf = unsafe { *(*bind).kids.add(0) };
        let bt = unsafe { (*bleaf).text };
        let btl = unsafe { (*bleaf).text_len };
        /* `_` bind: no declaration */
        if btl == 1 && unsafe { z_eq(bt, 1, b"_\0".as_ptr()) } {
            return;
        }
        /* literal payloads (`Ok(true)`, `Ok(0)`) are VALUE arms, not
         * binds: the parser hands them as PATH leaves with literal
         * spellings (true/false/numbers). Declaring them as a bind
         * minted `bool true = ..` — not C. The test compares the
         * payload to the literal (emit_pat_test's literal-arm twin
         * handles the compare when the pattern rides the RES path). */
        if (btl == 4 && unsafe { z_eq(bt, 4, b"true\0".as_ptr()) })
            || (btl == 5 && unsafe { z_eq(bt, 5, b"false\0".as_ptr()) })
        {
            return;
        }
        let mut numeric = false;
        if btl > 0 && !bt.is_null() {
            let c0 = unsafe { *bt };
            if (c0 >= b'0' && c0 <= b'9') || c0 == b'-' {
                let mut j = 0usize;
                numeric = true;
                while j < btl {
                    let ch = unsafe { *bt.add(j) };
                    if !(ch >= b'0' && ch <= b'9') {
                        numeric = false;
                        break;
                    }
                    j += 1;
                }
            }
        }
        if numeric {
            return;
        }
        /* payload pair without a tuple temp (a `let (a,b) = if..` here
         * registers its row mid-fn, past lower_file's tup_emit_rest —
         * the decl then names an unemitted typedef). */
        let mut pct: *const u8 = self.cur_res_ok.as_ptr();
        let mut pcl = self.cur_res_ok_len;
        if !is_ok {
            pct = self.cur_res_err.as_ptr();
            pcl = self.cur_res_err_len;
        }
        if pcl == 0 {
            return;
        }
        self.indent();
        self.out.put(pct, pcl);
        self.out.putc(b' ');
        self.out.put(bt, btl);
        self.out.puts(b" = \0".as_ptr());
        self.out.put(temp, temp_len);
        if is_ok {
            self.out.puts(b"._v;\n\0".as_ptr());
        } else {
            self.out.puts(b"._e;\n\0".as_ptr());
        }
        unsafe {
            (*locals).add(bt, btl, pct, pcl, 1);
        }
    }

    /* Declare Some(bind)'s binds from the scrutinee temp — one level of
     * destructure: a PATH bind aliases `temp` (pointer payload) or copies
     * `temp._v` (struct-Option payload, cur_opt_elem carries the element
     * type); a TUPLE bind (Some((a, b)) — Option-of-tuple) declares each
     * element bind from `temp._v._N`. `ct` is the scrutinee's C type. */
    unsafe fn emit_some_binds(
        &mut self,
        bind: *const pm_jit_rsx_ast_t,
        temp: *const u8,
        temp_len: usize,
        ct: *const u8,
        ct_len: usize,
        locals: *mut LocalTab,
    ) {
        if unsafe { (*bind).kind } == pm_jit_rsx_ast_kind::PATH && unsafe { (*bind).n_kids } >= 1 {
            let bleaf = unsafe { *(*bind).kids.add(0) };
            /* `_` bind: no declaration (a C ident named _ is not valid) */
            if unsafe { (*bleaf).text_len == 1 }
                && unsafe { z_eq(unsafe { (*bleaf).text }, 1, b"_\0".as_ptr()) }
            {
                return;
            }
            self.indent();
            let mut bct: *const u8 = ct;
            let mut bct_len = ct_len;
            let eln = self.cur_opt_elem_len;
            if eln > 0 {
                bct = self.cur_opt_elem.as_ptr();
                bct_len = eln;
                self.out.put(bct, bct_len);
                self.out.putc(b' ');
                self.out.put(unsafe { (*bleaf).text }, unsafe { (*bleaf).text_len });
                self.out.puts(b" = \0".as_ptr());
                self.out.put(temp, temp_len);
                self.out.puts(b"._v;\n\0".as_ptr());
            } else {
                self.out.put(ct, ct_len);
                self.out.putc(b' ');
                self.out.put(unsafe { (*bleaf).text }, unsafe { (*bleaf).text_len });
                self.out.puts(b" = \0".as_ptr());
                self.out.put(temp, temp_len);
                self.out.puts(b";\n\0".as_ptr());
            }
            unsafe {
                (*locals).add(
                    unsafe { (*bleaf).text },
                    unsafe { (*bleaf).text_len },
                    bct,
                    bct_len,
                    1,
                );
            }
            return;
        }
        if unsafe { (*bind).kind } == pm_jit_rsx_ast_kind::TUPLE {
            /* Option-of-tuple: the payload is the tuple struct. Each element
             * bind copies the designated field of `temp._v` (or of `temp`
             * itself when the Option is pointer-shaped — never: a pointer
             * payload tuple makes no sense; the struct path is the only
             * sound one). */
            if self.cur_opt_elem_len == 0 {
                unsafe {
                    self.err(b"unsupported: Some((..)) on a pointer-Option\0".as_ptr(), unsafe { (*bind).line });
                }
                return;
            }
            let pk = unsafe { (*bind).kids };
            let pn = unsafe { (*bind).n_kids } as usize;
            if pn == 0 || pn > TUP_MAXF {
                unsafe {
                    self.err(b"unsupported: tuple pattern with more than 4 binds\0".as_ptr(), unsafe { (*bind).line });
                }
                return;
            }
            /* payload spelling must be a registered tuple typedef */
            let pc = self.cur_opt_elem.as_ptr();
            let pl = self.cur_opt_elem_len;
            /* rsx_strpair_t (the split_once payload): the dedicated
             * (&str,&str) row — binds from _0/_1, both rsx_str_ref_t. */
            if pl == 13 && unsafe { z_eq(pc, 13, b"rsx_strpair_t\0".as_ptr()) } {
                if pn != 2 {
                    unsafe {
                        self.err(b"Some-tuple pattern arity mismatch\0".as_ptr(), unsafe { (*bind).line });
                    }
                    return;
                }
                let mut f3 = 0usize;
                while f3 < 2 {
                    let sub3 = unsafe { *pk.add(f3) };
                    let mut bnode3 = sub3;
                    if unsafe { (*bnode3).kind } == pm_jit_rsx_ast_kind::PATH
                        && unsafe { z_eq((*bnode3).text, (*bnode3).text_len, b"path\0".as_ptr()) }
                        && unsafe { (*bnode3).n_kids } as usize == 1
                    {
                        bnode3 = unsafe { *(*bnode3).kids.add(0) };
                    }
                    if unsafe { (*bnode3).kind } == pm_jit_rsx_ast_kind::PATH {
                        let bn3 = unsafe { (*bnode3).text };
                        let bl3 = unsafe { (*bnode3).text_len };
                        if !(bl3 == 1 && unsafe { z_eq(bn3, 1, b"_\0".as_ptr()) }) {
                            self.indent();
                            self.out.puts(b"rsx_str_ref_t \0".as_ptr());
                            self.out.put(bn3, bl3);
                            self.out.puts(b" = \0".as_ptr());
                            self.out.put(temp, temp_len);
                            self.out.puts(b"._v._\0".as_ptr());
                            let d = b'0' + f3 as u8;
                            self.out.putc(d);
                            self.out.puts(b";\n\0".as_ptr());
                            unsafe {
                                (*locals).add(bn3, bl3, b"rsx_str_ref_t\0".as_ptr(), 13, 1);
                            }
                        }
                    }
                    f3 += 1;
                }
                return;
            }
            let slot = unsafe { self.tup_find(pc, pl) };
            if slot >= TUP_CAP {
                unsafe {
                    self.err(b"internal: Some-tuple payload not registered\0".as_ptr(), unsafe { (*bind).line });
                }
                return;
            }
            if self.tup_counts[slot] != pn {
                unsafe {
                    self.err(b"Some-tuple pattern arity mismatch\0".as_ptr(), unsafe { (*bind).line });
                }
                return;
            }
            let mut f = 0usize;
            while f < pn {
                let sub = unsafe { *pk.add(f) };
                let mut bnode = sub;
                if unsafe { (*bnode).kind } == pm_jit_rsx_ast_kind::PATH
                    && unsafe { z_eq((*bnode).text, (*bnode).text_len, b"path\0".as_ptr()) }
                    && unsafe { (*bnode).n_kids } as usize == 1
                {
                    bnode = unsafe { *(*bnode).kids.add(0) };
                }
                if unsafe { (*bnode).kind } != pm_jit_rsx_ast_kind::PATH {
                    unsafe {
                        self.err(b"unsupported: tuple pattern element (expect binds)\0".as_ptr(), unsafe { (*bind).line });
                    }
                    return;
                }
                let bn = unsafe { (*bnode).text };
                let bl = unsafe { (*bnode).text_len };
                if bl == 1 && unsafe { z_eq(bn, 1, b"_\0".as_ptr()) } {
                    f += 1;
                    continue;
                }
                let a = slot * TUP_MAXF + f;
                let elct = self.tup_elems[a].as_ptr();
                let elct_len = self.tup_lens[a];
                self.indent();
                self.out.put(elct, elct_len);
                self.out.putc(b' ');
                self.out.put(bn, bl);
                self.out.puts(b" = \0".as_ptr());
                self.out.put(temp, temp_len);
                self.out.puts(b"._v._\0".as_ptr());
                let d = b'0' + f as u8;
                self.out.putc(d);
                self.out.puts(b";\n\0".as_ptr());
                unsafe {
                    (*locals).add(bn, bl, elct, elct_len, 1);
                }
                f += 1;
            }
        }
    }


    /* match with a value: arms store into temp. */
    unsafe fn emit_match_value(&mut self, s: *const pm_jit_rsx_ast_t, locals: *mut LocalTab, temp: *const u8, temp_len: usize) {
        let kids = unsafe { (*s).kids };
        let nk = unsafe { (*s).n_kids } as usize;
        if nk < 1 {
            return;
        }
        let mut scrut = unsafe { *kids.add(0) };
        let st = b"__rsx_m\0".as_ptr();
        let st_len = 7usize;
        /* let-chain sugar (see emit_match): unwrap `X && guard` when the
         * LHS types as an Option — the guard ANDs into the arm tests. */
        let mut chain_guard: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
        if unsafe { (*scrut).kind } == pm_jit_rsx_ast_kind::BINARY
            && (unsafe { (*scrut).n_kids } as usize) >= 2
            && unsafe { z_eq(unsafe { (*scrut).text }, unsafe { (*scrut).text_len }, b"&&\0".as_ptr()) }
        {
            let sk = unsafe { (*scrut).kids };
            let lhs = unsafe { *sk.add(0) };
            let gb = self.arena_tmp();
            let saved_pre = self.pre_pool_at;
            let gl = unsafe { self.expr_ctype(lhs, gb, 128, locals) };
            self.pre_pool_at = saved_pre;
            if gl > 8 && gl < 128 && unsafe { z_eq(gb, 8, b"rsx_opt_\0".as_ptr()) } {
                chain_guard = unsafe { *sk.add(1) };
                scrut = lhs;
            }
        }
        let ct = self.arena_tmp();
        let ct_len = unsafe { self.expr_ctype(scrut, ct, 128, locals) };
        if ct_len == 0 {
            unsafe {
                self.err(b"cannot infer match scrutinee type\0".as_ptr(), unsafe { (*s).line });
            }
            return;
        }
        /* stash the scrutinee type for emit_pat_test — string-literal
         * arms compare the view (len + memcmp), not a scalar == */
        unsafe {
            core::ptr::copy_nonoverlapping(ct, self.cur_scrut.as_mut_ptr(), ct_len);
        }
        self.cur_scrut_len = ct_len;
        /* struct-shaped Option: arms test ._has; the Some bind copies _v.
         * Named typedef (rsx_opt_<elem>) or legacy inline struct. */
        {
            let mut eln = unsafe {
                Lower::opt_typedef_elem(ct, ct_len, self.cur_opt_elem.as_mut_ptr(), 96)
            };
            if eln == 0 {
                eln = unsafe { Lower::opt_struct_elem(ct, ct_len) };
                if eln > 0 {
                    unsafe {
                        core::ptr::copy_nonoverlapping(ct.add(9), self.cur_opt_elem.as_mut_ptr(), eln);
                    }
                }
            }
            if eln > 96 {
                unsafe {
                    self.err(b"internal: option payload type too long\0".as_ptr(), unsafe { (*s).line });
                }
                return;
            }
            self.cur_opt_elem_len = eln;
        }
        /* Result scrutinee: decode both payload spellings for the Ok/Err
         * arm binds (._v / ._e) — the emit_match twin. */
        {
            let on = unsafe {
                Lower::res_typedef_elem(
                    ct,
                    ct_len,
                    self.cur_res_ok.as_mut_ptr(),
                    96,
                    self.cur_res_err.as_mut_ptr(),
                    96,
                )
            };
            self.cur_res_ok_len = on;
            if on > 0 {
                let mut el2 = 0usize;
                while el2 < 96 && unsafe { *self.cur_res_err.as_ptr().add(el2) } != 0 {
                    el2 += 1;
                }
                self.cur_res_err_len = el2;
            } else {
                self.cur_res_err_len = 0;
            }
        }
        /* same late-intern flush as emit_match (see there). */
        unsafe { self.fnp_emit_rest() };
        unsafe { self.opt_emit_rest(0) };
        unsafe { self.res_emit_rest() };
        self.indent();
        self.out.put(ct, ct_len);
        self.out.putc(b' ');
        self.out.put(st, st_len);
        self.out.puts(b" = \0".as_ptr());
        unsafe { self.emit_expr(scrut, locals) };
        self.out.puts(b";\n\0".as_ptr());
        /* pre-declare guarded Some(bind) arms' binds BEFORE the chain
         * (see emit_match): a decl mid-chain (`else T b = ..; if`) is
         * not C, so guarded Some arms hoist their bind. */
        {
            let mut pi = 1usize;
            while pi < nk {
                let parm = unsafe { *kids.add(pi) };
                let pak = unsafe { (*parm).kids };
                let pank = unsafe { (*parm).n_kids } as usize;
                /* guarded arms (explicit guard) always hoist; a let-chain
                 * arm (2 kids + chain_guard) hoists too — its guard types
                 * against the bind the same way. */
                if pank >= 3 || (!chain_guard.is_null() && pank >= 2) {
                    let ppat = unsafe { *pak.add(0) };
                    if unsafe { (*ppat).kind } == pm_jit_rsx_ast_kind::PATH {
                        let ppk = unsafe { (*ppat).kids };
                        let ppnk = unsafe { (*ppat).n_kids } as usize;
                        if ppnk >= 2 {
                            let phead = unsafe { *ppk.add(0) };
                            let pbind = unsafe { *ppk.add(1) };
                            if unsafe { (*phead).kind } == pm_jit_rsx_ast_kind::PATH
                                && unsafe { z_eq(unsafe { (*phead).text }, unsafe { (*phead).text_len }, b"Some\0".as_ptr()) }
                                && (unsafe { (*pbind).kind } == pm_jit_rsx_ast_kind::PATH
                                    || unsafe { (*pbind).kind } == pm_jit_rsx_ast_kind::TUPLE)
                            {
                                /* dedup (PATH binds only): a same-named bind
                                 * hoisted by an earlier arm is already
                                 * declared — a second C decl would clash. */
                                let mut already = false;
                                if unsafe { (*pbind).kind } == pm_jit_rsx_ast_kind::PATH {
                                    let bleaf = unsafe { *(*pbind).kids.add(0) };
                                    let ob = self.arena_tmp();
                                    already = unsafe {
                                        (*locals).lookup(unsafe { (*bleaf).text }, unsafe { (*bleaf).text_len }, ob) > 0
                                    };
                                }
                                if !already {
                                    unsafe { self.emit_some_binds(pbind, st, st_len, ct, ct_len, locals) };
                                }
                            }
                        }
                        /* a guarded PLAIN bind arm (`s if s.starts_with(..)
                         * => ..`) on a value-shaped scrutinee: the guard
                         * types against the bind, so it must be declared
                         * before the chain — same hoist (T b = temp). A
                         * bare bind parses as the "path" wrapper over one
                         * segment; unwrap. A unit-static name is a const
                         * pattern, not a bind; scalars stay bindless. */
                        {
                            let mut pbind2 = ppat;
                            if ppnk == 1
                                && unsafe { z_eq(unsafe { (*ppat).text }, unsafe { (*ppat).text_len }, b"path\0".as_ptr()) }
                            {
                                pbind2 = unsafe { *(*ppat).kids.add(0) };
                            }
                            let pb2nk = unsafe { (*pbind2).n_kids } as usize;
                            if pb2nk == 0 {
                                let ptl = unsafe { (*pbind2).text_len };
                                let pt = unsafe { (*pbind2).text };
                                let is_fat = ct_len == 13
                                    && unsafe { z_eq(ct, 13, b"rsx_str_ref_t\0".as_ptr()) };
                                let is_arr = ct_len > 8
                                    && ct_len < 128
                                    && unsafe { z_eq(ct, 8, b"rsx_arr_\0".as_ptr()) };
                                let is_vec = ct_len > 8
                                    && ct_len < 128
                                    && unsafe { z_eq(ct, 8, b"rsx_vec_\0".as_ptr()) };
                                let is_own = ct_len == 9
                                    && unsafe { z_eq(ct, 9, b"rsx_str_t\0".as_ptr()) };
                                let is_unit_static = {
                                    let sb = self.arena_tmp();
                                    let sl = unsafe { self.st_find(pt, ptl, sb, 128) };
                                    sl > 0
                                };
                                if ptl > 0
                                    && !pt.is_null()
                                    && !is_unit_static
                                    && !(ptl == 1 && unsafe { z_eq(pt, 1, b"_\0".as_ptr()) })
                                    && (is_fat || is_arr || is_vec || is_own)
                                {
                                    /* expr_ctype's MATCH pass pre-registered
                                     * every plain-bind arm (type rows only —
                                     * no C decl emitted), so the locals hit
                                     * here is expected and NOT a duplicate:
                                     * the guarded arm's guard needs the decl
                                     * before the chain; unguarded arms
                                     * re-declare inside their own block
                                     * scope (legal C shadow). Always emit. */
                                    self.indent();
                                    self.out.put(ct, ct_len);
                                    self.out.putc(b' ');
                                    self.out.put(pt, ptl);
                                    self.out.puts(b" = \0".as_ptr());
                                    self.out.put(st, st_len);
                                    self.out.puts(b";\n\0".as_ptr());
                                    unsafe {
                                        (*locals).add(pt, ptl, ct, ct_len, self.depth + 1);
                                    }
                                }
                            }
                        }
                    }
                }
                pi += 1;
            }
        }
        let mut i = 1usize;
        let mut first = true;
        while i < nk {
            let arm = unsafe { *kids.add(i) };
            let ak = unsafe { (*arm).kids };
            let ank = unsafe { (*arm).n_kids } as usize;
            if ank < 2 {
                i += 1;
                continue;
            }
            let pat = unsafe { *ak.add(0) };
            /* arm kids: [pat, body] or [pat, guard, body] */
            let has_guard = ank >= 3;
            let body = unsafe { *ak.add(ank - 1) };
            /* true when this arm's Some(bind) was pre-declared before
             * the chain (a guarded Some arm): the chain path emits test +
             * body, the bind is already in scope. */
            let mut hoisted_bind = false;
            self.indent();
            if !first {
                self.out.puts(b"else \0".as_ptr());
            }
            first = false;
            let is_wc = unsafe { (*pat).kind } == pm_jit_rsx_ast_kind::PATH
                && unsafe { z_eq(unsafe { (*pat).text }, unsafe { (*pat).text_len }, b"_\0".as_ptr()) };
            /* Some(bind) with a guard: scoped arm (see emit_match) —
             * binds + if + body in one brace scope so source shadowing
             * stays legal. */
            let guarded = has_guard || !chain_guard.is_null();
            let mut some_bind: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
            /* Result twin: the Ok(x)/Err(x) bind (see emit_match). */
            let mut res_bind: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
            let mut res_is_ok = false;
            /* Tagged-enum twin: `E::V(bind)` (see emit_match). */
            let mut enum_bind: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
            let mut enum_pay: [u8; 96] = [0; 96];
            let mut enum_pay_len: usize = 0;
            let mut enum_vname: [u8; 64] = [0; 64];
            let mut enum_vname_len: usize = 0;
            if unsafe { (*pat).kind } == pm_jit_rsx_ast_kind::PATH {
                let pk = unsafe { (*pat).kids };
                let pnk = unsafe { (*pat).n_kids } as usize;
                if pnk >= 2 {
                    let head = unsafe { *pk.add(0) };
                    let bind = unsafe { *pk.add(1) };
                    if unsafe { (*head).kind } == pm_jit_rsx_ast_kind::PATH {
                        let ht = unsafe { (*head).text };
                        let hl = unsafe { (*head).text_len };
                        if unsafe { z_eq(ht, hl, b"Some\0".as_ptr()) }
                            && (unsafe { (*bind).kind } == pm_jit_rsx_ast_kind::PATH
                                || unsafe { (*bind).kind } == pm_jit_rsx_ast_kind::TUPLE)
                        {
                            some_bind = bind;
                            if guarded
                                && (unsafe { (*bind).kind } == pm_jit_rsx_ast_kind::PATH
                                    || unsafe { (*bind).kind } == pm_jit_rsx_ast_kind::TUPLE)
                            {
                                hoisted_bind = true;
                            }
                            /* let-chain arms are `guarded` by chain_guard —
                             * same hoist, same flag (the bind was already
                             * declared before the chain). */
                        } else if (unsafe { z_eq(ht, hl, b"Ok\0".as_ptr()) }
                            || unsafe { z_eq(ht, hl, b"Err\0".as_ptr()) })
                            && unsafe { (*bind).kind } == pm_jit_rsx_ast_kind::PATH
                            && self.cur_res_ok_len > 0
                        {
                            res_bind = bind;
                            res_is_ok = unsafe { z_eq(ht, hl, b"Ok\0".as_ptr()) };
                        }
                    }
                }
                if enum_bind.is_null() && pnk >= 3 {
                    let mut joined = self.arena_tmp();
                    let mut jat = 0usize;
                    let mut ok_join = true;
                    let mut ji = 0usize;
                    while ji + 1 < pnk {
                        let seg = unsafe { *pk.add(ji) };
                        if unsafe { (*seg).kind } != pm_jit_rsx_ast_kind::PATH {
                            ok_join = false;
                            break;
                        }
                        if jat > 0 {
                            jat = unsafe { bput(joined, 128, jat, b"_\0".as_ptr(), 1) };
                        }
                        jat = unsafe { bput(joined, 128, jat, unsafe { (*seg).text }, unsafe { (*seg).text_len }) };
                        ji += 1;
                    }
                    if ok_join && jat > 0 {
                        unsafe {
                            *joined.add(jat) = 0;
                        }
                        let pvbuf = self.arena_tmp();
                        let plen = unsafe { self.enumpays.lookup(joined, jat, pvbuf, 96) };
                        if plen > 0 {
                            let vleaf = unsafe { *pk.add(pnk - 2) };
                            let vt2 = unsafe { (*vleaf).text };
                            let vl2 = unsafe { (*vleaf).text_len };
                            if vl2 > 0 && vl2 < 64 && !vt2.is_null() {
                                let mut c2 = 0usize;
                                while c2 < vl2 {
                                    enum_vname[c2] = unsafe { *vt2.add(c2) };
                                    c2 += 1;
                                }
                                enum_vname_len = vl2;
                                let mut c3 = 0usize;
                                while c3 < plen {
                                    enum_pay[c3] = unsafe { *pvbuf.add(c3) };
                                    c3 += 1;
                                }
                                enum_pay_len = plen;
                                enum_bind = unsafe { *pk.add(pnk - 1) };
                            }
                        }
                    }
                }
            }
            if guarded && !res_bind.is_null() {
                /* scoped Result arm (see emit_match): binds + if + body
                 * in one brace scope. */
                self.depth += 1;
                unsafe { (*locals).note_scope() };
                unsafe { self.emit_res_binds(res_bind, res_is_ok, st, st_len, locals) };
                self.indent();
                if !is_wc {
                    self.out.puts(b"if (\0".as_ptr());
                    unsafe { self.emit_pat_test(pat, st, st_len, locals) };
                    if has_guard {
                        self.out.puts(b" && (\0".as_ptr());
                        unsafe { self.emit_expr(*ak.add(1), locals) };
                        self.out.puts(b")\0".as_ptr());
                    } else if !chain_guard.is_null() {
                        self.out.puts(b" && (\0".as_ptr());
                        unsafe { self.emit_expr(chain_guard, locals) };
                        self.out.puts(b")\0".as_ptr());
                    }
                    self.out.puts(b") {\n\0".as_ptr());
                } else {
                    self.out.puts(b"{\n\0".as_ptr());
                }
                self.depth += 1;
                unsafe { (*locals).note_scope() };
                unsafe { self.emit_block_value(body, locals, temp, temp_len) };
                unsafe { (*locals).drop_scope() };
                self.depth -= 1;
                self.indent();
                self.out.puts(b"}\n\0".as_ptr());
                unsafe { (*locals).drop_scope() };
                self.depth -= 1;
                i += 1;
                continue;
            }
            /* guarded Some(bind) arms had their binds pre-declared before
             * the chain (the pre-scan above): fall through to the chain
             * path — test + body emit there, bind already in scope. */
            if !is_wc || has_guard {
                self.out.puts(b"if (\0".as_ptr());
                if !is_wc {
                    unsafe { self.emit_pat_test(pat, st, st_len, locals) };
                }
                if has_guard {
                    if !is_wc {
                        self.out.puts(b" && \0".as_ptr());
                    }
                    self.out.puts(b"(\0".as_ptr());
                    unsafe { self.emit_expr(*ak.add(1), locals) };
                    self.out.puts(b")\0".as_ptr());
                } else if !chain_guard.is_null() && !is_wc {
                    self.out.puts(b" && (\0".as_ptr());
                    unsafe { self.emit_expr(chain_guard, locals) };
                    self.out.puts(b")\0".as_ptr());
                }
                self.out.puts(b") \0".as_ptr());
            }
            self.out.puts(b"{\n\0".as_ptr());
            /* Some(bind) on an unguarded arm: bind decl inside the arm
             * braces — shadowing stays legal. Hoisted (guarded) arms
             * skip: their bind was declared before the chain. */
            if !some_bind.is_null() && !hoisted_bind {
                unsafe { (*locals).note_scope() };
                unsafe { self.emit_some_binds(some_bind, st, st_len, ct, ct_len, locals) };
            }
            /* Ok(bind)/Err(bind) unguarded: same arm-brace placement. */
            if !res_bind.is_null() {
                unsafe { (*locals).note_scope() };
                unsafe { self.emit_res_binds(res_bind, res_is_ok, st, st_len, locals) };
            }
            /* E::V(bind) unguarded: the tagged-union payload copy. */
            if !enum_bind.is_null() && enum_pay_len > 0 {
                unsafe { (*locals).note_scope() };
                let bleaf2 = unsafe { *(*enum_bind).kids.add(0) };
                let bt2 = unsafe { (*bleaf2).text };
                let btl2 = unsafe { (*bleaf2).text_len };
                if !(btl2 == 1 && unsafe { z_eq(bt2, 1, b"_\0".as_ptr()) }) {
                    self.indent();
                    self.out.put(enum_pay.as_ptr(), enum_pay_len);
                    self.out.putc(b' ');
                    self.out.put(bt2, btl2);
                    self.out.puts(b" = \0".as_ptr());
                    self.out.put(st, st_len);
                    self.out.puts(b"._u.\0".as_ptr());
                    self.out.put(enum_vname.as_ptr(), enum_vname_len);
                    self.out.puts(b";\n\0".as_ptr());
                    unsafe {
                        (*locals).add(bt2, btl2, enum_pay.as_ptr(), enum_pay_len, 1);
                    }
                }
            }
            /* tuple-pattern binding arm `(x, y)`: declare each element
             * bind from the temp's `_i` field with the row's element
             * ctype. Non-binding elements (literals) declare nothing. */
            if unsafe { (*pat).kind } == pm_jit_rsx_ast_kind::TUPLE {
                let tk = unsafe { (*pat).kids };
                let tn = unsafe { (*pat).n_kids } as usize;
                unsafe { (*locals).note_scope() };
                let mut ti = 0usize;
                while ti < tn {
                    let sub = unsafe { *tk.add(ti) };
                    let mut bnode = sub;
                    if unsafe { (*sub).kind } == pm_jit_rsx_ast_kind::PATH
                        && unsafe { (*sub).n_kids } as usize == 1
                        && unsafe { z_eq(unsafe { (*sub).text }, unsafe { (*sub).text_len }, b"path\0".as_ptr()) }
                    {
                        bnode = unsafe { *(*sub).kids.add(0) };
                    }
                    if unsafe { (*bnode).kind } != pm_jit_rsx_ast_kind::PATH {
                        ti += 1;
                        continue;
                    }
                    let bn = unsafe { (*bnode).text };
                    let bnl = unsafe { (*bnode).text_len };
                    if bnl == 0 || (bnl == 1 && unsafe { z_eq(bn, 1, b"_\0".as_ptr()) }) {
                        ti += 1;
                        continue;
                    }
                    /* element spelling: `(<temp>)._i` */
                    let eb = self.arena_tmp();
                    let mut eat = 0usize;
                    eat = unsafe { bput(eb, 72, eat, b"(\0".as_ptr(), 1) };
                    eat = unsafe { bput(eb, 72, eat, st, st_len) };
                    eat = unsafe { bput(eb, 72, eat, b")._\0".as_ptr(), 3) };
                    let mut digs = [0u8; 12];
                    let mut v = ti as u64;
                    let mut di = 0usize;
                    while v > 0 && di < 12 {
                        digs[di] = b'0' + (v % 10) as u8;
                        v /= 10;
                        di += 1;
                    }
                    if di == 0 {
                        digs[0] = b'0';
                        di = 1;
                    }
                    let mut dj = di;
                    while dj > 0 {
                        dj -= 1;
                        eat = unsafe { bput(eb, 72, eat, digs.as_ptr().add(dj), 1) };
                    }
                    /* element ctype from the registered row */
                    let ecb = self.arena_tmp();
                    let el = unsafe {
                        self.tup_row_elem_ctype(ct, ct_len, ti, ecb, 128)
                    };
                    if el == 0 {
                        unsafe {
                            self.err(b"cannot type tuple pattern binding\0".as_ptr(), unsafe { (*s).line });
                        }
                        return;
                    }
                    self.depth += 1;
                    self.indent();
                    self.out.put(ecb, el);
                    self.out.putc(b' ');
                    self.out.put(bn, bnl);
                    self.out.puts(b" = \0".as_ptr());
                    self.out.put(eb, eat);
                    self.out.puts(b";\n\0".as_ptr());
                    self.depth -= 1;
                    unsafe {
                        (*locals).add(bn, bnl, ecb, el, self.depth);
                    }
                    ti += 1;
                }
            }
            /* plain binding arm on a value-shaped scrutinee — same shape
             * as emit_match's: declare the bind as a copy of the temp. */
            if unsafe { (*pat).kind } == pm_jit_rsx_ast_kind::PATH {
                let mut bnode = pat;
                if unsafe { (*pat).n_kids } as usize == 1
                    && unsafe { z_eq(unsafe { (*pat).text }, unsafe { (*pat).text_len }, b"path\0".as_ptr()) }
                {
                    bnode = unsafe { *(*pat).kids.add(0) };
                }
                let pk = unsafe { (*bnode).kids };
                let pnk = unsafe { (*bnode).n_kids } as usize;
                let pt = unsafe { (*bnode).text };
                let ptl = unsafe { (*bnode).text_len };
                let is_fat = ct_len == 13
                    && unsafe { z_eq(ct, 13, b"rsx_str_ref_t\0".as_ptr()) };
                let is_arr = ct_len > 8
                    && ct_len < 128
                    && unsafe { z_eq(ct, 8, b"rsx_arr_\0".as_ptr()) };
                let is_vec = ct_len > 8
                    && ct_len < 128
                    && unsafe { z_eq(ct, 8, b"rsx_vec_\0".as_ptr()) };
                let is_own = ct_len == 9 && unsafe { z_eq(ct, 9, b"rsx_str_t\0".as_ptr()) };
                /* a single-segment name that IS a unit static (a `pub
                 * const X: &str`) is a CONST PATTERN, not a binding —
                 * emit_pat_test compares against the value; declaring it
                 * here would shadow the C static with a local. */
                let is_unit_static = {
                    let sb = self.arena_tmp();
                    let sl = unsafe { self.st_find(pt, ptl, sb, 128) };
                    sl > 0
                };
                if pnk == 0
                    && ptl > 0
                    && !pt.is_null()
                    && !is_unit_static
                    && !(ptl == 1 && unsafe { z_eq(pt, 1, b"_\0".as_ptr()) })
                    && !(ptl == 4 && unsafe { z_eq(pt, 4, b"None\0".as_ptr()) })
                    && (is_fat || is_arr || is_vec || is_own)
                {
                    /* a guarded arm already hoisted this bind before the
                     * chain — skip the second decl (same reasoning as
                     * emit_match's plain-bind dedup). */
                    let ob3 = self.arena_tmp();
                    let already3 = if guarded {
                        unsafe { (*locals).lookup(pt, ptl, ob3) > 0 }
                    } else {
                        false
                    };
                    if !already3 {
                        self.depth += 1;
                        self.indent();
                        self.out.put(ct, ct_len);
                        self.out.putc(b' ');
                        self.out.put(pt, ptl);
                        self.out.puts(b" = \0".as_ptr());
                        self.out.put(st, st_len);
                        self.out.puts(b";\n\0".as_ptr());
                        unsafe {
                            (*locals).add(pt, ptl, ct, ct_len, self.depth);
                        }
                        self.depth -= 1;
                    }
                }
            }
            self.depth += 1;
            unsafe { (*locals).note_scope() };
            unsafe { self.emit_block_value(body, locals, temp, temp_len) };
            unsafe { (*locals).drop_scope() };
            self.depth -= 1;
            self.indent();
            self.out.puts(b"}\n\0".as_ptr());
            i += 1;
        }
        self.cur_scrut_len = 0;
        self.cur_opt_elem_len = 0;
        self.cur_res_ok_len = 0;
        self.cur_res_err_len = 0;
    }

    /* Element ctype of a tuple ROW by its registered typedef name: render
     * every interned row's name and compare against `row` (the scrutinee's
     * ctype), then copy field `i`'s element type out. 0 when the name is
     * not a registered row (a scalar scrutinee, a struct, ..). */
    unsafe fn tup_row_elem_ctype(&mut self, row: *const u8, row_len: usize, i: usize, out: *mut u8, cap: usize) -> usize {
        if row_len < 10 || row.is_null() || i >= TUP_MAXF {
            return 0;
        }
        let mut slot = 0usize;
        while slot < TUP_CAP {
            let n = unsafe { *self.tup_counts.as_ptr().add(slot) };
            if n == 0 {
                break;
            }
            let base = self.tup_elems.as_ptr().add(slot * TUP_MAXF);
            let blens = self.tup_lens.as_ptr().add(slot * TUP_MAXF);
            let need = unsafe { Lower::tup_name_need(blens, n) };
            if need > 0 && need < 1100 {
                let nb = self.arena_tmp();
                let nl = unsafe { Lower::tup_typedef_name(base, blens, n, nb, need) };
                if nl == row_len {
                    let mut j = 0usize;
                    let mut eq = true;
                    while j < nl {
                        if unsafe { *nb.add(j) } != unsafe { *row.add(j) } {
                            eq = false;
                            break;
                        }
                        j += 1;
                    }
                    if eq {
                        let el = unsafe { *blens.add(i) };
                        if el == 0 || el >= cap {
                            return 0;
                        }
                        unsafe {
                            core::ptr::copy_nonoverlapping(base.add(i) as *const u8, out, el);
                            *out.add(el) = 0;
                        }
                        return el;
                    }
                }
            }
            slot += 1;
        }
        0
    }

    /* Pattern test against scrutinee temp `sv` (a C rvalue): emits the test
     * expression (no parens — caller wraps). Some(x) tests the inner
     * pointer's nullity; or-patterns join with ||. */
    unsafe fn emit_pat_test(&mut self, pat: *const pm_jit_rsx_ast_t, sv: *const u8, sv_len: usize, locals: *mut LocalTab) {
        if pat.is_null() {
            return;
        }
        let kind = unsafe { (*pat).kind };
        if kind == pm_jit_rsx_ast_kind::LITERAL {
            let pt = unsafe { (*pat).text };
            let ptl = unsafe { (*pat).text_len };
            /* string-literal pattern against an &str scrutinee: the view
             * compare (length + memcmp), never a scalar == on a struct */
            if ptl >= 2
                && !pt.is_null()
                && unsafe { *pt } == b'"'
                && self.cur_scrut_len == 13
                && unsafe { z_eq(self.cur_scrut.as_ptr(), 13, b"rsx_str_ref_t\0".as_ptr()) }
            {
                self.str_ref_used = true;
                let inner = ptl - 2;
                /* one wrapped spelling of the scrutinee, reused for both
                 * the .n and the .p reads — balance holds for a bare ident
                 * AND for a pre-parenthesized field spelling. */
                let wb = self.arena_tmp();
                let mut wat = 0usize;
                wat = unsafe { bput(wb, 72, 0, b"(\0".as_ptr(), 1) };
                wat = unsafe { bput(wb, 72, wat, sv, sv_len) };
                wat = unsafe { bput(wb, 72, wat, b")\0".as_ptr(), 1) };
                self.out.putc(b'(');
                self.out.put(wb, wat);
                self.out.puts(b".n == \0".as_ptr());
                self.out.put_u32(unsafe { Lower::c_str_len(pt.add(1), inner) });
                self.out.puts(b" && !memcmp(\0".as_ptr());
                self.out.put(wb, wat);
                self.out.puts(b".p, \0".as_ptr());
                self.out.put(pt, ptl);
                self.out.puts(b", \0".as_ptr());
                self.out.put_u32(unsafe { Lower::c_str_len(pt.add(1), inner) });
                self.out.puts(b"))\0".as_ptr());
                return;
            }
            self.out.put(sv, sv_len);
            self.out.puts(b" == \0".as_ptr());
            unsafe { self.emit_literal(unsafe { (*pat).text }, unsafe { (*pat).text_len }) };
            return;
        }
        if kind == pm_jit_rsx_ast_kind::UNARY {
            /* negative literal or &bind */
            let kids = unsafe { (*pat).kids };
            if unsafe { (*pat).n_kids } >= 1 {
                let inner = unsafe { *kids.add(0) };
                let t = unsafe { (*pat).text };
                if unsafe { z_eq(t, unsafe { (*pat).text_len }, b"-\0".as_ptr()) } {
                    self.out.put(sv, sv_len);
                    self.out.puts(b" == -\0".as_ptr());
                    unsafe { self.emit_literal(unsafe { (*inner).text }, unsafe { (*inner).text_len }) };
                    return;
                }
                if unsafe { z_eq(t, unsafe { (*pat).text_len }, b"&\0".as_ptr()) } {
                    /* &bind matches any pointer — compare address */
                    self.out.put(sv, sv_len);
                    self.out.puts(b" != 0\0".as_ptr());
                    return;
                }
            }
            unsafe {
                self.err(b"unsupported: pattern form\0".as_ptr(), unsafe { (*pat).line });
            }
            return;
        }
        if kind == pm_jit_rsx_ast_kind::PATH {
            let t = unsafe { (*pat).text };
            let tl = unsafe { (*pat).text_len };
            let kids = unsafe { (*pat).kids };
            let nk = unsafe { (*pat).n_kids } as usize;
            /* or-pattern */
            if nk > 0 && unsafe { z_eq(t, tl, b"or\0".as_ptr()) } {
                self.out.putc(b'(');
                let mut i = 0usize;
                while i < nk {
                    if i > 0 {
                        self.out.puts(b" || \0".as_ptr());
                    }
                    unsafe { self.emit_pat_test(*kids.add(i), sv, sv_len, locals) };
                    i += 1;
                }
                self.out.putc(b')');
                return;
            }
            /* range pattern `lo..=hi` — inclusive both ends. The atoms are
             * literal nodes; emit their values, not sv tests. */
            if nk >= 2 && unsafe { z_eq(t, tl, b"range\0".as_ptr()) } {
                let lo = unsafe { *kids.add(0) };
                let hi = unsafe { *kids.add(1) };
                if unsafe { (*lo).kind } != pm_jit_rsx_ast_kind::LITERAL
                    || unsafe { (*hi).kind } != pm_jit_rsx_ast_kind::LITERAL
                {
                    unsafe {
                        self.err(b"unsupported: range pattern atom\0".as_ptr(), unsafe { (*pat).line });
                    }
                    return;
                }
                self.out.putc(b'(');
                self.out.put(sv, sv_len);
                self.out.puts(b" >= \0".as_ptr());
                unsafe { self.emit_literal(unsafe { (*lo).text }, unsafe { (*lo).text_len }) };
                self.out.puts(b" && \0".as_ptr());
                self.out.put(sv, sv_len);
                self.out.puts(b" <= \0".as_ptr());
                unsafe { self.emit_literal(unsafe { (*hi).text }, unsafe { (*hi).text_len }) };
                self.out.putc(b')');
                return;
            }
            /* Some(inner) */
            if nk >= 1 && unsafe { (**kids.add(0)).kind } == pm_jit_rsx_ast_kind::PATH {
                let leaf = unsafe { *kids.add(0) };
                let lt = unsafe { (*leaf).text };
                let ltl = unsafe { (*leaf).text_len };
                if unsafe { z_eq(lt, ltl, b"Some\0".as_ptr()) } && nk >= 2 {
                    let bind = unsafe { *kids.add(1) };
                    let bt = unsafe { (*bind).text };
                    let btl = unsafe { (*bind).text_len };
                    /* struct-shaped Option: presence is the tag, the bind
                     * (declared at the arm body top) copies ._v. */
                    if self.cur_opt_elem_len > 0 {
                        self.out.put(sv, sv_len);
                        self.out.puts(b"._has\0".as_ptr());
                        self.out.puts(b" /* Some(\0".as_ptr());
                        self.out.put(bt, btl);
                        self.out.puts(b") */\0".as_ptr());
                        return;
                    }
                    /* pointer nullity test; the bind is declared at the top
                     * of the arm body by the caller (emit_match knows). */
                    self.out.put(sv, sv_len);
                    self.out.puts(b" != 0\0".as_ptr());
                    self.out.puts(b" /* Some(\0".as_ptr());
                    self.out.put(bt, btl);
                    self.out.puts(b") */\0".as_ptr());
                    return;
                }
                /* call-shaped Some path never reaches here: Some(x) parses
                 * as PATH kids [Some, x] — covered above. */
            }
            if unsafe { z_eq(t, tl, b"None\0".as_ptr()) } {
                if self.cur_opt_elem_len > 0 {
                    self.out.put(sv, sv_len);
                    self.out.puts(b"._has == 0\0".as_ptr());
                    return;
                }
                self.out.put(sv, sv_len);
                self.out.puts(b" == 0\0".as_ptr());
                return;
            }
            /* Ok(bind) / Err(bind) — the Result arms. Presence is `._ok`;
             * the bind (declared at the arm body top by emit_match's
             * some_bind path) copies `._v` (Ok) or `._e` (Err). */
            if nk >= 1
                && unsafe { (**kids.add(0)).kind } == pm_jit_rsx_ast_kind::PATH
                && (unsafe { z_eq(unsafe { (**kids.add(0)).text }, unsafe { (**kids.add(0)).text_len }, b"Ok\0".as_ptr()) }
                    || unsafe { z_eq(unsafe { (**kids.add(0)).text }, unsafe { (**kids.add(0)).text_len }, b"Err\0".as_ptr()) })
            {
                let is_ok = unsafe { z_eq(unsafe { (**kids.add(0)).text }, unsafe { (**kids.add(0)).text_len }, b"Ok\0".as_ptr()) };
                if is_ok {
                    self.out.put(sv, sv_len);
                    self.out.puts(b"._ok\0".as_ptr());
                } else {
                    self.out.put(sv, sv_len);
                    self.out.puts(b"._ok == 0\0".as_ptr());
                }
                if nk >= 2 {
                    let bind = unsafe { *kids.add(1) };
                    let bt = unsafe { (*bind).text };
                    let btl = unsafe { (*bind).text_len };
                    /* literal payload (`Ok(true)`, `Ok(0)`): VALUE compare —
                     * AND the presence test with the payload == literal.
                     * No bind is declared (emit_res_binds skips literals);
                     * without this the arm matched every Ok, not only the
                     * literal's. */
                    let is_true = btl == 4 && unsafe { z_eq(bt, 4, b"true\0".as_ptr()) };
                    let is_false = btl == 5 && unsafe { z_eq(bt, 5, b"false\0".as_ptr()) };
                    if is_true || is_false {
                        self.out.puts(b" && \0".as_ptr());
                        self.out.put(sv, sv_len);
                        if is_ok {
                            self.out.puts(b"._v == \0".as_ptr());
                        } else {
                            self.out.puts(b"._e == \0".as_ptr());
                        }
                        if is_true {
                            self.out.puts(b"true\0".as_ptr());
                        } else {
                            self.out.puts(b"false\0".as_ptr());
                        }
                        return;
                    }
                    let mut numeric = false;
                    if btl > 0 && !bt.is_null() {
                        let c0 = unsafe { *bt };
                        if c0 >= b'0' && c0 <= b'9' {
                            let mut j = 0usize;
                            numeric = true;
                            while j < btl {
                                let ch = unsafe { *bt.add(j) };
                                if !(ch >= b'0' && ch <= b'9') {
                                    numeric = false;
                                    break;
                                }
                                j += 1;
                            }
                        }
                    }
                    if numeric {
                        self.out.puts(b" && \0".as_ptr());
                        self.out.put(sv, sv_len);
                        if is_ok {
                            self.out.puts(b"._v == \0".as_ptr());
                        } else {
                            self.out.puts(b"._e == \0".as_ptr());
                        }
                        self.out.put(bt, btl);
                        return;
                    }
                    self.out.puts(b" /* \0".as_ptr());
                    self.out.put(unsafe { (**kids.add(0)).text }, 2);
                    self.out.putc(b'(');
                    self.out.put(bt, btl);
                    self.out.puts(b") */\0".as_ptr());
                }
                return;
            }
            if unsafe { z_eq(t, tl, b"_\0".as_ptr()) } {
                self.out.putc(b'1');
                return;
            }
            /* single-name path: enum variant or binding. Enum variants are
             * type-paths of 2+ segments (`State::Ready`); plain bindings are
             * single-segment and match anything. */
            if nk >= 2 {
                /* variant path — compare against VariantName */
                let leaf = unsafe { *kids.add(nk - 1) };
                /* joined variant name from all PATH segments — the LAST
                 * kid is the bind wrapper (PATH text `path` holding the
                 * binding name) when this arm carries a payload bind;
                 * the join stops before it. */
                let bind_is_last = unsafe { (*leaf).kind } == pm_jit_rsx_ast_kind::PATH
                    && unsafe { (*leaf).text_len } == 4
                    && !unsafe { (*leaf).text }.is_null()
                    && unsafe { z_eq(unsafe { (*leaf).text }, 4, b"path\0".as_ptr()) };
                let mut full = self.arena_tmp();
                let mut at = 0usize;
                let mut i = 0usize;
                while i < nk {
                    if i + 1 == nk && bind_is_last {
                        break;
                    }
                    let seg = unsafe { *kids.add(i) };
                    if unsafe { (*seg).kind } != pm_jit_rsx_ast_kind::PATH {
                        i += 1;
                        continue;
                    }
                    let st = unsafe { (*seg).text };
                    let sl = unsafe { (*seg).text_len };
                    /* the bind segment: parser marks it `pat` */
                    if sl == 3 && !st.is_null() && unsafe { z_eq(st, sl, b"pat\0".as_ptr()) } {
                        i += 1;
                        continue;
                    }
                    if at > 0 {
                        at = unsafe { bput(full, 128, at, b"_\0".as_ptr(), 1) };
                    }
                    at = unsafe { bput(full, 128, at, st, sl) };
                    i += 1;
                }
                unsafe {
                    *full.add(at) = 0;
                }
                /* payload variant: the test is the tag compare — the
                 * tagged-union shape. The bind's copy (`._u.<Variant>`)
                 * is declared at the arm body top by emit_match. */
                {
                    let pb = self.arena_tmp();
                    let pn = unsafe { self.enumpays.lookup(full, at, pb, 96) };
                    if pn > 0 {
                        self.out.put(sv, sv_len);
                        self.out.puts(b"._tag == \0".as_ptr());
                        self.out.put(full, at);
                        return;
                    }
                    /* fieldless variant of a TAGGED enum: same tag
                     * compare (the C value is the struct, never a bare
                     * int). The enum name is the first joined segment. */
                    if at > 0 {
                        let mut fe = 0usize;
                        while fe < at && unsafe { *full.add(fe) } != b'_' {
                            fe += 1;
                        }
                        if fe > 0 && unsafe { self.enumtags.has(full, fe) } {
                            self.out.put(sv, sv_len);
                            self.out.puts(b"._tag == \0".as_ptr());
                            self.out.put(full, at);
                            return;
                        }
                    }
                }
                self.out.put(sv, sv_len);
                self.out.puts(b" == \0".as_ptr());
                self.out.put(full, at);
                let _ = leaf;
                return;
            }
            if nk == 1 && unsafe { (**kids.add(0)).kind } == pm_jit_rsx_ast_kind::STRUCT_LIT {
                unsafe {
                    self.err(b"unsupported: struct pattern\0".as_ptr(), unsafe { (*pat).line });
                }
                return;
            }
            /* single-segment name in pattern position: enum variant with the
             * enum elided (Rust allows it inside a match on that type), a
             * plain binding (match anything), or a UNIT STATIC (`pub const
             * X: &str`) — a const pattern comparing the static's VALUE. */
            if nk == 0 && tl > 0 && !t.is_null() {
                let sb = self.arena_tmp();
                let sl = unsafe { self.st_find(t, tl, sb, 128) };
                if sl > 0 {
                    /* &str-typed const over an &str scrutinee: the view
                     * compare (length + memcmp), the literal shape. */
                    if sl == 13
                        && unsafe { z_eq(sb, 13, b"rsx_str_ref_t\0".as_ptr()) }
                        && self.cur_scrut_len == 13
                        && unsafe { z_eq(self.cur_scrut.as_ptr(), 13, b"rsx_str_ref_t\0".as_ptr()) }
                    {
                        self.str_ref_used = true;
                        self.out.putc(b'(');
                        self.out.put(sv, sv_len);
                        self.out.puts(b".n == \0".as_ptr());
                        self.out.put(t, tl);
                        self.out.puts(b".n && !memcmp(\0".as_ptr());
                        self.out.put(sv, sv_len);
                        self.out.puts(b".p, \0".as_ptr());
                        self.out.put(t, tl);
                        self.out.puts(b".p, \0".as_ptr());
                        self.out.put(sv, sv_len);
                        self.out.puts(b".n))\0".as_ptr());
                        return;
                    }
                    /* scalar-typed const: the plain == */
                    self.out.put(sv, sv_len);
                    self.out.puts(b" == \0".as_ptr());
                    self.out.put(t, tl);
                    return;
                }
            }
            self.out.putc(b'1');
            return;
        }
        /* tuple pattern `(p0, p1, ..)`: and-join of the element tests,
         * each against the tuple scrutinee's `_i` field. Literal string
         * elements route through the same LITERAL case below (recursed),
         * so the view compare fires with the element's own ctype once the
         * sub-test re-enters with the element spelling. */
        if kind == pm_jit_rsx_ast_kind::TUPLE {
            let kids = unsafe { (*pat).kids };
            let nk = unsafe { (*pat).n_kids } as usize;
            if nk == 0 {
                /* `()` matches the unit — always true */
                self.out.putc(b'1');
                return;
            }
            let mut i = 0usize;
            while i < nk {
                if i > 0 {
                    self.out.puts(b" && \0".as_ptr());
                }
                let sub = unsafe { *kids.add(i) };
                let sk = unsafe { (*sub).kind };
                if sk == pm_jit_rsx_ast_kind::LITERAL
                    || sk == pm_jit_rsx_ast_kind::UNARY
                    || sk == pm_jit_rsx_ast_kind::ARRAY
                    || sk == pm_jit_rsx_ast_kind::TUPLE
                {
                    /* value pattern: recurse with the element spelling as
                     * the scrutinee — the literal/string compare then runs
                     * against the element expression. */
                    let eb = self.arena_tmp();
                    let mut at = 0usize;
                    at = unsafe { bput(eb, 64, at, b"(\0".as_ptr(), 1) };
                    at = unsafe { bput(eb, 64, at, sv, sv_len) };
                    at = unsafe { bput(eb, 64, at, b")._\0".as_ptr(), 3) };
                    let mut digs = [0u8; 12];
                    let mut v = i as u64;
                    let mut di = 0usize;
                    while v > 0 && di < 12 {
                        digs[di] = b'0' + (v % 10) as u8;
                        v /= 10;
                        di += 1;
                    }
                    if di == 0 {
                        digs[0] = b'0';
                        di = 1;
                    }
                    let mut j = di;
                    while j > 0 {
                        j -= 1;
                        at = unsafe { bput(eb, 64, at, digs.as_ptr().add(j), 1) };
                    }
                    let saved_scrut: [u8; 128] = self.cur_scrut;
                    let saved_scrut_len = self.cur_scrut_len;
                    /* element ctype: a string-literal element against an
                     * &str field needs the view compare — probe the field's
                     * type by rendering the scrutinee's tuple row. The
                     * honest minimal: re-run emit_pat_test with the element
                     * spelling, with cur_scrut pointing at the field type
                     * when the row is known (cur_tuple_elems). */
                    let field_buf = self.arena_tmp();
                    let flen = unsafe {
                        self.tup_row_elem_ctype(self.cur_scrut.as_ptr(), self.cur_scrut_len, i, field_buf, 128)
                    };
                    if flen > 0 && flen < 128 {
                        unsafe {
                            core::ptr::copy_nonoverlapping(
                                field_buf,
                                self.cur_scrut.as_mut_ptr(),
                                flen,
                            );
                        }
                        self.cur_scrut_len = flen;
                    }
                    unsafe { self.emit_pat_test(sub, eb, at, locals) };
                    self.cur_scrut = saved_scrut;
                    self.cur_scrut_len = saved_scrut_len;
                } else {
                    /* binding element: matches anything (the bind itself
                     * is declared by the arm machinery elsewhere) */
                    self.out.putc(b'1');
                }
                i += 1;
            }
            return;
        }
        /* slice pattern `[p0, p1, ..]` against an rsx_arr row: length
         * equality plus per-element tests on `W.p[i]`, with W the
         * scrutinee wrapped once. An empty `[]` tests `.n == 0`. */
        if kind == pm_jit_rsx_ast_kind::ARRAY {
            let kids = unsafe { (*pat).kids };
            let nk = unsafe { (*pat).n_kids } as usize;
            let wb = self.arena_tmp();
            let mut wat = 0usize;
            wat = unsafe { bput(wb, 72, 0, b"(\0".as_ptr(), 1) };
            wat = unsafe { bput(wb, 72, wat, sv, sv_len) };
            wat = unsafe { bput(wb, 72, wat, b")\0".as_ptr(), 1) };
            self.out.putc(b'(');
            self.out.put(wb, wat);
            self.out.puts(b".n == \0".as_ptr());
            self.out.put_u32(nk as u32);
            let mut i = 0usize;
            while i < nk {
                self.out.puts(b" && \0".as_ptr());
                let sub = unsafe { *kids.add(i) };
                let sk = unsafe { (*sub).kind };
                if sk == pm_jit_rsx_ast_kind::LITERAL {
                    /* string-literal element: view compare against
                     * `W.p[i]` (an rsx_str_ref_t row) */
                    let pt = unsafe { (*sub).text };
                    let ptl = unsafe { (*sub).text_len };
                    if ptl >= 2 && !pt.is_null() && unsafe { *pt } == b'"' {
                        self.str_ref_used = true;
                        let inner = ptl - 2;
                        self.out.putc(b'(');
                        self.out.put(wb, wat);
                        self.out.puts(b".p[\0".as_ptr());
                        self.out.put_u32(i as u32);
                        self.out.puts(b"].n == \0".as_ptr());
                        self.out.put_u32(unsafe { Lower::c_str_len(pt.add(1), inner) });
                        self.out.puts(b" && !memcmp(\0".as_ptr());
                        self.out.put(wb, wat);
                        self.out.puts(b".p[\0".as_ptr());
                        self.out.put_u32(i as u32);
                        self.out.puts(b"].p, \0".as_ptr());
                        self.out.put(pt, ptl);
                        self.out.puts(b", \0".as_ptr());
                        self.out.put_u32(unsafe { Lower::c_str_len(pt.add(1), inner) });
                        self.out.puts(b"))\0".as_ptr());
                    } else {
                        self.out.putc(b'(');
                        self.out.put(wb, wat);
                        self.out.puts(b".p[\0".as_ptr());
                        self.out.put_u32(i as u32);
                        self.out.puts(b"] == \0".as_ptr());
                        unsafe { self.emit_literal(pt, ptl) };
                        self.out.putc(b')');
                    }
                } else if sk == pm_jit_rsx_ast_kind::PATH {
                    /* binding element: matches any element */
                    self.out.putc(b'1');
                } else {
                    unsafe {
                        self.err(b"unsupported: slice pattern element\0".as_ptr(), unsafe { (*sub).line });
                    }
                    return;
                }
                i += 1;
            }
            self.out.putc(b')');
            return;
        }
        unsafe {
            self.err(b"unsupported: pattern form\0".as_ptr(), unsafe { (*pat).line });
        }
    }

    /* Emit "rsx_lbl_<name>" (suffix "_cont"/"_end") from a label node text
     * — C labels share the ordinary identifier namespace with locals, so
     * the rsx_lbl_ prefix keeps `'outer` from colliding with an `outer`. */
    unsafe fn put_lbl(&mut self, t: *const u8, tl: usize, suffix: *const u8) {
        let mut i = 0usize;
        if tl > 0 && !t.is_null() && unsafe { *t.add(0) } == b'\'' {
            i = 1;
        }
        self.out.puts(b"rsx_lbl_\0".as_ptr());
        if tl > i {
            self.out.put(t.add(i), tl - i);
        }
        self.out.puts(suffix);
    }

    /* Is this loop node labeled? (label text starts with the quote). */
    unsafe fn loop_is_labeled(s: *const pm_jit_rsx_ast_t) -> bool {
        let t = unsafe { (*s).text };
        let tl = unsafe { (*s).text_len };
        tl > 0 && !t.is_null() && unsafe { *t == b'\'' }
    }

    unsafe fn emit_loop(&mut self, s: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        let kids = unsafe { (*s).kids };
        if unsafe { (*s).n_kids } < 1 {
            return;
        }
        let body = unsafe { *kids.add(0) };
        let labeled = unsafe { Lower::loop_is_labeled(s) };
        let lt = unsafe { (*s).text };
        let ll = unsafe { (*s).text_len };
        self.indent();
        if labeled {
            unsafe { self.put_lbl(lt, ll, b"_\0".as_ptr()) };
            self.out.puts(b": \0".as_ptr());
        }
        self.out.puts(b"for (;;) {\n\0".as_ptr());
        self.depth += 1;
        if unsafe { (*body).kind } == pm_jit_rsx_ast_kind::BLOCK {
            unsafe { self.emit_block_stmt(body, locals) };
        } else {
            /* a bare statement body — the while-let desugar's MATCH (no
             * source braces to borrow); emit it statement-form inside
             * the loop's own C braces. */
            unsafe { self.emit_stmt(body, locals, 0) };
        }
        self.depth -= 1;
        if labeled {
            self.indent();
            unsafe { self.put_lbl(lt, ll, b"_cont\0".as_ptr()) };
            self.out.puts(b": ;\n\0".as_ptr());
        }
        self.indent();
        self.out.puts(b"}\n\0".as_ptr());
        if labeled {
            self.indent();
            unsafe { self.put_lbl(lt, ll, b"_end\0".as_ptr()) };
            self.out.puts(b": ;\n\0".as_ptr());
        }
    }

    unsafe fn emit_while(&mut self, s: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        let kids = unsafe { (*s).kids };
        if unsafe { (*s).n_kids } < 2 {
            return;
        }
        let cond = unsafe { *kids.add(0) };
        let body = unsafe { *kids.add(1) };
        let labeled = unsafe { Lower::loop_is_labeled(s) };
        let lt = unsafe { (*s).text };
        let ll = unsafe { (*s).text_len };
        self.indent();
        if labeled {
            unsafe { self.put_lbl(lt, ll, b"_\0".as_ptr()) };
            self.out.puts(b": \0".as_ptr());
        }
        self.out.puts(b"while (\0".as_ptr());
        unsafe { self.emit_expr(cond, locals) };
        self.out.puts(b") {\n\0".as_ptr());
        self.depth += 1;
        unsafe { self.emit_block_stmt(body, locals) };
        self.depth -= 1;
        if labeled {
            self.indent();
            unsafe { self.put_lbl(lt, ll, b"_cont\0".as_ptr()) };
            self.out.puts(b": ;\n\0".as_ptr());
        }
        self.indent();
        self.out.puts(b"}\n\0".as_ptr());
        if labeled {
            self.indent();
            unsafe { self.put_lbl(lt, ll, b"_end\0".as_ptr()) };
            self.out.puts(b": ;\n\0".as_ptr());
        }
    }

    /* `for (i, &b) in X.iter().enumerate()` over a fixed array — returns 1
     * when emitted, 0 when the iterator shape is not enumerate (the range
     * refusal path handles that). X must be a value of array type (the
     * length is compile-time); slices refuse (no carried length). */
    /* `for bind in <slice>` where <slice> types as a &[T] fat row
     * (rsx_arr_<row>) — a slice param directly or `&vec` (the coercion
     * types the borrow as the row). The fat pair carries .n/.p — bind
     * element copies (field borrows inside the body take the copy's
     * address, loop-body lifetime exactly as the source). Returns 1
     * when handled, 0 when the receiver is not a slice row (fall
     * through to the array/other paths). */
    /* `for bind in [lit, lit, ...]` — an array literal of &str literals.
     * Interned as an rsx_arr_ row over rsx_str_ref_t: a static const
     * table at the loop head, then the shared .n/.p index loop. Only
     * string literals ride this plane (the common `for s in ["a", "b"]`
     * idiom); other element exprs refuse honestly below. */
    unsafe fn try_emit_for_arr_literal(
        &mut self,
        s: *const pm_jit_rsx_ast_t,
        pat: *const pm_jit_rsx_ast_t,
        recv: *const pm_jit_rsx_ast_t,
        body: *const pm_jit_rsx_ast_t,
        locals: *mut LocalTab,
    ) -> usize {
        let kids = unsafe { (*recv).kids };
        let nk = unsafe { (*recv).n_kids } as usize;
        if nk == 0 || nk > 9 {
            return 0;
        }
        /* every element must be a string literal (LITERAL starting '"') */
        let mut k = 0usize;
        while k < nk {
            let e = unsafe { *kids.add(k) };
            if unsafe { (*e).kind } != pm_jit_rsx_ast_kind::LITERAL {
                return 0;
            }
            let tl = unsafe { (*e).text_len };
            if tl < 2 || unsafe { *(*e).text } != b'"' {
                return 0;
            }
            k += 1;
        }
        /* bind: PATH (unwrap the "path" wrapper) */
        let mut bind = pat;
        if unsafe { (*pat).kind } == pm_jit_rsx_ast_kind::PATH
            && unsafe { z_eq(unsafe { (*pat).text }, unsafe { (*pat).text_len }, b"path\0".as_ptr()) }
            && unsafe { (*pat).n_kids } as usize == 1
        {
            let seg = unsafe { *(*pat).kids.add(0) };
            if unsafe { (*seg).kind } == pm_jit_rsx_ast_kind::PATH {
                bind = seg;
            }
        }
        if unsafe { (*bind).kind } != pm_jit_rsx_ast_kind::PATH {
            return 0;
        }
        let vname = unsafe { (*bind).text };
        let vlen = unsafe { (*bind).text_len };
        /* intern the rsx_arr_ row over rsx_str_ref_t elements */
        let row_slot = unsafe { self.arrs.intern(b"rsx_str_ref_t\0".as_ptr(), 13) };
        if row_slot >= ARR_CAP {
            unsafe {
                self.err(b"internal: arr row overflow\0".as_ptr(), unsafe { (*s).line });
            }
            return 1;
        }
        let row = self.arena_tmp();
        let row_len = unsafe { ArrTab::name_for(row_slot, row, 96) };
        if row_len == 0 {
            unsafe {
                self.err(b"internal: arr row name\0".as_ptr(), unsafe { (*s).line });
            }
            return 1;
        }
        self.str_ref_used = true;
        unsafe {
            (*locals).add(vname, vlen, row, row_len, self.depth + 1);
        }
        let labeled = unsafe { Lower::loop_is_labeled(s) };
        let lt = unsafe { (*s).text };
        let ll = unsafe { (*s).text_len };
        self.indent();
        if labeled {
            unsafe { self.put_lbl(lt, ll, b"_\0".as_ptr()) };
            self.out.puts(b": \0".as_ptr());
        }
        /* static const table of {p, n} pairs — one per literal. The C
         * byte length is the literal's own char count minus the quotes
         * and escapes stay escapes (ASCII plane). */
        self.out.puts(b"static const rsx_str_ref_t __rsx_al[\0".as_ptr());
        {
            let d = b'0' + nk as u8;
            self.out.putc(d);
        }
        self.out.puts(b"] = {\0".as_ptr());
        let mut i = 0usize;
        while i < nk {
            if i > 0 {
                self.out.puts(b", \0".as_ptr());
            }
            self.out.puts(b"{ (const uint8_t *)\0".as_ptr());
            let e = unsafe { *kids.add(i) };
            /* emit the literal text whole (quotes included) — same
             * pass-through emit_literal uses for strings */
            self.out.put(unsafe { (*e).text }, unsafe { (*e).text_len });
            self.out.puts(b", \0".as_ptr());
            /* byte length: the quote-stripped, escape-folded span */
            let bl = unsafe {
                Lower::c_str_len(
                    unsafe { (*e).text.add(1) },
                    unsafe { (*e).text_len.saturating_sub(2) },
                )
            };
            {
                let mut tmp: [u8; 12] = [0; 12];
                let mut wl = 0usize;
                if bl == 0 {
                    tmp[0] = b'0';
                    wl = 1;
                } else {
                    let mut v = bl;
                    while v > 0 {
                        let d = b'0' + (v % 10) as u8;
                        wl += 1;
                        tmp[12 - wl] = d;
                        v /= 10;
                    }
                }
                self.out.put(tmp.as_ptr().add(12 - wl), wl);
            }
            self.out.puts(b" }\0".as_ptr());
            i += 1;
        }
        self.out.puts(b"};\n\0".as_ptr());
        self.indent();
        self.out.puts(b"for (size_t __rsx_fi = 0; __rsx_fi < \0".as_ptr());
        {
            let d = b'0' + nk as u8;
            self.out.putc(d);
        }
        self.out.puts(b"; __rsx_fi++) {\n\0".as_ptr());
        self.depth += 1;
        self.indent();
        self.out.put(row, row_len);
        self.out.putc(b' ');
        self.out.put(vname, vlen);
        self.out.puts(b" = __rsx_al[__rsx_fi];\n\0".as_ptr());
        unsafe { self.emit_block_stmt(body, locals) };
        self.depth -= 1;
        self.indent();
        self.out.puts(b"}\n\0".as_ptr());
        if labeled {
            self.indent();
            unsafe { self.put_lbl(lt, ll, b"_cont\0".as_ptr()) };
            self.out.puts(b": ;\n\0".as_ptr());
            self.indent();
            unsafe { self.put_lbl(lt, ll, b"_end\0".as_ptr()) };
            self.out.puts(b": ;\n\0".as_ptr());
        }
        1
    }

    /* `for bind in env::args()` / `env::args().skip(N)` — the argv walk.
     * The receiver chain is METHOD_CALL(skip) over CALL(env::args) (or a
     * bare CALL). Each bind is a fresh rsx_str_t copy of the C argv
     * string — Rust's Args yields owned Strings. The argv/argc globals
     * ride the prelude (set once by the seat's main shim; a freestanding
     * seat with no argv sees an empty walk). */
    unsafe fn try_emit_for_args(
        &mut self,
        s: *const pm_jit_rsx_ast_t,
        pat: *const pm_jit_rsx_ast_t,
        iter: *const pm_jit_rsx_ast_t,
        body: *const pm_jit_rsx_ast_t,
        locals: *mut LocalTab,
    ) -> usize {
        /* unwrap `.skip(N)` / `.skip(1)` */
        let mut skip_n: u64 = 0;
        let mut call = iter;
        if unsafe { (*iter).kind } == pm_jit_rsx_ast_kind::METHOD_CALL {
            let ik = unsafe { (*iter).kids };
            if unsafe { (*iter).n_kids } as usize >= 3 {
                let nm = unsafe { *ik.add(1) };
                let argsn = unsafe { *ik.add(2) };
                if unsafe { (*nm).text_len } == 4
                    && unsafe { z_eq(unsafe { (*nm).text }, 4, b"skip\0".as_ptr()) }
                    && (unsafe { (*argsn).n_kids } as usize) == 1
                {
                    let a0 = unsafe { *(*argsn).kids.add(0) };
                    if unsafe { (*a0).kind } != pm_jit_rsx_ast_kind::LITERAL {
                        return 0;
                    }
                    let t = unsafe { (*a0).text };
                    let tl = unsafe { (*a0).text_len };
                    let mut i = 0usize;
                    let mut ok = tl > 0;
                    while i < tl {
                        let ch = unsafe { *t.add(i) };
                        if ch < b'0' || ch > b'9' {
                            if ch == b'u' || ch == b'i' || ch == b'_' {
                                i += 1;
                                continue;
                            }
                            ok = false;
                            break;
                        }
                        skip_n = skip_n * 10 + (ch - b'0') as u64;
                        i += 1;
                    }
                    if !ok {
                        return 0;
                    }
                    call = unsafe { *ik.add(0) };
                } else {
                    return 0;
                }
            } else {
                return 0;
            }
        }
        /* receiver must be the env::args() call: CALL(PATH[std, env, args]) */
        if unsafe { (*call).kind } != pm_jit_rsx_ast_kind::CALL {
            return 0;
        }
        {
            let rname = unsafe { *(*call).kids.add(0) };
            if unsafe { (*rname).kind } != pm_jit_rsx_ast_kind::PATH {
                return 0;
            }
            let nseg = unsafe { (*rname).n_kids } as usize;
            if nseg < 2 {
                return 0;
            }
            let segs = unsafe { (*rname).kids };
            let last = unsafe { *segs.add(nseg - 1) };
            let prev = unsafe { *segs.add(nseg - 2) };
            if !(unsafe { (*last).kind } == pm_jit_rsx_ast_kind::PATH
                && unsafe { z_eq(unsafe { (*last).text }, unsafe { (*last).text_len }, b"args\0".as_ptr()) }
                && unsafe { (*prev).kind } == pm_jit_rsx_ast_kind::PATH
                && unsafe { z_eq(unsafe { (*prev).text }, unsafe { (*prev).text_len }, b"env\0".as_ptr()) })
            {
                return 0;
            }
        }
        /* bind: PATH (unwrap the "path" wrapper) */
        let mut bind = pat;
        if unsafe { (*pat).kind } == pm_jit_rsx_ast_kind::PATH
            && unsafe { z_eq(unsafe { (*pat).text }, unsafe { (*pat).text_len }, b"path\0".as_ptr()) }
            && unsafe { (*pat).n_kids } as usize == 1
        {
            let seg = unsafe { *(*pat).kids.add(0) };
            if unsafe { (*seg).kind } == pm_jit_rsx_ast_kind::PATH {
                bind = seg;
            }
        }
        if unsafe { (*bind).kind } != pm_jit_rsx_ast_kind::PATH {
            return 0;
        }
        let vname = unsafe { (*bind).text };
        let vlen = unsafe { (*bind).text_len };
        self.str_own_used = true;
        self.env_args_used = true;
        unsafe {
            (*locals).add(vname, vlen, b"rsx_str_t\0".as_ptr(), 9, self.depth + 1);
        }
        let labeled = unsafe { Lower::loop_is_labeled(s) };
        let lt = unsafe { (*s).text };
        let ll = unsafe { (*s).text_len };
        self.indent();
        if labeled {
            unsafe { self.put_lbl(lt, ll, b"_\0".as_ptr()) };
            self.out.puts(b": \0".as_ptr());
        }
        self.out.puts(b"for (int __rsx_ai = \0".as_ptr());
        {
            let mut tmp: [u8; 12] = [0; 12];
            let mut wl = 0usize;
            if skip_n == 0 {
                tmp[0] = b'0';
                wl = 1;
            } else {
                let mut v = skip_n;
                while v > 0 {
                    let d = b'0' + (v % 10) as u8;
                    wl += 1;
                    tmp[12 - wl] = d;
                    v /= 10;
                }
            }
            self.out.put(tmp.as_ptr().add(12 - wl), wl);
        }
        self.out.puts(b"; __rsx_ai < __rsx_argc; __rsx_ai++) {\n\0".as_ptr());
        self.depth += 1;
        self.indent();
        self.out.puts(b"rsx_str_t \0".as_ptr());
        self.out.put(vname, vlen);
        self.out.puts(b" = {0}; rsx_str_push_lit(&\0".as_ptr());
        self.out.put(vname, vlen);
        self.out.puts(b", __rsx_argv[__rsx_ai]);\n\0".as_ptr());
        unsafe { self.emit_block_stmt(body, locals) };
        self.depth -= 1;
        self.indent();
        self.out.puts(b"}\n\0".as_ptr());
        if labeled {
            self.indent();
            unsafe { self.put_lbl(lt, ll, b"_cont\0".as_ptr()) };
            self.out.puts(b": ;\n\0".as_ptr());
            self.indent();
            unsafe { self.put_lbl(lt, ll, b"_end\0".as_ptr()) };
            self.out.puts(b": ;\n\0".as_ptr());
        }
        1
    }

    unsafe fn try_emit_for_arr(
        &mut self,
        s: *const pm_jit_rsx_ast_t,
        pat: *const pm_jit_rsx_ast_t,
        recv: *const pm_jit_rsx_ast_t,
        body: *const pm_jit_rsx_ast_t,
        locals: *mut LocalTab,
    ) -> usize {
        /* An ARRAY literal (`for sub in ["src", "examples"]`) types as
         * `const char *[]` — not a fat row. Lower it to an rsx_arr_ row
         * of rsx_str_ref_t elements: a static const table the loop
         * walks .n/.p over, same shape as a slice param. */
        if unsafe { (*recv).kind } == pm_jit_rsx_ast_kind::ARRAY
            && unsafe { self.try_emit_for_arr_literal(s, pat, recv, body, locals) } != 0
        {
            return 1;
        }
        let rct = self.arena_tmp();
        let rct_len = unsafe { self.expr_ctype(recv, rct, 128, locals) };
        if rct_len == 0 {
            return 0;
        }
        /* element spelling + row: &[T] fat rows and Vec rows both carry
         * .n/.p — the loop shape is shared. */
        let mut el: usize = 0;
        let mut eb: *mut u8 = core::ptr::null_mut();
        if rct_len > 8 && unsafe { z_eq(rct, 8, b"rsx_arr_\0".as_ptr()) } {
            let rs = unsafe { self.arrs.find_by_name(rct, rct_len) };
            if rs < ARR_CAP {
                el = self.arrs.elem_lens[rs];
                if el > 0 && el < 128 {
                    eb = self.arena_tmp();
                    let mut w = 0usize;
                    while w < el {
                        unsafe {
                            *eb.add(w) = self.arrs.elems[rs][w];
                        }
                        w += 1;
                    }
                    unsafe {
                        *eb.add(el) = 0;
                    }
                }
            }
        }
        if eb.is_null() && rct_len > 8 && unsafe { z_eq(rct, 8, b"rsx_vec_\0".as_ptr()) } {
            let vs = unsafe { self.vecs.find_by_name(rct, rct_len) };
            if vs < VEC_CAP {
                el = self.vecs.elem_lens[vs];
                if el > 0 && el < 128 {
                    eb = self.arena_tmp();
                    let mut w = 0usize;
                    while w < el {
                        unsafe {
                            *eb.add(w) = self.vecs.elems[vs][w];
                        }
                        w += 1;
                    }
                    unsafe {
                        *eb.add(el) = 0;
                    }
                }
            }
        }
        if eb.is_null() {
            if rct_len > 8
                && (unsafe { z_eq(rct, 8, b"rsx_arr_\0".as_ptr()) }
                    || unsafe { z_eq(rct, 8, b"rsx_vec_\0".as_ptr()) })
            {
                unsafe {
                    self.err(b"internal: unknown container row\0".as_ptr(), unsafe { (*s).line });
                }
                return 1;
            }
            return 0;
        }
        /* pat: a PATH bind, or a TUPLE destructure over a tuple-typed
         * element (e.g. `for (name, stub) in funcs` with
         * funcs: &[(String, String)] — each element an interned
         * rsx_tuple_ row; binds declare from elem._N). */
        let mut tuple_pat: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
        if unsafe { (*pat).kind } == pm_jit_rsx_ast_kind::TUPLE {
            tuple_pat = pat;
        }
        if tuple_pat.is_null() && unsafe { (*pat).kind } != pm_jit_rsx_ast_kind::PATH {
            unsafe {
                self.err(b"unsupported: for-iter pattern\0".as_ptr(), unsafe { (*s).line });
            }
            return 1;
        }
        /* tuple element: the row must be an interned tuple signature */
        let mut tup_slot = TUP_CAP;
        let mut tup_n = 0usize;
        if !tuple_pat.is_null() {
            if !(el >= 10 && unsafe { z_eq(eb, 10, b"rsx_tuple_\0".as_ptr()) }) {
                unsafe {
                    self.err(b"unsupported: for-iter tuple pattern on a non-tuple element\0".as_ptr(), unsafe { (*s).line });
                }
                return 1;
            }
            tup_slot = unsafe { self.tup_find(eb, el) };
            tup_n = unsafe { (*tuple_pat).n_kids } as usize;
            if tup_slot >= TUP_CAP
                || tup_n == 0
                || tup_n > TUP_MAXF
                || self.tup_counts[tup_slot] != tup_n
            {
                unsafe {
                    self.err(b"unsupported: for-iter tuple pattern arity\0".as_ptr(), unsafe { (*s).line });
                }
                return 1;
            }
        }
        let mut bind = pat;
        let mut vname: *const u8 = b"\0".as_ptr();
        let mut vlen = 0usize;
        if tuple_pat.is_null() {
            if unsafe { z_eq(unsafe { (*pat).text }, unsafe { (*pat).text_len }, b"path\0".as_ptr()) }
                && unsafe { (*pat).n_kids } == 1
            {
                let pk = unsafe { (*pat).kids };
                let seg: *mut pm_jit_rsx_ast_t = unsafe { *pk.add(0) };
                if (unsafe { (*seg).kind }) == pm_jit_rsx_ast_kind::PATH {
                    bind = seg;
                }
            }
            vname = unsafe { (*bind).text };
            vlen = unsafe { (*bind).text_len };
            unsafe {
                (*locals).add(vname, vlen, eb, el, self.depth + 1);
            }
        }
        let labeled = unsafe { Lower::loop_is_labeled(s) };
        let lt = unsafe { (*s).text };
        let ll = unsafe { (*s).text_len };
        self.indent();
        if labeled {
            unsafe { self.put_lbl(lt, ll, b"_\0".as_ptr()) };
            self.out.puts(b": \0".as_ptr());
        }
        self.out.puts(b"for (size_t __rsx_fi = 0; __rsx_fi < (\0".as_ptr());
        unsafe { self.emit_expr(recv, locals) };
        self.out.puts(b").n; __rsx_fi++) {\n\0".as_ptr());
        self.depth += 1;
        if !tuple_pat.is_null() {
            /* one hidden temp for the element, then a bind per field */
            self.indent();
            self.out.put(eb, el);
            self.out.puts(b" __rsx_fe = (\0".as_ptr());
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b").p[__rsx_fi];\n\0".as_ptr());
            let pk2 = unsafe { (*tuple_pat).kids };
            let mut f2 = 0usize;
            while f2 < tup_n {
                let sub2 = unsafe { *pk2.add(f2) };
                let mut bnode2 = sub2;
                if unsafe { (*bnode2).kind } == pm_jit_rsx_ast_kind::PATH
                    && unsafe { z_eq((*bnode2).text, (*bnode2).text_len, b"path\0".as_ptr()) }
                    && unsafe { (*bnode2).n_kids } as usize == 1
                {
                    bnode2 = unsafe { *(*bnode2).kids.add(0) };
                }
                if unsafe { (*bnode2).kind } == pm_jit_rsx_ast_kind::PATH {
                    let bn2 = unsafe { (*bnode2).text };
                    let bl2 = unsafe { (*bnode2).text_len };
                    if !(bl2 == 1 && unsafe { z_eq(bn2, 1, b"_\0".as_ptr()) }) {
                        let a2 = tup_slot * TUP_MAXF + f2;
                        let elct = self.tup_elems[a2].as_ptr();
                        let elct_len = self.tup_lens[a2];
                        self.indent();
                        self.out.put(elct, elct_len);
                        self.out.putc(b' ');
                        self.out.put(bn2, bl2);
                        self.out.puts(b" = __rsx_fe._\0".as_ptr());
                        let d = b'0' + f2 as u8;
                        self.out.putc(d);
                        self.out.puts(b";\n\0".as_ptr());
                        unsafe {
                            (*locals).add(bn2, bl2, elct, elct_len, self.depth);
                        }
                    }
                } else {
                    unsafe {
                        self.err(b"unsupported: for-iter tuple pattern element\0".as_ptr(), unsafe { (*s).line });
                    }
                    return 1;
                }
                f2 += 1;
            }
        } else {
            self.indent();
            self.out.put(eb, el);
            self.out.putc(b' ');
            self.out.put(vname, vlen);
            self.out.puts(b" = (\0".as_ptr());
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b").p[__rsx_fi];\n\0".as_ptr());
        }
        unsafe { self.emit_block_stmt(body, locals) };
        self.depth -= 1;
        self.indent();
        self.out.puts(b"}\n\0".as_ptr());
        if labeled {
            self.indent();
            unsafe { self.put_lbl(lt, ll, b"_cont\0".as_ptr()) };
            self.out.puts(b": ;\n\0".as_ptr());
            self.indent();
            unsafe { self.put_lbl(lt, ll, b"_end\0".as_ptr()) };
            self.out.puts(b": ;\n\0".as_ptr());
        }
        1
    }

    /* `for bind in X.iter()` / `X.iter_mut()` — X a fixed array (ctype
     * carries `[N]`). Emits an index loop whose bind is a pointer to the
     * element, so bodies read and write through it (const elided — a
     * documented divergence; C's array-of-struct element pointer is the
     * same for iter and iter_mut). Non-array receivers (slices) refuse.
     * Returns 1 when handled, 0 to fall through. */
    unsafe fn try_emit_for_iter(
        &mut self,
        s: *const pm_jit_rsx_ast_t,
        pat: *const pm_jit_rsx_ast_t,
        iter: *const pm_jit_rsx_ast_t,
        body: *const pm_jit_rsx_ast_t,
        locals: *mut LocalTab,
    ) -> usize {
        let ik = unsafe { (*iter).kids };
        let ink = unsafe { (*iter).n_kids } as usize;
        if ink < 2 {
            return 0;
        }
        let name = unsafe { *ik.add(1) };
        let mname = unsafe { (*name).text };
        let mlen = unsafe { (*name).text_len };
        if !(unsafe { z_eq(mname, mlen, b"iter\0".as_ptr()) }
            || unsafe { z_eq(mname, mlen, b"iter_mut\0".as_ptr()) })
        {
            return 0;
        }
        let recv = unsafe { *ik.add(0) };
        let rct = self.arena_tmp();
        let rct_len = unsafe { self.expr_ctype(recv, rct, 128, locals) };
        if rct_len == 0 {
            unsafe {
                self.err(b"cannot infer iter receiver type\0".as_ptr(), unsafe { (*s).line });
            }
            return 1;
        }
        /* &[T] slice ref (rsx_arr_<row>) receiver — the shared loop */
        if unsafe { self.try_emit_for_arr(s, pat, recv, body, locals) } != 0 || !self.ok {
            return 1;
        }
        /* find [N] — the element spelling is before the bracket, N after */
        let mut br = 0usize;
        let mut i = 0usize;
        while i < rct_len {
            if unsafe { *rct.add(i) } == b'[' {
                br = i;
                break;
            }
            i += 1;
        }
        if i >= rct_len {
            /* not an array — a slice/pointer: no length, refuse */
            unsafe {
                self.err(b"unsupported: iter over non-array (slice has no length)\0".as_ptr(), unsafe { (*s).line });
            }
            return 1;
        }
        let mut j = br + 1;
        while j < rct_len && unsafe { *rct.add(j) } != b']' {
            j += 1;
        }
        if j >= rct_len {
            unsafe {
                self.err(b"internal: array ctype has no ']'\0".as_ptr(), unsafe { (*s).line });
            }
            return 1;
        }
        /* pat: a PATH bind — unwrap the "path" wrapper like the range case */
        if unsafe { (*pat).kind } != pm_jit_rsx_ast_kind::PATH {
            unsafe {
                self.err(b"unsupported: for-iter pattern\0".as_ptr(), unsafe { (*s).line });
            }
            return 1;
        }
        let mut bind = pat;
        if unsafe { z_eq(unsafe { (*pat).text }, unsafe { (*pat).text_len }, b"path\0".as_ptr()) }
            && unsafe { (*pat).n_kids } == 1
        {
            let pk = unsafe { (*pat).kids };
            let seg: *mut pm_jit_rsx_ast_t = unsafe { *pk.add(0) };
            if (unsafe { (*seg).kind }) == pm_jit_rsx_ast_kind::PATH {
                bind = seg;
            }
        }
        let vname = unsafe { (*bind).text };
        let vlen = unsafe { (*bind).text_len };
        /* element pointer type: elem-spelling + " *" */
        let ect = self.arena_tmp();
        let mut e2 = 0usize;
        while e2 < br {
            unsafe {
                *ect.add(e2) = *rct.add(e2);
            }
            e2 += 1;
        }
        unsafe {
            *ect.add(e2) = b' ';
            *ect.add(e2 + 1) = b'*';
            *ect.add(e2 + 2) = 0;
        }
        unsafe {
            (*locals).add(vname, vlen, ect, e2 + 2, self.depth + 1);
        }
        /* __rsx_fi index local (fresh name; nested loops nest by C scope) */
        let idx = b"__rsx_fi\0".as_ptr();
        let idx_len = 8usize;
        unsafe {
            (*locals).add(idx, idx_len, b"size_t\0".as_ptr(), 7, self.depth + 1);
        }
        let labeled = unsafe { Lower::loop_is_labeled(s) };
        let lt = unsafe { (*s).text };
        let ll = unsafe { (*s).text_len };
        self.indent();
        if labeled {
            unsafe { self.put_lbl(lt, ll, b"_\0".as_ptr()) };
            self.out.puts(b": \0".as_ptr());
        }
        self.out.puts(b"for (size_t __rsx_fi = 0; __rsx_fi < \0".as_ptr());
        self.out.put(rct.add(br + 1), (j - br - 1) as usize);
        self.out.puts(b"; __rsx_fi++) {\n\0".as_ptr());
        self.depth += 1;
        self.indent();
        self.out.put(ect, e2 + 2);
        self.out.putc(b' ');
        self.out.put(vname, vlen);
        self.out.puts(b" = &\0".as_ptr());
        self.out.putc(b'(');
        unsafe { self.emit_expr(recv, locals) };
        self.out.puts(b")[__rsx_fi];\n\0".as_ptr());
        unsafe { self.emit_block_stmt(body, locals) };
        self.depth -= 1;
        self.indent();
        self.out.puts(b"}\n\0".as_ptr());
        if labeled {
            self.indent();
            unsafe { self.put_lbl(lt, ll, b"_cont\0".as_ptr()) };
            self.out.puts(b": ;\n\0".as_ptr());
            self.indent();
            unsafe { self.put_lbl(lt, ll, b"_end\0".as_ptr()) };
            self.out.puts(b": ;\n\0".as_ptr());
        }
        1
    }

    /* `for (i, x) in <vec-expr>.into_iter().enumerate()` — an index loop
     * over the Vec row: i is the index, x a copy of .p[i] (or &x a
     * pointer to it). The vec expr is emitted twice (once for .n, once
     * for .p) — the subset keeps call exprs side-effect-free. */
    unsafe fn emit_for_enumerate_vec(
        &mut self,
        s: *const pm_jit_rsx_ast_t,
        pat: *const pm_jit_rsx_ast_t,
        body: *const pm_jit_rsx_ast_t,
        locals: *mut LocalTab,
        base: *const pm_jit_rsx_ast_t,
        row: usize,
    ) -> usize {
        if unsafe { (*pat).kind } != pm_jit_rsx_ast_kind::TUPLE
            || unsafe { (*pat).n_kids } as usize != 2
        {
            unsafe {
                self.err(b"unsupported: enumerate pattern (expect (i, x) or (i, &x))\0".as_ptr(), unsafe { (*s).line });
            }
            return 1;
        }
        let pk = unsafe { (*pat).kids };
        let idx_pat = unsafe { *pk.add(0) };
        let val_pat = unsafe { *pk.add(1) };
        let mut idx_name = idx_pat;
        if unsafe { (*idx_name).kind } == pm_jit_rsx_ast_kind::PATH
            && unsafe { z_eq((*idx_name).text, (*idx_name).text_len, b"path\0".as_ptr()) }
            && unsafe { (*idx_name).n_kids } as usize == 1
        {
            idx_name = unsafe { *(*idx_name).kids.add(0) };
        }
        if unsafe { (*idx_name).kind } != pm_jit_rsx_ast_kind::PATH {
            unsafe {
                self.err(b"unsupported: enumerate index pattern\0".as_ptr(), unsafe { (*s).line });
            }
            return 1;
        }
        let iname2 = unsafe { (*idx_name).text };
        let inl2 = unsafe { (*idx_name).text_len };
        let mut is_ref = false;
        let mut val_name = val_pat;
        if unsafe { (*val_name).kind } == pm_jit_rsx_ast_kind::PATH
            && unsafe { z_eq((*val_name).text, (*val_name).text_len, b"path\0".as_ptr()) }
            && unsafe { (*val_name).n_kids } as usize == 1
        {
            val_name = unsafe { *(*val_name).kids.add(0) };
        }
        if unsafe { (*val_name).kind } == pm_jit_rsx_ast_kind::UNARY
            && unsafe { z_eq((*val_name).text, (*val_name).text_len, b"&\0".as_ptr()) }
            && unsafe { (*val_name).n_kids } as usize >= 1
        {
            let inner = unsafe { *(*val_name).kids.add(0) };
            let mut inner2 = inner;
            if unsafe { (*inner2).kind } == pm_jit_rsx_ast_kind::PATH
                && unsafe { z_eq((*inner2).text, (*inner2).text_len, b"path\0".as_ptr()) }
                && unsafe { (*inner2).n_kids } as usize == 1
            {
                inner2 = unsafe { *(*inner2).kids.add(0) };
            }
            if unsafe { (*inner2).kind } != pm_jit_rsx_ast_kind::PATH {
                unsafe {
                    self.err(b"unsupported: enumerate value pattern\0".as_ptr(), unsafe { (*s).line });
                }
                return 1;
            }
            is_ref = true;
            val_name = inner2;
        } else if unsafe { (*val_name).kind } != pm_jit_rsx_ast_kind::PATH {
            unsafe {
                self.err(b"unsupported: enumerate value pattern\0".as_ptr(), unsafe { (*s).line });
            }
            return 1;
        }
        let vname = unsafe { (*val_name).text };
        let vlen = unsafe { (*val_name).text_len };
        let elct = self.vecs.elems[row].as_ptr();
        let elct_len = self.vecs.elem_lens[row];
        let labeled = unsafe { Lower::loop_is_labeled(s) };
        let lt = unsafe { (*s).text };
        let ll = unsafe { (*s).text_len };
        let ivar = self.arena_tmp();
        let mut at = unsafe { bput(ivar, 160, 0, iname2, inl2) };
        at = unsafe { bput(ivar, 160, at, b"_i\0".as_ptr(), 2) };
        unsafe {
            *ivar.add(at) = 0;
        }
        self.indent();
        if labeled {
            unsafe { self.put_lbl(lt, ll, b"_\0".as_ptr()) };
            self.out.puts(b": \0".as_ptr());
        }
        self.out.puts(b"for (size_t \0".as_ptr());
        self.out.put(ivar, at);
        self.out.puts(b" = 0; \0".as_ptr());
        self.out.put(ivar, at);
        self.out.puts(b" < (\0".as_ptr());
        unsafe { self.emit_expr(base, locals) };
        self.out.puts(b").n; \0".as_ptr());
        self.out.put(ivar, at);
        self.out.puts(b"++) {\n\0".as_ptr());
        self.depth += 1;
        self.indent();
        self.out.puts(b"size_t \0".as_ptr());
        self.out.put(iname2, inl2);
        self.out.puts(b" = \0".as_ptr());
        self.out.put(ivar, at);
        self.out.puts(b";\n\0".as_ptr());
        unsafe {
            (*locals).add(iname2, inl2, b"size_t\0".as_ptr(), 6, self.depth);
        }
        self.indent();
        if is_ref {
            self.out.put(elct, elct_len);
            self.out.puts(b" *\0".as_ptr());
            self.out.put(vname, vlen);
            self.out.puts(b" = &(\0".as_ptr());
            unsafe { self.emit_expr(base, locals) };
            self.out.puts(b").p[\0".as_ptr());
            self.out.put(ivar, at);
            self.out.puts(b"];\n\0".as_ptr());
            unsafe {
                (*locals).add(vname, vlen, b"ptr\0".as_ptr(), 3, self.depth);
            }
        } else {
            self.out.put(elct, elct_len);
            self.out.putc(b' ');
            self.out.put(vname, vlen);
            self.out.puts(b" = (\0".as_ptr());
            unsafe { self.emit_expr(base, locals) };
            self.out.puts(b").p[\0".as_ptr());
            self.out.put(ivar, at);
            self.out.puts(b"];\n\0".as_ptr());
            unsafe {
                (*locals).add(vname, vlen, elct, elct_len, self.depth);
            }
        }
        unsafe { self.emit_block_stmt(body, locals) };
        self.depth -= 1;
        self.indent();
        self.out.puts(b"}\n\0".as_ptr());
        return 1;
    }

    unsafe fn try_emit_for_enumerate(
        &mut self,
        s: *const pm_jit_rsx_ast_t,
        pat: *const pm_jit_rsx_ast_t,
        iter: *const pm_jit_rsx_ast_t,
        body: *const pm_jit_rsx_ast_t,
        locals: *mut LocalTab,
    ) -> usize {
        let ik = unsafe { (*iter).kids };
        let in_ = unsafe { (*iter).n_kids } as usize;
        if in_ < 3 {
            return 0;
        }
        let iname = unsafe { *ik.add(1) };
        let it = unsafe { (*iname).text };
        let itl = unsafe { (*iname).text_len };
        let iargs = unsafe { *ik.add(2) };
        if !(unsafe { z_eq(it, itl, b"enumerate\0".as_ptr()) }
            && unsafe { (*iargs).n_kids } as usize == 0)
        {
            return 0;
        }
        let recv = unsafe { *ik.add(0) };
        /* unwrap `X.iter()` — plain X is fine too */
        let mut base = recv;
        if unsafe { (*recv).kind } == pm_jit_rsx_ast_kind::METHOD_CALL {
            let rk = unsafe { (*recv).kids };
            let rn = unsafe { (*recv).n_kids } as usize;
            if rn >= 3 {
                let rname = unsafe { *rk.add(1) };
                let rargs = unsafe { *rk.add(2) };
                let rt = unsafe { (*rname).text };
                let rtl = unsafe { (*rname).text_len };
                if unsafe { z_eq(rt, rtl, b"iter\0".as_ptr()) }
                    && unsafe { (*rargs).n_kids } as usize == 0
                {
                    base = unsafe { *rk.add(0) };
                }
            }
        }
        /* receiver must be a fixed array — the element type and length are
         * both compile-time. */
        let bt = self.arena_tmp();
        let btn = unsafe { self.expr_ctype(base, bt, 128, locals) };
        if btn == 0 {
            unsafe {
                self.err(b"cannot infer enumerate receiver type\0".as_ptr(), unsafe { (*s).line });
            }
            return 1;
        }
        /* T [N] -> element spelling before '[', length inside */
        let mut bracket = btn;
        let mut elem_len = 0usize;
        let mut i2 = 0usize;
        while i2 < btn {
            if unsafe { *bt.add(i2) } == b'[' {
                bracket = i2;
                elem_len = i2;
                break;
            }
            i2 += 1;
        }
        if bracket >= btn {
            /* Vec<T> receiver (e.g. `f(x).into_iter().enumerate()`): the
             * row carries the element spelling; the length is .n at
             * runtime, the element .p[_i] — an index loop over the row. */
            if btn > 8 && unsafe { z_eq(bt, 8, b"rsx_vec_\0".as_ptr()) } {
                let row = unsafe { self.vecs.find_by_name(bt, btn) };
                if row < VEC_CAP {
                    return unsafe {
                        self.emit_for_enumerate_vec(s, pat, body, locals, base, row)
                    };
                }
            }
            unsafe {
                self.err(b"unsupported: enumerate over a non-array (slices carry no length)\0".as_ptr(), unsafe { (*s).line });
            }
            return 1;
        }
        /* pattern: exactly 2 elements — index bind + (value | &value) */
        if unsafe { (*pat).kind } != pm_jit_rsx_ast_kind::TUPLE
            || unsafe { (*pat).n_kids } as usize != 2
        {
            unsafe {
                self.err(b"unsupported: enumerate pattern (expect (i, x) or (i, &x))\0".as_ptr(), unsafe { (*s).line });
            }
            return 1;
        }
        let pk = unsafe { (*pat).kids };
        let idx_pat = unsafe { *pk.add(0) };
        let val_pat = unsafe { *pk.add(1) };
        /* unwrap the PATH wrapper around each bind */
        let mut idx_name = idx_pat;
        if unsafe { (*idx_name).kind } == pm_jit_rsx_ast_kind::PATH
            && unsafe { z_eq((*idx_name).text, (*idx_name).text_len, b"path\0".as_ptr()) }
            && unsafe { (*idx_name).n_kids } as usize == 1
        {
            idx_name = unsafe { *(*idx_name).kids.add(0) };
        }
        if unsafe { (*idx_name).kind } != pm_jit_rsx_ast_kind::PATH {
            unsafe {
                self.err(b"unsupported: enumerate index pattern\0".as_ptr(), unsafe { (*s).line });
            }
            return 1;
        }
        let iname2 = unsafe { (*idx_name).text };
        let inl2 = unsafe { (*idx_name).text_len };
        /* value bind: PATH or UNARY(&) around PATH */
        let mut is_ref = false;
        let mut val_name = val_pat;
        if unsafe { (*val_name).kind } == pm_jit_rsx_ast_kind::PATH
            && unsafe { z_eq((*val_name).text, (*val_name).text_len, b"path\0".as_ptr()) }
            && unsafe { (*val_name).n_kids } as usize == 1
        {
            val_name = unsafe { *(*val_name).kids.add(0) };
        }
        if unsafe { (*val_name).kind } == pm_jit_rsx_ast_kind::UNARY
            && unsafe { z_eq((*val_name).text, (*val_name).text_len, b"&\0".as_ptr()) }
            && unsafe { (*val_name).n_kids } as usize >= 1
        {
            let inner = unsafe { *(*val_name).kids.add(0) };
            let mut inner2 = inner;
            if unsafe { (*inner2).kind } == pm_jit_rsx_ast_kind::PATH
                && unsafe { z_eq((*inner2).text, (*inner2).text_len, b"path\0".as_ptr()) }
                && unsafe { (*inner2).n_kids } as usize == 1
            {
                inner2 = unsafe { *(*inner2).kids.add(0) };
            }
            if unsafe { (*inner2).kind } != pm_jit_rsx_ast_kind::PATH {
                unsafe {
                    self.err(b"unsupported: enumerate value pattern\0".as_ptr(), unsafe { (*s).line });
                }
                return 1;
            }
            is_ref = true;
            val_name = inner2;
        } else if unsafe { (*val_name).kind } != pm_jit_rsx_ast_kind::PATH {
            unsafe {
                self.err(b"unsupported: enumerate value pattern\0".as_ptr(), unsafe { (*s).line });
            }
            return 1;
        }
        let vname = unsafe { (*val_name).text };
        let vlen = unsafe { (*val_name).text_len };
        let labeled = unsafe { Lower::loop_is_labeled(s) };
        let lt = unsafe { (*s).text };
        let ll = unsafe { (*s).text_len };
        /* emit: for (size_t _i = 0; _i < len; _i++) { i = _i; <bind> ... } */
        let ivar = self.arena_tmp();
        let mut at = unsafe { bput(ivar, 160, 0, iname2, inl2) };
        at = unsafe { bput(ivar, 160, at, b"_i\0".as_ptr(), 2) };
        unsafe {
            *ivar.add(at) = 0;
        }
        self.indent();
        if labeled {
            unsafe { self.put_lbl(lt, ll, b"_\0".as_ptr()) };
            self.out.puts(b": \0".as_ptr());
        }
        self.out.puts(b"for (size_t \0".as_ptr());
        self.out.put(ivar, at);
        self.out.puts(b" = 0; \0".as_ptr());
        self.out.put(ivar, at);
        self.out.puts(b" < \0".as_ptr());
        /* length: sizeof(X)/sizeof(X[0]) — receiver may be any expr */
        self.out.puts(b"(sizeof(\0".as_ptr());
        unsafe { self.emit_expr(base, locals) };
        self.out.puts(b") / sizeof((\0".as_ptr());
        unsafe { self.emit_expr(base, locals) };
        self.out.puts(b")[0]); \0".as_ptr());
        self.out.put(ivar, at);
        self.out.puts(b"++) {\n\0".as_ptr());
        self.depth += 1;
        /* index bind */
        self.indent();
        self.out.puts(b"size_t \0".as_ptr());
        self.out.put(iname2, inl2);
        self.out.puts(b" = \0".as_ptr());
        self.out.put(ivar, at);
        self.out.puts(b";\n\0".as_ptr());
        unsafe {
            (*locals).add(iname2, inl2, b"size_t\0".as_ptr(), 6, self.depth);
        }
        /* value bind: by-ref -> pointer to the element; by-value -> copy */
        self.indent();
        if is_ref {
            self.out.put(bt, elem_len);
            self.out.puts(b" *\0".as_ptr());
            self.out.putc(b' ');
            self.out.put(vname, vlen);
            self.out.puts(b" = &(\0".as_ptr());
            unsafe { self.emit_expr(base, locals) };
            self.out.puts(b")[\0".as_ptr());
            self.out.put(ivar, at);
            self.out.puts(b"];\n\0".as_ptr());
            let pbuf = self.arena_tmp();
            let mut pn = unsafe { bput(pbuf, 160, 0, bt, elem_len) };
            pn = unsafe { bput(pbuf, 160, pn, b" *\0".as_ptr(), 2) };
            unsafe {
                *pbuf.add(pn) = 0;
            }
            unsafe {
                (*locals).add(vname, vlen, pbuf, pn, self.depth);
            }
        } else {
            self.out.put(bt, elem_len);
            self.out.putc(b' ');
            self.out.put(vname, vlen);
            self.out.puts(b" = (\0".as_ptr());
            unsafe { self.emit_expr(base, locals) };
            self.out.puts(b")[\0".as_ptr());
            self.out.put(ivar, at);
            self.out.puts(b"];\n\0".as_ptr());
            unsafe {
                (*locals).add(vname, vlen, bt, elem_len, self.depth);
            }
        }
        unsafe { self.emit_block_stmt(body, locals) };
        self.depth -= 1;
        if labeled {
            self.indent();
            unsafe { self.put_lbl(lt, ll, b"_cont\0".as_ptr()) };
            self.out.puts(b": ;\n\0".as_ptr());
        }
        self.indent();
        self.out.puts(b"}\n\0".as_ptr());
        if labeled {
            self.indent();
            unsafe { self.put_lbl(lt, ll, b"_end\0".as_ptr()) };
            self.out.puts(b": ;\n\0".as_ptr());
        }
        1
    }

    /* `.all/.any/.position(|bind| body)` over a fixed array, as a GNU
     * statement expression. Returns 1 when emitted (or refused with a
     * specific message); 0 only when the shape is not ours (caller
     * continues to the user-method path). */
    /* `.map(|p| body)` on an Option receiver — 1 = emitted, 0 = not this
     * shape (fall through), the err path already spoke. Receiver payload:
     * struct-Option (rsx_opt_*) -> ._v under ._has; pointer-Option -> the
     * value under a NULL test. The closure needs exactly one plain bind.
     * The result: body type struct/other -> struct-Option out (payload
     * registered in the opt table); body type pointer -> pointer-Option.
     * The bind is a local of the statement expression scope (same
     * discipline as the closure builtins), so the body's PATH lookups
     * resolve through LocalTab. */
    unsafe fn try_emit_opt_map(
        &mut self,
        recv: *const pm_jit_rsx_ast_t,
        clo: *const pm_jit_rsx_ast_t,
        locals: *mut LocalTab,
        line: u32,
    ) -> usize {
        let ck = unsafe { (*clo).kids };
        let cn = unsafe { (*clo).n_kids } as usize;
        if cn < 2 {
            unsafe {
                self.err(b"unsupported: map closure needs one bind and a body\0".as_ptr(), line);
            }
            return 1;
        }
        let param = unsafe { *ck.add(0) };
        let body = unsafe { *ck.add(cn - 1) };
        if unsafe { (*param).kind } != pm_jit_rsx_ast_kind::PARAM {
            unsafe {
                self.err(b"unsupported: map closure needs one bind and a body\0".as_ptr(), line);
            }
            return 1;
        }
        let pk = unsafe { (*param).kids };
        let pn = unsafe { (*param).n_kids } as usize;
        if pn >= 1 {
            let first = unsafe { *pk.add(0) };
            if unsafe { (*first).kind } == pm_jit_rsx_ast_kind::UNARY {
                unsafe {
                    self.err(b"unsupported: map closure bind is by-value\0".as_ptr(), line);
                }
                return 1;
            }
        }
        let bname = unsafe { (*param).text };
        let blen = unsafe { (*param).text_len };
        if blen == 0 || bname.is_null() {
            return 0;
        }
        /* receiver's C type + Option shape */
        let rbuf = self.arena_tmp();
        let rn = unsafe { self.expr_ctype(recv, rbuf, 128, locals) };
        if rn == 0 {
            return 0;
        }
        let mut is_struct_opt = false;
        let mut payload_buf = self.arena_tmp();
        let mut payload_len = 0usize;
        if rn >= 8 && unsafe { z_eq(rbuf, 8, b"rsx_opt_\0".as_ptr()) } {
            is_struct_opt = true;
            let eb = self.arena_tmp();
            let el = unsafe { Lower::opt_typedef_elem(rbuf, rn, eb, 160) };
            if el == 0 {
                return 0;
            }
            payload_len = unsafe { bput(payload_buf, 160, 0, eb, el) };
            unsafe {
                *payload_buf.add(payload_len) = 0;
            }
        } else {
            /* pointer-Option: the payload IS the receiver's spelling */
            payload_len = unsafe { bput(payload_buf, 160, 0, rbuf, rn) };
            unsafe {
                *payload_buf.add(payload_len) = 0;
            }
        }
        /* the result type: the body's type with the bind registered —
         * scope-enter, register, type, and keep the registration for the
         * emission pass below (the same stmt-expr scope). */
        unsafe {
            (*locals).note_scope();
            (*locals).add(bname, blen, payload_buf, payload_len, self.depth);
        }
        let bbuf = self.arena_tmp();
        let bn = unsafe { self.expr_ctype(body, bbuf, 128, locals) };
        if bn == 0 {
            unsafe {
                (*locals).drop_scope();
            }
            return 0;
        }
        /* struct-Option out (payload in the opt table) or pointer out */
        let mut out_struct = false;
        let mut out_ptr = false;
        if bn > 0 && unsafe { *bbuf.add(bn - 1) } == b'*' {
            out_ptr = true;
        } else {
            out_struct = true;
            let slot = unsafe { self.opt_add(bbuf, bn) };
            if slot >= OPT_CAP {
                unsafe {
                    (*locals).drop_scope();
                    self.err(b"option type table overflow\0".as_ptr(), line);
                }
                return 1;
            }
        }
        let tdn = self.arena_tmp();
        let mut tdn_len = 0usize;
        if out_struct {
            tdn_len = unsafe { Lower::opt_typedef_name(bbuf, bn, tdn, 160) };
            if tdn_len == 0 {
                unsafe {
                    (*locals).drop_scope();
                }
                return 0;
            }
        }
        /* the emission */
        self.out.puts(b"({ \0".as_ptr());
        if out_struct {
            self.out.put(tdn, tdn_len);
            self.out.puts(b" __m; \0".as_ptr());
        } else {
            self.out.put(bbuf, bn);
            self.out.puts(b" __m; \0".as_ptr());
        }
        /* the payload read happens BEFORE the bind's declaration: a
         * closure param that shadows the receiver's own name (Rust's
         * `f.map(|f| ..)`) would otherwise emit `T f = f._v;` — in C
         * the declarator's own name is in scope in its initializer, so
         * the `._v` would name the payload field of the payload. The
         * temp carries the receiver's payload across the declaration. */
        if is_struct_opt {
            self.out.put(payload_buf, payload_len);
            self.out.puts(b" __rsx_pv = \0".as_ptr());
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b"._v; \0".as_ptr());
        }
        if is_struct_opt {
            self.out.puts(b"if (\0".as_ptr());
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b"._has) { \0".as_ptr());
            self.out.put(payload_buf, payload_len);
            self.out.putc(b' ');
            self.out.put(bname, blen);
            self.out.puts(b" = __rsx_pv; \0".as_ptr());
        } else {
            self.out.puts(b"if (\0".as_ptr());
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b") { \0".as_ptr());
            self.out.put(payload_buf, payload_len);
            self.out.putc(b' ');
            self.out.put(bname, blen);
            self.out.puts(b" = \0".as_ptr());
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b"; \0".as_ptr());
        }
        if out_struct {
            self.out.puts(b"__m._v = \0".as_ptr());
            unsafe { self.emit_expr(body, locals) };
            self.out.puts(b"; __m._has = 1; } else { __m._has = 0; }\0".as_ptr());
        } else {
            self.out.puts(b"__m = \0".as_ptr());
            unsafe { self.emit_expr(body, locals) };
            self.out.puts(b"; } else { __m = 0; }\0".as_ptr());
        }
        self.out.puts(b" __m; })\0".as_ptr());
        unsafe {
            (*locals).drop_scope();
        }
        1
    }

    /* `X.iter().map(|x| body).collect::<Vec<T>>()` — the map-collect
     * chain over a Vec/&[T]/array base: one stmt-expr building an output
     * row vec, pushing body(elem) per index. The closure's single param
     * binds a copy of the element. Returns 1 when handled/refused, 0
     * when the receiver is not this shape. */
    unsafe fn try_emit_map_collect(
        &mut self,
        e: *const pm_jit_rsx_ast_t,
        recv: *const pm_jit_rsx_ast_t,
        margs: *const pm_jit_rsx_ast_t,
        locals: *mut LocalTab,
    ) -> usize {
        /* recv must be X.iter().map(clo) */
        if unsafe { (*recv).kind } != pm_jit_rsx_ast_kind::METHOD_CALL
            || (unsafe { (*recv).n_kids } as usize) < 3
        {
            return 0;
        }
        let rk = unsafe { (*recv).kids };
        let rname = unsafe { *rk.add(1) };
        if !(unsafe { (*rname).text_len } == 3
            && unsafe { z_eq(unsafe { (*rname).text }, 3, b"map\0".as_ptr()) })
        {
            return 0;
        }
        let map_args = unsafe { *rk.add(2) };
        let mak = unsafe { (*map_args).kids };
        let man = unsafe { (*map_args).n_kids } as usize;
        if man != 1 {
            return 0;
        }
        let map_arg = unsafe { *mak.add(0) };
        /* `String::as_str` / `Path::new` (qualified fn paths): the borrow
         * combinators — the output element is rsx_str_ref_t (a view of
         * each element; a &Path IS the str view in this subset). */
        let mut as_str_fn = false;
        if unsafe { (*map_arg).kind } == pm_jit_rsx_ast_kind::PATH {
            let mkp = unsafe { (*map_arg).kids };
            let mkn = unsafe { (*map_arg).n_kids } as usize;
            if mkn >= 2 {
                let seg_last = unsafe { *mkp.add(mkn - 1) };
                let seg_prev = unsafe { *mkp.add(mkn - 2) };
                if unsafe { (*seg_last).kind } == pm_jit_rsx_ast_kind::PATH
                    && unsafe { (*seg_prev).kind } == pm_jit_rsx_ast_kind::PATH
                    && ((unsafe { z_eq(unsafe { (*seg_last).text }, unsafe { (*seg_last).text_len }, b"as_str\0".as_ptr()) }
                        && unsafe { z_eq(unsafe { (*seg_prev).text }, unsafe { (*seg_prev).text_len }, b"String\0".as_ptr()) })
                        || (unsafe { z_eq(unsafe { (*seg_last).text }, unsafe { (*seg_last).text_len }, b"new\0".as_ptr()) }
                            && unsafe { z_eq(unsafe { (*seg_prev).text }, unsafe { (*seg_prev).text_len }, b"Path\0".as_ptr()) }))
                {
                    as_str_fn = true;
                }
            }
        }
        if !as_str_fn && unsafe { (*map_arg).kind } != pm_jit_rsx_ast_kind::CLOSURE {
            return 0;
        }
        let clo = map_arg;
        let map_recv = unsafe { *rk.add(0) };
        /* unwrap .iter() / .into_iter() — both are the identity on the
         * subset's containers */
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
            if (is_iter || is_into) && (unsafe { (*mra).n_kids } as usize) == 0 {
                base = unsafe { *mrk.add(0) };
            }
        }
        /* the input element + row spelling */
        let bt = self.arena_tmp();
        let btn = unsafe { self.expr_ctype(base, bt, 128, locals) };
        /* String::as_str fast path: only over a Vec<String>/&[String] —
         * the output is the Vec<&str> view row. */
        if as_str_fn {
            let mut eb_s: *const u8 = b"\0".as_ptr();
            let mut el_s = 0usize;
            if btn > 8 && unsafe { z_eq(bt, 8, b"rsx_vec_\0".as_ptr()) } {
                let vs = unsafe { self.vecs.find_by_name(bt, btn) };
                if vs < VEC_CAP {
                    eb_s = self.vecs.elems[vs].as_ptr();
                    el_s = self.vecs.elem_lens[vs];
                }
            } else if btn > 8 && unsafe { z_eq(bt, 8, b"rsx_arr_\0".as_ptr()) } {
                let rs = unsafe { self.arrs.find_by_name(bt, btn) };
                if rs < ARR_CAP {
                    eb_s = self.arrs.elems[rs].as_ptr();
                    el_s = self.arrs.elem_lens[rs];
                }
            }
            if el_s == 9 && unsafe { z_eq(eb_s, 9, b"rsx_str_t\0".as_ptr()) } {
                /* output row: Vec<rsx_str_ref_t> */
                let row = unsafe { self.vecs.intern(b"rsx_str_ref_t\0".as_ptr(), 13) };
                if row < VEC_CAP {
                    let nb = self.arena_tmp();
                    let nn = unsafe { VecTab::name_for(row, nb, 96) };
                    if nn > 0 && nn < 128 {
                        self.str_ref_used = true;
                        self.str_own_used = true;
                        self.out.puts(b"({ \0".as_ptr());
                        self.out.put(nb, nn);
                        self.out.puts(b" _o = {0}; for (size_t _i = 0; _i < (\0".as_ptr());
                        unsafe { self.emit_expr(base, locals) };
                        self.out.puts(b").n; _i++) { \0".as_ptr());
                        self.out.put(nb, nn);
                        /* the fat view of each owned element — the row's
                         * element is rsx_str_ref_t, the pushed value is
                         * the element's own p/n pair, not the struct.
                         * The .p cast keeps char*-backed String rows and
                         * uint8_t*-backed views one warning-free shape. */
                        self.out.puts(b"_push(&_o, (rsx_str_ref_t){ (const uint8_t *)(\0".as_ptr());
                        unsafe { self.emit_expr(base, locals) };
                        self.out.puts(b").p[_i].p, (\0".as_ptr());
                        unsafe { self.emit_expr(base, locals) };
                        self.out.puts(b").p[_i].n }); } _o; })\0".as_ptr());
                        return 1;
                    }
                }
            }
            unsafe {
                self.err(b"unsupported: map-collect String::as_str on a non-String element\0".as_ptr(), unsafe { (*e).line });
            }
            return 1;
        }
        let mut eb: *const u8 = b"\0".as_ptr();
        let mut el = 0usize;
        let mut is_row = false; /* .n/.p shape vs fixed array */
        if btn > 8 && unsafe { z_eq(bt, 8, b"rsx_vec_\0".as_ptr()) } {
            let vs = unsafe { self.vecs.find_by_name(bt, btn) };
            if vs < VEC_CAP {
                eb = self.vecs.elems[vs].as_ptr();
                el = self.vecs.elem_lens[vs];
                is_row = true;
            }
        } else if btn > 8 && unsafe { z_eq(bt, 8, b"rsx_arr_\0".as_ptr()) } {
            let rs = unsafe { self.arrs.find_by_name(bt, btn) };
            if rs < ARR_CAP {
                eb = self.arrs.elems[rs].as_ptr();
                el = self.arrs.elem_lens[rs];
                is_row = true;
            }
        } else {
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
        if el == 0 || el >= 128 {
            return 0;
        }
        /* closure: one PARAM + body */
        let ck = unsafe { (*clo).kids };
        let cn = unsafe { (*clo).n_kids } as usize;
        if cn < 2 {
            unsafe {
                self.err(b"unsupported: map-collect closure shape\0".as_ptr(), unsafe { (*e).line });
            }
            return 1;
        }
        let param = unsafe { *ck.add(0) };
        let body = unsafe { *ck.add(cn - 1) };
        if unsafe { (*param).kind } != pm_jit_rsx_ast_kind::PARAM {
            unsafe {
                self.err(b"unsupported: map-collect closure shape\0".as_ptr(), unsafe { (*e).line });
            }
            return 1;
        }
        let bname = unsafe { (*param).text };
        let blen = unsafe { (*param).text_len };
        /* tuple param `|(a, b)|` (text "tup"): register each element
         * bind from the tuple row's registered signature. */
        let is_tup_param = blen == 3 && unsafe { z_eq(bname, 3, b"tup\0".as_ptr()) };
        let mut tup_slot = TUP_CAP;
        let mut tup_cnt = 0usize;
        if is_tup_param {
            if !(el >= 10 && unsafe { z_eq(eb, 10, b"rsx_tuple_\0".as_ptr()) }) {
                unsafe {
                    self.err(b"unsupported: map-collect tuple param on a non-tuple element\0".as_ptr(), unsafe { (*e).line });
                }
                return 1;
            }
            tup_slot = unsafe { self.tup_find(eb, el) };
            let pk4 = unsafe { (*param).kids };
            tup_cnt = unsafe { (*param).n_kids } as usize;
            if tup_slot >= TUP_CAP
                || tup_cnt == 0
                || tup_cnt > TUP_MAXF
                || self.tup_counts[tup_slot] != tup_cnt
            {
                unsafe {
                    self.err(b"unsupported: map-collect tuple param arity\0".as_ptr(), unsafe { (*e).line });
                }
                return 1;
            }
            let _ = pk4;
        }
        /* the output element: the body's type with the param registered */
        let ob = self.arena_tmp();
        let mut on = 0usize;
        if !locals.is_null() {
            if is_tup_param {
                let pk5 = unsafe { (*param).kids };
                let mut f5 = 0usize;
                while f5 < tup_cnt {
                    let sub5 = unsafe { *pk5.add(f5) };
                    if unsafe { (*sub5).kind } == pm_jit_rsx_ast_kind::PARAM {
                        let sn5 = unsafe { (*sub5).text };
                        let sl5 = unsafe { (*sub5).text_len };
                        let a5 = tup_slot * TUP_MAXF + f5;
                        let elct = self.tup_elems[a5].as_ptr();
                        let elct_len = self.tup_lens[a5];
                        unsafe {
                            (*locals).add(sn5, sl5, elct, elct_len, 0);
                        }
                    }
                    f5 += 1;
                }
            } else {
                unsafe {
                    (*locals).add(bname, blen, eb, el, 0);
                }
            }
            on = unsafe { self.expr_ctype(body, ob, 128, locals) };
        }
        if on == 0 || on >= 128 {
            unsafe {
                self.err(b"unsupported: map-collect body type\0".as_ptr(), unsafe { (*e).line });
            }
            return 1;
        }
        let row = unsafe { self.vecs.intern(ob, on) };
        if row >= VEC_CAP {
            unsafe {
                self.err(b"internal: map-collect output row overflow\0".as_ptr(), unsafe { (*e).line });
            }
            return 1;
        }
        let nb = self.arena_tmp();
        let nn = unsafe { VecTab::name_for(row, nb, 96) };
        if nn == 0 || nn >= 128 {
            unsafe {
                self.err(b"internal: map-collect row name\0".as_ptr(), unsafe { (*e).line });
            }
            return 1;
        }
        /* collect takes no args (a turbofish was skipped by the parser) */
        if (unsafe { (*margs).n_kids } as usize) != 0 {
            return 0;
        }
        /* emit: ({ out_row _o = {0}; for (i < n) { <elem decl>; row_push(&_o, body); } _o; })
         * The param DECLARATION lands in the loop body's braces — a
         * declaration inside _push's argument list is not C. */
        self.out.puts(b"({ \0".as_ptr());
        self.out.put(nb, nn);
        self.out.puts(b" _o = {0}; for (size_t _i = 0; _i < \0".as_ptr());
        if is_row {
            self.out.puts(b"(\0".as_ptr());
            unsafe { self.emit_expr(base, locals) };
            self.out.puts(b").n; _i++) { \0".as_ptr());
        } else {
            self.out.puts(b"sizeof(\0".as_ptr());
            unsafe { self.emit_expr(base, locals) };
            self.out.puts(b") / sizeof((\0".as_ptr());
            unsafe { self.emit_expr(base, locals) };
            self.out.puts(b")[0]); _i++) { \0".as_ptr());
        }
        /* register the param(s), DECLARE them in the loop scope, then push */
        if is_tup_param {
            let pk5 = unsafe { (*param).kids };
            self.out.put(eb, el);
            self.out.puts(b" __rsx_me = (\0".as_ptr());
            unsafe { self.emit_expr(base, locals) };
            if is_row {
                self.out.puts(b").p[_i]; \0".as_ptr());
            } else {
                self.out.puts(b")[_i]; \0".as_ptr());
            }
            let mut f5 = 0usize;
            while f5 < tup_cnt {
                let sub5 = unsafe { *pk5.add(f5) };
                if unsafe { (*sub5).kind } == pm_jit_rsx_ast_kind::PARAM {
                    let sn5 = unsafe { (*sub5).text };
                    let sl5 = unsafe { (*sub5).text_len };
                    let a5 = tup_slot * TUP_MAXF + f5;
                    let elct = self.tup_elems[a5].as_ptr();
                    let elct_len = self.tup_lens[a5];
                    self.out.put(elct, elct_len);
                    self.out.putc(b' ');
                    self.out.put(sn5, sl5);
                    self.out.puts(b" = __rsx_me._\0".as_ptr());
                    let d5 = b'0' + f5 as u8;
                    self.out.putc(d5);
                    self.out.puts(b"; \0".as_ptr());
                    unsafe {
                        (*locals).add(sn5, sl5, elct, elct_len, self.depth + 1);
                    }
                }
                f5 += 1;
            }
        } else {
            unsafe {
                (*locals).add(bname, blen, eb, el, self.depth + 1);
            }
            /* the element copy: <elem> b = base.p[_i] (row) / base[_i] */
            self.out.put(eb, el);
            self.out.putc(b' ');
            self.out.put(bname, blen);
            self.out.puts(b" = (\0".as_ptr());
            unsafe { self.emit_expr(base, locals) };
            if is_row {
                self.out.puts(b").p[_i]; \0".as_ptr());
            } else {
                self.out.puts(b")[_i]; \0".as_ptr());
            }
        }
        self.out.put(nb, nn);
        self.out.puts(b"_push(&_o, \0".as_ptr());
        unsafe { self.emit_expr(body, locals) };
        self.out.puts(b"); } _o; })\0".as_ptr());
        1
    }

    unsafe fn try_emit_closure_loop(
        &mut self,
        recv: *const pm_jit_rsx_ast_t,
        mname: *const u8,
        mlen: usize,
        clo: *const pm_jit_rsx_ast_t,
        locals: *mut LocalTab,
        line: u32,
    ) -> usize {
        /* unwrap `X.iter()` — the builtin walks X itself (plain X fine). */
        let mut base = recv;
        if unsafe { (*recv).kind } == pm_jit_rsx_ast_kind::METHOD_CALL {
            let rk = unsafe { (*recv).kids };
            let rn = unsafe { (*recv).n_kids } as usize;
            if rn >= 3 {
                let rname = unsafe { *rk.add(1) };
                let rargs = unsafe { *rk.add(2) };
                let rt = unsafe { (*rname).text };
                let rtl = unsafe { (*rname).text_len };
                if unsafe { z_eq(rt, rtl, b"iter\0".as_ptr()) }
                    && unsafe { (*rargs).n_kids } as usize == 0
                {
                    base = unsafe { *rk.add(0) };
                }
            }
        }
        /* receiver must be a fixed array (element spelling + compile-time
         * length via sizeof) or a Vec row (rsx_vec_<row> — the element
         * spelling is the interned row's own bytes, the length `.n`). */
        let bt = self.arena_tmp();
        let btn = unsafe { self.expr_ctype(base, bt, 128, locals) };
        if btn == 0 {
            return 0;
        }
        let mut bt_new: *mut u8 = core::ptr::null_mut();
        let mut elem_len_new: usize = 0;
        let mut is_vec = false;
        if btn > 8 && unsafe { z_eq(bt, 8, b"rsx_vec_\0".as_ptr()) } {
            let vs = unsafe { self.vecs.find_by_name(bt, btn) };
            if vs < VEC_CAP {
                let el = self.vecs.elem_lens[vs];
                if el > 0 && el < 128 {
                    let bt2 = self.arena_tmp();
                    let mut w = 0usize;
                    while w < el {
                        unsafe {
                            *bt2.add(w) = self.vecs.elems[vs][w];
                        }
                        w += 1;
                    }
                    unsafe {
                        *bt2.add(el) = 0;
                    }
                    bt_new = bt2;
                    elem_len_new = el;
                    is_vec = true;
                }
            }
        }
        /* &[T] slice ref (rsx_arr_<row>): the fat pair carries .n and
         * .p — the loop shape is the Vec's exactly, the element spelling
         * comes from the ArrTab row. */
        let mut is_arr = false;
        if !is_vec && btn > 8 && unsafe { z_eq(bt, 8, b"rsx_arr_\0".as_ptr()) } {
            let rs = unsafe { self.arrs.find_by_name(bt, btn) };
            if rs < ARR_CAP {
                let el = self.arrs.elem_lens[rs];
                if el > 0 && el < 128 {
                    let bt2 = self.arena_tmp();
                    let mut w = 0usize;
                    while w < el {
                        unsafe {
                            *bt2.add(w) = self.arrs.elems[rs][w];
                        }
                        w += 1;
                    }
                    unsafe {
                        *bt2.add(el) = 0;
                    }
                    bt_new = bt2;
                    elem_len_new = el;
                    is_arr = true;
                }
            }
        }
        let mut bracket = btn;
        let mut elem_len = 0usize;
        if is_vec || is_arr {
            bracket = btn;
            elem_len = elem_len_new;
        } else {
            let mut i = 0usize;
            while i < btn {
                if unsafe { *bt.add(i) } == b'[' {
                    bracket = i;
                    elem_len = i;
                    break;
                }
                i += 1;
            }
        }
        if ((is_vec || is_arr) && elem_len == 0) || (!is_vec && !is_arr && bracket >= btn) {
            unsafe {
                self.err(b"unsupported: closure builtin on a non-array (slices carry no length)\0".as_ptr(), line);
            }
            return 1;
        }
        /* closure: exactly one bind (PARAM, optional &-child) + body */
        let ck = unsafe { (*clo).kids };
        let cn = unsafe { (*clo).n_kids } as usize;
        if cn < 2 {
            unsafe {
                self.err(b"unsupported: closure builtin needs one bind and a body\0".as_ptr(), line);
            }
            return 1;
        }
        let param = unsafe { *ck.add(0) };
        let body = unsafe { *ck.add(cn - 1) };
        if unsafe { (*param).kind } != pm_jit_rsx_ast_kind::PARAM {
            unsafe {
                self.err(b"unsupported: closure builtin needs one bind and a body\0".as_ptr(), line);
            }
            return 1;
        }
        let bname = unsafe { (*param).text };
        let blen = unsafe { (*param).text_len };
        let mut by_ref = false;
        {
            let pk = unsafe { (*param).kids };
            let pn = unsafe { (*param).n_kids } as usize;
            if pn >= 1 {
                let first = unsafe { *pk.add(0) };
                if unsafe { (*first).kind } == pm_jit_rsx_ast_kind::UNARY
                    && unsafe { z_eq((*first).text, (*first).text_len, b"&\0".as_ptr()) }
                {
                    by_ref = true;
                }
            }
        }
        let want_all = unsafe { z_eq(mname, mlen, b"all\0".as_ptr()) };
        let want_any = unsafe { z_eq(mname, mlen, b"any\0".as_ptr()) };
        let want_pos = unsafe { z_eq(mname, mlen, b"position\0".as_ptr()) };
        let ivar = self.arena_tmp();
        let mut at = unsafe { bput(ivar, 160, 0, bname, blen) };
        at = unsafe { bput(ivar, 160, at, b"_i\0".as_ptr(), 2) };
        unsafe {
            *ivar.add(at) = 0;
        }
        let len_var = self.arena_tmp();
        let mut at2 = unsafe { bput(len_var, 160, 0, bname, blen) };
        at2 = unsafe { bput(len_var, 160, at2, b"_n\0".as_ptr(), 2) };
        unsafe {
            *len_var.add(at2) = 0;
        }
        self.out.puts(b"({\0".as_ptr());
        /* length once: arrays via sizeof, Vec/slice rows via `.n` */
        self.out.puts(b"size_t \0".as_ptr());
        self.out.put(len_var, at2);
        if is_vec || is_arr {
            self.out.puts(b" = (\0".as_ptr());
            unsafe { self.emit_expr(base, locals) };
            self.out.puts(b").n;\0".as_ptr());
        } else {
            self.out.puts(b" = sizeof(\0".as_ptr());
            unsafe { self.emit_expr(base, locals) };
            self.out.puts(b") / sizeof((\0".as_ptr());
            unsafe { self.emit_expr(base, locals) };
            self.out.puts(b")[0]);\0".as_ptr());
        }
        if want_pos {
            /* not-found marker: (size_t)-1 — expressible at the call site,
             * so a chained .unwrap_or(x) can test it. */
            self.out.puts(b"size_t \0".as_ptr());
            self.out.put(bname, blen);
            self.out.puts(b"_pos = (size_t)-1;\0".as_ptr());
        } else {
            self.out.puts(b"int \0".as_ptr());
            self.out.put(bname, blen);
            self.out.puts(b"_acc = \0".as_ptr());
            if want_all {
                self.out.putc(b'1');
            } else {
                self.out.putc(b'0');
            }
            self.out.putc(b';');
        }
        self.out.putc(b'\n');
        /* the loop: the bind is a local of the loop scope */
        self.depth += 1;
        self.indent();
        self.depth -= 1;
        self.out.puts(b"for (size_t \0".as_ptr());
        self.out.put(ivar, at);
        self.out.puts(b" = 0; \0".as_ptr());
        self.out.put(ivar, at);
        self.out.puts(b" < \0".as_ptr());
        self.out.put(len_var, at2);
        self.out.puts(b"; \0".as_ptr());
        self.out.put(ivar, at);
        self.out.puts(b"++) {\n\0".as_ptr());
        self.depth += 1;
        self.indent();
        /* declare the bind as an element COPY — `|&b|` destructures the
         * reference (the body sees the value), so the `&` is pattern
         * syntax only. A pointer bind would miscompile `b == 0` bodies. */
        let _ = by_ref;
        if is_vec || is_arr {
            self.out.put(bt_new, elem_len_new);
        } else {
            self.out.put(bt, elem_len);
        }
        self.out.putc(b' ');
        self.out.put(bname, blen);
        self.out.puts(b" = (\0".as_ptr());
        unsafe { self.emit_expr(base, locals) };
        if is_vec || is_arr {
            self.out.puts(b").p[\0".as_ptr());
        } else {
            self.out.puts(b")[\0".as_ptr());
        }
        self.out.put(ivar, at);
        self.out.puts(b"];\n\0".as_ptr());
        if is_vec || is_arr {
            unsafe {
                (*locals).add(bname, blen, bt_new, elem_len_new, self.depth);
            }
        } else {
            unsafe {
                (*locals).add(bname, blen, bt, elem_len, self.depth);
            }
        }
        self.indent();
        if want_pos {
            self.out.puts(b"if (\0".as_ptr());
            unsafe { self.emit_expr(body, locals) };
            self.out.puts(b") {\0".as_ptr());
            self.out.put(bname, blen);
            self.out.puts(b"_pos = \0".as_ptr());
            self.out.put(ivar, at);
            self.out.puts(b"; break; }\0".as_ptr());
        } else {
            self.out.puts(b"if (\0".as_ptr());
            if want_all {
                self.out.putc(b'!');
            }
            self.out.putc(b'(');
            unsafe { self.emit_expr(body, locals) };
            /* BOTH parens close: the wrap's own and the if-cond's —
             * `if (` + `(` + BODY + `) {` leaves the cond's paren
             * dangling (TCC: "',' expected"); a body that is itself a
             * parenthesized BINARY chain (`a || b`) re-balance-checks
             * the same way, one close per open. */
            self.out.puts(b")) {\0".as_ptr());
            if want_all {
                self.out.put(bname, blen);
                self.out.puts(b"_acc = 0;\0".as_ptr());
            } else {
                self.out.put(bname, blen);
                self.out.puts(b"_acc = 1;\0".as_ptr());
            }
            self.out.puts(b"break; }\0".as_ptr());
        }
        self.out.putc(b'\n');
        self.depth -= 1;
        self.indent();
        self.out.puts(b"}\0".as_ptr());
        self.out.putc(b'\n');
        /* value of the statement expression */
        if want_pos {
            self.out.put(bname, blen);
            self.out.puts(b"_pos;\0".as_ptr());
        } else {
            self.out.put(bname, blen);
            self.out.puts(b"_acc;\0".as_ptr());
        }
        self.out.puts(b"})\0".as_ptr());
        1
    }

    /* `for word in x.split(|c: char| pred)` — the &str split iterator:
     * a byte-cursor loop over the fat-ref view. Rust's split yields
     * every segment between pred-true bytes (empties included — the
     * body's `is_empty` guard is the source's own). The closure's one
     * param binds as a uint32_t char; the pred body emits inline in a
     * GNU stmt-expr in the scan's condition. Returns 1 when handled
     * (emitted or refused loudly), 0 when the iter is not this shape. */
    unsafe fn try_emit_for_split(
        &mut self,
        s: *const pm_jit_rsx_ast_t,
        pat: *const pm_jit_rsx_ast_t,
        iter: *const pm_jit_rsx_ast_t,
        body: *const pm_jit_rsx_ast_t,
        locals: *mut LocalTab,
    ) -> usize {
        let ik = unsafe { (*iter).kids };
        let ink = unsafe { (*iter).n_kids } as usize;
        if ink < 3 {
            return 0;
        }
        let recv = unsafe { *ik.add(0) };
        let name = unsafe { *ik.add(1) };
        let args = unsafe { *ik.add(2) };
        let mname = unsafe { (*name).text };
        let mlen = unsafe { (*name).text_len };
        if !(mlen == 5 && unsafe { z_eq(mname, mlen, b"split\0".as_ptr()) }) {
            return 0;
        }
        let ak = unsafe { (*args).kids };
        let an = unsafe { (*args).n_kids } as usize;
        if an != 1 {
            return 0;
        }
        let clo = unsafe { *ak.add(0) };
        /* two arg shapes ride the same scan skeleton:
         *   split('|c| ...')  — a pred closure over each byte
         *   split('.')        — a char-literal separator: pred = (c == '.')
         * The literal's text is the quoted char ('.' / b'.'-style); the
         * scan emits the comparison directly, no closure body. */
        let mut is_sep_lit = false;
        let mut sep_val: u8 = 0;
        if unsafe { (*clo).kind } == pm_jit_rsx_ast_kind::LITERAL {
            let lt = unsafe { (*clo).text };
            let ll = unsafe { (*clo).text_len };
            /* quoted char: '<c>' — take the byte between the quotes */
            if ll >= 3 && !lt.is_null() && unsafe { *lt } == b'\'' {
                sep_val = unsafe { *lt.add(1) };
                is_sep_lit = true;
            }
        }
        if !is_sep_lit && unsafe { (*clo).kind } != pm_jit_rsx_ast_kind::CLOSURE {
            return 0;
        }
        /* the receiver: the fat-ref view or the owned String */
        let rb = self.arena_tmp();
        let rl = unsafe { self.expr_ctype(recv, rb, 128, locals) };
        if !(rl == 13 && unsafe { z_eq(rb, 13, b"rsx_str_ref_t\0".as_ptr()) })
            && !(rl == 9 && unsafe { z_eq(rb, 9, b"rsx_str_t\0".as_ptr()) })
        {
            return 0;
        }
        /* closure: one bind (+ optional `: TYPE` ascription parsed as
         * a TYPE kid BEFORE the bind) + body — separator-literal form
         * has neither; the scan compares the byte inline. */
        let mut cname: *const u8 = b"_sep\0".as_ptr();
        let mut clen = 4usize;
        let mut cbody: *const pm_jit_rsx_ast_t = core::ptr::null_mut();
        if !is_sep_lit {
            let ck = unsafe { (*clo).kids };
            let cn = unsafe { (*clo).n_kids } as usize;
            if cn < 2 {
                unsafe {
                    self.err(b"unsupported: split needs one bind and a body\0".as_ptr(), unsafe { (*s).line });
                }
                return 1;
            }
            let mut param = unsafe { *ck.add(0) };
            {
                let mut q = 1usize;
                while q < cn && unsafe { (*param).kind } != pm_jit_rsx_ast_kind::PARAM {
                    param = unsafe { *ck.add(q) };
                    q += 1;
                }
            }
            cbody = unsafe { *ck.add(cn - 1) };
            if unsafe { (*param).kind } != pm_jit_rsx_ast_kind::PARAM {
                unsafe {
                    self.err(b"unsupported: split closure bind\0".as_ptr(), unsafe { (*s).line });
                }
                return 1;
            }
            cname = unsafe { (*param).text };
            clen = unsafe { (*param).text_len };
        }
        /* the loop bind: one PATH identifier */
        if unsafe { (*pat).kind } != pm_jit_rsx_ast_kind::PATH {
            unsafe {
                self.err(b"unsupported: split for binds one identifier\0".as_ptr(), unsafe { (*s).line });
            }
            return 1;
        }
        let pk = unsafe { (*pat).kids };
        if (unsafe { (*pat).n_kids } as usize) < 1 {
            unsafe {
                self.err(b"unsupported: split for bind\0".as_ptr(), unsafe { (*s).line });
            }
            return 1;
        }
        let pleaf = unsafe { *pk.add(0) };
        let bname = unsafe { (*pleaf).text };
        let blen = unsafe { (*pleaf).text_len };
        /* fresh cursor names (collide-proof against the body's own) */
        let sv = self.arena_tmp();
        let mut at = unsafe { bput(sv, 160, 0, bname, blen) };
        at = unsafe { bput(sv, 160, at, b"_sv\0".as_ptr(), 3) };
        unsafe {
            *sv.add(at) = 0;
        }
        let iv = self.arena_tmp();
        let mut at2 = unsafe { bput(iv, 160, 0, bname, blen) };
        at2 = unsafe { bput(iv, 160, at2, b"_i\0".as_ptr(), 2) };
        unsafe {
            *iv.add(at2) = 0;
        }
        let ev = self.arena_tmp();
        let mut at3 = unsafe { bput(ev, 160, 0, bname, blen) };
        at3 = unsafe { bput(ev, 160, at3, b"_e\0".as_ptr(), 2) };
        unsafe {
            *ev.add(at3) = 0;
        }
        /* scope for the param + the loop bind */
        unsafe {
            (*locals).note_scope();
        }
        unsafe {
            (*locals).add(cname, clen, b"uint32_t\0".as_ptr(), 8, self.depth + 1);
        }
        /* the loop bind is a fat-ref view segment */
        self.str_ref_used = true;
        unsafe {
            (*locals).add(bname, blen, b"rsx_str_ref_t\0".as_ptr(), 13, self.depth + 1);
        }
        self.indent();
        self.out.puts(b"{\n\0".as_ptr());
        self.depth += 1;
        self.indent();
        self.out.puts(b"rsx_str_ref_t \0".as_ptr());
        self.out.put(sv, at);
        self.out.puts(b" = \0".as_ptr());
        if rl == 9 {
            /* owned String: the p/n view of the same value, rendered
             * once into the local copy (re-evaluating a field expr per
             * iteration is both wasteful and unsafe if it mutates).
             * The .p cast: rsx_str_t.p is char*, the view's is
             * const uint8_t* — the same bytes, an explicit convert. */
            self.out.puts(b"(rsx_str_ref_t){ (const uint8_t *)\0".as_ptr());
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b".p, \0".as_ptr());
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b".n }; \0".as_ptr());
        } else {
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b"; \0".as_ptr());
        }
        self.out.puts(b"size_t \0".as_ptr());
        self.out.put(iv, at2);
        self.out.puts(b" = 0;\n\0".as_ptr());
        self.indent();
        self.out.puts(b"while (\0".as_ptr());
        self.out.put(iv, at2);
        self.out.puts(b" < \0".as_ptr());
        self.out.put(sv, at);
        self.out.puts(b".n) {\n\0".as_ptr());
        self.depth += 1;
        self.indent();
        self.out.putc(b's');
        self.out.puts(b"ize_t \0".as_ptr());
        self.out.put(ev, at3);
        self.out.puts(b" = \0".as_ptr());
        self.out.put(iv, at2);
        self.out.puts(b";\n\0".as_ptr());
        self.indent();
        self.out.puts(b"while (\0".as_ptr());
        self.out.put(ev, at3);
        self.out.puts(b" < \0".as_ptr());
        self.out.put(sv, at);
        self.out.puts(b".n && ({ uint32_t \0".as_ptr());
        self.out.put(cname, clen);
        self.out.puts(b" = (\0".as_ptr());
        self.out.put(sv, at);
        self.out.puts(b").p[\0".as_ptr());
        self.out.put(ev, at3);
        self.out.puts(b"]; \0".as_ptr());
        if is_sep_lit {
            /* the separator literal rides the same pred slot: the scan
             * compares the byte inline — no closure body to emit. */
            self.out.put(cname, clen);
            self.out.puts(b" == \0".as_ptr());
            self.out.put_u32(sep_val as u32);
            self.out.puts(b"u\0".as_ptr());
        } else {
            unsafe { self.emit_expr(cbody, locals) };
        }
        self.out.puts(b"; })) { \0".as_ptr());
        self.out.put(ev, at3);
        self.out.puts(b"++; }\n\0".as_ptr());
        /* the bind: the segment [i, e) */
        self.indent();
        self.out.puts(b"rsx_str_ref_t \0".as_ptr());
        self.out.put(bname, blen);
        self.out.puts(b" = { (\0".as_ptr());
        self.out.put(sv, at);
        self.out.puts(b").p + \0".as_ptr());
        self.out.put(iv, at2);
        self.out.puts(b", \0".as_ptr());
        self.out.put(ev, at3);
        self.out.puts(b" - \0".as_ptr());
        self.out.put(iv, at2);
        self.out.puts(b" };\n\0".as_ptr());
        /* body */
        unsafe { self.emit_block_stmt(body, locals) };
        self.indent();
        self.out.put(iv, at2);
        self.out.puts(b" = \0".as_ptr());
        self.out.put(ev, at3);
        self.out.puts(b" + 1;\n\0".as_ptr());
        self.depth -= 1;
        self.indent();
        self.out.puts(b"}\n\0".as_ptr());
        self.depth -= 1;
        self.indent();
        self.out.puts(b"}\n\0".as_ptr());
        unsafe {
            (*locals).drop_scope();
        }
        1
    }

    /* `for ch in s.bytes()` — a byte-index loop over the &str fat-ref
     * view (or the owned String's .p/.n): the bind is a uint32_t byte,
     * each iteration names `sv.p[i]`. ASCII scanning — bytes are chars,
     * UTF-8 decode is the documented subset divergence. Returns 1 when
     * handled (emitted or refused loudly), 0 when not this shape. */
    unsafe fn try_emit_for_bytes(
        &mut self,
        s: *const pm_jit_rsx_ast_t,
        pat: *const pm_jit_rsx_ast_t,
        iter: *const pm_jit_rsx_ast_t,
        body: *const pm_jit_rsx_ast_t,
        locals: *mut LocalTab,
    ) -> usize {
        let ik = unsafe { (*iter).kids };
        let ink = unsafe { (*iter).n_kids } as usize;
        if ink < 3 {
            return 0;
        }
        let recv = unsafe { *ik.add(0) };
        let name = unsafe { *ik.add(1) };
        let args = unsafe { *ik.add(2) };
        let mname = unsafe { (*name).text };
        let mlen = unsafe { (*name).text_len };
        if !(mlen == 5 && unsafe { z_eq(mname, mlen, b"bytes\0".as_ptr()) }) {
            return 0;
        }
        if unsafe { (*args).n_kids } as usize != 0 {
            return 0;
        }
        /* the receiver: the fat-ref view or the owned String */
        let rb = self.arena_tmp();
        let rl = unsafe { self.expr_ctype(recv, rb, 128, locals) };
        if !(rl == 13 && unsafe { z_eq(rb, 13, b"rsx_str_ref_t\0".as_ptr()) })
            && !(rl == 9 && unsafe { z_eq(rb, 9, b"rsx_str_t\0".as_ptr()) })
        {
            return 0;
        }
        /* the loop bind: one PATH identifier */
        if unsafe { (*pat).kind } != pm_jit_rsx_ast_kind::PATH {
            unsafe {
                self.err(b"unsupported: bytes for binds one identifier\0".as_ptr(), unsafe { (*s).line });
            }
            return 1;
        }
        let pk = unsafe { (*pat).kids };
        if (unsafe { (*pat).n_kids } as usize) < 1 {
            unsafe {
                self.err(b"unsupported: bytes for bind\0".as_ptr(), unsafe { (*s).line });
            }
            return 1;
        }
        let pleaf = unsafe { *pk.add(0) };
        let bname = unsafe { (*pleaf).text };
        let blen = unsafe { (*pleaf).text_len };
        /* fresh cursor names (collide-proof against the body's own) */
        let sv = self.arena_tmp();
        let mut at = unsafe { bput(sv, 160, 0, bname, blen) };
        at = unsafe { bput(sv, 160, at, b"_sv\0".as_ptr(), 3) };
        unsafe {
            *sv.add(at) = 0;
        }
        let iv = self.arena_tmp();
        let mut at2 = unsafe { bput(iv, 160, 0, bname, blen) };
        at2 = unsafe { bput(iv, 160, at2, b"_i\0".as_ptr(), 2) };
        unsafe {
            *iv.add(at2) = 0;
        }
        /* scope for the loop bind */
        unsafe {
            (*locals).note_scope();
        }
        self.str_ref_used = true;
        unsafe {
            (*locals).add(bname, blen, b"uint32_t\0".as_ptr(), 8, self.depth + 1);
        }
        self.indent();
        self.out.puts(b"{\n\0".as_ptr());
        self.depth += 1;
        self.indent();
        /* the receiver renders once, into a local view copy: the fat-ref
         * IS a view (plain parens); the owned String contributes its p/n
         * pair as a compound literal. */
        self.out.puts(b"rsx_str_ref_t \0".as_ptr());
        self.out.put(sv, at);
        self.out.puts(b" = \0".as_ptr());
        if rl == 9 {
            self.out.puts(b"(rsx_str_ref_t){ \0".as_ptr());
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b".p, \0".as_ptr());
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b".n };\n\0".as_ptr());
        } else {
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b";\n\0".as_ptr());
        }
        self.indent();
        let labeled = unsafe { Lower::loop_is_labeled(s) };
        let lt = unsafe { (*s).text };
        let ll = unsafe { (*s).text_len };
        if labeled {
            unsafe { self.put_lbl(lt, ll, b"_\0".as_ptr()) };
            self.out.puts(b": \0".as_ptr());
        }
        self.out.puts(b"for (size_t \0".as_ptr());
        self.out.put(iv, at2);
        self.out.puts(b" = 0; \0".as_ptr());
        self.out.put(iv, at2);
        self.out.puts(b" < \0".as_ptr());
        self.out.put(sv, at);
        self.out.puts(b".n; \0".as_ptr());
        self.out.put(iv, at2);
        self.out.puts(b"++) {\n\0".as_ptr());
        self.depth += 1;
        self.indent();
        self.out.puts(b"uint32_t \0".as_ptr());
        self.out.put(bname, blen);
        self.out.puts(b" = \0".as_ptr());
        self.out.put(sv, at);
        self.out.puts(b".p[\0".as_ptr());
        self.out.put(iv, at2);
        self.out.puts(b"];\n\0".as_ptr());
        /* body */
        unsafe { self.emit_block_stmt(body, locals) };
        self.depth -= 1;
        self.indent();
        self.out.puts(b"}\n\0".as_ptr());
        self.depth -= 1;
        self.indent();
        self.out.puts(b"}\n\0".as_ptr());
        unsafe {
            (*locals).drop_scope();
        }
        1
    }

    unsafe fn emit_for(&mut self, s: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        /* kids: pat(PATH bind), iter(range BINARY), body */
        let kids = unsafe { (*s).kids };
        let nk = unsafe { (*s).n_kids } as usize;
        if nk < 3 {
            unsafe {
                self.err(b"bad for loop\0".as_ptr(), unsafe { (*s).line });
            }
            return;
        }
        let pat = unsafe { *kids.add(0) };
        let iter = unsafe { *kids.add(1) };
        let body = unsafe { *kids.add(2) };
        /* `for bind in env::args().skip(1)` / `env::args()` — the argv
         * walk: an index loop over the process argv, each bind a fresh
         * rsx_str_t copy of the C string. The kernel seat's argv rides
         * the same global main() contract the host CLI uses. */
        if unsafe { self.try_emit_for_args(s, pat, iter, body, locals) } != 0 || !self.ok {
            return;
        }
        /* `for pat in X.iter().enumerate()` — an index+element pair over a
         * fixed array (the only receiver shape the subset carries a length
         * for). The pattern destructures `(i, bind)` / `(i, &bind)`. */
        if unsafe { (*iter).kind } == pm_jit_rsx_ast_kind::METHOD_CALL {
            let handled = unsafe { self.try_emit_for_enumerate(s, pat, iter, body, locals) };
            if handled != 0 || !self.ok {
                return;
            }
            /* `for bind in X.iter()` / `X.iter_mut()` over a fixed array:
             * an index loop binding a pointer to each element (bodies
             * read AND write through it — const-ness is elided, a
             * documented subset divergence). Slices refuse: no length. */
            if unsafe { self.try_emit_for_iter(s, pat, iter, body, locals) } != 0 || !self.ok {
                return;
            }
            /* `for word in x.split(pred)` — the &str split cursor */
            if unsafe { self.try_emit_for_split(s, pat, iter, body, locals) } != 0 || !self.ok {
                return;
            }
            /* `for ch in s.bytes()` — a byte-index loop over the &str
             * view (ASCII: bytes are chars, UTF-8 decode is the
             * documented subset divergence). The bind is a uint32_t. */
            if unsafe { self.try_emit_for_bytes(s, pat, iter, body, locals) } != 0 || !self.ok {
                return;
            }
            /* `for (i, ch) in s.char_indices()` — byte-index + char over
             * a &str view. ASCII scanning (bytes are chars; the subset
             * documents UTF-8 decode as unsupported — gen scans C
             * signatures). Pattern: a 2-bind tuple. */
            {
                let ik2 = unsafe { (*iter).kids };
                let ink2 = unsafe { (*iter).n_kids } as usize;
                if ink2 >= 3 {
                    let nm = unsafe { *ik2.add(1) };
                    if unsafe { (*nm).text_len } == 12
                        && unsafe { z_eq(unsafe { (*nm).text }, 12, b"char_indices\0".as_ptr()) }
                    {
                        let argsn = unsafe { *ik2.add(2) };
                        if unsafe { (*argsn).n_kids } as usize == 0 {
                            let recv = unsafe { *ik2.add(0) };
                            let sb = self.arena_tmp();
                            let sl = unsafe { self.expr_ctype(recv, sb, 128, locals) };
                            if sl > 0
                                && ((sl == 13 && unsafe { z_eq(sb, 13, b"rsx_str_ref_t\0".as_ptr()) })
                                    || (sl == 9 && unsafe { z_eq(sb, 9, b"rsx_str_t\0".as_ptr()) }))
                            {
                                /* pat: TUPLE of two PATH binds (unwrap the
                                 * "path" wrapper on each) */
                                let mut okpat = false;
                                let mut iv: *const u8 = core::ptr::null();
                                let mut ivl = 0usize;
                                let mut cv: *const u8 = core::ptr::null();
                                let mut cvl = 0usize;
                                if unsafe { (*pat).kind } == pm_jit_rsx_ast_kind::TUPLE {
                                    let pk = unsafe { (*pat).kids };
                                    if unsafe { (*pat).n_kids } as usize == 2 {
                                        let mut b0 = unsafe { *pk.add(0) };
                                        let mut b1 = unsafe { *pk.add(1) };
                                        if unsafe { (*b0).kind } == pm_jit_rsx_ast_kind::PATH
                                            && unsafe { z_eq(unsafe { (*b0).text }, unsafe { (*b0).text_len }, b"path\0".as_ptr()) }
                                            && unsafe { (*b0).n_kids } as usize == 1
                                        {
                                            b0 = unsafe { *(*b0).kids.add(0) };
                                        }
                                        if unsafe { (*b1).kind } == pm_jit_rsx_ast_kind::PATH
                                            && unsafe { z_eq(unsafe { (*b1).text }, unsafe { (*b1).text_len }, b"path\0".as_ptr()) }
                                            && unsafe { (*b1).n_kids } as usize == 1
                                        {
                                            b1 = unsafe { *(*b1).kids.add(0) };
                                        }
                                        if unsafe { (*b0).kind } == pm_jit_rsx_ast_kind::PATH
                                            && unsafe { (*b1).kind } == pm_jit_rsx_ast_kind::PATH
                                        {
                                            iv = unsafe { (*b0).text };
                                            ivl = unsafe { (*b0).text_len };
                                            cv = unsafe { (*b1).text };
                                            cvl = unsafe { (*b1).text_len };
                                            okpat = true;
                                        }
                                    }
                                }
                                if !okpat {
                                    unsafe {
                                        self.err(b"unsupported: char_indices pattern (expect two binds)\0".as_ptr(), unsafe { (*s).line });
                                    }
                                    return;
                                }
                                self.str_ref_used = true;
                                unsafe {
                                    (*locals).add(iv, ivl, b"size_t\0".as_ptr(), 7, self.depth + 1);
                                    (*locals).add(cv, cvl, b"uint32_t\0".as_ptr(), 8, self.depth + 1);
                                }
                                let labeled = unsafe { Lower::loop_is_labeled(s) };
                                let lt = unsafe { (*s).text };
                                let ll = unsafe { (*s).text_len };
                                self.indent();
                                if labeled {
                                    unsafe { self.put_lbl(lt, ll, b"_\0".as_ptr()) };
                                    self.out.puts(b": \0".as_ptr());
                                }
                                self.out.puts(b"for (size_t __rsx_ci = 0; __rsx_ci < (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").n; __rsx_ci++) {\n\0".as_ptr());
                                self.depth += 1;
                                self.indent();
                                self.out.puts(b"size_t \0".as_ptr());
                                self.out.put(iv, ivl);
                                self.out.puts(b" = __rsx_ci; uint32_t \0".as_ptr());
                                self.out.put(cv, cvl);
                                self.out.puts(b" = (\0".as_ptr());
                                unsafe { self.emit_expr(recv, locals) };
                                self.out.puts(b").p[__rsx_ci];\n\0".as_ptr());
                                unsafe { self.emit_block_stmt(body, locals) };
                                self.depth -= 1;
                                self.indent();
                                self.out.puts(b"}\n\0".as_ptr());
                                if labeled {
                                    self.indent();
                                    unsafe { self.put_lbl(lt, ll, b"_cont\0".as_ptr()) };
                                    self.out.puts(b": ;\n\0".as_ptr());
                                    self.indent();
                                    unsafe { self.put_lbl(lt, ll, b"_end\0".as_ptr()) };
                                    self.out.puts(b": ;\n\0".as_ptr());
                                }
                                return;
                            }
                        }
                    }
                }
            }
            /* `for c in x.chars()` — a char bind over a &str view.
             * ASCII scanning (the subset documents UTF-8 decode as
             * unsupported — gen scans C signatures, ASCII domain). */
            {
                let ik3 = unsafe { (*iter).kids };
                let ink3 = unsafe { (*iter).n_kids } as usize;
                if ink3 >= 3 {
                    let nm3 = unsafe { *ik3.add(1) };
                    if unsafe { (*nm3).text_len } == 5
                        && unsafe { z_eq(unsafe { (*nm3).text }, 5, b"chars\0".as_ptr()) }
                    {
                        let argsn3 = unsafe { *ik3.add(2) };
                        if unsafe { (*argsn3).n_kids } as usize == 0 {
                            let recv3 = unsafe { *ik3.add(0) };
                            let sb3 = self.arena_tmp();
                            let sl3 = unsafe { self.expr_ctype(recv3, sb3, 128, locals) };
                            if sl3 > 0
                                && ((sl3 == 13
                                    && unsafe { z_eq(sb3, 13, b"rsx_str_ref_t\0".as_ptr()) })
                                    || (sl3 == 9
                                        && unsafe { z_eq(sb3, 9, b"rsx_str_t\0".as_ptr()) }))
                            {
                                /* pat: one PATH bind (unwrap "path") */
                                let mut cv3: *const u8 = core::ptr::null();
                                let mut cvl3 = 0usize;
                                let mut bnode3 = pat;
                                if unsafe { (*pat).kind } == pm_jit_rsx_ast_kind::PATH
                                    && unsafe { z_eq(unsafe { (*pat).text }, unsafe { (*pat).text_len }, b"path\0".as_ptr()) }
                                    && unsafe { (*pat).n_kids } as usize == 1
                                {
                                    bnode3 = unsafe { *(*pat).kids.add(0) };
                                }
                                if unsafe { (*bnode3).kind } == pm_jit_rsx_ast_kind::PATH {
                                    cv3 = unsafe { (*bnode3).text };
                                    cvl3 = unsafe { (*bnode3).text_len };
                                }
                                if cvl3 == 0 {
                                    unsafe {
                                        self.err(b"unsupported: chars pattern (expect one bind)\0".as_ptr(), unsafe { (*s).line });
                                    }
                                    return;
                                }
                                self.str_ref_used = true;
                                unsafe {
                                    (*locals).add(cv3, cvl3, b"uint32_t\0".as_ptr(), 8, self.depth + 1);
                                }
                                let labeled = unsafe { Lower::loop_is_labeled(s) };
                                let lt = unsafe { (*s).text };
                                let ll = unsafe { (*s).text_len };
                                self.indent();
                                if labeled {
                                    unsafe { self.put_lbl(lt, ll, b"_\0".as_ptr()) };
                                    self.out.puts(b": \0".as_ptr());
                                }
                                self.out.puts(b"for (size_t __rsx_ch = 0; __rsx_ch < (\0".as_ptr());
                                unsafe { self.emit_expr(recv3, locals) };
                                self.out.puts(b").n; __rsx_ch++) {\n\0".as_ptr());
                                self.depth += 1;
                                self.indent();
                                self.out.puts(b"uint32_t \0".as_ptr());
                                self.out.put(cv3, cvl3);
                                self.out.puts(b" = (\0".as_ptr());
                                unsafe { self.emit_expr(recv3, locals) };
                                self.out.puts(b").p[__rsx_ch];\n\0".as_ptr());
                                unsafe { self.emit_block_stmt(body, locals) };
                                self.depth -= 1;
                                self.indent();
                                self.out.puts(b"}\n\0".as_ptr());
                                if labeled {
                                    self.indent();
                                    unsafe { self.put_lbl(lt, ll, b"_cont\0".as_ptr()) };
                                    self.out.puts(b": ;\n\0".as_ptr());
                                    self.indent();
                                    unsafe { self.put_lbl(lt, ll, b"_end\0".as_ptr()) };
                                    self.out.puts(b": ;\n\0".as_ptr());
                                }
                                return;
                            }
                        }
                    }
                }
            }
            /* not an enumerate/iter/split — fall through to the range refusal */
        }
        /* `for bind in &v` / `for bind in slice_param` — the &[T] fat-row
         * loop (a &Vec borrow types as the row via the coercion). */
        if unsafe { self.try_emit_for_arr(s, pat, iter, body, locals) } != 0 || !self.ok {
            return;
        }
        if unsafe { (*iter).kind } != pm_jit_rsx_ast_kind::BINARY {
            unsafe {
                self.err(b"unsupported: for over non-range iterator\0".as_ptr(), unsafe { (*s).line });
            }
            return;
        }
        if !unsafe { z_eq(unsafe { (*iter).text }, unsafe { (*iter).text_len }, b"..\0".as_ptr()) }
            && !unsafe { z_eq(unsafe { (*iter).text }, unsafe { (*iter).text_len }, b"..=\0".as_ptr()) }
        {
            unsafe {
                self.err(b"unsupported: for over non-range iterator\0".as_ptr(), unsafe { (*s).line });
            }
            return;
        }
        let inclusive = unsafe { z_eq(unsafe { (*iter).text }, unsafe { (*iter).text_len }, b"..=\0".as_ptr()) };
        let ikids = unsafe { (*iter).kids };
        if unsafe { (*iter).n_kids } < 2 {
            unsafe {
                self.err(b"bad range in for\0".as_ptr(), unsafe { (*s).line });
            }
            return;
        }
        let lo = unsafe { *ikids.add(0) };
        let hi = unsafe { *ikids.add(1) };
        /* pat must be a simple binding: a PATH wrapper (text "path") whose
         * one kid is the binding's own PATH segment — `k` in `for k in …`.
         * (Reading the wrapper's text yields the literal "path", a bug that
         * predates labeled loops; unwrap to the segment.) */
        if unsafe { (*pat).kind } != pm_jit_rsx_ast_kind::PATH {
            unsafe {
                self.err(b"unsupported: for pattern\0".as_ptr(), unsafe { (*s).line });
            }
            return;
        }
        let mut bind = pat;
        if unsafe { z_eq(unsafe { (*pat).text }, unsafe { (*pat).text_len }, b"path\0".as_ptr()) }
            && unsafe { (*pat).n_kids } == 1
        {
            let pk = unsafe { (*pat).kids };
            let seg: *mut pm_jit_rsx_ast_t = unsafe { *pk.add(0) };
            if (unsafe { (*seg).kind }) == pm_jit_rsx_ast_kind::PATH {
                bind = seg;
            }
        }
        let vname = unsafe { (*bind).text };
        let vlen = unsafe { (*bind).text_len };
        /* bound type: from the low end, but a bare integer literal `0` is
         * int32_t by default — when the high end carries a wider type
         * (`0..bn` with bn: usize), take the high end so the loop var
         * compares against the bound without a narrowing surprise. */
        let ct = self.arena_tmp();
        let mut ct_len = unsafe { self.expr_ctype(lo, ct, 128, locals) };
        if ct_len == 0 {
            unsafe {
                self.err(b"cannot infer for-range bound type\0".as_ptr(), unsafe { (*s).line });
            }
            return;
        }
        if (unsafe { (*lo).kind }) == pm_jit_rsx_ast_kind::LITERAL
            && unsafe { z_eq(ct, ct_len, b"int32_t\0".as_ptr()) }
        {
            let ht = self.arena_tmp();
            let hl = unsafe { self.expr_ctype(hi, ht, 128, locals) };
            if hl > 0 && !unsafe { z_eq(ht, hl, b"int32_t\0".as_ptr()) } {
                unsafe {
                    core::ptr::copy_nonoverlapping(ht, ct, hl);
                    *ct.add(hl) = 0;
                }
                ct_len = hl;
            }
        }
        unsafe {
            (*locals).add(vname, vlen, ct, ct_len, self.depth + 1);
        }
        let labeled = unsafe { Lower::loop_is_labeled(s) };
        let lt = unsafe { (*s).text };
        let ll = unsafe { (*s).text_len };
        self.indent();
        if labeled {
            unsafe { self.put_lbl(lt, ll, b"_\0".as_ptr()) };
            self.out.puts(b": \0".as_ptr());
        }
        self.out.puts(b"for (\0".as_ptr());
        self.out.put(ct, ct_len);
        self.out.putc(b' ');
        self.out.put(vname, vlen);
        self.out.puts(b" = \0".as_ptr());
        unsafe { self.emit_expr(lo, locals) };
        self.out.puts(b"; \0".as_ptr());
        self.out.put(vname, vlen);
        if inclusive {
            self.out.puts(b" <= \0".as_ptr());
        } else {
            self.out.puts(b" < \0".as_ptr());
        }
        unsafe { self.emit_expr(hi, locals) };
        self.out.puts(b"; \0".as_ptr());
        self.out.put(vname, vlen);
        self.out.puts(b"++) {\n\0".as_ptr());
        self.depth += 1;
        unsafe { self.emit_block_stmt(body, locals) };
        self.depth -= 1;
        if labeled {
            self.indent();
            unsafe { self.put_lbl(lt, ll, b"_cont\0".as_ptr()) };
            self.out.puts(b": ;\n\0".as_ptr());
        }
        self.indent();
        self.out.puts(b"}\n\0".as_ptr());
        if labeled {
            self.indent();
            unsafe { self.put_lbl(lt, ll, b"_end\0".as_ptr()) };
            self.out.puts(b": ;\n\0".as_ptr());
        }
    }

    unsafe fn emit_return(&mut self, s: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        /* live guards release ahead of the return: a mid-body return with
         * a guard in scope must not leak the acquire (the guard's own
         * scope-exit release never runs past a return). */
        unsafe { self.release_guards_all(locals) };
        self.indent();
        let kids = unsafe { (*s).kids };
        if unsafe { (*s).n_kids } >= 1 {
            let v = unsafe { *kids.add(0) };
            self.out.puts(b"return \0".as_ptr());
            unsafe { self.emit_ret_value(v, locals) };
            self.out.puts(b";\n\0".as_ptr());
            return;
        }
        self.out.puts(b"return;\n\0".as_ptr());
    }
}

