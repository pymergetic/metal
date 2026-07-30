//! Byte-count formatting — binary prefixes (KiB/MiB/GiB/TiB).
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code, non_camel_case_types)]

use pymergetic_metal_rt as _;

/// "1023 TiB" + NUL
pub const PM_METAL_UTIL_SIZE_FORMAT_MAX: usize = 16;

/// Format `bytes` with binary prefixes. Returns length, or -1 on error.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_size_format(
    out: *mut u8,
    cap: usize,
    bytes: u64,
) -> i32 {
    if out.is_null() || cap == 0 {
        return -1;
    }
    let mut buf = [0u8; PM_METAL_UTIL_SIZE_FORMAT_MAX];
    let n = format_into(&mut buf, bytes);
    if n + 1 > cap {
        return -1;
    }
    core::ptr::copy_nonoverlapping(buf.as_ptr(), out, n);
    *out.add(n) = 0;
    n as i32
}

/// Format as `"<bytes> (<human>)"`, e.g. `"92946432 (88 MiB)"`.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_size_format_bytes(
    out: *mut u8,
    cap: usize,
    bytes: u64,
) -> i32 {
    if out.is_null() || cap == 0 {
        return -1;
    }
    let mut human = [0u8; PM_METAL_UTIL_SIZE_FORMAT_MAX];
    let hn = format_into(&mut human, bytes);
    let mut tmp = [0u8; 48];
    let mut i = 0usize;
    let mut v = bytes;
    if v == 0 {
        tmp[0] = b'0';
        i = 1;
    } else {
        let mut digs = [0u8; 20];
        let mut d = 0usize;
        while v > 0 && d < digs.len() {
            digs[d] = b'0' + (v % 10) as u8;
            v /= 10;
            d += 1;
        }
        while d > 0 {
            d -= 1;
            tmp[i] = digs[d];
            i += 1;
        }
    }
    if i + 2 + hn + 1 >= tmp.len() {
        return -1;
    }
    tmp[i] = b' ';
    i += 1;
    tmp[i] = b'(';
    i += 1;
    tmp[i..i + hn].copy_from_slice(&human[..hn]);
    i += hn;
    tmp[i] = b')';
    i += 1;
    if i + 1 > cap {
        return -1;
    }
    core::ptr::copy_nonoverlapping(tmp.as_ptr(), out, i);
    *out.add(i) = 0;
    i as i32
}

fn format_into(out: &mut [u8], bytes: u64) -> usize {
    const UNITS: &[(u64, &[u8])] = &[
        (1024 * 1024 * 1024 * 1024, b" TiB"),
        (1024 * 1024 * 1024, b" GiB"),
        (1024 * 1024, b" MiB"),
        (1024, b" KiB"),
    ];
    for &(unit, suf) in UNITS {
        if bytes >= unit {
            return write_u64_suf(out, bytes / unit, suf);
        }
    }
    write_u64_suf(out, bytes, b" B")
}

fn write_u64_suf(out: &mut [u8], mut v: u64, suf: &[u8]) -> usize {
    let mut digs = [0u8; 20];
    let mut d = 0usize;
    if v == 0 {
        digs[0] = b'0';
        d = 1;
    } else {
        while v > 0 && d < digs.len() {
            digs[d] = b'0' + (v % 10) as u8;
            v /= 10;
            d += 1;
        }
    }
    let mut i = 0usize;
    while d > 0 && i < out.len() {
        d -= 1;
        out[i] = digs[d];
        i += 1;
    }
    for &b in suf {
        if i >= out.len() {
            break;
        }
        out[i] = b;
        i += 1;
    }
    i
}
