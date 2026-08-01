//! modjson — dumps/loads for None/bool/int/float/str/list/dict (finished subset).

use crate::upy::extmod::misc;
use crate::upy::py::malloc;
use crate::upy::py::obj::{self, MpObj};
use crate::upy::py::objects::{
    kind_of, objbool, objdict, objfloat, objint, objlist, objnone, objstr, TypeKind,
};
use crate::upy::py::qstr;

struct Buf {
    data: *mut u8,
    len: usize,
    cap: usize,
}

impl Buf {
    unsafe fn new() -> Option<Self> {
        let cap = 64usize;
        let data = malloc::m_malloc(cap);
        if data.is_null() {
            return None;
        }
        Some(Self {
            data,
            len: 0,
            cap,
        })
    }

    unsafe fn ensure(&mut self, extra: usize) -> bool {
        if self.len + extra <= self.cap {
            return true;
        }
        let mut ncap = self.cap;
        while self.len + extra > ncap {
            ncap = ncap.saturating_mul(2).max(self.len + extra);
        }
        let p = malloc::m_realloc(self.data, ncap);
        if p.is_null() {
            return false;
        }
        self.data = p;
        self.cap = ncap;
        true
    }

    unsafe fn push(&mut self, b: u8) -> bool {
        if !self.ensure(1) {
            return false;
        }
        *self.data.add(self.len) = b;
        self.len += 1;
        true
    }

    unsafe fn push_slice(&mut self, s: &[u8]) -> bool {
        if !self.ensure(s.len()) {
            return false;
        }
        core::ptr::copy_nonoverlapping(s.as_ptr(), self.data.add(self.len), s.len());
        self.len += s.len();
        true
    }

    unsafe fn into_str(self) -> MpObj {
        let slice = core::slice::from_raw_parts(self.data, self.len);
        let o = objstr::new(slice);
        malloc::m_free(self.data);
        o
    }

    unsafe fn drop(self) {
        malloc::m_free(self.data);
    }
}

