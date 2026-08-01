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
pub const STORE_SUBSCR: u8 = BASE_BYTE_O + 0x06;

pub const DUP_TOP: u8 = BASE_BYTE_O + 0x07;
pub const POP_TOP: u8 = BASE_BYTE_O + 0x09;
pub const ROT_TWO: u8 = BASE_BYTE_O + 0x0a;

pub const JUMP: u8 = BASE_JUMP_E + 0x02;
pub const POP_JUMP_IF_TRUE: u8 = BASE_JUMP_E + 0x03;
pub const POP_JUMP_IF_FALSE: u8 = BASE_JUMP_E + 0x04;

pub const BUILD_TUPLE: u8 = BASE_VINT_E + 0x0a;
pub const BUILD_LIST: u8 = BASE_VINT_E + 0x0b;
pub const BUILD_MAP: u8 = BASE_VINT_E + 0x0c;

pub const RETURN_VALUE: u8 = BASE_BYTE_E + 0x03;
pub const RAISE_OBJ: u8 = BASE_BYTE_E + 0x05;
pub const YIELD_VALUE: u8 = BASE_BYTE_E + 0x07;

pub const CALL_FUNCTION: u8 = BASE_VINT_O + 0x04;
pub const IMPORT_NAME: u8 = BASE_QSTR_O + 0x0b;
pub const IMPORT_STAR: u8 = BASE_BYTE_E + 0x09;

pub const LOAD_CONST_SMALL_INT_MULTI_NUM: usize = 64;
pub const LOAD_CONST_SMALL_INT_MULTI_EXCESS: isize = 16;
pub const LOAD_FAST_MULTI_NUM: usize = 16;
pub const STORE_FAST_MULTI_NUM: usize = 16;

/// Opcode format nibble table (upstream `MP_BC_FORMAT`).
pub fn format(op: u8) -> u8 {
    ((0x0000_03a4u32 >> (2 * (op >> 4))) & 3) as u8
}
