//! net.tls — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::{c_char, c_void};

use pymergetic_metal_rt as _;

#[repr(C)]
pub struct pm_metal_net_tls_ops_t {
    _opaque: [u8; 0],
}

extern "C" {
    fn pm_metal_net_tls_set_ops(ops: *const pm_metal_net_tls_ops_t);
    fn pm_metal_net_tls_ops() -> *const pm_metal_net_tls_ops_t;
    fn pm_metal_net_tls_mbedtls_register() -> i32;
    fn pm_metal_net_tls_init() -> i32;
    fn pm_metal_net_tls_fini();
    fn pm_metal_net_tls_client_open(sock: u32, hostname: *const c_char, flags: u32) -> u32;
    fn pm_metal_net_tls_server_open(sock: u32) -> u32;
    fn pm_metal_net_tls_handshake(th: u32) -> u32;
    fn pm_metal_net_tls_try_read(th: u32, buf: *mut c_void, len: u32) -> u32;
    fn pm_metal_net_tls_write(th: u32, buf: *const c_void, len: u32) -> u32;
    fn pm_metal_net_tls_close(th: u32);
    fn pm_metal_net_tls_load_ca_pem(pem: *const u8, len: u32) -> i32;
    fn pm_metal_net_tls_load_ca_file(path: *const c_char) -> i32;
    fn pm_metal_net_tls_set_server_cert_pem(pem: *const u8, len: u32) -> i32;
    fn pm_metal_net_tls_set_server_key_pem(pem: *const u8, len: u32) -> i32;
    fn pm_metal_net_tls_set_server_chain_pem(pem: *const u8, len: u32) -> i32;
    fn pm_metal_net_tls_load_smoke_server() -> i32;
    fn pm_metal_net_tls_poll();
}

#[inline] pub unsafe fn set_ops(ops: *const pm_metal_net_tls_ops_t) { pm_metal_net_tls_set_ops(ops) }
#[inline] pub unsafe fn ops() -> *const pm_metal_net_tls_ops_t { pm_metal_net_tls_ops() }
#[inline] pub fn mbedtls_register() -> i32 { unsafe { pm_metal_net_tls_mbedtls_register() } }
#[inline] pub fn init() -> i32 { unsafe { pm_metal_net_tls_init() } }
#[inline] pub fn fini() { unsafe { pm_metal_net_tls_fini() } }
#[inline] pub unsafe fn client_open(sock: u32, hostname: *const c_char, flags: u32) -> u32 { pm_metal_net_tls_client_open(sock, hostname, flags) }
#[inline] pub fn server_open(sock: u32) -> u32 { unsafe { pm_metal_net_tls_server_open(sock) } }
#[inline] pub fn handshake(th: u32) -> u32 { unsafe { pm_metal_net_tls_handshake(th) } }
#[inline] pub unsafe fn try_read(th: u32, buf: *mut c_void, len: u32) -> u32 { pm_metal_net_tls_try_read(th, buf, len) }
#[inline] pub unsafe fn write(th: u32, buf: *const c_void, len: u32) -> u32 { pm_metal_net_tls_write(th, buf, len) }
#[inline] pub fn close(th: u32) { unsafe { pm_metal_net_tls_close(th) } }
#[inline] pub unsafe fn load_ca_pem(pem: *const u8, len: u32) -> i32 { pm_metal_net_tls_load_ca_pem(pem, len) }
#[inline] pub unsafe fn load_ca_file(path: *const c_char) -> i32 { pm_metal_net_tls_load_ca_file(path) }
#[inline] pub unsafe fn set_server_cert_pem(pem: *const u8, len: u32) -> i32 { pm_metal_net_tls_set_server_cert_pem(pem, len) }
#[inline] pub unsafe fn set_server_key_pem(pem: *const u8, len: u32) -> i32 { pm_metal_net_tls_set_server_key_pem(pem, len) }
#[inline] pub unsafe fn set_server_chain_pem(pem: *const u8, len: u32) -> i32 { pm_metal_net_tls_set_server_chain_pem(pem, len) }
#[inline] pub fn load_smoke_server() -> i32 { unsafe { pm_metal_net_tls_load_smoke_server() } }
#[inline] pub fn poll() { unsafe { pm_metal_net_tls_poll() } }
