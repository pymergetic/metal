//! Minimal static qstr strings (stand-in until genhdr / makeqstrdata lands).

pub type Qstr = usize;

pub const QSTR_NULL: Qstr = 0;
pub const QSTR_NAME: Qstr = 1;
pub const QSTR_INIT: Qstr = 2;
pub const QSTR_MAIN: Qstr = 3;
pub const QSTR_TRUE: Qstr = 4;
pub const QSTR_FALSE: Qstr = 5;
pub const QSTR_NONE: Qstr = 6;
pub const QSTR_OBJECT: Qstr = 7;
pub const QSTR_TYPE: Qstr = 8;
pub const QSTR_INT: Qstr = 9;
pub const QSTR_STR: Qstr = 10;
pub const QSTR_LIST: Qstr = 11;
pub const QSTR_DICT: Qstr = 12;
pub const QSTR_TUPLE: Qstr = 13;
pub const QSTR_EXCEPTION: Qstr = 14;
pub const QSTR_BOOL: Qstr = 15;
pub const QSTR_FUNCTION: Qstr = 16;
pub const QSTR_MODULE: Qstr = 17;
pub const QSTR_FLOAT: Qstr = 18;
pub const QSTR_SET: Qstr = 19;
pub const QSTR_RANGE: Qstr = 20;
pub const QSTR_SLICE: Qstr = 21;
pub const QSTR_CELL: Qstr = 22;
pub const QSTR_BYTEARRAY: Qstr = 23;
pub const QSTR_DEQUE: Qstr = 24;

pub static STATIC_STRS: &[&[u8]] = &[
    b"",
    b"__name__",
    b"__init__",
    b"__main__",
    b"True",
    b"False",
    b"None",
    b"object",
    b"type",
    b"int",
    b"str",
    b"list",
    b"dict",
    b"tuple",
    b"Exception",
    b"bool",
    b"function",
    b"module",
    b"float",
    b"set",
    b"range",
    b"slice",
    b"cell",
    b"bytearray",
    b"deque",
];

pub fn static_count() -> usize {
    STATIC_STRS.len()
}
