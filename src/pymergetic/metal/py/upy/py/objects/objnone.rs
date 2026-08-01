//! objnone — singleton None (REPR_A immediate).

use crate::upy::py::obj::{self, MpObj};

#[inline]
pub fn get() -> MpObj {
    (2usize << 3) | 6
}

#[inline]
pub fn is_none(o: MpObj) -> bool {
    obj::is_immediate(o) && (o >> 3) == 2
}
