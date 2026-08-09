//! util.endian — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use pymergetic_metal_rt as _;


extern "C" {
    fn pm_metal_util_endian_host_is_le() -> i32;
    fn pm_metal_util_endian_load_u16_le(src: *const u8) -> u16;
    fn pm_metal_util_endian_store_u16_le(dst: *mut u8, v: u16);
    fn pm_metal_util_endian_load_u32_le(src: *const u8) -> u32;
    fn pm_metal_util_endian_store_u32_le(dst: *mut u8, v: u32);
    fn pm_metal_util_endian_load_u64_le(src: *const u8) -> u64;
    fn pm_metal_util_endian_store_u64_le(dst: *mut u8, v: u64);
}

#[inline] pub fn host_is_le() -> i32 { unsafe { pm_metal_util_endian_host_is_le() } }
#[inline] pub unsafe fn load_u16_le(src: *const u8) -> u16 { pm_metal_util_endian_load_u16_le(src) }
#[inline] pub unsafe fn store_u16_le(dst: *mut u8, v: u16) { pm_metal_util_endian_store_u16_le(dst, v) }
#[inline] pub unsafe fn load_u32_le(src: *const u8) -> u32 { pm_metal_util_endian_load_u32_le(src) }
#[inline] pub unsafe fn store_u32_le(dst: *mut u8, v: u32) { pm_metal_util_endian_store_u32_le(dst, v) }
#[inline] pub unsafe fn load_u64_le(src: *const u8) -> u64 { pm_metal_util_endian_load_u64_le(src) }
#[inline] pub unsafe fn store_u64_le(dst: *mut u8, v: u64) { pm_metal_util_endian_store_u64_le(dst, v) }
