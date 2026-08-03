//! emit — backend-agnostic emitter interface (upstream `py/emit.h` mirror
//! slice: `mp_emit_method_table_t`).
//!
//! Upstream keeps `compile.c` backend-agnostic: it calls through
//! `comp->emit_method_table` so the same compiler pass can target either
//! `emitbc.c` (bytecode) or `emitnative.c` (native/viper). Metal has only
//! one backend today (`emitbc.rs`), but `compile.rs` is still written
//! generically over this trait — the same real reason upstream keeps the
//! indirection: a future native emitter implements `Emit` too, without
//! touching `compile.rs`.
//!
//! Only the operations this compiler's supported subset actually needs
//! are here (see `compile.rs` module doc for the full list of omissions).

use crate::upy::py::obj::MpObj;
use crate::upy::py::qstrdefs::Qstr;

/// Placeholder for a relative jump offset (two bytes at [`JumpHole::offset_at`]).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct JumpHole {
    pub offset_at: usize,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EmitError {
    OutOfMemory,
}

pub trait Emit {
    fn load_const_none(&mut self) -> Result<(), EmitError>;
    fn load_const_true(&mut self) -> Result<(), EmitError>;
    fn load_const_false(&mut self) -> Result<(), EmitError>;
    /// `v` must fit in a small int (upstream `MP_SMALL_INT_FITS`) — the
    /// caller (`compile.rs`) is the one that knows the source int literal
    /// already round-tripped through that check at parse time.
    fn load_const_small_int(&mut self, v: isize) -> Result<(), EmitError>;

    /// Module-scope name load/store against the runtime globals dict
    /// (upstream `LOAD_NAME`/`STORE_NAME`).
    fn load_name(&mut self, qst: Qstr) -> Result<(), EmitError>;
    fn store_name(&mut self, qst: Qstr) -> Result<(), EmitError>;

    /// Function-scope local load/store by slot (upstream
    /// `LOAD_FAST_N`/`STORE_FAST_N`, `LOAD_FAST_MULTI`/`STORE_FAST_MULTI`
    /// range only — see `scope.rs` module doc on `MAX_LOCALS`).
    fn load_fast(&mut self, slot: u16) -> Result<(), EmitError>;
    fn store_fast(&mut self, slot: u16) -> Result<(), EmitError>;

    /// `op` is one of `emitcommon::UNARY_OP_*`.
    fn unary_op(&mut self, op: u8) -> Result<(), EmitError>;
    /// `op` is one of `emitcommon::BINARY_OP_*`.
    fn binary_op(&mut self, op: u8) -> Result<(), EmitError>;

    fn pop_top(&mut self) -> Result<(), EmitError>;
    fn return_value(&mut self) -> Result<(), EmitError>;

    /// Push a real heap `str` object built from qstr `qst`'s bytes
    /// (upstream `LOAD_CONST_STRING`) -- unlike every other `load_const_*`
    /// method here, this allocates each time the instruction runs (a
    /// string is a real heap object, not an immediate).
    fn load_const_string(&mut self, qst: Qstr) -> Result<(), EmitError>;

    /// Attribute load off TOS (upstream `LOAD_ATTR`): pops the object,
    /// pushes `obj.<qst>` -- module attribute lookup only for now (see
    /// `vm.rs`).
    fn load_attr(&mut self, qst: Qstr) -> Result<(), EmitError>;

    /// Positional-only call (upstream `CALL_FUNCTION` with `n_kw` fixed at
    /// `0` -- no keyword-argument support in this compiler slice). Pops
    /// `n_pos` args (already pushed left-to-right) plus the callee below
    /// them, pushes the call's result.
    fn call_function(&mut self, n_pos: u16) -> Result<(), EmitError>;

    /// `import <dotted name>` (upstream `IMPORT_NAME`): pops the fromlist
    /// TOS (this compiler always pushes `None` first for plain `import`),
    /// pushes the imported module.
    fn import_name(&mut self, qst: Qstr) -> Result<(), EmitError>;

    /// `from ... import name` attr load (upstream `IMPORT_FROM`): module
    /// stays on the stack; pushes `getattr(module, qst)`.
    fn import_from(&mut self, qst: Qstr) -> Result<(), EmitError>;

    /// Current bytecode length (next instruction would be emitted here).
    fn here(&self) -> usize;

    /// Emit `JUMP` + a patchable 2-byte relative offset placeholder.
    fn jump(&mut self) -> Result<JumpHole, EmitError>;

    /// Emit `POP_JUMP_IF_FALSE` + patchable offset placeholder.
    fn pop_jump_if_false(&mut self) -> Result<JumpHole, EmitError>;

