//! emitcommon — binary/unary-op encoding shared by `compile.rs`, `emitbc.rs`
//! and `vm.rs` (upstream `py/emitcommon.c` mirror slice).
//!
//! Upstream's `emitcommon.c` holds infrastructure shared by *every* emit
//! backend (bytecode and native): qstr/const-object interning tables. This
//! mirror has only one backend (`emitbc.rs`; no native emitter), so the
//! thing actually shared across `compile.rs` <-> `emitbc.rs` <-> `vm.rs` is
//! the binary/unary-op numbering and the token/rule -> op mapping — moved
//! here instead of duplicated in each.
//!
//! The numeric layout mirrors upstream `mp_binary_op_t`/`mp_unary_op_t`
//! (`py/runtime0.h`) exactly (9 relational + 13 inplace + 13 normal
//! arithmetic for binary; 4 bytecode-visible unary ops), even though only
//! a subset has a real handler in `vm.rs` today — see that module's doc
//! for the supported list. Keeping the *numbering* exact (not just the
//! ops we implement) means `BINARY_OP_MULTI + op` never silently shifts
//! if support for another op is added later.

use crate::upy::py::grammar::RuleId;
use crate::upy::py::lexer::TokenKind;

/// Cap on positional args a single `CALL_FUNCTION` can carry — shared by
/// `compile.rs` (rejects a call with more args as `Unsupported`, not a
/// silent truncation) and `vm.rs` (fixed-size arg buffer, no heap alloc
/// per call). Generous for the finished vertical slice's actual call
/// shapes (0- and 1-arg native/print calls); grows trivially later if a
/// real caller needs more.
pub const MAX_CALL_ARGS: usize = 8;

/// Cap on per-`RawCode` constant-object slots (`LOAD_CONST_OBJ` /
/// `Writer::push_const`). Enough for nested `def` FunBc objects embedded
/// in a module body without a full MicroPython const table.
pub const MAX_CONST_OBJS: usize = 32;

// -- unary ops (bytecode-visible subset only -- upstream also has
// runtime-only ops like __len__/__hash__ that never appear in bytecode at
// all, so they have no place in this table) -----------------------------
pub const UNARY_OP_POSITIVE: u8 = 0;
pub const UNARY_OP_NEGATIVE: u8 = 1;
pub const UNARY_OP_INVERT: u8 = 2;
pub const UNARY_OP_NOT: u8 = 3;

// -- binary ops -----------------------------------------------------------
// 9 relational (order matches the OpLess..OpNotEqual token run below).
pub const BINARY_OP_LESS: u8 = 0;
pub const BINARY_OP_MORE: u8 = 1;
pub const BINARY_OP_EQUAL: u8 = 2;
pub const BINARY_OP_LESS_EQUAL: u8 = 3;
pub const BINARY_OP_MORE_EQUAL: u8 = 4;
pub const BINARY_OP_NOT_EQUAL: u8 = 5;
pub const BINARY_OP_IN: u8 = 6;
pub const BINARY_OP_IS: u8 = 7;
pub const BINARY_OP_EXCEPTION_MATCH: u8 = 8;
// 9..21: 13 inplace ops (`+=` etc.). This compiler slice has no
// augmented-assignment support, so none of these are ever emitted --
// listed only so the arithmetic ops below keep upstream's exact offset.
// 21..34: 13 normal arithmetic ops (order matches the
// OpDblLess..OpPercent + OpDblStar token run).
pub const BINARY_OP_OR: u8 = 21;
pub const BINARY_OP_XOR: u8 = 22;
pub const BINARY_OP_AND: u8 = 23;
pub const BINARY_OP_LSHIFT: u8 = 24;
pub const BINARY_OP_RSHIFT: u8 = 25;
pub const BINARY_OP_ADD: u8 = 26;
pub const BINARY_OP_SUBTRACT: u8 = 27;
pub const BINARY_OP_MULTIPLY: u8 = 28;
pub const BINARY_OP_MAT_MULTIPLY: u8 = 29;
pub const BINARY_OP_FLOOR_DIVIDE: u8 = 30;
pub const BINARY_OP_TRUE_DIVIDE: u8 = 31;
pub const BINARY_OP_MODULO: u8 = 32;
pub const BINARY_OP_POWER: u8 = 33;

