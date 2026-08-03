//! objiter — forward iterator over list/tuple/range (position + source
//! object reference; no separate per-source-type cursor -- `next`
//! re-dispatches on the source's own type each call, same duck-typing
//! `vm.rs`'s `is_true`/subscript helpers already use for the other
//! containers). Backs `bc0::GET_ITER`/`bc0::FOR_ITER` (upstream
//! `mp_getiter`/`mp_iternext` slice).

use crate::upy::py::malloc;
use crate::upy::py::obj::{self, MpObj, MpObjBase};
use crate::upy::py::objects::{objlist, objrange, objtuple, TypeDesc, TYPE_ITER};

#[repr(C)]
pub struct Iter {
    pub base: MpObjBase,
    pub source: MpObj,
    pub pos: usize,
}

pub unsafe fn new(source: MpObj) -> MpObj {
    let p = malloc::m_malloc(core::mem::size_of::<Iter>()) as *mut Iter;
    if p.is_null() {
        return obj::OBJ_NULL;
    }
    (*p).base = MpObjBase::new((&TYPE_ITER as *const TypeDesc) as *const u8);
    (*p).source = source;
    (*p).pos = 0;
    p as MpObj
}

unsafe fn as_mut(o: MpObj) -> Option<*mut Iter> {
    if !obj::is_obj(o) {
        return None;
    }
    let p = o as *mut Iter;
    if (*p).base.type_ptr != (&TYPE_ITER as *const TypeDesc) as *const u8 {
        return None;
    }
    Some(p)
}

/// `None` if `o` isn't an [`Iter`] or its source isn't a type this VM
/// knows how to step (a real type error) -- `Some(None)` on ordinary
/// exhaustion (`StopIteration`); `Some(Some(item))` for the next value.
pub unsafe fn next(o: MpObj) -> Option<Option<MpObj>> {
    let p = as_mut(o)?;
    let src = (*p).source;
    let pos = (*p).pos;
    if let Some(n) = objlist::len(src) {
        if pos >= n {
            return Some(None);
        }
        (*p).pos += 1;
        return Some(objlist::get(src, pos));
    }
    if let Some(n) = objtuple::len(src) {
        if pos >= n {
            return Some(None);
        }
        (*p).pos += 1;
        return Some(objtuple::get(src, pos));
    }
    if let Some(n) = objrange::len(src) {
        if pos >= n {
            return Some(None);
        }
        (*p).pos += 1;
        return Some(objrange::get(src, pos).map(obj::new_small_int));
    }
    None
}

pub unsafe fn free(o: MpObj) {
    if as_mut(o).is_some() {
        malloc::m_free(o as *mut u8);
    }
}
