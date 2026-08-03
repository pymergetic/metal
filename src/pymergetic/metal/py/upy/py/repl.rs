//! repl — REPL line-continuation + a thin lex/parse/compile/execute
//! helper (upstream `py/repl.c` mirror slice).
//!
//! ## What's ported
//!
//! - [`continue_with_input`]: a faithful, byte-for-byte port of upstream's
//!   `mp_repl_continue_with_input` (unmatched brackets/triple-quotes,
//!   trailing backslash, compound-keyword-without-a-body detection). No
//!   `MICROPY_PY_SYS_PS1_PS2` config gate to port -- Metal's REPL PS1/PS2
//!   strings are a boot-shell concern (W11.2/W11.3), not this module's.
//! - [`exec_line`]: **not** upstream (`repl.c` has no such function --
//!   upstream's real console loop lives in `shared/runtime/pyexec.c` and
//!   drives the lexer/compiler/VM itself). This is Metal's own thin glue
//!   tying `continue_with_input` to the lexer/`parse`/`compile`/`vm` chain
//!   that already exists, so a future console (W11.2/W11.3) has one call
//!   to make per line instead of re-deriving this dispatch. It reuses
//!   `compile.rs`'s existing supported subset as-is -- no new compiler
//!   surface.
//!
//! ## What's *not* ported
//!
//! - `mp_repl_autocomplete` (upstream's qstr-table name-completion +
//!   column-formatted printing): real, but a console-output feature, not
//!   compile/execute logic -- out of scope for this pass. `mp_repl_get_ps1`/
//!   `_ps2` similarly belong with the console (W11.2/W11.3).
//! - A `MP_PARSE_SINGLE_INPUT`-grammar path. Upstream's real REPL always
//!   parses with `MP_PARSE_SINGLE_INPUT` and lets the *parser* mark a lone
//!   expression for auto-print; `compile.rs`'s supported subset (see its
//!   module doc) was never built against that grammar rule. Auto-print
//!   here instead tries [`crate::upy::py::parse::InputKind::Eval`] first
//!   (an honest "is this a bare expression" probe) and only falls back to
//!   `InputKind::File` if that parse fails -- same user-visible REPL
//!   behavior for the subset `compile.rs` actually supports, without
//!   pretending `Single`-grammar support that isn't there.
//!
//! ## Session state
//!
//! `exec_line` takes the module globals dict (`vm::CodeState::globals`) as
//! a plain argument instead of owning one itself: this module has no
//! long-lived singleton, matching upstream's own `mp_globals_get()` model
//! where *the console* owns `__main__`'s dict across the whole session,
//! not `repl.c`. A future console creates one `objdict` at boot and passes
//! it to every `exec_line` call for the session's lifetime (freeing it
//! itself when the session ends); pass `obj::OBJ_NULL` for a stateless
//! one-shot eval (module-level names then honestly `Exception` on load/
//! store, per `vm.rs`'s existing behavior -- not a silent no-op).

use crate::upy::py::compile::{self, CompileError};
use crate::upy::py::lexer::Lexer;
use crate::upy::py::obj::{self, MpObj};
use crate::upy::py::objects::{objbool, objnone};
use crate::upy::py::parse::{self, InputKind, ParseError};
use crate::upy::py::qstr;
use crate::upy::py::reader::Reader;
use crate::upy::py::runtime::{self, VmReturnKind};
use crate::upy::py::vm::{self, CodeState};

fn is_ident_char(b: u8) -> bool {
    b.is_ascii_alphanumeric() || b == b'_'
}

/// Upstream `str_startswith_word`, on bytes instead of a NUL-terminated
/// C string.
fn starts_with_word(input: &[u8], word: &[u8]) -> bool {
    if input.len() < word.len() || &input[..word.len()] != word {
        return false;
    }
    match input.get(word.len()) {
        None => true,
        Some(&b) => !is_ident_char(b),
    }
}

