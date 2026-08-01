//! objint — small ints via REPR_A; heap mpz-ish later.

use crate::upy::py::obj::{self, MpObj};

#[inline]
pub fn from_isize(v: isize) -> MpObj {
    obj::new_small_int(v)
}

#[inline]
pub fn as_isize(o: MpObj) -> Option<isize> {
    if obj::is_small_int(o) {
        Some(obj::small_int_value(o))
    } else {
        None
    }
}

#[inline]
pub fn is_int(o: MpObj) -> bool {
    obj::is_small_int(o)
}
