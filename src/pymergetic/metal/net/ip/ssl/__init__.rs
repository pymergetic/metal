//! SSL addon under net.ip (mbedTLS).
//!
//! App face: CA + server creds only. Session wrap / handshake / read / write are
//! for `net.ip.tcp` only — after connect/accept, apps use the opaque TCP stream
//! (`pm_metal_net_ip_tcp_read` / `write` / `close`), not these helpers.
#![allow(non_camel_case_types)]

use core::ffi::{c_char, c_void};

pub const PM_METAL_NET_IP_SSL_WIRE_MAX: usize = 16384;
pub const PM_METAL_NET_IP_SSL_WANT_READ: i32 = -0x6900;
pub const PM_METAL_NET_IP_SSL_WANT_WRITE: i32 = -0x6880;

/// Cleartext wire buffer owned by a TCP stream while SSL is attached.
pub struct Wire {
    pub buf: [u8; PM_METAL_NET_IP_SSL_WIRE_MAX],
    pub len: u32,
    pub off: u32,
}

type BioSend =
    Option<unsafe extern "C" fn(ctx: *mut c_void, buf: *const u8, len: usize) -> i32>;
type BioRecv =
    Option<unsafe extern "C" fn(ctx: *mut c_void, buf: *mut u8, len: usize) -> i32>;

extern "C" {
    fn ssl_metal_set_ca(pem: *const u8, len: u32) -> i32;
    fn ssl_metal_creds_open() -> u32;
    fn ssl_metal_creds_load_buffers(
        h: u32,
        cert: *const c_void,
        cert_len: u32,
        key: *const c_void,
        key_len: u32,
        client_ca: *const c_void,
        client_ca_len: u32,
        client_auth: i32,
    ) -> i32;
    fn ssl_metal_creds_close(h: u32) -> i32;
    fn ssl_metal_wrap_client(
        bio_ctx: *mut c_void,
        send: BioSend,
        recv: BioRecv,
        sni: *const c_char,
        insecure: i32,
    ) -> u32;
    fn ssl_metal_wrap_server(
        bio_ctx: *mut c_void,
        send: BioSend,
        recv: BioRecv,
        creds_h: u32,
    ) -> u32;
    fn ssl_metal_close(h: u32);
    fn ssl_metal_handshake_step(h: u32) -> i32;
    fn ssl_metal_read(h: u32, buf: *mut c_void, cap: u32) -> i32;
    fn ssl_metal_write(h: u32, buf: *const c_void, len: u32) -> i32;
}

/// Install default trust store PEM (optional; verify-on by default).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_ssl_set_ca(pem: *const u8, len: u32) -> i32 {
    ssl_metal_set_ca(pem, len)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_ssl_creds_open() -> u32 {
    ssl_metal_creds_open()
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_ssl_creds_load_buffers(
    h: u32,
    cert: *const c_void,
    cert_len: u32,
    key: *const c_void,
    key_len: u32,
    client_ca: *const c_void,
    client_ca_len: u32,
    client_auth: i32,
) -> i32 {
    ssl_metal_creds_load_buffers(
        h,
        cert,
        cert_len,
        key,
        key_len,
        client_ca,
        client_ca_len,
        client_auth,
    )
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_ip_ssl_creds_close(h: u32) -> i32 {
    ssl_metal_creds_close(h)
}

/// Wrap a TCP bio as client SSL (tcp connect takeover only).
pub unsafe fn wrap_client(
    bio_ctx: *mut c_void,
    send: BioSend,
    recv: BioRecv,
    sni: *const c_char,
    insecure: i32,
) -> u32 {
    ssl_metal_wrap_client(bio_ctx, send, recv, sni, insecure)
}

/// Wrap a TCP bio as server SSL (tcp accept takeover only).
pub unsafe fn wrap_server(
    bio_ctx: *mut c_void,
    send: BioSend,
    recv: BioRecv,
    creds_h: u32,
) -> u32 {
    ssl_metal_wrap_server(bio_ctx, send, recv, creds_h)
}

pub unsafe fn close(h: u32) {
    ssl_metal_close(h)
}

/// 0 = done, 1 = need more I/O, -1 = error.
pub unsafe fn handshake_step(h: u32) -> i32 {
    ssl_metal_handshake_step(h)
}

pub unsafe fn read(h: u32, buf: *mut u8, cap: u32) -> i32 {
    ssl_metal_read(h, buf as *mut c_void, cap)
}

pub unsafe fn write(h: u32, buf: *const u8, len: u32) -> i32 {
    ssl_metal_write(h, buf as *const c_void, len)
}
