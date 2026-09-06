    unsafe {
        *out.add(at) = 0;
    }
}

fn is_ident_start(b: u8) -> bool {
    b == b'_' || b.is_ascii_alphabetic()
}

fn is_ident_cont(b: u8) -> bool {
    b == b'_' || b.is_ascii_alphanumeric()
}

/* ---- growable token array (arena; concrete — this file is its own subset) */

struct Toks {
    arena: *mut pm_util_mem_arena_t,
    p: *mut pm_jit_rsx_token_t,
    n: usize,
    cap: usize,
    ok: bool,
}

impl Toks {
    unsafe fn new(arena: *mut pm_util_mem_arena_t) -> Toks {
        Toks {
            arena,
            p: core::ptr::null_mut(),
            n: 0,
            cap: 0,
            ok: true,
        }
    }

    unsafe fn push(&mut self, t: pm_jit_rsx_token_t) {
        let nb: *mut pm_jit_rsx_token_t;
        let ncap: usize;
        if !self.ok {
            return;
        }
        if self.n == self.cap {
            ncap = if self.cap == 0 { 256 } else { self.cap * 2 };
            nb = unsafe {
                pm_util_mem_alloc(
                    self.arena,
                    ncap * core::mem::size_of::<pm_jit_rsx_token_t>(),
                )
            } as *mut pm_jit_rsx_token_t;
            if nb.is_null() {
                self.ok = false;
                return;
            }
            if self.n > 0 {
                unsafe {
                    core::ptr::copy_nonoverlapping(self.p, nb, self.n);
                }
            }
            if !self.p.is_null() {
                unsafe {
                    pm_util_mem_free(self.arena, self.p as *mut u8);
                }
            }
            self.p = nb;
            self.cap = ncap;
        }
        unsafe {
            *self.p.add(self.n) = t;
        }
        self.n += 1;
    }
}

/* ---- output byte buffer (dump text, generated C) ---- */

struct Out {
    arena: *mut pm_util_mem_arena_t,
    p: *mut u8,
    len: usize,
    cap: usize,
    ok: bool,
}

impl Out {
    unsafe fn new(arena: *mut pm_util_mem_arena_t) -> Out {
        Out {
            arena,
            p: core::ptr::null_mut(),
            len: 0,
            cap: 0,
            ok: true,
        }
    }

    unsafe fn reserve(&mut self, extra: usize) {
        let mut ncap: usize;
        let nb: *mut u8;
        if !self.ok {
            return;
        }
        if self.len + extra <= self.cap {
            return;
        }
        ncap = if self.cap == 0 { 4096 } else { self.cap };
        while ncap < self.len + extra {
            ncap = ncap * 2;
        }
        nb = unsafe { pm_util_mem_alloc(self.arena, ncap) };
        if nb.is_null() {
            self.ok = false;
            return;
        }
        if self.len > 0 {
            unsafe {
                core::ptr::copy_nonoverlapping(self.p, nb, self.len);
            }
        }
        if !self.p.is_null() {
            unsafe {
                pm_util_mem_free(self.arena, self.p as *mut u8);
            }
        }
        self.p = nb;
        self.cap = ncap;
    }

    unsafe fn put(&mut self, p: *const u8, n: usize) {
        if n == 0 {
            return;
        }
        unsafe {
            self.reserve(n);
        }
        if !self.ok {
            return;
        }
        unsafe {
            core::ptr::copy_nonoverlapping(p, self.p.add(self.len), n);
        }
        self.len += n;
    }

    /* NUL-terminated fixed string. */
    unsafe fn puts(&mut self, z: *const u8) {
        let mut i = 0usize;
        loop {
            let c = unsafe { *z.add(i) };
            if c == 0 {
                break;
            }
            i += 1;
        }
        unsafe {
            self.put(z, i);
        }
    }

    unsafe fn putc(&mut self, c: u8) {
        let one = [c];
        unsafe {
            self.put(one.as_ptr(), 1);
        }
    }

