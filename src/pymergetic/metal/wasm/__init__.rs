//! Metal wasm host — load packs via WAMR into Metal memory; each loaded
//! pack joins the *same* registry every other module publishes into
//! (`pymergetic_metal_reg`'s `RegMod`/`RegEntry`, quiesce-protected
//! unload) -- there is no separate "wasm registration" tier anymore.
//!
//! Host + freestanding: real WAMR (`external/wamr`) over one Metal pool.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code, non_camel_case_types)]

extern crate alloc;

use alloc::boxed::Box;
use alloc::string::String;
use alloc::vec::Vec;
use core::cell::Cell;
use core::ffi::c_void;

#[path = "_load.rs"]
mod load;
#[path = "_stress.rs"]
mod stress;
#[cfg(any(target_os = "none", target_os = "uefi"))]
#[path = "_fetch.rs"]
mod fetch;

use pymergetic_metal_async as _;
use pymergetic_metal_log as _;
use pymergetic_metal_mem as _;
use pymergetic_metal_rt as _;

use pymergetic_metal_reg::{pm_metal_reg_mod_load, pm_metal_reg_mod_unload, RegEntry, RegMod};

extern "C" {
    fn pm_metal_wasm_port_ready() -> i32;
    fn pm_metal_wasm_port_init() -> i32;
    fn pm_metal_wasm_port_shutdown();
    fn pm_metal_wasm_port_unload(full_module: *const u8);
    fn pm_metal_wasm_port_call0(full_module: *const u8, func: *const u8) -> i32;
    fn pm_metal_wasm_port_export_count(full_module: *const u8) -> i32;
    fn pm_metal_wasm_port_export_name(
        full_module: *const u8,
        idx: i32,
        buf: *mut u8,
        buf_n: u32,
    ) -> i32;
    fn pm_metal_wasm_port_claim_trampoline(full_module: *const u8, func: *const u8) -> *const c_void;
    fn pm_metal_wasm_port_image(
        full_module: *const u8,
        out_bytes: *mut *const u8,
        out_len: *mut u32,
    ) -> i32;
    fn pm_metal_wasm_port_guest_coro_create(full_module: *const u8, state_bytes: u32) -> u32;
}

/// Host: create guest coro for a loaded module under the current callin step.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_wasm_guest_coro_create_for(
    full_module: *const u8,
    state_bytes: u32,
) -> u32 {
    pm_metal_wasm_port_guest_coro_create(full_module, state_bytes)
}

const NAME_BUF_MAX: usize = 96;
const FUNC_BUF_MAX: usize = 64;

/// Read a bounded NUL-terminated byte buffer into an owned `String`.
unsafe fn cstr_to_string(p: *const u8, max: usize) -> Option<String> {
    if p.is_null() {
        return None;
    }
    let mut n = 0usize;
    while *p.add(n) != 0 {
        n += 1;
        if n >= max {
            return None;
        }
    }
    let bytes = core::slice::from_raw_parts(p, n);
    core::str::from_utf8(bytes).ok().map(String::from)
}

/// Leak `s` as a NUL-terminated `'static` byte buffer (for repeated
/// calls into the C port, which take raw C strings) and return both the
/// terminated byte buffer and a `'static str` view of the same bytes
/// (no separate second allocation -- one leak per loaded module name).
fn leak_cstring(s: String) -> (&'static [u8], &'static str) {
    let mut v = s.into_bytes();
    let len = v.len();
    v.push(0);
    let leaked: &'static mut [u8] = Box::leak(v.into_boxed_slice());
    let leaked: &'static [u8] = leaked;
    let name = core::str::from_utf8(&leaked[..len]).unwrap_or("");
    (leaked, name)
}

/// 1 if the wasm runtime is initialized.
#[no_mangle]
pub extern "C" fn pm_metal_wasm_ready() -> i32 {
    unsafe { pm_metal_wasm_port_ready() }
}

