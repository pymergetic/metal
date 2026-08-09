//! C floor RegMods — load hooks only (declares live in owning muscle TUs).
#![allow(non_upper_case_globals)]

extern "C" {
    fn pm_metal_auth_reg_load() -> i32;
    fn pm_metal_boot_mod_reg_load() -> i32;
    fn pm_metal_boot_tree_reg_load() -> i32;
    fn pm_metal_bus_pci_reg_load() -> i32;
    fn pm_metal_bus_virtio_reg_load() -> i32;
    fn pm_metal_draw_reg_load() -> i32;
    fn pm_metal_net_asgi_reg_load() -> i32;
    fn pm_metal_net_dhcp_reg_load() -> i32;
    fn pm_metal_net_dns_reg_load() -> i32;
    fn pm_metal_net_faces_reg_load() -> i32;
    fn pm_metal_net_http_reg_load() -> i32;
    fn pm_metal_net_ip_reg_load() -> i32;
    fn pm_metal_net_nic_reg_load() -> i32;
    fn pm_metal_net_ntp_reg_load() -> i32;
    fn pm_metal_net_pump_reg_load() -> i32;
    fn pm_metal_net_tftp_reg_load() -> i32;
    fn pm_metal_net_tls_reg_load() -> i32;
    fn pm_metal_net_wg_reg_load() -> i32;
    fn pm_metal_pack_reg_load() -> i32;
    fn pm_metal_shell_tui_reg_load() -> i32;
    fn pm_metal_shell_ui_reg_load() -> i32;
    fn pm_metal_shell_vt_reg_load() -> i32;
    fn pm_metal_trust_reg_load() -> i32;
    fn pm_metal_util_ascii_reg_load() -> i32;
    fn pm_metal_util_eightcc_reg_load() -> i32;
    fn pm_metal_util_endian_reg_load() -> i32;
    fn pm_metal_util_fourcc_reg_load() -> i32;
}

/// Load all C floor RegMods declared via `PM_METAL_REG_MOD*`.
pub fn load_all() -> i32 {
    let mut rc = 0i32;
    unsafe {
        rc |= pm_metal_boot_mod_reg_load();
        rc |= pm_metal_boot_tree_reg_load();
        rc |= pm_metal_draw_reg_load();
        rc |= pm_metal_net_ip_reg_load();
        rc |= pm_metal_net_http_reg_load();
        rc |= pm_metal_net_dhcp_reg_load();
        rc |= pm_metal_net_dns_reg_load();
        rc |= pm_metal_net_nic_reg_load();
        rc |= pm_metal_net_pump_reg_load();
        rc |= pm_metal_net_ntp_reg_load();
        rc |= pm_metal_net_tftp_reg_load();
        rc |= pm_metal_pack_reg_load();
        rc |= pm_metal_trust_reg_load();
        rc |= pm_metal_auth_reg_load();
        rc |= pm_metal_shell_vt_reg_load();
        rc |= pm_metal_net_tls_reg_load();
        rc |= pm_metal_net_wg_reg_load();
        rc |= pm_metal_net_asgi_reg_load();
        rc |= pm_metal_net_faces_reg_load();
        rc |= pm_metal_shell_tui_reg_load();
        rc |= pm_metal_shell_ui_reg_load();
        rc |= pm_metal_bus_pci_reg_load();
        rc |= pm_metal_bus_virtio_reg_load();
        rc |= pm_metal_util_ascii_reg_load();
        rc |= pm_metal_util_endian_reg_load();
        rc |= pm_metal_util_fourcc_reg_load();
        rc |= pm_metal_util_eightcc_reg_load();
    }
    rc
}
