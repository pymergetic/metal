//! objfun — bytecode function object (code ptr + prelude sizes).

use crate::upy::py::malloc;
use crate::upy::py::obj::{self, MpObj, MpObjBase};
use crate::upy::py::objects::{TypeDesc, TYPE_FUN};

#[repr(C)]
pub struct FunBc {
    pub base: MpObjBase,
    pub bytecode: *const u8,
    pub bytecode_len: usize,
    pub n_state: usize,
}

pub unsafe fn new(code: &[u8], n_state: usize) -> MpObj {
    let p = malloc::m_malloc(core::mem::size_of::<FunBc>()) as *mut FunBc;
    if p.is_null() {
        return obj::OBJ_NULL;
    }
    let buf = malloc::m_malloc(code.len());
    if buf.is_null() {
        malloc::m_free(p as *mut u8);
        return obj::OBJ_NULL;
    }
    core::ptr::copy_nonoverlapping(code.as_ptr(), buf, code.len());
    (*p).base = MpObjBase::new((&TYPE_FUN as *const TypeDesc) as *const u8);
    (*p).bytecode = buf;
    (*p).bytecode_len = code.len();
    (*p).n_state = n_state;
    p as MpObj
}

pub unsafe fn code(o: MpObj) -> Option<&'static [u8]> {
    let p = as_ref(o)?;
    Some(core::slice::from_raw_parts((*p).bytecode, (*p).bytecode_len))
}

pub unsafe fn n_state(o: MpObj) -> Option<usize> {
    as_ref(o).map(|p| (*p).n_state)
}

unsafe fn as_ref(o: MpObj) -> Option<*const FunBc> {
    if !obj::is_obj(o) {
        return None;
    }
    let p = o as *const FunBc;
    if (*p).base.type_ptr != (&TYPE_FUN as *const TypeDesc) as *const u8 {
        return None;
    }
    Some(p)
}

pub unsafe fn free(o: MpObj) {
    if let Some(p) = as_ref(o) {
        let p = p as *mut FunBc;
        malloc::m_free((*p).bytecode as *mut u8);
        malloc::m_free(p as *mut u8);
    }
}
