//! Compile `port/mphalport.c` into the `pymergetic_metal_py` staticlib.
//!
//! Firmware (bios/efi) already gets `dev/stream/__init__.c` from forge's
//! own `PRODUCT_COMMON` unit table (`forge/_build.rs`) at the final `ld`
//! step, so this only bundles `mphalport.c` there -- adding
//! `dev/stream/__init__.c` a second time would duplicate-define its
//! symbols at link time. The host smoke binary is a standalone
//! executable with no forge C-unit step at all, so for a non-freestanding
//! target this also compiles `dev/stream/__init__.c` (its own transitive
//! calls into `pm_metal_mem_*`/`pm_metal_async_*` resolve against the
//! real Rust crate symbols already in `pymergetic_metal_py`'s dependency
//! graph, see `.pm/Cargo.toml`).
use std::env;
use std::path::PathBuf;

fn main() {
    // CARGO_MANIFEST_DIR = py/.pm/
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let mod_dir = manifest.join("..").canonicalize().expect("py module dir");
    // py/ -> metal/
    let metal_dir = mod_dir.join("..").canonicalize().expect("metal dir");
    // py/ -> metal/ -> pymergetic/ -> src/ -> packages/metal
    let pkg_root = mod_dir
        .join("../../../..")
        .canonicalize()
        .expect("package root");

    let target = env::var("TARGET").unwrap_or_default();
    let freestanding = target.contains("none") || target.contains("uefi");
    let uefi = target.contains("uefi");

    let port_dir = mod_dir.join("port");
    let libc_dir = metal_dir.join("libc");
    let stream_c = metal_dir.join("dev/stream/__init__.c");

    let mut build = cc::Build::new();
    build.file(port_dir.join("mphalport.c"));
    build.include(&port_dir);
    build.include(pkg_root.join("include"));
    build.include(pkg_root.join("src"));
    build.warnings(false);

    if !freestanding {
        build.file(&stream_c);
    }
    if freestanding {
        build.flag("-nostdinc");
        build.flag("-ffreestanding");
        build.include(&libc_dir);
    }
    if uefi {
        build.compiler("clang");
        build.flag("--target=x86_64-unknown-windows-gnu");
        build.flag("-fshort-wchar");
        build.flag("-mno-red-zone");
    }
    build.compile("metal_py_hal");

    println!(
        "cargo:rerun-if-changed={}",
        port_dir.join("mphalport.c").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        port_dir.join("mphalport.h").display()
    );
    println!("cargo:rerun-if-changed={}", stream_c.display());
}
