//! net.ssh — Rust face over the C impl + RegModStatic declare.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::{c_char, c_void};

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_net_ssh_available() -> i32;
    fn pm_metal_net_ssh_init() -> i32;
    fn pm_metal_net_ssh_autoload() -> i32;
    fn pm_metal_net_ssh_listen(port: u32) -> u32;
    fn pm_metal_net_ssh_release();
    fn pm_metal_net_ssh_close(s: u32);
    fn pm_metal_net_ssh_poll() -> i32;
    fn pm_metal_net_ssh_served() -> i32;
    fn pm_metal_net_ssh_status(buf: *mut u8, buf_len: u32) -> i32;
    fn pm_metal_net_ssh_listen_port() -> u32;
    fn pm_metal_net_ssh_hostkey_label(buf: *mut u8, buf_len: u32) -> i32;
    fn pm_metal_net_ssh_client_exec(
        host: *const c_char,
        port: u16,
        user: *const c_char,
        cmd: *const c_char,
        buf: *mut u8,
        cap: u32,
        len_out: *mut u32,
    ) -> i32;
    fn pm_metal_net_ssh_banner_send() -> i32;
    fn pm_metal_net_ssh_banner_sent() -> i32;
    fn pm_metal_net_ssh_banner_reset();
}

#[inline]
pub fn available() -> i32 {
    unsafe { pm_metal_net_ssh_available() }
}
#[inline]
pub fn init() -> i32 {
    unsafe { pm_metal_net_ssh_init() }
}
#[inline]
pub fn autoload() -> i32 {
    unsafe { pm_metal_net_ssh_autoload() }
}
#[inline]
pub fn listen(port: u32) -> u32 {
    unsafe { pm_metal_net_ssh_listen(port) }
}
#[inline]
pub fn release() {
    unsafe { pm_metal_net_ssh_release() }
}
#[inline]
pub fn close(s: u32) {
    unsafe { pm_metal_net_ssh_close(s) }
}
#[inline]
pub fn poll() -> i32 {
    unsafe { pm_metal_net_ssh_poll() }
}
#[inline]
pub fn served() -> i32 {
    unsafe { pm_metal_net_ssh_served() }
}
#[inline]
pub unsafe fn status(buf: *mut u8, buf_len: u32) -> i32 {
    pm_metal_net_ssh_status(buf, buf_len)
}
#[inline]
pub fn listen_port() -> u32 {
    unsafe { pm_metal_net_ssh_listen_port() }
}
#[inline]
pub unsafe fn hostkey_label(buf: *mut u8, buf_len: u32) -> i32 {
    pm_metal_net_ssh_hostkey_label(buf, buf_len)
}
#[inline]
pub unsafe fn client_exec(
    host: *const c_char,
    port: u16,
    user: *const c_char,
    cmd: *const c_char,
    buf: *mut u8,
    cap: u32,
    len_out: *mut u32,
) -> i32 {
    pm_metal_net_ssh_client_exec(host, port, user, cmd, buf, cap, len_out)
}
#[inline]
pub fn banner_send() -> i32 {
    unsafe { pm_metal_net_ssh_banner_send() }
}
#[inline]
pub fn banner_sent() -> i32 {
    unsafe { pm_metal_net_ssh_banner_sent() }
}
#[inline]
pub fn banner_reset() {
    unsafe { pm_metal_net_ssh_banner_reset() }
}

use pymergetic_metal_reg::{pm_metal_reg_mod_load, RegMod};

pymergetic_metal_reg::reg_mod! {
    mod ssh = "pymergetic.metal.net.ssh";
    exports: [listen, close, autoload, status];
    imports: [yield_ = "pymergetic.metal.async"::"yield"];
}

extern "C" fn ssh_register_symbols(_ctx: *mut c_void) -> i32 {
    ssh::listen.publish(pm_metal_net_ssh_listen as *const c_void);
    ssh::close.publish(pm_metal_net_ssh_close as *const c_void);
    ssh::autoload.publish(pm_metal_net_ssh_autoload as *const c_void);
    ssh::status.publish(pm_metal_net_ssh_status as *const c_void);
    0
}

static SSH_MOD: RegMod = RegMod::from_static(
    ssh::NAME,
    &ssh::STORAGE.exports,
    &ssh::STORAGE.imports,
    Some(ssh_register_symbols),
);

/// Load this module into the kernel RegMod ring (idempotent).
#[no_mangle]
pub extern "C" fn pm_metal_net_ssh_reg_load() -> i32 {
    if pymergetic_metal_reg::find_mod(ssh::NAME).is_some() {
        return 0;
    }
    unsafe { pm_metal_reg_mod_load(&SSH_MOD) }
}

/// Compat: old dyn-table bind path now loads the RegMod.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ssh_bind_reg() -> i32 {
    pm_metal_net_ssh_reg_load()
}

#[inline]
pub fn bind_reg() -> i32 {
    pm_metal_net_ssh_reg_load()
}

#[inline]
pub fn reg_load() -> i32 {
    pm_metal_net_ssh_reg_load()
}
