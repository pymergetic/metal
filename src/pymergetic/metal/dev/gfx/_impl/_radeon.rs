//! Thin Rust Ops glue for radeon RV370 C port (PCI 1002:5460).

use crate::scanout::{Bind, Ops};

#[cfg(all(pm_gfx_radeon, any(target_os = "none", target_os = "uefi")))]
extern "C" {
    fn pm_metal_dev_gfx_scanout_radeon_probe(b: *const Bind) -> i32;
    fn pm_metal_dev_gfx_scanout_radeon_present_rect(x: i32, y: i32, w: i32, h: i32) -> i32;
    fn pm_metal_dev_gfx_scanout_radeon_job_begin(x: i32, y: i32, w: i32, h: i32) -> i32;
    fn pm_metal_dev_gfx_scanout_radeon_job_step() -> i32;
    fn pm_metal_dev_gfx_scanout_radeon_caps() -> u32;
    fn pm_metal_dev_gfx_scanout_radeon_fini();
}

fn probe(b: &Bind) -> i32 {
    #[cfg(all(pm_gfx_radeon, any(target_os = "none", target_os = "uefi")))]
    unsafe {
        return pm_metal_dev_gfx_scanout_radeon_probe(b as *const Bind);
    }
    #[cfg(not(all(pm_gfx_radeon, any(target_os = "none", target_os = "uefi"))))]
    {
        let _ = b;
        -1
    }
}

fn present_rect(x: i32, y: i32, w: i32, h: i32) -> i32 {
    #[cfg(all(pm_gfx_radeon, any(target_os = "none", target_os = "uefi")))]
    unsafe {
        return pm_metal_dev_gfx_scanout_radeon_present_rect(x, y, w, h);
    }
    #[cfg(not(all(pm_gfx_radeon, any(target_os = "none", target_os = "uefi"))))]
    {
        let _ = (x, y, w, h);
        -1
    }
}

fn job_begin(x: i32, y: i32, w: i32, h: i32) -> i32 {
    #[cfg(all(pm_gfx_radeon, any(target_os = "none", target_os = "uefi")))]
    unsafe {
        return pm_metal_dev_gfx_scanout_radeon_job_begin(x, y, w, h);
    }
    #[cfg(not(all(pm_gfx_radeon, any(target_os = "none", target_os = "uefi"))))]
    {
        let _ = (x, y, w, h);
        -1
    }
}

fn job_step() -> i32 {
    #[cfg(all(pm_gfx_radeon, any(target_os = "none", target_os = "uefi")))]
    unsafe {
        return pm_metal_dev_gfx_scanout_radeon_job_step();
    }
    #[cfg(not(all(pm_gfx_radeon, any(target_os = "none", target_os = "uefi"))))]
    {
        0
    }
}

fn caps() -> u32 {
    #[cfg(all(pm_gfx_radeon, any(target_os = "none", target_os = "uefi")))]
    unsafe {
        return pm_metal_dev_gfx_scanout_radeon_caps();
    }
    #[cfg(not(all(pm_gfx_radeon, any(target_os = "none", target_os = "uefi"))))]
    {
        0
    }
}

fn fini() {
    #[cfg(all(pm_gfx_radeon, any(target_os = "none", target_os = "uefi")))]
    unsafe {
        pm_metal_dev_gfx_scanout_radeon_fini();
    }
}

pub static OPS: Ops = Ops {
    name: "radeon_rv370",
    probe,
    present_rect,
    job_begin,
    job_step,
    caps,
    adopt_shadow: None,
    after_flip: None,
    fini,
};
