//! lexer — MicroPython-style tokeniser (upstream `py/lexer.{h,c}`, mem reader
//! only).
//!
//! Deliberate omissions vs. upstream (kept honest, not stubbed):
//! - No f-strings / t-strings (`MICROPY_PY_FSTRINGS` / `MICROPY_PY_TSTRINGS`
//!   off) — `is_string_or_bytes`/the string loop mirror the `#else` branch.
//! - No `MICROPY_PY_BUILTINS_STR_UNICODE` — `\u`/`\U` escapes above `0xFF`
//!   surface as `TokenKind::Invalid`, same as upstream's non-unicode branch.
//! - `\N{...}` unicode-name escapes need a name table Metal doesn't carry;
//!   surfaced as `TokenKind::Invalid` (a real token, not a panic) instead of
//!   upstream's `mp_raise_NotImplementedError`.
//! - No POSIX/VFS file reader (`reader::Reader` is mem-only).

use crate::upy::py::malloc;
use crate::upy::py::qstr;
use crate::upy::py::qstrdefs::Qstr;
use crate::upy::py::reader::{Reader, READER_EOF};

const TAB_SIZE: usize = 8;
/// Sentinel chr0/chr1/chr2 value for end-of-stream (upstream `MP_LEXER_EOF`).
const LEXER_EOF: u32 = 0;
/// A real `\0` byte in the source is remapped to this so it isn't confused
/// with the end-of-stream sentinel (upstream `MP_LEXER_INVALID_BYTE`).
const LEXER_INVALID_BYTE: u32 = 1;

#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TokenKind {
    End,

    Invalid,
    DedentMismatch,
    LonelyStringOpen,

    Newline,
    Indent,
    Dedent,

    Name,
    Integer,
    FloatOrImag,
    String,
    Bytes,

    Ellipsis,

    KwFalse,
    KwNone,
    KwTrue,
    KwDebug,
    KwAnd,
    KwAs,
    KwAssert,
    KwAsync,
    KwAwait,
    KwBreak,
    KwClass,
    KwContinue,
    KwDef,
    KwDel,
    KwElif,
    KwElse,
    KwExcept,
    KwFinally,
    KwFor,
    KwFrom,
    KwGlobal,
    KwIf,
    KwImport,
    KwIn,
    KwIs,
    KwLambda,
    KwNonlocal,
    KwNot,
    KwOr,
    KwPass,
    KwRaise,
    KwReturn,
    KwTry,
    KwWhile,
    KwWith,
    KwYield,

    OpAssign,
    OpTilde,

    // Order matches upstream's mp_binary_op_t grouping comment; kept for
    // side-by-side diffing against lexer.c, not consumed here.
    OpLess,
    OpMore,
    OpDblEqual,
    OpLessEqual,
    OpMoreEqual,
    OpNotEqual,

    OpPipe,
    OpCaret,
    OpAmpersand,
    OpDblLess,
    OpDblMore,
    OpPlus,
    OpMinus,
    OpStar,
    OpAt,
    OpDblSlash,
    OpSlash,
    OpPercent,
    OpDblStar,

    DelPipeEqual,
    DelCaretEqual,
    DelAmpersandEqual,
    DelDblLessEqual,
    DelDblMoreEqual,
    DelPlusEqual,
    DelMinusEqual,
    DelStarEqual,
    DelAtEqual,
    DelDblSlashEqual,
    DelSlashEqual,
    DelPercentEqual,
    DelDblStarEqual,

    DelParenOpen,
    DelParenClose,
    DelBracketOpen,
    DelBracketClose,
    DelBraceOpen,
    DelBraceClose,
    DelComma,
    DelColon,
    DelPeriod,
    DelSemicolon,
    DelEqual,
    DelMinusMore,
}

// Sorted for strcmp-style keyword lookup (upstream `tok_kw`), async/await
// always included (no `MICROPY_PY_ASYNC_AWAIT` gate in Metal's config).
static TOK_KW: &[&[u8]] = &[
    b"False",
    b"None",
    b"True",
    b"__debug__",
    b"and",
    b"as",
    b"assert",
    b"async",
    b"await",
    b"break",
    b"class",
    b"continue",
    b"def",
    b"del",
    b"elif",
    b"else",
    b"except",
    b"finally",
    b"for",
    b"from",
    b"global",
    b"if",
    b"import",
    b"in",
    b"is",
    b"lambda",
    b"nonlocal",
    b"not",
    b"or",
    b"pass",
    b"raise",
    b"return",
    b"try",
    b"while",
    b"with",
    b"yield",
];

