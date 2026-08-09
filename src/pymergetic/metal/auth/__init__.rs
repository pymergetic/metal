//! auth — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_char;

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_auth_users_set(users: *const u8, n: u32);
    fn pm_metal_auth_user_check(user: *const c_char, pass: *const c_char) -> i32;
    fn pm_metal_auth_hash_verify(encoded: *const c_char, pass: *const c_char) -> i32;
    fn pm_metal_auth_basic_decode(
        b64: *const c_char,
        user: *mut c_char,
        user_cap: u32,
        pass: *mut c_char,
        pass_cap: u32,
    ) -> i32;
    fn pm_metal_auth_pubkeys_clear();
    fn pm_metal_auth_pubkey_add(
        user: *const c_char,
        algo: *const c_char,
        blob: *const u8,
        blob_len: u32,
    ) -> i32;
    fn pm_metal_auth_pubkey_add_line(user: *const c_char, line: *const c_char) -> i32;
    fn pm_metal_auth_pubkey_load_text(
        user: *const c_char,
        text: *const c_char,
        text_len: u32,
    ) -> i32;
    fn pm_metal_auth_pubkey_check(
        user: *const c_char,
        algo: *const c_char,
        key_blob: *const u8,
        key_len: u32,
    ) -> i32;
}

#[inline]
pub unsafe fn users_set(users: *const u8, n: u32) {
    pm_metal_auth_users_set(users, n)
}
#[inline]
pub unsafe fn user_check(user: *const c_char, pass: *const c_char) -> i32 {
    pm_metal_auth_user_check(user, pass)
}
#[inline]
pub unsafe fn hash_verify(encoded: *const c_char, pass: *const c_char) -> i32 {
    pm_metal_auth_hash_verify(encoded, pass)
}
#[inline]
pub unsafe fn basic_decode(
    b64: *const c_char,
    user: *mut c_char,
    user_cap: u32,
    pass: *mut c_char,
    pass_cap: u32,
) -> i32 {
    pm_metal_auth_basic_decode(b64, user, user_cap, pass, pass_cap)
}
#[inline]
pub fn pubkeys_clear() {
    unsafe { pm_metal_auth_pubkeys_clear() }
}
#[inline]
pub unsafe fn pubkey_add(
    user: *const c_char,
    algo: *const c_char,
    blob: *const u8,
    blob_len: u32,
) -> i32 {
    pm_metal_auth_pubkey_add(user, algo, blob, blob_len)
}
#[inline]
pub unsafe fn pubkey_add_line(user: *const c_char, line: *const c_char) -> i32 {
    pm_metal_auth_pubkey_add_line(user, line)
}
#[inline]
pub unsafe fn pubkey_load_text(user: *const c_char, text: *const c_char, text_len: u32) -> i32 {
    pm_metal_auth_pubkey_load_text(user, text, text_len)
}
#[inline]
pub unsafe fn pubkey_check(
    user: *const c_char,
    algo: *const c_char,
    key_blob: *const u8,
    key_len: u32,
) -> i32 {
    pm_metal_auth_pubkey_check(user, algo, key_blob, key_len)
}
