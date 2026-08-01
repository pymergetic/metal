//! objmodule — module = name + attribute dict.

use crate::upy::py::malloc;
use crate::upy::py::obj::{self, MpObj, MpObjBase};
use crate::upy::py::objects::objdict;
use crate::upy::py::objects::{TypeDesc, TYPE_MODULE};
use crate::upy::py::qstrdefs::Qstr;

#[repr(C)]
pub struct Module {
    pub base: MpObjBase,
    pub name: Qstr,
    pub globals: MpObj,
}

pub unsafe fn new(name: Qstr) -> MpObj {
    let globals = objdict::new(8);
    if globals == obj::OBJ_NULL {
        return obj::OBJ_NULL;
    }
    let p = malloc::m_malloc(core::mem::size_of::<Module>()) as *mut Module;
    if p.is_null() {
        objdict::free(globals);
        return obj::OBJ_NULL;
    }
    (*p).base = MpObjBase::new((&TYPE_MODULE as *const TypeDesc) as *const u8);
    (*p).name = name;
    (*p).globals = globals;
    p as MpObj
}

pub unsafe fn globals(o: MpObj) -> Option<MpObj> {
    as_ref(o).map(|p| (*p).globals)
}

pub unsafe fn name(o: MpObj) -> Option<Qstr> {
    as_ref(o).map(|p| (*p).name)
}

pub unsafe fn store_attr(o: MpObj, key: MpObj, val: MpObj) -> bool {
    let Some(g) = globals(o) else {
        return false;
    };
    objdict::store(g, key, val)
}

pub unsafe fn load_attr(o: MpObj, key: MpObj) -> Option<MpObj> {
    objdict::load(globals(o)?, key)
}

unsafe fn as_ref(o: MpObj) -> Option<*const Module> {
    if !obj::is_obj(o) {
        return None;
    }
    let p = o as *const Module;
    if (*p).base.type_ptr != (&TYPE_MODULE as *const TypeDesc) as *const u8 {
        return None;
    }
    Some(p)
}

pub unsafe fn free(o: MpObj) {
    if let Some(p) = as_ref(o) {
        let p = p as *mut Module;
        objdict::free((*p).globals);
        malloc::m_free(p as *mut u8);
    }
}
