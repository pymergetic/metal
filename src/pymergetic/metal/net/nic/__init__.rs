//! net.nic — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_char;

use pymergetic_metal_rt as _;

#[repr(C)]
pub struct pm_metal_net_nic_l2_ops_t {
    _opaque: [u8; 0],
}

extern "C" {
    fn pm_metal_net_nic_register(name: *const c_char, ops: *const pm_metal_net_nic_l2_ops_t) -> i32;
    fn pm_metal_net_nic_ops() -> *const pm_metal_net_nic_l2_ops_t;
    fn pm_metal_net_nic_name() -> *const c_char;
    fn pm_metal_net_nic_attach_upy() -> i32;
}

#[inline] pub unsafe fn register(name: *const c_char, ops: *const pm_metal_net_nic_l2_ops_t) -> i32 { pm_metal_net_nic_register(name, ops) }
#[inline] pub unsafe fn ops() -> *const pm_metal_net_nic_l2_ops_t { pm_metal_net_nic_ops() }
#[inline] pub unsafe fn name() -> *const c_char { pm_metal_net_nic_name() }
#[inline] pub fn attach_upy() -> i32 { unsafe { pm_metal_net_nic_attach_upy() } }
