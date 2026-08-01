//! objlist — growable object list (Metal alloc).

use crate::upy::py::malloc;
use crate::upy::py::obj::{self, MpObj, MpObjBase};
use crate::upy::py::objects::{TypeDesc, TYPE_LIST};

#[repr(C)]
pub struct List {
    pub base: MpObjBase,
    pub alloc: usize,
    pub len: usize,
    pub items: *mut MpObj,
}

pub unsafe fn new(n: usize) -> MpObj {
    let p = malloc::m_malloc(core::mem::size_of::<List>()) as *mut List;
    if p.is_null() {
        return obj::OBJ_NULL;
    }
    let cap = if n == 0 { 4 } else { n };
    let items = malloc::m_malloc0(cap * core::mem::size_of::<MpObj>()) as *mut MpObj;
    if items.is_null() {
        malloc::m_free(p as *mut u8);
        return obj::OBJ_NULL;
    }
    (*p).base = MpObjBase::new((&TYPE_LIST as *const TypeDesc) as *const u8);
    (*p).alloc = cap;
    (*p).len = 0;
    (*p).items = items;
    p as MpObj
}

unsafe fn as_mut(o: MpObj) -> Option<*mut List> {
    if !obj::is_obj(o) {
        return None;
    }
    let p = o as *mut List;
    if (*p).base.type_ptr != (&TYPE_LIST as *const TypeDesc) as *const u8 {
        return None;
    }
    Some(p)
}

pub unsafe fn len(o: MpObj) -> Option<usize> {
    as_mut(o).map(|p| (*p).len)
}

pub unsafe fn append(o: MpObj, item: MpObj) -> bool {
    let Some(p) = as_mut(o) else {
        return false;
    };
    if (*p).len >= (*p).alloc {
        let ncap = (*p).alloc * 2;
        let nbytes = ncap * core::mem::size_of::<MpObj>();
        let ni = malloc::m_realloc((*p).items as *mut u8, nbytes) as *mut MpObj;
        if ni.is_null() {
            return false;
        }
        (*p).items = ni;
        (*p).alloc = ncap;
    }
    *(*p).items.add((*p).len) = item;
    (*p).len += 1;
    true
}

pub unsafe fn get(o: MpObj, i: usize) -> Option<MpObj> {
    let p = as_mut(o)?;
    if i >= (*p).len {
        return None;
    }
    Some(*(*p).items.add(i))
}

pub unsafe fn set(o: MpObj, i: usize, item: MpObj) -> bool {
    let Some(p) = as_mut(o) else {
        return false;
    };
    if i >= (*p).len {
        return false;
    }
    *(*p).items.add(i) = item;
    true
}

pub unsafe fn pop(o: MpObj) -> Option<MpObj> {
    let p = as_mut(o)?;
    if (*p).len == 0 {
        return None;
    }
    (*p).len -= 1;
    Some(*(*p).items.add((*p).len))
}

pub unsafe fn free(o: MpObj) {
    if let Some(p) = as_mut(o) {
        malloc::m_free((*p).items as *mut u8);
        malloc::m_free(p as *mut u8);
    }
}