unsafe fn dumps_into(o: MpObj, out: &mut Buf, depth: usize) -> bool {
    if depth > 32 {
        return false;
    }
    match kind_of(o) {
        Some(TypeKind::None) => out.push_slice(b"null"),
        Some(TypeKind::Bool) => {
            if objbool::value(o) == Some(true) {
                out.push_slice(b"true")
            } else {
                out.push_slice(b"false")
            }
        }
        Some(TypeKind::Int) => {
            let Some(v) = objint::as_isize(o) else {
                return false;
            };
            let mut tmp = [0u8; 24];
            let mut pos = 0usize;
            if v < 0 {
                misc::push_byte(&mut tmp, &mut pos, b'-');
                misc::push_u64(&mut tmp, &mut pos, (-v) as u64);
            } else {
                misc::push_u64(&mut tmp, &mut pos, v as u64);
            }
            out.push_slice(&tmp[..pos])
        }
        Some(TypeKind::Float) => {
            let Some(v) = objfloat::value(o) else {
                return false;
            };
            if !v.is_finite() {
                return out.push_slice(b"null");
            }
            let mut tmp = [0u8; 48];
            let mut pos = 0usize;
            let neg = v < 0.0;
            let a = if neg { -v } else { v };
            if neg {
                misc::push_byte(&mut tmp, &mut pos, b'-');
            }
            let ip = a as u64;
            misc::push_u64(&mut tmp, &mut pos, ip);
            misc::push_byte(&mut tmp, &mut pos, b'.');
            let frac = ((a - (ip as f64)) * 1_000_000.0 + 0.5) as u64;
            // six digits, zero-padded
            let mut f = frac;
            let mut digs = [0u8; 6];
            for i in (0..6).rev() {
                digs[i] = b'0' + (f % 10) as u8;
                f /= 10;
            }
            for &d in &digs {
                misc::push_byte(&mut tmp, &mut pos, d);
            }
            out.push_slice(&tmp[..pos])
        }
        Some(TypeKind::Str) => {
            let Some(bytes) = objstr::as_bytes(o) else {
                return false;
            };
            if !out.push(b'"') {
                return false;
            }
            for &b in bytes {
                match b {
                    b'"' | b'\\' => {
                        if !out.push(b'\\') || !out.push(b) {
                            return false;
                        }
                    }
                    b'\n' => {
                        if !out.push_slice(b"\\n") {
                            return false;
                        }
                    }
                    b'\r' => {
                        if !out.push_slice(b"\\r") {
                            return false;
                        }
                    }
                    b'\t' => {
                        if !out.push_slice(b"\\t") {
                            return false;
                        }
                    }
                    _ => {
                        if !out.push(b) {
                            return false;
                        }
                    }
                }
            }
            out.push(b'"')
        }
        Some(TypeKind::List) | Some(TypeKind::Tuple) => {
            if !out.push(b'[') {
                return false;
            }
            let Some(n) = objlist::len(o).or_else(|| {
                // tuple uses same layout? check tuple separately
                crate::upy::py::objects::objtuple::len(o)
            }) else {
                return false;
            };
            for i in 0..n {
                if i > 0 && !out.push(b',') {
                    return false;
                }
                let item = if kind_of(o) == Some(TypeKind::List) {
                    objlist::get(o, i)
                } else {
                    crate::upy::py::objects::objtuple::get(o, i)
                };
                let Some(item) = item else {
                    return false;
                };
                if !dumps_into(item, out, depth + 1) {
                    return false;
                }
            }
            out.push(b']')
        }
        Some(TypeKind::Dict) => {
            if !out.push(b'{') {
                return false;
            }
            let mut first = true;
            let ok = objdict::for_each(o, |k, v| {
                if !first {
                    if !out.push(b',') {
                        return false;
                    }
                }
                first = false;
                // keys: str or qstr
                let key_bytes: Option<&[u8]> = if obj::is_qstr(k) {
                    Some(qstr::str(obj::qstr_value(k)))
                } else {
                    objstr::as_bytes(k)
                };
                let Some(kb) = key_bytes else {
                    return false;
                };
                if !out.push(b'"') {
                    return false;
                }
                if !out.push_slice(kb) {
                    return false;
                }
                if !out.push_slice(b"\":") {
                    return false;
                }
                dumps_into(v, out, depth + 1)
            });
            if !ok {
                return false;
            }
            out.push(b'}')
        }
        _ => false,
    }
}

pub unsafe fn dumps(o: MpObj) -> Option<MpObj> {
    let mut buf = Buf::new()?;
    if !dumps_into(o, &mut buf, 0) {
        buf.drop();
        return None;
    }
    Some(buf.into_str())
}

struct Parser<'a> {
    s: &'a [u8],
    i: usize,
}

impl<'a> Parser<'a> {
    fn peek(&self) -> Option<u8> {
        self.s.get(self.i).copied()
    }
    fn bump(&mut self) {
        self.i += 1;
    }
    fn skip_ws(&mut self) {
        while matches!(self.peek(), Some(b' ' | b'\t' | b'\n' | b'\r')) {
            self.bump();
        }
    }
}

unsafe fn parse_value(p: &mut Parser<'_>) -> Option<MpObj> {
    p.skip_ws();
    match p.peek()? {
        b'n' => {
            if p.s.get(p.i..p.i + 4) == Some(b"null") {
                p.i += 4;
                return Some(objnone::get());
            }
            None
        }
        b't' => {
            if p.s.get(p.i..p.i + 4) == Some(b"true") {
                p.i += 4;
                return Some(objbool::get(true));
            }
            None
        }
        b'f' => {
            if p.s.get(p.i..p.i + 5) == Some(b"false") {
                p.i += 5;
                return Some(objbool::get(false));
            }
            None
        }
        b'"' => parse_string(p),
        b'[' => parse_array(p),
        b'{' => parse_object(p),
        b'-' | b'0'..=b'9' => parse_number(p),
        _ => None,
    }
}

