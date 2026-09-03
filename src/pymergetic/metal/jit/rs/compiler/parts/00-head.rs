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
//! `extern "C"` qualifiers, `mod name;` / `mod name { .. }` items (module
//! structure is compile-time — both forms lower to a comment, the inline
//! body is not lowered). Refused: generics on items, `trait` items,
//! `macro_rules!`, `async`/`const` blocks, nested items in fn bodies.
//!
//! Statements: `let` (ident / `mut ident` / `_`, optional type + init; let-else
//! refused), `if`/`else`, `match`, `loop`, `while`, `for pat in a..b` /
//! `a..=b`, `return`/`break`/`continue` (labeled too: `'l: for …` +
//! `continue 'l` — C has no labeled break, so it lowers to goto labels),
//! expression and assignment statements.
//!
//! Expressions: literals (int with suffixes, float, char, byte char/string,
//! string, raw string), paths, calls, method calls, field access, indexing
//! (incl. range indexes `a[..n]` `a[n..]` `a[n..m]` — the slice stays a
//! pointer to the start element, the length side is not carried), casts,
//! unary `! - * & &mut`, binary ops, ranges, parens, struct literals
//! (`T { f: v }` — `..base` and shorthand refused), block-exprs, `unsafe`
//! blocks, `expr?` (Option-of-pointer only, inside a pointer-returning fn —
//! lowers to a GNU statement expression that early-returns `0`/None).
//! Closures parse but lowering refuses them.
//!
//! Types: `u8..u64` `i8..i64` `usize` `isize` `f32` `f64` `bool` `char`
//! (`u128`/`i128` have no C type), `*const T` `*mut T` `&T` `&mut T`
//! (lifetimes skipped), `[T; N]` `[T]` `&[T]`, `()` (return only), paths
//! with one generic list (`Option<T>` — pointer/fn-ptr payload only),
//! `fn(..) -> R` and `unsafe extern "C" fn(..) -> R`, tuples `(A, B)` ->
//! anonymous-layout structs `_0`/`_1`/... (let-pattern destructuring and
//! numeric field access `t.0` included; tuple struct patterns refuse).
//!
//! ## Lowering rules
//!
//! - `struct S { f: T }` -> `typedef struct S { C_T f; } S;` (declaration
//!   order is the layout; `#[repr(C)]` is accepted and recorded, non-repr
//!   structs lower the same way — documented divergence).
//! - `union U { f: T }` -> `typedef union U { C_T f; } U;` — same field
//!   grammar as a struct, same registration; a literal `U { f: v }` is a
//!   designated initializer (sets the active member), field access reads
//!   the active member like any C union.
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
//!   literals/variants, literal range patterns `lo..=hi` (inclusive both
//!   ends). Guards, open-ended ranges, tuple-of-Some `let-else` is the one
//!   tuple form accepted; struct patterns refuse.
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
//!   `.add(k)`/`.sub(k)`->`(x + k)`/`(x - k)`, `.as_ptr()` -> identity
//!   (on a pointer-to-array receiver: the C deref — the array lvalue
//!   decays to the element pointer),
//!   `.is_ascii_{digit,alphanumeric,alphabetic}()` -> range tests,
//!   `.len()` -> literal/array constant only,
//!   `AtomicU32` -> `_Atomic uint32_t`, `AtomicU32::new(v)` -> `(v)`,
//!   `.load/.store/.swap(Ordering::X)` on an `AtomicU32` ->
//!   `__atomic_load/_store/_exchange` builtins with the stdatomic.h
//!   order numbering (Relaxed=0..SeqCst=5), `Ordering::X` -> the int,
//!   `core::hint::spin_loop()` -> `0`. Anything else refuses.
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

/* Does a block's control flow never fall off the end? let-else's flat
 * lowering is only sound when the else-block diverges (return/break/
 * continue as its last statement, or a nested block/stmt wrapper that
 * does). Loops/if-chains that always-diverge are NOT detected — the
 * corpus writes straight-line else-blocks, and a missed shape refuses
 * at parse rather than miscompiling. */
unsafe fn block_diverges(b: *const pm_jit_rsx_ast_t) -> bool {
    let mut n = b;
    let mut hops = 0usize;
    while !n.is_null() && hops < 32 {
        let k = unsafe { (*n).kind };
        if k == pm_jit_rsx_ast_kind::RETURN
            || k == pm_jit_rsx_ast_kind::BREAK
            || k == pm_jit_rsx_ast_kind::CONTINUE
        {
            return true;
        }
        if k == pm_jit_rsx_ast_kind::BLOCK || k == pm_jit_rsx_ast_kind::STMT {
            let kids = unsafe { (*n).kids };
            let nk = unsafe { (*n).n_kids } as usize;
            if nk == 0 {
                return false;
            }
            n = unsafe { *kids.add(nk - 1) };
            hops += 1;
            continue;
        }
        if k == pm_jit_rsx_ast_kind::EXPR_STMT {
            let kids = unsafe { (*n).kids };
            let nk = unsafe { (*n).n_kids } as usize;
            if nk == 0 {
                return false;
            }
            n = unsafe { *kids.add(0) };
            hops += 1;
            continue;
        }
        return false;
    }
    false
}

/* An INDEX node whose index kid is a range (`a[lo..hi]` / `a[..hi]` /
 * `a[lo..]`): the index is a BINARY with text ".."/"..=". Shared by the
 * emission, ctype-inference, and unary-& paths. */
unsafe fn rsx_idx_is_range(e: *const pm_jit_rsx_ast_t) -> bool {
    if e.is_null() {
        return false;
    }
    if (unsafe { (*e).kind }) != pm_jit_rsx_ast_kind::INDEX {
        return false;
    }
    let kids = unsafe { (*e).kids };
    if (unsafe { (*e).n_kids }) < 2 {
        return false;
    }
    let idx: *const pm_jit_rsx_ast_t = unsafe { *kids.add(1) };
    if (unsafe { (*idx).kind }) != pm_jit_rsx_ast_kind::BINARY {
        return false;
    }
    let t = unsafe { (*idx).text };
    let tl = unsafe { (*idx).text_len };
    (unsafe { z_eq(t, tl, b"..\0".as_ptr()) }) || (unsafe { z_eq(t, tl, b"..=\0".as_ptr()) })
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
