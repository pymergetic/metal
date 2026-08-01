//! Registry bootstrap — the loader's one call before the rest of bring-up.
//!
//! First loads the kernel namespace root itself (`pymergetic.metal`,
//! module #0 in `pymergetic_metal_reg`'s kernel table — see
//! `docs/definitions/module.md` "Kernel is a module too"), through its
//! generated fast-path face (`pymergetic.metal` is permanently
//! `unloadable = false`, so this is a plain link-time `extern "C"` call,
//! not a registry lookup).
//!
//! Then runs `rt`'s dynamic-table register/connect step: `rt` registers
//! `halt`/`panic`/`panic_at` onto the registry's *dynamic* table (a
//! separate, older tier — see `reg/_impl/__init__.rs`) for genuinely
//! late/dynamic callers (wasm guest code, Python) that cannot
//! Cargo-depend on it directly — see `rt/_impl/_ffi.rs`.
//!
//! Every other statically-linked floor module (console, mem, dt, log, the
//! detectors, async, ...) does not go through either tier for its own
//! exports; peers call it through its own generated fast-path face
//! instead (see `docs/definitions/module.md`).
//!
//! Kept as a named, callable step (rather than inlined into `bringup.c`)
//! so the loader's C ABI surface (`pm_metal_boot_reg_bootstrap`) doesn't
//! change if a future genuinely-unloadable module needs the same hook.

#[path = "../../../../../include/pymergetic/metal/__init__.rs"]
mod kernel_face;

extern "C" {
    fn pm_metal_rt_register_symbols() -> i32;
    fn pm_metal_rt_connect_symbols() -> i32;
}

/// Bootstrap the registry: load the kernel module first, then `rt`'s own
/// dynamic-table register + connect step. Call once, before the rest of
/// bring-up (`mem_init`, `console_init0`, ...). Returns 0, or -1 on
/// failure.
pub unsafe fn reg_bootstrap() -> i32 {
    if kernel_face::pm_metal_kernel_load() != 0 {
        return -1;
    }
    if pm_metal_rt_register_symbols() != 0 {
        return -1;
    }
    if pm_metal_rt_connect_symbols() != 0 {
        return -1;
    }
    0
}
