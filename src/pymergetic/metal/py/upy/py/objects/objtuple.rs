//! objtuple — fixed-length object tuple (Metal alloc).

use crate::upy::py::malloc;
use crate::upy::py::obj::{self, MpObj, MpObjBase};
use crate::upy::py::objects::{TypeDesc, TYPE_TUPLE};

#[repr(C)]
pub struct Tuple {
    pub base: MpObjBase,
    pub len: usize,
    pub items: *mut MpObj,
}

pub unsafe fn new(items: &[MpObj]) -> MpObj {
    let p = malloc::m_malloc(core::mem::size_of::<Tuple>()) as *mut Tuple;
    if p.is_null() {
        return obj::OBJ_NULL;
    }
    let buf = if items.is_empty() {
        core::ptr::null_mut()
    } else {
        let b = malloc::m_malloc(items.len() * core::mem::size_of::<MpObj>()) as *mut MpObj;
        if b.is_null() {
            malloc::m_free(p as *mut u8);
            return obj::OBJ_NULL;
        }
        core::ptr::copy_nonoverlapping(items.as_ptr(), b, items.len());
        b
    };
    (*p).base = MpObjBase::new((&TYPE_TUPLE as *const TypeDesc) as *const u8);
    (*p).len = items.len();
    (*p).items = buf;
    p as MpObj
}

pub unsafe fn len(o: MpObj) -> Option<usize> {
    as_ref(o).map(|t| (*t).len)
}

pub unsafe fn get(o: MpObj, i: usize) -> Option<MpObj> {
    let t = as_ref(o)?;
    if i >= (*t).len {
        return None;
    }
    Some(*(*t).items.add(i))
}

unsafe fn as_ref(o: MpObj) -> Option<*const Tuple> {
    if !obj::is_obj(o) {
        return None;
    }
    let t = o as *const Tuple;
    if (*t).base.type_ptr != (&TYPE_TUPLE as *const TypeDesc) as *const u8 {
        return None;
    }
    Some(t)
}

pub unsafe fn free(o: MpObj) {
    if let Some(t) = as_ref(o) {
        let t = t as *mut Tuple;
        if !(*t).items.is_null() {
            malloc::m_free((*t).items as *mut u8);
        }
        malloc::m_free(t as *mut u8);
    }
}
