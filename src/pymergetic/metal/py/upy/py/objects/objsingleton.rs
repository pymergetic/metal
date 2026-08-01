//! objsingleton — named immortal singleton (e.g. Ellipsis / NotImplemented).

use crate::upy::py::obj::MpObj;
use crate::upy::py::qstrdefs::Qstr;

#[repr(C)]
pub struct Singleton {
    pub name: Qstr,
    pub word: MpObj,
}

/// Build a REPR_A immediate-style unique word from a small id (tag 6, slot>=8).
pub const fn make(id: usize) -> MpObj {
    ((id + 8) << 3) | 6
}

pub const ELLIPSIS: MpObj = make(0);
pub const NOT_IMPLEMENTED: MpObj = make(1);

pub fn is_ellipsis(o: MpObj) -> bool {
    o == ELLIPSIS
}

pub fn is_not_implemented(o: MpObj) -> bool {
    o == NOT_IMPLEMENTED
}
