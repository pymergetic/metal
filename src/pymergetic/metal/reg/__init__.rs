//! Cross-lang registry bus — two tiers, both keyed by full module names
//! like `pymergetic.metal.fs.open` (Locked #2).
#![deny(warnings)]
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

#[path = "_bind.rs"]
mod bind;
#[path = "_bulk.rs"]
mod bulk;
#[path = "_c_desc.rs"]
mod c_desc;
#[path = "_declare.rs"]
mod declare;
#[path = "_dynbuf.rs"]
mod dynbuf;
#[path = "_entry.rs"]
mod entry;
#[path = "_floor.rs"]
mod floor;
#[path = "_kernel.rs"]
mod kernel;
#[path = "_ledger.rs"]
mod ledger;
#[path = "_macros.rs"]
#[macro_use]
mod macros;
#[path = "_spin.rs"]
mod spin;
#[path = "_table.rs"]
mod table;

use core::ffi::c_void;

use pymergetic_metal_rt as _;

use bind::bind as bind_key;
use table::{cstr_bytes, Table, FUNC_MAX, MODULE_MAX};

pub use bulk::{register_rows, register_rows_bytes};
#[allow(deprecated)]
pub use declare::{ImportRow, RegImport, RegModStatic};
#[allow(deprecated)]
pub use entry::{RegEntry, RegExport};
pub use floor::{publish_exports, publish_exports_meta};
pub use kernel::{find_mod, HookFn, RegMod};
pub use ledger::{
    HONESTY_INCOMPLETE, HONESTY_OK, HONESTY_STUB, LANG_C, LANG_PY, LANG_RS, ROLE_FACE, ROLE_MUSCLE,
    ROLE_SHIM, ROLE_TRAMPOLINE, VIA_BIND, VIA_GUEST_FWD, VIA_IMPORT_ROW, VIA_PY_ATTR,
};

/// Cold-ledger append (inspect only). Thin Rust wrapper over the ledger table.
pub fn ledger_add_callee(
    module: &[u8],
    func: &[u8],
    lang: u8,
    role: u8,
    honesty: u8,
    sync: bool,
    async_: bool,
    partner: &[u8],
    label: &[u8],
    ptr: *const c_void,
) -> i32 {
    ledger::LEDGER.add_callee(
        module, func, lang, role, honesty, sync, async_, partner, label, ptr,
    )
}

pub fn ledger_add_caller(
    module: &[u8],
    func: &[u8],
    lang: u8,
    caller_module: &[u8],
    via: u8,
    honesty: u8,
) -> i32 {
    ledger::LEDGER.add_caller(module, func, lang, caller_module, via, honesty)
}

/// Fill `row` from the kernel table when its slot is still null.
/// Used by generated always-proxy faces (path-included RegImports are
/// often not listed on any `RegMod.imports` slice, so `connect_all`
/// alone cannot see them). After the first successful resolve the slot
/// stays hot -- subsequent calls are one atomic load.
#[inline]
pub fn resolve_import(row: &RegImport) {
    if row.entry().is_none() {
        row.set(kernel::find_entry(row.module, row.func));
    }
}

static TABLE: Table = Table::new();

/// Resolve `(full_module, func)` across both registry tiers: the late
/// dynamic table first, then the static `RegMod` kernel ring (wasm packs
/// and floor modules). One name space for call-by-name convenience APIs.
fn bind_any(full_module: *const u8, func: *const u8) -> *const c_void {
    let p = bind_key(&TABLE, full_module, func);
    if !p.is_null() {
        return p;
    }
    let Some(m) = cstr_bytes(full_module, MODULE_MAX) else {
        return core::ptr::null();
    };
    let Some(f) = cstr_bytes(func, FUNC_MAX) else {
        return core::ptr::null();
    };
    let Ok(m) = core::str::from_utf8(m) else {
        return core::ptr::null();
    };
    let Ok(f) = core::str::from_utf8(f) else {
        return core::ptr::null();
    };
    match kernel::find_entry(m, f) {
        Some(e) => e.get(),
        None => core::ptr::null(),
    }
}

