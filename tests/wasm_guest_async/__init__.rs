//! tests.wasm_guest_async — kernel async + mem cookie natives.
#![no_std]

#[link(wasm_import_module = "pymergetic.metal.async.time")]
extern "C" {
    fn pm_metal_async_mono_us() -> u64;
    fn pm_metal_async_yield() -> u32;
    fn pm_metal_async_sleep(ms: u32) -> u32;
}

#[link(wasm_import_module = "pymergetic.metal.mem")]
extern "C" {
    fn pm_metal_mem_alloc(size: u32) -> u32;
    fn pm_metal_mem_free(cookie: u32);
    fn pm_metal_mem_copy_in(dest: u32, src_off: u32, n: u32) -> i32;
    fn pm_metal_mem_copy_out(src: u32, dest_off: u32, n: u32) -> i32;
}

#[link(wasm_import_module = "pymergetic.metal.log")]
extern "C" {
    fn pm_metal_log(line: *const u8);
}

/// Pack export: exercise yield + mem cookie copy; return 0 ok.
#[no_mangle]
pub extern "C" fn ready() -> i32 {
    unsafe {
        let _t0 = pm_metal_async_mono_us();
        let y = pm_metal_async_yield();
        if y == 0 {
            return -1;
        }
        let _s = pm_metal_async_sleep(0);
        let c = pm_metal_mem_alloc(8);
        if c == 0 {
            return -2;
        }
        let mut src = [0x11u8, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88];
        let mut dst = [0u8; 8];
        if pm_metal_mem_copy_in(c, src.as_mut_ptr() as u32, 8) != 0 {
            pm_metal_mem_free(c);
            return -3;
        }
        if pm_metal_mem_copy_out(c, dst.as_mut_ptr() as u32, 8) != 0 {
            pm_metal_mem_free(c);
            return -4;
        }
        pm_metal_mem_free(c);
        let mut i = 0usize;
        while i < 8 {
            if src[i] != dst[i] {
                return -5;
            }
            i += 1;
        }
        pm_metal_log(b"guest async ok\0".as_ptr());
    }
    0
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
