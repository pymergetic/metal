//! Registry bootstrap — the loader's one call before the rest of bring-up.
//!
//! 1. Load the kernel namespace root (`pymergetic.metal`) via its spine
//!    fast-path face (must work before any registry entry exists).
//! 2. Load floor modules that publish into `RegMod` so always-proxy faces
//!    can resolve (console/log/ascii + detectors).
//! 3. `rt`'s dynamic-table register/connect for late/unloadable callers.
//! 4. `connect_all` so any `RegMod.imports` slots are filled (path-included
//!    face ImportRows also self-resolve via `resolve_import` on first use).

#[path = "../../../../../include/pymergetic/metal/__init__.rs"]
mod kernel_face;

extern "C" {
    fn pm_metal_console_mod_load() -> i32;
    fn pm_metal_log_mod_load() -> i32;
    fn pm_metal_shell_mod_load() -> i32;
    fn pm_metal_util_ascii_mod_load() -> i32;
    fn pm_metal_boot_externals_mod_load() -> i32;
    fn pm_metal_bus_pci_mod_load() -> i32;
    fn pm_metal_dev_time_mod_load() -> i32;
    fn pm_metal_dev_acpi_mod_load() -> i32;
    fn pm_metal_dev_random_mod_load() -> i32;
    fn pm_metal_dev_input_mod_load() -> i32;
    fn pm_metal_dev_gfx_mod_load() -> i32;
    fn pm_metal_dev_audio_mod_load() -> i32;
    fn pm_metal_py_bind_reg() -> i32;
    fn pm_metal_net_http_microdot_bind_reg() -> i32;
    fn pm_metal_rt_register_symbols() -> i32;
    fn pm_metal_rt_connect_symbols() -> i32;
    fn pm_metal_reg_mod_connect_all();
}

fn load_floor(load: unsafe extern "C" fn() -> i32) -> i32 {
    unsafe { load() }
}

/// Bootstrap the registry. Call once, before the rest of bring-up.
/// Returns 0, or -1 on failure.
pub unsafe fn reg_bootstrap() -> i32 {
    if kernel_face::pm_metal_kernel_load() != 0 {
        return -1;
    }
    /* Order: providers before consumers that call them via proxy faces. */
    if load_floor(pm_metal_console_mod_load) != 0 {
        return -1;
    }
    if load_floor(pm_metal_log_mod_load) != 0 {
        return -1;
    }
    if load_floor(pm_metal_shell_mod_load) != 0 {
        return -1;
    }
    if load_floor(pm_metal_util_ascii_mod_load) != 0 {
        return -1;
    }
    if load_floor(pm_metal_boot_externals_mod_load) != 0 {
        return -1;
    }
    if load_floor(pm_metal_bus_pci_mod_load) != 0 {
        return -1;
    }
    if load_floor(pm_metal_dev_time_mod_load) != 0 {
        return -1;
    }
    if load_floor(pm_metal_dev_acpi_mod_load) != 0 {
        return -1;
    }
    if load_floor(pm_metal_dev_random_mod_load) != 0 {
        return -1;
    }
    if load_floor(pm_metal_dev_input_mod_load) != 0 {
        return -1;
    }
    if load_floor(pm_metal_dev_gfx_mod_load) != 0 {
        return -1;
    }
    if load_floor(pm_metal_dev_audio_mod_load) != 0 {
        return -1;
    }
    /* Product edge: py publishes onto the dynamic/late table today
     * (RegMod conversion of the full py catalog is W11). */
    if pm_metal_py_bind_reg() != 0 {
        return -1;
    }
    /* Microdot C ABI -> reg so REPL `import ...microdot` gets real natives. */
    if pm_metal_net_http_microdot_bind_reg() != 0 {
        return -1;
    }
    if pm_metal_rt_register_symbols() != 0 {
        return -1;
    }
    if pm_metal_rt_connect_symbols() != 0 {
        return -1;
    }
    pm_metal_reg_mod_connect_all();
    0
}
