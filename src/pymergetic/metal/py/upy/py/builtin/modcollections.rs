//! modcollections — deque as ring buffer of MpObj (Metal alloc).

use crate::upy::py::malloc;
use crate::upy::py::obj::{self, MpObj, MpObjBase};
use crate::upy::py::objects::{TypeDesc, TypeKind};
use crate::upy::py::qstrdefs;

/// Deque type lives under collections for B4 (not a core TypeKind slot reuse).
pub static TYPE_DEQUE: TypeDesc = TypeDesc {
    kind: TypeKind::Deque,
    name: qstrdefs::QSTR_DEQUE,
};

#[repr(C)]
pub struct Deque {
    pub base: MpObjBase,
    pub alloc: usize,
    pub len: usize,
    pub head: usize,
    pub items: *mut MpObj,
}

pub unsafe fn deque_new(cap: usize) -> MpObj {
    let cap = if cap < 4 { 4 } else { cap };
    let p = malloc::m_malloc(core::mem::size_of::<Deque>()) as *mut Deque;
    if p.is_null() {
        return obj::OBJ_NULL;
    }
    let items = malloc::m_malloc0(cap * core::mem::size_of::<MpObj>()) as *mut MpObj;
    if items.is_null() {
        malloc::m_free(p as *mut u8);
        return obj::OBJ_NULL;
    }
    (*p).base = MpObjBase::new((&TYPE_DEQUE as *const TypeDesc) as *const u8);
    (*p).alloc = cap;
    (*p).len = 0;
    (*p).head = 0;
    (*p).items = items;
    p as MpObj
}

unsafe fn as_mut(o: MpObj) -> Option<*mut Deque> {
    if !obj::is_obj(o) {
        return None;
    }
    let p = o as *mut Deque;
    if (*p).base.type_ptr != (&TYPE_DEQUE as *const TypeDesc) as *const u8 {
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
        return false;
    }
    let i = ((*p).head + (*p).len) % (*p).alloc;
    *(*p).items.add(i) = item;
    (*p).len += 1;
    true
}

pub unsafe fn popleft(o: MpObj) -> Option<MpObj> {
    let p = as_mut(o)?;
    if (*p).len == 0 {
        return None;
    }
    let v = *(*p).items.add((*p).head);
    (*p).head = ((*p).head + 1) % (*p).alloc;
    (*p).len -= 1;
    Some(v)
}

pub unsafe fn free(o: MpObj) {
    if let Some(p) = as_mut(o) {
        malloc::m_free((*p).items as *mut u8);
        malloc::m_free(p as *mut u8);
    }
}
