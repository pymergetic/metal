//! objdict — small open map (linear probe, Metal alloc).

use crate::upy::py::malloc;
use crate::upy::py::obj::{self, MpObj, MpObjBase};
use crate::upy::py::objects::{obj_eq, TypeDesc, TYPE_DICT};

const EMPTY: MpObj = obj::OBJ_NULL;

unsafe fn key_eq(a: MpObj, b: MpObj) -> bool {
    obj_eq(a, b)
}

#[repr(C)]
pub struct Dict {
    pub base: MpObjBase,
    pub alloc: usize,
    pub used: usize,
    pub keys: *mut MpObj,
    pub values: *mut MpObj,
}

pub unsafe fn new(cap: usize) -> MpObj {
    let cap = if cap < 4 { 4 } else { cap };
    let p = malloc::m_malloc(core::mem::size_of::<Dict>()) as *mut Dict;
    if p.is_null() {
        return obj::OBJ_NULL;
    }
    let keys = malloc::m_malloc0(cap * core::mem::size_of::<MpObj>()) as *mut MpObj;
    let values = malloc::m_malloc0(cap * core::mem::size_of::<MpObj>()) as *mut MpObj;
    if keys.is_null() || values.is_null() {
        if !keys.is_null() {
            malloc::m_free(keys as *mut u8);
        }
        if !values.is_null() {
            malloc::m_free(values as *mut u8);
        }
        malloc::m_free(p as *mut u8);
        return obj::OBJ_NULL;
    }
    (*p).base = MpObjBase::new((&TYPE_DICT as *const TypeDesc) as *const u8);
    (*p).alloc = cap;
    (*p).used = 0;
    (*p).keys = keys;
    (*p).values = values;
    p as MpObj
}

unsafe fn as_mut(o: MpObj) -> Option<*mut Dict> {
    if !obj::is_obj(o) {
        return None;
    }
    let p = o as *mut Dict;
    if (*p).base.type_ptr != (&TYPE_DICT as *const TypeDesc) as *const u8 {
        return None;
    }
    Some(p)
}

pub unsafe fn len(o: MpObj) -> Option<usize> {
    as_mut(o).map(|p| (*p).used)
}

pub unsafe fn store(o: MpObj, key: MpObj, val: MpObj) -> bool {
    let Some(p) = as_mut(o) else {
        return false;
    };
    if key == EMPTY {
        return false;
    }
    // linear scan upsert (str/qstr by content)
    for i in 0..(*p).alloc {
        let k = *(*p).keys.add(i);
        if k != EMPTY && key_eq(k, key) {
            *(*p).values.add(i) = val;
            return true;
        }
        if k == EMPTY {
            *(*p).keys.add(i) = key;
            *(*p).values.add(i) = val;
            (*p).used += 1;
            return true;
        }
    }
    false
}

pub unsafe fn load(o: MpObj, key: MpObj) -> Option<MpObj> {
    let p = as_mut(o)?;
    for i in 0..(*p).alloc {
        let k = *(*p).keys.add(i);
        if k != EMPTY && key_eq(k, key) {
            return Some(*(*p).values.add(i));
        }
    }
    None
}

/// Call `f(key, value)` for each occupied slot. Stops early if `f` returns false.
pub unsafe fn for_each<F>(o: MpObj, mut f: F) -> bool
where
    F: FnMut(MpObj, MpObj) -> bool,
{
    let Some(p) = as_mut(o) else {
        return false;
    };
    for i in 0..(*p).alloc {
        let k = *(*p).keys.add(i);
        if k != EMPTY {
            if !f(k, *(*p).values.add(i)) {
                return false;
            }
        }
    }
    true
}

pub unsafe fn free(o: MpObj) {
    if let Some(p) = as_mut(o) {
        malloc::m_free((*p).keys as *mut u8);
        malloc::m_free((*p).values as *mut u8);
        malloc::m_free(p as *mut u8);
    }
}