// Parallel to `TOK_KW` (upstream computes this as `MP_TOKEN_KW_FALSE + i`;
// a parallel table is the plain-Rust equivalent of that offset trick).
static TOK_KW_KIND: &[TokenKind] = &[
    TokenKind::KwFalse,
    TokenKind::KwNone,
    TokenKind::KwTrue,
    TokenKind::KwDebug,
    TokenKind::KwAnd,
    TokenKind::KwAs,
    TokenKind::KwAssert,
    TokenKind::KwAsync,
    TokenKind::KwAwait,
    TokenKind::KwBreak,
    TokenKind::KwClass,
    TokenKind::KwContinue,
    TokenKind::KwDef,
    TokenKind::KwDel,
    TokenKind::KwElif,
    TokenKind::KwElse,
    TokenKind::KwExcept,
    TokenKind::KwFinally,
    TokenKind::KwFor,
    TokenKind::KwFrom,
    TokenKind::KwGlobal,
    TokenKind::KwIf,
    TokenKind::KwImport,
    TokenKind::KwIn,
    TokenKind::KwIs,
    TokenKind::KwLambda,
    TokenKind::KwNonlocal,
    TokenKind::KwNot,
    TokenKind::KwOr,
    TokenKind::KwPass,
    TokenKind::KwRaise,
    TokenKind::KwReturn,
    TokenKind::KwTry,
    TokenKind::KwWhile,
    TokenKind::KwWith,
    TokenKind::KwYield,
];

// Upstream's tricky operator encoding (verbatim comment from lexer.c):
//     <op>  = begin with <op>, if this opchar matches then begin here
//     e<op> = end with <op>, if this opchar matches then end
//     c<op> = continue with <op>, if this opchar matches then continue
// this means if the start of two ops are the same then they are equal til
// the last char.
const TOK_ENC: &[u8] =
    b"()[]{},;~:e=<e=c<e=>e=c>e=*e=c*e=+e=-e=e>&e=|e=/e=c/e=%e=^e=@e==e=!.";

// Must have the same order as `TOK_ENC`'s single-char entries (upstream
// `tok_enc_kind`).
static TOK_ENC_KIND: [TokenKind; 44] = [
    TokenKind::DelParenOpen,
    TokenKind::DelParenClose,
    TokenKind::DelBracketOpen,
    TokenKind::DelBracketClose,
    TokenKind::DelBraceOpen,
    TokenKind::DelBraceClose,
    TokenKind::DelComma,
    TokenKind::DelSemicolon,
    TokenKind::OpTilde,
    TokenKind::DelColon,
    TokenKind::OpAssign,
    TokenKind::OpLess,
    TokenKind::OpLessEqual,
    TokenKind::OpDblLess,
    TokenKind::DelDblLessEqual,
    TokenKind::OpMore,
    TokenKind::OpMoreEqual,
    TokenKind::OpDblMore,
    TokenKind::DelDblMoreEqual,
    TokenKind::OpStar,
    TokenKind::DelStarEqual,
    TokenKind::OpDblStar,
    TokenKind::DelDblStarEqual,
    TokenKind::OpPlus,
    TokenKind::DelPlusEqual,
    TokenKind::OpMinus,
    TokenKind::DelMinusEqual,
    TokenKind::DelMinusMore,
    TokenKind::OpAmpersand,
    TokenKind::DelAmpersandEqual,
    TokenKind::OpPipe,
    TokenKind::DelPipeEqual,
    TokenKind::OpSlash,
    TokenKind::DelSlashEqual,
    TokenKind::OpDblSlash,
    TokenKind::DelDblSlashEqual,
    TokenKind::OpPercent,
    TokenKind::DelPercentEqual,
    TokenKind::OpCaret,
    TokenKind::DelCaretEqual,
    TokenKind::OpAt,
    TokenKind::DelAtEqual,
    TokenKind::DelEqual,
    TokenKind::OpDblEqual,
];

