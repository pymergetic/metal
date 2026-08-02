//! pymergetic.metal.forge — module codegen engine.
//!
//! Public C border: version / capability probes + convert / sync entry
//! points. Per-language faces stay private (`_import_*` / `_export_*`).
//! Ports: `_port/solo` / `_port/metal`. Linux app: hidden `cli/` + `./forge-cli` launcher.
#![cfg_attr(not(feature = "solo"), no_std)]
#![allow(dead_code, non_camel_case_types)]

extern crate alloc;

#[path = "_banner.rs"]
mod _banner;
#[path = "_catalog.rs"]
mod _catalog;
#[path = "_cli.rs"]
mod _cli;
#[cfg(feature = "solo")]
#[path = "_config.rs"]
mod _config;
#[path = "_comment.rs"]
mod _comment;
#[path = "_convert.rs"]
mod _convert;
#[cfg(feature = "solo")]
#[path = "_host.rs"]
mod _host;
#[cfg(feature = "solo")]
#[path = "_build.rs"]
mod _build;
#[cfg(feature = "solo")]
#[path = "_pack.rs"]
mod _pack;
#[cfg(feature = "solo")]
#[path = "_wasm_import_section.rs"]
mod _wasm_import_section;
#[cfg(feature = "solo")]
#[path = "_run.rs"]
mod _run;
#[path = "_export_c.rs"]
mod _export_c;
#[path = "_export_py.rs"]
mod _export_py;
#[path = "_export_rs.rs"]
mod _export_rs;
#[path = "_export_toml.rs"]
mod _export_toml;
#[path = "_hash.rs"]
mod _hash;
#[cfg(feature = "builders")]
#[path = "_img.rs"]
mod _img;
#[path = "_import_c.rs"]
mod _import_c;
#[path = "_import_py.rs"]
mod _import_py;
#[path = "_import_rs.rs"]
mod _import_rs;
#[path = "_import_toml.rs"]
mod _import_toml;
#[path = "_meta.rs"]
mod _meta;
#[path = "_pool.rs"]
mod _pool;
#[path = "_port/__init__.rs"]
mod _port;
#[path = "_sync.rs"]
mod _sync;
#[path = "_template.rs"]
mod _template;

const FORGE_VERSION: &str = "0.1.0";

/// Whether convert/sync may skip a face whose `Source-sha` still matches.
#[repr(u32)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum pm_metal_forge_refresh_t {
    PM_METAL_FORGE_REFRESH_IF_STALE = 0,
    PM_METAL_FORGE_REFRESH_FORCE = 1,
}

/// Result of a single-file convert.
#[repr(u32)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum pm_metal_forge_convert_t {
    PM_METAL_FORGE_CONVERT_WRITTEN = 0,
    PM_METAL_FORGE_CONVERT_FRESH = 1,
    PM_METAL_FORGE_CONVERT_EMPTY = 2,
    PM_METAL_FORGE_CONVERT_ERR = 3,
}

/// NUL-terminated version string for C callers.
#[no_mangle]
pub extern "C" fn pm_metal_forge_version() -> *const u8 {
    static V: &[u8] = b"0.1.0\0";
    V.as_ptr()
}

/// Borrowed version (Rust / tools).
pub fn pm_metal_forge_version_str() -> &'static str {
    FORGE_VERSION
}

/// 1 if built with the solo (Linux FS/stdio) port shim.
#[no_mangle]
pub extern "C" fn pm_metal_forge_has_solo() -> i32 {
    if cfg!(feature = "solo") {
        1
    } else {
        0
    }
}

/// 1 if built with the metal (process/console) port stubs.
#[no_mangle]
pub extern "C" fn pm_metal_forge_has_metal() -> i32 {
    if cfg!(feature = "metal") {
        1
    } else {
        0
    }
}

/// Solo port types for the outside app (`./forge-cli`).
#[cfg(feature = "solo")]
pub use _port::solo::{SoloSession, SoloStore};

/// Run mod sync|check|clean|ls / convert against an injected store/session.
#[cfg(feature = "solo")]
pub fn run_cli(store: &mut SoloStore, sess: &mut SoloSession, default_metal_root: &str) -> i32 {
    _cli::run(store, sess, default_metal_root)
}

/// Convert `src` -> `dst` by filetype (solo FS).
#[cfg(feature = "solo")]
#[no_mangle]
pub extern "C" fn pm_metal_forge_convert(
    src: *const u8,
    dst: *const u8,
    refresh: pm_metal_forge_refresh_t,
) -> pm_metal_forge_convert_t {
    use core::ffi::CStr;
    if src.is_null() || dst.is_null() {
        return pm_metal_forge_convert_t::PM_METAL_FORGE_CONVERT_ERR;
    }
    let src = unsafe { CStr::from_ptr(src as *const i8) }
        .to_str()
        .unwrap_or("");
    let dst = unsafe { CStr::from_ptr(dst as *const i8) }
        .to_str()
        .unwrap_or("");
    if src.is_empty() || dst.is_empty() {
        return pm_metal_forge_convert_t::PM_METAL_FORGE_CONVERT_ERR;
    }
    let force = refresh == pm_metal_forge_refresh_t::PM_METAL_FORGE_REFRESH_FORCE;
    let mut store = SoloStore::new();
    match _convert::convert_paths(&mut store, src, dst, force) {
        Ok(_convert::FaceAction::Written) => {
            pm_metal_forge_convert_t::PM_METAL_FORGE_CONVERT_WRITTEN
        }
        Ok(_convert::FaceAction::Fresh) => pm_metal_forge_convert_t::PM_METAL_FORGE_CONVERT_FRESH,
        Ok(_convert::FaceAction::Empty) => pm_metal_forge_convert_t::PM_METAL_FORGE_CONVERT_EMPTY,
        Err(_) => pm_metal_forge_convert_t::PM_METAL_FORGE_CONVERT_ERR,
    }
}
