//! objarray — mutable bytearray (Metal alloc).

use crate::upy::py::malloc;
use crate::upy::py::obj::{self, MpObj, MpObjBase};
use crate::upy::py::objects::{TypeDesc, TYPE_BYTEARRAY};

#[repr(C)]
pub struct ByteArray {
    pub base: MpObjBase,
    pub len: usize,
    pub data: *mut u8,
}

pub unsafe fn new(n: usize) -> MpObj {
    let p = malloc::m_malloc(core::mem::size_of::<ByteArray>()) as *mut ByteArray;
    if p.is_null() {
        return obj::OBJ_NULL;
    }
    let data = if n == 0 {
        core::ptr::null_mut()
    } else {
        let d = malloc::m_malloc0(n);
        if d.is_null() {
            malloc::m_free(p as *mut u8);
            return obj::OBJ_NULL;
        }
        d
    };
    (*p).base = MpObjBase::new((&TYPE_BYTEARRAY as *const TypeDesc) as *const u8);
    (*p).len = n;
    (*p).data = data;
    p as MpObj
}

pub unsafe fn len(o: MpObj) -> Option<usize> {
    as_ref(o).map(|p| (*p).len)
}

pub unsafe fn get(o: MpObj, i: usize) -> Option<u8> {
    let p = as_ref(o)?;
    if i >= (*p).len {
        return None;
    }
    Some(*(*p).data.add(i))
}

pub unsafe fn set(o: MpObj, i: usize, v: u8) -> bool {
    let Some(p) = as_mut(o) else {
        return false;
    };
    if i >= (*p).len {
        return false;
    }
    *(*p).data.add(i) = v;
    true
}

unsafe fn as_ref(o: MpObj) -> Option<*const ByteArray> {
    if !obj::is_obj(o) {
        return None;
    }
    let p = o as *const ByteArray;
    if (*p).base.type_ptr != (&TYPE_BYTEARRAY as *const TypeDesc) as *const u8 {
        return None;
    }
    Some(p)
}

unsafe fn as_mut(o: MpObj) -> Option<*mut ByteArray> {
    as_ref(o).map(|p| p as *mut ByteArray)
}

pub unsafe fn free(o: MpObj) {
    if let Some(p) = as_ref(o) {
        let p = p as *mut ByteArray;
        if !(*p).data.is_null() {
            malloc::m_free((*p).data);
        }
        malloc::m_free(p as *mut u8);
    }
}