    unsafe fn put_u32(&mut self, v: u32) {
        let mut digs = [0u8; 10];
        let mut n = v;
        let mut di = 0usize;
        let mut j = 0usize;
        while n > 0 && di < digs.len() {
            digs[di] = b'0' + (n % 10) as u8;
            n /= 10;
            di += 1;
        }
        if di == 0 {
            unsafe {
                self.putc(b'0');
            }
            return;
        }
        j = di;
        while j > 0 {
            j -= 1;
            unsafe {
                self.putc(digs[j]);
            }
        }
    }

    unsafe fn put_i64(&mut self, v: i64) {
        if v < 0 {
            unsafe {
                self.putc(b'-');
            }
            if v == i64::MIN {
                unsafe {
                    self.puts(b"9223372036854775808\0".as_ptr());
                }
                return;
            }
            unsafe {
                self.put_u32((-v) as u32);
            }
            return;
        }
        unsafe {
            self.put_u32(v as u32);
        }
    }
}

/* ---- lexer ---- */

const MAX_TOKS: usize = 1048576;

struct Lexer {
    arena: *mut pm_util_mem_arena_t,
    src: *const u8,
    src_len: usize,
    pos: usize,
    line: u32,
    toks: Toks,
    errbuf: *mut u8,
    errcap: usize,
    ok: bool,
}

impl Lexer {
    unsafe fn err(&mut self, msg: *const u8) {
        if self.ok {
            unsafe {
                err_set(self.errbuf, self.errcap, msg, self.line);
            }
            self.ok = false;
        }
    }

    unsafe fn at(&self, i: usize) -> u8 {
        if i < self.src_len {
            unsafe { *self.src.add(i) }
        } else {
            0
        }
    }

    unsafe fn peek(&self, k: usize) -> u8 {
        unsafe { self.at(self.pos + k) }
    }

    unsafe fn push(&mut self, kind: pm_jit_rsx_tok_kind, start: usize, end: usize) {
        let n = end - start;
        let p = unsafe { pm_util_mem_alloc(self.arena, n + 1) };
        if p.is_null() {
            unsafe {
                self.err(b"arena exhausted\0".as_ptr());
            }
            return;
        }
        unsafe {
            core::ptr::copy_nonoverlapping(self.src.add(start), p, n);
            *p.add(n) = 0;
        }
        unsafe {
            self.toks.push(pm_jit_rsx_token_t {
                kind,
                line: self.line,
                text: p,
                text_len: n,
            });
        }
        if !self.toks.ok {
            self.ok = false;
        }
    }

    /* Whitespace and both comment forms (block comments nest). */
    unsafe fn skip_trivia(&mut self) {
        let mut c: u8;
        loop {
            if self.pos >= self.src_len {
                return;
            }
            c = unsafe { *self.src.add(self.pos) };
            if c == b'\n' {
                self.line += 1;
                self.pos += 1;
                continue;
            }
            if c == b' ' || c == b'\t' || c == b'\r' {
                self.pos += 1;
                continue;
            }
            if c == b'/' && unsafe { self.peek(1) } == b'/' {
                while self.pos < self.src_len && unsafe { *self.src.add(self.pos) } != b'\n' {
                    self.pos += 1;
                }
                continue;
            }
            if c == b'/' && unsafe { self.peek(1) } == b'*' {
                self.pos += 2;
                let mut depth = 1usize;
                while self.pos < self.src_len && depth > 0 {
                    let d = unsafe { *self.src.add(self.pos) };
                    if d == b'/' && unsafe { self.peek(1) } == b'*' {
                        depth += 1;
                        self.pos += 2;
                    } else if d == b'*' && unsafe { self.peek(1) } == b'/' {
                        depth -= 1;
                        self.pos += 2;
                    } else {
                        if d == b'\n' {
                            self.line += 1;
                        }
                        self.pos += 1;
                    }
                }
                if depth != 0 {
                    unsafe {
                        self.err(b"unterminated block comment\0".as_ptr());
                    }
                    return;
                }
                continue;
            }
            return;
        }
    }

