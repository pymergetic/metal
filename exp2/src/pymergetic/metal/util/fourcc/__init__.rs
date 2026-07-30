//! FourCC tags — semantic BE u32 + LE wire bytes (inline LE load/store).
#![cfg_attr(target_os = "none", no_std)]
#![allow(dead_code, non_camel_case_types)]

use pymergetic_metal_rt as _;


const LEN: usize = 4;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct pm_metal_util_fourcc_t {
    pub v: u32,
}

#[inline]
fn load_u32_le(p: *const u8) -> u32 {
    unsafe {
        u32::from_le_bytes([*p, *p.add(1), *p.add(2), *p.add(3)])
    }
}

#[inline]
fn store_u32_le(p: *mut u8, v: u32) {
    let b = v.to_le_bytes();
    unsafe {
        *p = b[0];
        *p.add(1) = b[1];
        *p.add(2) = b[2];
        *p.add(3) = b[3];
    }
}

#[inline]
fn pack_be(a: u8, b: u8, c: u8, d: u8) -> u32 {
    ((a as u32) << 24) | ((b as u32) << 16) | ((c as u32) << 8) | (d as u32)
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
pub unsafe extern "C" fn pm_metal_util_fourcc_from_u32(out: *mut pm_metal_util_fourcc_t, v: u32) {
    if out.is_null() {
        return;
    }
    (*out).v = v;
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_fourcc_to_u32(tag: *const pm_metal_util_fourcc_t) -> u32 {
    if tag.is_null() {
        0
    } else {
        (*tag).v
    }
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_fourcc_from_wire_bytes(
    bytes: *const u8,
    out: *mut pm_metal_util_fourcc_t,
) {
    if bytes.is_null() || out.is_null() {
        return;
    }
    (*out).v = load_u32_le(bytes);
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_fourcc_to_wire_bytes(
    tag: *const pm_metal_util_fourcc_t,
    out: *mut u8,
) {
    if tag.is_null() || out.is_null() {
        return;
    }
    store_u32_le(out, (*tag).v);
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_fourcc_to_bytes(
    tag: *const pm_metal_util_fourcc_t,
    out: *mut u8,
) {
    if tag.is_null() || out.is_null() {
        return;
    }
    let v = (*tag).v;
    *out = (v >> 24) as u8;
    *out.add(1) = (v >> 16) as u8;
    *out.add(2) = (v >> 8) as u8;
    *out.add(3) = v as u8;
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_fourcc_from_bytes(
    bytes: *const u8,
    out: *mut pm_metal_util_fourcc_t,
) {
    if bytes.is_null() || out.is_null() {
        return;
    }
    (*out).v = pack_be(*bytes, *bytes.add(1), *bytes.add(2), *bytes.add(3));
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_fourcc_to_string(
    tag: *const pm_metal_util_fourcc_t,
    out: *mut u8,
) -> i32 {
    if tag.is_null() || out.is_null() {
        return -1;
    }
    store_u32_le(out, (*tag).v);
    *out.add(LEN) = 0;
    0
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_fourcc_from_string(
    s: *const u8,
    out: *mut pm_metal_util_fourcc_t,
) -> i32 {
    if s.is_null() || out.is_null() || cstr_len(s) != LEN {
        return -1;
    }
    (*out).v = load_u32_le(s);
    0
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_util_fourcc_label(magic: u32, out: *mut u8) -> i32 {
    if out.is_null() {
        return -1;
    }
    let tag = pm_metal_util_fourcc_t { v: magic };
    pm_metal_util_fourcc_to_string(&tag, out)
}
