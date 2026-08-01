//! bc — bytecode decode helpers (upstream `bc.h` / encoding comments).

use super::bc0;

/// Decode an unsigned varint starting at `code[ip]`; advances `ip`.
pub fn decode_uint(code: &[u8], ip: &mut usize) -> Option<usize> {
    let mut unum: usize = 0;
    let mut shift = 0u32;
    loop {
        if *ip >= code.len() {
            return None;
        }
        let b = code[*ip];
        *ip += 1;
        unum |= ((b & 0x7f) as usize) << shift;
        if b & 0x80 == 0 {
            return Some(unum);
        }
        shift += 7;
        if shift > (usize::BITS) {
            return None;
        }
    }
}

/// Unsigned relative bytecode offset (1 or 2 bytes after opcode).
pub fn decode_uint_offset(code: &[u8], ip: &mut usize) -> Option<usize> {
    if *ip >= code.len() {
        return None;
    }
    let b = code[*ip];
    *ip += 1;
    if b & 0x80 == 0 {
        return Some(b as usize);
    }
    if *ip >= code.len() {
        return None;
    }
    let b2 = code[*ip];
    *ip += 1;
    Some(((b & 0x7f) as usize) | ((b2 as usize) << 7))
}

/// Signed relative bytecode offset.
pub fn decode_sint_offset(code: &[u8], ip: &mut usize) -> Option<isize> {
    if *ip >= code.len() {
        return None;
    }
    let b = code[*ip];
    *ip += 1;
    if b & 0x80 == 0 {
        return Some((b as isize) - 0x40);
    }
    if *ip >= code.len() {
        return None;
    }
    let b2 = code[*ip];
    *ip += 1;
    let u = ((b & 0x7f) as isize) | ((b2 as isize) << 7);
    Some(u - 0x4000)
}

pub use bc0::format;
