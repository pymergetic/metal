//! scope — id/local tracking for the compiler (upstream `py/scope.{c,h}`
//! mirror slice).
//!
//! Upstream's `scope_t` tracks every kind of identifier a function can
//! have (params, locals, cells, free vars, globals-declared-in-function)
//! across arbitrarily nested scopes. This mirror only needs what
//! `compile.rs`'s supported subset actually uses:
//!
//! - **Module scope** never allocates local slots — top-level names always
//!   compile to `LOAD_NAME`/`STORE_NAME` against the runtime globals dict
//!   supplied by the caller at execute time (`vm::CodeState::globals`), so
//!   `Scope` for `ScopeKind::Module` is just a marker.
//! - **Function scope** allocates one slot per parameter (in source order)
//!   and per distinct assigned name (upstream `scope_find_or_add_id`), capped
//!   at `MAX_LOCALS` slots. That cap is the *only* local-slot opcode range
//!   this compiler emits (`LOAD_FAST_MULTI`/`STORE_FAST_MULTI`); the
//!   general `LOAD_FAST_N`/`STORE_FAST_N` (uint-indexed, unbounded slot
//!   count) has no emitter/vm support yet — a real, documented omission,
//!   not a silent truncation (`compile.rs` returns `CompileError::TooManyLocals`
//!   instead of emitting past the cap).
//! - **Simple positional parameters only** (no defaults, `*`, `**`, or
//!   annotations). Unbound names in a function body use `LOAD_NAME`.
//!
//! A fixed-size array (not a Metal-heap growable buffer) is enough here:
//! `MAX_LOCALS` is a small, deliberate compile-time cap, so there is
//! nothing to grow — unlike `parse.rs`'s unbounded rule/result stacks.

use crate::upy::py::bc0;
use crate::upy::py::qstrdefs::{Qstr, QSTR_NULL};

/// Function scopes cap out at this many distinct locals — matches
/// `bc0::LOAD_FAST_MULTI_NUM`/`STORE_FAST_MULTI_NUM`, the only local-slot
/// opcodes this compiler emits (see module doc).
pub const MAX_LOCALS: usize = bc0::LOAD_FAST_MULTI_NUM;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ScopeKind {
    Module,
    Function,
}

pub struct Scope {
    pub kind: ScopeKind,
    ids: [Qstr; MAX_LOCALS],
    len: usize,
}

impl Scope {
    pub fn new(kind: ScopeKind) -> Self {
        Self {
            kind,
            ids: [QSTR_NULL; MAX_LOCALS],
            len: 0,
        }
    }

    pub fn is_function(&self) -> bool {
        matches!(self.kind, ScopeKind::Function)
    }

    pub fn num_locals(&self) -> usize {
        self.len
    }

    /// Slot for an already-assigned local, if any (upstream `scope_find_local_id`).
    pub fn find_local(&self, qst: Qstr) -> Option<u16> {
        self.ids[..self.len].iter().position(|&q| q == qst).map(|i| i as u16)
    }

    /// Find-or-allocate a local slot for `qst` (upstream
    /// `scope_find_or_add_id`). `None` only once `MAX_LOCALS` is
    /// exhausted; the caller turns that into `CompileError::TooManyLocals`.
    pub fn add_local(&mut self, qst: Qstr) -> Option<u16> {
        if let Some(slot) = self.find_local(qst) {
            return Some(slot);
        }
        if self.len >= MAX_LOCALS {
            return None;
        }
        let slot = self.len as u16;
        self.ids[self.len] = qst;
        self.len += 1;
        Some(slot)
    }
}
