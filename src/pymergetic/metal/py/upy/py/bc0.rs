//! bc0 — bytecode opcode constants (upstream `bc0.h`).

pub const MASK_FORMAT: u8 = 0xf0;
pub const MASK_EXTRA_BYTE: u8 = 0x9e;

pub const FORMAT_BYTE: u8 = 0;
pub const FORMAT_QSTR: u8 = 1;
pub const FORMAT_VAR_UINT: u8 = 2;
pub const FORMAT_OFFSET: u8 = 3;

pub const BASE_RESERVED: u8 = 0x00;
pub const BASE_QSTR_O: u8 = 0x10;
pub const BASE_VINT_E: u8 = 0x20;
pub const BASE_VINT_O: u8 = 0x30;
pub const BASE_JUMP_E: u8 = 0x40;
pub const BASE_BYTE_O: u8 = 0x50;
pub const BASE_BYTE_E: u8 = 0x60;
pub const LOAD_CONST_SMALL_INT_MULTI: u8 = 0x70;
pub const LOAD_FAST_MULTI: u8 = 0xb0;
pub const STORE_FAST_MULTI: u8 = 0xc0;
pub const UNARY_OP_MULTI: u8 = 0xd0;
pub const BINARY_OP_MULTI: u8 = 0xd7;

pub const LOAD_CONST_FALSE: u8 = BASE_BYTE_O + 0x00;
pub const LOAD_CONST_NONE: u8 = BASE_BYTE_O + 0x01;
pub const LOAD_CONST_TRUE: u8 = BASE_BYTE_O + 0x02;
pub const LOAD_CONST_SMALL_INT: u8 = BASE_VINT_E + 0x02;
pub const LOAD_CONST_STRING: u8 = BASE_QSTR_O + 0x00;
pub const LOAD_CONST_OBJ: u8 = BASE_VINT_E + 0x03;
pub const LOAD_NULL: u8 = BASE_BYTE_O + 0x03;

pub const LOAD_FAST_N: u8 = BASE_VINT_E + 0x04;
pub const LOAD_NAME: u8 = BASE_QSTR_O + 0x01;
pub const LOAD_GLOBAL: u8 = BASE_QSTR_O + 0x02;
pub const LOAD_ATTR: u8 = BASE_QSTR_O + 0x03;

pub const STORE_FAST_N: u8 = BASE_VINT_E + 0x06;
pub const STORE_NAME: u8 = BASE_QSTR_O + 0x06;
pub const STORE_GLOBAL: u8 = BASE_QSTR_O + 0x07;
pub const STORE_ATTR: u8 = BASE_QSTR_O + 0x08;
pub const LOAD_SUBSCR: u8 = BASE_BYTE_O + 0x05;
pub const STORE_SUBSCR: u8 = BASE_BYTE_O + 0x06;

pub const DUP_TOP: u8 = BASE_BYTE_O + 0x07;
/// Metal extension (free `BASE_BYTE_O` slot 0x08): dup the top *two*
/// stack items preserving order (`[a, b]` -> `[a, b, a, b]`) -- needed by
/// augmented subscript/attr assignment (`x[i] += v`, `obj.attr += v`) to
/// evaluate the container/index or object exactly once while still being
/// able to both load the current value and store the new one. Upstream
/// MicroPython doesn't need this (its `STORE_SUBSCR`/binary-op sequencing
/// differs); no upstream mirror.
pub const DUP_TOP_TWO: u8 = BASE_BYTE_O + 0x08;
pub const POP_TOP: u8 = BASE_BYTE_O + 0x09;
pub const ROT_TWO: u8 = BASE_BYTE_O + 0x0a;
/// Metal extension (free `BASE_BYTE_O` slot 0x0b): pop an iterable, push
/// a fresh iterator heap object over it (upstream `MP_BC_GET_ITER`, kept
/// non-stackless since this VM has no generator/`yield`-inside-`for`
/// support to preserve across suspension).
pub const GET_ITER: u8 = BASE_BYTE_O + 0x0b;
/// Metal extension (free `BASE_BYTE_O` slot 0x0c): pop `value` then
/// `list`, append `value` to `list`, push nothing back (upstream's own
/// `LIST_APPEND` instead re-peeks the list at a caller-supplied stack
/// depth and leaves it in place -- this mirror's `compile.rs` lowering
/// always keeps the in-progress list under a real `STORE_NAME`/
/// `STORE_FAST` name, not on the value stack, so there is nothing to
/// leave behind: the name binding is the only live reference needed for
/// the next loop iteration or the comprehension's own trailing load).
pub const LIST_APPEND: u8 = BASE_BYTE_O + 0x0c;