    /* self.pos is at the opening quote. */
    unsafe fn skip_string_body(&mut self) {
        self.pos += 1;
        while self.pos < self.src_len {
            let c = unsafe { *self.src.add(self.pos) };
            if c == b'\\' {
                if self.pos + 1 >= self.src_len {
                    break;
                }
                self.pos += 2;
                continue;
            }
            if c == b'"' {
                self.pos += 1;
                return;
            }
            if c == b'\n' {
                self.line += 1;
            }
            self.pos += 1;
        }
        unsafe {
            self.err(b"unterminated string\0".as_ptr());
        }
    }

    /* self.pos is at the opening quote. Distinguishes the three forms that
     * start here: char literal `'x'` / `'\n'` / `'\\'` (true, consumed),
     * lifetime `'a` or `'static` (false, nothing consumed — caller handles),
     * and anything else unterminated (true, refused). */
    unsafe fn skip_char_body(&mut self) -> bool {
        let p1 = unsafe { self.peek(1) };
        if p1 == b'\\' {
            /* escaped form: `'\\X'` — exactly one escaped payload char. */
            if unsafe { self.peek(3) } == b'\'' {
                self.pos += 4;
                return true;
            }
            unsafe {
                self.err(b"unterminated char literal\0".as_ptr());
            }
            return true;
        }
        if p1 == b'\'' {
            /* `''` — not a char literal in this subset; leave for caller. */
            return false;
        }
        if p1 != 0 && unsafe { self.peek(2) } == b'\'' {
            /* plain form `'x'`. */
            self.pos += 3;
            return true;
        }
        /* `'a` / `'static` — lifetime only when a word follows. */
        if is_ident_start(p1) {
            return false;
        }
        unsafe {
            self.err(b"unterminated char literal\0".as_ptr());
        }
        true
    }

    unsafe fn run(&mut self) {
        let start = 0usize;
        loop {
            unsafe {
                self.skip_trivia();
            }
            if !self.ok {
                return;
            }
            if self.pos >= self.src_len {
                unsafe {
                    self.toks.push(pm_jit_rsx_token_t {
                        kind: pm_jit_rsx_tok_kind::END,
                        line: self.line,
                        text: b"\0".as_ptr(),
                        text_len: 0,
                    });
                }
                if !self.toks.ok {
                    self.ok = false;
                }
                return;
            }
            if self.toks.n >= MAX_TOKS {
                unsafe {
                    self.err(b"token limit exceeded\0".as_ptr());
                }
                return;
            }
            let at = self.pos;
            let b = unsafe { *self.src.add(at) };
            if is_ident_start(b) {
                unsafe {
                    self.lex_ident();
                }
                continue;
            }
            if b.is_ascii_digit() {
                unsafe {
                    self.lex_number();
                }
                continue;
            }
            if b == b'"' {
                unsafe {
                    self.skip_string_body();
                }
                if self.ok {
                    unsafe {
                        self.push(pm_jit_rsx_tok_kind::STRING_LITERAL, at, self.pos);
                    }
                }
                continue;
            }
            if b == b'\'' {
                if unsafe { self.skip_char_body() } {
                    if self.ok {
                        unsafe {
                            self.push(pm_jit_rsx_tok_kind::CHAR_LITERAL, at, self.pos);
                        }
                    }
                    continue;
                }
                self.pos += 1;
                if self.pos < self.src_len
                    && is_ident_start(unsafe { *self.src.add(self.pos) })
                {
                    while self.pos < self.src_len
                        && is_ident_cont(unsafe { *self.src.add(self.pos) })
                    {
                        self.pos += 1;
                    }
                    unsafe {
                        self.push(pm_jit_rsx_tok_kind::LIFETIME, at, self.pos);
                    }
                    continue;
                }
                self.pos = at;
                unsafe {
                    self.err(b"unsupported character\0".as_ptr());
                }
                continue;
            }
            unsafe {
                self.lex_punct();
            }
        }
    }