/// `true` iff `input` (one REPL submission so far, as typed -- may span
/// several `\n`-joined lines already) is not yet a complete statement and
/// the REPL should keep reading more lines before lexing/parsing it.
///
/// Port of upstream `mp_repl_continue_with_input`, adapted for one real
/// difference in convention: upstream's caller (`pyexec.c`) builds `input`
/// from a raw `readline()` buffer that has **no** trailing `\n` for the
/// line just typed (a `\n` is appended only *before* reading the next
/// continuation line, so the buffer ends in `\n` exactly when the user's
/// most recent line was empty -- upstream's real "blank line finishes the
/// block" signal). Every source buffer elsewhere in this mirror (see
/// `compile.rs`'s smoke, `lexer.rs`, ...) instead always carries its own
/// trailing `\n`, matching a REPL that appends the newline as soon as the
/// user presses Enter. To keep the same "blank line finishes the block"
/// signal under that convention, this strips exactly one trailing `\n`
/// (if present) before running the rest of the algorithm unchanged: a
/// plain line like `b"def f():\n"` becomes `"def f():"` for the checks
/// below (matching upstream's un-terminated single line), while a
/// genuinely blank continuation line leaves an extra trailing `\n` behind
/// after the strip (e.g. `b"if x:\n    pass\n\n"` -> `"if x:\n    pass\n"`,
/// ending in `\n` -- "don't continue", same as upstream).
pub fn continue_with_input(input: &[u8]) -> bool {
    if input.is_empty() {
        return false;
    }
    let body = match input.split_last() {
        Some((&b'\n', rest)) => rest,
        _ => input,
    };

    let starts_with_compound_keyword = body.first() == Some(&b'@')
        || starts_with_word(body, b"if")
        || starts_with_word(body, b"while")
        || starts_with_word(body, b"for")
        || starts_with_word(body, b"try")
        || starts_with_word(body, b"with")
        || starts_with_word(body, b"def")
        || starts_with_word(body, b"class")
        || starts_with_word(body, b"async");

    const Q_NONE: u8 = 0;
    const Q_1_SINGLE: u8 = 1;
    const Q_1_DOUBLE: u8 = 2;
    const Q_3_SINGLE: u8 = 3;
    const Q_3_DOUBLE: u8 = 4;

    let mut n_paren: i32 = 0;
    let mut n_brack: i32 = 0;
    let mut n_brace: i32 = 0;
    let mut in_quote: u8 = Q_NONE;

    let n = body.len();
    let mut i = 0usize;
    while i < n {
        let c = body[i];
        if c == b'\'' {
            if (in_quote == Q_NONE || in_quote == Q_3_SINGLE)
                && i + 2 < n
                && body[i + 1] == b'\''
                && body[i + 2] == b'\''
            {
                i += 2;
                in_quote = Q_3_SINGLE - in_quote;
            } else if in_quote == Q_NONE || in_quote == Q_1_SINGLE {
                in_quote = Q_1_SINGLE - in_quote;
            }
        } else if c == b'"' {
            if (in_quote == Q_NONE || in_quote == Q_3_DOUBLE)
                && i + 2 < n
                && body[i + 1] == b'"'
                && body[i + 2] == b'"'
            {
                i += 2;
                in_quote = Q_3_DOUBLE - in_quote;
            } else if in_quote == Q_NONE || in_quote == Q_1_DOUBLE {
                in_quote = Q_1_DOUBLE - in_quote;
            }
        } else if c == b'\\' && i + 1 < n && matches!(body[i + 1], b'\'' | b'"' | b'\\') {
            if in_quote != Q_NONE {
                i += 1;
            }
        } else if in_quote == Q_NONE {
            match c {
                b'(' => n_paren += 1,
                b')' => n_paren -= 1,
                b'[' => n_brack += 1,
                b']' => n_brack -= 1,
                b'{' => n_brace += 1,
                b'}' => n_brace -= 1,
                _ => {}
            }
        }
        i += 1;
    }

    if in_quote == Q_3_SINGLE || in_quote == Q_3_DOUBLE {
        return true;
    }
    if (n_paren > 0 || n_brack > 0 || n_brace > 0) && in_quote == Q_NONE {
        return true;
    }
    if body.last() == Some(&b'\\') {
        return true;
    }
    if starts_with_compound_keyword && body.last() != Some(&b'\n') {
        return true;
    }
    false
}

/// Whether a line is being fed as one interactive REPL submission
/// (auto-print a lone expression's value, matching a real `>>>` prompt)
/// or as a batch of file/script source (never auto-print). See module
/// doc for exactly how `Single` decides "expression" vs "statement".
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ReplMode {
    Single,
    File,
}

/// A successfully-evaluated REPL value, with an ASCII decimal/keyword
/// repr precomputed (bounded: the widest producible value today is a
/// signed `isize`, `None`, `True`, or `False` -- see `format_value`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ReplValue {
    pub obj: MpObj,
    text: [u8; 24],
    len: u8,
}

