//! trust — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_char;
use core::ffi::c_void;

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_trust_mode() -> i32;
    fn pm_metal_trust_mode_str() -> *const c_char;
    fn pm_metal_trust_ready() -> i32;
    fn pm_metal_trust_mods_pubkey_set(pk: *const u8, pk_len: u32) -> i32;
    fn pm_metal_trust_verify_mods(
        data: *const c_void,
        data_len: u32,
        sig: *const c_void,
        sig_len: u32,
    ) -> i32;
    fn pm_metal_trust_accept_mods(
        data: *const c_void,
        data_len: u32,
        sig: *const c_void,
        sig_len: u32,
    ) -> i32;
    fn pm_metal_trust_proof() -> i32;
}

#[inline]
pub fn mode() -> i32 {
    unsafe { pm_metal_trust_mode() }
}
#[inline]
pub fn mode_str() -> *const c_char {
    unsafe { pm_metal_trust_mode_str() }
}
#[inline]
pub fn ready() -> i32 {
    unsafe { pm_metal_trust_ready() }
}
#[inline]
pub unsafe fn mods_pubkey_set(pk: *const u8, pk_len: u32) -> i32 {
    pm_metal_trust_mods_pubkey_set(pk, pk_len)
}
#[inline]
pub unsafe fn verify_mods(
    data: *const c_void,
    data_len: u32,
    sig: *const c_void,
    sig_len: u32,
) -> i32 {
    pm_metal_trust_verify_mods(data, data_len, sig, sig_len)
}
#[inline]
pub unsafe fn accept_mods(
    data: *const c_void,
    data_len: u32,
    sig: *const c_void,
    sig_len: u32,
) -> i32 {
    pm_metal_trust_accept_mods(data, data_len, sig, sig_len)
}
#[inline]
pub fn proof() -> i32 {
    unsafe { pm_metal_trust_proof() }
}
