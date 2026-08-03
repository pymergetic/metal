//! Compile HW scanout C ports + emit rustc-cfg from build/autoconf.h.
use std::env;
use std::path::{Path, PathBuf};

fn autoconf_enabled(root: &Path, name: &str) -> bool {
    let p = root.join("build/autoconf.h");
    let Ok(text) = std::fs::read_to_string(&p) else {
        /* No autoconf yet — keep QEMU + HW defaults on. */
        return true;
    };
    let needle = format!("#define {name} 1");
    text.lines().any(|l| l.trim() == needle)
}

fn main() {
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let mod_dir = manifest.join("..").canonicalize().expect("gfx module dir");
    let metal_dir = mod_dir.join("../..").canonicalize().expect("metal dir");
    /* gfx -> metal -> pymergetic -> src -> packages/metal */
    let package_root = mod_dir
        .join("../../../../..")
        .canonicalize()
        .expect("package root");
    let impl_dir = mod_dir.join("_impl");
    let libc_dir = metal_dir.join("libc");
    let target = env::var("TARGET").unwrap_or_default();
    let freestanding = target.contains("none") || target.contains("uefi");
    let uefi = target.contains("uefi");

    let virtio = autoconf_enabled(&package_root, "CONFIG_PM_METAL_GFX_SCANOUT_VIRTIO_GPU");
    let bochs = autoconf_enabled(&package_root, "CONFIG_PM_METAL_GFX_SCANOUT_BOCHS");
    let radeon = autoconf_enabled(&package_root, "CONFIG_PM_METAL_GFX_SCANOUT_RADEON");
    let i915 = autoconf_enabled(&package_root, "CONFIG_PM_METAL_GFX_SCANOUT_I915");
    let gop = autoconf_enabled(&package_root, "CONFIG_PM_METAL_GFX_SCANOUT_GOP");
    let lfb = autoconf_enabled(&package_root, "CONFIG_PM_METAL_GFX_SCANOUT_LFB");
    let ui = autoconf_enabled(&package_root, "CONFIG_PM_METAL_GFX_UI_BOOT_STRIPE");

    if virtio {
        println!("cargo:rustc-cfg=pm_gfx_virtio");
    }
    if bochs {
        println!("cargo:rustc-cfg=pm_gfx_bochs");
    }
    if radeon {
        println!("cargo:rustc-cfg=pm_gfx_radeon");
    }
    if i915 {
        println!("cargo:rustc-cfg=pm_gfx_i915");
    }
    if gop {
        println!("cargo:rustc-cfg=pm_gfx_gop");
    }
    if lfb {
        println!("cargo:rustc-cfg=pm_gfx_lfb");
    }
    if ui {
        println!("cargo:rustc-cfg=pm_gfx_ui");
    }

    println!(
        "cargo:rerun-if-changed={}",
        package_root.join("build/autoconf.h").display()
    );

    if !freestanding {
        return;
    }

    let i915_c = impl_dir.join("scanout_i915_855gm.c");
    let rado_c = impl_dir.join("scanout_radeon_rv370.c");
    let hw_c = impl_dir.join("_scanout_hw.c");
    let hw_h = impl_dir.join("_scanout_hw.h");
    let fw = mod_dir.join("fw/r300_cp.inc.c");

    let mut any_hw = false;
    let mut build = cc::Build::new();
    if i915 {
        build.file(&i915_c);
        any_hw = true;
        println!("cargo:rerun-if-changed={}", i915_c.display());
    }
    if radeon {
        build.file(&rado_c);
        any_hw = true;
        println!("cargo:rerun-if-changed={}", rado_c.display());
        println!("cargo:rerun-if-changed={}", fw.display());
    }
    if !any_hw {
        return;
    }

    build.file(&hw_c);
    println!("cargo:rerun-if-changed={}", hw_c.display());
    build.include(&impl_dir);
    build.include(package_root.join("src"));
    build.include(package_root.join("include"));
    build.include(&libc_dir);
    build.flag("-nostdinc");
    build.flag("-ffreestanding");
    build.flag("-fno-stack-protector");
    build.flag("-Wall");
    build.flag("-Wextra");
    build.flag("-Wno-unused-parameter");
    build.define("PM_METAL_GFX_HW_SCANOUT", None);
    if uefi {
        build.compiler("clang");
        build.flag("--target=x86_64-unknown-windows-gnu");
        build.flag("-fshort-wchar");
        build.flag("-mno-red-zone");
    } else {
        build.flag("-m64");
        build.flag("-mno-red-zone");
        build.flag("-fno-pic");
        build.flag("-fno-pie");
    }
    build.compile("metal_dev_gfx_hw");
    println!("cargo:rerun-if-changed={}", hw_h.display());
}
