//! objobject — base `object` type marker (static TypeDesc only for B4).

use crate::upy::py::obj::MpObj;
use crate::upy::py::objects::{objtype, TYPE_OBJECT};

pub fn type_obj() -> MpObj {
    objtype::as_obj(&TYPE_OBJECT)
}