/// Map a `comp_op` relational token (`<`,`>`,`==`,`<=`,`>=`,`!=`) to its
/// binary-op id. `tok` is a leaf token node's raw `parse::leaf_arg` value
/// (i.e. `TokenKind as usize`) — `parse.rs` never hands back a `TokenKind`
/// directly, only that raw id, so every caller here compares against
/// `TokenKind::X as usize` rather than reconstructing an enum value.
/// `in`/`is`/`not in`/`is not` are structurally distinct (see `parse.rs`'s
/// `CompOpNotIn`/`CompOpIs`/`CompOpIsNot`) and are handled in `compile.rs`
/// (token `KwIn` maps here; the struct forms invert with `UNARY_OP_NOT`).
pub fn compare_op_from_token(tok: usize) -> Option<u8> {
    if tok == TokenKind::KwIn as usize {
        return Some(BINARY_OP_IN);
    }
    if tok == TokenKind::KwIs as usize {
        return Some(BINARY_OP_IS);
    }
    let base = TokenKind::OpLess as usize;
    if tok < base || tok > TokenKind::OpNotEqual as usize {
        return None;
    }
    Some(BINARY_OP_LESS + (tok - base) as u8)
}

/// Map an `arith_op`/`term_op`/`shift_op` token to its binary-op id (see
/// `compare_op_from_token` doc on the raw `usize` token id). Token order
/// mirrors the op order exactly over this contiguous run (see `lexer.rs`
/// module doc) — same trick upstream's `compile_term` uses. Note
/// `OpDblStar` (`**`) is deliberately *not* in this run: `power` is a
/// separate grammar rule (`PowerDblStar`) this compiler doesn't handle.
pub fn binary_op_from_term_token(tok: usize) -> Option<u8> {
    let base = TokenKind::OpDblLess as usize;
    if tok < base || tok > TokenKind::OpPercent as usize {
        return None;
    }
    Some(BINARY_OP_LSHIFT + (tok - base) as u8)
}

/// Map an `expr`/`xor_expr`/`and_expr` struct kind (bitwise `|`/`^`/`&`,
/// where the operator is implied by the rule itself, not stored as a
/// token) to its binary-op id.
pub fn binary_op_from_rule(kind: RuleId) -> Option<u8> {
    match kind {
        RuleId::Expr => Some(BINARY_OP_OR),
        RuleId::XorExpr => Some(BINARY_OP_XOR),
        RuleId::AndExpr => Some(BINARY_OP_AND),
        _ => None,
    }
}

/// Map a `factor_2` unary-prefix token (`+`,`-`,`~`) to its unary-op id
/// (see `compare_op_from_token` doc on the raw `usize` token id).
pub fn unary_op_from_factor_token(tok: usize) -> Option<u8> {
    if tok == TokenKind::OpTilde as usize {
        Some(UNARY_OP_INVERT)
    } else if tok == TokenKind::OpPlus as usize {
        Some(UNARY_OP_POSITIVE)
    } else if tok == TokenKind::OpMinus as usize {
        Some(UNARY_OP_NEGATIVE)
    } else {
        None
    }
}

/// Map an `augassign` token (`+=`, `-=`, ...) to the plain binary-op id
/// this compiler lowers it to (upstream keeps separate inplace op ids
/// 9..21 -- see the module doc's numbering note -- but `vm.rs` has no
/// separate inplace handlers, and Metal's compiler already lowers every
/// augmented assignment to "load, binary-op, store" at compile time, so
/// mapping straight to the normal arithmetic op is the honest, finished
/// choice rather than adding 13 unused inplace opcodes no backend
/// implements). Not a contiguous token run (grammar declares them in a
/// different order than the lexer or the op numbering), so this is a
/// plain match, not an offset trick like `compare_op_from_token`.
/// `None` for `@=`/`**=` (matrix-multiply/power have no `vm.rs` binary
/// handler either -- `compile.rs` turns that into `Unsupported`).
pub fn binary_op_from_augassign_token(tok: usize) -> Option<u8> {
    if tok == TokenKind::DelPlusEqual as usize {
        Some(BINARY_OP_ADD)
    } else if tok == TokenKind::DelMinusEqual as usize {
        Some(BINARY_OP_SUBTRACT)
    } else if tok == TokenKind::DelStarEqual as usize {
        Some(BINARY_OP_MULTIPLY)
    } else if tok == TokenKind::DelSlashEqual as usize {
        Some(BINARY_OP_TRUE_DIVIDE)
    } else if tok == TokenKind::DelDblSlashEqual as usize {
        Some(BINARY_OP_FLOOR_DIVIDE)
    } else if tok == TokenKind::DelPercentEqual as usize {
        Some(BINARY_OP_MODULO)
    } else if tok == TokenKind::DelAmpersandEqual as usize {
        Some(BINARY_OP_AND)
    } else if tok == TokenKind::DelPipeEqual as usize {
        Some(BINARY_OP_OR)
    } else if tok == TokenKind::DelCaretEqual as usize {
        Some(BINARY_OP_XOR)
    } else if tok == TokenKind::DelDblLessEqual as usize {
        Some(BINARY_OP_LSHIFT)
    } else if tok == TokenKind::DelDblMoreEqual as usize {
        Some(BINARY_OP_RSHIFT)
    } else {
        None
    }
}
