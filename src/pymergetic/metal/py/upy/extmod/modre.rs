//! modre — compile/match for a finished literal + `.` / `.*` / `^$` subset.

use crate::upy::py::malloc;
use crate::upy::py::obj::{self, MpObj, MpObjBase};
use crate::upy::py::objects::{objbool, objstr, TypeDesc, TypeKind};
use crate::upy::py::qstrdefs;

static TYPE_RE: TypeDesc = TypeDesc {
    kind: TypeKind::Object, // reuse Object kind; identity via type_ptr
    is_user: false,
    _pad: 0,
    magic: 0,
    name: qstrdefs::QSTR_OBJECT,
};

#[repr(C)]
struct RePat {
    base: MpObjBase,
    len: usize,
    data: *mut u8,
}

unsafe fn as_pat(o: MpObj) -> Option<*mut RePat> {
    if !obj::is_obj(o) {
        return None;
    }
    let p = o as *mut RePat;
    if (*p).base.type_ptr != (&TYPE_RE as *const TypeDesc) as *const u8 {
        return None;
    }
    Some(p)
}

pub unsafe fn compile(pattern: &[u8]) -> Option<MpObj> {
    let p = malloc::m_malloc(core::mem::size_of::<RePat>()) as *mut RePat;
    if p.is_null() {
        return None;
    }
    let data = malloc::m_malloc(pattern.len() + 1);
    if data.is_null() {
        malloc::m_free(p as *mut u8);
        return None;
    }
    core::ptr::copy_nonoverlapping(pattern.as_ptr(), data, pattern.len());
    *data.add(pattern.len()) = 0;
    (*p).base = MpObjBase::new((&TYPE_RE as *const TypeDesc) as *const u8);
    (*p).len = pattern.len();
    (*p).data = data;
    Some(p as MpObj)
}

pub unsafe fn compile_obj(o: MpObj) -> Option<MpObj> {
    compile(objstr::as_bytes(o)?)
}

fn match_here(pat: &[u8], text: &[u8]) -> bool {
    let mut pi = 0usize;
    let mut ti = 0usize;
    while pi < pat.len() {
        if pi + 1 < pat.len() && pat[pi + 1] == b'*' {
            let c = pat[pi];
            pi += 2;
            // greedy .* / c*
            loop {
                if match_here(&pat[pi..], &text[ti..]) {
                    return true;
                }
                if ti >= text.len() {
                    return false;
                }
                if c != b'.' && text[ti] != c {
                    return false;
                }
                ti += 1;
            }
        }
        if pat[pi] == b'$' && pi + 1 == pat.len() {
            return ti == text.len();
        }
        if ti >= text.len() {
            return false;
        }
        if pat[pi] != b'.' && pat[pi] != text[ti] {
            return false;
        }
        pi += 1;
        ti += 1;
    }
    ti == text.len()
}

fn match_full(pat: &[u8], text: &[u8]) -> bool {
    // re.match semantics: anchored at start (optional explicit ^).
    let pat = if pat.first() == Some(&b'^') {
        &pat[1..]
    } else {
        pat
    };
    match_here(pat, text)
}

pub unsafe fn match_pat(pat_obj: MpObj, text: &[u8]) -> Option<bool> {
    let p = as_pat(pat_obj)?;
    let pat = core::slice::from_raw_parts((*p).data, (*p).len);
    Some(match_full(pat, text))
}

pub unsafe fn match_obj(pat_obj: MpObj, text_obj: MpObj) -> Option<MpObj> {
    let text = objstr::as_bytes(text_obj)?;
    let ok = match_pat(pat_obj, text)?;
    Some(objbool::get(ok))
}

/// One-shot: compile + match.
pub unsafe fn match_str(pattern: &[u8], text: &[u8]) -> bool {
    match_full(pattern, text)
}

pub unsafe fn free(o: MpObj) {
    if let Some(p) = as_pat(o) {
        malloc::m_free((*p).data);
        malloc::m_free(p as *mut u8);
    }
}
