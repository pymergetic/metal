//! Object types (B2 essentials + B4 extensions).

pub mod objarray;
pub mod objbool;
pub mod objboundmethod;
pub mod objcell;
pub mod objdict;
pub mod objexcept;
pub mod objfloat;
pub mod objfun;
pub mod objfun_native;
pub mod objinstance;
pub mod objint;
pub mod objiter;
pub mod objlist;
pub mod objmodule;
pub mod objnone;
pub mod objobject;
pub mod objrange;
pub mod objset;
pub mod objslice;
pub mod objsingleton;
pub mod objstr;
pub mod objtuple;
pub mod objtype;

use super::obj::{MpObj, MpObjBase};
use super::qstrdefs::Qstr;

#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum TypeKind {
    None = 0,
    Bool = 1,
    Int = 2,
    Str = 3,
    List = 4,
    Dict = 5,
    Tuple = 6,
    Type = 7,
    Except = 8,
    Fun = 9,
    Module = 10,
    Float = 11,
    Set = 12,
    Range = 13,
    Slice = 14,
    Cell = 15,
    ByteArray = 16,
    Object = 17,
    Singleton = 18,
    Deque = 19,
    Iter = 20,
}

/// Marker written only into heap [`objtype::UserType::desc`]. Distinguishes
/// a user-class `MpObj` (which *is* a `TypeDesc` at offset 0) from every
/// other heap object whose first word is `MpObjBase.type_ptr` -- casting
/// those as `TypeDesc` and reading `is_user` from the pointer's 2nd byte
/// is a false positive (broke FunBc `CALL_FUNCTION` in smoke).
pub const USER_TYPE_MAGIC: u32 = 0x5554_5950; // 'UTYP'

#[repr(C)]
pub struct TypeDesc {
    pub kind: TypeKind,
    /// `true` only for a heap-allocated [`objtype::UserType`].
    pub is_user: bool,
    pub _pad: u8,
    /// [`USER_TYPE_MAGIC`] for user types; `0` for every static `TYPE_*`.
    pub magic: u32,
    pub name: Qstr,
}

