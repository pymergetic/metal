//! objset — small set of MpObj (linear, Metal alloc).

use crate::upy::py::malloc;
use crate::upy::py::obj::{self, MpObj, MpObjBase};
use crate::upy::py::objects::{obj_eq, TypeDesc, TYPE_SET};

#[repr(C)]
pub struct Set {
    pub base: MpObjBase,
    pub alloc: usize,
    pub used: usize,
    pub items: *mut MpObj,
}

pub unsafe fn new(cap: usize) -> MpObj {
    let cap = if cap < 4 { 4 } else { cap };
    let p = malloc::m_malloc(core::mem::size_of::<Set>()) as *mut Set;
    if p.is_null() {
        return obj::OBJ_NULL;
    }
    let items = malloc::m_malloc0(cap * core::mem::size_of::<MpObj>()) as *mut MpObj;
    if items.is_null() {
        malloc::m_free(p as *mut u8);
        return obj::OBJ_NULL;
    }
    (*p).base = MpObjBase::new((&TYPE_SET as *const TypeDesc) as *const u8);
    (*p).alloc = cap;
    (*p).used = 0;
    (*p).items = items;
    p as MpObj
}

unsafe fn as_mut(o: MpObj) -> Option<*mut Set> {
    if !obj::is_obj(o) {
        return None;
    }
    let p = o as *mut Set;
    if (*p).base.type_ptr != (&TYPE_SET as *const TypeDesc) as *const u8 {
        return None;
    }
    Some(p)
}

pub unsafe fn len(o: MpObj) -> Option<usize> {
    as_mut(o).map(|p| (*p).used)
}

pub unsafe fn contains(o: MpObj, item: MpObj) -> bool {
    let Some(p) = as_mut(o) else {
        return false;
    };
    for i in 0..(*p).alloc {
        let v = *(*p).items.add(i);
        if v != obj::OBJ_NULL && obj_eq(v, item) {
            return true;
        }
    }
    false
}

pub unsafe fn add(o: MpObj, item: MpObj) -> bool {
    if item == obj::OBJ_NULL {
        return false;
    }
    if contains(o, item) {
        return true;
    }
    let Some(p) = as_mut(o) else {
        return false;
    };
    for i in 0..(*p).alloc {
        if *(*p).items.add(i) == obj::OBJ_NULL {
            *(*p).items.add(i) = item;
            (*p).used += 1;
            return true;
        }
    }
    false
}

pub unsafe fn free(o: MpObj) {
    if let Some(p) = as_mut(o) {
        malloc::m_free((*p).items as *mut u8);
        malloc::m_free(p as *mut u8);
    }
}