fn ascii_is_space(c: u32) -> bool {
    (0x09..=0x0D).contains(&c) || c == 0x20
}

fn ascii_is_alpha(c: u32) -> bool {
    (0x41..=0x5A).contains(&c) || (0x61..=0x7A).contains(&c)
}

fn ascii_is_digit(c: u32) -> bool {
    (0x30..=0x39).contains(&c)
}

fn ascii_is_xdigit(c: u32) -> bool {
    ascii_is_digit(c) || (0x41..=0x46).contains(&c) || (0x61..=0x66).contains(&c)
}

fn ascii_xdigit_value(c: u32) -> u32 {
    if ascii_is_digit(c) {
        c - 0x30
    } else {
        (c | 0x20) - 0x61 + 10
    }
}

/// Growable indent-column stack (upstream `indent_level`); grows via Metal
/// heap so deeply nested code never hits an arbitrary fixed cap.
struct IndentStack {
    ptr: *mut u16,
    cap: usize,
    len: usize,
}

impl IndentStack {
    fn new() -> Self {
        const INIT_CAP: usize = 8;
        let ptr = unsafe { malloc::m_malloc(INIT_CAP * core::mem::size_of::<u16>()) } as *mut u16;
        if ptr.is_null() {
            return IndentStack {
                ptr: core::ptr::null_mut(),
                cap: 0,
                len: 0,
            };
        }
        unsafe { *ptr = 0 };
        IndentStack {
            ptr,
            cap: INIT_CAP,
            len: 1,
        }
    }

    fn top(&self) -> u16 {
        if self.ptr.is_null() || self.len == 0 {
            return 0;
        }
        unsafe { *self.ptr.add(self.len - 1) }
    }

    fn push(&mut self, v: u16) -> bool {
        if self.ptr.is_null() {
            return false;
        }
        if self.len >= self.cap {
            let ncap = self.cap * 2;
            let np =
                unsafe { malloc::m_realloc(self.ptr as *mut u8, ncap * core::mem::size_of::<u16>()) }
                    as *mut u16;
            if np.is_null() {
                return false;
            }
            self.ptr = np;
            self.cap = ncap;
        }
        unsafe { *self.ptr.add(self.len) = v };
        self.len += 1;
        true
    }

    fn pop(&mut self) {
        if self.len > 0 {
            self.len -= 1;
        }
    }
}

impl Drop for IndentStack {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { malloc::m_free(self.ptr as *mut u8) };
        }
    }
}

/// Tokeniser over a `Reader`. Public fields mirror upstream's `mp_lexer_t`
/// (`source_name`, `tok_line`, `tok_column`, `tok_kind`, token text via
/// `tok_text()`/`vstr`).
pub struct Lexer {
    source_name: Qstr,
    reader: Reader,

    chr0: u32,
    chr1: u8,
    chr2: u8,

    line: usize,
    column: usize,

    emit_dent: i32,
    nested_bracket_level: i32,

    indent: IndentStack,

    pub tok_line: usize,
    pub tok_column: usize,
    pub tok_kind: TokenKind,

    tok_buf: *mut u8,
    tok_len: usize,
    tok_cap: usize,
}

impl Lexer {
    pub fn new(source_name: Qstr, reader: Reader) -> Lexer {
        let mut lex = Lexer {
            source_name,
            reader,
            chr0: 0,
            chr1: 0,
            chr2: 0,
            line: 1,
            // Account for 3 dummy priming bytes below (upstream `(size_t)-2`).
            column: 0usize.wrapping_sub(2),
            emit_dent: 0,
            nested_bracket_level: 0,
            indent: IndentStack::new(),
            tok_line: 0,
            tok_column: 0,
            tok_kind: TokenKind::End,
            tok_buf: core::ptr::null_mut(),
            tok_len: 0,
            tok_cap: 0,
        };

        // Load first 3 bytes of the stream; next_char() advances column to 1.
        lex.next_char();
        lex.next_char();
        lex.next_char();

        lex.to_next();

        // First token must be at column 1 unless it's a bare newline;
        // otherwise force an INDENT so the parser reports a syntax error.
        if lex.tok_column != 1 && lex.tok_kind != TokenKind::Newline {
            lex.tok_kind = TokenKind::Indent;
        }

        lex
    }