    /// Emit `POP_JUMP_IF_TRUE` + patchable offset placeholder.
    fn pop_jump_if_true(&mut self) -> Result<JumpHole, EmitError>;

    /// Patch a prior jump hole to branch to `target` (`here()` index).
    fn patch_jump(&mut self, hole: JumpHole, target: usize) -> Result<(), EmitError>;

    /// Pop `n` stack items, build list, push it (upstream `BUILD_LIST`).
    fn build_list(&mut self, n: u16) -> Result<(), EmitError>;

    /// Pop `n` stack items, build tuple, push it (upstream `BUILD_TUPLE`).
    fn build_tuple(&mut self, n: u16) -> Result<(), EmitError>;

    /// Subscript load: pop index then container, push `container[index]`.
    fn load_subscr(&mut self) -> Result<(), EmitError>;

    /// Subscript store: pop value, index, container.
    fn store_subscr(&mut self) -> Result<(), EmitError>;

    /// Intern a heap object in this code's const table and emit
    /// `LOAD_CONST_OBJ` (for `def` bodies stored via `STORE_NAME`).
    fn load_const_obj_value(&mut self, obj: MpObj) -> Result<(), EmitError>;

    /// Attribute store (upstream `STORE_ATTR`): pops the object then the
    /// value (source order `<expr>.<qst> = value` pushes value then
    /// object... see `emitbc.rs`/`vm.rs` for the exact pop order).
    fn store_attr(&mut self, qst: Qstr) -> Result<(), EmitError>;

    /// Duplicate the top stack item (upstream `DUP_TOP`) -- used by
    /// short-circuit `and`/`or` to keep the left operand available both
    /// for the truth test and as the expression's own result.
    fn dup_top(&mut self) -> Result<(), EmitError>;

    /// Duplicate the top *two* stack items preserving order (Metal
    /// extension, see `bc0::DUP_TOP_TWO` doc) -- used by augmented
    /// subscript assignment (`x[i] += v`) to evaluate the container/
    /// index once while still loading the current value and storing the
    /// new one.
    fn dup_top_two(&mut self) -> Result<(), EmitError>;

    /// Swap the top two stack items (upstream `ROT_TWO`).
    fn rot_two(&mut self) -> Result<(), EmitError>;

    /// Pop `2*n` stack items (key, value pairs, in source order), build a
    /// `dict`, push it (upstream `BUILD_MAP`).
    fn build_map(&mut self, n: u16) -> Result<(), EmitError>;

    /// Pop `n` stack items, build a `set`, push it (upstream
    /// `BUILD_SET`).
    fn build_set(&mut self, n: u16) -> Result<(), EmitError>;

    /// Pop an iterable, push a fresh iterator heap object over it
    /// (upstream `GET_ITER`).
    fn get_iter(&mut self) -> Result<(), EmitError>;

    /// Emit `FOR_ITER` + a patchable relative-offset placeholder
    /// (upstream `FOR_ITER`): peeks the iterator TOS, pushes its next
    /// value and falls through, or (exhausted) pops the iterator and
    /// jumps to the patched target.
    fn for_iter(&mut self) -> Result<JumpHole, EmitError>;

    /// Push a new exception-handler frame (upstream `SETUP_EXCEPT`) with
    /// a patchable jump target for the `except`-chain dispatch point.
    fn setup_except(&mut self) -> Result<JumpHole, EmitError>;

    /// Pop the top exception-handler frame without touching the value
    /// stack (upstream `POP_EXCEPT`'s non-jumping half -- see
    /// `bc0::POP_EXCEPT` doc).
    fn pop_except(&mut self) -> Result<(), EmitError>;

    /// Pop TOS, raise it as the active exception (upstream `RAISE_OBJ`).
    fn raise_obj(&mut self) -> Result<(), EmitError>;

    /// Push an immediate qstr-tagged value (upstream `LOAD_CONST_QSTR`,
    /// see `bc0.rs` doc) -- used for `except Name:` clause type markers
    /// (compared by [`EXCEPTION_MATCH`](crate::upy::py::emitcommon::BINARY_OP_EXCEPTION_MATCH),
    /// never as a `str` value).
    fn load_const_qstr(&mut self, qst: Qstr) -> Result<(), EmitError>;

    /// Pop `value` then `list`, append `value` to `list`, push nothing
    /// back (see `bc0::LIST_APPEND` doc) -- used by list-comprehension
    /// lowering.
    fn list_append(&mut self) -> Result<(), EmitError>;
}
