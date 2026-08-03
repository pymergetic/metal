//! tests.wasm_guest_log — import kernel `pm_metal_log` via guest_surface.
#![no_std]

#[link(wasm_import_module = "pymergetic.metal.log")]
extern "C" {
    fn pm_metal_log(line: *const u8);
}

/// Pack export: log a fixed line, return 0.
#[no_mangle]
pub extern "C" fn ready() -> i32 {
    unsafe {
        pm_metal_log(b"guest log ok\0".as_ptr());
    }
    0
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
