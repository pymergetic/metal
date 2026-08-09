//! Dev/externals C floor RegMods — load hooks only (declares in owning muscle TUs).

extern "C" {
    fn pm_metal_externals_reg_load() -> i32;
    fn pm_metal_dev_serial_reg_load() -> i32;
    fn pm_metal_dev_blk_reg_load() -> i32;
    fn pm_metal_dev_acpi_reg_load() -> i32;
    fn pm_metal_dev_stream_reg_load() -> i32;
    fn pm_metal_dev_input_kbd_reg_load() -> i32;
    fn pm_metal_dev_gfx_scanout_reg_load() -> i32;
    fn pm_metal_dev_gfx_text_reg_load() -> i32;
    fn pm_metal_dev_gfx_compositor_reg_load() -> i32;
    fn pm_metal_dev_net_virtio_net_reg_load() -> i32;
    fn pm_metal_dev_net_bge_reg_load() -> i32;
}

pub fn load_all() -> i32 {
    let mut rc = 0i32;
    unsafe {
        rc |= pm_metal_externals_reg_load();
        rc |= pm_metal_dev_serial_reg_load();
        rc |= pm_metal_dev_blk_reg_load();
        rc |= pm_metal_dev_acpi_reg_load();
        rc |= pm_metal_dev_stream_reg_load();
        rc |= pm_metal_dev_input_kbd_reg_load();
        rc |= pm_metal_dev_gfx_scanout_reg_load();
        rc |= pm_metal_dev_gfx_text_reg_load();
        rc |= pm_metal_dev_gfx_compositor_reg_load();
        rc |= pm_metal_dev_net_virtio_net_reg_load();
        rc |= pm_metal_dev_net_bge_reg_load();
    }
    rc
}
