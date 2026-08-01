//! objstr — heap string (Metal alloc, NUL-terminated data).

use crate::upy::py::malloc;
use crate::upy::py::obj::{self, MpObj, MpObjBase};
use crate::upy::py::objects::{TypeDesc, TYPE_STR};
use crate::upy::py::qstr;

#[repr(C)]
pub struct Str {
    pub base: MpObjBase,
    pub hash: u16,
    pub len: usize,
    pub data: *mut u8,
}

pub unsafe fn new(bytes: &[u8]) -> MpObj {
    let p = malloc::m_malloc(core::mem::size_of::<Str>()) as *mut Str;
    if p.is_null() {
        return obj::OBJ_NULL;
    }
    let data = malloc::m_malloc(bytes.len() + 1);
    if data.is_null() {
        malloc::m_free(p as *mut u8);
        return obj::OBJ_NULL;
    }
    core::ptr::copy_nonoverlapping(bytes.as_ptr(), data, bytes.len());
    *data.add(bytes.len()) = 0;
    (*p).base = MpObjBase::new((&TYPE_STR as *const TypeDesc) as *const u8);
    (*p).hash = qstr::compute_hash(bytes);
    (*p).len = bytes.len();
    (*p).data = data;
    p as MpObj
}

pub unsafe fn as_bytes(o: MpObj) -> Option<&'static [u8]> {
    if !obj::is_obj(o) {
        return None;
    }
    let s = o as *const Str;
    if (*s).base.type_ptr != (&TYPE_STR as *const TypeDesc) as *const u8 {
        return None;
    }
    Some(core::slice::from_raw_parts((*s).data, (*s).len))
}

pub unsafe fn free(o: MpObj) {
    if let Some(_) = as_bytes(o) {
        let s = o as *mut Str;
        malloc::m_free((*s).data);
        malloc::m_free(s as *mut u8);
    }
}
