use std::env;
use std::path::PathBuf;

fn main() {
    // CARGO_MANIFEST_DIR = mem/.pm/
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let mod_dir = manifest.join("..").canonicalize().expect("mem module dir");
    // mem/ -> metal/
    let metal_dir = mod_dir.join("..").canonicalize().expect("metal dir");
    // metal/ -> pymergetic/ -> src/ -> packages/metal
    let metal_root = mod_dir
        .join("../../../..")
        .canonicalize()
        .expect("metal root");
    let tlsf_dir = metal_root.join("external/tlsf");
    let tlsf_h = tlsf_dir.join("tlsf.h");
    if !tlsf_h.is_file() {
        panic!(
            "missing {}: git submodule update --init external/tlsf (external/tlsf)",
            tlsf_h.display()
        );
    }

    let target = env::var("TARGET").unwrap_or_default();
    let freestanding = target.contains("none") || target.contains("uefi");
    let uefi = target.contains("uefi");
    let port_dir = mod_dir.join("tlsf");
    let libc_dir = metal_dir.join("libc");

    let mut build = cc::Build::new();
    build.file(port_dir.join("_port.c"));
    build.include(&tlsf_dir);
    build.include(&port_dir);
    build.warnings(false);
    if freestanding {
        build.define("PM_METAL_TLSF_FREESTANDING", None);
        build.flag("-nostdinc");
        build.flag("-ffreestanding");
        /* Shared firmware freestanding ISO C — metal/libc (not guest-facing). */
        build.include(&libc_dir);
        build.file(libc_dir.join("string.c"));
        build.file(libc_dir.join("stdlib.c"));
        build.file(libc_dir.join("stdio.c"));
    }
    /* UEFI rustc objects are COFF; force clang's windows-gnu COFF (not host ELF, not MSVC). */
    if uefi {
        build.compiler("clang");
        build.flag("--target=x86_64-unknown-windows-gnu");
        build.flag("-fshort-wchar");
        build.flag("-mno-red-zone");
    }
    build.compile("metal_mem_tlsf");

    println!("cargo:rerun-if-changed={}", port_dir.join("_port.c").display());
    println!("cargo:rerun-if-changed={}", port_dir.join("_shim.h").display());
    println!("cargo:rerun-if-changed={}", libc_dir.join("string.c").display());
    println!("cargo:rerun-if-changed={}", libc_dir.join("stdlib.c").display());
    println!("cargo:rerun-if-changed={}", libc_dir.join("stdio.c").display());
    println!("cargo:rerun-if-changed={}", libc_dir.display());
    println!("cargo:rerun-if-changed={}", tlsf_dir.join("tlsf.c").display());
    println!("cargo:rerun-if-changed={}", tlsf_h.display());
}
