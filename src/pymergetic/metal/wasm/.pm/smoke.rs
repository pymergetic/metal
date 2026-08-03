//! Host smoke — WAMR load -> register -> call (direct + through the
//! registry's import slot) -> quiesced unload.
use std::alloc::{alloc, Layout};
use std::cell::Cell;
use std::os::raw::c_void;

use pymergetic_metal_mem::api as mem;
use pymergetic_metal_reg::{
    pm_metal_reg_mod_connect_all, pm_metal_reg_mod_load, ImportRow, RegMod, RegModStatic,
};
use pymergetic_metal_wasm::{
    pm_metal_wasm_call0, pm_metal_wasm_init, pm_metal_wasm_load_register,
    pm_metal_wasm_proof_stress, pm_metal_wasm_ready, pm_metal_wasm_shutdown,
    pm_metal_wasm_unload,
};

/* A real cross-module consumer never calls `pm_metal_wasm_*` directly --
 * it declares an `ImportRow` for (module, func) and gets connected by
 * `_kernel::connect_all` whenever anything loads/unloads, same as any
 * other registry border. This synthetic importer proves the wasm pack's
 * exports actually land in the *same* kernel ring every other module
 * publishes into (see docs/definitions/module.md "wasm packages join
 * the same registry"), not the separate dynamic/late table
 * (`pm_metal_reg_register`/`_call0`, for Python attach). */
static IMPORTER_IMPORTS: RegModStatic<0, 1> =
    RegModStatic::new([], [ImportRow::new("tests.wasm_hello", "ready")]);