    /// `src` must outlive the lexer (e.g. a `&'static` firmware source).
    pub fn new_from_str_len(source_name: Qstr, src: &'static [u8]) -> Lexer {
        Lexer::new(source_name, Reader::new_mem(src))
    }

    /// Convenience over `new_from_str_len` that interns `name` via `qstr::from_str`.
    pub fn new_from_named_str(name: &str, src: &'static [u8]) -> Lexer {
        Lexer::new_from_str_len(qstr::from_str(name), src)
    }

    pub fn source_name(&self) -> Qstr {
        self.source_name
    }

    pub fn tok_text(&self) -> &[u8] {
        if self.tok_buf.is_null() {
            &[]
        } else {
            unsafe { core::slice::from_raw_parts(self.tok_buf, self.tok_len) }
        }
    }

    fn tok_reset(&mut self) {
        self.tok_len = 0;
    }

    fn tok_push(&mut self, b: u8) {
        if self.tok_len >= self.tok_cap {
            let ncap = if self.tok_cap == 0 { 32 } else { self.tok_cap * 2 };
            let np = unsafe { malloc::m_realloc(self.tok_buf, ncap) };
            if np.is_null() {
                self.tok_kind = TokenKind::Invalid;
                return;
            }
            self.tok_buf = np;
            self.tok_cap = ncap;
        }
        unsafe { *self.tok_buf.add(self.tok_len) = b };
        self.tok_len += 1;
    }

    fn tok_cut_tail(&mut self, n: usize) {
        self.tok_len = self.tok_len.saturating_sub(n);
    }

    fn is_end(&self) -> bool {
        self.chr0 == LEXER_EOF
    }

    fn is_physical_newline(&self) -> bool {
        self.chr0 == b'\n' as u32
    }

    fn is_char(&self, c: u8) -> bool {
        self.chr0 == c as u32
    }

    fn is_char_or(&self, c1: u8, c2: u8) -> bool {
        self.chr0 == c1 as u32 || self.chr0 == c2 as u32
    }

    fn is_char_or3(&self, c1: u8, c2: u8, c3: u8) -> bool {
        self.chr0 == c1 as u32 || self.chr0 == c2 as u32 || self.chr0 == c3 as u32
    }

    fn is_char_following(&self, c: u8) -> bool {
        self.chr1 == c
    }

    fn is_char_following_following_or(&self, c1: u8, c2: u8) -> bool {
        self.chr2 == c1 || self.chr2 == c2
    }

    fn is_char_and(&self, c1: u8, c2: u8) -> bool {
        self.chr0 == c1 as u32 && self.chr1 == c2
    }

    fn is_whitespace(&self) -> bool {
        ascii_is_space(self.chr0)
    }

    fn is_letter(&self) -> bool {
        ascii_is_alpha(self.chr0)
    }

    fn is_digit(&self) -> bool {
        ascii_is_digit(self.chr0)
    }

    fn is_following_digit(&self) -> bool {
        ascii_is_digit(self.chr1 as u32)
    }

    fn is_following_base_char(&self) -> bool {
        let c = self.chr1 | 0x20;
        c == b'b' || c == b'o' || c == b'x'
    }

    fn is_following_odigit(&self) -> bool {
        (b'0'..=b'7').contains(&self.chr1)
    }

    fn is_string_or_bytes(&self) -> bool {
        self.is_char_or(b'\'', b'"')
            || (self.is_char_or3(b'r', b'u', b'b') && self.is_char_following_or(b'\'', b'"'))
            || ((self.is_char_and(b'r', b'b') || self.is_char_and(b'b', b'r'))
                && self.is_char_following_following_or(b'\'', b'"'))
    }

    fn is_char_following_or(&self, c1: u8, c2: u8) -> bool {
        self.chr1 == c1 || self.chr1 == c2
    }

    // to easily parse utf-8 identifiers we allow any raw byte with high bit set
    fn is_head_of_identifier(&self) -> bool {
        self.is_letter() || self.is_char(b'_') || self.chr0 >= 0x80
    }

    fn is_tail_of_identifier(&self) -> bool {
        self.is_head_of_identifier() || self.is_digit()
    }