/// Register or replace `(full_module, func)` -> `ptr`. Returns 0 or -1.
/// For runtime-chosen module/func pairs only (Python attach, wasm-mod
/// registration) — compile-time-known peers should be a plain Cargo
/// dependency + direct call instead (see module docs).
/// Also appends a cold-ledger callee row (lang C, role muscle) — inspect only.
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
    let rc = TABLE.register(m, f, ptr);
    if rc == 0 {
        let _ = ledger::LEDGER.add_callee(
            m,
            f,
            LANG_C,
            ROLE_MUSCLE,
            HONESTY_OK,
            false,
            false,
            b"",
            b"dyn_register",
            ptr,
        );
    }
    rc
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
    let p = bind_any(full_module, func);
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
    bind_any(full_module, func)
}

/// Convenience: call a registered `extern "C" fn() -> i32`. Returns the
/// callee result, or -1 if missing / null args.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_call0(full_module: *const u8, func: *const u8) -> i32 {
    let p = bind_any(full_module, func);
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

/// Write the full name of the loaded module at `index` (0..mod_count)
/// into `name_out` (NUL-terminated). Returns 0 or -1.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_mod_at(
    index: u32,
    name_out: *mut u8,
    name_cap: u32,
) -> i32 {
    if name_out.is_null() || name_cap == 0 {
        return -1;
    }
    match kernel::mod_at(index as usize) {
        Some(m) => write_cstr(name_out, name_cap, m.name),
        None => -1,
    }
}

/// How many export slots a loaded module declares (0 if not loaded).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_mod_entry_count(full_module: *const u8) -> u32 {
    match cstr_str(full_module, MODULE_MAX).and_then(kernel::find_mod) {
        Some(m) => m.exports.len() as u32,
        None => 0,
    }
}

/// Write the short entry name at `index` for a loaded module; optionally
/// store the published pointer in `out_ptr` (may be null while unloaded
/// mid-lifecycle). Returns 0 or -1.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_mod_entry_at(
    full_module: *const u8,
    index: u32,
    name_out: *mut u8,
    name_cap: u32,
    out_ptr: *mut *const c_void,
) -> i32 {
    if name_out.is_null() || name_cap == 0 {
        return -1;
    }
    let Some(m) = cstr_str(full_module, MODULE_MAX).and_then(kernel::find_mod) else {
        return -1;
    };
    let Some(e) = m.exports.get(index as usize) else {
        return -1;
    };
    if write_cstr(name_out, name_cap, e.name) != 0 {
        return -1;
    }
    if !out_ptr.is_null() {
        *out_ptr = e.get();
    }
    0
}

/// Late/dynamic table slot at `index` (0..[`pm_metal_reg_count`]).
/// Writes module + func names and optional pointer. Returns 0 or -1.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_dyn_at(
    index: u32,
    module_out: *mut u8,
    module_cap: u32,
    func_out: *mut u8,
    func_cap: u32,
    out_ptr: *mut *const c_void,
) -> i32 {
    if module_out.is_null()
        || func_out.is_null()
        || module_cap == 0
        || func_cap == 0
    {
        return -1;
    }
    let mut mod_buf = [0u8; MODULE_MAX];
    let mut func_buf = [0u8; FUNC_MAX];
    let mut ptr = core::ptr::null();
    if !TABLE.at(index as usize, &mut mod_buf, &mut func_buf, &mut ptr) {
        return -1;
    }
    let ml = mod_buf.iter().position(|&b| b == 0).unwrap_or(MODULE_MAX);
    let fl = func_buf.iter().position(|&b| b == 0).unwrap_or(FUNC_MAX);
    if ml + 1 > module_cap as usize || fl + 1 > func_cap as usize {
        return -1;
    }
    core::ptr::copy_nonoverlapping(mod_buf.as_ptr(), module_out, ml + 1);
    core::ptr::copy_nonoverlapping(func_buf.as_ptr(), func_out, fl + 1);
    if !out_ptr.is_null() {
        *out_ptr = ptr;
    }
    0
}

/* ---- Cold ledger (inspect only; never on hot call path) ---- */

