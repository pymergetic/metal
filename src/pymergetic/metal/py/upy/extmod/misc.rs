//! extmod/misc — small shared helpers for keep-list modules.

/// Append ASCII digits of `n` (non-negative) into `out`.
pub fn push_u64(out: &mut [u8], pos: &mut usize, mut n: u64) {
    let mut tmp = [0u8; 20];
    let mut i = 0usize;
    if n == 0 {
        push_byte(out, pos, b'0');
        return;
    }
    while n > 0 && i < tmp.len() {
        tmp[i] = b'0' + (n % 10) as u8;
        n /= 10;
        i += 1;
    }
    while i > 0 {
        i -= 1;
        push_byte(out, pos, tmp[i]);
    }
}

pub fn push_byte(out: &mut [u8], pos: &mut usize, b: u8) {
    if *pos < out.len() {
        out[*pos] = b;
        *pos += 1;
    }
}

pub fn push_bytes(out: &mut [u8], pos: &mut usize, s: &[u8]) {
    for &b in s {
        push_byte(out, pos, b);
    }
}

/// Parse unsigned decimal; returns (value, bytes_consumed).
pub fn parse_u64(s: &[u8]) -> Option<(u64, usize)> {
    if s.is_empty() || !(s[0] as char).is_ascii_digit() {
        return None;
    }
    let mut n = 0u64;
    let mut i = 0usize;
    while i < s.len() && (s[i] as char).is_ascii_digit() {
        n = n.saturating_mul(10).saturating_add((s[i] - b'0') as u64);
        i += 1;
    }
    Some((n, i))
}