pub const JUMP: u8 = BASE_JUMP_E + 0x02;
pub const POP_JUMP_IF_TRUE: u8 = BASE_JUMP_E + 0x03;
pub const POP_JUMP_IF_FALSE: u8 = BASE_JUMP_E + 0x04;
/// Metal extension (free `BASE_JUMP_E` slot 0x00): peek the iterator TOS,
/// push its next value and fall through, or (exhausted) pop the iterator
/// and jump to the loop-exit offset (upstream `MP_BC_FOR_ITER`).
pub const FOR_ITER: u8 = BASE_JUMP_E + 0x00;
/// Metal extension (free `BASE_JUMP_E` slot 0x01): push a new exception
/// handler frame (jump target = the `except`-chain dispatch point,
/// recorded stack depth = current `sp`) onto `CodeState`'s handler stack
/// (upstream `MP_BC_SETUP_EXCEPT`, no `SETUP_FINALLY`/`SETUP_WITH`
/// distinction -- `compile.rs` uses the same opcode for a `finally`-only
/// wrapper, see its module doc).
pub const SETUP_EXCEPT: u8 = BASE_JUMP_E + 0x01;

pub const BUILD_TUPLE: u8 = BASE_VINT_E + 0x0a;
pub const BUILD_LIST: u8 = BASE_VINT_E + 0x0b;
pub const BUILD_MAP: u8 = BASE_VINT_E + 0x0c;
/// Metal extension (free `BASE_VINT_E` slot 0x0d): pop `n` stack items,
/// build a `set`, push it (upstream `MP_BC_BUILD_SET`).
pub const BUILD_SET: u8 = BASE_VINT_E + 0x0d;

pub const RETURN_VALUE: u8 = BASE_BYTE_E + 0x03;
pub const RAISE_OBJ: u8 = BASE_BYTE_E + 0x05;
pub const YIELD_VALUE: u8 = BASE_BYTE_E + 0x07;
/// Metal extension (free `BASE_BYTE_E` slot 0x00): pop the top exception
/// handler frame pushed by [`SETUP_EXCEPT`] without touching the value
/// stack (upstream `MP_BC_POP_EXCEPT_JUMP`'s non-jumping half -- this
/// mirror always follows it with an explicit `JUMP` when a skip is
/// needed, so no offset operand is carried on the opcode itself).
pub const POP_EXCEPT: u8 = BASE_BYTE_E + 0x00;

pub const CALL_FUNCTION: u8 = BASE_VINT_O + 0x04;
pub const IMPORT_NAME: u8 = BASE_QSTR_O + 0x0b;
/// `fromlist` attr load: peek module TOS, push `getattr(module, qstr)`
/// without popping the module (upstream `MP_BC_IMPORT_FROM`).
pub const IMPORT_FROM: u8 = BASE_QSTR_O + 0x0c;
pub const IMPORT_STAR: u8 = BASE_BYTE_E + 0x09;
/// Metal extension (free `BASE_QSTR_O` slot 0x0d): push an immediate
/// qstr-tagged value (`obj::new_qstr(qst)`, no heap allocation) -- used
/// for keyword-call argument keys (`f(name=value)`) and `except Name:`
/// clause type markers, where the qstr identifies a *name*, not a
/// `str` value (contrast [`LOAD_CONST_STRING`], which allocates a real
/// heap `str`).
pub const LOAD_CONST_QSTR: u8 = BASE_QSTR_O + 0x0d;

pub const LOAD_CONST_SMALL_INT_MULTI_NUM: usize = 64;
pub const LOAD_CONST_SMALL_INT_MULTI_EXCESS: isize = 16;
pub const LOAD_FAST_MULTI_NUM: usize = 16;
pub const STORE_FAST_MULTI_NUM: usize = 16;

/// Opcode format nibble table (upstream `MP_BC_FORMAT`).
pub fn format(op: u8) -> u8 {
    ((0x0000_03a4u32 >> (2 * (op >> 4))) & 3) as u8
}