pub static TYPE_NONE: TypeDesc = TypeDesc {
    kind: TypeKind::None,
    is_user: false,
    _pad: 0,
    magic: 0,
    name: super::qstrdefs::QSTR_NONE,
};
pub static TYPE_BOOL: TypeDesc = TypeDesc {
    kind: TypeKind::Bool,
    is_user: false,
    _pad: 0,
    magic: 0,
    name: super::qstrdefs::QSTR_BOOL,
};
pub static TYPE_INT: TypeDesc = TypeDesc {
    kind: TypeKind::Int,
    is_user: false,
    _pad: 0,
    magic: 0,
    name: super::qstrdefs::QSTR_INT,
};
pub static TYPE_STR: TypeDesc = TypeDesc {
    kind: TypeKind::Str,
    is_user: false,
    _pad: 0,
    magic: 0,
    name: super::qstrdefs::QSTR_STR,
};
pub static TYPE_LIST: TypeDesc = TypeDesc {
    kind: TypeKind::List,
    is_user: false,
    _pad: 0,
    magic: 0,
    name: super::qstrdefs::QSTR_LIST,
};
pub static TYPE_DICT: TypeDesc = TypeDesc {
    kind: TypeKind::Dict,
    is_user: false,
    _pad: 0,
    magic: 0,
    name: super::qstrdefs::QSTR_DICT,
};
pub static TYPE_TUPLE: TypeDesc = TypeDesc {
    kind: TypeKind::Tuple,
    is_user: false,
    _pad: 0,
    magic: 0,
    name: super::qstrdefs::QSTR_TUPLE,
};
pub static TYPE_TYPE: TypeDesc = TypeDesc {
    kind: TypeKind::Type,
    is_user: false,
    _pad: 0,
    magic: 0,
    name: super::qstrdefs::QSTR_TYPE,
};
pub static TYPE_EXCEPT: TypeDesc = TypeDesc {
    kind: TypeKind::Except,
    is_user: false,
    _pad: 0,
    magic: 0,
    name: super::qstrdefs::QSTR_EXCEPTION,
};
pub static TYPE_FUN: TypeDesc = TypeDesc {
    kind: TypeKind::Fun,
    is_user: false,
    _pad: 0,
    magic: 0,
    name: super::qstrdefs::QSTR_FUNCTION,
};
/// Distinct identity from [`TYPE_FUN`] (a bytecode `objfun::FunBc`) so
/// `objfun_native::as_ref` never mistakes one for the other, even though
/// both report [`TypeKind::Fun`] to `kind_of`/`isinstance`-style checks.
pub static TYPE_FUN_NATIVE: TypeDesc = TypeDesc {
    kind: TypeKind::Fun,
    is_user: false,
    _pad: 0,
    magic: 0,
    name: super::qstrdefs::QSTR_FUNCTION,
};
/// Bound instance method (`objboundmethod::BoundMethod`) -- reports
/// [`TypeKind::Fun`] too (it is callable), distinct type identity from
/// [`TYPE_FUN`]/[`TYPE_FUN_NATIVE`] so `objboundmethod::as_ref` never
/// mistakes a plain function for a bound one or vice versa.
pub static TYPE_BOUND_METHOD: TypeDesc = TypeDesc {
    kind: TypeKind::Fun,
    is_user: false,
    _pad: 0,
    magic: 0,
    name: super::qstrdefs::QSTR_FUNCTION,
};
pub static TYPE_MODULE: TypeDesc = TypeDesc {
    kind: TypeKind::Module,
    is_user: false,
    _pad: 0,
    magic: 0,
    name: super::qstrdefs::QSTR_MODULE,
};
pub static TYPE_FLOAT: TypeDesc = TypeDesc {
    kind: TypeKind::Float,
    is_user: false,
    _pad: 0,
    magic: 0,
    name: super::qstrdefs::QSTR_FLOAT,
};
pub static TYPE_SET: TypeDesc = TypeDesc {
    kind: TypeKind::Set,
    is_user: false,
    _pad: 0,
    magic: 0,
    name: super::qstrdefs::QSTR_SET,
};
pub static TYPE_RANGE: TypeDesc = TypeDesc {
    kind: TypeKind::Range,
    is_user: false,
    _pad: 0,
    magic: 0,
    name: super::qstrdefs::QSTR_RANGE,
};
pub static TYPE_SLICE: TypeDesc = TypeDesc {
    kind: TypeKind::Slice,
    is_user: false,
    _pad: 0,
    magic: 0,
    name: super::qstrdefs::QSTR_SLICE,
};
pub static TYPE_CELL: TypeDesc = TypeDesc {
    kind: TypeKind::Cell,
    is_user: false,
    _pad: 0,
    magic: 0,
    name: super::qstrdefs::QSTR_CELL,
};
pub static TYPE_BYTEARRAY: TypeDesc = TypeDesc {
    kind: TypeKind::ByteArray,
    is_user: false,
    _pad: 0,
    magic: 0,
    name: super::qstrdefs::QSTR_BYTEARRAY,
};
pub static TYPE_OBJECT: TypeDesc = TypeDesc {
    kind: TypeKind::Object,
    is_user: false,
    _pad: 0,
    magic: 0,
    name: super::qstrdefs::QSTR_OBJECT,
};
pub static TYPE_ITER: TypeDesc = TypeDesc {
    kind: TypeKind::Iter,
    is_user: false,
    _pad: 0,
    magic: 0,
    name: super::qstrdefs::QSTR_ITER,
};

#[inline]
pub fn type_of_heap(o: MpObj) -> Option<&'static TypeDesc> {
    if !super::obj::is_obj(o) {
        return None;
    }
    let base = o as *const MpObjBase;
    unsafe {
        let t = (*base).type_ptr as *const TypeDesc;
        if t.is_null() {
            None
        } else {
            Some(&*t)
        }
    }
}

/// Content-aware equality shared by `objdict` (key match), `objset`
/// (membership) and `vm.rs` (`in`/comparison containment): small ints by
/// value, qstr/`str` by byte content (either side), everything else by
/// raw word identity (covers `None`/`bool`/pointer-identity heap
/// objects). Not a deep structural equality for list/tuple/dict --
/// those still fall back to identity, an honest limitation shared with
/// upstream's own `mp_obj_equal` subset this VM implements.
pub unsafe fn obj_eq(a: MpObj, b: MpObj) -> bool {
    if a == b {
        return true;
    }
    if super::obj::is_small_int(a) && super::obj::is_small_int(b) {
        return super::obj::small_int_value(a) == super::obj::small_int_value(b);
    }
    fn bytes_of(o: MpObj) -> Option<&'static [u8]> {
        if super::obj::is_qstr(o) {
            Some(super::qstr::str(super::obj::qstr_value(o)))
        } else {
            unsafe { objstr::as_bytes(o) }
        }
    }
    match (bytes_of(a), bytes_of(b)) {
        (Some(x), Some(y)) => x == y,
        _ => false,
    }
}

#[inline]
pub fn kind_of(o: MpObj) -> Option<TypeKind> {
    if super::obj::is_small_int(o) {
        return Some(TypeKind::Int);
    }
    if super::obj::is_immediate(o) {
        let slot = o >> 3;
        return Some(match slot {
            0 | 1 => TypeKind::Bool,
            2 => TypeKind::None,
            s if s >= 8 => TypeKind::Singleton,
            _ => return None,
        });
    }
    type_of_heap(o).map(|t| t.kind)
}
