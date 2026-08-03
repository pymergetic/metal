//! objinstance — user-class instance: header `type_ptr` points at the
//! owning `objtype::UserType`'s embedded `TypeDesc` (so `obj::is_obj` +
//! the usual `type_ptr` dispatch keeps working unmodified), plus a
//! per-instance attribute dict.

use crate::upy::py::malloc;
use crate::upy::py::obj::{self, MpObj, MpObjBase};
use crate::upy::py::objects::{objdict, objtype, TypeDesc, USER_TYPE_MAGIC};

#[repr(C)]
pub struct Instance {
    pub base: MpObjBase,
    pub attrs: MpObj,
}

/// `class` must be a live `objtype::new_user_type` result -- `OBJ_NULL`
/// on a non-class `class` or OOM.
pub unsafe fn new(class: MpObj) -> MpObj {
    if !objtype::is_user_type(class) {
        return obj::OBJ_NULL;
    }
    let attrs = objdict::new(8);
    if attrs == obj::OBJ_NULL {
        return obj::OBJ_NULL;
    }
    let p = malloc::m_malloc(core::mem::size_of::<Instance>()) as *mut Instance;
    if p.is_null() {
        objdict::free(attrs);
        return obj::OBJ_NULL;
    }
    (*p).base = MpObjBase::new(class as *const u8);
    (*p).attrs = attrs;
    p as MpObj
}

unsafe fn as_ref(o: MpObj) -> Option<*const Instance> {
    if !obj::is_obj(o) {
        return None;
    }
    let p = o as *const Instance;
    let td = (*p).base.type_ptr as *const TypeDesc;
    if td.is_null() || !(*td).is_user || (*td).magic != USER_TYPE_MAGIC {
        return None;
    }
    Some(p)
}

pub unsafe fn is_instance(o: MpObj) -> bool {
    as_ref(o).is_some()
}

/// The `UserType` this instance was constructed from (an
/// `objtype::is_user_type` value) -- used for method-resolution lookups.
pub unsafe fn class_of(o: MpObj) -> Option<MpObj> {
    let p = as_ref(o)?;
    Some((*p).base.type_ptr as MpObj)
}

pub unsafe fn load_attr(o: MpObj, key: MpObj) -> Option<MpObj> {
    let p = as_ref(o)?;
    objdict::load((*p).attrs, key)
}

pub unsafe fn store_attr(o: MpObj, key: MpObj, val: MpObj) -> bool {
    let Some(p) = as_ref(o) else {
        return false;
    };
    objdict::store((*p).attrs, key, val)
}

pub unsafe fn free(o: MpObj) {
    if let Some(p) = as_ref(o) {
        objdict::free((*p).attrs);
        malloc::m_free(p as *mut u8);
    }
}