unsafe fn parse_string(p: &mut Parser<'_>) -> Option<MpObj> {
    p.bump(); // "
    let mut tmp = [0u8; 256];
    let mut n = 0usize;
    while let Some(b) = p.peek() {
        p.bump();
        if b == b'"' {
            return Some(objstr::new(&tmp[..n]));
        }
        let c = if b == b'\\' {
            match p.peek()? {
                b'"' => {
                    p.bump();
                    b'"'
                }
                b'\\' => {
                    p.bump();
                    b'\\'
                }
                b'n' => {
                    p.bump();
                    b'\n'
                }
                b'r' => {
                    p.bump();
                    b'\r'
                }
                b't' => {
                    p.bump();
                    b'\t'
                }
                _ => return None,
            }
        } else {
            b
        };
        if n >= tmp.len() {
            return None;
        }
        tmp[n] = c;
        n += 1;
    }
    None
}

unsafe fn parse_array(p: &mut Parser<'_>) -> Option<MpObj> {
    p.bump(); // [
    let lst = objlist::new(0);
    if lst == obj::OBJ_NULL {
        return None;
    }
    p.skip_ws();
    if p.peek() == Some(b']') {
        p.bump();
        return Some(lst);
    }
    loop {
        let v = parse_value(p)?;
        if !objlist::append(lst, v) {
            return None;
        }
        p.skip_ws();
        match p.peek()? {
            b']' => {
                p.bump();
                return Some(lst);
            }
            b',' => {
                p.bump();
                p.skip_ws();
            }
            _ => return None,
        }
    }
}

unsafe fn parse_object(p: &mut Parser<'_>) -> Option<MpObj> {
    p.bump(); // {
    let d = objdict::new(8);
    if d == obj::OBJ_NULL {
        return None;
    }
    p.skip_ws();
    if p.peek() == Some(b'}') {
        p.bump();
        return Some(d);
    }
    loop {
        p.skip_ws();
        let key = parse_string(p)?;
        p.skip_ws();
        if p.peek() != Some(b':') {
            return None;
        }
        p.bump();
        let val = parse_value(p)?;
        if !objdict::store(d, key, val) {
            return None;
        }
        p.skip_ws();
        match p.peek()? {
            b'}' => {
                p.bump();
                return Some(d);
            }
            b',' => {
                p.bump();
            }
            _ => return None,
        }
    }
}

unsafe fn parse_number(p: &mut Parser<'_>) -> Option<MpObj> {
    let start = p.i;
    let neg = if p.peek() == Some(b'-') {
        p.bump();
        true
    } else {
        false
    };
    let (ip, n) = misc::parse_u64(&p.s[p.i..])?;
    p.i += n;
    if p.peek() == Some(b'.') || p.peek() == Some(b'e') || p.peek() == Some(b'E') {
        // float path: consume rest of number lexeme and parse simply
        p.bump(); // .
        let mut frac = 0u64;
        let mut places = 0u32;
        while matches!(p.peek(), Some(b'0'..=b'9')) {
            if places < 6 {
                frac = frac * 10 + (p.peek().unwrap() - b'0') as u64;
                places += 1;
            }
            p.bump();
        }
        while places < 6 {
            frac *= 10;
            places += 1;
        }
        let mut v = (ip as f64) + (frac as f64) / 1_000_000.0;
        if neg {
            v = -v;
        }
        let _ = start;
        return Some(objfloat::new(v));
    }
    let mut v = ip as isize;
    if neg {
        v = -v;
    }
    Some(objint::from_isize(v))
}

pub unsafe fn loads(bytes: &[u8]) -> Option<MpObj> {
    let mut p = Parser { s: bytes, i: 0 };
    let v = parse_value(&mut p)?;
    p.skip_ws();
    if p.i != p.s.len() {
        return None;
    }
    Some(v)
}

pub unsafe fn loads_obj(o: MpObj) -> Option<MpObj> {
    let bytes = objstr::as_bytes(o)?;
    loads(bytes)
}
