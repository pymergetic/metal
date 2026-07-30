//! Compile vendor/lfs.c + _glue.c with clang (no crates.io `cc`).
use std::env;
use std::path::{Path, PathBuf};
use std::process::Command;

fn main() {
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let mod_dir = manifest.join("..");
    let vendor = mod_dir.join("vendor");
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let target = env::var("TARGET").unwrap_or_default();
    let freestanding = target.contains("none") || target.contains("uefi");
    let uefi = target.contains("uefi");

    let metal_dir = mod_dir
        .join("../..")
        .canonicalize()
        .expect("metal dir");
    let libc_dir = metal_dir.join("libc");

    let lfs_c = vendor.join("lfs.c");
    let glue_c = mod_dir.join("_glue.c");
    let cfg_h = mod_dir.join("lfs_config.h");

    let lfs_o = out_dir.join("lfs.o");
    let glue_o = out_dir.join("glue.o");
    let lib = out_dir.join("libmetal_fs_littlefs.a");

    compile_c(
        &lfs_c,
        &lfs_o,
        &vendor,
        &mod_dir,
        &libc_dir,
        freestanding,
        uefi,
        false,
    );
    compile_c(
        &glue_c,
        &glue_o,
        &vendor,
        &mod_dir,
        &libc_dir,
        freestanding,
        uefi,
        false,
    );

    let mut objs = vec![lfs_o, glue_o];
    if freestanding {
        let string_c = libc_dir.join("string.c");
        let string_o = out_dir.join("string.o");
        compile_c(
            &string_c,
            &string_o,
            &vendor,
            &mod_dir,
            &libc_dir,
            freestanding,
            uefi,
            true,
        );
        objs.push(string_o);
        println!("cargo:rerun-if-changed={}", string_c.display());
    }

    let ar = env::var("AR").unwrap_or_else(|_| String::from("ar"));
    let mut ar_cmd = Command::new(&ar);
    ar_cmd.arg("rcs").arg(&lib);
    for o in &objs {
        ar_cmd.arg(o);
    }
    let status = ar_cmd
        .status()
        .unwrap_or_else(|e| panic!("ar failed: {e}"));
    if !status.success() {
        panic!("ar rcs failed");
    }

    println!("cargo:rustc-link-search=native={}", out_dir.display());
    println!("cargo:rustc-link-lib=static=metal_fs_littlefs");
    println!("cargo:rerun-if-changed={}", lfs_c.display());
    println!("cargo:rerun-if-changed={}", glue_c.display());
    println!("cargo:rerun-if-changed={}", cfg_h.display());
    println!("cargo:rerun-if-changed={}", vendor.join("lfs.h").display());
    println!("cargo:rerun-if-changed={}", vendor.join("lfs_util.h").display());
}

fn compile_c(
    src: &Path,
    obj: &Path,
    vendor: &Path,
    mod_dir: &Path,
    libc_dir: &Path,
    freestanding: bool,
    uefi: bool,
    libc_only: bool,
) {
    let mut cmd = Command::new("clang");
    cmd.arg("-c")
        .arg(src)
        .arg("-o")
        .arg(obj)
        .arg("-Wall")
        .arg("-Wno-unused-parameter")
        .arg("-ffunction-sections")
        .arg("-fdata-sections");

    if !libc_only {
        cmd.arg("-I")
            .arg(vendor)
            .arg("-I")
            .arg(mod_dir)
            .arg("-DLFS_CONFIG=lfs_config.h");
    }

    if freestanding {
        if !libc_only {
            cmd.arg("-DPM_METAL_LFS_FREESTANDING");
        }
        cmd.arg("-nostdinc")
            .arg("-ffreestanding")
            .arg("-I")
            .arg(libc_dir);
    }
    if uefi {
        cmd.arg("--target=x86_64-unknown-windows-gnu")
            .arg("-fshort-wchar")
            .arg("-mno-red-zone");
    }

    let status = cmd
        .status()
        .unwrap_or_else(|e| panic!("clang failed on {}: {e}", src.display()));
    if !status.success() {
        panic!("clang compile failed: {}", src.display());
    }
}
