//! pack — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use pymergetic_metal_rt as _;

extern "C" {
    static pm_metal_pack_inspect: u8;
    static pm_metal_pack_inspect_len: u32;
    static pm_metal_pack_metal: u8;
    static pm_metal_pack_metal_len: u32;
    fn pm_metal_mod_packs_mount_all() -> i32;
}

#[inline]
pub unsafe fn pack_inspect() -> *const u8 {
    &pm_metal_pack_inspect as *const u8
}
#[inline]
pub fn pack_inspect_len() -> u32 {
    unsafe { pm_metal_pack_inspect_len }
}
#[inline]
pub unsafe fn pack_metal() -> *const u8 {
    &pm_metal_pack_metal as *const u8
}
#[inline]
pub fn pack_metal_len() -> u32 {
    unsafe { pm_metal_pack_metal_len }
}
#[inline]
pub fn mount_all() -> i32 {
    unsafe { pm_metal_mod_packs_mount_all() }
}

