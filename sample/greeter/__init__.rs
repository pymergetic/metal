//! sample.greeter — minimal Rust wasm pack with two nested source-level
//! submodules (`words`, `numbers`). Neither has its own `.pm/module`:
//! `sample/` is not scanned by `forge mod sync` (that only walks `src/`),
//! so a nested `type=module` here would never actually get synced --
//! they're plain Rust `mod` files compiled straight into this one crate,
//! same as `mem/arena`/`mem/tlsf` are plain source under a `type=module`
//! parent for kernel-linked modules. Both submodules' `#[no_mangle]`
//! exports land in the one compiled `sample_greeter.wasm`, callable by
//! `sample.announcer` across the package boundary (see its `imports`).
#![no_std]

mod numbers;
mod words;

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
