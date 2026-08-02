//! Host smoke — dynamic layer (register/lookup/bind/call0) + static
//! per-module lifecycle (load/connect/quiesce/cascading unload).
use std::cell::Cell;
use std::os::raw::c_void;
use std::sync::atomic::{AtomicU32, Ordering};

use pymergetic_metal_reg::{
    pm_metal_reg_bind, pm_metal_reg_call0, pm_metal_reg_count, pm_metal_reg_lookup,
    pm_metal_reg_mod_connect_all, pm_metal_reg_mod_count, pm_metal_reg_mod_load,
    pm_metal_reg_mod_unload, pm_metal_reg_register, register_rows_bytes, ImportRow, RegEntry,
    RegMod, RegModStatic,
};

extern "C" fn smoke_forty_two() -> i32 {
    42
}

extern "C" fn smoke_seven() -> i32 {
    7
}

fn main() {
    let mod_name = b"pymergetic.metal.reg.smoke\0";
    let f_a = b"forty_two\0";
    let f_b = b"seven\0";
    let missing = b"missing\0";

    unsafe {
        assert_eq!(
            pm_metal_reg_register(
                mod_name.as_ptr(),
                f_a.as_ptr(),
                smoke_forty_two as *const c_void,
            ),
            0
        );
        assert_eq!(
            pm_metal_reg_register(mod_name.as_ptr(), f_b.as_ptr(), smoke_seven as *const c_void,),
            0
        );
        assert_eq!(pm_metal_reg_count(), 2);

        let mut out: *const c_void = std::ptr::null();
        assert_eq!(
            pm_metal_reg_lookup(mod_name.as_ptr(), f_a.as_ptr(), &mut out),
            0
        );
        assert!(!out.is_null());
        assert_eq!(out, smoke_forty_two as *const c_void);

        let bound = pm_metal_reg_bind(mod_name.as_ptr(), f_b.as_ptr());
        assert_eq!(bound, smoke_seven as *const c_void);

        assert_eq!(pm_metal_reg_call0(mod_name.as_ptr(), f_a.as_ptr()), 42);
        assert_eq!(pm_metal_reg_call0(mod_name.as_ptr(), f_b.as_ptr()), 7);
        assert_eq!(pm_metal_reg_call0(mod_name.as_ptr(), missing.as_ptr()), -1);

        // Replace existing
        assert_eq!(
            pm_metal_reg_register(mod_name.as_ptr(), f_a.as_ptr(), smoke_seven as *const c_void,),
            0
        );
        assert_eq!(pm_metal_reg_call0(mod_name.as_ptr(), f_a.as_ptr()), 7);
        assert_eq!(pm_metal_reg_count(), 2);

        // Reject empty / null
        assert_eq!(
            pm_metal_reg_register(mod_name.as_ptr(), f_a.as_ptr(), std::ptr::null()),
            -1
        );
        assert_eq!(
            pm_metal_reg_register(std::ptr::null(), f_a.as_ptr(), smoke_seven as *const c_void),
            -1
        );

        // W7.1 helpers: bulk register under one module
        let bulk_mod = b"pymergetic.metal.reg.bulk\0";
        assert_eq!(
            register_rows_bytes(
                bulk_mod,
                &[
                    (b"a\0", smoke_forty_two as *const c_void),
                    (b"b\0", smoke_seven as *const c_void),
                ],
            ),
            0
        );
        assert_eq!(pm_metal_reg_call0(bulk_mod.as_ptr(), b"a\0".as_ptr()), 42);
        assert_eq!(pm_metal_reg_call0(bulk_mod.as_ptr(), b"b\0".as_ptr()), 7);
    }

    eprintln!("reg smoke ok");
    lifecycle_smoke();
}

// --- Static per-module lifecycle: synthetic unloadable module + child --

static PARENT_ENTRIES: RegModStatic<1, 0> = RegModStatic::new([RegEntry::new("answer")], []);
static CHILD_ENTRIES: RegModStatic<1, 0> = RegModStatic::new([RegEntry::new("child_val")], []);
static IMPORTER_IMPORTS: RegModStatic<0, 1> = RegModStatic::new(
    [],
    [ImportRow::new(
        "pymergetic.metal.reg.smoke.lifecycle.parent",
        "answer",
    )],
);

static PARENT_LOADS: AtomicU32 = AtomicU32::new(0);
static PARENT_REGISTERED: AtomicU32 = AtomicU32::new(0);
static PARENT_DEREGISTERED: AtomicU32 = AtomicU32::new(0);
static PARENT_UNLOADED: AtomicU32 = AtomicU32::new(0);
static CHILD_UNLOADED: AtomicU32 = AtomicU32::new(0);

extern "C" fn parent_answer_fn() -> i32 {
    42
}

extern "C" fn parent_on_load(_ctx: *mut c_void) -> i32 {
    PARENT_LOADS.fetch_add(1, Ordering::SeqCst);
    0
}

extern "C" fn parent_register(_ctx: *mut c_void) -> i32 {
    PARENT_ENTRIES.entries[0].publish(parent_answer_fn as *const c_void);
    PARENT_REGISTERED.fetch_add(1, Ordering::SeqCst);
    0
}

extern "C" fn parent_deregister(_ctx: *mut c_void) -> i32 {
    PARENT_DEREGISTERED.fetch_add(1, Ordering::SeqCst);
    0
}

extern "C" fn parent_on_unload(_ctx: *mut c_void) -> i32 {
    PARENT_UNLOADED.fetch_add(1, Ordering::SeqCst);
    0
}

