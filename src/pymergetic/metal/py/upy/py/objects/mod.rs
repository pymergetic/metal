//! Object types (B2 essentials + B4 extensions).

pub mod objarray;
pub mod objbool;
pub mod objcell;
pub mod objdict;
pub mod objexcept;
pub mod objfloat;
pub mod objfun;
pub mod objint;
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
}

#[repr(C)]
pub struct TypeDesc {
    pub kind: TypeKind,
    pub name: Qstr,
}

pub static TYPE_NONE: TypeDesc = TypeDesc {
    kind: TypeKind::None,
    name: super::qstrdefs::QSTR_NONE,
};
pub static TYPE_BOOL: TypeDesc = TypeDesc {
    kind: TypeKind::Bool,
    name: super::qstrdefs::QSTR_BOOL,
};
pub static TYPE_INT: TypeDesc = TypeDesc {
    kind: TypeKind::Int,
    name: super::qstrdefs::QSTR_INT,
};
pub static TYPE_STR: TypeDesc = TypeDesc {
    kind: TypeKind::Str,
    name: super::qstrdefs::QSTR_STR,
};
pub static TYPE_LIST: TypeDesc = TypeDesc {
    kind: TypeKind::List,
    name: super::qstrdefs::QSTR_LIST,
};
pub static TYPE_DICT: TypeDesc = TypeDesc {
    kind: TypeKind::Dict,
    name: super::qstrdefs::QSTR_DICT,
};
pub static TYPE_TUPLE: TypeDesc = TypeDesc {
    kind: TypeKind::Tuple,
    name: super::qstrdefs::QSTR_TUPLE,
};
pub static TYPE_TYPE: TypeDesc = TypeDesc {
    kind: TypeKind::Type,
    name: super::qstrdefs::QSTR_TYPE,
};
pub static TYPE_EXCEPT: TypeDesc = TypeDesc {
    kind: TypeKind::Except,
    name: super::qstrdefs::QSTR_EXCEPTION,
};
pub static TYPE_FUN: TypeDesc = TypeDesc {
    kind: TypeKind::Fun,
    name: super::qstrdefs::QSTR_FUNCTION,
};
pub static TYPE_MODULE: TypeDesc = TypeDesc {
    kind: TypeKind::Module,
    name: super::qstrdefs::QSTR_MODULE,
};
pub static TYPE_FLOAT: TypeDesc = TypeDesc {
    kind: TypeKind::Float,
    name: super::qstrdefs::QSTR_FLOAT,
};
pub static TYPE_SET: TypeDesc = TypeDesc {
    kind: TypeKind::Set,
    name: super::qstrdefs::QSTR_SET,
};
pub static TYPE_RANGE: TypeDesc = TypeDesc {
    kind: TypeKind::Range,
    name: super::qstrdefs::QSTR_RANGE,
};
pub static TYPE_SLICE: TypeDesc = TypeDesc {
    kind: TypeKind::Slice,
    name: super::qstrdefs::QSTR_SLICE,
};
pub static TYPE_CELL: TypeDesc = TypeDesc {
    kind: TypeKind::Cell,
    name: super::qstrdefs::QSTR_CELL,
};
pub static TYPE_BYTEARRAY: TypeDesc = TypeDesc {
    kind: TypeKind::ByteArray,
    name: super::qstrdefs::QSTR_BYTEARRAY,
};
pub static TYPE_OBJECT: TypeDesc = TypeDesc {
    kind: TypeKind::Object,
    name: super::qstrdefs::QSTR_OBJECT,
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
