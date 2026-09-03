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
            pm_jit_rsx_ast_kind::MATCH => unsafe { self.emit_match(s, locals) },
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
        if !is_unsafe_block {
            unsafe {
                (*locals).drop_scope();
            }
        }
        unsafe {
            (*locals).epoch = saved_epoch;
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
                unsafe { self.emit_expr(b, locals) };
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
                        return;
                    }
                    if k == pm_jit_rsx_ast_kind::MATCH {
                        unsafe { self.emit_match_value(st2, locals, temp, temp_len) };
                        unsafe {
                            (*locals).epoch = saved_epoch;
                        }
                        return;
                    }
                    if k == pm_jit_rsx_ast_kind::BLOCK {
                        unsafe { self.emit_block_value(st2, locals, temp, temp_len) };
                        unsafe {
                            (*locals).epoch = saved_epoch;
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
                        return;
                    }
                    unsafe {
                        self.err(
                            b"unsupported: value-position block expression\0".as_ptr(),
                            unsafe { (*st2).line },
                        );
                    }
                    unsafe { self.end_block(locals, is_unsafe_block2, saved_epoch) };
                    return;
                }
                /* plain tail expr: emit as `temp = expr;` when a temp is given */
                if !temp.is_null() {
                    self.indent();
                    self.out.put(temp, temp_len);
                    self.out.puts(b" = \0".as_ptr());
                    unsafe { self.emit_expr(st2, locals) };
                    self.out.puts(b";\n\0".as_ptr());
                    unsafe { self.end_block(locals, is_unsafe_block2, saved_epoch) };
                    return;
                }
            }
            unsafe { self.emit_stmt(st, locals, 0) };
            i += 1;
        }
        unsafe { self.end_block(locals, is_unsafe_block2, saved_epoch) };
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
            self.indent();
            self.out.put(ct, ct_len);
            self.out.puts(b" __rsx_le = \0".as_ptr());
            unsafe { self.emit_expr(init, locals) };
            self.out.puts(b";\n\0".as_ptr());
            self.indent();
            self.out.puts(b"if (!__rsx_le._has) {\n\0".as_ptr());
            self.depth += 1;
            unsafe { self.emit_block_stmt(els, locals) };
            self.depth -= 1;
            self.indent();
            self.out.puts(b"}\n\0".as_ptr());
            if is_tuple_bind {
                /* Option-of-tuple: cur_opt_elem drives the shared destructure */
                unsafe { core::ptr::copy_nonoverlapping(elbuf, self.cur_opt_elem.as_mut_ptr(), eln) };
                self.cur_opt_elem_len = eln;
                unsafe { self.emit_some_binds(bind, b"__rsx_le\0".as_ptr(), 8, ct, ct_len, locals) };
            } else {
                self.indent();
                self.out.put(elbuf, eln);
                self.out.putc(b' ');
                self.out.put(bname, blen);
                self.out.puts(b" = __rsx_le._v;\n\0".as_ptr());
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
        let ct_len = unsafe { self.expr_ctype(init, ct, 128, locals) };
        if ct_len == 0 || ct_len >= 128 {
            unsafe {
                self.err(b"cannot infer tuple let-else type\0".as_ptr(), line);
            }
            return;
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
        if ik == pm_jit_rsx_ast_kind::IF {
            /* cur_ret carries the expected tuple to the arm bodies so
             * unsuffixed literals in `(0, 0)` mint the right signature */
            let saved_ret_len = self.cur_ret_len;
            {
                let mut j = 0usize;
                while j < ct_len && j < 127 {
                    self.cur_ret[j] = unsafe { *ct.add(j) };
                    j += 1;
                }
                self.cur_ret_len = ct_len;
            }
            unsafe { self.emit_if_value(init, locals, tmp, tmp_len) };
            self.cur_ret_len = saved_ret_len;
        } else if ik == pm_jit_rsx_ast_kind::MATCH {
            let saved_ret_len = self.cur_ret_len;
            {
                let mut j = 0usize;
                while j < ct_len && j < 127 {
                    self.cur_ret[j] = unsafe { *ct.add(j) };
                    j += 1;
                }
                self.cur_ret_len = ct_len;
            }
            unsafe { self.emit_match_value(init, locals, tmp, tmp_len) };
            self.cur_ret_len = saved_ret_len;
        } else {
            self.indent();
            self.out.put(tmp, tmp_len);
            self.out.puts(b" = \0".as_ptr());
            unsafe { self.emit_expr(init, locals) };
            self.out.puts(b";\n\0".as_ptr());
        }
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
         * runs for side effects (registration calls, reserved values). */
        if !have_name || (name_len == 1 && unsafe { z_eq(name, 1, b"_\0".as_ptr()) } && ty.is_null()) {
            if !init.is_null() {
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
        } else {
            unsafe {
                self.err(b"let with no type and no initializer\0".as_ptr(), line);
            }
            return;
        }
        /* Shadowing: same spelling + same C type in the same block reuses
         * the declaration (plain assignment). Any other scope declares
         * fresh — C block scopes take care of nesting. */
        let mut reuse = false;
        unsafe {
            if (*locals).same_type_same_scope(name, name_len, ct, ct_len, self.depth, (*locals).epoch)
            {
                reuse = true;
            }
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
        /* locals are never const in C — Rust's deferred-init
         * (`let x; if c { x = 1 } else { x = 0 }`) writes them after the
         * declaration, and inference may have carried a `const ` prefix
         * (deref of a `*const T`). Strip it. */
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
                if ik == pm_jit_rsx_ast_kind::IF {
                    unsafe { self.emit_if_value(init, locals, name, name_len) };
                } else {
                    unsafe { self.emit_match_value(init, locals, name, name_len) };
                }
                return;
            }
            if ik == pm_jit_rsx_ast_kind::CLOSURE {
                unsafe {
                    self.err(b"unsupported: closure in let initializer\0".as_ptr(), line);
                }
                return;
            }
        }
        self.indent();
        unsafe {
            self.emit_declarator(ct, ct_len, name, name_len);
        }
        if !init.is_null() {
            self.out.puts(b" = \0".as_ptr());
            unsafe { self.emit_expr(init, locals) };
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
        unsafe { self.emit_block_tail_ret(then_b, locals) };
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
                unsafe { self.emit_block_tail_ret(els, locals) };
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
            unsafe { self.emit_expr(e, locals) };
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
                    let n = unsafe { self.expr_ctype(body, ct, 128, locals) };
                    if n > 0 {
                        ct_len = n;
                    }
                    break;
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
            unsafe { self.emit_expr(st, locals) };
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
        let scrut = unsafe { *kids.add(0) };
        let temp = b"__rsx_m\0".as_ptr();
        let temp_len = 7usize;
        let ct = self.arena_tmp();
        let ct_len = unsafe { self.expr_ctype(scrut, ct, 128, locals) };
        if ct_len == 0 {
            unsafe {
                self.err(b"cannot infer match scrutinee type\0".as_ptr(), unsafe { (*s).line });
            }
            return;
        }
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
        self.indent();
        self.out.put(ct, ct_len);
        self.out.putc(b' ');
        self.out.put(temp, temp_len);
        self.out.puts(b" = \0".as_ptr());
        unsafe { self.emit_expr(scrut, locals) };
        self.out.puts(b";\n\0".as_ptr());
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
            let body = unsafe { *ak.add(1) };
            self.indent();
            if !first {
                self.out.puts(b"else \0".as_ptr());
            }
            first = false;
            /* wildcard arm: unconditional else */
            let is_wc = unsafe { (*pat).kind } == pm_jit_rsx_ast_kind::PATH
                && unsafe { z_eq(unsafe { (*pat).text }, unsafe { (*pat).text_len }, b"_\0".as_ptr()) };
            if !is_wc {
                self.out.puts(b"if (\0".as_ptr());
                unsafe { self.emit_pat_test(pat, temp, temp_len, locals) };
                self.out.puts(b") \0".as_ptr());
            }
            self.out.puts(b"{\n\0".as_ptr());
            self.depth += 1;
            unsafe { (*locals).note_scope() };
            /* Some(bind): declare the bind from the scrutinee temp before the
             * body — pointer payload aliases the temp, integer payload (struct
             * Option) copies ._v. Same shape as emit_match_value's arm. */
            if unsafe { (*pat).kind } == pm_jit_rsx_ast_kind::PATH {
                let pk = unsafe { (*pat).kids };
                let pnk = unsafe { (*pat).n_kids } as usize;
                if pnk >= 2 {
                    let head = unsafe { *pk.add(0) };
                    let bind = unsafe { *pk.add(1) };
                    if unsafe { (*head).kind } == pm_jit_rsx_ast_kind::PATH
                        && unsafe { z_eq(unsafe { (*head).text }, unsafe { (*head).text_len }, b"Some\0".as_ptr()) }
                        && (unsafe { (*bind).kind } == pm_jit_rsx_ast_kind::PATH
                            || unsafe { (*bind).kind } == pm_jit_rsx_ast_kind::TUPLE)
                    {
                        unsafe { self.emit_some_binds(bind, temp, temp_len, ct, ct_len, locals) };
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
    }

    /* match arms with pattern-local bindings: the binding declared inside the
     * `if (…)` test needs to be visible in the body. `Some(x)` lowers to a
     * pointer nullity test with the bind declared before the if — done by
     * rewriting the arm as: `if (sv != 0) { T x = sv; body }`. That is what
     * emit_pat_test's Some-branch does inline (it emits the decl after the
     * test, still inside the if's condition, which is wrong for scoping), so
     * match with Some-patterns uses a pre-declared temp instead. */

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
        let scrut = unsafe { *kids.add(0) };
        let st = b"__rsx_m\0".as_ptr();
        let st_len = 7usize;
        let ct = self.arena_tmp();
        let ct_len = unsafe { self.expr_ctype(scrut, ct, 128, locals) };
        if ct_len == 0 {
            unsafe {
                self.err(b"cannot infer match scrutinee type\0".as_ptr(), unsafe { (*s).line });
            }
            return;
        }
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
        self.indent();
        self.out.put(ct, ct_len);
        self.out.putc(b' ');
        self.out.put(st, st_len);
        self.out.puts(b" = \0".as_ptr());
        unsafe { self.emit_expr(scrut, locals) };
        self.out.puts(b";\n\0".as_ptr());
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
            let body = unsafe { *ak.add(1) };
            self.indent();
            if !first {
                self.out.puts(b"else \0".as_ptr());
            }
            first = false;
            let is_wc = unsafe { (*pat).kind } == pm_jit_rsx_ast_kind::PATH
                && unsafe { z_eq(unsafe { (*pat).text }, unsafe { (*pat).text_len }, b"_\0".as_ptr()) };
            if !is_wc {
                self.out.puts(b"if (\0".as_ptr());
                unsafe { self.emit_pat_test(pat, st, st_len, locals) };
                self.out.puts(b") \0".as_ptr());
            }
            self.out.puts(b"{\n\0".as_ptr());
            /* Some(bind): the scrutinee temp IS the inner pointer (pointer
             * payload) or carries it in ._v (integer payload) — declare the
             * bind as an alias so the arm body names it. */
            if unsafe { (*pat).kind } == pm_jit_rsx_ast_kind::PATH {
                let pk = unsafe { (*pat).kids };
                let pnk = unsafe { (*pat).n_kids } as usize;
                if pnk >= 2 {
                    let head = unsafe { *pk.add(0) };
                    let bind = unsafe { *pk.add(1) };
                    if unsafe { (*head).kind } == pm_jit_rsx_ast_kind::PATH
                        && unsafe { z_eq(unsafe { (*head).text }, unsafe { (*head).text_len }, b"Some\0".as_ptr()) }
                        && (unsafe { (*bind).kind } == pm_jit_rsx_ast_kind::PATH
                            || unsafe { (*bind).kind } == pm_jit_rsx_ast_kind::TUPLE)
                    {
                        self.depth += 1;
                        unsafe { self.emit_some_binds(bind, st, st_len, ct, ct_len, locals) };
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
                self.out.put(sv, sv_len);
                self.out.puts(b" == \0".as_ptr());
                /* strip the enum prefix: emit just the leaf, uppercased? No —
                 * enum variants lower to `Enum_Variant` constants. */
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
             * enum elided (Rust allows it inside a match on that type) or a
             * plain binding — match anything (binding). */
            self.out.putc(b'1');
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
        /* receiver must be a fixed array: element spelling + compile-time
         * length via sizeof. */
        let bt = self.arena_tmp();
        let btn = unsafe { self.expr_ctype(base, bt, 128, locals) };
        if btn == 0 {
            return 0;
        }
        let mut bracket = btn;
        let mut elem_len = 0usize;
        let mut i = 0usize;
        while i < btn {
            if unsafe { *bt.add(i) } == b'[' {
                bracket = i;
                elem_len = i;
                break;
            }
            i += 1;
        }
        if bracket >= btn {
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
        /* length once: size_t bn_n = sizeof(R)/sizeof(R[0]); */
        self.out.puts(b"size_t \0".as_ptr());
        self.out.put(len_var, at2);
        self.out.puts(b" = sizeof(\0".as_ptr());
        unsafe { self.emit_expr(base, locals) };
        self.out.puts(b") / sizeof((\0".as_ptr());
        unsafe { self.emit_expr(base, locals) };
        self.out.puts(b")[0]);\0".as_ptr());
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
        self.out.put(bt, elem_len);
        self.out.putc(b' ');
        self.out.put(bname, blen);
        self.out.puts(b" = (\0".as_ptr());
        unsafe { self.emit_expr(base, locals) };
        self.out.puts(b")[\0".as_ptr());
        self.out.put(ivar, at);
        self.out.puts(b"];\n\0".as_ptr());
        unsafe {
            (*locals).add(bname, blen, bt, elem_len, self.depth);
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
            self.out.puts(b") {\0".as_ptr());
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
            let ik = unsafe { (*iter).kids };
            let ink = unsafe { (*iter).n_kids } as usize;
            if unsafe { self.try_emit_for_iter(s, pat, iter, body, locals) } != 0 || !self.ok {
                return;
            }
            /* not an enumerate/iter — fall through to the range refusal */
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
        self.indent();
        let kids = unsafe { (*s).kids };
        if unsafe { (*s).n_kids } >= 1 {
            let v = unsafe { *kids.add(0) };
            self.out.puts(b"return \0".as_ptr());
            unsafe { self.emit_expr(v, locals) };
            self.out.puts(b";\n\0".as_ptr());
            return;
        }
        self.out.puts(b"return;\n\0".as_ptr());
    }
}