/// Init WAMR over a Metal pool (one memory). Cross-package native
/// imports are no longer registered here: each package's own
/// `pm_metal_wasm_load` call registers whatever it declares, straight
/// from its own bytes (see `pm_metal_wasm_port_load`'s doc and
/// docs/definitions/module.md "Cross-package imports"). `0` ok, `-1`
/// fail.
#[no_mangle]
pub extern "C" fn pm_metal_wasm_init() -> i32 {
    unsafe { pm_metal_wasm_port_init() }
}

#[no_mangle]
pub extern "C" fn pm_metal_wasm_shutdown() {
    unsafe { pm_metal_wasm_port_shutdown() }
}

/// Load + instantiate wasm bytes under `full_module` (NUL C string).
/// Does not yet join the registry -- see [`pm_metal_wasm_register`].
#[no_mangle]
pub unsafe extern "C" fn pm_metal_wasm_load(
    full_module: *const u8,
    bytes: *const u8,
    len: u32,
) -> i32 {
    load::load(full_module, bytes, len)
}

/// Borrow loaded wasm image bytes (still owned by the port slot).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_wasm_image(
    full_module: *const u8,
    out_bytes: *mut *const u8,
    out_len: *mut u32,
) -> i32 {
    if out_bytes.is_null() || out_len.is_null() {
        return -1;
    }
    pm_metal_wasm_port_image(full_module, out_bytes, out_len)
}

extern "C" fn wasm_on_unload(ctx: *mut c_void) -> i32 {
    // `ctx` is the leaked NUL-terminated name buffer from `register`.
    unsafe { pm_metal_wasm_port_unload(ctx as *const u8) };
    0
}

/// Join the registry: discover this (already-loaded) instance's
/// `() -> i32` exports, claim one C trampoline per export, and
/// `pm_metal_reg_mod_load` a heap-built [`RegMod`] for it (always
/// `unloadable: true` -- a wasm pack is exactly the genuinely-unloadable
/// provider case the registry's quiesce-protected unload exists for).
/// Returns the published export count, or `-1`.
///
/// The `RegMod`/its `entries`/its name buffer are `Box::leak`ed to get
/// the `'static` lifetime the registry API requires; a repeated
/// load/unload/reload cycle under the same name currently leaks a new
/// copy each time (matches every other genuinely dynamic provider's
/// lifetime story today -- there is no generational/arena reclaim for
/// unloaded `RegMod`s yet anywhere in the tree, not specific to wasm).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_wasm_register(full_module: *const u8) -> i32 {
    let Some(name) = cstr_to_string(full_module, NAME_BUF_MAX) else {
        return -1;
    };
    let n = pm_metal_wasm_port_export_count(full_module);
    if n < 0 {
        return -1;
    }
    let n = n as usize;

    let mut entries: Vec<RegEntry> = Vec::with_capacity(n);
    let mut buf = [0u8; FUNC_BUF_MAX];
    for i in 0..n {
        if pm_metal_wasm_port_export_name(full_module, i as i32, buf.as_mut_ptr(), buf.len() as u32)
            != 0
        {
            return -1;
        }
        let Some(fname) = cstr_to_string(buf.as_ptr(), FUNC_BUF_MAX) else {
            return -1;
        };
        let (fname_cstr, fname_str) = leak_cstring(fname);
        let ptr = pm_metal_wasm_port_claim_trampoline(full_module, fname_cstr.as_ptr());
        if ptr.is_null() {
            return -1;
        }
        let entry = RegEntry::new(fname_str);
        entry.publish(ptr);
        entries.push(entry);
    }
    let entries: &'static [RegEntry] = Box::leak(entries.into_boxed_slice());
    let (name_cstr, name_str) = leak_cstring(name);

    let regmod: &'static RegMod = Box::leak(Box::new(RegMod {
        name: name_str,
        unloadable: true,
        parent: None,
        ctx: name_cstr.as_ptr() as *mut c_void,
        on_load: None,
        register_symbols: None,
        connect_symbols: None,
        on_registrations_updated: None,
        deregister_symbols: None,
        on_unload: Some(wasm_on_unload),
        entries,
        imports: &[],
        raw_next: Cell::new(core::ptr::null()),
        raw_prev: Cell::new(core::ptr::null()),
    }));
    if pm_metal_reg_mod_load(regmod) != 0 {
        return -1;
    }
    entries.len() as i32
}

