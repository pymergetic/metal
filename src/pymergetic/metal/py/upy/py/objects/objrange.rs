//! objrange — Python range(start, stop, step) as heap object.

use crate::upy::py::malloc;
use crate::upy::py::obj::{self, MpObj, MpObjBase};
use crate::upy::py::objects::{TypeDesc, TYPE_RANGE};

#[repr(C)]
pub struct Range {
    pub base: MpObjBase,
    pub start: isize,
    pub stop: isize,
    pub step: isize,
}

pub unsafe fn new(start: isize, stop: isize, step: isize) -> MpObj {
    if step == 0 {
        return obj::OBJ_NULL;
    }
    let p = malloc::m_malloc(core::mem::size_of::<Range>()) as *mut Range;
    if p.is_null() {
        return obj::OBJ_NULL;
    }
    (*p).base = MpObjBase::new((&TYPE_RANGE as *const TypeDesc) as *const u8);
    (*p).start = start;
    (*p).stop = stop;
    (*p).step = step;
    p as MpObj
}

pub unsafe fn len(o: MpObj) -> Option<usize> {
    let p = as_ref(o)?;
    let start = (*p).start;
    let stop = (*p).stop;
    let step = (*p).step;
    if step > 0 {
        if start >= stop {
            Some(0)
        } else {
            Some(((stop - start + step - 1) / step) as usize)
        }
    } else if start <= stop {
        Some(0)
    } else {
        Some(((start - stop + (-step) - 1) / (-step)) as usize)
    }
}

pub unsafe fn get(o: MpObj, i: usize) -> Option<isize> {
    let p = as_ref(o)?;
    let n = len(o)?;
    if i >= n {
        return None;
    }
    Some((*p).start + (i as isize) * (*p).step)
}

unsafe fn as_ref(o: MpObj) -> Option<*const Range> {
    if !obj::is_obj(o) {
        return None;
    }
    let p = o as *const Range;
    if (*p).base.type_ptr != (&TYPE_RANGE as *const TypeDesc) as *const u8 {
        return None;
    }
    Some(p)
}

pub unsafe fn free(o: MpObj) {
    if as_ref(o).is_some() {
        malloc::m_free(o as *mut u8);
    }
}
