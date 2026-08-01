//! Host smoke — WAMR load → reg → call0 (one Metal memory).
use std::alloc::{alloc, Layout};

use pymergetic_metal_mem::api as mem;
use pymergetic_metal_reg::pm_metal_reg_call0;
use pymergetic_metal_wasm::{
    pm_metal_wasm_call0, pm_metal_wasm_init, pm_metal_wasm_load_publish, pm_metal_wasm_ready,
    pm_metal_wasm_shutdown,
};

fn main() {
    const N: usize = 8 * 1024 * 1024;
    let layout = Layout::from_size_align(N, 4096).unwrap();
    let base = unsafe { alloc(layout) };
    assert!(!base.is_null());
    assert_eq!(mem::init(base, N), 0);

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
        let n = pm_metal_wasm_load_publish(mod_name.as_ptr(), wasm.as_ptr(), wasm.len() as u32);
        assert!(n >= 1, "publish count {n}");
        assert_eq!(pm_metal_wasm_call0(mod_name.as_ptr(), b"ready\0".as_ptr()), 0);
        assert_eq!(pm_metal_reg_call0(mod_name.as_ptr(), b"ready\0".as_ptr()), 0);
    }

    pm_metal_wasm_shutdown();
    assert_eq!(pm_metal_wasm_ready(), 0);
    eprintln!("wasm W5.2 smoke ok");
}
