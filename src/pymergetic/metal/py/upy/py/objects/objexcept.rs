//! objexcept — simple exception instance (type name qstr + message str obj).

use crate::upy::py::malloc;
use crate::upy::py::obj::{self, MpObj, MpObjBase};
use crate::upy::py::objects::{TypeDesc, TYPE_EXCEPT};
use crate::upy::py::qstrdefs::Qstr;

#[repr(C)]
pub struct Except {
    pub base: MpObjBase,
    pub type_name: Qstr,
    pub msg: MpObj,
}

pub unsafe fn new(type_name: Qstr, msg: MpObj) -> MpObj {
    let p = malloc::m_malloc(core::mem::size_of::<Except>()) as *mut Except;
    if p.is_null() {
        return obj::OBJ_NULL;
    }
    (*p).base = MpObjBase::new((&TYPE_EXCEPT as *const TypeDesc) as *const u8);
    (*p).type_name = type_name;
    (*p).msg = msg;
    p as MpObj
}

pub unsafe fn msg(o: MpObj) -> Option<MpObj> {
    let p = as_ref(o)?;
    Some((*p).msg)
}

pub unsafe fn type_name(o: MpObj) -> Option<Qstr> {
    let p = as_ref(o)?;
    Some((*p).type_name)
}

unsafe fn as_ref(o: MpObj) -> Option<*const Except> {
    if !obj::is_obj(o) {
        return None;
    }
    let p = o as *const Except;
    if (*p).base.type_ptr != (&TYPE_EXCEPT as *const TypeDesc) as *const u8 {
        return None;
    }
    Some(p)
}

pub unsafe fn free(o: MpObj) {
    if as_ref(o).is_some() {
        malloc::m_free(o as *mut u8);
    }
}
