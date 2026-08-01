//! objtype — type descriptor object (wraps TypeDesc pointer as MpObj).

use crate::upy::py::obj::{self, MpObj};
use crate::upy::py::objects::{TypeDesc, TypeKind};
use crate::upy::py::qstrdefs::Qstr;

/// Types are immortal statics for B2 — return pointer tagged as heap obj word.
pub fn as_obj(td: &'static TypeDesc) -> MpObj {
    // Pointers are 8-byte aligned on x86_64; low bits clear => is_obj.
    td as *const TypeDesc as MpObj
}

pub fn name(o: MpObj) -> Option<Qstr> {
    if !obj::is_obj(o) {
        return None;
    }
    let td = o as *const TypeDesc;
    unsafe { Some((*td).name) }
}

pub fn kind(o: MpObj) -> Option<TypeKind> {
    if !obj::is_obj(o) {
        return None;
    }
    let td = o as *const TypeDesc;
    unsafe { Some((*td).kind) }
}
