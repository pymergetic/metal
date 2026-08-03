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

// -- generic signed small-int codec (used by `LOAD_CONST_SMALL_INT`, the
// general-value fallback for `emitbc::Writer::load_const_small_int` when a
// literal doesn't fit `LOAD_CONST_SMALL_INT_MULTI`'s -16..47 window) -----
//
// Upstream encodes the signed value directly with a big-endian two's-
// complement varint (`emit_write_bytecode_byte_int` / the `DECODE_SIGNED`-
// style loop in `vm.c`'s `MP_BC_LOAD_CONST_SMALL_INT` case) -- a different
// byte order from `decode_uint`/`mp_decode_uint` above. Metal has no .mpy
// binary-compatibility requirement, so rather than carry a *second*
// varint byte order just for this one opcode, `LOAD_CONST_SMALL_INT`
// zigzag-maps the signed value onto the unsigned domain and reuses the
// already-tested `decode_uint`/its encoder counterpart in `emitbc.rs` --
// one codec, not two, for the same "let's not repeat parse.rs's
// chunk-allocator sin against DRY" reason.

/// Map a signed value onto the unsigned domain (small magnitude either
/// sign -> small encoded value): `0,-1,1,-2,2,... -> 0,1,2,3,4,...`.
pub fn zigzag_encode(v: isize) -> usize {
    ((v << 1) ^ (v >> (isize::BITS as usize - 1))) as usize
}

/// Inverse of [`zigzag_encode`].
pub fn zigzag_decode(u: usize) -> isize {
    ((u >> 1) as isize) ^ -((u & 1) as isize)
}