    fn next_char(&mut self) {
        if self.chr0 == b'\n' as u32 {
            self.line += 1;
            self.column = 1;
        } else if self.chr0 == b'\t' as u32 {
            self.column = (((self.column - 1 + TAB_SIZE) / TAB_SIZE) * TAB_SIZE) + 1;
        } else {
            self.column = self.column.wrapping_add(1);
        }

        self.chr0 = self.chr1 as u32;
        self.chr1 = self.chr2;

        let mut chr2: u32;
        loop {
            let b = self.reader.readbyte();
            chr2 = if b == READER_EOF {
                LEXER_EOF
            } else if b == LEXER_EOF {
                LEXER_INVALID_BYTE
            } else {
                b
            };

            if self.chr1 == b'\r' {
                self.chr1 = b'\n';
                if chr2 == b'\n' as u32 {
                    // CR LF is a single new line, throw out the extra LF
                    continue;
                }
            }
            break;
        }

        // insert a newline at end of file if the last real char wasn't one
        if chr2 == LEXER_EOF && self.chr1 != LEXER_EOF as u8 && self.chr1 != b'\n' {
            chr2 = b'\n' as u32;
        }

        self.chr2 = chr2 as u8;
    }

    /// Called with `chr0` at the char before the first hex digit; advances
    /// past `num_digits` hex chars. Sets `tok_kind` to `Invalid` on a
    /// non-hex char (upstream's `get_hex` returning false).
    fn get_hex(&mut self, num_digits: u32) -> u32 {
        let mut num: u32 = 0;
        for _ in 0..num_digits {
            self.next_char();
            let c = self.chr0;
            if !ascii_is_xdigit(c) {
                self.tok_kind = TokenKind::Invalid;
                return num;
            }
            num = (num << 4) + ascii_xdigit_value(c);
        }
        num
    }

    /// `chr0` is the char right after the backslash. Returns the resolved
    /// char value to push, or `None` if nothing should be pushed (escaped
    /// newline: a line continuation inside the string).
    fn escape_value(&mut self) -> Option<u32> {
        let c = self.chr0;
        match c as u8 {
            b'\n' => None,
            b'\\' | b'\'' | b'"' => Some(c),
            b'a' => Some(0x07),
            b'b' => Some(0x08),
            b't' => Some(0x09),
            b'n' => Some(0x0a),
            b'v' => Some(0x0b),
            b'f' => Some(0x0c),
            b'r' => Some(0x0d),
            b'x' => Some(self.get_hex(2)),
            b'u' => Some(self.get_hex(4)),
            b'U' => Some(self.get_hex(8)),
            b'N' => {
                // No unicode name table in Metal; a real error token, not a stub.
                self.tok_kind = TokenKind::Invalid;
                None
            }
            b'0'..=b'7' => {
                let mut digits = 3i32;
                let mut num = c - b'0' as u32;
                loop {
                    if !self.is_following_odigit() {
                        break;
                    }
                    digits -= 1;
                    if digits == 0 {
                        break;
                    }
                    self.next_char();
                    num = num * 8 + (self.chr0 - b'0' as u32);
                }
                Some(num)
            }
            _ => {
                // unrecognised escape: CPython lets this through verbatim
                // as '\' followed by the character.
                self.tok_push(b'\\');
                Some(c)
            }
        }
    }

    fn parse_string_literal(&mut self, is_raw: bool) {
        let mut quote_char = b'\'';
        if self.is_char(b'"') {
            quote_char = b'"';
        }
        self.next_char();

        let num_quotes: usize;
        if self.is_char_and(quote_char, quote_char) {
            self.next_char();
            self.next_char();
            num_quotes = 3;
        } else {
            num_quotes = 1;
        }

        let mut n_closing = 0usize;
        while !self.is_end() && (num_quotes > 1 || !self.is_char(b'\n')) && n_closing < num_quotes {
            if self.is_char(quote_char) {
                n_closing += 1;
                self.tok_push(self.chr0 as u8);
            } else {
                n_closing = 0;
                if self.is_char(b'\\') {
                    self.next_char();
                    let value = if is_raw {
                        self.tok_push(b'\\');
                        Some(self.chr0)
                    } else {
                        self.escape_value()
                    };
                    if let Some(v) = value {
                        if v < 0x100 {
                            self.tok_push(v as u8);
                        } else {
                            self.tok_kind = TokenKind::Invalid;
                        }
                    }
                } else {
                    self.tok_push(self.chr0 as u8);
                }
            }
            self.next_char();
        }

        if n_closing < num_quotes {
            self.tok_kind = TokenKind::LonelyStringOpen;
        }
        self.tok_cut_tail(n_closing);
    }

