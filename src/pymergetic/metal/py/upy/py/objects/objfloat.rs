//! objfloat — heap float64 (Metal alloc).

use crate::upy::py::malloc;
use crate::upy::py::obj::{self, MpObj, MpObjBase};
use crate::upy::py::objects::{TypeDesc, TYPE_FLOAT};

#[repr(C)]
pub struct Float {
    pub base: MpObjBase,
    pub value: f64,
}

pub unsafe fn new(v: f64) -> MpObj {
    let p = malloc::m_malloc(core::mem::size_of::<Float>()) as *mut Float;
    if p.is_null() {
        return obj::OBJ_NULL;
    }
    (*p).base = MpObjBase::new((&TYPE_FLOAT as *const TypeDesc) as *const u8);
    (*p).value = v;
    p as MpObj
}

pub unsafe fn value(o: MpObj) -> Option<f64> {
    let p = as_ref(o)?;
    Some((*p).value)
}

unsafe fn as_ref(o: MpObj) -> Option<*const Float> {
    if !obj::is_obj(o) {
        return None;
    }
    let p = o as *const Float;
    if (*p).base.type_ptr != (&TYPE_FLOAT as *const TypeDesc) as *const u8 {
        return None;
    }
    Some(p)
}

pub unsafe fn free(o: MpObj) {
    if as_ref(o).is_some() {
        malloc::m_free(o as *mut u8);
    }
}
