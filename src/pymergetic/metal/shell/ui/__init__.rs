//! shell.ui — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_shell_ui_attach_console0() -> i32;
    fn pm_metal_shell_ui_present() -> i32;
}

#[inline] pub fn attach_console0() -> i32 { unsafe { pm_metal_shell_ui_attach_console0() } }
#[inline] pub fn present() -> i32 { unsafe { pm_metal_shell_ui_present() } }