extern "C" fn child_val_fn() -> i32 {
    99
}

extern "C" fn child_register(_ctx: *mut c_void) -> i32 {
    CHILD_ENTRIES.entries[0].publish(child_val_fn as *const c_void);
    0
}

extern "C" fn child_on_unload(_ctx: *mut c_void) -> i32 {
    CHILD_UNLOADED.fetch_add(1, Ordering::SeqCst);
    0
}

static PARENT_MOD: RegMod = RegMod {
    name: "pymergetic.metal.reg.smoke.lifecycle.parent",
    unloadable: true,
    parent: None,
    ctx: std::ptr::null_mut(),
    on_load: Some(parent_on_load),
    register_symbols: Some(parent_register),
    connect_symbols: None,
    on_registrations_updated: None,
    deregister_symbols: Some(parent_deregister),
    on_unload: Some(parent_on_unload),
    entries: &PARENT_ENTRIES.entries,
    imports: &[],
    raw_next: Cell::new(std::ptr::null()),
    raw_prev: Cell::new(std::ptr::null()),
};

static CHILD_MOD: RegMod = RegMod {
    name: "pymergetic.metal.reg.smoke.lifecycle.child",
    unloadable: true,
    parent: Some("pymergetic.metal.reg.smoke.lifecycle.parent"),
    ctx: std::ptr::null_mut(),
    on_load: None,
    register_symbols: Some(child_register),
    connect_symbols: None,
    on_registrations_updated: None,
    deregister_symbols: None,
    on_unload: Some(child_on_unload),
    entries: &CHILD_ENTRIES.entries,
    imports: &[],
    raw_next: Cell::new(std::ptr::null()),
    raw_prev: Cell::new(std::ptr::null()),
};

static IMPORTER_MOD: RegMod = RegMod {
    name: "pymergetic.metal.reg.smoke.lifecycle.importer",
    unloadable: false,
    parent: None,
    ctx: std::ptr::null_mut(),
    on_load: None,
    register_symbols: None,
    connect_symbols: None,
    on_registrations_updated: None,
    deregister_symbols: None,
    on_unload: None,
    entries: &[],
    imports: &IMPORTER_IMPORTS.imports,
    raw_next: Cell::new(std::ptr::null()),
    raw_prev: Cell::new(std::ptr::null()),
};

/// Full cycle: `on_load` -> `register_symbols` -> global connect -> a
/// cross-module call through the resolved import slot -> cascading
/// unload (child first, quiesced -- no async engine is running in this
/// host smoke, so the quiesce wait resolves immediately, same as before
/// quiesce existed) -> reconnect drops the importer's slot back to noop
/// -> the (`unloadable: false`) importer itself refuses unload. Exercises
/// every lifecycle hook for real, not just scaffolding that compiles.
fn lifecycle_smoke() {
    unsafe {
        assert_eq!(pm_metal_reg_mod_load(&PARENT_MOD), 0);
        assert_eq!(PARENT_LOADS.load(Ordering::SeqCst), 1);
        assert_eq!(PARENT_REGISTERED.load(Ordering::SeqCst), 1);

        assert_eq!(pm_metal_reg_mod_load(&CHILD_MOD), 0);
        assert_eq!(pm_metal_reg_mod_load(&IMPORTER_MOD), 0);
        pm_metal_reg_mod_connect_all();
        assert_eq!(pm_metal_reg_mod_count(), 3);

        // Importer's import slot resolved against the parent's published
        // entry purely via connect (never a direct Cargo dependency).
        let row = &IMPORTER_IMPORTS.imports[0];
        let ptr = row.entry().map(|e| e.get()).unwrap_or(std::ptr::null());
        assert!(!ptr.is_null());
        let f: extern "C" fn() -> i32 = std::mem::transmute(ptr);
        assert_eq!(f(), 42);

        // Unload cascades into the child first, quiesced (no per-call
        // refcount gate anymore -- see docs/definitions/module.md).
        let parent_name = b"pymergetic.metal.reg.smoke.lifecycle.parent\0";
        assert_eq!(pm_metal_reg_mod_unload(parent_name.as_ptr()), 0);
        assert_eq!(CHILD_UNLOADED.load(Ordering::SeqCst), 1);
        assert_eq!(PARENT_DEREGISTERED.load(Ordering::SeqCst), 1);
        assert_eq!(PARENT_UNLOADED.load(Ordering::SeqCst), 1);
        assert!(CHILD_ENTRIES.entries[0].get().is_null());
        assert!(PARENT_ENTRIES.entries[0].get().is_null());

        // The unload's own reconnect pass already dropped the importer's
        // slot back to noop; a redundant explicit pass must agree.
        let resolved = |r: &ImportRow| r.entry().map(|e| e.get()).unwrap_or(std::ptr::null());
        assert!(resolved(row).is_null());
        pm_metal_reg_mod_connect_all();
        assert!(resolved(row).is_null());
        assert_eq!(pm_metal_reg_mod_count(), 1); // importer only

        // The importer is `unloadable: false` (a permanently-linked
        // consumer, matching a real fast-path module that still needs a
        // registry-proxy import slot connected) -- `unload` must refuse
        // it outright, same as the kernel namespace root itself.
        let importer_name = b"pymergetic.metal.reg.smoke.lifecycle.importer\0";
        assert_eq!(pm_metal_reg_mod_unload(importer_name.as_ptr()), -1);
        assert_eq!(pm_metal_reg_mod_count(), 1);
    }

    eprintln!("reg lifecycle smoke ok");
}
