//! modplatform — platform identity for Metal upy.

use crate::upy::py::obj::MpObj;
use crate::upy::py::objects::objstr;

pub unsafe fn platform() -> MpObj {
    objstr::new(b"Metal")
}

pub unsafe fn python_compiler() -> MpObj {
    objstr::new(b"upy-metal")
}

pub unsafe fn libc_ver() -> MpObj {
    objstr::new(b"metal")
}
