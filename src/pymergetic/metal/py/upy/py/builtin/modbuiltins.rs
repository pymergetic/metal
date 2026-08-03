//! modbuiltins — core builtins as Rust ops + a `builtins` module with
//! real FunNative callables (`print`, `len`, `range`, `int`, `str`).

use core::ffi::c_void;

use crate::upy::py::nativeglue;
use crate::upy::py::obj::{self, MpObj};
use crate::upy::py::objects::{
    self as objects, objbool, objdict, objfun_native, objint, objlist, objmodule, objnone,
    objrange, objstr, objtuple, TypeKind,
};
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
    if let Some(n) = objrange::len(o) {
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

unsafe fn builtin_len(args: &[MpObj]) -> Option<MpObj> {
    if args.len() != 1 {
        return None;
    }
    let n = len(args[0])?;
    Some(objint::from_isize(n as isize))
}

unsafe fn builtin_range(args: &[MpObj]) -> Option<MpObj> {
    let (start, stop, step) = match args.len() {
        1 => (0, nativeglue::as_i32(args[0])? as isize, 1),
        2 => (
            nativeglue::as_i32(args[0])? as isize,
            nativeglue::as_i32(args[1])? as isize,
            1,
        ),
        3 => (
            nativeglue::as_i32(args[0])? as isize,
            nativeglue::as_i32(args[1])? as isize,
            nativeglue::as_i32(args[2])? as isize,
        ),
        _ => return None,
    };
    let r = objrange::new(start, stop, step);
    if r == obj::OBJ_NULL {
        None
    } else {
        Some(r)
    }
}

unsafe fn builtin_int(args: &[MpObj]) -> Option<MpObj> {
    if args.len() != 1 {
        return None;
    }
    let o = args[0];
    if let Some(v) = objint::as_isize(o) {
        return Some(objint::from_isize(v));
    }
    if let Some(b) = objbool::value(o) {
        return Some(objint::from_isize(if b { 1 } else { 0 }));
    }
    if objnone::is_none(o) {
        return Some(objint::from_isize(0));
    }
    if let Some(bytes) = objstr::as_bytes(o) {
        let mut i = 0usize;
        let mut neg = false;
        if bytes.first() == Some(&b'-') {
            neg = true;
            i = 1;
        } else if bytes.first() == Some(&b'+') {
            i = 1;
        }
        if i >= bytes.len() {
            return None;
        }
        let mut v: isize = 0;
        while i < bytes.len() {
            let c = bytes[i];
            if c < b'0' || c > b'9' {
                return None;
            }
            v = v.checked_mul(10)?.checked_add((c - b'0') as isize)?;
            i += 1;
        }
        if neg {
            v = -v;
        }
        return Some(objint::from_isize(v));
    }
    None
}

unsafe fn builtin_str(args: &[MpObj]) -> Option<MpObj> {
    if args.len() != 1 {
        return None;
    }
    let o = args[0];
    if let Some(b) = objstr::as_bytes(o) {
        return Some(objstr::new(b));
    }
    if objnone::is_none(o) {
        return Some(objstr::new(b"None"));
    }
    if let Some(v) = objbool::value(o) {
        return Some(objstr::new(if v { b"True" } else { b"False" }));
    }
    if let Some(n) = objint::as_isize(o) {
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
                return None;
            }
            tmp[pos] = digs[nd];
            pos += 1;
        }
        return Some(objstr::new(&tmp[..pos]));
    }
    None
}

unsafe fn store_native(m: MpObj, name: &str, ptr: *const c_void, n_args: u8, kind: u8) {
    let key = obj::new_qstr(qstr::from_str(name));
    let fun = objfun_native::new(ptr, n_args, kind);
    if fun != obj::OBJ_NULL {
        let _ = objmodule::store_attr(m, key, fun);
    }
}

/// Seed `print` / `len` / `range` / `int` / `str` into a globals dict
/// (REPL session) or any module dict-like store via the same FunNative
/// objects the `builtins` module exposes.
pub unsafe fn seed_callables_into_dict(d: MpObj) {
    let print = objfun_native::new(core::ptr::null(), 1, objfun_native::KIND_PRINT);
    if print != obj::OBJ_NULL {
        let _ = objdict::store(d, obj::new_qstr(qstr::from_str("print")), print);
    }
    let pairs: &[(&str, objfun_native::ObjFn, u8)] = &[
        ("len", builtin_len, 1),
        ("range", builtin_range, 1),
        ("int", builtin_int, 1),
        ("str", builtin_str, 1),
    ];
    for &(name, f, n) in pairs {
        let fun = objfun_native::new(f as *const c_void, n, objfun_native::KIND_OBJ);
        if fun != obj::OBJ_NULL {
            let _ = objdict::store(d, obj::new_qstr(qstr::from_str(name)), fun);
        }
    }
}

/// Build `builtins` module with type name attrs + real callables.
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
    store_native(
        m,
        "print",
        core::ptr::null(),
        1,
        objfun_native::KIND_PRINT,
    );
    store_native(
        m,
        "len",
        builtin_len as *const c_void,
        1,
        objfun_native::KIND_OBJ,
    );
    store_native(
        m,
        "range",
        builtin_range as *const c_void,
        1,
        objfun_native::KIND_OBJ,
    );
    store_native(
        m,
        "int",
        builtin_int as *const c_void,
        1,
        objfun_native::KIND_OBJ,
    );
    store_native(
        m,
        "str",
        builtin_str as *const c_void,
        1,
        objfun_native::KIND_OBJ,
    );
    m
}