static IMPORTER_MOD: RegMod = RegMod {
    name: "wasm.smoke.importer",
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

fn resolved(row: &ImportRow) -> *const c_void {
    row.entry().map(|e| e.get()).unwrap_or(std::ptr::null())
}

/* Produced by `forge pack` (forge build runs pack all in prep) -- real
 * multi-crate packs (sample.greeter's words/numbers submodules compiled
 * into one wasm; sample.announcer's cross-package import of it), not
 * the hand-crafted single-export module above. */
static SAMPLE_GREETER: &[u8] = include_bytes!("../../../../../build/packs/sample.greeter.wasm");
static SAMPLE_ANNOUNCER: &[u8] = include_bytes!("../../../../../build/packs/sample.announcer.wasm");

fn floor_log_ready() {
    /* Always-proxy log needs console's RegMod + init (same as py smoke). */
    use pymergetic_metal_console as _;
    extern "C" {
        fn pm_metal_console_mod_load() -> i32;
        fn pm_metal_log_mod_load() -> i32;
        fn pm_metal_console_init0(ring_bytes: usize) -> i32;
    }
    unsafe {
        assert_eq!(pm_metal_console_mod_load(), 0);
        assert_eq!(pm_metal_log_mod_load(), 0);
        assert_eq!(pm_metal_console_init0(0), 0);
    }
}

fn main() {
    const N: usize = 8 * 1024 * 1024;
    let layout = Layout::from_size_align(N, 4096).unwrap();
    let base = unsafe { alloc(layout) };
    assert!(!base.is_null());
    assert_eq!(mem::init(base, N), 0);

    floor_log_ready();

    assert_eq!(pm_metal_wasm_init(), 0);
    assert_eq!(pm_metal_wasm_ready(), 1);

    // Minimal module: export ready() -> i32 { 0 }
    let wasm: &[u8] = &[
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x05, 0x01, 0x60, 0x00, 0x01,
        0x7f, 0x03, 0x02, 0x01, 0x00, 0x07, 0x09, 0x01, 0x05, 0x72, 0x65, 0x61, 0x64, 0x79,
        0x00, 0x00, 0x0a, 0x06, 0x01, 0x04, 0x00, 0x41, 0x00, 0x0b,
    ];
    let mod_name = b"tests.wasm_hello\0";
    unsafe {
        // Importer loads first (its own import slot resolves later, on
        // the wasm pack's own `connect_all` pass -- N x M, same as every
        // other module addition).
        assert_eq!(pm_metal_reg_mod_load(&IMPORTER_MOD), 0);
        assert!(resolved(&IMPORTER_IMPORTS.imports[0]).is_null());

        let n = pm_metal_wasm_load_register(mod_name.as_ptr(), wasm.as_ptr(), wasm.len() as u32);
        assert!(n >= 1, "registered export count {n}");

        // Direct host<->guest call (bypasses the registry).
        assert_eq!(pm_metal_wasm_call0(mod_name.as_ptr(), b"ready\0".as_ptr()), 0);

        // Through the registry's import slot -- proves the wasm pack's
        // trampoline landed in the same kernel ring as any other module.
        let ptr = resolved(&IMPORTER_IMPORTS.imports[0]);
        assert!(!ptr.is_null());
        let f: extern "C" fn() -> i32 = std::mem::transmute(ptr);
        assert_eq!(f(), 0);

        // Cascading, quiesced unload through the registry (see
        // docs/definitions/module.md "wasm packages join the same
        // registry") -- the reconnect pass it triggers drops the
        // importer's slot back to null.
        assert_eq!(pm_metal_wasm_unload(mod_name.as_ptr()), 0);
        assert!(resolved(&IMPORTER_IMPORTS.imports[0]).is_null());
        pm_metal_reg_mod_connect_all();
        assert!(resolved(&IMPORTER_IMPORTS.imports[0]).is_null());

        // Regression: the module ring is a heap-allocated list, not a
        // fixed array -- prove there's no compile-time cap by loading
        // more at once than the old array-based design's MAX_MODS (8).
        const MANY: usize = 12;
        let names: Vec<Vec<u8>> = (0..MANY)
            .map(|i| format!("tests.wasm_hello_many.{i}\0").into_bytes())
            .collect();
        for name in &names {
            let n =
                pm_metal_wasm_load_register(name.as_ptr(), wasm.as_ptr(), wasm.len() as u32);
            assert!(n >= 1, "load {:?} failed: {n}", String::from_utf8_lossy(name));
        }
        for name in &names {
            assert_eq!(pm_metal_wasm_call0(name.as_ptr(), b"ready\0".as_ptr()), 0);
        }
        for name in &names {
            assert_eq!(pm_metal_wasm_unload(name.as_ptr()), 0);
        }

        // Regression: the trampoline pool is a heap-allocated, self-
        // stamped ring (see runtime_host.c's stamp_tramp/alloc_tramp),
        // not a fixed-size const-generic array -- one instance at a
        // time, but well past the old MAX_TRAMP=64 claims over the
        // life of the process, to prove claim/release genuinely
        // recycles/regrows rather than exhausting a compile-time pool.
        const CYCLES: usize = 96;
        let cyc_name = b"tests.wasm_hello_cycle\0";
        for _ in 0..CYCLES {
            let n = pm_metal_wasm_load_register(cyc_name.as_ptr(), wasm.as_ptr(), wasm.len() as u32);
            assert!(n >= 1, "cycle load failed: {n}");
            assert_eq!(pm_metal_wasm_call0(cyc_name.as_ptr(), b"ready\0".as_ptr()), 0);
            assert_eq!(pm_metal_wasm_unload(cyc_name.as_ptr()), 0);
        }

        // Cross-package import: sample.announcer imports sample.greeter's
        // hello()/lucky() (see sample/announcer/.pm/module "imports"). The
        // native for sample.greeter's hello/lucky gets registered when
        // sample.announcer itself loads below (forge embedded its declared
        // imports as a custom wasm section; runtime_host.c reads it back
        // and registers a forwarding native before instantiating) -- load
        // order between provider and consumer here only affects what the
        // forward call reaches at call time, not whether the import
        // resolves.
        let greeter_name = b"sample.greeter\0";
        let announcer_name = b"sample.announcer\0";
        let n = pm_metal_wasm_load_register(
            greeter_name.as_ptr(),
            SAMPLE_GREETER.as_ptr(),
            SAMPLE_GREETER.len() as u32,
        );
        assert!(n >= 1, "sample.greeter register failed: {n}");
        let n = pm_metal_wasm_load_register(
            announcer_name.as_ptr(),
            SAMPLE_ANNOUNCER.as_ptr(),
            SAMPLE_ANNOUNCER.len() as u32,
        );
        assert!(n >= 1, "sample.announcer register failed: {n}");
        assert_eq!(
            pm_metal_wasm_call0(announcer_name.as_ptr(), b"announce\0".as_ptr()),
            12,
            "hello()+lucky() via cross-package import"
        );

        // Cascading unload: withdraw the provider first. `announce()`
        // must not crash -- the forwarding native (`fwd_native` in
        // runtime_host.c) degrades to -1 per call whose target instance
        // is gone, the same "guest went away" behavior any quiesced
        // unload has, not a special case for cross-package imports.
        assert_eq!(pm_metal_wasm_unload(greeter_name.as_ptr()), 0);
        assert_eq!(
            pm_metal_wasm_call0(announcer_name.as_ptr(), b"announce\0".as_ptr()),
            -2,
            "hello()+lucky() both -1 once sample.greeter is unloaded"
        );

        // Regression: `fwd_native` caches its resolved slot pointer
        // (`fwd_reg_t.cached`) after the first successful call above --
        // reloading the provider under the same name must not leave the
        // forwarding native pointing at the freed slot_t that
        // `pm_metal_wasm_unload` just released. `free_slot` invalidates
        // every `fwd_reg_t.cached` referencing it, so this must resolve
        // fresh and land back on 12, not crash/UB from a dangling
        // pointer and not silently stay stuck at -2.
        let n = pm_metal_wasm_load_register(
            greeter_name.as_ptr(),
            SAMPLE_GREETER.as_ptr(),
            SAMPLE_GREETER.len() as u32,
        );
        assert!(n >= 1, "sample.greeter reload failed: {n}");
        assert_eq!(
            pm_metal_wasm_call0(announcer_name.as_ptr(), b"announce\0".as_ptr()),
            12,
            "fwd_native must re-resolve after provider reload, not reuse a freed cache"
        );
        assert_eq!(pm_metal_wasm_unload(greeter_name.as_ptr()), 0);
        assert_eq!(pm_metal_wasm_unload(announcer_name.as_ptr()), 0);

        // W11.6: concurrent call stress + unload/reload under traffic.
        assert_eq!(
            pm_metal_wasm_proof_stress(),
            0,
            "wasm proof_stress failed"
        );
    }

    pm_metal_wasm_shutdown();
    assert_eq!(pm_metal_wasm_ready(), 0);
    eprintln!("wasm W5.2 smoke ok");
}
