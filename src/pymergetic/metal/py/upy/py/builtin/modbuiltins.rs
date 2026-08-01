//! modbuiltins — core builtins as Rust ops + a `builtins` module shell.

use crate::upy::py::obj::{self, MpObj};
use crate::upy::py::objects::{objdict, objlist, objmodule, objstr, objtuple, TypeKind};
use crate::upy::py::objects::{self as objects};
use crate::upy::py::qstr;
use crate::upy::py::qstrdefs;

extern "C" {
    fn pm_metal_log(line: *const u8);
}

pub unsafe fn len(o: MpObj) -> Option<usize> {
    if let Some(n) = objlist::len(o) {
        return Some(n);
    }
    if let Some(n) = objtuple::len(o) {
        return Some(n);
    }
    if let Some(n) = objdict::len(o) {
        return Some(n);
    }
    if let Some(b) = objstr::as_bytes(o) {
        return Some(b.len());
    }
    None
}

pub fn abs_int(o: MpObj) -> Option<isize> {
    let v = objects::objint::as_isize(o)?;
    Some(if v < 0 { -v } else { v })
}

pub fn isinstance(o: MpObj, kind: TypeKind) -> bool {
    objects::kind_of(o) == Some(kind)
}

/// `print(...)` -> Metal log (ASCII). One arg for the finished subset.
pub unsafe fn print_bytes(bytes: &[u8]) -> bool {
    let mut line = [0u8; 256];
    if bytes.len() >= line.len() {
        return false;
    }
    line[..bytes.len()].copy_from_slice(bytes);
    line[bytes.len()] = 0;
    pm_metal_log(line.as_ptr());
    true
}

pub unsafe fn print_obj(o: MpObj) -> bool {
    if let Some(b) = objstr::as_bytes(o) {
        return print_bytes(b);
    }
    if objects::objnone::is_none(o) {
        return print_bytes(b"None");
    }
    if let Some(v) = objects::objbool::value(o) {
        return print_bytes(if v { b"True" } else { b"False" });
    }
    if let Some(n) = objects::objint::as_isize(o) {
        let mut tmp = [0u8; 24];
        let mut pos = 0usize;
        let mut v = n;
        if v < 0 {
            tmp[pos] = b'-';
            pos += 1;
            v = -v;
        }
        let mut digs = [0u8; 20];
        let mut nd = 0usize;
        if v == 0 {
            digs[0] = b'0';
            nd = 1;
        } else {
            let mut u = v as u64;
            while u > 0 {
                digs[nd] = b'0' + (u % 10) as u8;
                u /= 10;
                nd += 1;
            }
        }
        while nd > 0 {
            nd -= 1;
            if pos >= tmp.len() {
                return false;
            }
            tmp[pos] = digs[nd];
            pos += 1;
        }
        return print_bytes(&tmp[..pos]);
    }
    false
}

/// Build `builtins` module with type name attrs (data only for B3).
pub unsafe fn make_module() -> MpObj {
    let m = objmodule::new(qstr::from_str("builtins"));
    if m == obj::OBJ_NULL {
        return m;
    }
    let _ = objmodule::store_attr(
        m,
        obj::new_qstr(qstrdefs::QSTR_NAME),
        obj::new_qstr(qstr::from_str("builtins")),
    );
    let _ = objmodule::store_attr(
        m,
        obj::new_qstr(qstrdefs::QSTR_INT),
        objects::objtype::as_obj(&objects::TYPE_INT),
    );
    let _ = objmodule::store_attr(
        m,
        obj::new_qstr(qstrdefs::QSTR_STR),
        objects::objtype::as_obj(&objects::TYPE_STR),
    );
    let _ = objmodule::store_attr(
        m,
        obj::new_qstr(qstrdefs::QSTR_LIST),
        objects::objtype::as_obj(&objects::TYPE_LIST),
    );
    let _ = objmodule::store_attr(
        m,
        obj::new_qstr(qstrdefs::QSTR_DICT),
        objects::objtype::as_obj(&objects::TYPE_DICT),
    );
    let _ = objmodule::store_attr(
        m,
        obj::new_qstr(qstrdefs::QSTR_BOOL),
        objects::objtype::as_obj(&objects::TYPE_BOOL),
    );
    m
}