    unsafe fn lex_ident(&mut self) {
        let start = self.pos;
        let mut wp: *const u8;
        let mut wn: usize;
        while self.pos < self.src_len && is_ident_cont(unsafe { *self.src.add(self.pos) }) {
            self.pos += 1;
        }
        wp = unsafe { self.src.add(start) };
        wn = self.pos - start;
        /* Raw / byte string prefixes: r"…" r#"…"# b"…" br#"…"#. */
        if unsafe { z_eq(wp, wn, b"r\0".as_ptr()) }
            && (unsafe { self.peek(0) } == b'"' || unsafe { self.peek(0) } == b'#')
        {
            /* `r#ident` — a raw identifier (keyword escape), not a string:
             * the token text is the bare ident (`r#gen` == `gen`), exactly
             * what Rust name resolution sees. */
            if unsafe { self.peek(0) } == b'#'
                && self.pos + 1 < self.src_len
                && is_ident_start(unsafe { *self.src.add(self.pos + 1) })
            {
                let id_start = self.pos + 1;
                self.pos = id_start;
                while self.pos < self.src_len
                    && is_ident_cont(unsafe { *self.src.add(self.pos) })
                {
                    self.pos += 1;
                }
                unsafe {
                    self.push(pm_jit_rsx_tok_kind::IDENT, id_start, self.pos);
                }
                return;
            }
            unsafe {
                self.lex_raw_string(start, 0);
            }
            return;
        }
        if unsafe { z_eq(wp, wn, b"br\0".as_ptr()) }
            && (unsafe { self.peek(0) } == b'"' || unsafe { self.peek(0) } == b'#')
        {
            unsafe {
                self.lex_raw_string(start, 1);
            }
            return;
        }
        if unsafe { z_eq(wp, wn, b"b\0".as_ptr()) } {
            if unsafe { self.peek(0) } == b'"' {
                unsafe {
                    self.skip_string_body();
                }
                if self.ok {
                    unsafe {
                        self.push(pm_jit_rsx_tok_kind::BYTE_STR_LITERAL, start, self.pos);
                    }
                }
                return;
            }
            if unsafe { self.peek(0) } == b'\'' {
                if unsafe { self.skip_char_body() } {
                    if self.ok {
                        unsafe {
                            self.push(pm_jit_rsx_tok_kind::BYTE_STR_LITERAL, start, self.pos);
                        }
                    }
                    return;
                }
                /* `'` present but not a literal form — `b` stays an ident. */
            }
        }
        /* Macro invocation: ident! then a balanced delimiter group. One
         * token — the parser keeps it opaque, lowering decides
         * pass-through (PM_MOD_* ctors) vs honest refusal. */
        if unsafe { self.peek(0) } == b'!' {
            let n = unsafe { self.peek(1) };
            if n == b'(' || n == b'[' || n == b'{' {
                let close = match n {
                    b'(' => b')',
                    b'[' => b']',
                    _ => b'}',
                };
                self.pos += 2;
                let mut depth = 1usize;
                while self.pos < self.src_len && depth > 0 {
                    let c = unsafe { *self.src.add(self.pos) };
                    if c == b'"' {
                        unsafe {
                            self.skip_string_body();
                        }
                        if !self.ok {
                            return;
                        }
                        continue;
                    }
                    if c == b'\'' && unsafe { self.skip_char_body() } {
                        continue;
                    }
                    if c == b'/' && unsafe { self.peek(1) } == b'/' {
                        unsafe {
                            self.skip_trivia();
                        }
                        if !self.ok {
                            return;
                        }
                        continue;
                    }
                    if c == b'\n' {
                        self.line += 1;
                    }
                    if c == n {
                        depth += 1;
                    } else if c == close {
                        depth -= 1;
                    }
                    self.pos += 1;
                }
                if depth != 0 {
                    unsafe {
                        self.err(b"unterminated macro invocation\0".as_ptr());
                    }
                    return;
                }
                unsafe {
                    self.push(pm_jit_rsx_tok_kind::MACRO_INVOC, start, self.pos);
                }
                return;
            }
        }
        unsafe {
            self.push(pm_jit_rsx_tok_kind::IDENT, start, self.pos);
        }
    }

