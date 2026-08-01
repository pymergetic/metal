//! modmath — basic float/int math helpers (Metal freestanding subset).

use crate::upy::py::obj::MpObj;
use crate::upy::py::objects::{objfloat, objint};

pub fn fabs(o: MpObj) -> Option<f64> {
    unsafe {
        if let Some(v) = objfloat::value(o) {
            return Some(if v < 0.0 { -v } else { v });
        }
    }
    let i = objint::as_isize(o)?;
    let f = i as f64;
    Some(if f < 0.0 { -f } else { f })
}

pub unsafe fn add(a: MpObj, b: MpObj) -> Option<MpObj> {
    if let (Some(x), Some(y)) = (objfloat::value(a), objfloat::value(b)) {
        return Some(objfloat::new(x + y));
    }
    if let (Some(x), Some(y)) = (objint::as_isize(a), objint::as_isize(b)) {
        return Some(objint::from_isize(x.wrapping_add(y)));
    }
    if let (Some(x), Some(y)) = (objfloat::value(a), objint::as_isize(b)) {
        return Some(objfloat::new(x + (y as f64)));
    }
    if let (Some(x), Some(y)) = (objint::as_isize(a), objfloat::value(b)) {
        return Some(objfloat::new((x as f64) + y));
    }
    None
}

pub fn isqrt(n: isize) -> Option<isize> {
    if n < 0 {
        return None;
    }
    let mut x = n;
    let mut y = (x + 1) / 2;
    while y < x {
        x = y;
        y = (x + n / x) / 2;
    }
    Some(x)
}