/// Load, then register (join the registry). Returns the published
/// export count, or `-1`.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_wasm_load_register(
    full_module: *const u8,
    bytes: *const u8,
    len: u32,
) -> i32 {
    if load::load(full_module, bytes, len) != 0 {
        return -1;
    }
    pm_metal_wasm_register(full_module)
}

/// Trust policy gate (`pm_metal_trust_accept_mods`), then load+register.
/// `sig` may be null / `sig_len` 0 (soft/off accept unsigned).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_wasm_load_verified(
    full_module: *const u8,
    bytes: *const u8,
    len: u32,
    sig: *const u8,
    sig_len: u32,
) -> i32 {
    extern "C" {
        fn pm_metal_trust_accept_mods(
            data: *const c_void,
            data_len: u32,
            sig: *const c_void,
            sig_len: u32,
        ) -> i32;
    }
    if bytes.is_null() || len == 0 {
        return -1;
    }
    if pm_metal_trust_accept_mods(
        bytes as *const c_void,
        len,
        sig as *const c_void,
        sig_len,
    ) != 0
    {
        return -1;
    }
    pm_metal_wasm_load_register(full_module, bytes, len)
}

/// Cascading, quiesced unload through the registry -- withdraws every
/// published `RegEntry` first (no concurrent caller anywhere in the
/// system while that happens, see `pymergetic_metal_reg::kernel::unload`),
/// then this module's `on_unload` hook tears down the WAMR instance.
/// `0` ok, `-1` if not loaded.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_wasm_unload(full_module: *const u8) -> i32 {
    if full_module.is_null() {
        return -1;
    }
    pm_metal_reg_mod_unload(full_module)
}

/// Call export directly (host-side convenience; does not touch `reg`).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_wasm_call0(full_module: *const u8, func: *const u8) -> i32 {
    if full_module.is_null() || func.is_null() {
        return -1;
    }
    pm_metal_wasm_port_call0(full_module, func)
}

/// W11.6: cross-lang/wasm stress (body in `_stress.rs`).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_wasm_proof_stress() -> i32 {
    stress::proof_stress()
}

