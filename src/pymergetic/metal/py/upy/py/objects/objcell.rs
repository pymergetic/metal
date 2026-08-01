//! objcell — closure cell holding one object.

use crate::upy::py::malloc;
use crate::upy::py::obj::{self, MpObj, MpObjBase};
use crate::upy::py::objects::{TypeDesc, TYPE_CELL};

#[repr(C)]
pub struct Cell {
    pub base: MpObjBase,
    pub obj: MpObj,
}

pub unsafe fn new(obj: MpObj) -> MpObj {
    let p = malloc::m_malloc(core::mem::size_of::<Cell>()) as *mut Cell;
    if p.is_null() {
        return obj::OBJ_NULL;
    }
    (*p).base = MpObjBase::new((&TYPE_CELL as *const TypeDesc) as *const u8);
    (*p).obj = obj;
    p as MpObj
}

pub unsafe fn get(o: MpObj) -> Option<MpObj> {
    as_ref(o).map(|p| (*p).obj)
}

pub unsafe fn set(o: MpObj, v: MpObj) -> bool {
    let Some(p) = as_mut(o) else {
        return false;
    };
    (*p).obj = v;
    true
}

unsafe fn as_ref(o: MpObj) -> Option<*const Cell> {
    if !obj::is_obj(o) {
        return None;
    }
    let p = o as *const Cell;
    if (*p).base.type_ptr != (&TYPE_CELL as *const TypeDesc) as *const u8 {
        return None;
    }
    Some(p)
}

unsafe fn as_mut(o: MpObj) -> Option<*mut Cell> {
    as_ref(o).map(|p| p as *mut Cell)
}

pub unsafe fn free(o: MpObj) {
    if as_ref(o).is_some() {
        malloc::m_free(o as *mut u8);
    }
}