    // Returns whether it crossed a physical newline (always true when
    // `stop_at_newline`, since it stops right there).
    fn skip_whitespace(&mut self, stop_at_newline: bool) -> bool {
        while !self.is_end() {
            if self.is_physical_newline() {
                if stop_at_newline && self.nested_bracket_level == 0 {
                    return true;
                }
                self.next_char();
            } else if self.is_whitespace() {
                self.next_char();
            } else if self.is_char(b'#') {
                self.next_char();
                while !self.is_end() && !self.is_physical_newline() {
                    self.next_char();
                }
            } else if self.is_char_and(b'\\', b'\n') {
                self.next_char();
                self.next_char();
            } else {
                break;
            }
        }
        false
    }

    fn lex_string_or_bytes(&mut self) {
        // MP_TOKEN_END is the "first literal in this run" sentinel.
        self.tok_kind = TokenKind::End;
        loop {
            let mut is_raw = false;
            let mut kind = TokenKind::String;
            let mut n_char = 0u8;
            if self.is_char(b'u') {
                n_char = 1;
            } else if self.is_char(b'b') {
                kind = TokenKind::Bytes;
                n_char = 1;
                if self.is_char_following(b'r') {
                    is_raw = true;
                    n_char = 2;
                }
            } else if self.is_char(b'r') {
                is_raw = true;
                n_char = 1;
                if self.is_char_following(b'b') {
                    kind = TokenKind::Bytes;
                    n_char = 2;
                }
            }

            if self.tok_kind == TokenKind::End {
                self.tok_kind = kind;
            } else if self.tok_kind != kind {
                // can't concatenate str with bytes
                break;
            }

            if n_char != 0 {
                self.next_char();
                if n_char == 2 {
                    self.next_char();
                }
            }

            self.parse_string_literal(is_raw);

            self.skip_whitespace(true);
            if !self.is_string_or_bytes() {
                break;
            }
        }
    }

    fn lex_name(&mut self) {
        self.tok_kind = TokenKind::Name;

        self.tok_push(self.chr0 as u8);
        self.next_char();

        while !self.is_end() && self.is_tail_of_identifier() {
            self.tok_push(self.chr0 as u8);
            self.next_char();
        }

        let s = self.tok_text();
        for (i, kw) in TOK_KW.iter().enumerate() {
            match s.cmp(*kw) {
                core::cmp::Ordering::Equal => {
                    self.tok_kind = TOK_KW_KIND[i];
                    if self.tok_kind == TokenKind::KwDebug {
                        // Optimise level fixed at 0 (no compiler flag surface
                        // yet): __debug__ always resolves to True.
                        self.tok_kind = TokenKind::KwTrue;
                    }
                    break;
                }
                core::cmp::Ordering::Less => break,
                core::cmp::Ordering::Greater => {}
            }
        }
    }

    fn lex_number(&mut self) {
        let mut forced_integer = false;
        if self.is_char(b'.') {
            self.tok_kind = TokenKind::FloatOrImag;
        } else {
            self.tok_kind = TokenKind::Integer;
            if self.is_char(b'0') && self.is_following_base_char() {
                forced_integer = true;
            }
        }

        self.tok_push(self.chr0 as u8);
        self.next_char();

        loop {
            if self.is_end() {
                break;
            }
            if !forced_integer && self.is_char_or(b'e', b'E') {
                self.tok_kind = TokenKind::FloatOrImag;
                self.tok_push(b'e');
                self.next_char();
                if self.is_char(b'+') || self.is_char(b'-') {
                    self.tok_push(self.chr0 as u8);
                    self.next_char();
                }
            } else if self.is_letter() || self.is_digit() || self.is_char(b'.') {
                if self.is_char(b'.') || self.is_char(b'j') || self.is_char(b'J') {
                    self.tok_kind = TokenKind::FloatOrImag;
                }
                self.tok_push(self.chr0 as u8);
                self.next_char();
            } else if self.is_char(b'_') {
                self.next_char();
            } else {
                break;
            }
        }
    }

