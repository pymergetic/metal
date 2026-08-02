//! sample.announcer — imports sample.greeter's hello()/lucky() across a
//! package boundary (a separate wasm binary, separate instance) and
//! combines them into one exported value. The `imports` array in
//! `.pm/module` only tells `forge pack` what to embed in this
//! package's own `.wasm` (a `pm_metal_imports` custom section -- see
//! docs/definitions/module.md "Cross-package imports") so the host
//! loader can register the right forwarding native when this package
//! loads; this `extern "C"` block with `#[link(wasm_import_module =
//! "...")]` is the real, compiler-checked half -- a wasm import is just
//! a `(module, name)` string pair resolved by the host at
//! instantiation time, same mechanism as any other wasm import.
#![no_std]

#[link(wasm_import_module = "sample.greeter")]
extern "C" {
    fn hello() -> i32;
    fn lucky() -> i32;
}

/// Pack export: `() -> i32`. Loaded under full name `sample.announcer`.
#[no_mangle]
pub extern "C" fn announce() -> i32 {
    unsafe { hello() + lucky() }
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
