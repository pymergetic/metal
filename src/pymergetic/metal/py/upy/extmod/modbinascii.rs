//! modbinascii — hexlify / unhexlify / b2a_base64 / a2b_base64.

use crate::upy::py::obj::{self, MpObj};
use crate::upy::py::objects::objstr;

const HEX: &[u8; 16] = b"0123456789abcdef";

pub unsafe fn hexlify(data: &[u8]) -> Option<MpObj> {
    let mut out = [0u8; 512];
    if data.len() * 2 > out.len() {
        return None;
    }
    for (i, &b) in data.iter().enumerate() {
        out[i * 2] = HEX[(b >> 4) as usize];
        out[i * 2 + 1] = HEX[(b & 0xf) as usize];
    }
    Some(objstr::new(&out[..data.len() * 2]))
}

pub unsafe fn hexlify_obj(o: MpObj) -> Option<MpObj> {
    hexlify(objstr::as_bytes(o)?)
}

fn hex_nibble(c: u8) -> Option<u8> {
    match c {
        b'0'..=b'9' => Some(c - b'0'),
        b'a'..=b'f' => Some(c - b'a' + 10),
        b'A'..=b'F' => Some(c - b'A' + 10),
        _ => None,
    }
}

pub unsafe fn unhexlify(data: &[u8]) -> Option<MpObj> {
    if data.len() % 2 != 0 {
        return None;
    }
    let mut out = [0u8; 256];
    let n = data.len() / 2;
    if n > out.len() {
        return None;
    }
    for i in 0..n {
        let hi = hex_nibble(data[i * 2])?;
        let lo = hex_nibble(data[i * 2 + 1])?;
        out[i] = (hi << 4) | lo;
    }
    Some(objstr::new(&out[..n]))
}

pub unsafe fn unhexlify_obj(o: MpObj) -> Option<MpObj> {
    unhexlify(objstr::as_bytes(o)?)
}

const B64: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

pub unsafe fn b2a_base64(data: &[u8]) -> Option<MpObj> {
    let out_len = ((data.len() + 2) / 3) * 4;
    let mut out = [0u8; 512];
    if out_len > out.len() {
        return None;
    }
    let mut o = 0usize;
    let mut i = 0usize;
    while i + 3 <= data.len() {
        let n = ((data[i] as u32) << 16) | ((data[i + 1] as u32) << 8) | (data[i + 2] as u32);
        out[o] = B64[((n >> 18) & 63) as usize];
        out[o + 1] = B64[((n >> 12) & 63) as usize];
        out[o + 2] = B64[((n >> 6) & 63) as usize];
        out[o + 3] = B64[(n & 63) as usize];
        o += 4;
        i += 3;
    }
    let rem = data.len() - i;
    if rem == 1 {
        let n = (data[i] as u32) << 16;
        out[o] = B64[((n >> 18) & 63) as usize];
        out[o + 1] = B64[((n >> 12) & 63) as usize];
        out[o + 2] = b'=';
        out[o + 3] = b'=';
        o += 4;
    } else if rem == 2 {
        let n = ((data[i] as u32) << 16) | ((data[i + 1] as u32) << 8);
        out[o] = B64[((n >> 18) & 63) as usize];
        out[o + 1] = B64[((n >> 12) & 63) as usize];
        out[o + 2] = B64[((n >> 6) & 63) as usize];
        out[o + 3] = b'=';
        o += 4;
    }
    let _ = obj::OBJ_NULL;
    Some(objstr::new(&out[..o]))
}

pub unsafe fn b2a_base64_obj(o: MpObj) -> Option<MpObj> {
    b2a_base64(objstr::as_bytes(o)?)
}

fn b64_val(c: u8) -> Option<u8> {
    match c {
        b'A'..=b'Z' => Some(c - b'A'),
        b'a'..=b'z' => Some(c - b'a' + 26),
        b'0'..=b'9' => Some(c - b'0' + 52),
        b'+' => Some(62),
        b'/' => Some(63),
        _ => None,
    }
}

pub unsafe fn a2b_base64(data: &[u8]) -> Option<MpObj> {
    if data.len() % 4 != 0 {
        return None;
    }
    let mut out = [0u8; 384];
    let mut o = 0usize;
    let mut i = 0usize;
    while i < data.len() {
        let a = data[i];
        let b = data[i + 1];
        let c = data[i + 2];
        let d = data[i + 3];
        let va = b64_val(a)?;
        let vb = b64_val(b)?;
        let vc = if c == b'=' { 0 } else { b64_val(c)? };
        let vd = if d == b'=' { 0 } else { b64_val(d)? };
        let n = ((va as u32) << 18) | ((vb as u32) << 12) | ((vc as u32) << 6) | (vd as u32);
        if o >= out.len() {
            return None;
        }
        out[o] = ((n >> 16) & 0xff) as u8;
        o += 1;
        if c != b'=' {
            if o >= out.len() {
                return None;
            }
            out[o] = ((n >> 8) & 0xff) as u8;
            o += 1;
        }
        if d != b'=' {
            if o >= out.len() {
                return None;
            }
            out[o] = (n & 0xff) as u8;
            o += 1;
        }
        i += 4;
    }
    Some(objstr::new(&out[..o]))
}

pub unsafe fn a2b_base64_obj(o: MpObj) -> Option<MpObj> {
    a2b_base64(objstr::as_bytes(o)?)
}
