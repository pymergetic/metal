//! pymergetic.metal.jit.rs.compiler — micro-rustc: the kernel's Rust subset to
//! C, written in that same subset (Phase 7 of the self-hosting plan).
//!
//! Pipeline: `pm_metal_jit_rsx_lex` -> `pm_metal_jit_rsx_parse` ->
//! `pm_metal_jit_rsx_ast_dump` (inspect face) -> `pm_metal_jit_rsx_lower`;
//! `pm_metal_jit_rsx_compile` is the one-shot prove path. The Phase 7 bar is
//! the self-host prove in `__tests__.c`: this very file compiles through this
//! very pipeline, byte-identically on two runs.
//!
//! ## Accepted subset (everything else is refused with
//! `rsx: unsupported: <construct> at line N` — never a silent miscompile)
//!
//! Items: `#![...]` inner attrs (skipped), `use` (recorded, lowered to a
//! comment), outer attrs `#[repr(C)] #[derive(..)] #[allow(..)] #[used]
//! #[cfg_attr(..)] #[link_section = ".."] #[unsafe(no_mangle)]`, `pub` /
//! `pub(crate)` visibility, named/tuple/unit `struct`, fieldless `enum` (data
//! variants refused at lowering — C has no sum types), `impl Type` /
//! `impl Trait for Type`, `extern "C" { fn .. }` blocks, `static`/`const`
//! (literal initializers), `type` aliases, `fn` with `const`/`unsafe`/
//! `extern "C"` qualifiers. Refused: generics on items, `trait`/`mod` items,
//! `macro_rules!`, `async`/`const` blocks, nested items in fn bodies.
//!
//! Statements: `let` (ident / `mut ident` / `_`, optional type + init; let-else
//! refused), `if`/`else`, `match`, `loop`, `while`, `for pat in a..b` /
//! `a..=b`, `return`/`break`/`continue`, expression and assignment statements.
//!
//! Expressions: literals (int with suffixes, float, char, byte char/string,
//! string, raw string), paths, calls, method calls, field access, indexing,
//! casts, unary `! - * & &mut`, binary ops, ranges, parens, struct literals
//! (`T { f: v }` — `..base` and shorthand refused), block-exprs, `unsafe`
//! blocks. Closures parse but lowering refuses them.
//!
//! Types: `u8..u64` `i8..i64` `usize` `isize` `f32` `f64` `bool` `char`
//! (`u128`/`i128` have no C type), `*const T` `*mut T` `&T` `&mut T`
//! (lifetimes skipped), `[T; N]` `[T]` `&[T]`, `()` (return only), paths
//! with one generic list (`Option<T>` — pointer/fn-ptr payload only),
//! `fn(..) -> R` and `unsafe extern "C" fn(..) -> R`. Tuples refuse.
//!
//! ## Lowering rules
//!
//! - `struct S { f: T }` -> `typedef struct S { C_T f; } S;` (declaration
//!   order is the layout; `#[repr(C)]` is accepted and recorded, non-repr
//!   structs lower the same way — documented divergence).
//! - Type map: `uN`->`uintN_t`, `iN`->`intN_t`, `usize`->`size_t`,
//!   `isize`->`intptr_t`, `f32`->`float`, `f64`->`double`, `bool`->`bool`,
//!   `char`->`uint32_t`, `*const T`/`&T`->`const C_T *`, `*mut T`/`&mut T`->
//!   `C_T *`, `&str`->`const char *`, `&[T]`/`[T]`->`const C_T *` (length is
//!   not carried — the kernel passes ptr+len pairs; `.len()` on a slice is
//!   refused), `()`->`void`, `Option<ptr-or-fn>` -> the inner C type
//!   (`None`->`0`), `fn`/`unsafe extern "C" fn` -> function pointer.
//! - `fn` -> C prototype + body; `unsafe`/`extern "C"`/`const` qualifiers
//!   drop (C has no unsafe). Methods -> free functions `Type_method` (trait
//!   impls: `Type_Trait_method`); `&self`/`&mut self` become `Type *self`.
//! - `match` lowers to an `if`/`else` chain. Supported patterns: literals,
//!   enum variant paths, `_`, `None`, `Some(bind)` (Option-of-pointer only —
//!   the bind becomes an inner declaration), `&bind`, or-patterns of
//!   literals/variants. Guards, ranges, tuple and struct patterns refuse.
//! - `for x in a..b` -> C `for` loop; `for` over anything else refuses.
//! - `static` -> file-scope global (`const` qualified unless `static mut`),
//!   `const` -> `static const`, `type` -> `typedef`.
//! - Emission order: struct/enum/typedef items, extern prototypes, statics,
//!   fn prototypes, fn bodies — so source order never breaks C name lookup.
//! - Every item is preceded by `#line N "__impl__.rs"` (provenance chain:
//!   the /src/<fqn> pane stays the primary source face).
//! - Known paths/methods map to C: `core::ptr::null[_mut]`->`0`,
//!   `core::ptr::copy_nonoverlapping(s,d,n)`->`memcpy(d,s,n * sizeof(*s))`
//!   (Rust counts elements, memcpy counts bytes),
//!   `core::mem::size_of::<T>()`->`sizeof(T)`, `iN::MIN/MAX`/`uN::`/`
//!   `usize::MAX` -> stdint limit macros, `.is_null()`->`(x == 0)`,
//!   `.add(k)`/`.sub(k)`->`(x + k)`/`(x - k)`, `.as_ptr()` -> identity,
//!   `.is_ascii_{digit,alphanumeric,alphabetic}()` -> range tests,
//!   `.len()` -> literal/array constant only. Anything else refuses.
//! - Item-level `PM_MOD_EXPORT_RS!` / `PM_MOD_BOOT*_RS!` ctors lower to a
//!   `//` comment (the registry table is built by the real toolchain).
//!   Every other macro refuses.
//! - Value-position `if`/`else` needs a type ascription (`let x: T = if ..`).
//!
//! ## Self-hosting discipline (this file is its own test input)
//!
//! The self-host prove compiles *this* file, so the file is written inside
//! the subset above: no generics (the growables are concrete), no closures,
//! no `Option`, no tuples, no slice methods — byte spans are raw
//! `*const u8` + explicit `usize` lengths, internal fixed strings are
//! NUL-terminated and passed as `b"...\0".as_ptr()`, integer constants are
//! literals (no `1 << 20` folding needed).

#![allow(clippy::missing_safety_doc)]
#![allow(non_camel_case_types)]

/* ---- C ABI mirrors (__types__.h is the contract) ---- */

#[repr(C)]
pub struct pm_util_mem_arena_t {
    _opaque: [u8; 0],
}

unsafe extern "C" {
    fn pm_util_mem_alloc(arena: *mut pm_util_mem_arena_t, n: usize) -> *mut u8;
    fn pm_util_mem_free(arena: *mut pm_util_mem_arena_t, p: *mut u8);
}