    /* pos is at `"` (zero hashes) or at the first `#`. */
    unsafe fn lex_raw_string(&mut self, start: usize, is_byte: usize) {
        let mut hashes = 0usize;
        let mut done = false;
        while unsafe { self.peek(0) } == b'#' {
            hashes += 1;
            self.pos += 1;
        }
        if unsafe { self.peek(0) } != b'"' {
            self.pos = start;
            unsafe {
                self.err(b"malformed raw string\0".as_ptr());
            }
            return;
        }
        self.pos += 1;
        while self.pos < self.src_len {
            let c = unsafe { *self.src.add(self.pos) };
            if c == b'\n' {
                self.line += 1;
            }
            if c == b'"' {
                let mut k = 0usize;
                while k < hashes && unsafe { self.peek(k + 1) } == b'#' {
                    k += 1;
                }
                if k == hashes {
                    self.pos += 1 + hashes;
                    done = true;
                    break;
                }
            }
            self.pos += 1;
        }
        if !done {
            unsafe {
                self.err(b"unterminated raw string\0".as_ptr());
            }
            return;
        }
        if is_byte != 0 {
            unsafe {
                self.push(pm_jit_rsx_tok_kind::BYTE_STR_LITERAL, start, self.pos);
            }
        } else {
            unsafe {
                self.push(pm_jit_rsx_tok_kind::STRING_LITERAL, start, self.pos);
            }
        }
    }

    unsafe fn lex_number(&mut self) {
        let start = self.pos;
        let mut is_float = false;
        let sfx_start: usize;
        let sfx_len: usize;
        if unsafe { *self.src.add(self.pos) } == b'0'
            && (unsafe { self.peek(1) } == b'x'
                || unsafe { self.peek(1) } == b'o'
                || unsafe { self.peek(1) } == b'b')
        {
            self.pos += 2;
            while self.pos < self.src_len {
                let c = unsafe { *self.src.add(self.pos) };
                if c.is_ascii_alphanumeric() || c == b'_' {
                    self.pos += 1;
                } else {
                    break;
                }
            }
        } else {
            while self.pos < self.src_len {
                let c = unsafe { *self.src.add(self.pos) };
                if c.is_ascii_digit() || c == b'_' {
                    self.pos += 1;
                } else {
                    break;
                }
            }
            /* `1.5` is a float; `1..2` is INT RANGE INT; `1.` followed by
             * a word char is `1.method()` (field call on int literal). */
            if unsafe { self.peek(0) } == b'.'
                && unsafe { self.peek(1) } != b'.'
                && !is_ident_start(unsafe { self.peek(1) })
            {
                is_float = true;
                self.pos += 1;
                while self.pos < self.src_len {
                    let c = unsafe { *self.src.add(self.pos) };
                    if c.is_ascii_digit() || c == b'_' {
                        self.pos += 1;
                    } else {
                        break;
                    }
                }
            }
            if unsafe { self.peek(0) } == b'e' || unsafe { self.peek(0) } == b'E' {
                let mut k = 1usize;
                if unsafe { self.peek(1) } == b'+' || unsafe { self.peek(1) } == b'-' {
                    k = 2;
                }
                if unsafe { self.peek(k) }.is_ascii_digit() {
                    is_float = true;
                    self.pos += k;
                    while self.pos < self.src_len {
                        let c = unsafe { *self.src.add(self.pos) };
                        if c.is_ascii_digit() || c == b'_' {
                            self.pos += 1;
                        } else {
                            break;
                        }
                    }
                }
            }
        }
        sfx_start = self.pos;
        while self.pos < self.src_len && is_ident_cont(unsafe { *self.src.add(self.pos) }) {
            self.pos += 1;
        }
        sfx_len = self.pos - sfx_start;
        if !unsafe { num_suffix_ok(self.src.add(sfx_start), sfx_len, is_float) } {
            self.pos = start;
            unsafe {
                self.err(b"unsupported number suffix\0".as_ptr());
            }
            return;
        }
        if is_float {
            unsafe {
                self.push(pm_jit_rsx_tok_kind::FLOAT_LITERAL, start, self.pos);
            }
        } else {
            unsafe {
                self.push(pm_jit_rsx_tok_kind::INT_LITERAL, start, self.pos);
            }
        }
    }

