//! nativeglue — small marshalling helpers for native callables and the
//! VM's typed `objfun_native` dispatch (box/unbox small-int / C string /
//! None). Bind authors and extmod wrappers call these instead of each
//! reinventing REPR_A tagging.

use crate::upy::py::obj::{self, MpObj};
use crate::upy::py::objects::objstr;

/// `Some(v)` iff `o` is a small int that fits in `i32`.
pub fn as_i32(o: MpObj) -> Option<i32> {
    let v = obj::small_int_value_checked(o)?;
    i32::try_from(v).ok()
}

/// Tag `i` as a small-int `MpObj`.
pub fn from_i32(i: i32) -> MpObj {
    obj::new_small_int(i as isize)
}

/// Pointer to a heap `str`'s NUL-terminated bytes (`objstr::new` always
/// writes a trailing 0). `None` if `o` is not an `objstr`.
pub unsafe fn as_cstr_ptr(o: MpObj) -> Option<*const u8> {
    let bytes = objstr::as_bytes(o)?;
    Some(bytes.as_ptr())
}

/// Pop `n` values from a left-to-right arg buffer already filled by the
/// VM (`args[0]` is the first positional). Used by multi-arg i32 kinds.
pub fn args_as_i32_2(args: &[MpObj]) -> Option<(i32, i32)> {
    if args.len() != 2 {
        return None;
    }
    Some((as_i32(args[0])?, as_i32(args[1])?))
}

/// Single positional -> `i32` (for `KIND_I32_1` / similar).
pub fn arg0_as_i32(args: &[MpObj]) -> Option<i32> {
    if args.len() != 1 {
        return None;
    }
    as_i32(args[0])
}
