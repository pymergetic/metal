//! objslice — slice(start, stop, step); None fields as OBJ_NULL.

use crate::upy::py::malloc;
use crate::upy::py::obj::{self, MpObj, MpObjBase};
use crate::upy::py::objects::{TypeDesc, TYPE_SLICE};

#[repr(C)]
pub struct Slice {
    pub base: MpObjBase,
    pub start: MpObj,
    pub stop: MpObj,
    pub step: MpObj,
}

pub unsafe fn new(start: MpObj, stop: MpObj, step: MpObj) -> MpObj {
    let p = malloc::m_malloc(core::mem::size_of::<Slice>()) as *mut Slice;
    if p.is_null() {
        return obj::OBJ_NULL;
    }
    (*p).base = MpObjBase::new((&TYPE_SLICE as *const TypeDesc) as *const u8);
    (*p).start = start;
    (*p).stop = stop;
    (*p).step = step;
    p as MpObj
}

pub unsafe fn parts(o: MpObj) -> Option<(MpObj, MpObj, MpObj)> {
    let p = as_ref(o)?;
    Some(((*p).start, (*p).stop, (*p).step))
}

unsafe fn as_ref(o: MpObj) -> Option<*const Slice> {
    if !obj::is_obj(o) {
        return None;
    }
    let p = o as *const Slice;
    if (*p).base.type_ptr != (&TYPE_SLICE as *const TypeDesc) as *const u8 {
        return None;
    }
    Some(p)
}

pub unsafe fn free(o: MpObj) {
    if as_ref(o).is_some() {
        malloc::m_free(o as *mut u8);
    }
}
