//! objboundmethod — `instance.method` bound-method object (`self` +
//! the underlying `FunBc`/`FunNative`; `self` is injected as arg 0 when
//! `vm.rs`'s `call_function` calls through one of these).

use crate::upy::py::malloc;
use crate::upy::py::obj::{self, MpObj, MpObjBase};
use crate::upy::py::objects::{TypeDesc, TYPE_BOUND_METHOD};

#[repr(C)]
pub struct BoundMethod {
    pub base: MpObjBase,
    pub self_obj: MpObj,
    pub func: MpObj,
}

pub unsafe fn new(self_obj: MpObj, func: MpObj) -> MpObj {
    let p = malloc::m_malloc(core::mem::size_of::<BoundMethod>()) as *mut BoundMethod;
    if p.is_null() {
        return obj::OBJ_NULL;
    }
    (*p).base = MpObjBase::new((&TYPE_BOUND_METHOD as *const TypeDesc) as *const u8);
    (*p).self_obj = self_obj;
    (*p).func = func;
    p as MpObj
}

unsafe fn as_ref(o: MpObj) -> Option<*const BoundMethod> {
    if !obj::is_obj(o) {
        return None;
    }
    let p = o as *const BoundMethod;
    if (*p).base.type_ptr != (&TYPE_BOUND_METHOD as *const TypeDesc) as *const u8 {
        return None;
    }
    Some(p)
}

pub unsafe fn is_bound_method(o: MpObj) -> bool {
    as_ref(o).is_some()
}

/// `(self, underlying_func)` for a bound-method call.
pub unsafe fn parts(o: MpObj) -> Option<(MpObj, MpObj)> {
    let p = as_ref(o)?;
    Some(((*p).self_obj, (*p).func))
}

pub unsafe fn free(o: MpObj) {
    if as_ref(o).is_some() {
        malloc::m_free(o as *mut u8);
    }
}
