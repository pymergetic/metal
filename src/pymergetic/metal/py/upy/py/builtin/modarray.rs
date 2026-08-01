//! modarray — construct bytearrays (thin wrapper over objects::objarray).

use crate::upy::py::obj::MpObj;
use crate::upy::py::objects::objarray;

pub unsafe fn bytearray(n: usize) -> MpObj {
    objarray::new(n)
}

pub unsafe fn len(o: MpObj) -> Option<usize> {
    objarray::len(o)
}
