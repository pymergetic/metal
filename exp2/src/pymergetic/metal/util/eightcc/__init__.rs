//! EightCC tags — semantic BE u64 + LE wire bytes (inline LE load/store).
#![cfg_attr(target_os = "none", no_std)]
#![allow(dead_code, non_camel_case_types)]

use pymergetic_metal_rt as _;


const LEN: usize = 8;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct pm_metal_util_eightcc_t {
    pub v: u64,
}

#[inline]
fn load_u64_le(p: *const u8) -> u64 {
    unsafe {
        u64::from_le_bytes([
            *p,
            *p.add(1),
            *p.add(2),
            *p.add(3),
            *p.add(4),
            *p.add(5),
            *p.add(6),
            *p.add(7),
        ])
    }
}

#[inline]
fn store_u64_le(p: *mut u8, v: u64) {
    let b = v.to_le_bytes();
    unsafe {
        for i in 0..8 {
            *p.add(i) = b[i];
        }
    }
}

#[inline]
fn pack_be8(bytes: [u8; 8]) -> u64 {
    ((bytes[0] as u64) << 56)
        | ((bytes[1] as u64) << 48)
        | ((bytes[2] as u64) << 40)
        | ((bytes[3] as u64) << 32)
        | ((bytes[4] as u64) << 24)
        | ((bytes[5] as u64) << 16)
        | ((bytes[6] as u64) << 8)
        | (bytes[7] as u64)
}

fn cstr_len(s: *const u8) -> usize {
    if s.is_null() {
        return 0;
    }
    let mut n = 0usize;
    unsafe {
        while *s.add(n) != 0 {
            n += 1;
        }
    }
    n
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_eightcc_from_u64(out: *mut pm_metal_util_eightcc_t, v: u64) {
    if out.is_null() {
        return;
    }
    (*out).v = v;
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_eightcc_to_u64(tag: *const pm_metal_util_eightcc_t) -> u64 {
    if tag.is_null() {
        0
    } else {
        (*tag).v
    }
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_eightcc_from_wire_bytes(
    bytes: *const u8,
    out: *mut pm_metal_util_eightcc_t,
) {
    if bytes.is_null() || out.is_null() {
        return;
    }
    (*out).v = load_u64_le(bytes);
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_eightcc_to_wire_bytes(
    tag: *const pm_metal_util_eightcc_t,
    out: *mut u8,
) {
    if tag.is_null() || out.is_null() {
        return;
    }
    store_u64_le(out, (*tag).v);
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_eightcc_to_bytes(
    tag: *const pm_metal_util_eightcc_t,
    out: *mut u8,
) {
    if tag.is_null() || out.is_null() {
        return;
    }
    let v = (*tag).v;
    *out = (v >> 56) as u8;
    *out.add(1) = (v >> 48) as u8;
    *out.add(2) = (v >> 40) as u8;
    *out.add(3) = (v >> 32) as u8;
    *out.add(4) = (v >> 24) as u8;
    *out.add(5) = (v >> 16) as u8;
    *out.add(6) = (v >> 8) as u8;
    *out.add(7) = v as u8;
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_eightcc_from_bytes(
    bytes: *const u8,
    out: *mut pm_metal_util_eightcc_t,
) {
    if bytes.is_null() || out.is_null() {
        return;
    }
    let b = [
        *bytes,
        *bytes.add(1),
        *bytes.add(2),
        *bytes.add(3),
        *bytes.add(4),
        *bytes.add(5),
        *bytes.add(6),
        *bytes.add(7),
    ];
    (*out).v = pack_be8(b);
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_eightcc_to_string(
    tag: *const pm_metal_util_eightcc_t,
    out: *mut u8,
) -> i32 {
    if tag.is_null() || out.is_null() {
        return -1;
    }
    store_u64_le(out, (*tag).v);
    *out.add(LEN) = 0;
    0
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_eightcc_from_string(
    s: *const u8,
    out: *mut pm_metal_util_eightcc_t,
) -> i32 {
    if s.is_null() || out.is_null() || cstr_len(s) != LEN {
        return -1;
    }
    (*out).v = load_u64_le(s);
    0
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_eightcc_label(magic: u64, out: *mut u8) -> i32 {
    if out.is_null() {
        return -1;
    }
    let tag = pm_metal_util_eightcc_t { v: magic };
    pm_metal_util_eightcc_to_string(&tag, out)
}