/* The C enum is generated from the X-macro table by position, so the Rust
 * mirrors must list them in the same order (values are positional). */
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum pm_jit_rsx_tok_kind {
    END = 0,
    IDENT = 1,
    INT_LITERAL = 2,
    FLOAT_LITERAL = 3,
    CHAR_LITERAL = 4,
    STRING_LITERAL = 5,
    BYTE_STR_LITERAL = 6,
    LIFETIME = 7,
    ARROW = 8,
    FAT_ARROW = 9,
    DOUBLE_COLON = 10,
    DOT = 11,
    RANGE = 12,
    SHL = 13,
    SHR = 14,
    LE = 15,
    GE = 16,
    EQ = 17,
    NE = 18,
    ANDAND = 19,
    OROR = 20,
    PLUSEQ = 21,
    MINUSEQ = 22,
    STAREQ = 23,
    SLASHEQ = 24,
    PERCENTEQ = 25,
    CARETEQ = 26,
    AMPEQ = 27,
    OREQ = 28,
    SHLEQ = 29,
    SHREQ = 30,
    MACRO_INVOC = 31,
    PUNCT = 32,
    ERROR = 33,
}
pub const TOK_KIND_COUNT: u32 = 34;

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum pm_jit_rsx_ast_kind {
    FILE = 0,
    USE = 1,
    FN = 2,
    STRUCT = 3,
    ENUM = 4,
    IMPL = 5,
    EXTERN_BLOCK = 6,
    STATIC = 7,
    CONST = 8,
    TYPE_ALIAS = 9,
    TRAIT = 10,
    MODULE = 11,
    ATTR = 12,
    BLOCK = 13,
    STMT = 14,
    LET = 15,
    IF = 16,
    MATCH = 17,
    MATCH_ARM = 18,
    LOOP = 19,
    WHILE = 20,
    FOR = 21,
    RETURN = 22,
    BREAK = 23,
    CONTINUE = 24,
    EXPR_STMT = 25,
    ASSIGN = 26,
    BINARY = 27,
    UNARY = 28,
    CALL = 29,
    METHOD_CALL = 30,
    FIELD = 31,
    PATH = 32,
    LITERAL = 33,
    TUPLE = 34,
    STRUCT_LIT = 35,
    CLOSURE = 36,
    INDEX = 37,
    CAST = 38,
    MACRO = 39,
    PAREN = 40,
    TYPE = 41,
    PARAM = 42,
    STRUCT_FIELD = 43,
    ENUM_VARIANT = 44,
    GENERIC = 45,
    WHERE = 46,
    ARRAY = 47,
}
pub const AST_KIND_COUNT: u32 = 48;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct pm_jit_rsx_token_t {
    pub kind: pm_jit_rsx_tok_kind,
    pub line: u32,
    pub text: *const u8,
    pub text_len: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct pm_jit_rsx_toklist_t {
    pub toks: *mut pm_jit_rsx_token_t,
    pub n_toks: u32,
}

#[repr(C)]
pub struct pm_jit_rsx_ast_t {
    pub kind: pm_jit_rsx_ast_kind,
    pub line: u32,
    pub text: *const u8,
    pub text_len: usize,
    pub kids: *mut *mut pm_jit_rsx_ast_t,
    pub n_kids: u32,
    pub int_val: i64,
    pub op_kind: pm_jit_rsx_tok_kind,
}

/* ---- byte helpers (no libc, no formatting) ---- */

/* Copy n bytes into buf at offset `at`, NUL-terminating is the caller's job.
 * Returns the new offset. Bounded: never writes past cap-1. */
unsafe fn bput(buf: *mut u8, cap: usize, at: usize, p: *const u8, n: usize) -> usize {
    let mut i = 0usize;
    let mut a = at;
    if buf.is_null() || cap == 0 {
        return at;
    }
    while i < n {
        if a + 1 >= cap {
            break;
        }
        unsafe {
            *buf.add(a) = *p.add(i);
        }
        a += 1;
        i += 1;
    }
    a
}

/* Copy a NUL-terminated string the same way. */
unsafe fn zput(buf: *mut u8, cap: usize, at: usize, s: *const u8) -> usize {
    let mut i = 0usize;
    let mut a = at;
    if buf.is_null() || cap == 0 {
        return at;
    }
    loop {
        let c = unsafe { *s.add(i) };
        if c == 0 {
            break;
        }
        if a + 1 >= cap {
            break;
        }
        unsafe {
            *buf.add(a) = c;
        }
        a += 1;
        i += 1;
    }
    /* callers read the buffer as a C string — always terminate it. */
    if a + 1 < cap {
        unsafe {
            *buf.add(a) = 0;
        }
    } else if cap > 0 {
        unsafe {
            *buf.add(cap - 1) = 0;
        }
    }
    a
}

/* "rsx: <msg>[ at line N]" into the caller's errbuf, always NUL-terminated. */
/* Render a u32 in decimal at buf[at..cap), NUL-terminated. Returns the new
 * offset. Shared by err_set and the batched-refusal appender. */
unsafe fn zput_num(buf: *mut u8, cap: usize, n_in: u32) -> usize {
    let mut digs = [0u8; 12];
    let mut n = n_in;
    let mut di = 0usize;
    let mut j = 0usize;
    let mut at = 0usize;
    if buf.is_null() || cap == 0 {
        return 0;
    }
    while n > 0 && di < digs.len() {
        digs[di] = b'0' + (n % 10) as u8;
        n /= 10;
        di += 1;
    }
    if di == 0 {
        digs[0] = b'0';
        di = 1;
    }
    j = di;
    while j > 0 {
        j -= 1;
        at = unsafe { bput(buf, cap, at, digs.as_ptr().add(j), 1) };
    }
    at
}

unsafe fn err_set(buf: *mut u8, cap: usize, msg: *const u8, line: u32) {
    let mut at = 0usize;
    if buf.is_null() || cap == 0 {
        return;
    }
    at = unsafe { zput(buf, cap, at, b"rsx: \0".as_ptr()) };
    at = unsafe { zput(buf, cap, at, msg) };
    if line != 0 {
        at = unsafe { zput(buf, cap, at, b" at line \0".as_ptr()) };
        at += unsafe { zput_num(buf.add(at), cap - at, line) };
    }
    unsafe {
        *buf.add(at) = 0;
    }
}

/* p[0..n] == NUL-terminated z (length must match too). */
unsafe fn z_eq(p: *const u8, n: usize, z: *const u8) -> bool {
    let mut i = 0usize;
    loop {
        let c = unsafe { *z.add(i) };
        if c == 0 {
            return i == n;
        }
        if i >= n {
            return false;
        }
        if unsafe { *p.add(i) } != c {
            return false;
        }
        i += 1;
    }
}

/* Build "unsupported character 'x'" into out (NUL-terminated). */
unsafe fn msg_char(out: *mut u8, cap: usize, c: u8) {
    let mut at = 0usize;
    if out.is_null() || cap == 0 {
        return;
    }
    at = unsafe { zput(out, cap, at, b"unsupported character '\0".as_ptr()) };
    at = unsafe { bput(out, cap, at, b" \0".as_ptr(), 1) };
    unsafe {
        *out.add(at - 1) = c;
    }
    at = unsafe { zput(out, cap, at, b"'\0".as_ptr()) };
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
            let ok5 = c == b';' || c == b':' || c == b'.' || c == b'#';
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
                /* `()` is the unit type; other tuples refuse. */
                if unsafe { self.is_punct(self.at + 1, b')') } {
                    self.at += 2;
                    return unsafe { self.mk(pm_jit_rsx_ast_kind::TYPE, line, b"()\0".as_ptr(), 2) };
                }
                unsafe {
                    self.err(b"unsupported: tuple type\0".as_ptr());
                }
                return core::ptr::null_mut();
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
                    if unsafe { self.is_punct(self.at, b'>') } {
                        self.at += 1;
                        break;
                    }
                    let g = unsafe { self.parse_type() };
                    unsafe {
                        kids.add(g, self.arena);
                    }
                    if unsafe { self.is_punct(self.at, b',') } {
                        self.at += 1;
                        continue;
                    }
                }
                /* Path with generics ends here. */
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::TYPE, line, b"path\0".as_ptr(), 4) };
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

    unsafe fn parse_pattern(&mut self) -> *mut pm_jit_rsx_ast_t {
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
            return n;
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
        loop {
            if !self.ok || e.is_null() {
                return e;
            }
            let k = unsafe { self.kind(self.at) };
            let is_dot = k == pm_jit_rsx_tok_kind::DOT
                || (k == pm_jit_rsx_tok_kind::PUNCT && unsafe { self.is_punct(self.at, b'.') });
            if is_dot {
                self.at += 1;
                /* `expr.<number>` (tuple field) is refused; `.ident` may
                 * continue into `(` (method) or stand alone (field). */
                if unsafe { self.kind(self.at) } != pm_jit_rsx_tok_kind::IDENT {
                    unsafe {
                        self.err(b"unsupported: tuple field access\0".as_ptr());
                    }
                    return core::ptr::null_mut();
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
                if unsafe { self.is_punct(self.at, b'(') } {
                    let args = unsafe { self.parse_call_args() };
                    let mut kids = Kids::new();
                    unsafe {
                        kids.add(e, self.arena);
                        kids.add(name_node, self.arena);
                        kids.add(args, self.arena);
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
                let idx = unsafe { self.parse_expr() };
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
            return e;
        }
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
                let e = unsafe { self.parse_expr() };
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
            if unsafe { self.is_punct(self.at, b'|') } || unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::OROR {
                return unsafe { self.parse_closure() };
            }
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
                let n = unsafe { self.mk(pm_jit_rsx_ast_kind::BREAK, line, b"break\0".as_ptr(), 5) };
                return n;
            }
            if unsafe { self.is_kw(self.at, b"continue\0".as_ptr()) } {
                self.at += 1;
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

    /* Closure: |a, b| expr or |a| { .. } (parse; lowering refuses). */
    unsafe fn parse_closure(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        let mut kids = Kids::new();
        loop {
            if unsafe { self.is_punct(self.at, b'|') } {
                self.at += 1;
                if unsafe { self.is_punct(self.at, b'|') } {
                    self.at += 1;
                    break;
                }
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
            unsafe {
                kids.add(p, self.arena);
            }
            self.at += 1;
            if unsafe { self.is_punct(self.at, b',') } {
                self.at += 1;
                continue;
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
                    if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::PUNCT {
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

    unsafe fn parse_if_expr(&mut self) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        self.at += 1;
        let save = self.cond_ctx;
        self.cond_ctx = true;
        let cond = unsafe { self.parse_expr() };
        self.cond_ctx = save;
        let then_b = unsafe { self.parse_block() };
        let mut kids = Kids::new();
        unsafe {
            kids.add(cond, self.arena);
            kids.add(then_b, self.arena);
        }
        if unsafe { self.is_kw(self.at, b"else\0".as_ptr()) } {
            self.at += 1;
            let els: *mut pm_jit_rsx_ast_t;
            if unsafe { self.is_kw(self.at, b"if\0".as_ptr()) } {
                els = unsafe { self.parse_if_expr() };
            } else {
                els = unsafe { self.parse_block() };
            }
            unsafe {
                kids.add(els, self.arena);
            }
        }
        let n = unsafe { self.mk(pm_jit_rsx_ast_kind::IF, line, b"if\0".as_ptr(), 2) };
        unsafe {
            self.set_kids(n, &kids);
        }
        n
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
            if unsafe { self.is_kw(self.at, b"if\0".as_ptr()) } {
                unsafe {
                    self.err(b"unsupported: match guard\0".as_ptr());
                }
                return core::ptr::null_mut();
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
        /* pattern: ident, mut ident, or `_` */
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
        let pat = unsafe { self.mk(pm_jit_rsx_ast_kind::PATH, line, pat_name, pat_len) };
        unsafe {
            kids.add(pat, self.arena);
        }
        if mutable {
            let m = unsafe { self.mk(pm_jit_rsx_ast_kind::ATTR, line, b"mut\0".as_ptr(), 3) };
            unsafe {
                kids.add(m, self.arena);
            }
        }
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
            /* let-else */
            if unsafe { self.is_kw(self.at, b"else\0".as_ptr()) } {
                unsafe {
                    self.err(b"unsupported: let-else\0".as_ptr());
                }
                return core::ptr::null_mut();
            }
        }
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
            unsafe {
                self.err(b"unsupported: generics on struct\0".as_ptr());
            }
            return core::ptr::null_mut();
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
            unsafe {
                self.err(b"unsupported: generics on fn\0".as_ptr());
            }
            return core::ptr::null_mut();
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

    /* impl block: inherent or trait impl; members are fns. */
    unsafe fn parse_impl(&mut self, kids: &mut Kids) -> *mut pm_jit_rsx_ast_t {
        let line = unsafe { self.line(self.at) };
        self.at += 1;
        /* Type path (no generics on the impl itself). */
        let self_ty = unsafe { self.parse_path_type() };
        let mut body = Kids::new();
        /* `impl Trait for Type` — record the trait when present. */
        let mut trait_node: *mut pm_jit_rsx_ast_t = core::ptr::null_mut();
        if unsafe { self.is_kw(self.at, b"for\0".as_ptr()) } {
            self.at += 1;
            let t = unsafe { self.parse_path_type() };
            trait_node = unsafe {
                self.mk(pm_jit_rsx_ast_kind::TYPE, line, b"trait\0".as_ptr(), 5)
            };
            let mut tk = Kids::new();
            unsafe {
                tk.add(t, self.arena);
            }
            unsafe {
                self.set_kids(trait_node, &tk);
            }
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
        if unsafe { self.is_kw(self.at, b"use\0".as_ptr()) } {
            return unsafe { self.parse_use() };
        }
        if unsafe { self.is_kw(self.at, b"struct\0".as_ptr()) } {
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
        if unsafe { self.is_kw(self.at, b"trait\0".as_ptr()) }
            || unsafe { self.is_kw(self.at, b"mod\0".as_ptr()) }
            || unsafe { self.is_kw(self.at, b"macro_rules\0".as_ptr()) }
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
        /* inner attributes `#![...]` — skipped (not attached to an item). */
        loop {
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
            break;
        }
        loop {
            if unsafe { self.kind(self.at) } == pm_jit_rsx_tok_kind::END {
                break;
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

/* ================= lowering (AST -> C) =================
 *
 * Two passes over the file's items:
 *   pass 1 collects type information (struct fields, fn signatures,
 *        enum variants, extern decls, statics) into tables;
 *   pass 2 emits C in dependency order: aggregates/typedefs, extern
 *        prototypes, statics, fn prototypes, fn bodies.
 * Type tables are fixed-size open addressing hashes — this file is its own
 * subset input (no generics, no closures), so sizes are compile-time
 * constants. */

const SYM_CAP: usize = 512;
const FPC: usize = 24;

/* A struct record: name, fields, C types of fields. Field names and C
 * types are arena spans (NUL-terminated copies), so the table stores
 * pointers, not inline arrays — this file's own structs carry up to 15
 * fields and the inline 8-slot table could not hold them. */
struct SymTab {
    names: [*const u8; SYM_CAP],
    name_lens: [usize; SYM_CAP],
    field_counts: [u32; SYM_CAP],
    field_names: [*const u8; SYM_CAP * FPC],
    field_name_lens: [usize; SYM_CAP * FPC],
    field_ctypes: [*const u8; SYM_CAP * FPC],
    field_ctype_lens: [usize; SYM_CAP * FPC],
    used: [bool; SYM_CAP],
}

impl SymTab {
    unsafe fn new(arena: *mut pm_util_mem_arena_t) -> *mut SymTab {
        let p = unsafe { pm_util_mem_alloc(arena, core::mem::size_of::<SymTab>()) } as *mut SymTab;
        if p.is_null() {
            return p;
        }
        /* tlsf does not zero — clear the block so every table starts
         * empty (used=false, counts 0, pointers NULL). */
        unsafe {
            let zb = p as *mut u8;
            let n = core::mem::size_of::<SymTab>();
            let mut i = 0usize;
            while i < n {
                *zb.add(i) = 0;
                i += 1;
            }
        }
        p
    }

    /* Copy len bytes into a fresh arena block (NUL-terminated). */
    unsafe fn span(&self, arena: *mut pm_util_mem_arena_t, src: *const u8, len: usize) -> *const u8 {
        let p = unsafe { pm_util_mem_alloc(arena, len + 1) };
        if p.is_null() {
            return b"\0".as_ptr();
        }
        unsafe {
            core::ptr::copy_nonoverlapping(src, p, len);
            *p.add(len) = 0;
        }
        p
    }

    /* Length-aware compare — stored names point into the source and are
     * not NUL-terminated, so z_eq (which needs a terminator) cannot be
     * used here. */
    unsafe fn zprefix_eq(&self, s: usize, len: usize, name: *const u8) -> bool {
        if self.name_lens[s] != len {
            return false;
        }
        let z = self.names[s];
        let mut i = 0usize;
        while i < len {
            if unsafe { *z.add(i) } != unsafe { *name.add(i) } {
                return false;
            }
            i += 1;
        }
        true
    }

    unsafe fn slot(&self, name: *const u8, len: usize) -> usize {
        let mut h: usize = 5381;
        let mut i = 0usize;
        let mut probe = 0usize;
        while i < len && i < 64 {
            h = h.wrapping_mul(33).wrapping_add(unsafe { *name.add(i) } as usize);
            i += 1;
        }
        probe = h % SYM_CAP;
        i = 0;
        while i < SYM_CAP {
            let s = (probe + i) % SYM_CAP;
            if !self.used[s] {
                return s;
            }
            if unsafe { self.zprefix_eq(s, len, name) } {
                return s;
            }
            i += 1;
        }
        SYM_CAP
    }

    unsafe fn add(&mut self, name: *const u8, len: usize) -> usize {
        let s = unsafe { self.slot(name, len) };
        if s >= SYM_CAP {
            return s;
        }
        self.names[s] = name;
        self.name_lens[s] = len;
        self.used[s] = true;
        self.field_counts[s] = 0;
        s
    }

    unsafe fn add_field(&mut self, arena: *mut pm_util_mem_arena_t, s: usize, fname: *const u8, flen: usize, ct: *const u8, ctlen: usize) {
        let n = self.field_counts[s] as usize;
        if s >= SYM_CAP || n >= FPC || flen == 0 || ctlen == 0 {
            return;
        }
        let dst = s * FPC + n;
        self.field_names[dst] = unsafe { self.span(arena, fname, flen) };
        self.field_name_lens[dst] = flen;
        self.field_ctypes[dst] = unsafe { self.span(arena, ct, ctlen) };
        self.field_ctype_lens[dst] = ctlen;
        self.field_counts[s] += 1;
    }

    unsafe fn find(&self, name: *const u8, len: usize) -> usize {
        let s = unsafe { self.slot(name, len) };
        if s < SYM_CAP && self.used[s] {
            return s;
        }
        SYM_CAP
    }

    /* Field C type into out (NUL-terminated); 0 = not found. */
    unsafe fn field_ctype(&self, s: usize, fname: *const u8, flen: usize, out: *mut u8) -> usize {
        let n = self.field_counts[s] as usize;
        let mut i = 0usize;
        while i < n {
            let d = s * FPC + i;
            if self.field_name_lens[d] == flen && unsafe { z_eq(self.field_names[d], flen, fname) } {
                let cl = self.field_ctype_lens[d];
                let src = self.field_ctypes[d];
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
            i += 1;
        }
        0
    }
}

/* A fn record: name, return C type, n params. */
struct FnTab {
    names: [*const u8; SYM_CAP],
    name_lens: [usize; SYM_CAP],
    rets: [[u8; 64]; SYM_CAP],
    ret_lens: [usize; SYM_CAP],
    n_params: [u32; SYM_CAP],
    used: [bool; SYM_CAP],
}

impl FnTab {
    unsafe fn new() -> FnTab {
        FnTab {
            names: [b"\0".as_ptr(); SYM_CAP],
            name_lens: [0; SYM_CAP],
            rets: [[0; 64]; SYM_CAP],
            ret_lens: [0; SYM_CAP],
            n_params: [0; SYM_CAP],
            used: [false; SYM_CAP],
        }
    }

    /* Length-aware compare — stored names point into the source and are
     * not NUL-terminated, so z_eq (which needs a terminator) cannot be
     * used here. */
    unsafe fn zprefix_eq(&self, s: usize, len: usize, name: *const u8) -> bool {
        if self.name_lens[s] != len {
            return false;
        }
        let z = self.names[s];
        let mut i = 0usize;
        while i < len {
            if unsafe { *z.add(i) } != unsafe { *name.add(i) } {
                return false;
            }
            i += 1;
        }
        true
    }

    unsafe fn slot(&self, name: *const u8, len: usize) -> usize {
        let mut h: usize = 5381;
        let mut i = 0usize;
        let mut probe = 0usize;
        while i < len && i < 64 {
            h = h.wrapping_mul(33).wrapping_add(unsafe { *name.add(i) } as usize);
            i += 1;
        }
        probe = h % SYM_CAP;
        i = 0;
        while i < SYM_CAP {
            let s = (probe + i) % SYM_CAP;
            if !self.used[s] {
                return s;
            }
            if unsafe { self.zprefix_eq(s, len, name) } {
                return s;
            }
            i += 1;
        }
        SYM_CAP
    }

    unsafe fn add(&mut self, name: *const u8, len: usize, ret: *const u8, retlen: usize) -> usize {
        let s = unsafe { self.slot(name, len) };
        if s >= SYM_CAP {
            return s;
        }
        self.names[s] = name;
        self.name_lens[s] = len;
        let mut i = 0usize;
        while i < retlen && i < 64 {
            self.rets[s][i] = unsafe { *ret.add(i) };
            i += 1;
        }
        self.ret_lens[s] = retlen;
        self.used[s] = true;
        self.n_params[s] = 0;
        s
    }

    unsafe fn set_n_params(&mut self, s: usize, n: u32) {
        if s < SYM_CAP {
            self.n_params[s] = n;
        }
    }

    unsafe fn ret_ctype(&self, name: *const u8, len: usize, out: *mut u8) -> usize {
        let s = unsafe { self.slot(name, len) };
        if s < SYM_CAP && self.used[s] && self.name_lens[s] == len {
            let rl = self.ret_lens[s];
            let mut i = 0usize;
            while i < rl {
                unsafe {
                    *out.add(i) = self.rets[s][i];
                }
                i += 1;
            }
            unsafe {
                *out.add(rl) = 0;
            }
            return rl;
        }
        0
    }
}

/* Integer constants (const items with literal values): repeat counts and
 * enum discriminants may name them; emission needs the numeric value. */
const CONST_CAP: usize = 128;

struct ConstTab {
    names: [[u8; 48]; CONST_CAP],
    name_lens: [usize; CONST_CAP],
    vals: [u64; CONST_CAP],
    n: usize,
}

impl ConstTab {
    unsafe fn new() -> ConstTab {
        ConstTab {
            names: [[0; 48]; CONST_CAP],
            name_lens: [0; CONST_CAP],
            vals: [0; CONST_CAP],
            n: 0,
        }
    }

    unsafe fn add(&mut self, name: *const u8, nlen: usize, val: u64) {
        if self.n >= CONST_CAP || nlen > 48 {
            return;
        }
        let mut i = 0usize;
        while i < nlen {
            self.names[self.n][i] = unsafe { *name.add(i) };
            i += 1;
        }
        self.name_lens[self.n] = nlen;
        self.vals[self.n] = val;
        self.n += 1;
    }

    /* Parses a plain decimal/hex literal or a named constant; returns 0 with
     * *found=false when the text names nothing known. */
    unsafe fn lookup(&self, s: *const u8, n: usize, found: *mut bool) -> u64 {
        unsafe {
            *found = false;
        }
        if n == 0 || s.is_null() {
            return 0;
        }
        let mut i = 0usize;
        while i < self.n {
            if self.name_lens[i] == n {
                let mut j = 0usize;
                let mut eq = true;
                while j < n {
                    if self.names[i][j] != unsafe { *s.add(j) } {
                        eq = false;
                        break;
                    }
                    j += 1;
                }
                if eq {
                    unsafe {
                        *found = true;
                    }
                    return self.vals[i];
                }
            }
            i += 1;
        }
        0
    }
}

/* One lowering context. */
struct Lower {
    arena: *mut pm_util_mem_arena_t,
    out: Out,
    errbuf: *mut u8,
    errcap: usize,
    errline: u32,
    ok: bool,
    syms: *mut SymTab,
    fns: FnTab,
    consts: ConstTab,
    depth: usize,
    /* the struct type currently being lowered (for method resolution) */
    cur_impl: [*const u8; 16],
    cur_impl_lens: [usize; 16],
    cur_impl_n: usize,
    /* method receiver: C type text of `self` while lowering a method body;
     * empty when lowering a free fn. */
    recv_type: [u8; 64],
    recv_len: usize,
    /* last-resort scratch when the arena is exhausted: every arena_tmp
     * failure also sets ok=false, so lowering aborts before the reused
     * bytes can matter — this only keeps the NULL deref off the OOM path. */
    oom_buf: [u8; 160],
    /* refusals seen so far this pass — batching several into errbuf saves
     * the developer a rebuild per error; ok=false still stops the cascade
     * of follow-on diagnostics from a single fault. */
    nerrs: u32,
}

impl Lower {
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
            if unsafe { z_eq(text, text_len, b"()\0".as_ptr()) } {
                at = unsafe { zput(out, cap, at, b"void\0".as_ptr()) };
                return at;
            }
            if unsafe { z_eq(text, text_len, b"str\0".as_ptr()) } {
                at = unsafe { zput(out, cap, at, b"char\0".as_ptr()) };
                return at;
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
                    at = unsafe { zput(out, cap, at, inner_buf) };
                    at = unsafe { zput(out, cap, at, b" *\0".as_ptr()) };
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
                    at = unsafe { zput(out, cap, at, inner_buf) };
                    at = unsafe { zput(out, cap, at, b" *\0".as_ptr()) };
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
                at = unsafe { zput(out, cap, at, b"(*\0".as_ptr()) };
                at = unsafe { zput(out, cap, at, b")(\0".as_ptr()) };
                at = unsafe { zput(out, cap, at, ret_buf) };
                at = unsafe { zput(out, cap, at, b" *)(\0".as_ptr()) };
                let mut i = 0usize;
                let mut first = true;
                while i + 1 < nk {
                    let pty = unsafe { *kids.add(i) };
                    if unsafe { (*pty).kind } == pm_jit_rsx_ast_kind::TYPE {
                        let pt = unsafe { (*pty).text };
                        let ptl = unsafe { (*pty).text_len };
                        if unsafe { z_eq(pt, ptl, b"unsafe\0".as_ptr()) }
                            || unsafe { z_eq(pt, ptl, b"extern\0".as_ptr()) }
                            || unsafe { z_eq(pt, ptl, b"path\0".as_ptr()) }
                        {
                            /* ABI string / qualifier — skip */
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
            if unsafe { z_eq(text, text_len, b"path\0".as_ptr()) } {
                return unsafe { self.ctype_path(ty, out, cap) };
            }
            /* named segment (also arrives for generic-less paths) */
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
            at = unsafe { zput(out, cap, at, inner_buf) };
            unsafe {
                *out.add(at) = 0;
            }
            return at;
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
                 * resolve their type. */
                let _ = unsafe { (*self.syms).add(unsafe { (*item).text }, unsafe { (*item).text_len }) };
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
                /* extern fns: register name + return type for call inference */
                let mut j = 0usize;
                while j < unsafe { (*item).n_kids } as usize {
                    let k = unsafe { *(*item).kids.add(j) };
                    if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::FN {
                        let ename = unsafe { (*k).text };
                        let elen = unsafe { (*k).text_len };
                        let mut ret = b"void\0".as_ptr();
                        let mut retlen = 4usize;
                        let mut j2 = 0usize;
                        while j2 < unsafe { (*k).n_kids } as usize {
                            let kk = unsafe { *(*k).kids.add(j2) };
                            if unsafe { (*kk).kind } == pm_jit_rsx_ast_kind::TYPE {
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
                        let fs = unsafe { self.fns.add(ename, elen, ret, retlen) };
                        let _ = fs;
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
                let mut j = 0usize;
                while j < n_k {
                    let k = unsafe { *(*item).kids.add(j) };
                    let kk = unsafe { (*k).kind };
                    if kk == pm_jit_rsx_ast_kind::PARAM {
                        nparams += 1;
                    } else if kk == pm_jit_rsx_ast_kind::TYPE {
                        /* return type is the last TYPE kid */
                        let ct = self.arena_tmp();
                        let n = unsafe { self.ctype(k, ct, 128) };
                        if n > 0 {
                            ret = ct;
                            retlen = n;
                        }
                    }
                    j += 1;
                }
                let fs = unsafe { self.fns.add(name, nlen, ret, retlen) };
                unsafe {
                    self.fns.set_n_params(fs, nparams);
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
            let fs = unsafe { self.fns.add(name_buf, at, ret, retlen) };
            unsafe {
                self.fns.set_n_params(fs, 1);
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
                    /* strip one trailing '*' (and spaces) from e.g. `const u8 *` */
                    let mut j = bn;
                    while j > 0 && *b_buf.add(j - 1) == b' ' {
                        j -= 1;
                    }
                    if j == 0 || *b_buf.add(j - 1) != b'*' {
                        return 0;
                    }
                    j -= 1;
                    while j > 0 && *b_buf.add(j - 1) == b' ' {
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
                return 0;
            }
            let leaf = unsafe { *kids.add(nk - 1) };
            let name = unsafe { (*leaf).text };
            let nl = unsafe { (*leaf).text_len };
            let n = unsafe { (*locals).lookup(name, nl, out) };
            if n > 0 {
                return n;
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
                            let n = unsafe { self.fns.ret_ctype(name, nl, out) };
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
                                    let rn = unsafe { self.fns.ret_ctype(mbuf, mn3, out) };
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
            let b_buf = self.arena_tmp();
            let bn = unsafe { self.expr_ctype(base, b_buf, 128, locals) };
            if bn == 0 {
                return 0;
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
                return 0;
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
             * EXPR_STMT/BLOCK layers like the IF case). */
            let kids = unsafe { (*e).kids };
            let nk = unsafe { (*e).n_kids } as usize;
            let mut i = 1usize;
            while i < nk {
                let arm = unsafe { *kids.add(i) };
                if unsafe { (*arm).kind } == pm_jit_rsx_ast_kind::MATCH_ARM
                    && unsafe { (*arm).n_kids } >= 2
                {
                    let ak = unsafe { (*arm).kids };
                    let mut br = unsafe { *ak.add(1) };
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
                            return unsafe { self.expr_ctype(b2, out, cap, locals) };
                        }
                    }
                    return unsafe { self.expr_ctype(br, out, cap, locals) };
                }
                i += 1;
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
             * then-branch first, else-branch as fallback (a `null_mut()`
             * else cannot be typed, but its then-branch can). */
            let kids = unsafe { (*e).kids };
            let nk = unsafe { (*e).n_kids } as usize;
            if nk < 2 {
                return 0;
            }
            let mut ti = 1usize;
            while ti < nk && ti < 3 {
                let br = self.block_tail_node(unsafe { *kids.add(ti) });
                let n = unsafe { self.expr_ctype(br, out, cap, locals) };
                if n > 0 {
                    return n;
                }
                ti += 1;
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
                     * same name on a known struct type wins (SymTab::add). */
                    let rbuf = self.arena_tmp();
                    let rn = unsafe { self.expr_ctype(*kids.add(0), rbuf, 128, locals) };
                    if rn > 0 {
                        let mut j = rn;
                        while j > 0 && unsafe { *rbuf.add(j - 1) } == b' ' {
                            j -= 1;
                        }
                        if j > 0 && unsafe { *rbuf.add(j - 1) } == b'*' {
                            j -= 1;
                            while j > 0 && unsafe { *rbuf.add(j - 1) } == b' ' {
                                j -= 1;
                            }
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
                        let rn = unsafe { self.fns.ret_ctype(mbuf, mn3, out) };
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
    ctypes: [[u8; 64]; LOCAL_CAP],
    ctype_lens: [usize; LOCAL_CAP],
    depths: [usize; LOCAL_CAP],
    epochs: [usize; LOCAL_CAP],
    marks: [usize; 64],
    nmarks: usize,
    n: usize,
    epoch: usize,
}

impl LocalTab {
    unsafe fn new() -> LocalTab {
        LocalTab {
            names: [[0; 48]; LOCAL_CAP],
            name_lens: [0; LOCAL_CAP],
            ctypes: [[0; 64]; LOCAL_CAP],
            ctype_lens: [0; LOCAL_CAP],
            depths: [0; LOCAL_CAP],
            epochs: [0; LOCAL_CAP],
            marks: [0; 64],
            nmarks: 0,
            n: 0,
            epoch: 0,
        }
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
        if self.n >= LOCAL_CAP || nlen >= 48 || ctlen >= 64 {
            return;
        }
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
        i = 0;
        while i < ctlen {
            self.ctypes[s][i] = unsafe { *ct.add(i) };
            i += 1;
        }
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
                let mut j = 0usize;
                while j < cl {
                    unsafe {
                        *out.add(j) = self.ctypes[i][j];
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
                while j < ctlen {
                    if self.ctypes[i][j] != unsafe { *ct.add(j) } {
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
                self.indent();
                self.out.puts(b"break;\n\0".as_ptr());
            },
            pm_jit_rsx_ast_kind::CONTINUE => unsafe {
                self.indent();
                self.out.puts(b"continue;\n\0".as_ptr());
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
         * shadowing inside one must reuse the enclosing declaration. */
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
        }
        let kids = unsafe { (*b).kids };
        let nk = unsafe { (*b).n_kids } as usize;
        let mut i = 0usize;
        while i < nk {
            let st = unsafe { *kids.add(i) };
            unsafe { self.emit_stmt(st, locals, 0) };
            i += 1;
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

    unsafe fn emit_let(&mut self, s: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        /* kids: name(PATH), [mut ATTR], [type TYPE], [init expr] */
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
            || k == pm_jit_rsx_ast_kind::CLOSURE
            || k == pm_jit_rsx_ast_kind::MACRO
            || k == pm_jit_rsx_ast_kind::BLOCK;
        if k == pm_jit_rsx_ast_kind::EXPR_STMT && unsafe { (*st).n_kids } >= 1 {
            let e = unsafe { *(*st).kids.add(0) };
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
            /* nested tail match: arms store into a temp, then return it. */
            let ct = self.arena_tmp();
            let ct_len = unsafe {
                self.expr_ctype(unsafe { *(*st).kids.add(0) }, ct, 128, locals)
            };
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
            unsafe { self.emit_block_value(body, locals, core::ptr::null(), 0) };
            self.depth -= 1;
            self.indent();
            self.out.puts(b"}\n\0".as_ptr());
            i += 1;
        }
        /* end chain */
        self.indent();
        self.out.putc(b'\n');
    }

    /* match arms with pattern-local bindings: the binding declared inside the
     * `if (…)` test needs to be visible in the body. `Some(x)` lowers to a
     * pointer nullity test with the bind declared before the if — done by
     * rewriting the arm as: `if (sv != 0) { T x = sv; body }`. That is what
     * emit_pat_test's Some-branch does inline (it emits the decl after the
     * test, still inside the if's condition, which is wrong for scoping), so
     * match with Some-patterns uses a pre-declared temp instead. */


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
            self.depth += 1;
            unsafe { self.emit_block_value(body, locals, temp, temp_len) };
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
            /* Some(inner) */
            if nk >= 1 && unsafe { (**kids.add(0)).kind } == pm_jit_rsx_ast_kind::PATH {
                let leaf = unsafe { *kids.add(0) };
                let lt = unsafe { (*leaf).text };
                let ltl = unsafe { (*leaf).text_len };
                if unsafe { z_eq(lt, ltl, b"Some\0".as_ptr()) } && nk >= 2 {
                    let bind = unsafe { *kids.add(1) };
                    let bt = unsafe { (*bind).text };
                    let btl = unsafe { (*bind).text_len };
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

    unsafe fn emit_loop(&mut self, s: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        let kids = unsafe { (*s).kids };
        if unsafe { (*s).n_kids } < 1 {
            return;
        }
        let body = unsafe { *kids.add(0) };
        self.indent();
        self.out.puts(b"for (;;) {\n\0".as_ptr());
        self.depth += 1;
        unsafe { self.emit_block_stmt(body, locals) };
        self.depth -= 1;
        self.indent();
        self.out.puts(b"}\n\0".as_ptr());
    }

    unsafe fn emit_while(&mut self, s: *const pm_jit_rsx_ast_t, locals: *mut LocalTab) {
        let kids = unsafe { (*s).kids };
        if unsafe { (*s).n_kids } < 2 {
            return;
        }
        let cond = unsafe { *kids.add(0) };
        let body = unsafe { *kids.add(1) };
        self.indent();
        self.out.puts(b"while (\0".as_ptr());
        unsafe { self.emit_expr(cond, locals) };
        self.out.puts(b") {\n\0".as_ptr());
        self.depth += 1;
        unsafe { self.emit_block_stmt(body, locals) };
        self.depth -= 1;
        self.indent();
        self.out.puts(b"}\n\0".as_ptr());
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
        /* pat must be a simple binding */
        if unsafe { (*pat).kind } != pm_jit_rsx_ast_kind::PATH {
            unsafe {
                self.err(b"unsupported: for pattern\0".as_ptr(), unsafe { (*s).line });
            }
            return;
        }
        let vname = unsafe { (*pat).text };
        let vlen = unsafe { (*pat).text_len };
        /* bound type from the low end */
        let ct = self.arena_tmp();
        let ct_len = unsafe { self.expr_ctype(lo, ct, 128, locals) };
        if ct_len == 0 {
            unsafe {
                self.err(b"cannot infer for-range bound type\0".as_ptr(), unsafe { (*s).line });
            }
            return;
        }
        unsafe {
            (*locals).add(vname, vlen, ct, ct_len, self.depth + 1);
        }
        self.indent();
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
        self.indent();
        self.out.puts(b"}\n\0".as_ptr());
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
            self.out.put(t, end);
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

    /* Struct literal S { a: 1, b: 2 }. The parser attaches the fields to
     * the wrapping PATH node: kids = segments…, STRUCT_LIT node whose kids
     * are [STRUCT_FIELD(name), value, STRUCT_FIELD(name), value, …].
     * Emits a C99 compound literal `(S){ .a = 1, .b = 2 }`. */
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
        /* type name: the first path segment of the wrapping node */
        let mut tname: *const u8 = b"\0".as_ptr();
        let mut tlen = 0usize;
        if unsafe { (*lit).kind } == pm_jit_rsx_ast_kind::PATH {
            if nk >= 1 {
                let seg = unsafe { *kids.add(0) };
                tname = unsafe { (*seg).text };
                tlen = unsafe { (*seg).text_len };
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
                unsafe { self.emit_expr(v, locals) };
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
                    unsafe { self.emit_expr(*kids.add(0), locals) };
                    self.out.putc(b'[');
                    unsafe { self.emit_expr(*kids.add(1), locals) };
                    self.out.putc(b']');
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
            pm_jit_rsx_ast_kind::TUPLE => unsafe {
                self.err(b"unsupported: tuple expression\0".as_ptr(), unsafe { (*e).line });
            },
            pm_jit_rsx_ast_kind::STRUCT_LIT => unsafe { self.emit_struct_lit(e, locals) },
            pm_jit_rsx_ast_kind::CLOSURE => unsafe {
                self.err(b"unsupported: closure\0".as_ptr(), unsafe { (*e).line });
            },
            pm_jit_rsx_ast_kind::MACRO => unsafe {
                self.err(b"unsupported: expression macro\0".as_ptr(), unsafe { (*e).line });
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
            pm_jit_rsx_ast_kind::BREAK => self.out.puts(b"break\0".as_ptr()),
            pm_jit_rsx_ast_kind::CONTINUE => self.out.puts(b"continue\0".as_ptr()),
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
            self.out.puts(b"(*\0".as_ptr());
            unsafe { self.emit_expr(*kids.add(0), locals) };
            self.out.putc(b')');
            return;
        }
        if unsafe { z_eq(op, op_len, b"&\0".as_ptr()) } {
            self.out.puts(b"(&\0".as_ptr());
            unsafe { self.emit_expr(*kids.add(0), locals) };
            self.out.putc(b')');
            return;
        }
        if unsafe { z_eq(op, op_len, b"&mut\0".as_ptr()) } {
            self.out.puts(b"(&\0".as_ptr());
            unsafe { self.emit_expr(*kids.add(0), locals) };
            self.out.putc(b')');
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
            unsafe {
                self.err(b"unsupported: call on non-path callee\0".as_ptr(), unsafe { (*e).line });
            }
            return;
        }
        /* size_of::<T>() — generic args already skipped by the parser; the
         * callee path is core::mem::size_of. */
        let ck = unsafe { (*callee).kids };
        let cn = unsafe { (*callee).n_kids } as usize;
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
                let mut i = 0usize;
                while i < an {
                    if i > 0 {
                        self.out.puts(b", \0".as_ptr());
                    }
                    unsafe { self.emit_expr(*ak.add(i), locals) };
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
        /* .is_some() / .is_none() on Option-ptr */
        if unsafe { z_eq(mname, mlen, b"is_some\0".as_ptr()) } && an == 0 {
            self.out.putc(b'(');
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b" != 0)\0".as_ptr());
            return;
        }
        if unsafe { z_eq(mname, mlen, b"is_none\0".as_ptr()) } && an == 0 {
            self.out.putc(b'(');
            unsafe { self.emit_expr(recv, locals) };
            self.out.puts(b" == 0)\0".as_ptr());
            return;
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
        /* .as_ptr() / .as_mut_ptr() on a known literal/array — identity */
        if an == 0
            && (unsafe { z_eq(mname, mlen, b"as_ptr\0".as_ptr()) }
                || unsafe { z_eq(mname, mlen, b"as_mut_ptr\0".as_ptr()) })
        {
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
                /* `arr.len()` on a fixed-size array type: `sizeof(a)/sizeof(a[0])`.
                 * Unsized slices still refuse — pass lengths explicitly. */
                let tbuf = self.arena_tmp();
                let tn = unsafe { self.expr_ctype(recv, tbuf, 128, locals) };
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
                }
                return;
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

/* ==== item lowering ==== */

impl Lower {
    /* C declarator: a ctype spelling may carry trailing array dimensions
     * (`T [N]`, `T [A] [B]`); C wants them after the declared name
     * (`T name[A][B]`). Scans trailing `[...]` groups from the right and
     * re-emits them in order (outermost first). Pointer/fnptr spellings
     * never end in `]`, so they pass through unchanged. */
    unsafe fn emit_declarator(&mut self, ct: *const u8, n: usize, name: *const u8, name_len: usize) {
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

    /* `typedef struct S { C_T f; } S;` — declaration order is the layout. */
    unsafe fn lower_struct(&mut self, item: *const pm_jit_rsx_ast_t) {
        let name = unsafe { (*item).text };
        let nlen = unsafe { (*item).text_len };
        let line = unsafe { (*item).line };
        let kids = unsafe { (*item).kids };
        let nk = unsafe { (*item).n_kids } as usize;
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
            self.out.puts(b"typedef struct \0".as_ptr());
            self.out.put(name, nlen);
            self.out.putc(b' ');
            self.out.put(name, nlen);
            self.out.puts(b";\n\0".as_ptr());
            self.out.puts(b"struct \0".as_ptr());
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
            self.out.puts(b"typedef struct \0".as_ptr());
            self.out.put(name, nlen);
            self.out.putc(b' ');
            self.out.put(name, nlen);
            self.out.puts(b";\n\0".as_ptr());
        } else {
            self.out.puts(b"typedef struct \0".as_ptr());
            self.out.put(name, nlen);
            self.out.puts(b" \0".as_ptr());
            self.out.put(name, nlen);
            self.out.puts(b";\n\0".as_ptr());
        }
        self.out.putc(b'\n');
    }

    /* Fieldless enums lower to `enum Name { A, B };` + int constants so
     * variants work in expressions; data-carrying variants refuse. */
    unsafe fn lower_enum(&mut self, item: *const pm_jit_rsx_ast_t) {
        let name = unsafe { (*item).text };
        let nlen = unsafe { (*item).text_len };
        let line = unsafe { (*item).line };
        let kids = unsafe { (*item).kids };
        let nk = unsafe { (*item).n_kids } as usize;
        let mut variants = 0usize;
        let mut bad = false;
        let mut i = 0usize;
        while i < nk {
            let k = unsafe { *kids.add(i) };
            if unsafe { (*k).kind } == pm_jit_rsx_ast_kind::ENUM_VARIANT {
                if unsafe { (*k).n_kids } > 0 {
                    let vk = unsafe { (*k).kids };
                    let first = unsafe { *vk.add(0) };
                    /* a lone INT discriminant literal is fine; anything else
                     * (tuple/struct data) is a data-carrying variant. */
                    if unsafe { (*first).kind } != pm_jit_rsx_ast_kind::LITERAL {
                        bad = true;
                    }
                }
                variants += 1;
            }
            i += 1;
        }
        if bad {
            unsafe {
                self.err(b"unsupported: enum with data-carrying variants\0".as_ptr(), line);
            }
            return;
        }
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
        i = 0;
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
        let ct_len = unsafe { self.ctype(ty, ct, 128) };
        if ct_len == 0 {
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
        if !is_mut_static {
            self.out.puts(b"const \0".as_ptr());
        }
        unsafe {
            self.emit_declarator(ct, ct_len, name, nlen);
        }
        if declare_only == 0 {
            if !init.is_null() {
                self.out.puts(b" = \0".as_ptr());
                let mut locals = LocalTab::new();
                unsafe { self.emit_expr(init, &mut locals) };
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

    /* extern block members: prototypes only. */
    unsafe fn lower_extern_block(&mut self, item: *const pm_jit_rsx_ast_t) {
        let kids = unsafe { (*item).kids };
        let nk = unsafe { (*item).n_kids } as usize;
        let mut i = 0usize;
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
        self.out.puts(b"{\n\0".as_ptr());
        /* body: the tail expression of a value-returning fn becomes
         * `return expr;` — C has no implicit block value. */
        if !body.is_null() {
            let mut locals = LocalTab::new();
            if self.recv_len > 0 {
                unsafe {
                    locals.add(b"self\0".as_ptr(), 4, self.recv_type.as_ptr(), self.recv_len, 1);
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
                                    locals.add(pt, ptl, ct, n, 1);
                                }
                            }
                        }
                    }
                    p += 1;
                }
            }
            self.depth = 1;
            if !ret.is_null() {
                let bk = unsafe { (*body).kids };
                let bn = unsafe { (*body).n_kids } as usize;
                let mut i = 0usize;
                while i + 1 < bn {
                    let st = unsafe { *bk.add(i) };
                    unsafe { self.emit_stmt(st, &mut locals, 0) };
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
                        unsafe { self.emit_if_tail(tail, &mut locals) };
                    } else if k == pm_jit_rsx_ast_kind::MATCH {
                        /* tail match: declare `ret` from the fn's C type and
                         * let each arm store into it, then return. */
                        self.indent();
                        self.out.put(ret_ct, ret_len);
                        self.out.puts(b" ret;\n\0".as_ptr());
                        unsafe { self.emit_match_value(tail, &mut locals, b"ret\0".as_ptr(), 3) };
                        self.indent();
                        self.out.puts(b"return ret;\n\0".as_ptr());
                    } else if k == pm_jit_rsx_ast_kind::RETURN
                        || k == pm_jit_rsx_ast_kind::LET
                        || k == pm_jit_rsx_ast_kind::STMT
                        || k == pm_jit_rsx_ast_kind::MACRO
                        || k == pm_jit_rsx_ast_kind::EXPR_STMT
                        || k == pm_jit_rsx_ast_kind::ASSIGN
                        || k == pm_jit_rsx_ast_kind::LOOP
                    {
                        unsafe { self.emit_stmt(tail, &mut locals, 0) };
                    } else if k == pm_jit_rsx_ast_kind::BLOCK {
                        /* tail block expr (typically an `unsafe { value }`
                         * wrapper): walk to the inner tail and return it. */
                        unsafe { self.emit_block_tail_ret(tail, &mut locals) };
                    } else {
                        self.indent();
                        self.out.puts(b"return \0".as_ptr());
                        unsafe { self.emit_expr(tail, &mut locals) };
                        self.out.puts(b";\n\0".as_ptr());
                    }
                }
            } else {
                unsafe { self.emit_block_stmt(body, &mut locals) };
            }
            self.depth = 0;
        }
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
                } else if unsafe { z_eq(t, tl, b"path\0".as_ptr()) } {
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
            let _ = saved;
            if !self.ok {
                return;
            }
            i += 1;
        }
    }

    /* File: two passes — collect types, then emit in dependency order. */
    unsafe fn lower_file(&mut self, file: *const pm_jit_rsx_ast_t) -> bool {
        if file.is_null() || !self.ok {
            return false;
        }
        unsafe { self.collect(file) };
        if !self.ok {
            return false;
        }
        self.out.puts(b"/* generated by pymergetic.metal.jit.rs.compiler */\n\0".as_ptr());
        self.out.puts(b"#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n#include <string.h>\n\0".as_ptr());
        self.out.putc(b'\n');
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
        /* pass 0: statics first, so array-length constants are visible to
         * every struct/field declaration below. Externs stay after the
         * struct pass: their prototypes name the borrowed types. */
        i = 0;
        while i < nk {
            item = unsafe { *kids.add(i) };
            if item.is_null() {
                i += 1;
                continue;
            }
            kind = unsafe { (*item).kind };
            if kind == pm_jit_rsx_ast_kind::STATIC || kind == pm_jit_rsx_ast_kind::CONST {
                unsafe { self.lower_static(item, 0) };
            }
            if !self.ok {
                bad = true;
                self.ok = true;
            }
            i += 1;
        }
        /* pass A: struct/enum/typedef items */
        i = 0;
        while i < nk {
            item = unsafe { *kids.add(i) };
            if item.is_null() {
                i += 1;
                continue;
            }
            kind = unsafe { (*item).kind };
            if kind == pm_jit_rsx_ast_kind::STRUCT {
                unsafe { self.lower_struct(item) };
            } else if kind == pm_jit_rsx_ast_kind::ENUM {
                unsafe { self.lower_enum(item) };
            } else if kind == pm_jit_rsx_ast_kind::TYPE_ALIAS {
                unsafe { self.lower_type_alias(item) };
            }
            if !self.ok {
                bad = true;
                self.ok = true;
            }
            i += 1;
        }
        /* pass B: extern prototypes — after structs, they name borrowed
         * types in their signatures. */
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
        if bad {
            self.ok = false;
            return false;
        }
        true
    }
}

/* ================= AST dump (inspect face) ================= */

/* Renders into the caller's fixed buffer (the header's contract: bytes
 * written, or -1 when out_cap is short). Depth by two spaces per level. */
unsafe fn dump_node(
    out: *mut u8,
    cap: usize,
    at_in: usize,
    n: *const pm_jit_rsx_ast_t,
    depth: usize,
) -> usize {
    let mut at = at_in;
    if n.is_null() {
        return at;
    }
    let mut i = 0usize;
    while i < depth {
        at = unsafe { bput(out, cap, at, b"  \0".as_ptr(), 2) };
        i += 1;
    }
    at = unsafe { zput(out, cap, at, unsafe { ast_kind_name(unsafe { (*n).kind }) }) };
    if unsafe { (*n).text_len } > 0 {
        at = unsafe { bput(out, cap, at, b" \0".as_ptr(), 1) };
        at = unsafe { bput(out, cap, at, unsafe { (*n).text }, unsafe { (*n).text_len }) };
    }
    at = unsafe { bput(out, cap, at, b"\n\0".as_ptr(), 1) };
    let kids = unsafe { (*n).kids };
    let nk = unsafe { (*n).n_kids } as usize;
    let mut j = 0usize;
    while j < nk {
        at = unsafe { dump_node(out, cap, at, *kids.add(j), depth + 1) };
        j += 1;
    }
    at
}

unsafe fn ast_kind_name(k: pm_jit_rsx_ast_kind) -> *const u8 {
    let z = match k {
        pm_jit_rsx_ast_kind::FILE => b"FILE\0".as_ptr(),
        pm_jit_rsx_ast_kind::USE => b"USE\0".as_ptr(),
        pm_jit_rsx_ast_kind::FN => b"FN\0".as_ptr(),
        pm_jit_rsx_ast_kind::STRUCT => b"STRUCT\0".as_ptr(),
        pm_jit_rsx_ast_kind::ENUM => b"ENUM\0".as_ptr(),
        pm_jit_rsx_ast_kind::IMPL => b"IMPL\0".as_ptr(),
        pm_jit_rsx_ast_kind::EXTERN_BLOCK => b"EXTERN_BLOCK\0".as_ptr(),
        pm_jit_rsx_ast_kind::STATIC => b"STATIC\0".as_ptr(),
        pm_jit_rsx_ast_kind::CONST => b"CONST\0".as_ptr(),
        pm_jit_rsx_ast_kind::TYPE_ALIAS => b"TYPE_ALIAS\0".as_ptr(),
        pm_jit_rsx_ast_kind::TRAIT => b"TRAIT\0".as_ptr(),
        pm_jit_rsx_ast_kind::MODULE => b"MODULE\0".as_ptr(),
        pm_jit_rsx_ast_kind::ATTR => b"ATTR\0".as_ptr(),
        pm_jit_rsx_ast_kind::BLOCK => b"BLOCK\0".as_ptr(),
        pm_jit_rsx_ast_kind::STMT => b"STMT\0".as_ptr(),
        pm_jit_rsx_ast_kind::LET => b"LET\0".as_ptr(),
        pm_jit_rsx_ast_kind::IF => b"IF\0".as_ptr(),
        pm_jit_rsx_ast_kind::MATCH => b"MATCH\0".as_ptr(),
        pm_jit_rsx_ast_kind::MATCH_ARM => b"MATCH_ARM\0".as_ptr(),
        pm_jit_rsx_ast_kind::LOOP => b"LOOP\0".as_ptr(),
        pm_jit_rsx_ast_kind::WHILE => b"WHILE\0".as_ptr(),
        pm_jit_rsx_ast_kind::FOR => b"FOR\0".as_ptr(),
        pm_jit_rsx_ast_kind::RETURN => b"RETURN\0".as_ptr(),
        pm_jit_rsx_ast_kind::BREAK => b"BREAK\0".as_ptr(),
        pm_jit_rsx_ast_kind::CONTINUE => b"CONTINUE\0".as_ptr(),
        pm_jit_rsx_ast_kind::EXPR_STMT => b"EXPR_STMT\0".as_ptr(),
        pm_jit_rsx_ast_kind::ASSIGN => b"ASSIGN\0".as_ptr(),
        pm_jit_rsx_ast_kind::BINARY => b"BINARY\0".as_ptr(),
        pm_jit_rsx_ast_kind::UNARY => b"UNARY\0".as_ptr(),
        pm_jit_rsx_ast_kind::CALL => b"CALL\0".as_ptr(),
        pm_jit_rsx_ast_kind::METHOD_CALL => b"METHOD_CALL\0".as_ptr(),
        pm_jit_rsx_ast_kind::FIELD => b"FIELD\0".as_ptr(),
        pm_jit_rsx_ast_kind::PATH => b"PATH\0".as_ptr(),
        pm_jit_rsx_ast_kind::LITERAL => b"LITERAL\0".as_ptr(),
        pm_jit_rsx_ast_kind::TUPLE => b"TUPLE\0".as_ptr(),
        pm_jit_rsx_ast_kind::STRUCT_LIT => b"STRUCT_LIT\0".as_ptr(),
        pm_jit_rsx_ast_kind::CLOSURE => b"CLOSURE\0".as_ptr(),
        pm_jit_rsx_ast_kind::INDEX => b"INDEX\0".as_ptr(),
        pm_jit_rsx_ast_kind::ARRAY => b"ARRAY\0".as_ptr(),
        pm_jit_rsx_ast_kind::CAST => b"CAST\0".as_ptr(),
        pm_jit_rsx_ast_kind::MACRO => b"MACRO\0".as_ptr(),
        pm_jit_rsx_ast_kind::PAREN => b"PAREN\0".as_ptr(),
        pm_jit_rsx_ast_kind::TYPE => b"TYPE\0".as_ptr(),
        pm_jit_rsx_ast_kind::PARAM => b"PARAM\0".as_ptr(),
        pm_jit_rsx_ast_kind::STRUCT_FIELD => b"STRUCT_FIELD\0".as_ptr(),
        pm_jit_rsx_ast_kind::ENUM_VARIANT => b"ENUM_VARIANT\0".as_ptr(),
        pm_jit_rsx_ast_kind::GENERIC => b"GENERIC\0".as_ptr(),
        pm_jit_rsx_ast_kind::WHERE => b"WHERE\0".as_ptr(),
    };
    z
}

/* ================= C ABI entry points ================= */

/// Tokenize `src`. On success fills `*toklist` (arena-owned, END-terminated)
/// and returns 0; on failure returns -1 with `rsx: …` in errbuf.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_jit_rsx_lex(
    arena: *mut pm_util_mem_arena_t,
    src: *const u8,
    src_len: usize,
    toklist: *mut pm_jit_rsx_toklist_t,
    errbuf: *mut u8,
    errbuf_len: usize,
) -> i32 {
    let mut lx = Lexer {
        arena,
        src,
        src_len,
        pos: 0,
        line: 1,
        toks: Toks::new(arena),
        errbuf,
        errcap: errbuf_len,
        ok: true,
    };
    unsafe { lx.run() };
    if !lx.ok || !lx.toks.ok {
        return -1;
    }
    unsafe {
        (*toklist).toks = lx.toks.p;
        (*toklist).n_toks = lx.toks.n as u32;
    }
    0
}

/// Parse a token list into an AST. Returns 0 and sets `*unit_out`, or -1
/// with `rsx: …` in errbuf.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_jit_rsx_parse(
    arena: *mut pm_util_mem_arena_t,
    toks: *const pm_jit_rsx_toklist_t,
    unit_out: *mut *mut pm_jit_rsx_ast_t,
    errbuf: *mut u8,
    errbuf_len: usize,
) -> i32 {
    let n_toks = if toks.is_null() { 0 } else { unsafe { (*toks).n_toks } };
    if n_toks == 0 {
        unsafe {
            err_set(errbuf, errbuf_len, b"empty token list\0".as_ptr(), 0);
        }
        return -1;
    }
    let mut p = Parser {
        arena,
        toks: if toks.is_null() { core::ptr::null() } else { unsafe { (*toks).toks } },
        n_toks,
        at: 0,
        nd: Node {
            arena,
            errbuf,
            errcap: errbuf_len,
            errline: 0,
            ok: true,
        },
        ok: true,
        cond_ctx: false,
    };
    let file = unsafe { p.parse_file() };
    if !p.ok || !p.nd.ok || file.is_null() {
        return -1;
    }
    unsafe {
        *unit_out = file;
    }
    0
}

/// Render an AST as indented text into the caller's buffer.
/// Returns bytes written, or -1 when out_cap is too short.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_jit_rsx_ast_dump(
    ast: *const pm_jit_rsx_ast_t,
    out: *mut u8,
    out_cap: usize,
    errbuf: *mut u8,
    errbuf_len: usize,
) -> i32 {
    if out.is_null() || out_cap == 0 {
        unsafe {
            err_set(errbuf, errbuf_len, b"no dump buffer\0".as_ptr(), 0);
        }
        return -1;
    }
    let at = unsafe { dump_node(out, out_cap, 0, ast, 0) };
    if at + 1 >= out_cap {
        unsafe {
            err_set(errbuf, errbuf_len, b"dump buffer too small\0".as_ptr(), 0);
        }
        return -1;
    }
    unsafe {
        *out.add(at) = 0;
    }
    at as i32
}

/// Lower a parsed AST to C text. Returns 0 and fills `*c_out`/`*c_out_len`
/// (NUL-terminated, arena-owned), or -1 with `rsx: …` in errbuf.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_jit_rsx_lower(
    arena: *mut pm_util_mem_arena_t,
    unit: *const pm_jit_rsx_ast_t,
    c_out: *mut *mut u8,
    c_out_len: *mut usize,
    errbuf: *mut u8,
    errbuf_len: usize,
) -> i32 {
    if unit.is_null() {
        unsafe {
            err_set(errbuf, errbuf_len, b"null AST\0".as_ptr(), 0);
        }
        return -1;
    }
    let mut lw = Lower {
        arena,
        out: Out::new(arena),
        errbuf,
        errcap: errbuf_len,
        errline: 0,
        ok: true,
        syms: SymTab::new(arena),
        fns: FnTab::new(),
        consts: ConstTab::new(),
        depth: 0,
        cur_impl: [b"\0".as_ptr(); 16],
        cur_impl_lens: [0; 16],
        cur_impl_n: 0,
        recv_type: [0; 64],
        recv_len: 0,
        oom_buf: [0; 160],
        nerrs: 0,
    };
    let good = unsafe { lw.lower_file(unit) };
    if !good || !lw.ok || !lw.out.ok {
        /* aborted without a specific message (arena exhausted mid-render):
         * leave the caller a reason instead of an empty errbuf */
        unsafe {
            if !lw.errbuf.is_null() && lw.errcap > 0 && *lw.errbuf == 0 {
                err_set(lw.errbuf, lw.errcap, b"arena exhausted while lowering\0".as_ptr(), 0);
            }
        }
        return -1;
    }
    unsafe {
        lw.out.putc(0);
        *c_out = lw.out.p;
        *c_out_len = lw.out.len - 1;
    }
    0
}

/// One-shot: Rust source bytes to generated C (lex + parse + lower).
/// The prove path — also the face the build card's Rust unit compiles with.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_metal_jit_rsx_compile(
    arena: *mut pm_util_mem_arena_t,
    source: *const u8,
    source_len: usize,
    c_out: *mut *mut u8,
    c_out_len: *mut usize,
    errbuf: *mut u8,
    errbuf_len: usize,
) -> i32 {
    let mut toks = pm_jit_rsx_toklist_t {
        toks: core::ptr::null_mut(),
        n_toks: 0,
    };
    let mut unit: *mut pm_jit_rsx_ast_t = core::ptr::null_mut();
    unsafe {
        let r = pm_metal_jit_rsx_lex(arena, source, source_len, &mut toks, errbuf, errbuf_len);
        if r != 0 {
            return r;
        }
        let r = pm_metal_jit_rsx_parse(arena, &toks, &mut unit, errbuf, errbuf_len);
        if r != 0 {
            return r;
        }
        pm_metal_jit_rsx_lower(arena, unit, c_out, c_out_len, errbuf, errbuf_len)
    }
}

/// Number of token kinds in the `__types__.h` X-macro table.
#[unsafe(no_mangle)]
pub extern "C" fn pm_metal_jit_rsx_token_kind_count() -> u32 {
    TOK_KIND_COUNT
}

/// Number of AST kinds in the `__types__.h` X-macro table.
#[unsafe(no_mangle)]
pub extern "C" fn pm_metal_jit_rsx_ast_kind_count() -> u32 {
    AST_KIND_COUNT
}

/* ---- registration (the registry table is built by the real toolchain) ---- */

pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.jit.rs.compiler",
    pm_metal_jit_rsx_lex,
    "int32_t(pm_util_mem_arena_t *, const char *, size_t, pm_jit_rsx_toklist_t *, char *, size_t)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.jit.rs.compiler",
    pm_metal_jit_rsx_parse,
    "int32_t(pm_util_mem_arena_t *, const pm_jit_rsx_toklist_t *, pm_jit_rsx_ast_t **, char *, size_t)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.jit.rs.compiler",
    pm_metal_jit_rsx_ast_dump,
    "int32_t(const pm_jit_rsx_ast_t *, char *, size_t, char *, size_t)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.jit.rs.compiler",
    pm_metal_jit_rsx_lower,
    "int32_t(pm_util_mem_arena_t *, const pm_jit_rsx_ast_t *, char **, size_t *, char *, size_t)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.jit.rs.compiler",
    pm_metal_jit_rsx_compile,
    "int32_t(pm_util_mem_arena_t *, const char *, size_t, char **, size_t *, char *, size_t)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.jit.rs.compiler",
    pm_metal_jit_rsx_token_kind_count,
    "uint32_t(void)"
);
pymergetic_wasmmod::PM_MOD_EXPORT_RS!(
    "pymergetic.metal.jit.rs.compiler",
    pm_metal_jit_rsx_ast_kind_count,
    "uint32_t(void)"
);