    /* three-char punct lookahead helper (kept a plain fn — no closures in
     * the subset this compiler's own source must stay inside) */
    unsafe fn punct3(&self, start: usize, a: u8, b: u8, d: u8) -> bool {
        start + 2 < self.src_len
            && unsafe { *self.src.add(start) } == a
            && unsafe { *self.src.add(start + 1) } == b
            && unsafe { *self.src.add(start + 2) } == d
    }

    unsafe fn lex_punct(&mut self) {
        let start = self.pos;
        let c = unsafe { *self.src.add(start) };
        let mut kind = pm_jit_rsx_tok_kind::PUNCT;
        let mut len = 1usize;
        if unsafe { self.punct3(start, b'<', b'<', b'=') } {
            kind = pm_jit_rsx_tok_kind::SHLEQ;
            len = 3;
        } else if unsafe { self.punct3(start, b'>', b'>', b'=') } {
            kind = pm_jit_rsx_tok_kind::SHREQ;
            len = 3;
        } else if unsafe { self.punct3(start, b'.', b'.', b'=') } {
            kind = pm_jit_rsx_tok_kind::RANGE;
            len = 3;
        }
        let mut k2 = pm_jit_rsx_tok_kind::PUNCT;
        let mut l2 = 0usize;
        if l2 == 0 && kind == pm_jit_rsx_tok_kind::PUNCT {
            let d2 = start + 1 < self.src_len;
            let cc = if d2 { unsafe { *self.src.add(start + 1) } } else { 0 };
            if d2 {
                if c == b'-' && cc == b'>' {
                    k2 = pm_jit_rsx_tok_kind::ARROW;
                    l2 = 2;
                } else if c == b'=' && cc == b'>' {
                    k2 = pm_jit_rsx_tok_kind::FAT_ARROW;
                    l2 = 2;
                } else if c == b':' && cc == b':' {
                    k2 = pm_jit_rsx_tok_kind::DOUBLE_COLON;
                    l2 = 2;
                } else if c == b'.' && cc == b'.' {
                    k2 = pm_jit_rsx_tok_kind::RANGE;
                    l2 = 2;
                } else if c == b'<' && cc == b'<' {
                    k2 = pm_jit_rsx_tok_kind::SHL;
                    l2 = 2;
                } else if c == b'>' && cc == b'>' {
                    k2 = pm_jit_rsx_tok_kind::SHR;
                    l2 = 2;
                } else if c == b'<' && cc == b'=' {
                    k2 = pm_jit_rsx_tok_kind::LE;
                    l2 = 2;
                } else if c == b'>' && cc == b'=' {
                    k2 = pm_jit_rsx_tok_kind::GE;
                    l2 = 2;
                } else if c == b'=' && cc == b'=' {
                    k2 = pm_jit_rsx_tok_kind::EQ;
                    l2 = 2;
                } else if c == b'!' && cc == b'=' {
                    k2 = pm_jit_rsx_tok_kind::NE;
                    l2 = 2;
                } else if c == b'&' && cc == b'&' {
                    k2 = pm_jit_rsx_tok_kind::ANDAND;
                    l2 = 2;
                } else if c == b'|' && cc == b'|' {
                    k2 = pm_jit_rsx_tok_kind::OROR;
                    l2 = 2;
                } else if c == b'+' && cc == b'=' {
                    k2 = pm_jit_rsx_tok_kind::PLUSEQ;
                    l2 = 2;
                } else if c == b'-' && cc == b'=' {
                    k2 = pm_jit_rsx_tok_kind::MINUSEQ;
                    l2 = 2;
                } else if c == b'*' && cc == b'=' {
                    k2 = pm_jit_rsx_tok_kind::STAREQ;
                    l2 = 2;
                } else if c == b'/' && cc == b'=' {
                    k2 = pm_jit_rsx_tok_kind::SLASHEQ;
                    l2 = 2;
                } else if c == b'%' && cc == b'=' {
                    k2 = pm_jit_rsx_tok_kind::PERCENTEQ;
                    l2 = 2;
                } else if c == b'^' && cc == b'=' {
                    k2 = pm_jit_rsx_tok_kind::CARETEQ;
                    l2 = 2;
                } else if c == b'&' && cc == b'=' {
                    k2 = pm_jit_rsx_tok_kind::AMPEQ;
                    l2 = 2;
                } else if c == b'|' && cc == b'=' {
                    k2 = pm_jit_rsx_tok_kind::OREQ;
                    l2 = 2;
                }
            }
        }
        if l2 != 0 {
            kind = k2;
            len = l2;
        }
        if len == 1 && kind == pm_jit_rsx_tok_kind::PUNCT {
            let ok = c == b'+' || c == b'-' || c == b'*' || c == b'/';
            let ok2 = c == b'%' || c == b'^' || c == b'!' || c == b'&' || c == b'|';
            let ok3 = c == b'<' || c == b'>' || c == b'=' || c == b'(' || c == b')';
            let ok4 = c == b'[' || c == b']' || c == b'{' || c == b'}' || c == b',';
            let ok5 = c == b';' || c == b':' || c == b'.' || c == b'#' || c == b'?';
            if !ok && !ok2 && !ok3 && !ok4 && !ok5 {
                let mut m = [0u8; 32];
                unsafe {
                    msg_char(m.as_mut_ptr(), m.len(), c);
                }
                self.pos = start;
                unsafe {
                    self.err(m.as_ptr());
                }
                return;
            }
        }
        self.pos = start + len;
        unsafe {
            self.push(kind, start, self.pos);
        }
    }
}