/// W6.3 proof: load forge-packed `tests.wasm_hello` + `_c`, call via
/// host + the registry. W16.1: also `tests.wasm_guest_log` (kernel log import).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_wasm_proof() -> i32 {
    extern "C" {
        fn pm_metal_reg_call0(full_module: *const u8, func: *const u8) -> i32;
        fn pm_metal_log(line: *const u8);
    }

    /* Produced by `forge pack` (forge build runs pack all in prep). */
    static WASM_RS: &[u8] = include_bytes!("../../../../build/packs/tests.wasm_hello.wasm");
    static WASM_C: &[u8] = include_bytes!("../../../../build/packs/tests.wasm_hello_c.wasm");
    static WASM_GUEST_LOG: &[u8] =
        include_bytes!("../../../../build/packs/tests.wasm_guest_log.wasm");
    static WASM_GUEST_ASYNC: &[u8] =
        include_bytes!("../../../../build/packs/tests.wasm_guest_async.wasm");
    static WASM_GUEST_SURFACES: &[u8] =
        include_bytes!("../../../../build/packs/tests.wasm_guest_surfaces.wasm");
    static WASM_GUEST_INPUT: &[u8] =
        include_bytes!("../../../../build/packs/tests.wasm_guest_input.wasm");
    static WASM_GUEST_CORO: &[u8] =
        include_bytes!("../../../../build/packs/tests.wasm_guest_coro.wasm");
    static WASM_GUEST_AUDIO: &[u8] =
        include_bytes!("../../../../build/packs/tests.wasm_guest_audio.wasm");
    static MOD_RS: [u8; 17] = *b"tests.wasm_hello\0";
    static MOD_C: [u8; 19] = *b"tests.wasm_hello_c\0";
    static MOD_GUEST_LOG: [u8; 21] = *b"tests.wasm_guest_log\0";
    static MOD_GUEST_ASYNC: [u8; 23] = *b"tests.wasm_guest_async\0";
    static MOD_GUEST_SURFACES: [u8; 26] = *b"tests.wasm_guest_surfaces\0";
    static MOD_GUEST_INPUT: [u8; 23] = *b"tests.wasm_guest_input\0";
    static MOD_GUEST_CORO: [u8; 22] = *b"tests.wasm_guest_coro\0";
    static MOD_GUEST_AUDIO: [u8; 23] = *b"tests.wasm_guest_audio\0";

    if pm_metal_wasm_init() != 0 {
        return -1;
    }
    for (mod_name, bytes) in [(MOD_RS.as_slice(), WASM_RS), (MOD_C.as_slice(), WASM_C)] {
        let n = pm_metal_wasm_load_register(mod_name.as_ptr(), bytes.as_ptr(), bytes.len() as u32);
        if n < 1 {
            return -1;
        }
        if pm_metal_wasm_call0(mod_name.as_ptr(), b"ready\0".as_ptr()) != 0 {
            return -1;
        }
        if pm_metal_reg_call0(mod_name.as_ptr(), b"ready\0".as_ptr()) != 0 {
            return -1;
        }
    }
    /* Guest -> kernel guest_surface (host natives), not guest-to-guest fwd. */
    let n = pm_metal_wasm_load_register(
        MOD_GUEST_LOG.as_ptr(),
        WASM_GUEST_LOG.as_ptr(),
        WASM_GUEST_LOG.len() as u32,
    );
    if n < 1 {
        return -1;
    }
    if pm_metal_wasm_call0(MOD_GUEST_LOG.as_ptr(), b"ready\0".as_ptr()) != 0 {
        return -1;
    }
    let n = pm_metal_wasm_load_register(
        MOD_GUEST_ASYNC.as_ptr(),
        WASM_GUEST_ASYNC.as_ptr(),
        WASM_GUEST_ASYNC.len() as u32,
    );
    if n < 1 {
        return -1;
    }
    if pm_metal_wasm_call0(MOD_GUEST_ASYNC.as_ptr(), b"ready\0".as_ptr()) != 0 {
        return -1;
    }
    let n = pm_metal_wasm_load_register(
        MOD_GUEST_SURFACES.as_ptr(),
        WASM_GUEST_SURFACES.as_ptr(),
        WASM_GUEST_SURFACES.len() as u32,
    );
    if n < 1 {
        return -1;
    }
    if pm_metal_wasm_call0(MOD_GUEST_SURFACES.as_ptr(), b"ready\0".as_ptr()) != 0 {
        return -1;
    }
    let n = pm_metal_wasm_load_register(
        MOD_GUEST_INPUT.as_ptr(),
        WASM_GUEST_INPUT.as_ptr(),
        WASM_GUEST_INPUT.len() as u32,
    );
    if n < 1 {
        return -1;
    }
    if pm_metal_wasm_call0(MOD_GUEST_INPUT.as_ptr(), b"ready\0".as_ptr()) != 0 {
        return -1;
    }
    let n = pm_metal_wasm_load_register(
        MOD_GUEST_CORO.as_ptr(),
        WASM_GUEST_CORO.as_ptr(),
        WASM_GUEST_CORO.len() as u32,
    );
    if n < 1 {
        return -1;
    }
    if pm_metal_wasm_call0(MOD_GUEST_CORO.as_ptr(), b"ready\0".as_ptr()) != 0 {
        return -1;
    }
    let n = pm_metal_wasm_load_register(
        MOD_GUEST_AUDIO.as_ptr(),
        WASM_GUEST_AUDIO.as_ptr(),
        WASM_GUEST_AUDIO.len() as u32,
    );
    if n < 1 {
        return -1;
    }
    if pm_metal_wasm_call0(MOD_GUEST_AUDIO.as_ptr(), b"ready\0".as_ptr()) != 0 {
        return -1;
    }
    pm_metal_log(b"guest surface ok\0".as_ptr());
    0
}
