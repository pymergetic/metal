//! tests.wasm_hello — minimal Rust wasm pack (`ready` -> 0).
#![no_std]

/// Pack export: `() -> i32`. Loaded under full name `tests.wasm_hello`.
#[no_mangle]
pub extern "C" fn ready() -> i32 {
    0
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