unsafe fn num_suffix_ok(s: *const u8, n: usize, is_float: bool) -> bool {
    if n == 0 {
        return true;
    }
    if is_float {
        if unsafe { z_eq(s, n, b"f32\0".as_ptr()) } {
            return true;
        }
        return unsafe { z_eq(s, n, b"f64\0".as_ptr()) };
    }
    if unsafe { z_eq(s, n, b"u8\0".as_ptr()) } {
        return true;
    }
    if unsafe { z_eq(s, n, b"u16\0".as_ptr()) } {
        return true;
    }
    if unsafe { z_eq(s, n, b"u32\0".as_ptr()) } {
        return true;
    }
    if unsafe { z_eq(s, n, b"u64\0".as_ptr()) } {
        return true;
    }
    if unsafe { z_eq(s, n, b"u128\0".as_ptr()) } {
        return false;
    }
    if unsafe { z_eq(s, n, b"usize\0".as_ptr()) } {
        return true;
    }
    if unsafe { z_eq(s, n, b"i8\0".as_ptr()) } {
        return true;
    }
    if unsafe { z_eq(s, n, b"i16\0".as_ptr()) } {
        return true;
    }
    if unsafe { z_eq(s, n, b"i32\0".as_ptr()) } {
        return true;
    }
    if unsafe { z_eq(s, n, b"i64\0".as_ptr()) } {
        return true;
    }
    if unsafe { z_eq(s, n, b"i128\0".as_ptr()) } {
        return false;
    }
    unsafe { z_eq(s, n, b"isize\0".as_ptr()) }
}

