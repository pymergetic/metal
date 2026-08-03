//! tests.wasm_guest_surfaces — W16.3a: fs cookie I/O + gfx blit/present.
#![no_std]

#[link(wasm_import_module = "pymergetic.metal.log")]
extern "C" {
    fn pm_metal_log(line: *const u8);
}

#[link(wasm_import_module = "pymergetic.metal.mem")]
extern "C" {
    fn pm_metal_mem_alloc(size: u32) -> u32;
    fn pm_metal_mem_free(cookie: u32);
    fn pm_metal_mem_copy_in(dest: u32, src_off: u32, n: u32) -> i32;
    fn pm_metal_mem_copy_out(src: u32, dest_off: u32, n: u32) -> i32;
}

#[link(wasm_import_module = "pymergetic.metal.fs")]
extern "C" {
    fn pm_metal_fs_mkdir_async(path: *const u8) -> u32;
    fn pm_metal_fs_write_mem_async(path: *const u8, src: u32, src_len: u32) -> u32;
    fn pm_metal_fs_size_async(path: *const u8) -> u32;
    fn pm_metal_fs_read_mem_async(path: *const u8, dest: u32, dest_len: u32) -> u32;
    fn pm_metal_fs_result(h: u32) -> u32;
}

#[link(wasm_import_module = "pymergetic.metal.dev.gfx")]
extern "C" {
    fn pm_metal_dev_gfx_ready() -> i32;
    fn pm_metal_dev_gfx_width() -> i32;
    fn pm_metal_dev_gfx_height() -> i32;
    fn pm_metal_dev_gfx_blit_bgra(
        dx: i32,
        dy: i32,
        dw: i32,
        dh: i32,
        pixels: u32,
        src_w: i32,
        src_h: i32,
        src_pitch: i32,
    ) -> i32;
    fn pm_metal_dev_gfx_present_async(surface: u32) -> u32;
}

/// Pack export: fs round-trip + optional gfx blit; return 0 ok.
#[no_mangle]
pub extern "C" fn ready() -> i32 {
    unsafe {
        /* --- fs: mkdir + write_mem + size + read_mem --- */
        let dir = b"/guest_surf\0";
        let path = b"/guest_surf/proof.bin\0";
        let _ = pm_metal_fs_mkdir_async(dir.as_ptr());
        let mut payload = [0xABu8, 0xCD, 0xEF, 0x01, 0x23, 0x45, 0x67, 0x89];
        let c = pm_metal_mem_alloc(8);
        if c == 0 {
            return -1;
        }
        if pm_metal_mem_copy_in(c, payload.as_mut_ptr() as u32, 8) != 0 {
            pm_metal_mem_free(c);
            return -2;
        }
        let wh = pm_metal_fs_write_mem_async(path.as_ptr(), c, 8);
        if pm_metal_fs_result(wh) != 8 {
            pm_metal_mem_free(c);
            return -3;
        }
        let sh = pm_metal_fs_size_async(path.as_ptr());
        if pm_metal_fs_result(sh) != 8 {
            pm_metal_mem_free(c);
            return -4;
        }
        let c2 = pm_metal_mem_alloc(8);
        if c2 == 0 {
            pm_metal_mem_free(c);
            return -5;
        }
        let rh = pm_metal_fs_read_mem_async(path.as_ptr(), c2, 8);
        if pm_metal_fs_result(rh) != 8 {
            pm_metal_mem_free(c);
            pm_metal_mem_free(c2);
            return -6;
        }
        let mut got = [0u8; 8];
        if pm_metal_mem_copy_out(c2, got.as_mut_ptr() as u32, 8) != 0 {
            pm_metal_mem_free(c);
            pm_metal_mem_free(c2);
            return -7;
        }
        pm_metal_mem_free(c);
        pm_metal_mem_free(c2);
        let mut i = 0usize;
        while i < 8 {
            if payload[i] != got[i] {
                return -8;
            }
            i += 1;
        }
        pm_metal_log(b"guest fs ok\0".as_ptr());

        /* --- gfx: 8x8 solid blit + present_async when FB ready --- */
        if pm_metal_dev_gfx_ready() != 0 {
            let w = pm_metal_dev_gfx_width();
            let h = pm_metal_dev_gfx_height();
            if w <= 0 || h <= 0 {
                return -9;
            }
            let mut pix = [0u32; 64];
            let mut p = 0usize;
            while p < 64 {
                pix[p] = 0xff_3a_7a_d0u32;
                p += 1;
            }
            if pm_metal_dev_gfx_blit_bgra(
                16,
                16,
                8,
                8,
                pix.as_mut_ptr() as u32,
                8,
                8,
                8 * 4,
            ) != 0
            {
                return -10;
            }
            let ph = pm_metal_dev_gfx_present_async(0);
            if ph == 0 {
                return -11;
            }
            pm_metal_log(b"guest gfx ok\0".as_ptr());
        } else {
            pm_metal_log(b"guest gfx skip\0".as_ptr());
        }
    }
    0
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