#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_ledger_add_callee(
    full_module: *const u8,
    func: *const u8,
    lang: u8,
    role: u8,
    honesty: u8,
    sync: i32,
    async_: i32,
    partner: *const u8,
    label: *const u8,
    ptr: *const c_void,
) -> i32 {
    let Some(m) = cstr_bytes(full_module, MODULE_MAX) else {
        return -1;
    };
    let Some(f) = cstr_bytes(func, FUNC_MAX) else {
        return -1;
    };
    let p = if partner.is_null() {
        &[][..]
    } else {
        cstr_bytes(partner, FUNC_MAX).unwrap_or(&[])
    };
    let l = if label.is_null() {
        &[][..]
    } else {
        cstr_bytes(label, ledger::LEDGER_LABEL_MAX).unwrap_or(&[])
    };
    ledger::LEDGER.add_callee(m, f, lang, role, honesty, sync != 0, async_ != 0, p, l, ptr)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_ledger_add_caller(
    full_module: *const u8,
    func: *const u8,
    lang: u8,
    caller_module: *const u8,
    via: u8,
    honesty: u8,
) -> i32 {
    let Some(m) = cstr_bytes(full_module, MODULE_MAX) else {
        return -1;
    };
    let Some(f) = cstr_bytes(func, FUNC_MAX) else {
        return -1;
    };
    let Some(cm) = cstr_bytes(caller_module, MODULE_MAX) else {
        return -1;
    };
    ledger::LEDGER.add_caller(m, f, lang, cm, via, honesty)
}

#[no_mangle]
pub extern "C" fn pm_metal_reg_ledger_method_count() -> u32 {
    ledger::LEDGER.method_count() as u32
}

#[no_mangle]
pub extern "C" fn pm_metal_reg_ledger_gap_count() -> u32 {
    ledger::LEDGER.gap_count()
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_ledger_json(buf: *mut u8, cap: u32) -> i32 {
    if buf.is_null() || cap == 0 {
        return -1;
    }
    let slice = core::slice::from_raw_parts_mut(buf, cap as usize);
    ledger::LEDGER.write_json(slice)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_ledger_module_json(
    full_module: *const u8,
    buf: *mut u8,
    cap: u32,
) -> i32 {
    if buf.is_null() || cap == 0 {
        return -1;
    }
    let Some(m) = cstr_bytes(full_module, MODULE_MAX) else {
        return -1;
    };
    let slice = core::slice::from_raw_parts_mut(buf, cap as usize);
    ledger::LEDGER.write_module_json(m, slice)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_ledger_method_json(
    full_module: *const u8,
    func: *const u8,
    buf: *mut u8,
    cap: u32,
) -> i32 {
    if buf.is_null() || cap == 0 {
        return -1;
    }
    let Some(m) = cstr_bytes(full_module, MODULE_MAX) else {
        return -1;
    };
    let Some(f) = cstr_bytes(func, FUNC_MAX) else {
        return -1;
    };
    let slice = core::slice::from_raw_parts_mut(buf, cap as usize);
    ledger::LEDGER.write_method_json(m, f, slice)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_ledger_json_heap(out_buf: *mut *mut u8) -> i32 {
    if out_buf.is_null() {
        return -1;
    }
    let (p, n) = ledger::LEDGER.write_json_heap();
    if p.is_null() && n > 0 {
        return -1;
    }
    *out_buf = p;
    n as i32
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_ledger_module_json_heap(
    full_module: *const u8,
    out_buf: *mut *mut u8,
) -> i32 {
    if out_buf.is_null() {
        return -1;
    }
    let Some(m) = cstr_bytes(full_module, MODULE_MAX) else {
        return -1;
    };
    let (p, n) = ledger::LEDGER.write_module_json_heap(m);
    if p.is_null() && n > 0 {
        return -1;
    }
    if p.is_null() {
        return -1;
    }
    *out_buf = p;
    n as i32
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_ledger_method_json_heap(
    full_module: *const u8,
    func: *const u8,
    out_buf: *mut *mut u8,
) -> i32 {
    if out_buf.is_null() {
        return -1;
    }
    let Some(m) = cstr_bytes(full_module, MODULE_MAX) else {
        return -1;
    };
    let Some(f) = cstr_bytes(func, FUNC_MAX) else {
        return -1;
    };
    let (p, n) = ledger::LEDGER.write_method_json_heap(m, f);
    if p.is_null() {
        return -1;
    }
    *out_buf = p;
    n as i32
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_ledger_completeness_heap(
    module: *const u8,
    gaps_only: i32,
    detail: i32,
    fmt_json: i32,
    out_buf: *mut *mut u8,
) -> i32 {
    if out_buf.is_null() {
        return -1;
    }
    let filter = if module.is_null() {
        None
    } else {
        cstr_bytes(module, MODULE_MAX).filter(|m| !m.is_empty())
    };
    let (p, n) = ledger::LEDGER.write_completeness_heap(ledger::CompletenessOpts {
        module: filter,
        gaps_only: gaps_only != 0,
        detail: detail != 0,
        json: fmt_json != 0,
    });
    if p.is_null() && n > 0 {
        return -1;
    }
    *out_buf = p;
    n as i32
}

/// Completeness tree/JSON rollup. `module` NULL or empty = all modules.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_ledger_completeness(
    module: *const u8,
    gaps_only: i32,
    detail: i32,
    fmt_json: i32,
    buf: *mut u8,
    cap: u32,
) -> i32 {
    if buf.is_null() || cap == 0 {
        return -1;
    }
    let filter = if module.is_null() {
        None
    } else {
        cstr_bytes(module, MODULE_MAX).filter(|m| !m.is_empty())
    };
    let slice = core::slice::from_raw_parts_mut(buf, cap as usize);
    ledger::LEDGER.write_completeness(
        ledger::CompletenessOpts {
            module: filter,
            gaps_only: gaps_only != 0,
            detail: detail != 0,
            json: fmt_json != 0,
        },
        slice,
    )
}

/// Deprecated: ledger rows come from `RegExport` publish via `mod_load`.
/// Kept as a no-op so existing Inspect/glue call sites stay green.
#[no_mangle]
pub extern "C" fn pm_metal_reg_ledger_seed_pilot() -> i32 {
    0
}

/// Boot/smoke proof: walk loaded modules + one published entry.
/// Caller logs `reg reflect ok` on success (keeps host smoke free of log).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_proof_reflect() -> i32 {
    let nmod = pm_metal_reg_mod_count();
    if nmod == 0 {
        return -1;
    }
    let mut found = false;
    let mut i = 0u32;
    while i < nmod {
        let mut mod_name = [0u8; MODULE_MAX];
        if pm_metal_reg_mod_at(i, mod_name.as_mut_ptr(), MODULE_MAX as u32) != 0 {
            return -1;
        }
        let nent = pm_metal_reg_mod_entry_count(mod_name.as_ptr());
        let mut j = 0u32;
        while j < nent {
            let mut ename = [0u8; FUNC_MAX];
            let mut ptr: *const c_void = core::ptr::null();
            if pm_metal_reg_mod_entry_at(
                mod_name.as_ptr(),
                j,
                ename.as_mut_ptr(),
                FUNC_MAX as u32,
                &mut ptr,
            ) != 0
            {
                return -1;
            }
            if !ptr.is_null() {
                found = true;
                break;
            }
            j += 1;
        }
        if found {
            break;
        }
        i += 1;
    }
    if !found {
        return -1;
    }
    /* Late table must also be indexable when non-empty. */
    let ndyn = pm_metal_reg_count();
    if ndyn > 0 {
        let mut m = [0u8; MODULE_MAX];
        let mut f = [0u8; FUNC_MAX];
        let mut p: *const c_void = core::ptr::null();
        if pm_metal_reg_dyn_at(
            0,
            m.as_mut_ptr(),
            MODULE_MAX as u32,
            f.as_mut_ptr(),
            FUNC_MAX as u32,
            &mut p,
        ) != 0
            || p.is_null()
        {
            return -1;
        }
    }
    0
}

fn write_cstr(dst: *mut u8, cap: u32, s: &str) -> i32 {
    let cap = cap as usize;
    if dst.is_null() || cap == 0 || s.len() + 1 > cap {
        return -1;
    }
    unsafe {
        core::ptr::copy_nonoverlapping(s.as_ptr(), dst, s.len());
        *dst.add(s.len()) = 0;
    }
    0
}

fn cstr_str<'a>(p: *const u8, max: usize) -> Option<&'a str> {
    core::str::from_utf8(cstr_bytes(p, max)?).ok()
}
