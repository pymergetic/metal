//! Cross-lang registry bus — two tiers, both keyed by full module names
//! like `pymergetic.metal.fs.open` (Locked #2).
//!
//! - **Dynamic/late** (`_table.rs`): a small flat linear-scan table for
//!   callers that only know their (module, func) pair at runtime and
//!   have no per-load `RegMod` of their own -- Python attach (see
//!   `pm_metal_reg_register`'s real callers in `py/`). `wasm`'s own
//!   module registration uses the static tier below instead (each
//!   loaded pack gets its own `RegMod`, same shape as a compile-time
//!   module).
//! - **Static per-module lifecycle** (`_entry.rs` / `_declare.rs` /
//!   `_kernel.rs`): `on_load` -> `register_symbols` -> `connect_symbols`
//!   -> ... -> `deregister_symbols` -> `on_unload`. No per-entry
//!   refcount: `unload` quiesces every async runner (parks every
//!   dispatch loop at its own next checkpoint, see
//!   `pymergetic_metal_async::quiesce`) before withdrawing anything, so
//!   there is no concurrent caller to protect against, fixed provider or
//!   unloadable one alike. Only genuinely `unloadable` providers (wasm,
//!   Python) publish through here; `pymergetic.metal` itself is the
//!   first module loaded this way (kernel is kernel: permanent, first in
//!   boot order, but architecturally just another module).
//!
//! Compile-time-known floor modules that are never unloaded do not go
//! through either tier for their own exports: their generated
//! `include/pymergetic/metal/<mod>/` face is a plain link-time
//! `extern "C"` declaration instead (or, only where a Cargo dependency
//! would cycle, a hand-written one) -- see `docs/definitions/module.md`.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code, non_camel_case_types)]

#[path = "_bind.rs"]
mod bind;
#[path = "_bulk.rs"]
mod bulk;
#[path = "_declare.rs"]
mod declare;
#[path = "_entry.rs"]
mod entry;
#[path = "_kernel.rs"]
mod kernel;
#[path = "_spin.rs"]
mod spin;
#[path = "_table.rs"]
mod table;

use core::ffi::c_void;

use pymergetic_metal_async as _;
use pymergetic_metal_rt as _;

use bind::bind as bind_key;
use table::{cstr_bytes, Table, FUNC_MAX, MODULE_MAX};

pub use bulk::{register_rows, register_rows_bytes};
pub use declare::{ImportRow, RegModStatic};
pub use entry::RegEntry;
pub use kernel::{HookFn, RegMod};

static TABLE: Table = Table::new();

/// Register or replace `(full_module, func)` -> `ptr`. Returns 0 or -1.
/// For runtime-chosen module/func pairs only (Python attach, wasm-mod
/// registration) — compile-time-known peers should be a plain Cargo
/// dependency + direct call instead (see module docs).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_register(
    full_module: *const u8,
    func: *const u8,
    ptr: *const c_void,
) -> i32 {
    let Some(m) = cstr_bytes(full_module, MODULE_MAX) else {
        return -1;
    };
    let Some(f) = cstr_bytes(func, FUNC_MAX) else {
        return -1;
    };
    TABLE.register(m, f, ptr)
}

/// Lookup: write pointer to `*out_ptr` on success. Returns 0 or -1.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_lookup(
    full_module: *const u8,
    func: *const u8,
    out_ptr: *mut *const c_void,
) -> i32 {
    if out_ptr.is_null() {
        return -1;
    }
    let p = bind_key(&TABLE, full_module, func);
    if p.is_null() {
        *out_ptr = core::ptr::null();
        return -1;
    }
    *out_ptr = p;
    0
}

/// Hot path: bound function pointer, or null if missing.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_bind(
    full_module: *const u8,
    func: *const u8,
) -> *const c_void {
    bind_key(&TABLE, full_module, func)
}

/// Convenience: call a registered `extern "C" fn() -> i32`. Returns the
/// callee result, or -1 if missing / null args.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_call0(full_module: *const u8, func: *const u8) -> i32 {
    let p = bind_key(&TABLE, full_module, func);
    if p.is_null() {
        return -1;
    }
    let f: extern "C" fn() -> i32 = core::mem::transmute(p);
    f()
}

/// How many symbols are currently registered.
#[no_mangle]
pub extern "C" fn pm_metal_reg_count() -> u32 {
    TABLE.count() as u32
}

// --- Static per-module lifecycle (unloadable providers + kernel) -----

/// Load a module into the kernel lifecycle: `on_load` -> `register_symbols`
/// -> a global connect pass over every loaded module. `0` on success.
///
/// # Safety
/// `m`, if non-null, must point to a valid `RegMod` that stays alive for
/// as long as it remains loaded (it is only ever built as a `'static`
/// value by generated/hand-written module init code, never freed).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_mod_load(m: *const RegMod) -> i32 {
    if m.is_null() {
        return -1;
    }
    kernel::load(&*m)
}

/// Cascading unload by full module name: children first, then this
/// module's own `deregister_symbols`/`on_unload`, then a reconnect
/// pass. `-1` if not loaded, or if `unloadable` is `false` (a
/// permanently-linked module, or a sticky package). The actual withdraw
/// runs inside a global quiesce (every async runner parked first), so
/// there is no live-caller check needed -- see `kernel::unload`.
///
/// # Safety
/// `name` must be null or point to a valid NUL-terminated string.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_mod_unload(name: *const u8) -> i32 {
    match cstr_str(name, MODULE_MAX) {
        Some(n) => kernel::unload(n),
        None => -1,
    }
}

/// Re-run every loaded module's `connect_symbols` against the current
/// kernel table (e.g. after a load this module's own `load` call already
/// triggered, or to force a reconnect out-of-band).
#[no_mangle]
pub extern "C" fn pm_metal_reg_mod_connect_all() {
    kernel::connect_all();
}

/// How many modules are currently loaded through the static lifecycle
/// (distinct from [`pm_metal_reg_count`], which counts dynamic-table
/// symbols).
#[no_mangle]
pub extern "C" fn pm_metal_reg_mod_count() -> u32 {
    kernel::count() as u32
}

fn cstr_str<'a>(p: *const u8, max: usize) -> Option<&'a str> {
    core::str::from_utf8(cstr_bytes(p, max)?).ok()
}
