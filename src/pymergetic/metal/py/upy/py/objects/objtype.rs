//! objtype — type descriptor object (wraps TypeDesc pointer as MpObj),
//! plus [`UserType`]: a heap-allocated class created by `compile.rs`'s
//! `class` support (name + methods dict + optional single base),
//! embedding a real `TypeDesc` as its first field so every existing
//! `type_ptr`-dispatch site (`obj::is_obj`, `objinstance::new`'s header,
//! `kind_of`) keeps working unmodified -- see `objects/mod.rs`'s
//! `TypeDesc::is_user` doc for why that field exists.

use crate::upy::py::malloc;
use crate::upy::py::obj::{self, MpObj};
use crate::upy::py::objects::{objdict, TypeDesc, TypeKind, USER_TYPE_MAGIC};
use crate::upy::py::qstrdefs::Qstr;

/// Types are immortal statics for B2 — return pointer tagged as heap obj word.
pub fn as_obj(td: &'static TypeDesc) -> MpObj {
    // Pointers are 8-byte aligned on x86_64; low bits clear => is_obj.
    td as *const TypeDesc as MpObj
}

pub fn name(o: MpObj) -> Option<Qstr> {
    if !obj::is_obj(o) {
        return None;
    }
    let td = o as *const TypeDesc;
    unsafe { Some((*td).name) }
}

pub fn kind(o: MpObj) -> Option<TypeKind> {
    if !obj::is_obj(o) {
        return None;
    }
    let td = o as *const TypeDesc;
    unsafe { Some((*td).kind) }
}

/// A user-defined `class` (`compile.rs`'s `classdef` support): real,
/// heap-allocated `TypeDesc` (`is_user = true`) plus a methods dict and
/// an optional single base class (`OBJ_NULL` for none -- upstream
/// multiple-inheritance MRO is out of scope for this slice, a real,
/// documented limitation, not a silent single-base miscompile of a
/// multi-base `class Foo(A, B):`, which `compile.rs` rejects outright).
#[repr(C)]
pub struct UserType {
    pub desc: TypeDesc,
    pub methods: MpObj,
    pub base: MpObj,
}

/// `base` must be `obj::OBJ_NULL` (no base) or a prior `new_user_type`
/// result. `OBJ_NULL` on OOM.
pub unsafe fn new_user_type(name: Qstr, base: MpObj) -> MpObj {
    let methods = objdict::new(8);
    if methods == obj::OBJ_NULL {
        return obj::OBJ_NULL;
    }
    let p = malloc::m_malloc(core::mem::size_of::<UserType>()) as *mut UserType;
    if p.is_null() {
        objdict::free(methods);
        return obj::OBJ_NULL;
    }
    (*p).desc = TypeDesc {
        kind: TypeKind::Object,
        is_user: true,
        _pad: 0,
        magic: USER_TYPE_MAGIC,
        name,
    };
    (*p).methods = methods;
    (*p).base = base;
    p as MpObj
}

pub fn is_user_type(o: MpObj) -> bool {
    if !obj::is_obj(o) {
        return false;
    }
    // UserType MpObjs start with TypeDesc{is_user, magic}; FunBc/etc.
    // start with MpObjBase.type_ptr -- never trust `is_user` alone.
    let td = o as *const TypeDesc;
    unsafe { (*td).is_user && (*td).magic == USER_TYPE_MAGIC }
}

unsafe fn as_user_ref(o: MpObj) -> Option<*const UserType> {
    if !is_user_type(o) {
        return None;
    }
    Some(o as *const UserType)
}

pub unsafe fn user_methods(o: MpObj) -> Option<MpObj> {
    as_user_ref(o).map(|p| (*p).methods)
}

pub unsafe fn user_base(o: MpObj) -> Option<MpObj> {
    as_user_ref(o).map(|p| (*p).base)
}

/// Look up `key` (a qstr-tagged `MpObj`) through the MRO chain (`o`,
/// then its bases, single-inheritance only -- see [`UserType`] doc).
pub unsafe fn find_method(o: MpObj, key: MpObj) -> Option<MpObj> {
    let mut cur = o;
    loop {
        let methods = user_methods(cur)?;
        if let Some(v) = objdict::load(methods, key) {
            return Some(v);
        }
        let base = user_base(cur)?;
        if base == obj::OBJ_NULL {
            return None;
        }
        cur = base;
    }
}

pub unsafe fn free_user_type(o: MpObj) {
    if let Some(p) = as_user_ref(o) {
        let p = p as *mut UserType;
        objdict::free((*p).methods);
        malloc::m_free(p as *mut u8);
    }
}
