//! objbool — True / False (REPR_A immediates).

use crate::upy::py::obj::{self, MpObj};

#[inline]
pub fn get(v: bool) -> MpObj {
    let slot = if v { 1usize } else { 0 };
    (slot << 3) | 6
}

#[inline]
pub fn is_bool(o: MpObj) -> bool {
    obj::is_immediate(o) && matches!(o >> 3, 0 | 1)
}

#[inline]
pub fn value(o: MpObj) -> Option<bool> {
    if !is_bool(o) {
        return None;
    }
    Some((o >> 3) != 0)
}