    fn lex_op_or_delim(&mut self) {
        let mut ti = 0usize;
        let mut tok_enc_index = 0usize;
        while ti < TOK_ENC.len() && !self.is_char(TOK_ENC[ti]) {
            if TOK_ENC[ti] == b'e' || TOK_ENC[ti] == b'c' {
                ti += 1;
            }
            ti += 1;
            tok_enc_index += 1;
        }

        self.next_char();

        if ti >= TOK_ENC.len() {
            self.tok_kind = TokenKind::Invalid;
        } else if TOK_ENC[ti] == b'!' {
            // "!=" is a special case because "!" is not a valid operator
            if self.is_char(b'=') {
                self.next_char();
                self.tok_kind = TokenKind::OpNotEqual;
            } else {
                self.tok_kind = TokenKind::Invalid;
            }
        } else if TOK_ENC[ti] == b'.' {
            // "." and "..." are special cases because ".." is not valid
            if self.is_char_and(b'.', b'.') {
                self.next_char();
                self.next_char();
                self.tok_kind = TokenKind::Ellipsis;
            } else {
                self.tok_kind = TokenKind::DelPeriod;
            }
        } else {
            let mut tp = ti + 1;
            let mut t_index = tok_enc_index;
            while tp < TOK_ENC.len() && (TOK_ENC[tp] == b'c' || TOK_ENC[tp] == b'e') {
                t_index += 1;
                if tp + 1 < TOK_ENC.len() && self.is_char(TOK_ENC[tp + 1]) {
                    self.next_char();
                    tok_enc_index = t_index;
                    if TOK_ENC[tp] == b'e' {
                        break;
                    }
                } else if TOK_ENC[tp] == b'c' {
                    break;
                }
                tp += 2;
            }

            self.tok_kind = TOK_ENC_KIND[tok_enc_index];

            match self.tok_kind {
                TokenKind::DelParenOpen | TokenKind::DelBracketOpen | TokenKind::DelBraceOpen => {
                    self.nested_bracket_level += 1;
                }
                TokenKind::DelParenClose | TokenKind::DelBracketClose | TokenKind::DelBraceClose => {
                    self.nested_bracket_level -= 1;
                }
                _ => {}
            }
        }
    }

    /// Advance to the next token (upstream `mp_lexer_to_next`).
    pub fn to_next(&mut self) {
        self.tok_reset();

        // Skip whitespace/comments; the newline (if any) is reported at the
        // position of the preceding line, then re-skipped from scratch below.
        let had_physical_newline = self.skip_whitespace(true);

        self.tok_line = self.line;
        self.tok_column = self.column;

        if self.emit_dent < 0 {
            self.tok_kind = TokenKind::Dedent;
            self.emit_dent += 1;
        } else if self.emit_dent > 0 {
            self.tok_kind = TokenKind::Indent;
            self.emit_dent -= 1;
        } else if had_physical_newline {
            self.skip_whitespace(false);
            self.tok_kind = TokenKind::Newline;

            let num_spaces = (self.column - 1) as u16;
            let top = self.indent.top();
            if num_spaces > top {
                self.indent.push(num_spaces);
                self.emit_dent += 1;
            } else if num_spaces < top {
                while num_spaces < self.indent.top() {
                    self.indent.pop();
                    self.emit_dent -= 1;
                }
                if num_spaces != self.indent.top() {
                    self.tok_kind = TokenKind::DedentMismatch;
                }
            }
        } else if self.is_end() {
            self.tok_kind = TokenKind::End;
        } else if self.is_string_or_bytes() {
            self.lex_string_or_bytes();
        } else if self.is_head_of_identifier() {
            self.lex_name();
        } else if self.is_digit() || (self.is_char(b'.') && self.is_following_digit()) {
            self.lex_number();
        } else {
            self.lex_op_or_delim();
        }
    }
}

impl Drop for Lexer {
    fn drop(&mut self) {
        if !self.tok_buf.is_null() {
            unsafe { malloc::m_free(self.tok_buf) };
        }
    }
}
