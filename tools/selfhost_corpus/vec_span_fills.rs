/* Vec/span fills: `extend_from_slice` (a borrowed &[T] or &[T; N] appended
 * element by element), `extend(std::iter::repeat_n(v, n))` (a counted value
 * generator, not a collection), and `dst[a..b].copy_from_slice(src)` (a range
 * span write). Each lowers to the row's _push loop / a memcpy over the span —
 * the same append the fill table already does for push/extend, with the source
 * a raw slice or a repeat count. Covers both a Vec local and a &mut Vec param
 * receiver (the latter is already the row pointer; the local takes `&`). */
pub struct Sink {
    held: u32,
}

pub fn drain(dst: &mut Vec<u8>, src: &[u8], pad: u8) {
    dst.extend_from_slice(src);
    dst.extend(std::iter::repeat_n(pad, 2));
    let mut hdr = [0u8; 8];
    hdr[2..5].copy_from_slice(src);
    dst.extend_from_slice(&hdr);
}

pub fn count_bytes() -> u32 {
    let mut v: Vec<u8> = Vec::new();
    let data: &[u8] = b"abc";
    v.extend_from_slice(data);
    v.len() as u32
}
