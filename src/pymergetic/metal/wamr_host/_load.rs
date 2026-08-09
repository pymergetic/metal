//! load — validate args then port load.

extern "C" {
    fn pm_metal_wasm_port_load(
        full_module: *const u8,
        bytes: *const u8,
        len: u32,
    ) -> i32;
}

pub unsafe fn load(full_module: *const u8, bytes: *const u8, len: u32) -> i32 {
    if full_module.is_null() || bytes.is_null() || len == 0 {
        return -1;
    }
    pm_metal_wasm_port_load(full_module, bytes, len)
}
