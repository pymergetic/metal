use std::env;
use std::path::PathBuf;

fn main() {
    // CARGO_MANIFEST_DIR = mem/.pm/
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let mod_dir = manifest.join("..").canonicalize().expect("mem module dir");
    // mem/ -> metal/ -> pymergetic/ -> src/ -> exp2/ -> packages/metal
    let metal_root = mod_dir
        .join("../../../../..")
        .canonicalize()
        .expect("metal root");
    let tlsf_dir = metal_root.join("external/tlsf");
    let tlsf_h = tlsf_dir.join("tlsf.h");
    if !tlsf_h.is_file() {
        panic!(
            "missing {}: run scripts/setup for tlsf (external/tlsf)",
            tlsf_h.display()
        );
    }

    let target = env::var("TARGET").unwrap_or_default();
    let freestanding = target.contains("none") || target.contains("uefi");
    let port_dir = mod_dir.join("tlsf");

    let mut build = cc::Build::new();
    build.file(port_dir.join("_port.c"));
    build.include(&tlsf_dir);
    build.include(&port_dir);
    build.warnings(false);
    if freestanding {
        build.define("PM_METAL_TLSF_FREESTANDING", None);
        build.flag("-nostdinc");
        build.flag("-ffreestanding");
        build.include(port_dir.join("_inc"));
    }
    build.compile("metal_mem_tlsf");

    println!("cargo:rerun-if-changed={}", port_dir.join("_port.c").display());
    println!("cargo:rerun-if-changed={}", port_dir.join("_shim.h").display());
    println!("cargo:rerun-if-changed={}", port_dir.join("_inc").display());
    println!("cargo:rerun-if-changed={}", tlsf_dir.join("tlsf.c").display());
    println!("cargo:rerun-if-changed={}", tlsf_h.display());
}
