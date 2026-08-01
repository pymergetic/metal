//! modheapq — heappush / heappop on lists (int keys; finished subset).

use crate::upy::py::obj::MpObj;
use crate::upy::py::objects::{objint, objlist};

unsafe fn less(a: MpObj, b: MpObj) -> Option<bool> {
    Some(objint::as_isize(a)? < objint::as_isize(b)?)
}

pub unsafe fn heappush(heap: MpObj, item: MpObj) -> bool {
    if !objlist::append(heap, item) {
        return false;
    }
    let mut i = match objlist::len(heap) {
        Some(n) if n > 0 => n - 1,
        _ => return false,
    };
    while i > 0 {
        let parent = (i - 1) / 2;
        let iv = objlist::get(heap, i).unwrap();
        let pv = objlist::get(heap, parent).unwrap();
        if less(iv, pv) != Some(true) {
            break;
        }
        let _ = objlist::set(heap, i, pv);
        let _ = objlist::set(heap, parent, iv);
        i = parent;
    }
    true
}

pub unsafe fn heappop(heap: MpObj) -> Option<MpObj> {
    let n = objlist::len(heap)?;
    if n == 0 {
        return None;
    }
    let top = objlist::get(heap, 0)?;
    let last = objlist::pop(heap)?;
    if n == 1 {
        return Some(top);
    }
    let _ = objlist::set(heap, 0, last);
    let mut i = 0usize;
    let len = objlist::len(heap)?;
    loop {
        let left = 2 * i + 1;
        let right = left + 1;
        if left >= len {
            break;
        }
        let mut smallest = left;
        if right < len {
            let lv = objlist::get(heap, left)?;
            let rv = objlist::get(heap, right)?;
            if less(rv, lv) == Some(true) {
                smallest = right;
            }
        }
        let iv = objlist::get(heap, i)?;
        let sv = objlist::get(heap, smallest)?;
        if less(sv, iv) != Some(true) {
            break;
        }
        let _ = objlist::set(heap, i, sv);
        let _ = objlist::set(heap, smallest, iv);
        i = smallest;
    }
    Some(top)
}
