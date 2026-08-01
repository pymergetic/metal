//! moderrno — errno constants as small ints on a module.

use crate::upy::py::obj::{self, MpObj};
use crate::upy::py::objects::objdict;
use crate::upy::py::objects::objmodule;
use crate::upy::py::qstr;
use crate::upy::py::qstrdefs;

pub const EPERM: isize = 1;
pub const ENOENT: isize = 2;
pub const EIO: isize = 5;
pub const ENOMEM: isize = 12;
pub const EACCES: isize = 13;
pub const EEXIST: isize = 17;
pub const EINVAL: isize = 22;

/// Build `errno` module (caller owns; free with `objmodule::free`).
pub unsafe fn make_module() -> MpObj {
    let name = qstr::from_str("errno");
    let m = objmodule::new(name);
    if m == obj::OBJ_NULL {
        return m;
    }
    let pairs: &[(&str, isize)] = &[
        ("EPERM", EPERM),
        ("ENOENT", ENOENT),
        ("EIO", EIO),
        ("ENOMEM", ENOMEM),
        ("EACCES", EACCES),
        ("EEXIST", EEXIST),
        ("EINVAL", EINVAL),
    ];
    for &(k, v) in pairs {
        let key = obj::new_qstr(qstr::from_str(k));
        let val = obj::new_small_int(v);
        let _ = objmodule::store_attr(m, key, val);
    }
    let _ = objmodule::store_attr(
        m,
        obj::new_qstr(qstrdefs::QSTR_NAME),
        obj::new_qstr(qstr::from_str("errno")),
    );
    m
}

pub unsafe fn get_attr(mod_obj: MpObj, name: &str) -> Option<isize> {
    let key = obj::new_qstr(qstr::from_str(name));
    let v = objmodule::load_attr(mod_obj, key)?;
    if obj::is_small_int(v) {
        Some(obj::small_int_value(v))
    } else {
        None
    }
}

/// Len of errno module's globals (for smoke).
pub unsafe fn attr_count(mod_obj: MpObj) -> Option<usize> {
    objdict::len(objmodule::globals(mod_obj)?)
}