impl ReplValue {
    pub fn repr(&self) -> &[u8] {
        &self.text[..self.len as usize]
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ReplResult {
    /// `continue_with_input` says this submission isn't complete yet --
    /// the caller should read another line and append it before retrying.
    NeedMore,
    /// A bare expression evaluated to this value (only reachable in
    /// [`ReplMode::Single`] -- see module doc).
    Value(ReplValue),
    /// A statement ran to completion with nothing to print.
    Executed,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ReplError {
    Parse(ParseError),
    Compile(CompileError),
    /// The VM raised (upstream would print the traceback here -- no
    /// exception object/traceback surface exists yet, see `vm.rs`).
    Exception,
}

fn write_isize(buf: &mut [u8; 24], v: isize) -> usize {
    if v == 0 {
        buf[0] = b'0';
        return 1;
    }
    let neg = v < 0;
    let mut mag = v.unsigned_abs();
    let mut digits = [0u8; 24];
    let mut n_digits = 0;
    while mag > 0 {
        digits[n_digits] = b'0' + (mag % 10) as u8;
        mag /= 10;
        n_digits += 1;
    }
    let mut len = 0;
    if neg {
        buf[0] = b'-';
        len = 1;
    }
    for j in (0..n_digits).rev() {
        buf[len] = digits[j];
        len += 1;
    }
    len
}

/// Repr for the values `compile.rs`'s current supported subset can
/// actually produce (small int / `None` / `True` / `False`). Anything
/// else prints an honest placeholder rather than a wrong value -- not
/// reachable today, but the VM/object layer will grow before this
/// exhaustively covers every `MpObj` kind, and a placeholder is better
/// than a panic when it does.
fn format_value(o: MpObj) -> ReplValue {
    let mut text = [0u8; 24];
    let len = if let Some(v) = obj::small_int_value_checked(o) {
        write_isize(&mut text, v)
    } else if objnone::is_none(o) {
        text[..4].copy_from_slice(b"None");
        4
    } else if let Some(b) = objbool::value(o) {
        if b {
            text[..4].copy_from_slice(b"True");
            4
        } else {
            text[..5].copy_from_slice(b"False");
            5
        }
    } else {
        text[..9].copy_from_slice(b"<unknown>");
        9
    };
    ReplValue {
        obj: o,
        text,
        len: len as u8,
    }
}

/// Lex+parse+compile+execute one already-assembled REPL submission (a
/// caller loop should keep appending lines while `continue_with_input`
/// says to, then call this once on the whole thing).
///
/// - [`ReplMode::Single`]: tries it as a bare expression first
///   (`parse::InputKind::Eval` -- auto-print, matching an interactive
///   `>>>` prompt); if that doesn't parse as an expression at all, falls
///   back to compiling/running it as a statement.
/// - [`ReplMode::File`]: always compiles/runs as a statement (module top
///   level) -- a script's top-level expressions are evaluated and
///   discarded, never auto-printed.
///
/// `src` need not be `'static` (a REPL line buffer is typically freshly
/// assembled each submission) -- it only has to outlive this call, which
/// it does: nothing here stores a borrow past `exec_line` returning.
///
/// `globals` is threaded straight into `vm::CodeState::globals` for both
/// the eval and statement paths (see "Session state" above) -- the same
/// dict passed across repeated calls gives a persistent REPL session.
pub fn exec_line(src: &[u8], mode: ReplMode, globals: MpObj) -> Result<ReplResult, ReplError> {
    if continue_with_input(src) {
        return Ok(ReplResult::NeedMore);
    }
    if !crate::upy::py::mpstate::ready() {
        runtime::init();
    }
    let name = qstr::from_str("<repl>");

    if mode == ReplMode::Single {
        let mut lex = Lexer::new(name, unsafe { Reader::borrow(src.as_ptr(), src.len()) });
        if let Ok(tree) = parse::parse(&mut lex, InputKind::Eval) {
            let raw = compile::compile_eval(&tree).map_err(ReplError::Compile)?;
            let mut st = CodeState::new();
            st.globals = globals;
            return match vm::execute(raw.as_bytecode(), raw.as_consts(), &mut st) {
                VmReturnKind::Normal => Ok(ReplResult::Value(format_value(st.result))),
                _ => Err(ReplError::Exception),
            };
        }
    }

    let mut lex = Lexer::new(name, unsafe { Reader::borrow(src.as_ptr(), src.len()) });
    let tree = parse::parse(&mut lex, InputKind::File).map_err(ReplError::Parse)?;
    let raw = compile::compile_file(&tree).map_err(ReplError::Compile)?;
    let mut st = CodeState::new();
    st.globals = globals;
    match vm::execute(raw.as_bytecode(), raw.as_consts(), &mut st) {
        VmReturnKind::Normal => Ok(ReplResult::Executed),
        _ => Err(ReplError::Exception),
    }
}
