//! obj — MicroPython object tagging (OBJ_REPR_A on `usize`).

use super::qstrdefs::Qstr;

/// Opaque object word (REPR_A).
pub type MpObj = usize;
pub type MpConstObj = usize;

pub const OBJ_NULL: MpObj = 0;
pub const OBJ_STOP_ITERATION: MpObj = 0;
pub const OBJ_SENTINEL: MpObj = 4;

#[inline]
pub const fn is_small_int(o: MpConstObj) -> bool {
    (o & 1) != 0
}

#[inline]
pub const fn small_int_value(o: MpConstObj) -> isize {
    (o as isize) >> 1
}

#[inline]
pub const fn new_small_int(v: isize) -> MpObj {
    ((v as usize) << 1) | 1
}

#[inline]
pub const fn is_qstr(o: MpConstObj) -> bool {
    (o & 7) == 2
}

#[inline]
pub const fn qstr_value(o: MpConstObj) -> Qstr {
    o >> 3
}

#[inline]
pub const fn new_qstr(q: Qstr) -> MpObj {
    (q << 3) | 2
}

#[inline]
pub const fn is_immediate(o: MpConstObj) -> bool {
    (o & 7) == 6
}

#[inline]
pub const fn is_obj(o: MpConstObj) -> bool {
    (o & 3) == 0 && o != 0
}

#[inline]
pub fn from_ptr(p: *const u8) -> MpObj {
    p as usize
}

#[inline]
pub fn to_ptr(o: MpConstObj) -> *const u8 {
    o as *const u8
}

/// Concrete heap object header (first field of every concrete type).
#[repr(C)]
pub struct MpObjBase {
    pub type_ptr: *const u8,
}

impl MpObjBase {
    pub const fn new(type_ptr: *const u8) -> Self {
        Self { type_ptr }
    }
}
